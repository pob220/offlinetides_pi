#include "native_tide_comparison.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace xtidal {
namespace {

std::string XtidalDatumEquivalence(const HeightCurve& curve) {
  if (curve.datum_id == "model-mean-sea-level") return "MSL";
  if (!curve.vertical_datum) return {};
  switch (curve.vertical_datum->realization_class) {
    case 1:
      return "LAT";
    case 2:
      return "MLLW";
    case 3:
      return "LLWLT";
    case 4:
      return "NLLW";
    case 5:
      return "MLWS";
    case 6:
      return "MSL";
    case 7:
      return "TLT";
    case 8:
      return "CHART_DATUM";
    default:
      return {};
  }
}

std::optional<double> MeanAlignmentShift(const HeightCurve& target,
                                         const HeightCurve& source) {
  double difference_sum = 0.0;
  std::size_t count = 0;
  for (const auto& sample : source.samples) {
    const auto target_sample = InterpolateHeightSample(target, sample.time);
    if (!target_sample) continue;
    difference_sum += target_sample->height_m - sample.height_m;
    ++count;
  }
  if (!count) return std::nullopt;
  return difference_sum / static_cast<double>(count);
}

}  // namespace

double GreatCircleDistanceKm(double first_latitude, double first_longitude,
                             double second_latitude, double second_longitude) {
  constexpr double radius_km = 6371.0088;
  const auto radians = [](double degrees) {
    return degrees * std::numbers::pi / 180.0;
  };
  const double dlat = radians(second_latitude - first_latitude);
  const double dlon = radians(second_longitude - first_longitude);
  const double a = std::pow(std::sin(dlat / 2.0), 2) +
                   std::cos(radians(first_latitude)) *
                       std::cos(radians(second_latitude)) *
                       std::pow(std::sin(dlon / 2.0), 2);
  return radius_km * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

bool IsNativeStationWithinDistance(const HeightCurve& xtidal_curve,
                                   const NativeTideStation& station,
                                   double maximum_distance_nm) {
  constexpr double kilometres_per_nautical_mile = 1.852;
  return std::isfinite(maximum_distance_nm) && maximum_distance_nm >= 0.0 &&
         GreatCircleDistanceKm(xtidal_curve.latitude, xtidal_curve.longitude,
                               station.latitude, station.longitude) <=
             maximum_distance_nm * kilometres_per_nautical_mile;
}

NativeTideComparison CompareNativeTideStation(const HeightCurve& xtidal_curve,
                                              NativeTideStation station,
                                              HeightCurve native_curve) {
  NativeTideComparison result;
  result.xtidal_datum_id = xtidal_curve.datum_id;
  result.xtidal_datum_name = xtidal_curve.datum_name;
  result.distance_km =
      GreatCircleDistanceKm(xtidal_curve.latitude, xtidal_curve.longitude,
                            station.latitude, station.longitude);
  result.station = std::move(station);
  result.native_curve = std::move(native_curve);
  const std::string xtidal_equivalence = XtidalDatumEquivalence(xtidal_curve);
  const bool exact_datum =
      result.station.datum_status == 1 && !result.station.datum_approximate &&
      !result.station.subordinate && !xtidal_equivalence.empty() &&
      xtidal_equivalence == result.station.datum_equivalence_key;
  if (exact_datum) {
    result.curve_mode = NativeCurveMode::kAbsoluteSameDatum;
  } else if (result.station.datum_offset_m &&
             std::isfinite(*result.station.datum_offset_m) &&
             result.station.height_in_metres_available &&
             !result.station.subordinate &&
             (xtidal_curve.datum_id == "model-mean-sea-level" ||
              xtidal_curve.vertical_datum)) {
    const double target_offset = xtidal_curve.vertical_datum
                                     ? xtidal_curve.vertical_datum->offset_m
                                     : 0.0;
    result.curve_mode = NativeCurveMode::kZ0Transformed;
    result.applied_vertical_shift_m =
        target_offset - *result.station.datum_offset_m;
  } else {
    result.curve_mode = NativeCurveMode::kMeanAligned;
    result.applied_vertical_shift_m =
        MeanAlignmentShift(xtidal_curve, result.native_curve).value_or(0.0);
  }
  for (auto& sample : result.native_curve.samples)
    sample.height_m += result.applied_vertical_shift_m;
  if (result.curve_mode != NativeCurveMode::kMeanAligned) {
    result.native_curve.datum_id = xtidal_curve.datum_id;
    result.native_curve.datum_name = xtidal_curve.datum_name;
  } else {
    result.native_curve.datum_id = "aligned-comparison";
    result.native_curve.datum_name = "Vertically aligned comparison";
  }
  const auto xtidal_events = PredictionService::FindHeightEvents(xtidal_curve);
  const auto native_events =
      PredictionService::FindHeightEvents(result.native_curve);
  const auto metrics = PredictionService::CompareHeightEvents(
      xtidal_events, native_events, std::chrono::hours{4});
  result.matched_events = metrics.matched_events;
  if (metrics.matched_events)
    result.mean_absolute_event_time_difference_minutes =
        metrics.mean_absolute_time_error_minutes;
  result.height_difference_comparable =
      result.curve_mode != NativeCurveMode::kMeanAligned;
  if (result.height_difference_comparable && metrics.matched_events)
    result.mean_absolute_height_difference_m =
        metrics.mean_absolute_height_error_m;
  else if (result.curve_mode == NativeCurveMode::kMeanAligned)
    result.height_comparison_reason =
        "suppressed: native curve is vertically aligned for timing/range "
        "comparison because absolute datum equivalence is unavailable";
  else
    result.height_comparison_reason =
        "unavailable: no matched extrema are present";
  return result;
}

std::string NativeCurveModeDescription(NativeCurveMode mode) {
  switch (mode) {
    case NativeCurveMode::kAbsoluteSameDatum:
      return "absolute heights use the same declared vertical datum";
    case NativeCurveMode::kZ0Transformed:
      return "native TCD Z0 transformed to the OfflineTides display datum; "
             "this is not an independent validation of the OfflineTides "
             "datum field";
    case NativeCurveMode::kMeanAligned:
      return "native curve vertically aligned for timing/range comparison — "
             "absolute height comparison unavailable because datum "
             "equivalence is unknown";
  }
  return "unknown comparison mode";
}

}  // namespace xtidal
