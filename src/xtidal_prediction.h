#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "environmental_grib/xtd_package.h"

namespace xtidal {

using TimePoint = environmental_grib::TimePoint;
using CurrentGrid = environmental_grib::CurrentGrid;
using OfflineCurrentMode = environmental_grib::OfflineCurrentMode;

struct PackageInfo {
  std::filesystem::path path;
  std::uint32_t format_version{};
  bool authenticated{};
  bool tide_available{};
  bool expected_current_available{};
  bool height_available{};
  bool height_quality_available{};
  bool vertical_datum_available{};
  Json::Value metadata;
  std::string height_datum_id;
  std::string height_datum_name;
  std::string package_id;
};

struct CurrentSample {
  double east_mps{};
  double north_mps{};
  double speed_knots{};
  double direction_degrees_true{};
};

struct HeightSample {
  TimePoint time;
  double height_m{};
};

struct HeightQualitySample {
  double harmonic_sigma_m{};
  double datum_sigma_m{};
  double nearest_observation_distance_km{};
  std::uint8_t support_class{};
  std::uint16_t observation_count{};
};

enum class HeightReference { kChartDatum, kModelMeanSeaLevel };

struct VerticalDatumSample {
  double offset_m{};
  double uncertainty_m{};
  double nearest_station_distance_km{};
  std::uint8_t realization_class{};
  std::uint8_t support_class{};
  std::uint16_t station_count{};
  std::string target_datum_id;
  std::string target_datum_name;
  std::string epoch;
};

[[nodiscard]] std::string VerticalDatumSupportDescription(
    const VerticalDatumSample& datum);
[[nodiscard]] std::string VerticalDatumRealizationDescription(
    const VerticalDatumSample& datum);

struct DisplayTime {
  int year{};
  unsigned month{};
  unsigned day{};
  unsigned hour{};
  unsigned minute{};
  unsigned second{};
  int utc_offset_minutes{};
  std::string abbreviation;
};

struct ForecastDate {
  int year{};
  unsigned month{};
  unsigned day{};
};

struct ForecastWindow {
  TimePoint start;
  TimePoint end;
  TimePoint selected_time;
};

// Converts a UTC prediction timestamp for display only.  `system-local`
// selects the operating-system zone; all other values are IANA zone names.
// Prediction, matching and package timestamps always remain UTC.
[[nodiscard]] DisplayTime ConvertTimeForDisplay(
    TimePoint time, const std::string& time_zone_id);

// Build the rolling view used by "Next tides" and a whole local-calendar-day
// view used by the date picker.  Local-day conversion is deliberately kept in
// the prediction layer so daylight-saving changes produce 23/25-hour windows
// while package sampling remains UTC.
[[nodiscard]] ForecastWindow BuildNextTidesWindow(TimePoint selected_time);
[[nodiscard]] ForecastWindow BuildLocalDateWindow(
    ForecastDate date, const std::string& time_zone_id);

// Parse a WGS84 latitude or longitude in signed decimal degrees.  Optional
// N/S/E/W prefixes or suffixes and a degree symbol are accepted.
[[nodiscard]] double ParseCoordinate(const std::string& text, bool latitude);

[[nodiscard]] std::string HeightSupportDescription(
    const HeightQualitySample& quality);

struct HeightCurve {
  double latitude{};
  double longitude{};
  std::string datum_id;
  std::string datum_name;
  std::vector<HeightSample> samples;
  std::optional<HeightQualitySample> quality;
  std::optional<VerticalDatumSample> vertical_datum;
};

[[nodiscard]] bool IsChartDatum(const HeightCurve& curve);
[[nodiscard]] std::string HeightReferencePhrase(const HeightCurve& curve);
[[nodiscard]] std::string FormatHeightForDisplay(const HeightCurve& curve,
                                                 double height_m);
// Linearly sample an already-computed height curve at an arbitrary UTC time.
// Returns unknown outside the curve or when fewer than two ordered samples are
// available.  This keeps graph inspection deterministic and GUI-independent.
[[nodiscard]] std::optional<HeightSample> InterpolateHeightSample(
    const HeightCurve& curve, TimePoint time);

enum class HeightEventType { kLowWater, kHighWater };

struct HeightEvent {
  HeightEventType type{};
  TimePoint time;
  double height_m{};
};

struct HeightEventMatch {
  HeightEvent reference;
  HeightEvent predicted;
  double signed_time_error_minutes{};
  double signed_height_error_m{};
};

struct HeightValidationMetrics {
  std::size_t reference_events{};
  std::size_t matched_events{};
  double mean_absolute_time_error_minutes{};
  double maximum_absolute_time_error_minutes{};
  double mean_absolute_height_error_m{};
  double maximum_absolute_height_error_m{};
  std::vector<HeightEventMatch> matches;
};

class PredictionService {
public:
  void Load(const std::filesystem::path& path);
  void Unload();

  [[nodiscard]] bool loaded() const noexcept { return reader_ != nullptr; }
  [[nodiscard]] const PackageInfo& package() const;

  CurrentGrid PredictGrid(const std::vector<double>& latitudes,
                          const std::vector<double>& longitudes, TimePoint time,
                          OfflineCurrentMode mode);
  std::optional<CurrentSample> PredictPoint(double latitude, double longitude,
                                            TimePoint time,
                                            OfflineCurrentMode mode);
  std::optional<double> PredictHeight(double latitude, double longitude,
                                      TimePoint time);
  std::optional<HeightQualitySample> PredictHeightQuality(double latitude,
                                                          double longitude);
  std::optional<VerticalDatumSample> PredictVerticalDatum(double latitude,
                                                          double longitude);
  HeightCurve PredictHeightCurve(
      double latitude, double longitude, TimePoint start,
      std::chrono::seconds duration,
      std::chrono::minutes step = std::chrono::minutes{10},
      HeightReference reference = HeightReference::kModelMeanSeaLevel);
  static std::vector<HeightEvent> FindHeightEvents(const HeightCurve& curve);
  static HeightValidationMetrics CompareHeightEvents(
      const std::vector<HeightEvent>& predicted,
      const std::vector<HeightEvent>& reference,
      std::chrono::minutes maximum_time_difference = std::chrono::hours{4});

private:
  std::unique_ptr<environmental_grib::XtdPackageReader> reader_;
  PackageInfo package_;
};

}  // namespace xtidal
