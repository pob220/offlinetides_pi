#pragma once

#include <optional>
#include <string>

#include "xtidal_prediction.h"

namespace xtidal {

struct NativeTideStation {
  int index{};
  std::string name;
  double latitude{};
  double longitude{};
  std::string source_name;
  std::string source_dataset_id;
  std::string source_dataset_version;
  std::string source_description;
  std::string stable_id;
  std::string reference_name;
  std::string datum_id;
  std::string datum_name;
  std::string datum_equivalence_key;
  int datum_status{};
  bool datum_approximate{};
  bool subordinate{};
  bool height_in_metres_available{};
  std::optional<double> datum_offset_m;
};

enum class NativeCurveMode {
  kAbsoluteSameDatum,
  kZ0Transformed,
  kMeanAligned
};

struct NativeTideComparison {
  NativeTideStation station;
  std::string xtidal_datum_id;
  std::string xtidal_datum_name;
  double distance_km{};
  NativeCurveMode curve_mode{NativeCurveMode::kMeanAligned};
  double applied_vertical_shift_m{};
  HeightCurve native_curve;
  std::size_t matched_events{};
  std::optional<double> mean_absolute_event_time_difference_minutes;
  bool height_difference_comparable{};
  std::optional<double> mean_absolute_height_difference_m;
  std::string height_comparison_reason;
};

[[nodiscard]] double GreatCircleDistanceKm(double first_latitude,
                                           double first_longitude,
                                           double second_latitude,
                                           double second_longitude);
[[nodiscard]] bool IsNativeStationWithinDistance(
    const HeightCurve& xtidal_curve, const NativeTideStation& station,
    double maximum_distance_nm);
[[nodiscard]] NativeTideComparison CompareNativeTideStation(
    const HeightCurve& xtidal_curve, NativeTideStation station,
    HeightCurve native_curve);
[[nodiscard]] std::string NativeCurveModeDescription(NativeCurveMode mode);

}  // namespace xtidal
