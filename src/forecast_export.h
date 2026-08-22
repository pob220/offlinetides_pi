#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "xtidal_prediction.h"

namespace xtidal {

enum class ForecastValueState { kKnown, kUnknown, kMasked };

struct ForecastExportSample {
  TimePoint valid_time_utc;
  ForecastValueState state{ForecastValueState::kUnknown};
  std::optional<double> water_level_m;
};

struct ForecastExportDocument {
  std::string schema{"xtidal-water-level-forecast"};
  std::uint32_t schema_version{1};
  TimePoint generated_utc;
  double latitude_wgs84{};
  double longitude_wgs84{};
  std::string vertical_datum_id;
  std::string vertical_datum_name;
  std::string vertical_datum_epoch;
  std::string prediction_method{"harmonic-astronomical"};
  std::optional<double> uncertainty_m;
  std::string package_id;
  std::string source_id;
  std::string source_name;
  std::vector<ForecastExportSample> samples;
};

[[nodiscard]] ForecastExportDocument BuildForecastExport(
    const HeightCurve& curve, const PackageInfo& package, TimePoint generated);
[[nodiscard]] std::string SerializeForecastJson(
    const ForecastExportDocument& document);
[[nodiscard]] std::string SerializeForecastCsv(
    const ForecastExportDocument& document);
[[nodiscard]] const char* ForecastValueStateName(ForecastValueState state);

}  // namespace xtidal
