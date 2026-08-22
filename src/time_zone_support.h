#pragma once

#include <chrono>
#include <string>

namespace xtidal::time_zone {

using TimePoint = std::chrono::sys_seconds;

struct LocalDisplay {
  std::chrono::local_seconds time;
  int utc_offset_minutes{};
  std::string abbreviation;
};

enum class LocalStatus { kValid, kAmbiguous, kNonexistent, kInvalid };

struct UtcConversion {
  TimePoint time;
  LocalStatus status{LocalStatus::kInvalid};
};

// Resolve one UTC instant without changing the process-wide TZ environment.
// `system-local` uses the host's named IANA zone. Unknown zones throw.
LocalDisplay ToLocal(TimePoint time, const std::string& zone_name);

// Convert local calendar fields to UTC. Ambiguous values select the earliest
// occurrence; nonexistent values remain explicit and have no usable instant.
UtcConversion ToUtc(int year, unsigned month, unsigned day, unsigned hour,
                    unsigned minute, unsigned second,
                    const std::string& zone_name);

bool IsAvailable(const std::string& zone_name);

}  // namespace xtidal::time_zone
