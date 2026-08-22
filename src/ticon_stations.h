#pragma once

#include <complex>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace xtidal::authoring {

struct HarmonicObservation {
  std::string id;
  std::string gauge_type;
  double latitude{};
  double longitude{};
  double valid_fraction{};
  std::size_t observation_count{};
  double maximum_gap_days{};
  std::map<std::string, std::complex<double>> coefficients_m;
  std::map<std::string, double> complex_standard_deviation_m;
};

struct Ticon3Catalogue {
  std::string source_doi;
  std::string source_license;
  std::string input_sha256;
  std::vector<HarmonicObservation> stations;
};

// Loads the normalized, authoring-only JSON produced by import_ticon3.py.
// The runtime plugin never calls this and never reads TICON source data.
Ticon3Catalogue LoadTicon3Catalogue(const std::filesystem::path& path);

}  // namespace xtidal::authoring
