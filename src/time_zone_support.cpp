#include "time_zone_support.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace xtidal::time_zone {
namespace {

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
#define OFFLINETIDES_HAS_CHRONO_TZDB 1
#else
#define OFFLINETIDES_HAS_CHRONO_TZDB 0
#endif

struct TzifType {
  std::int32_t utc_offset{};
  bool daylight{};
  std::string abbreviation;
};

struct TzifZone {
  std::vector<std::int64_t> transitions;
  std::vector<std::uint8_t> transition_types;
  std::vector<TzifType> types;
};

struct TzifCounts {
  std::uint32_t utc_indicators{};
  std::uint32_t standard_indicators{};
  std::uint32_t leap_seconds{};
  std::uint32_t transitions{};
  std::uint32_t types{};
  std::uint32_t abbreviation_bytes{};
};

std::uint32_t ReadBig32(const std::vector<unsigned char>& bytes,
                        std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::int64_t ReadSignedTime(const std::vector<unsigned char>& bytes,
                            std::size_t offset, std::size_t width) {
  std::uint64_t value{};
  for (std::size_t i = 0; i < width; ++i)
    value = (value << 8) | bytes[offset + i];
  if (width == 4)
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
  if ((value & (std::uint64_t{1} << 63)) == 0)
    return static_cast<std::int64_t>(value);
  const std::uint64_t magnitude = (~value) + 1;
  if (magnitude == (std::uint64_t{1} << 63))
    return std::numeric_limits<std::int64_t>::min();
  return -static_cast<std::int64_t>(magnitude);
}

bool ReadHeader(const std::vector<unsigned char>& bytes, std::size_t offset,
                char& version, TzifCounts& counts) {
  if (offset + 44 > bytes.size() || bytes[offset] != 'T' ||
      bytes[offset + 1] != 'Z' || bytes[offset + 2] != 'i' ||
      bytes[offset + 3] != 'f')
    return false;
  version = static_cast<char>(bytes[offset + 4]);
  counts.utc_indicators = ReadBig32(bytes, offset + 20);
  counts.standard_indicators = ReadBig32(bytes, offset + 24);
  counts.leap_seconds = ReadBig32(bytes, offset + 28);
  counts.transitions = ReadBig32(bytes, offset + 32);
  counts.types = ReadBig32(bytes, offset + 36);
  counts.abbreviation_bytes = ReadBig32(bytes, offset + 40);
  return counts.types > 0 && counts.types <= 256;
}

bool AddChecked(std::size_t& value, std::uint64_t amount, std::size_t limit) {
  if (amount > limit || value > limit - static_cast<std::size_t>(amount))
    return false;
  value += static_cast<std::size_t>(amount);
  return true;
}

bool SkipBlock(const std::vector<unsigned char>& bytes, std::size_t& offset,
               const TzifCounts& counts, std::size_t time_width) {
  const std::uint64_t size =
      static_cast<std::uint64_t>(counts.transitions) * time_width +
      counts.transitions + static_cast<std::uint64_t>(counts.types) * 6 +
      counts.abbreviation_bytes +
      static_cast<std::uint64_t>(counts.leap_seconds) * (time_width + 4) +
      counts.standard_indicators + counts.utc_indicators;
  return AddChecked(offset, size, bytes.size());
}

std::shared_ptr<const TzifZone> ParseTzif(
    const std::vector<unsigned char>& bytes) {
  char version{};
  TzifCounts counts;
  if (!ReadHeader(bytes, 0, version, counts)) return {};
  std::size_t offset = 44;
  std::size_t time_width = 4;
  if (version == '2' || version == '3' || version == '4') {
    if (!SkipBlock(bytes, offset, counts, 4) ||
        !ReadHeader(bytes, offset, version, counts))
      return {};
    offset += 44;
    time_width = 8;
  }

  const std::uint64_t required =
      static_cast<std::uint64_t>(counts.transitions) * time_width +
      counts.transitions + static_cast<std::uint64_t>(counts.types) * 6 +
      counts.abbreviation_bytes;
  if (required > bytes.size() || offset > bytes.size() - required) return {};

  auto zone = std::make_shared<TzifZone>();
  zone->transitions.reserve(counts.transitions);
  for (std::uint32_t i = 0; i < counts.transitions; ++i) {
    zone->transitions.push_back(ReadSignedTime(bytes, offset, time_width));
    offset += time_width;
  }
  zone->transition_types.assign(bytes.begin() + offset,
                                bytes.begin() + offset + counts.transitions);
  offset += counts.transitions;

  struct RawType {
    std::int32_t offset;
    bool daylight;
    std::uint8_t abbreviation;
  };
  std::vector<RawType> raw_types;
  raw_types.reserve(counts.types);
  for (std::uint32_t i = 0; i < counts.types; ++i) {
    raw_types.push_back({static_cast<std::int32_t>(ReadBig32(bytes, offset)),
                         bytes[offset + 4] != 0, bytes[offset + 5]});
    offset += 6;
  }
  const std::string abbreviations(
      reinterpret_cast<const char*>(bytes.data() + offset),
      counts.abbreviation_bytes);
  for (const auto& raw : raw_types) {
    if (raw.abbreviation >= abbreviations.size()) return {};
    const std::size_t end = abbreviations.find('\0', raw.abbreviation);
    zone->types.push_back(
        {raw.offset, raw.daylight,
         abbreviations.substr(raw.abbreviation, end - raw.abbreviation)});
  }
  for (const std::uint8_t type : zone->transition_types)
    if (type >= zone->types.size()) return {};
  return zone;
}

const std::vector<std::filesystem::path>& ZoneInfoRoots() {
  static const std::vector<std::filesystem::path> roots = [] {
    std::vector<std::filesystem::path> result;
    if (const char* configured = std::getenv("OCPN_TIMEZONE_DIR"))
      if (*configured) result.emplace_back(configured);
#ifndef _WIN32
    result.emplace_back("/usr/share/zoneinfo");
    result.emplace_back("/usr/share/lib/zoneinfo");
    result.emplace_back("/usr/lib/zoneinfo");
#endif
    return result;
  }();
  return roots;
}

bool SafeZoneName(const std::string& name) {
  if (name.empty() || name.front() == '/' ||
      name.find('\\') != std::string::npos)
    return false;
  for (const auto& part : std::filesystem::path{name})
    if (part == "." || part == "..") return false;
  return true;
}

std::filesystem::path FindZoneFile(const std::string& name) {
  if (!SafeZoneName(name)) return {};
  std::error_code error;
  for (const auto& root : ZoneInfoRoots()) {
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) {
      error.clear();
      continue;
    }
    const auto candidate =
        std::filesystem::weakly_canonical(canonical_root / name, error);
    if (error) {
      error.clear();
      continue;
    }
    const auto relative =
        std::filesystem::relative(candidate, canonical_root, error);
    if (error || relative.empty() || *relative.begin() == "..") {
      error.clear();
      continue;
    }
    if (std::filesystem::is_regular_file(candidate, error) && !error)
      return candidate;
    error.clear();
  }
  return {};
}

std::shared_ptr<const TzifZone> LoadTzif(const std::string& name) {
  static std::mutex mutex;
  static std::map<std::string, std::shared_ptr<const TzifZone>> cache;
  {
    const std::lock_guard lock(mutex);
    const auto found = cache.find(name);
    if (found != cache.end()) return found->second;
  }
  std::shared_ptr<const TzifZone> parsed;
  const auto path = FindZoneFile(name);
  if (!path.empty()) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
    parsed = ParseTzif(bytes);
  }
  const std::lock_guard lock(mutex);
  return cache.emplace(name, std::move(parsed)).first->second;
}

const TzifType* TypeAt(const TzifZone& zone, std::int64_t utc_seconds) {
  if (zone.types.empty()) return nullptr;
  const auto after = std::upper_bound(zone.transitions.begin(),
                                      zone.transitions.end(), utc_seconds);
  if (after != zone.transitions.begin()) {
    const auto index =
        static_cast<std::size_t>(after - zone.transitions.begin() - 1);
    return &zone.types[zone.transition_types[index]];
  }
  for (const auto& type : zone.types)
    if (!type.daylight) return &type;
  return &zone.types.front();
}

std::string SystemZone() {
#if OFFLINETIDES_HAS_CHRONO_TZDB
  try {
    return std::string{std::chrono::current_zone()->name()};
  } catch (const std::runtime_error&) {
  }
#endif
  if (const char* environment = std::getenv("TZ")) {
    std::string name{environment};
    if (!name.empty() && name.front() == ':') name.erase(0, 1);
    if (LoadTzif(name)) return name;
  }
#ifndef _WIN32
  std::error_code error;
  const auto local =
      std::filesystem::weakly_canonical("/etc/localtime", error).string();
  const std::string marker = "/zoneinfo/";
  const auto marker_at = local.find(marker);
  if (!error && marker_at != std::string::npos) {
    const auto name = local.substr(marker_at + marker.size());
    if (LoadTzif(name)) return name;
  }
  std::ifstream timezone("/etc/timezone");
  std::string name;
  if (std::getline(timezone, name) && LoadTzif(name)) return name;
#endif
  return "UTC";
}

std::string ResolveName(const std::string& requested) {
  return requested == "system-local" ? SystemZone() : requested;
}

std::string CanonicalAbbreviation(const std::string& zone_name,
                                  std::chrono::seconds offset,
                                  std::string abbreviation) {
  static const std::set<std::string> british_zones = {"Europe/London",
                                                      "Europe/Guernsey",
                                                      "Europe/Isle_of_Man",
                                                      "Europe/Jersey",
                                                      "GB",
                                                      "GB-Eire"};
  if (british_zones.contains(zone_name)) {
    if (offset == std::chrono::hours{0}) return "GMT";
    if (offset == std::chrono::hours{1}) return "BST";
  }
  if (zone_name == "Europe/Dublin" || zone_name == "Eire") {
    if (offset == std::chrono::hours{0}) return "GMT";
    if (offset == std::chrono::hours{1}) return "IST";
  }
  return abbreviation;
}

#if OFFLINETIDES_HAS_CHRONO_TZDB
const std::chrono::time_zone* Locate(const std::string& name) {
  try {
    return std::chrono::locate_zone(name);
  } catch (const std::runtime_error&) {
    return nullptr;
  }
}
#endif

}  // namespace

bool IsAvailable(const std::string& requested_name) {
  const auto name = ResolveName(requested_name);
  if (name == "UTC") return true;
#if OFFLINETIDES_HAS_CHRONO_TZDB
  if (Locate(name)) return true;
#endif
  return LoadTzif(name) != nullptr;
}

LocalDisplay ToLocal(TimePoint time, const std::string& requested_name) {
  const auto name = ResolveName(requested_name);
  if (name == "UTC")
    return {std::chrono::local_seconds{time.time_since_epoch()}, 0, "UTC"};
#if OFFLINETIDES_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(name)) {
    const auto info = zone->get_info(time);
    const auto offset =
        std::chrono::duration_cast<std::chrono::seconds>(info.offset);
    return {
        std::chrono::floor<std::chrono::seconds>(zone->to_local(time)),
        static_cast<int>(
            std::chrono::duration_cast<std::chrono::minutes>(offset).count()),
        CanonicalAbbreviation(name, offset, info.abbrev)};
  }
#endif
  if (const auto zone = LoadTzif(name)) {
    const auto seconds = time.time_since_epoch().count();
    if (const auto* type = TypeAt(*zone, seconds)) {
      const std::chrono::seconds offset{type->utc_offset};
      return {std::chrono::local_seconds{
                  std::chrono::seconds{seconds + type->utc_offset}},
              type->utc_offset / 60,
              CanonicalAbbreviation(name, offset, type->abbreviation)};
    }
  }
  throw std::invalid_argument("unknown display time zone '" + requested_name +
                              "'");
}

UtcConversion ToUtc(int year, unsigned month, unsigned day, unsigned hour,
                    unsigned minute, unsigned second,
                    const std::string& requested_name) {
  using namespace std::chrono;
  const year_month_day date{std::chrono::year{year}, std::chrono::month{month},
                            std::chrono::day{day}};
  UtcConversion result;
  if (!date.ok() || hour > 23 || minute > 59 || second > 59) return result;
  const local_seconds wall{local_days{date}.time_since_epoch() + hours{hour} +
                           minutes{minute} + seconds{second}};
  const auto name = ResolveName(requested_name);
  if (name == "UTC") {
    result.time = TimePoint{wall.time_since_epoch()};
    result.status = LocalStatus::kValid;
    return result;
  }
#if OFFLINETIDES_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(name)) {
    const auto info = zone->get_info(wall);
    if (info.result == local_info::nonexistent) {
      result.status = LocalStatus::kNonexistent;
      return result;
    }
    result.time = floor<seconds>(zone->to_sys(wall, choose::earliest));
    result.status = info.result == local_info::ambiguous
                        ? LocalStatus::kAmbiguous
                        : LocalStatus::kValid;
    return result;
  }
#endif
  if (const auto zone = LoadTzif(name)) {
    const auto naive = duration_cast<seconds>(wall.time_since_epoch()).count();
    std::set<std::int64_t> candidates;
    for (const auto& possible : zone->types) {
      const std::int64_t candidate = naive - possible.utc_offset;
      const auto* actual = TypeAt(*zone, candidate);
      if (actual && candidate + actual->utc_offset == naive)
        candidates.insert(candidate);
    }
    if (candidates.empty()) {
      result.status = LocalStatus::kNonexistent;
      return result;
    }
    result.time = TimePoint{seconds{*candidates.begin()}};
    result.status =
        candidates.size() > 1 ? LocalStatus::kAmbiguous : LocalStatus::kValid;
    return result;
  }
  return result;
}

}  // namespace xtidal::time_zone
