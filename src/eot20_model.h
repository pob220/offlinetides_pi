#pragma once

#include <filesystem>

#include "environmental_grib/xtd_package.h"

namespace xtidal::authoring {

// Loads the open EOT20 ocean-tide NetCDF atlas for offline authoring.  The
// returned grid is normalized to WGS84 longitudes [-180, 180].  Raw EOT20 is
// never used by the runtime plugin.
environmental_grib::TideHeightHarmonics LoadEot20HeightModel(
    const std::filesystem::path& ocean_tide_directory,
    double imaginary_sign = -1.0);

environmental_grib::TideHeightHarmonics LoadHamtide11aHeightModel(
    const std::filesystem::path& ocean_tide_directory,
    double imaginary_sign = -1.0);

environmental_grib::TideHeightHarmonics LoadGtsm41HeightModel(
    const std::filesystem::path& ocean_tide_directory,
    double imaginary_sign = -1.0);

}  // namespace xtidal::authoring
