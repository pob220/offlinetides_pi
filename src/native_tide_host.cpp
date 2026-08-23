#include "native_tide_host.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xtidal {
namespace {

void* ResolveProcessSymbol(const char* name) {
#ifdef _WIN32
  const auto process = GetModuleHandle(nullptr);
  return process ? reinterpret_cast<void*>(GetProcAddress(process, name))
                 : nullptr;
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

template <typename T>
T ResolveSymbol(const char* name) {
  return reinterpret_cast<T>(ResolveProcessSymbol(name));
}

HostTideStationInfoV1 EmptyInfo() {
  HostTideStationInfoV1 result{};
  result.struct_size = sizeof(result);
  return result;
}

}  // namespace

bool NativeTideHost::Resolve() {
  maximum_index_ = ResolveSymbol<MaximumIndexFn>(
      "PlugIn_GetTideStationMaximumIndexV1");
  station_info_ =
      ResolveSymbol<StationInfoFn>("PlugIn_GetTideStationInfoV1");
  nearest_ =
      ResolveSymbol<NearestFn>("PlugIn_GetNearestTideStationsV1");
  height_metres_ =
      ResolveSymbol<HeightMetresFn>("PlugIn_GetTideHeightMetersV1");
  return available();
}

std::vector<HostTideStationInfoV1> NativeTideHost::Nearest(
    double latitude, double longitude, double maximum_distance_nm,
    int maximum_results) const {
  if (!available() || maximum_results <= 0) return {};
  std::vector<HostTideStationInfoV1> result(maximum_results);
  for (auto& station : result) station = EmptyInfo();
  const int count = nearest_(latitude, longitude, maximum_distance_nm,
                             result.data(), maximum_results);
  if (count <= 0) return {};
  result.resize((std::min)(count, maximum_results));
  return result;
}

std::optional<double> NativeTideHost::HeightMetres(int station_index,
                                                   std::time_t time) const {
  if (!available()) return std::nullopt;
  double result = 0.0;
  if (!height_metres_(station_index, time, &result) || !std::isfinite(result))
    return std::nullopt;
  return result;
}

std::vector<HostTideStationInfoV1> NativeTideHost::Enumerate() const {
  std::vector<HostTideStationInfoV1> result;
  if (!available()) return result;
  const int maximum = maximum_index_();
  for (int index = 1; index <= maximum; ++index) {
    auto station = EmptyInfo();
    if (station_info_(index, &station)) result.push_back(station);
  }
  return result;
}

}  // namespace xtidal
