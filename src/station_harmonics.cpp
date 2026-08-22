#include "station_harmonics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "environmental_grib/error.h"
#include "environmental_grib/tpxo.h"

namespace xtidal::authoring {
namespace {
constexpr double kPi = 3.14159265358979323846;

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

class Tokens {
public:
  Tokens(const std::vector<std::string>& lines, std::size_t start) {
    for (std::size_t index = start; index < lines.size(); ++index) {
      auto line = lines[index];
      const auto comment = line.find('#');
      if (comment != std::string::npos) line.resize(comment);
      std::istringstream input(line);
      for (std::string token; input >> token;) values_.push_back(token);
    }
  }
  std::string Next() {
    if (position_ >= values_.size())
      throw std::runtime_error("unexpected end of harmonic header");
    return values_[position_++];
  }
  int NextInt() { return std::stoi(Next()); }
  double NextDouble() { return std::stod(Next()); }

private:
  std::vector<std::string> values_;
  std::size_t position_{};
};

struct Header {
  int first_year{};
  int years{};
  std::vector<std::string> names;
  std::vector<double> speed_rad_s;
  std::vector<std::vector<double>> equilibrium_rad;
  std::vector<std::vector<double>> node;
};

struct SourceStation {
  std::string name;
  int meridian_seconds{};
  double datum{};
  std::string units;
  std::vector<double> amplitude;
  std::vector<double> phase_rad;
};

Header ParseHeader(const std::vector<std::string>& lines) {
  const auto marker =
      std::find_if(lines.begin(), lines.end(), [](const auto& l) {
        return l.find("Begin congen output") != std::string::npos;
      });
  if (marker == lines.end())
    throw std::runtime_error("harmonic header marker not found");
  Tokens input(lines, static_cast<std::size_t>(marker - lines.begin() + 1));
  const int count = input.NextInt();
  Header result;
  result.names.resize(count);
  result.speed_rad_s.resize(count);
  for (int index = 0; index < count; ++index) {
    result.names[index] = input.Next();
    result.speed_rad_s[index] = input.NextDouble() * kPi / (180.0 * 3600.0);
  }
  result.first_year = input.NextInt();
  result.years = input.NextInt();
  result.equilibrium_rad.assign(count, std::vector<double>(result.years));
  for (int constituent = 0; constituent < count; ++constituent) {
    (void)input.Next();
    for (int year = 0; year < result.years; ++year)
      result.equilibrium_rad[constituent][year] =
          input.NextDouble() * kPi / 180.0;
  }
  if (input.Next() != "*END*")
    throw std::runtime_error("equilibrium table terminator is missing");
  if (input.NextInt() != result.years)
    throw std::runtime_error("equilibrium/node year counts differ");
  result.node.assign(count, std::vector<double>(result.years));
  for (int constituent = 0; constituent < count; ++constituent) {
    (void)input.Next();
    for (int year = 0; year < result.years; ++year)
      result.node[constituent][year] = input.NextDouble();
  }
  return result;
}

std::string NextDataLine(const std::vector<std::string>& lines,
                         std::size_t& position) {
  while (++position < lines.size()) {
    const auto line = Trim(lines[position]);
    if (!line.empty() && line.front() != '#') return line;
  }
  throw std::runtime_error("unexpected end of station record");
}

int ParseMeridianSeconds(const std::string& value) {
  int hours = 0, minutes = 0;
  char separator = 0;
  std::istringstream input(value);
  input >> hours >> separator >> minutes;
  if (!input || separator != ':')
    throw std::runtime_error("invalid station meridian");
  if (hours < 0) minutes = -minutes;
  return hours * 3600 + minutes * 60;
}

SourceStation ParseStation(const std::vector<std::string>& lines,
                           const Header& header, const std::string& prefix) {
  for (std::size_t position = 0; position < lines.size(); ++position) {
    const auto candidate = Trim(lines[position]);
    if (candidate.empty() || candidate.front() == '#' ||
        candidate.rfind(prefix, 0) != 0)
      continue;
    SourceStation station;
    station.name = candidate;
    const auto meridian_line = NextDataLine(lines, position);
    station.meridian_seconds =
        ParseMeridianSeconds(meridian_line.substr(0, meridian_line.find(' ')));
    std::istringstream datum_line(NextDataLine(lines, position));
    datum_line >> station.datum >> station.units;
    if (!datum_line) throw std::runtime_error("invalid station datum line");
    station.amplitude.resize(header.names.size());
    station.phase_rad.resize(header.names.size());
    for (std::size_t index = 0; index < header.names.size(); ++index) {
      std::string name;
      double amplitude = 0.0, phase = 0.0;
      std::istringstream constituent(NextDataLine(lines, position));
      constituent >> name >> amplitude >> phase;
      if (!constituent)
        throw std::runtime_error("invalid station constituent line");
      station.amplitude[index] = amplitude;
      station.phase_rad[index] = phase * kPi / 180.0;
    }
    return station;
  }
  throw std::runtime_error("station not found: " + prefix);
}

environmental_grib::TimePoint YearStart(int year) {
  std::tm value{};
  value.tm_year = year - 1900;
  value.tm_mon = 0;
  value.tm_mday = 1;
  return environmental_grib::TimePoint{std::chrono::seconds{timegm(&value)}};
}
}  // namespace

StationHarmonicConstants LoadStationHarmonicConstants(
    const std::filesystem::path& source_path, const std::string& station_prefix,
    int calibration_year) {
  std::ifstream source(source_path);
  if (!source) throw std::runtime_error("could not open harmonic source");
  std::vector<std::string> lines;
  for (std::string line; std::getline(source, line);) lines.push_back(line);
  const auto header = ParseHeader(lines);
  auto station = ParseStation(lines, header, station_prefix);
  double unit_scale = 1.0;
  if (station.units == "feet") {
    unit_scale = 0.3048;
  } else if (station.units != "meters") {
    throw std::runtime_error("unsupported station harmonic height unit: " +
                             station.units);
  }
  station.datum *= unit_scale;
  for (auto& amplitude : station.amplitude) amplitude *= unit_scale;
  const int year_index = calibration_year - header.first_year;
  if (year_index < 0 || year_index >= header.years)
    throw std::runtime_error("calibration year is outside harmonic tables");

  const auto year_start = YearStart(calibration_year);
  const auto calibration_time = year_start + std::chrono::hours{24 * 14};
  const double seconds =
      std::chrono::duration<double>(calibration_time - year_start).count();
  StationHarmonicConstants result;
  result.name = station.name;
  result.chart_datum_reference_m = station.datum;
  for (std::size_t index = 0; index < header.names.size(); ++index) {
    const auto name = Lower(header.names[index]);
    try {
      const auto real_basis = environmental_grib::PredictAtlasHarmonicGrid(
          {name}, {std::complex<double>{1.0, 0.0}}, 1, {calibration_time},
          false)[0];
      const auto imaginary_basis = environmental_grib::PredictAtlasHarmonicGrid(
          {name}, {std::complex<double>{0.0, 1.0}}, 1, {calibration_time},
          false)[0];
      const double factor = std::hypot(real_basis, imaginary_basis);
      if (factor <= 0.0) continue;
      const double generic_phase = std::atan2(-imaginary_basis, real_basis);
      const double source_phase =
          header.speed_rad_s[index] * (seconds + station.meridian_seconds) +
          header.equilibrium_rad[index][year_index] - station.phase_rad[index];
      const double amplitude =
          station.amplitude[index] * header.node[index][year_index] / factor;
      const double difference = source_phase - generic_phase;
      result.coefficients_m[name] = {amplitude * std::cos(difference),
                                     amplitude * std::sin(difference)};
    } catch (const environmental_grib::ValidationError&) {
      // The independent source curve can contain constituents which the
      // runtime does not support; they are deliberately not fabricated.
    }
  }
  if (result.coefficients_m.empty())
    throw std::runtime_error("station has no supported harmonic constants");
  return result;
}

}  // namespace xtidal::authoring
