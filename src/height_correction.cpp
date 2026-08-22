#include "height_correction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace xtidal::authoring {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusKm = 6371.0088;

enum class SupportKind { kMajor, kShallow, kDatum };

bool PointInSupportPolygon(const HarmonicAnchor& anchor, double latitude,
                           double longitude) {
  if (anchor.support_polygon.empty()) return true;
  if (anchor.support_polygon.size() < 3) return false;
  bool inside = false;
  for (std::size_t i = 0, j = anchor.support_polygon.size() - 1;
       i < anchor.support_polygon.size(); j = i++) {
    const auto [xi, yi] = anchor.support_polygon[i];
    const auto [xj, yj] = anchor.support_polygon[j];
    const bool crosses =
        ((yi > latitude) != (yj > latitude)) &&
        (longitude < (xj - xi) * (latitude - yi) / (yj - yi) + xi);
    if (crosses) inside = !inside;
  }
  return inside;
}

double EffectiveSupport(double configured, const HarmonicAnchor& anchor) {
  return configured > 0.0 ? configured : anchor.support_radius_km;
}

double InfluenceWeight(const HarmonicAnchor& anchor, double latitude,
                       double longitude, SupportKind kind) {
  if (!PointInSupportPolygon(anchor, latitude, longitude)) return 0.0;
  double along_support = 0.0;
  double cross_support = 0.0;
  switch (kind) {
    case SupportKind::kMajor:
      along_support = EffectiveSupport(anchor.major_along_support_km, anchor);
      cross_support = EffectiveSupport(anchor.major_cross_support_km, anchor);
      break;
    case SupportKind::kShallow:
      along_support = EffectiveSupport(anchor.shallow_along_support_km, anchor);
      cross_support = EffectiveSupport(anchor.shallow_cross_support_km, anchor);
      break;
    case SupportKind::kDatum:
      along_support = EffectiveSupport(anchor.datum_along_support_km, anchor);
      cross_support = EffectiveSupport(anchor.datum_cross_support_km, anchor);
      break;
  }
  if (along_support <= 0.0 || cross_support <= 0.0) return 0.0;

  const double mean_latitude = (latitude + anchor.latitude) * kPi / 360.0;
  const double north_km =
      (latitude - anchor.latitude) * kPi / 180.0 * kEarthRadiusKm;
  const double east_km = (longitude - anchor.longitude) * kPi / 180.0 *
                         kEarthRadiusKm * std::cos(mean_latitude);
  const double bearing = anchor.axis_bearing_degrees * kPi / 180.0;
  const double along_km =
      north_km * std::cos(bearing) + east_km * std::sin(bearing);
  const double cross_km =
      -north_km * std::sin(bearing) + east_km * std::cos(bearing);
  const double ratio_squared = std::pow(along_km / along_support, 2) +
                               std::pow(cross_km / cross_support, 2);
  if (ratio_squared >= 1.0) return 0.0;
  return anchor.quality_weight * std::pow(1.0 - ratio_squared, 2);
}

bool BackgroundValid(const environmental_grib::TideHeightHarmonics& background,
                     std::size_t point) {
  if (!background.mask.empty() && background.mask[point]) return false;
  const auto points = background.grid.size();
  for (std::size_t constituent = 0;
       constituent < background.constituents.size(); ++constituent) {
    const auto value = background.coefficients_m[constituent * points + point];
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
      return false;
  }
  return true;
}

std::size_t NearestValidPoint(
    const environmental_grib::TideHeightHarmonics& background,
    const HarmonicAnchor& anchor) {
  const auto columns = background.grid.longitudes.size();
  auto best = background.grid.size();
  double best_distance = std::numeric_limits<double>::infinity();
  for (std::size_t point = 0; point < background.grid.size(); ++point) {
    if (!BackgroundValid(background, point)) continue;
    const auto y = point / columns;
    const auto x = point % columns;
    const auto distance = GreatCircleDistanceKm(
        anchor.latitude, anchor.longitude, background.grid.latitudes[y],
        background.grid.longitudes[x]);
    if (distance < best_distance) {
      best = point;
      best_distance = distance;
    }
  }
  if (best == background.grid.size())
    throw std::runtime_error("correction background contains no valid point");
  return best;
}

std::complex<double> SampleBackgroundCoefficient(
    const environmental_grib::TideHeightHarmonics& background,
    std::size_t constituent, const HarmonicAnchor& anchor) {
  const auto& latitudes = background.grid.latitudes;
  const auto& longitudes = background.grid.longitudes;
  const auto points = background.grid.size();
  const auto nearest = NearestValidPoint(background, anchor);
  if (latitudes.size() < 2 || longitudes.size() < 2 ||
      anchor.latitude < latitudes.front() ||
      anchor.latitude > latitudes.back() ||
      anchor.longitude < longitudes.front() ||
      anchor.longitude > longitudes.back())
    return background.coefficients_m[constituent * points + nearest];

  const auto upper_x =
      std::upper_bound(longitudes.begin(), longitudes.end(), anchor.longitude);
  const auto upper_y =
      std::upper_bound(latitudes.begin(), latitudes.end(), anchor.latitude);
  const auto x1 = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
      upper_x - longitudes.begin(), 1,
      static_cast<std::ptrdiff_t>(longitudes.size() - 1)));
  const auto y1 = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
      upper_y - latitudes.begin(), 1,
      static_cast<std::ptrdiff_t>(latitudes.size() - 1)));
  const auto x0 = x1 - 1;
  const auto y0 = y1 - 1;
  const auto columns = longitudes.size();
  const std::array<std::size_t, 4> corners{y0 * columns + x0, y0 * columns + x1,
                                           y1 * columns + x0,
                                           y1 * columns + x1};
  if (std::any_of(corners.begin(), corners.end(), [&](std::size_t point) {
        return !BackgroundValid(background, point);
      }))
    return background.coefficients_m[constituent * points + nearest];
  const double tx =
      (anchor.longitude - longitudes[x0]) / (longitudes[x1] - longitudes[x0]);
  const double ty =
      (anchor.latitude - latitudes[y0]) / (latitudes[y1] - latitudes[y0]);
  const auto value = [&](std::size_t point) {
    return background.coefficients_m[constituent * points + point];
  };
  return (1.0 - ty) *
             ((1.0 - tx) * value(corners[0]) + tx * value(corners[1])) +
         ty * ((1.0 - tx) * value(corners[2]) + tx * value(corners[3]));
}
}  // namespace

double GreatCircleDistanceKm(double latitude_a, double longitude_a,
                             double latitude_b, double longitude_b) {
  const auto radians = [](double value) { return value * kPi / 180.0; };
  const double lat_a = radians(latitude_a);
  const double lat_b = radians(latitude_b);
  const double delta_lat = lat_b - lat_a;
  const double delta_lon = radians(longitude_b - longitude_a);
  const double haversine = std::pow(std::sin(delta_lat / 2.0), 2) +
                           std::cos(lat_a) * std::cos(lat_b) *
                               std::pow(std::sin(delta_lon / 2.0), 2);
  return 2.0 * kEarthRadiusKm *
         std::asin(std::sqrt(std::clamp(haversine, 0.0, 1.0)));
}

bool IsShallowWaterConstituent(const std::string& name) {
  static const std::set<std::string> shallow{"2mk3", "2sm2", "m3",  "m4", "m6",
                                             "mk3",  "mn4",  "ms4", "s4"};
  return shallow.contains(name);
}

void CorrectedHeightField::Validate() const {
  if (grid.size() == 0)
    throw std::runtime_error("corrected height grid is empty");
  if (constituents.empty() || constituents.front() != "z0")
    throw std::runtime_error("corrected height field must start with z0");
  if (coefficients_m.size() != constituents.size() * grid.size())
    throw std::runtime_error("corrected coefficient dimensions are invalid");
  if (valid.size() != grid.size() ||
      nearest_anchor_distance_km.size() != grid.size())
    throw std::runtime_error("corrected validity dimensions are invalid");
  if (anchor_ids.empty())
    throw std::runtime_error("corrected field has no anchors");
}

CorrectedHeightField ApplyLocalHarmonicCorrections(
    const environmental_grib::TideHeightHarmonics& background,
    const std::vector<HarmonicAnchor>& anchors) {
  background.Validate();
  if (anchors.empty())
    throw std::invalid_argument("at least one harmonic anchor is required");

  std::set<std::string> names(background.constituents.begin(),
                              background.constituents.end());
  names.erase("z0");
  for (const auto& anchor : anchors) {
    if (anchor.id.empty() || anchor.name.empty() ||
        !std::isfinite(anchor.latitude) || !std::isfinite(anchor.longitude) ||
        !std::isfinite(anchor.chart_datum_reference_m) ||
        !std::isfinite(anchor.support_radius_km) ||
        !std::isfinite(anchor.axis_bearing_degrees) ||
        !std::isfinite(anchor.quality_weight) || anchor.quality_weight <= 0.0 ||
        !std::isfinite(anchor.major_correction_gain) ||
        anchor.major_correction_gain < 0.0 ||
        !std::isfinite(anchor.shallow_correction_gain) ||
        anchor.shallow_correction_gain < 0.0 ||
        !std::isfinite(anchor.prediction_time_shift_minutes) ||
        !std::isfinite(anchor.amplitude_scale) ||
        anchor.amplitude_scale <= 0.0 ||
        !std::isfinite(anchor.chart_datum_offset_m) ||
        EffectiveSupport(anchor.major_along_support_km, anchor) <= 0.0 ||
        EffectiveSupport(anchor.major_cross_support_km, anchor) <= 0.0 ||
        EffectiveSupport(anchor.shallow_along_support_km, anchor) <= 0.0 ||
        EffectiveSupport(anchor.shallow_cross_support_km, anchor) <= 0.0 ||
        EffectiveSupport(anchor.datum_along_support_km, anchor) <= 0.0 ||
        EffectiveSupport(anchor.datum_cross_support_km, anchor) <= 0.0 ||
        (!anchor.support_polygon.empty() && anchor.support_polygon.size() < 3))
      throw std::invalid_argument("harmonic anchor is invalid");
    for (const auto& [name, value] : anchor.coefficients_m) {
      if (name != "z0" && std::isfinite(value.real()) &&
          std::isfinite(value.imag()))
        names.insert(name);
    }
  }

  CorrectedHeightField result;
  result.grid = background.grid;
  result.constituents.push_back("z0");
  result.constituents.insert(result.constituents.end(), names.begin(),
                             names.end());
  const auto points = result.grid.size();
  result.coefficients_m.assign(result.constituents.size() * points,
                               {std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::quiet_NaN()});
  result.valid.assign(points, 0);
  result.nearest_anchor_distance_km.assign(
      points, std::numeric_limits<double>::infinity());
  for (const auto& anchor : anchors) result.anchor_ids.push_back(anchor.id);

  std::map<std::string, std::size_t> background_index;
  for (std::size_t index = 0; index < background.constituents.size(); ++index)
    background_index.emplace(background.constituents[index], index);

  struct PreparedAnchor {
    const HarmonicAnchor* anchor{};
    std::map<std::string, std::complex<double>> innovation;
  };
  std::vector<PreparedAnchor> prepared;
  prepared.reserve(anchors.size());
  for (const auto& anchor : anchors) {
    PreparedAnchor item;
    item.anchor = &anchor;
    for (const auto& [name, source_raw] : anchor.coefficients_m) {
      if (name == "z0" || !names.contains(name)) continue;
      const auto source =
          anchor.amplitude_scale *
          environmental_grib::ShiftAtlasHarmonicCoefficient(
              name, source_raw, anchor.prediction_time_shift_minutes * 60.0);
      std::complex<double> base{};
      if (const auto found = background_index.find(name);
          found != background_index.end())
        base = SampleBackgroundCoefficient(background, found->second, anchor);
      item.innovation.emplace(name, source - base);
    }
    prepared.push_back(std::move(item));
  }

  const auto columns = result.grid.longitudes.size();
  for (std::size_t point = 0; point < points; ++point) {
    if (!BackgroundValid(background, point)) continue;
    const auto y = point / columns;
    const auto x = point % columns;
    std::vector<std::pair<const PreparedAnchor*, double>> datum_active;
    double datum_weight_sum = 0.0;
    for (const auto& item : prepared) {
      const auto& anchor = *item.anchor;
      const double distance = GreatCircleDistanceKm(
          result.grid.latitudes[y], result.grid.longitudes[x], anchor.latitude,
          anchor.longitude);
      result.nearest_anchor_distance_km[point] =
          std::min(result.nearest_anchor_distance_km[point], distance);
      const double weight =
          InfluenceWeight(anchor, result.grid.latitudes[y],
                          result.grid.longitudes[x], SupportKind::kDatum);
      if (weight > 0.0) {
        datum_active.emplace_back(&item, weight);
        datum_weight_sum += weight;
      }
    }
    if (datum_active.empty() || datum_weight_sum <= 0.0) continue;
    result.valid[point] = 1;

    std::complex<double> datum{};
    for (const auto& [item, weight] : datum_active)
      datum += weight * (item->anchor->chart_datum_reference_m +
                         item->anchor->chart_datum_offset_m);
    result.coefficients_m[point] = datum / datum_weight_sum;

    for (std::size_t output_index = 1;
         output_index < result.constituents.size(); ++output_index) {
      const auto& name = result.constituents[output_index];
      std::complex<double> value{};
      if (const auto found = background_index.find(name);
          found != background_index.end())
        value = background.coefficients_m[found->second * points + point];
      std::complex<double> correction{};
      double correction_weight_sum = 0.0;
      for (const auto& item : prepared) {
        const auto innovation = item.innovation.find(name);
        if (innovation == item.innovation.end()) continue;
        const auto kind = IsShallowWaterConstituent(name)
                              ? SupportKind::kShallow
                              : SupportKind::kMajor;
        const double weight =
            InfluenceWeight(*item.anchor, result.grid.latitudes[y],
                            result.grid.longitudes[x], kind);
        if (weight <= 0.0) continue;
        const double gain = IsShallowWaterConstituent(name)
                                ? item.anchor->shallow_correction_gain
                                : item.anchor->major_correction_gain;
        correction += weight * gain * innovation->second;
        correction_weight_sum += weight;
      }
      result.coefficients_m[output_index * points + point] =
          correction_weight_sum > 0.0
              ? value + correction / correction_weight_sum
              : value;
    }
  }
  result.Validate();
  return result;
}

}  // namespace xtidal::authoring
