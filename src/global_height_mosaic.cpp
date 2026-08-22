#include "global_height_mosaic.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace xtidal::authoring {
namespace {

bool SameGrid(const environmental_grib::RegularGrid& first,
              const environmental_grib::RegularGrid& second) {
  return first.latitudes == second.latitudes &&
         first.longitudes == second.longitudes;
}

bool ValidPoint(const environmental_grib::TideHeightHarmonics& field,
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

std::map<std::string, std::size_t> Indices(
    const environmental_grib::TideHeightHarmonics& field) {
  std::map<std::string, std::size_t> result;
  for (std::size_t index = 0; index < field.constituents.size(); ++index)
    result.emplace(field.constituents[index], index);
  return result;
}

}  // namespace

AssimilatedHeightField BuildConservativeHeightMosaic(
    const std::vector<HeightModelMember>& members,
    const HeightMosaicOptions& options) {
  if (members.empty())
    throw std::invalid_argument("height mosaic needs at least one member");
  if (!std::isfinite(options.harmonic_error_floor_m) ||
      options.harmonic_error_floor_m < 0.0 ||
      !std::isfinite(options.model_spread_multiplier) ||
      options.model_spread_multiplier < 0.0)
    throw std::invalid_argument("height mosaic options are invalid");
  for (const auto& member : members) {
    if (member.label.empty())
      throw std::invalid_argument("height mosaic member label is empty");
    member.harmonics.Validate();
    if (!SameGrid(members.front().harmonics.grid, member.harmonics.grid))
      throw std::invalid_argument("height mosaic member grids differ");
    if (member.harmonics.datum_id != members.front().harmonics.datum_id)
      throw std::invalid_argument("height mosaic member datums differ");
  }

  AssimilatedHeightField result;
  result.harmonics = members.front().harmonics;
  const auto points = result.harmonics.grid.size();
  if (result.harmonics.mask.empty())
    result.harmonics.mask.assign(points, 0);
  const auto output_indices = Indices(result.harmonics);
  std::vector<std::map<std::string, std::size_t>> member_indices;
  std::vector<std::vector<std::uint8_t>> member_validity;
  member_indices.reserve(members.size());
  member_validity.reserve(members.size());
  for (const auto& member : members) {
    member_indices.push_back(Indices(member.harmonics));
    std::vector<std::uint8_t> valid(points);
    for (std::size_t point = 0; point < points; ++point)
      valid[point] = ValidPoint(member.harmonics, point);
    member_validity.push_back(std::move(valid));
  }

  result.quality.support_class.assign(points, HeightSupportClass::kUnknown);
  result.quality.nearest_observation_distance_km.assign(
      points, std::numeric_limits<double>::infinity());
  result.quality.harmonic_sigma_m.assign(
      points, options.harmonic_error_floor_m);
  result.quality.contributing_observations.assign(points, 0);
  static const std::set<std::string> spread_constituents{
      "m2", "s2", "n2", "k2", "k1", "o1", "p1", "q1"};

  for (std::size_t point = 0; point < points; ++point) {
    std::size_t selected = members.size();
    for (std::size_t member = 0; member < members.size(); ++member) {
      if (member_validity[member][point]) {
        selected = member;
        break;
      }
    }
    if (selected == members.size()) {
      result.harmonics.mask[point] = 1;
      continue;
    }
    result.harmonics.mask[point] = 0;
    result.quality.support_class[point] =
        selected == 0 ? HeightSupportClass::kBackgroundOnly
                      : HeightSupportClass::kIndependentModelFallback;
    const auto selected_points = members[selected].harmonics.grid.size();
    for (const auto& [name, output_index] : output_indices) {
      const auto source = member_indices[selected].find(name);
      result.harmonics.coefficients_m[output_index * points + point] =
          source == member_indices[selected].end()
              ? std::complex<double>{}
              : members[selected].harmonics.coefficients_m[
                    source->second * selected_points + point];
    }

    double spread_squared = 0.0;
    for (const auto& name : spread_constituents) {
      std::vector<std::complex<double>> values;
      for (std::size_t member = 0; member < members.size(); ++member) {
        if (!member_validity[member][point]) continue;
        const auto found = member_indices[member].find(name);
        if (found == member_indices[member].end()) continue;
        values.push_back(members[member].harmonics.coefficients_m[
            found->second * points + point]);
      }
      if (values.size() < 2) continue;
      std::complex<double> mean{};
      for (const auto value : values) mean += value;
      mean /= static_cast<double>(values.size());
      double constituent_spread = 0.0;
      for (const auto value : values)
        constituent_spread += std::norm(value - mean);
      spread_squared += constituent_spread / values.size();
    }
    result.quality.harmonic_sigma_m[point] =
        options.harmonic_error_floor_m +
        options.model_spread_multiplier * std::sqrt(spread_squared);
  }
  result.Validate();
  return result;
}

}  // namespace xtidal::authoring
