#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace xtidal {

// Private mirror of the versioned POD bridge. Keeping this out of the pinned
// API 1.21 header lets one OfflineTides binary load on both stock and enhanced
// OpenCPN hosts; every call is resolved at runtime and size-checked.
struct HostTideStationInfoV1 {
  std::uint32_t struct_size{};
  std::uint32_t api_version{};
  int index{};
  double lat{};
  double lon{};
  double distance_nm{};
  int subordinate{};
  int datum_status{};
  int datum_approximate{};
  int height_in_metres_available{};
  double datum_offset_m{};
  char name[90]{};
  char stable_id[256]{};
  char reference_name[90]{};
  char station_id_context[90]{};
  char station_id[90]{};
  char source_dataset_id[256]{};
  char source_dataset_name[90]{};
  char source_dataset_version[180]{};
  char source_description[180]{};
  char datum_name[90]{};
  char datum_equivalence_key[32]{};
  char level_units[40]{};
};

class NativeTideHost final {
public:
  bool Resolve();
  bool available() const {
    return nearest_ && station_info_ && height_metres_ && maximum_index_;
  }
  std::vector<HostTideStationInfoV1> Nearest(double latitude, double longitude,
                                             double maximum_distance_nm,
                                             int maximum_results) const;
  std::optional<double> HeightMetres(int station_index, std::time_t time) const;
  std::vector<HostTideStationInfoV1> Enumerate() const;

private:
  using MaximumIndexFn = int (*)();
  using StationInfoFn = bool (*)(int, HostTideStationInfoV1*);
  using NearestFn = int (*)(double, double, double, HostTideStationInfoV1*,
                            int);
  using HeightMetresFn = bool (*)(int, std::time_t, double*);

  MaximumIndexFn maximum_index_{};
  StationInfoFn station_info_{};
  NearestFn nearest_{};
  HeightMetresFn height_metres_{};
};

}  // namespace xtidal
