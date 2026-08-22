#pragma once

#include <complex>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "environmental_grib/xtd_package.h"

namespace xtidal::authoring {

struct HarmonicAnchor {
  std::string id;
  std::string name;
  double latitude{};
  double longitude{};
  double support_radius_km{};
  double axis_bearing_degrees{};
  double major_along_support_km{};
  double major_cross_support_km{};
  double shallow_along_support_km{};
  double shallow_cross_support_km{};
  double datum_along_support_km{};
  double datum_cross_support_km{};
  double quality_weight{1.0};
  double major_correction_gain{1.0};
  double shallow_correction_gain{1.0};
  double prediction_time_shift_minutes{};
  double amplitude_scale{1.0};
  double chart_datum_offset_m{};
  double chart_datum_reference_m{};
  std::string station_class{"open-coast"};
  std::string hydro_region;
  // Optional [longitude, latitude] polygon.  This provides a hard land or
  // hydrographic-reach boundary around an otherwise smooth kernel.
  std::vector<std::pair<double, double>> support_polygon;
  std::map<std::string, std::complex<double>> coefficients_m;
};

struct CorrectedHeightField {
  environmental_grib::RegularGrid grid;
  std::vector<std::string> constituents;
  std::vector<std::complex<double>> coefficients_m;
  std::vector<std::uint8_t> valid;
  std::vector<double> nearest_anchor_distance_km;
  std::vector<std::string> anchor_ids;

  void Validate() const;
};

// Produces a Chart-Datum field only where an anchor's bounded support applies.
// Harmonic innovations are interpolated as complex values, never as separate
// amplitudes and phase angles.  Unsupported points remain invalid.
CorrectedHeightField ApplyLocalHarmonicCorrections(
    const environmental_grib::TideHeightHarmonics& background,
    const std::vector<HarmonicAnchor>& anchors);

double GreatCircleDistanceKm(double latitude_a, double longitude_a,
                             double latitude_b, double longitude_b);

[[nodiscard]] bool IsShallowWaterConstituent(const std::string& name);

}  // namespace xtidal::authoring
