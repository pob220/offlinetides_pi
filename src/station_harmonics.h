#pragma once

#include <complex>
#include <filesystem>
#include <map>
#include <string>

namespace xtidal::authoring {

struct StationHarmonicConstants {
  std::string name;
  double chart_datum_reference_m{};
  std::map<std::string, std::complex<double>> coefficients_m;
};

// Reads one station from OpenCPN's text harmonic catalogue and converts its
// constants to the same complex ATLAS convention used by XTD water levels.
StationHarmonicConstants LoadStationHarmonicConstants(
    const std::filesystem::path& source, const std::string& station_prefix,
    int calibration_year);

}  // namespace xtidal::authoring
