#pragma once

#include <string>
#include <vector>

#include "environmental_grib/xtd_package.h"
#include "height_assimilation.h"

namespace xtidal::authoring {

struct HeightModelMember {
  std::string label;
  environmental_grib::TideHeightHarmonics harmonics;
};

struct HeightMosaicOptions {
  // Empirically calibrated on spatially independent TICON-3 gauges.  This is
  // a constituent-vector error envelope, not a vertical-datum uncertainty.
  double harmonic_error_floor_m{0.10};
  double model_spread_multiplier{2.10};
};

// Builds a deliberately conservative mosaic.  The first member is retained
// wherever it is valid; later independent members only fill coverage holes.
// Cross-model disagreement changes the uncertainty field, never the chosen
// coefficient.  All members must already be sampled on the same grid.
AssimilatedHeightField BuildConservativeHeightMosaic(
    const std::vector<HeightModelMember>& members,
    const HeightMosaicOptions& options = {});

}  // namespace xtidal::authoring
