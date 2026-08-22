#include "height_assimilation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <map>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <tuple>

#include "height_correction.h"
#include "environmental_grib/error.h"
#include "environmental_grib/tpxo.h"

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

bool IsGlobalLongitudeGrid(const environmental_grib::RegularGrid& grid) {
  if (grid.longitudes.size() < 2) return false;
  const double spacing = grid.longitudes[1] - grid.longitudes[0];
  return grid.longitudes.back() - grid.longitudes.front() + spacing >= 359.9;
}

double NormalizeLongitude(double longitude,
                          const environmental_grib::RegularGrid& grid) {
  if (!IsGlobalLongitudeGrid(grid)) return longitude;
  const double west = grid.longitudes.front();
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
  if (y > 0) result.push_back(point - nx);
  if (y + 1 < ny) result.push_back(point + nx);
  if (x > 0)
    result.push_back(point - 1);
  else if (IsGlobalLongitudeGrid(field.grid))
    result.push_back(point + nx - 1);
  if (x + 1 < nx)
    result.push_back(point + 1);
  else if (IsGlobalLongitudeGrid(field.grid))
    result.push_back(point - nx + 1);
  return result;
}

std::size_t NearestWaterPoint(
    const environmental_grib::TideHeightHarmonics& background,
    const HarmonicObservation& observation, double maximum_distance_km) {
  const auto& latitudes = background.grid.latitudes;
  const auto& longitudes = background.grid.longitudes;
  const auto nx = longitudes.size();
  const double longitude =
      NormalizeLongitude(observation.longitude, background.grid);
  const auto y_center = static_cast<std::size_t>(std::distance(
      latitudes.begin(),
      std::min_element(latitudes.begin(), latitudes.end(),
                       [&](double a, double b) {
                         return std::abs(a - observation.latitude) <
                                std::abs(b - observation.latitude);
                       })));
  const auto x_center = static_cast<std::size_t>(std::distance(
      longitudes.begin(), std::min_element(longitudes.begin(), longitudes.end(),
                                           [&](double a, double b) {
                                             return std::abs(a - longitude) <
                                                    std::abs(b - longitude);
                                           })));
  const double lat_step =
      latitudes.size() > 1 ? std::abs(latitudes[1] - latitudes[0]) : 180.0;
  const double lon_step =
      longitudes.size() > 1 ? std::abs(longitudes[1] - longitudes[0]) : 360.0;
  const auto y_radius = static_cast<std::size_t>(std::ceil(
                            maximum_distance_km / (111.2 * lat_step))) +
                        1;
  const double longitude_scale = std::max(
      0.02,
      std::abs(std::cos(observation.latitude * std::numbers::pi / 180.0)));
  const auto x_radius =
      static_cast<std::size_t>(std::ceil(
          maximum_distance_km / (111.2 * lon_step * longitude_scale))) +
      1;

  std::size_t best = background.grid.size();
  double best_distance = maximum_distance_km;
  const auto y0 = y_center > y_radius ? y_center - y_radius : 0;
  const auto y1 = std::min(latitudes.size() - 1, y_center + y_radius);
  for (std::size_t y = y0; y <= y1; ++y) {
    for (std::ptrdiff_t offset = -static_cast<std::ptrdiff_t>(x_radius);
         offset <= static_cast<std::ptrdiff_t>(x_radius); ++offset) {
      std::ptrdiff_t x = static_cast<std::ptrdiff_t>(x_center) + offset;
      if (IsGlobalLongitudeGrid(background.grid)) {
        x %= static_cast<std::ptrdiff_t>(nx);
        if (x < 0) x += static_cast<std::ptrdiff_t>(nx);
      } else if (x < 0 || x >= static_cast<std::ptrdiff_t>(nx)) {
        continue;
      }
      const auto point = y * nx + static_cast<std::size_t>(x);
      if (!IsWater(background, point)) continue;
      const double distance = GreatCircleDistanceKm(
          observation.latitude, observation.longitude, latitudes[y],
          longitudes[static_cast<std::size_t>(x)]);
      if (distance <= best_distance) {
        best = point;
        best_distance = distance;
      }
    }
  }
  return best;
}

double ObservationMeanSigma(const HarmonicObservation& observation,
                            double floor_m) {
  if (observation.complex_standard_deviation_m.empty())
    return std::max(0.25, floor_m);
  double sum = 0.0;
  for (const auto& [name, sigma] : observation.complex_standard_deviation_m) {
    (void)name;
    sum += sigma;
  }
  return std::max(floor_m,
                  sum / observation.complex_standard_deviation_m.size());
}

struct MappedObservation {
  const HarmonicObservation* observation{};
  std::size_t point{};
};

std::vector<std::size_t> BracketingNodes(const std::vector<double>& axis,
                                         double value) {
  if (value <= axis.front()) return {0};
  if (value >= axis.back()) return {axis.size() - 1};
  const auto upper = std::lower_bound(axis.begin(), axis.end(), value);
  const auto high = static_cast<std::size_t>(upper - axis.begin());
  if (std::abs(axis[high] - value) <= 1e-10) return {high};
  return {high - 1, high};
}

}  // namespace

void AssimilatedHeightField::Validate() const {
  harmonics.Validate();
  const auto points = harmonics.grid.size();
  if (quality.support_class.size() != points ||
      quality.nearest_observation_distance_km.size() != points ||
      quality.harmonic_sigma_m.size() != points ||
      quality.contributing_observations.size() != points)
    throw std::runtime_error("height-assimilation quality dimensions differ");
}

AssimilatedHeightField AssimilateHarmonicObservations(
    const environmental_grib::TideHeightHarmonics& background,
    const std::vector<HarmonicObservation>& observations,
    const HeightAssimilationOptions& options) {
  background.Validate();
  if (observations.empty())
    throw std::invalid_argument("height assimilation needs observations");
  if (!std::isfinite(options.maximum_influence_km) ||
      options.maximum_influence_km <= 0.0 ||
      !std::isfinite(options.river_maximum_influence_km) ||
      options.river_maximum_influence_km <= 0.0 ||
      !std::isfinite(options.full_strength_radius_km) ||
      options.full_strength_radius_km < 0.0 ||
      options.full_strength_radius_km >=
          std::min(options.maximum_influence_km,
                   options.river_maximum_influence_km) ||
      !std::isfinite(options.observation_anchor_gain) ||
      options.observation_anchor_gain < 0.0 ||
      options.observation_anchor_gain > 1.0 ||
      !std::isfinite(options.residual_gain) || options.residual_gain < 0.0 ||
      options.residual_gain > 1.0 ||
      !std::isfinite(options.maximum_station_snap_km) ||
      options.maximum_station_snap_km <= 0.0 ||
      !std::isfinite(options.background_only_sigma_m) ||
      options.background_only_sigma_m < 0.0 ||
      !std::isfinite(options.observation_sigma_floor_m) ||
      options.observation_sigma_floor_m < 0.0 ||
      !std::isfinite(options.distance_sigma_m) ||
      options.distance_sigma_m < 0.0)
    throw std::invalid_argument("height assimilation options are invalid");
  if (!options.background_sigma_by_point_m.empty() &&
      options.background_sigma_by_point_m.size() != background.grid.size())
    throw std::invalid_argument(
        "height assimilation background sigma dimensions differ");
  if (!options.background_support_class_by_point.empty() &&
      options.background_support_class_by_point.size() !=
          background.grid.size())
    throw std::invalid_argument(
        "height assimilation background support dimensions differ");

  AssimilatedHeightField result;
  result.harmonics = background;
  const auto points = background.grid.size();
  std::map<std::string, bool> known_constituents;
  for (const auto& name : result.harmonics.constituents)
    known_constituents.emplace(name, true);
  std::vector<std::string> additions;
  const environmental_grib::TimePoint support_probe{
      std::chrono::seconds{1'787'184'000}};
  for (const auto& observation : observations) {
    for (const auto& [name, value] : observation.coefficients_m) {
      if (known_constituents.contains(name)) continue;
      try {
        (void)environmental_grib::PredictAtlasHarmonicGrid(
            {name}, {value}, 1, {support_probe}, false);
        known_constituents.emplace(name, true);
        additions.push_back(name);
      } catch (const environmental_grib::ValidationError&) {
      }
    }
  }
  std::sort(additions.begin(), additions.end());
  if (!additions.empty()) {
    result.harmonics.constituents.insert(result.harmonics.constituents.end(),
                                         additions.begin(), additions.end());
    result.harmonics.coefficients_m.resize(
        result.harmonics.constituents.size() * points,
        std::complex<double>{0.0, 0.0});
  }
  const auto columns = background.grid.longitudes.size();

  const auto better_observation = [](const HarmonicObservation& candidate,
                                     const HarmonicObservation& current) {
    const bool candidate_river = candidate.gauge_type == "river";
    const bool current_river = current.gauge_type == "river";
    if (candidate_river != current_river) return candidate_river;
    if (candidate.observation_count != current.observation_count)
      return candidate.observation_count > current.observation_count;
    if (candidate.valid_fraction != current.valid_fraction)
      return candidate.valid_fraction > current.valid_fraction;
    if (candidate.maximum_gap_days != current.maximum_gap_days)
      return candidate.maximum_gap_days < current.maximum_gap_days;
    return candidate.id < current.id;
  };
  std::map<std::size_t, const HarmonicObservation*> selected_by_cell;
  for (const auto& observation : observations) {
    const double longitude =
        NormalizeLongitude(observation.longitude, background.grid);
    const auto xs = BracketingNodes(background.grid.longitudes, longitude);
    const auto ys =
        BracketingNodes(background.grid.latitudes, observation.latitude);
    auto target = points;
    double target_distance = kInfinity;
    for (const auto y : ys) {
      for (const auto x : xs) {
        const auto point = y * columns + x;
        const double distance = GreatCircleDistanceKm(
            observation.latitude, observation.longitude,
            background.grid.latitudes[y], background.grid.longitudes[x]);
        if (distance < target_distance) {
          target = point;
          target_distance = distance;
        }
      }
    }
    if (target == points) continue;
    const auto current = selected_by_cell.find(target);
    if (current == selected_by_cell.end() ||
        better_observation(observation, *current->second))
      selected_by_cell[target] = &observation;
  }

  std::map<std::size_t, const HarmonicObservation*> mapped_by_point;
  for (const auto& [selected_target, observation_pointer] : selected_by_cell) {
    const auto& observation = *observation_pointer;
    const auto nearest_background = NearestWaterPoint(
        background, observation, options.maximum_station_snap_km);
    const double longitude =
        NormalizeLongitude(observation.longitude, background.grid);
    const auto xs = BracketingNodes(background.grid.longitudes, longitude);
    const auto ys =
        BracketingNodes(background.grid.latitudes, observation.latitude);
    const auto target = selected_target;
    if (target == points) continue;
    if (!IsWater(background, target)) {
      if (result.harmonics.mask.empty())
        result.harmonics.mask.assign(points, 0);
      for (const auto y : ys) {
        for (const auto x : xs) {
          const auto point = y * columns + x;
          for (std::size_t constituent = 0;
               constituent < result.harmonics.constituents.size();
               ++constituent) {
            const auto& name = result.harmonics.constituents[constituent];
            const auto observed = observation.coefficients_m.find(name);
            if (observed != observation.coefficients_m.end()) {
              result.harmonics.coefficients_m[constituent * points + point] =
                  observed->second;
            } else if (constituent < background.constituents.size() &&
                       nearest_background != points) {
              result.harmonics.coefficients_m[constituent * points + point] =
                  background.coefficients_m[constituent * points +
                                            nearest_background];
            } else {
              result.harmonics.coefficients_m[constituent * points + point] = {
                  0.0, 0.0};
            }
          }
          result.harmonics.mask[point] = 0;
        }
      }
    }
    // Constrain every interpolation corner surrounding the observation.  A
    // single-node anchor is diluted by bilinear sampling even at the exact
    // gauge coordinate on a coarse operational grid.  These four nodes are
    // all direct support; residuals propagated beyond this footprint still
    // use the separately cross-validated transfer gain and taper.
    bool mapped_observation = false;
    for (const auto y : ys) {
      for (const auto x : xs) {
        const auto point = y * columns + x;
        if (!IsWater(result.harmonics, point)) continue;
        mapped_observation = true;
        const auto current = mapped_by_point.find(point);
        if (current == mapped_by_point.end() ||
            better_observation(observation, *current->second))
          mapped_by_point[point] = &observation;
      }
    }
    if (!mapped_observation && nearest_background != points)
      mapped_by_point[nearest_background] = &observation;
  }
  std::vector<MappedObservation> mapped;
  mapped.reserve(mapped_by_point.size());
  for (const auto& [point, observation] : mapped_by_point)
    mapped.push_back({observation, point});
  if (mapped.empty())
    throw std::runtime_error(
        "no observation maps to the background water mask");

  result.quality.support_class = options.background_support_class_by_point;
  if (result.quality.support_class.empty())
    result.quality.support_class.assign(points, HeightSupportClass::kUnknown);
  result.quality.nearest_observation_distance_km.assign(points, kInfinity);
  result.quality.harmonic_sigma_m = options.background_sigma_by_point_m;
  if (result.quality.harmonic_sigma_m.empty())
    result.quality.harmonic_sigma_m.assign(points,
                                           options.background_only_sigma_m);
  result.quality.contributing_observations.assign(points, 0);
  std::vector<std::uint8_t> water(points, 0);
  for (std::size_t point = 0; point < points; ++point) {
    water[point] = IsWater(result.harmonics, point);
    if (water[point] &&
        result.quality.support_class[point] == HeightSupportClass::kUnknown)
      result.quality.support_class[point] = HeightSupportClass::kBackgroundOnly;
  }

  struct Adjacency {
    std::array<std::uint32_t, 4> points{};
    std::uint8_t count{};
  };
  std::vector<Adjacency> adjacency(points);
  for (std::size_t point = 0; point < points; ++point) {
    if (!water[point]) continue;
    for (const auto neighbour : Neighbours(result.harmonics, point))
      if (water[neighbour])
        adjacency[point].points[adjacency[point].count++] =
            static_cast<std::uint32_t>(neighbour);
  }

  struct QueueNode {
    double distance{};
    std::size_t point{};
    HeightSupportClass source_class{};
    double source_sigma{};
    double maximum_influence_km{};
    std::size_t source{};
    bool operator>(const QueueNode& other) const {
      return distance > other.distance;
    }
  };
  std::priority_queue<QueueNode, std::vector<QueueNode>,
                      std::greater<QueueNode>>
      queue;
  std::vector<std::size_t> nearest_source(points, mapped.size());
  for (std::size_t source = 0; source < mapped.size(); ++source) {
    const auto& item = mapped[source];
    const auto support =
        item.observation->gauge_type == "river"
            ? HeightSupportClass::kEstuaryObservationConstrained
            : HeightSupportClass::kObservationConstrained;
    const double maximum_influence = item.observation->gauge_type == "river"
                                         ? options.river_maximum_influence_km
                                         : options.maximum_influence_km;
    queue.push({0.0, item.point, support,
                ObservationMeanSigma(*item.observation,
                                     options.observation_sigma_floor_m),
                maximum_influence, source});
    if (result.quality.contributing_observations[item.point] !=
        std::numeric_limits<std::uint16_t>::max())
      ++result.quality.contributing_observations[item.point];
  }
  while (!queue.empty()) {
    const auto node = queue.top();
    queue.pop();
    if (node.distance >=
        result.quality.nearest_observation_distance_km[node.point])
      continue;
    result.quality.nearest_observation_distance_km[node.point] = node.distance;
    nearest_source[node.point] = node.source;
    result.quality.support_class[node.point] = node.source_class;
    const double fraction =
        std::min(1.0, node.distance / node.maximum_influence_km);
    const double background_sigma = result.quality.harmonic_sigma_m[node.point];
    result.quality.harmonic_sigma_m[node.point] =
        std::hypot(node.source_sigma,
                   fraction * (background_sigma + options.distance_sigma_m));
    if (node.distance >= node.maximum_influence_km) continue;
    const auto nx = result.harmonics.grid.longitudes.size();
    const auto x = node.point % nx;
    const auto y = node.point / nx;
    for (std::size_t edge_index = 0; edge_index < adjacency[node.point].count;
         ++edge_index) {
      const auto neighbour = adjacency[node.point].points[edge_index];
      const auto nx2 = neighbour % nx;
      const auto ny2 = neighbour / nx;
      const double edge =
          GreatCircleDistanceKm(result.harmonics.grid.latitudes[y],
                                result.harmonics.grid.longitudes[x],
                                result.harmonics.grid.latitudes[ny2],
                                result.harmonics.grid.longitudes[nx2]);
      const double candidate = node.distance + edge;
      if (candidate < node.maximum_influence_km &&
          candidate < result.quality.nearest_observation_distance_km[neighbour])
        queue.push({candidate, neighbour, node.source_class, node.source_sigma,
                    node.maximum_influence_km, node.source});
    }
  }

  std::map<std::string, std::size_t> constituent_index;
  for (std::size_t index = 0; index < result.harmonics.constituents.size();
       ++index)
    constituent_index.emplace(result.harmonics.constituents[index], index);
  std::vector<std::size_t> active_points;
  active_points.reserve(points / 4);
  for (std::size_t point = 0; point < points; ++point)
    if (water[point] && result.quality.nearest_observation_distance_km[point] <
                            options.maximum_influence_km)
      active_points.push_back(point);

  for (const auto& [name, index] : constituent_index) {
    std::vector<std::complex<double>> source_residual(mapped.size());
    std::vector<std::uint8_t> source_valid(mapped.size());
    for (std::size_t source = 0; source < mapped.size(); ++source) {
      const auto& item = mapped[source];
      const auto coefficient = item.observation->coefficients_m.find(name);
      if (coefficient == item.observation->coefficients_m.end()) continue;
      const auto background_value =
          result.harmonics.coefficients_m[index * points + item.point];
      source_residual[source] = coefficient->second - background_value;
      source_valid[source] = 1;
    }
    for (const auto point : active_points) {
      const auto source = nearest_source[point];
      if (source >= mapped.size() || !source_valid[source]) continue;
      const auto& item = mapped[source];
      const double radius = item.observation->gauge_type == "river"
                                ? options.river_maximum_influence_km
                                : options.maximum_influence_km;
      const double fraction =
          std::clamp((result.quality.nearest_observation_distance_km[point] -
                      options.full_strength_radius_km) /
                         (radius - options.full_strength_radius_km),
                     0.0, 1.0);
      const double taper = (1.0 - fraction) * (1.0 - fraction);
      const double anchor_fraction =
          options.full_strength_radius_km > 0.0
              ? std::clamp(
                    1.0 -
                        result.quality.nearest_observation_distance_km[point] /
                            options.full_strength_radius_km,
                    0.0, 1.0)
              : (result.quality.nearest_observation_distance_km[point] == 0.0
                     ? 1.0
                     : 0.0);
      const double gain = options.residual_gain +
                          anchor_fraction * (options.observation_anchor_gain -
                                             options.residual_gain);
      result.harmonics.coefficients_m[index * points + point] +=
          gain * taper * source_residual[source];
    }
  }
  result.Validate();
  return result;
}

}  // namespace xtidal::authoring
