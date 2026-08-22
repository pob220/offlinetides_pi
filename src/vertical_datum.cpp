#include "vertical_datum.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include <json/json.h>

#include "height_correction.h"

namespace xtidal::authoring {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

bool IsWater(const environmental_grib::TideHeightHarmonics& field,
             std::size_t point) {
  if (!field.mask.empty() && field.mask[point]) return false;
  const auto points = field.grid.size();
  for (std::size_t constituent = 0; constituent < field.constituents.size();
       ++constituent) {
    const auto value = field.coefficients_m[constituent * points + point];
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
      return false;
  }
  return true;
}

bool GlobalGrid(const environmental_grib::RegularGrid& grid) {
  if (grid.longitudes.size() < 2) return false;
  const double step = grid.longitudes[1] - grid.longitudes[0];
  return grid.longitudes.back() - grid.longitudes.front() + step >= 359.9;
}

double NormalizedLongitude(double longitude,
                           const environmental_grib::RegularGrid& grid) {
  if (!GlobalGrid(grid)) return longitude;
  const auto west = grid.longitudes.front();
  while (longitude < west) longitude += 360.0;
  while (longitude >= west + 360.0) longitude -= 360.0;
  return longitude;
}

std::vector<std::size_t> Neighbours(
    const environmental_grib::TideHeightHarmonics& field, std::size_t point) {
  const auto nx = field.grid.longitudes.size();
  const auto ny = field.grid.latitudes.size();
  const auto x = point % nx;
  const auto y = point / nx;
  std::vector<std::size_t> result;
  result.reserve(4);
  if (y) result.push_back(point - nx);
  if (y + 1 < ny) result.push_back(point + nx);
  if (x)
    result.push_back(point - 1);
  else if (GlobalGrid(field.grid))
    result.push_back(point + nx - 1);
  if (x + 1 < nx)
    result.push_back(point + 1);
  else if (GlobalGrid(field.grid))
    result.push_back(point - nx + 1);
  return result;
}

double EdgeDistance(const environmental_grib::RegularGrid& grid,
                    std::size_t first, std::size_t second) {
  const auto nx = grid.longitudes.size();
  return GreatCircleDistanceKm(
      grid.latitudes[first / nx], grid.longitudes[first % nx],
      grid.latitudes[second / nx], grid.longitudes[second % nx]);
}

std::size_t NearestWaterPoint(
    const environmental_grib::TideHeightHarmonics& field,
    const ChartDatumStation& station, double maximum_km) {
  const auto nx = field.grid.longitudes.size();
  const auto longitude = NormalizedLongitude(station.longitude, field.grid);
  const auto nearest_x = static_cast<std::size_t>(std::distance(
      field.grid.longitudes.begin(),
      std::min_element(field.grid.longitudes.begin(),
                       field.grid.longitudes.end(), [&](double a, double b) {
                         return std::abs(a - longitude) <
                                std::abs(b - longitude);
                       })));
  const auto nearest_y = static_cast<std::size_t>(std::distance(
      field.grid.latitudes.begin(),
      std::min_element(field.grid.latitudes.begin(), field.grid.latitudes.end(),
                       [&](double a, double b) {
                         return std::abs(a - station.latitude) <
                                std::abs(b - station.latitude);
                       })));
  const auto lat_step =
      std::abs(field.grid.latitudes[1] - field.grid.latitudes[0]);
  const auto lon_step =
      std::abs(field.grid.longitudes[1] - field.grid.longitudes[0]);
  const auto yr =
      static_cast<std::size_t>(std::ceil(maximum_km / (111.2 * lat_step))) + 1;
  const double lon_scale = std::max(
      0.02, std::abs(std::cos(station.latitude * std::numbers::pi / 180.0)));
  const auto xr = static_cast<std::size_t>(
                      std::ceil(maximum_km / (111.2 * lon_step * lon_scale))) +
                  1;
  auto best = field.grid.size();
  double best_distance = maximum_km;
  const auto y0 = nearest_y > yr ? nearest_y - yr : 0;
  const auto y1 = std::min(field.grid.latitudes.size() - 1, nearest_y + yr);
  for (std::size_t y = y0; y <= y1; ++y) {
    for (std::ptrdiff_t dx = -static_cast<std::ptrdiff_t>(xr);
         dx <= static_cast<std::ptrdiff_t>(xr); ++dx) {
      auto x = static_cast<std::ptrdiff_t>(nearest_x) + dx;
      if (GlobalGrid(field.grid)) {
        x %= static_cast<std::ptrdiff_t>(nx);
        if (x < 0) x += nx;
      } else if (x < 0 || x >= static_cast<std::ptrdiff_t>(nx)) {
        continue;
      }
      const auto point = y * nx + static_cast<std::size_t>(x);
      if (!IsWater(field, point)) continue;
      const auto distance = GreatCircleDistanceKm(
          station.latitude, station.longitude, field.grid.latitudes[y],
          field.grid.longitudes[static_cast<std::size_t>(x)]);
      if (distance <= best_distance) {
        best = point;
        best_distance = distance;
      }
    }
  }
  return best;
}

struct MappedStation {
  const ChartDatumStation* station{};
  std::size_t point{};
  double radius_km{};
};

struct QueueNode {
  double distance{};
  std::size_t point{};
  std::size_t source{};
  bool operator>(const QueueNode& other) const {
    return distance > other.distance;
  }
};

DatumRealization ParseRealization(const std::string& value) {
  if (value == "LAT") return DatumRealization::kLowestAstronomicalTide;
  if (value == "MLLW") return DatumRealization::kMeanLowerLowWater;
  if (value == "LLWLT") return DatumRealization::kLowerLowWaterLargeTide;
  if (value == "NLLW") return DatumRealization::kNearlyLowestLowWater;
  if (value == "MLWS") return DatumRealization::kMeanLowWaterSprings;
  if (value == "MSL") return DatumRealization::kMeanSeaLevel;
  if (value == "TLT" || value == "LLW")
    return DatumRealization::kLowestLowWater;
  return DatumRealization::kOtherAuthorityChartDatum;
}

}  // namespace

std::vector<ChartDatumStation> LoadChartDatumStations(
    const std::filesystem::path& path, const Ticon3Catalogue* ticon) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open Chart Datum catalogue");
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(builder, input, &root, &errors) ||
      root.get("schema", "").asString() != "xtidal-chart-datum-stations" ||
      root.get("schema_version", 0).asInt() != 1 || !root["stations"].isArray())
    throw std::runtime_error("Chart Datum catalogue is invalid: " + errors);
  std::vector<ChartDatumStation> result;
  for (const auto& item : root["stations"]) {
    // Public-domain catalogues remain the default input.  A separately
    // documented authority value can also be used when the authoring
    // catalogue records explicit permission; this never changes the runtime
    // package's raw-data-free distribution policy.
    if (item.get("restriction", "").asString() != "Public Domain" &&
        !item.get("authoring_permission", false).asBool())
      continue;
    ChartDatumStation station;
    station.id = "tcd-" + std::to_string(item["record"].asUInt());
    station.name = item.get("name", "").asString();
    station.country = item.get("country", "").asString();
    station.source = item.get("source", "").asString();
    station.latitude = item["latitude"].asDouble();
    station.longitude = item["longitude"].asDouble();
    station.msl_above_chart_datum_m =
        item["msl_above_chart_datum_m"].asDouble();
    station.realization =
        ParseRealization(item.get("source_datum", "").asString());
    station.confidence =
        static_cast<std::uint8_t>(item.get("confidence", 0).asUInt());
    if (station.name.empty() || !std::isfinite(station.latitude) ||
        !std::isfinite(station.longitude) ||
        !std::isfinite(station.msl_above_chart_datum_m) ||
        station.msl_above_chart_datum_m < -5.0 ||
        station.msl_above_chart_datum_m > 15.0)
      continue;
    if (ticon) {
      double best = 5.0;
      for (const auto& observation : ticon->stations) {
        const auto distance =
            GreatCircleDistanceKm(station.latitude, station.longitude,
                                  observation.latitude, observation.longitude);
        if (distance < best) {
          best = distance;
          station.estuary = observation.gauge_type == "river";
        }
      }
    }
    result.push_back(std::move(station));
  }
  if (result.empty())
    throw std::runtime_error("Chart Datum catalogue has no usable stations");
  return result;
}

void VerticalDatumField::Validate() const {
  const auto points = grid.size();
  if (!points || offset_m.size() != points || uncertainty_m.size() != points ||
      nearest_station_distance_km.size() != points ||
      realization_class.size() != points || support_class.size() != points ||
      station_count.size() != points || valid.size() != points)
    throw std::runtime_error("vertical-datum field dimensions are invalid");
}

VerticalDatumField BuildVerticalDatumField(
    const environmental_grib::TideHeightHarmonics& water_mask,
    const std::vector<ChartDatumStation>& stations,
    const VerticalDatumAuthoringOptions& options) {
  water_mask.Validate();
  if (stations.empty() || options.coastal_support_radius_km <= 0.0 ||
      options.estuary_support_radius_km <= 0.0 ||
      options.maximum_station_snap_km <= 0.0)
    throw std::invalid_argument("vertical-datum authoring options are invalid");
  std::vector<MappedStation> mapped;
  for (const auto& station : stations) {
    const auto point =
        NearestWaterPoint(water_mask, station, options.maximum_station_snap_km);
    if (point == water_mask.grid.size()) continue;
    mapped.push_back({&station, point,
                      station.estuary ? options.estuary_support_radius_km
                                      : options.coastal_support_radius_km});
  }
  if (mapped.empty())
    throw std::runtime_error("no Chart Datum station maps to the water grid");

  const auto points = water_mask.grid.size();
  std::vector<double> nearest(points, kInfinity);
  std::vector<std::size_t> nearest_source(points, mapped.size());
  std::priority_queue<QueueNode, std::vector<QueueNode>,
                      std::greater<QueueNode>>
      queue;
  for (std::size_t source = 0; source < mapped.size(); ++source)
    queue.push({0.0, mapped[source].point, source});
  while (!queue.empty()) {
    const auto node = queue.top();
    queue.pop();
    if (node.distance >= nearest[node.point] ||
        node.distance > mapped[node.source].radius_km)
      continue;
    nearest[node.point] = node.distance;
    nearest_source[node.point] = node.source;
    for (const auto neighbour : Neighbours(water_mask, node.point)) {
      if (!IsWater(water_mask, neighbour)) continue;
      const auto candidate =
          node.distance + EdgeDistance(water_mask.grid, node.point, neighbour);
      if (candidate <= mapped[node.source].radius_km &&
          candidate < nearest[neighbour])
        queue.push({candidate, neighbour, node.source});
    }
  }

  std::vector<double> weight(points), weighted_offset(points),
      weighted_square(points);
  std::vector<std::uint32_t> counts(points);
  std::vector<std::uint8_t> has_estuary(points);
  for (std::size_t source = 0; source < mapped.size(); ++source) {
    const auto& item = mapped[source];
    std::priority_queue<QueueNode, std::vector<QueueNode>,
                        std::greater<QueueNode>>
        local_queue;
    std::unordered_map<std::size_t, double> visited;
    local_queue.push({0.0, item.point, source});
    while (!local_queue.empty()) {
      const auto node = local_queue.top();
      local_queue.pop();
      if (node.distance > item.radius_km) continue;
      const auto found = visited.find(node.point);
      if (found != visited.end() && found->second <= node.distance) continue;
      visited[node.point] = node.distance;
      const auto nearest_index = nearest_source[node.point];
      if (nearest_index < mapped.size() &&
          mapped[nearest_index].station->realization ==
              item.station->realization) {
        const double fraction = node.distance / item.radius_km;
        const double w = std::pow(std::max(0.0, 1.0 - fraction), 2);
        weight[node.point] += w;
        weighted_offset[node.point] +=
            w * item.station->msl_above_chart_datum_m;
        weighted_square[node.point] +=
            w * std::pow(item.station->msl_above_chart_datum_m, 2);
        ++counts[node.point];
        has_estuary[node.point] |= item.station->estuary;
      }
      for (const auto neighbour : Neighbours(water_mask, node.point)) {
        if (!IsWater(water_mask, neighbour)) continue;
        const auto candidate =
            node.distance +
            EdgeDistance(water_mask.grid, node.point, neighbour);
        if (candidate <= item.radius_km)
          local_queue.push({candidate, neighbour, source});
      }
    }
  }

  VerticalDatumField result;
  result.grid = water_mask.grid;
  result.offset_m.assign(points, std::numeric_limits<double>::quiet_NaN());
  result.uncertainty_m.assign(points, std::numeric_limits<double>::quiet_NaN());
  result.nearest_station_distance_km = nearest;
  result.realization_class.assign(points, 0);
  result.support_class.assign(points, 0);
  result.station_count.assign(points, 0);
  result.valid.assign(points, 0);
  for (std::size_t point = 0; point < points; ++point) {
    if (weight[point] <= 0.0 || nearest_source[point] >= mapped.size())
      continue;
    const auto mean = weighted_offset[point] / weight[point];
    const auto variance =
        std::max(0.0, weighted_square[point] / weight[point] - mean * mean);
    const bool estuary = has_estuary[point];
    const auto floor = estuary ? options.estuary_uncertainty_floor_m
                               : options.coastal_uncertainty_floor_m;
    result.offset_m[point] = mean;
    result.uncertainty_m[point] =
        std::hypot(std::hypot(floor, std::sqrt(variance)),
                   options.distance_uncertainty_m_per_km * nearest[point]);
    result.realization_class[point] = static_cast<std::uint8_t>(
        mapped[nearest_source[point]].station->realization);
    result.support_class[point] = static_cast<std::uint8_t>(
        estuary ? DatumSupportClass::kEstuaryStationConstrained
                : DatumSupportClass::kStationConstrained);
    result.station_count[point] = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(counts[point], 65535));
    result.valid[point] = 1;
  }
  result.Validate();
  return result;
}

}  // namespace xtidal::authoring
