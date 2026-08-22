#pragma once

#include <cstdint>
#include <vector>

#include "environmental_grib/xtd_package.h"
#include "ticon_stations.h"

namespace xtidal::authoring {

enum class HeightSupportClass : std::uint8_t {
  kUnknown = 0,
  kBackgroundOnly = 1,
  kObservationConstrained = 2,
  kEstuaryObservationConstrained = 3,
  kIndependentModelFallback = 4,
};

struct HeightAssimilationOptions {
  double maximum_influence_km{350.0};
  double river_maximum_influence_km{100.0};
  double full_strength_radius_km{10.0};
  // A harmonic observation is authoritative at its surrounding interpolation
  // nodes.  The
  // residual_gain below applies once a correction is transferred away from
  // that anchor; the gain is blended between these values inside the
  // full-strength radius.
  double observation_anchor_gain{1.0};
  double residual_gain{0.50};
  double maximum_station_snap_km{75.0};
  double background_only_sigma_m{0.50};
  std::vector<double> background_sigma_by_point_m;
  std::vector<HeightSupportClass> background_support_class_by_point;
  double observation_sigma_floor_m{0.05};
  double distance_sigma_m{0.20};
};

struct HeightAssimilationQuality {
  std::vector<HeightSupportClass> support_class;
  std::vector<double> nearest_observation_distance_km;
  std::vector<double> harmonic_sigma_m;
  std::vector<std::uint16_t> contributing_observations;
};

struct AssimilatedHeightField {
  environmental_grib::TideHeightHarmonics harmonics;
  HeightAssimilationQuality quality;

  void Validate() const;
};

// Assimilates complex harmonic innovations into an existing global
// background. The nearest same-regime residual is smoothly tapered to zero at
// the support boundary measured on the connected water graph. River/estuary
// and coastal residual domains are separate, and observations cannot
// influence cells across intervening land.
AssimilatedHeightField AssimilateHarmonicObservations(
    const environmental_grib::TideHeightHarmonics& background,
    const std::vector<HarmonicObservation>& observations,
    const HeightAssimilationOptions& options = {});

}  // namespace xtidal::authoring
