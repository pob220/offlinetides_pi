#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <json/json.h>

#include "xtd_test_support.h"
#include "global_height_mosaic.h"
#include "forecast_export.h"
#include "height_assimilation.h"
#include "height_correction.h"
#include "native_tide_comparison.h"
#include "ticon_stations.h"
#include "vertical_datum.h"
#include "xtidal_prediction.h"

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main() {
  namespace eg = environmental_grib;
  namespace test = environmental_grib::test;
  const auto root =
      std::filesystem::temp_directory_path() / "xtidal-first-version-tests";
  std::filesystem::create_directories(root);
  const auto v1 = root / "tide-only.xtd";
  test::XtdFixtureOptions options;
  options.metadata["dataset_name"] = "X-Tidal test fixture";
  test::WriteXtdFixture(v1, options);

  xtidal::PredictionService service;
  service.Load(v1);
  Require(service.loaded(), "service did not retain loaded package");
  Require(service.package().authenticated, "package was not authenticated");
  Require(service.package().tide_available, "tide component unavailable");
  Require(!service.package().expected_current_available,
          "v1 fixture unexpectedly has expected-current data");

  const eg::TimePoint time{std::chrono::seconds{1'767'225'600}};
  const auto london_summer = xtidal::ConvertTimeForDisplay(
      eg::ParseUtcDateTime("2026-08-21T21:33:08Z"), "Europe/London");
  Require(london_summer.year == 2026 && london_summer.month == 8 &&
              london_summer.day == 21 && london_summer.hour == 22 &&
              london_summer.minute == 33 && london_summer.second == 8 &&
              london_summer.utc_offset_minutes == 60 &&
              london_summer.abbreviation == "BST",
          "Europe/London summer display did not apply BST");
  const auto london_winter = xtidal::ConvertTimeForDisplay(
      eg::ParseUtcDateTime("2026-01-21T21:33:08Z"), "Europe/London");
  Require(london_winter.hour == 21 && london_winter.utc_offset_minutes == 0 &&
              london_winter.abbreviation == "GMT",
          "Europe/London winter display did not apply GMT");
  const auto utc_display = xtidal::ConvertTimeForDisplay(
      eg::ParseUtcDateTime("2026-08-21T21:33:08Z"), "UTC");
  Require(utc_display.hour == 21 && utc_display.utc_offset_minutes == 0 &&
              utc_display.abbreviation == "UTC",
          "UTC display conversion changed the prediction timestamp");
  bool invalid_zone_rejected = false;
  try {
    (void)xtidal::ConvertTimeForDisplay(time, "Not/A_Time_Zone");
  } catch (const std::invalid_argument&) {
    invalid_zone_rejected = true;
  }
  Require(invalid_zone_rejected, "invalid display time zone was accepted");
  const auto next_window = xtidal::BuildNextTidesWindow(time);
  Require(next_window.start == time - std::chrono::hours{24} &&
              next_window.end == time + std::chrono::hours{24} &&
              next_window.selected_time == time,
          "next-tides forecast window changed the rolling 48-hour view");
  const auto london_date =
      xtidal::BuildLocalDateWindow({2026, 8, 21}, "Europe/London");
  Require(london_date.start == eg::ParseUtcDateTime("2026-08-20T23:00:00Z") &&
              london_date.end == eg::ParseUtcDateTime("2026-08-21T23:00:00Z") &&
              london_date.selected_time ==
                  eg::ParseUtcDateTime("2026-08-21T11:00:00Z"),
          "selected London summer date was not converted through BST");
  const auto spring_date =
      xtidal::BuildLocalDateWindow({2026, 3, 29}, "Europe/London");
  Require(spring_date.end - spring_date.start == std::chrono::hours{23},
          "spring daylight-saving date was not a 23-hour local day");
  const auto autumn_date =
      xtidal::BuildLocalDateWindow({2026, 10, 25}, "Europe/London");
  Require(autumn_date.end - autumn_date.start == std::chrono::hours{25},
          "autumn daylight-saving date was not a 25-hour local day");
  Require(
      std::abs(xtidal::ParseCoordinate("53.33149 N", true) - 53.33149) < 1e-8 &&
          std::abs(xtidal::ParseCoordinate("4.61325 W", false) + 4.61325) <
              1e-8 &&
          std::abs(xtidal::ParseCoordinate("-4.61325\xC2\xB0 W", false) +
                   4.61325) < 1e-8,
      "manual WGS84 coordinate parser changed valid coordinates");
  bool invalid_coordinate_rejected = false;
  try {
    (void)xtidal::ParseCoordinate("91 N", true);
  } catch (const std::invalid_argument&) {
    invalid_coordinate_rejected = true;
  }
  Require(invalid_coordinate_rejected,
          "manual coordinate parser accepted latitude outside WGS84 bounds");
  invalid_coordinate_rejected = false;
  try {
    (void)xtidal::ParseCoordinate("12 E", true);
  } catch (const std::invalid_argument&) {
    invalid_coordinate_rejected = true;
  }
  Require(
      invalid_coordinate_rejected,
      "manual coordinate parser accepted a longitude hemisphere as latitude");
  const auto sample = service.PredictPoint(
      0.0, 90.0, time, eg::OfflineCurrentMode::kAstronomicalTideOnly);
  Require(sample.has_value(), "covered point returned unknown");
  Require(sample->speed_knots > 0.0, "covered point has no current speed");
  const auto missing = service.PredictPoint(
      80.0, 90.0, time, eg::OfflineCurrentMode::kAstronomicalTideOnly);
  Require(!missing.has_value(), "outside point was not fail-closed");

  bool rejected = false;
  try {
    (void)service.PredictPoint(
        0.0, 90.0, time,
        eg::OfflineCurrentMode::kTideAndExpectedSeasonalCirculation);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "missing expected-current component silently fell back");

  const auto height_only = root / "height-only.xtdt";
  test::XtdV2FixtureOptions height_options;
  height_options.include_tide = false;
  height_options.include_residual = false;
  height_options.include_uncertainty = false;
  height_options.include_height = true;
  height_options.include_height_quality = true;
  height_options.include_vertical_datum = true;
  height_options.outer_metadata["package_profile"] = "xtidal-tide-v1";
  height_options.outer_metadata["recommended_extension"] = ".xtdt";
  height_options.height_reference_level_m = 2.0;
  height_options.height_datum_id = "model-mean-sea-level";
  height_options.height_datum_name = "Model mean sea level";
  test::WriteXtdV2Fixture(height_only, height_options);
  service.Load(height_only);
  Require(service.package().authenticated,
          "height-only package was not authenticated");
  Require(service.package().height_available,
          "height-only package lacks height capability");
  Require(service.package().height_quality_available,
          "height-only .xtdt lacks its quality capability");
  Require(service.package().vertical_datum_available,
          "height-only .xtdt lacks its vertical-datum capability");
  Require(service.package().metadata["package_profile"].asString() ==
              "xtidal-tide-v1",
          "X-Tidal profile metadata was not retained");
  Require(!service.package().tide_available,
          "height-only package falsely advertises currents");
  const auto curve = service.PredictHeightCurve(
      0.0, 90.0, time, std::chrono::hours{30}, std::chrono::minutes{5},
      xtidal::HeightReference::kChartDatum);
  Require(curve.samples.size() == 361,
          "height-only package did not produce the requested curve");
  Require(curve.quality.has_value(),
          "height curve did not carry local quality information");
  Require(std::abs(curve.quality->harmonic_sigma_m - 0.20) < 0.001 &&
              std::abs(curve.quality->datum_sigma_m - 0.12) < 0.001 &&
              std::abs(curve.quality->nearest_observation_distance_km - 25.0) <
                  0.051,
          "height curve quality fields are incorrect");
  Require(xtidal::HeightSupportDescription(*curve.quality) ==
              "background model only",
          "height support description is incorrect");
  Require(curve.datum_id == "chart-datum",
          "height curve lost its vertical datum");
  const auto events = xtidal::PredictionService::FindHeightEvents(curve);
  Require(!events.empty(), "height curve did not expose extrema");
  Require(xtidal::IsChartDatum(curve),
          "Chart Datum curve was not recognised for display");
  Require(
      xtidal::FormatHeightForDisplay(curve, 5.42) == "5.42 m above Chart Datum",
      "Chart Datum height display is ambiguous");
  xtidal::HeightCurve inspection_curve;
  inspection_curve.samples = {{time, 1.25},
                              {time + std::chrono::minutes{10}, 2.75}};
  const auto interpolated = xtidal::InterpolateHeightSample(
      inspection_curve, time + std::chrono::minutes{4});
  Require(interpolated.has_value() &&
              interpolated->time == time + std::chrono::minutes{4} &&
              std::abs(interpolated->height_m - 1.85) < 1e-12,
          "curve inspection did not interpolate height at the pointer time");
  Require(!xtidal::InterpolateHeightSample(inspection_curve,
                                           time - std::chrono::seconds{1})
                  .has_value() &&
              !xtidal::InterpolateHeightSample(
                   inspection_curve,
                   time + std::chrono::minutes{10} + std::chrono::seconds{1})
                   .has_value(),
          "curve inspection extrapolated beyond the displayed forecast");
  const auto export_document =
      xtidal::BuildForecastExport(curve, service.package(), time);
  const auto export_json = xtidal::SerializeForecastJson(export_document);
  Json::Value parsed_export;
  {
    Json::CharReaderBuilder export_parser;
    std::string export_errors;
    std::istringstream input(export_json);
    Require(Json::parseFromStream(export_parser, input, &parsed_export,
                                  &export_errors),
            "forecast JSON export is invalid");
  }
  Require(
      parsed_export["schema_version"].asInt() == 1 &&
          parsed_export["geometry"]["coordinate_reference_system"] == "WGS84" &&
          parsed_export["samples"][0]["valid_time_utc"].asString().back() ==
              'Z' &&
          parsed_export["samples"][0]["state"] == "known" &&
          parsed_export["vertical_datum"]["identifier"] == "chart-datum" &&
          parsed_export["prediction_method"] == "harmonic-astronomical" &&
          parsed_export["package"]["id"].isString(),
      "forecast JSON export lost S-104 mapping fields");
  const auto export_csv = xtidal::SerializeForecastCsv(export_document);
  Require(export_csv.find("schema_version,valid_time_utc") == 0 &&
              export_csv.find(",known,") != std::string::npos &&
              export_csv.find(",chart-datum,") != std::string::npos,
          "forecast CSV export lost its version, state or datum");
  auto unknown_export = export_document;
  unknown_export.samples = {
      {time, xtidal::ForecastValueState::kMasked, std::nullopt}};
  const auto unknown_json = xtidal::SerializeForecastJson(unknown_export);
  Require(
      unknown_json.find("\"state\" : \"masked\"") != std::string::npos &&
          unknown_json.find("\"water_level_m\" : null") != std::string::npos,
      "forecast export did not preserve masked water level state");

  xtidal::NativeTideStation native_station;
  native_station.index = 7;
  native_station.name = "Test native station";
  native_station.latitude = curve.latitude + 0.1;
  native_station.longitude = curve.longitude;
  native_station.source_name = "OpenCPN test harmonics";
  native_station.datum_id = "unknown";
  native_station.datum_name = "Not exposed by API 1.21";
  native_station.height_in_metres_available = true;
  Require(xtidal::IsNativeStationWithinDistance(curve, native_station, 100.0) &&
              !xtidal::IsNativeStationWithinDistance(curve, native_station,
                                                     5.0),
          "native station comparison distance limit is not enforced");
  auto comparison_curve = curve;
  comparison_curve.latitude = native_station.latitude;
  comparison_curve.datum_id = "unknown";
  comparison_curve.datum_name = native_station.datum_name;
  for (auto& comparison_sample : comparison_curve.samples)
    comparison_sample.time += std::chrono::minutes{10};
  const auto native_comparison = xtidal::CompareNativeTideStation(
      curve, native_station, comparison_curve);
  Require(
      native_comparison.distance_km > 11.0 &&
          native_comparison.distance_km < 11.2 &&
          native_comparison.matched_events > 0 &&
          native_comparison.mean_absolute_event_time_difference_minutes &&
          std::abs(
              *native_comparison.mean_absolute_event_time_difference_minutes -
              10.0) < 0.1 &&
          !native_comparison.height_difference_comparable &&
          !native_comparison.mean_absolute_height_difference_m &&
          native_comparison.curve_mode ==
              xtidal::NativeCurveMode::kMeanAligned,
      "native tide comparison did not enforce unknown-datum suppression");

  auto exact_station = native_station;
  exact_station.datum_id = "tcd:LAT";
  exact_station.datum_name = "Lowest Astronomical Tide";
  exact_station.datum_equivalence_key = "LAT";
  exact_station.datum_status = 1;
  exact_station.datum_approximate = false;
  const auto exact_comparison = xtidal::CompareNativeTideStation(
      curve, exact_station, curve);
  Require(exact_comparison.curve_mode ==
              xtidal::NativeCurveMode::kAbsoluteSameDatum &&
              exact_comparison.height_difference_comparable &&
              exact_comparison.mean_absolute_height_difference_m &&
              *exact_comparison.mean_absolute_height_difference_m < 1e-9,
          "matching declared native datum was not compared absolutely");

  auto transformed_station = exact_station;
  transformed_station.datum_id = "tcd:MLLW";
  transformed_station.datum_name = "Mean Lower Low Water";
  transformed_station.datum_equivalence_key = "MLLW";
  transformed_station.datum_offset_m = 1.5;
  auto transformed_curve = curve;
  const double expected_z0_shift =
      curve.vertical_datum->offset_m - *transformed_station.datum_offset_m;
  for (auto& sample : transformed_curve.samples)
    sample.height_m -= expected_z0_shift;
  const auto transformed_comparison = xtidal::CompareNativeTideStation(
      curve, transformed_station, transformed_curve);
  const auto transformed_failure =
      "native Z0 transform did not preserve absolute curve heights: shift=" +
      std::to_string(transformed_comparison.applied_vertical_shift_m) +
      ", target_offset=" +
      std::to_string(curve.vertical_datum
                         ? curve.vertical_datum->offset_m
                         : std::numeric_limits<double>::quiet_NaN()) +
      ", mean_height_error=" +
      (transformed_comparison.mean_absolute_height_difference_m
           ? std::to_string(
                 *transformed_comparison.mean_absolute_height_difference_m)
           : "unavailable");
  Require(
      transformed_comparison.curve_mode ==
              xtidal::NativeCurveMode::kZ0Transformed &&
          curve.vertical_datum &&
          std::abs(transformed_comparison.applied_vertical_shift_m -
                   expected_z0_shift) < 1e-9 &&
          transformed_comparison.mean_absolute_height_difference_m &&
          *transformed_comparison.mean_absolute_height_difference_m < 1e-6,
      transformed_failure.c_str());
  eg::XtdPackageReader height_reader(height_only);
  eg::RegularGrid sample_grid;
  sample_grid.latitudes = {0.0};
  sample_grid.longitudes = {90.0};
  const auto sampled_harmonics =
      height_reader.SampleHeightHarmonics(sample_grid);
  Require(sampled_harmonics.coefficients_m.size() ==
              sampled_harmonics.constituents.size(),
          "offline XTD harmonic sampling lost coefficients");
  Require(sampled_harmonics.datum_id == "model-mean-sea-level",
          "offline XTD harmonic sampling lost its datum");
  const auto sampled_datum = height_reader.SampleVerticalDatum(sample_grid);
  Require(std::abs(sampled_datum.offset_m.front() - 2.5) < 0.001 &&
              sampled_datum.target_datum_id == "chart-datum",
          "offline XTD vertical-datum sampling is incorrect");
  const auto native_curve = service.PredictHeightCurve(
      0.0, 90.0, time, std::chrono::hours{0}, std::chrono::minutes{5},
      xtidal::HeightReference::kModelMeanSeaLevel);
  Require(native_curve.datum_id == "model-mean-sea-level" &&
              std::abs(curve.samples.front().height_m -
                       native_curve.samples.front().height_m - 2.5) < 0.001,
          "Chart Datum display did not preserve the MSL prediction plus the "
          "authenticated transform");
  rejected = false;
  try {
    (void)service.PredictPoint(0.0, 90.0, time,
                               eg::OfflineCurrentMode::kAstronomicalTideOnly);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  Require(rejected, "height-only package manufactured a current field");

  const auto coastal_node = root / "exact-coastal-node.xtdt";
  test::XtdV2FixtureOptions coastal_options;
  coastal_options.include_tide = false;
  coastal_options.include_residual = false;
  coastal_options.include_uncertainty = false;
  coastal_options.include_height = true;
  coastal_options.valid = [](std::uint32_t x, std::uint32_t y) {
    return x == 0 && y == 0;
  };
  test::WriteXtdV2Fixture(coastal_node, coastal_options);
  service.Load(coastal_node);
  Require(service.PredictHeight(-2.0, 0.0, time).has_value(),
          "exact wet grid node incorrectly required adjacent land nodes");
  Require(!service.PredictHeight(-1.5, 0.0, time).has_value(),
          "interpolation across a wet/land edge did not fail closed");

  const auto combined = root / "height-and-streams.xtdt";
  test::XtdV2FixtureOptions combined_options;
  combined_options.include_height = true;
  combined_options.include_height_quality = true;
  combined_options.outer_metadata["package_profile"] = "xtidal-tide-v1";
  test::WriteXtdV2Fixture(combined, combined_options);
  service.Load(combined);
  Require(service.package().height_available &&
              service.package().tide_available &&
              service.package().expected_current_available,
          "combined .xtdt did not expose its independent components");

  const auto corrupt = root / "corrupt.xtd";
  std::filesystem::copy_file(v1, corrupt,
                             std::filesystem::copy_options::overwrite_existing);
  test::CorruptXtdFixtureByte(corrupt, 20);
  rejected = false;
  try {
    service.Load(corrupt);
  } catch (const std::exception&) {
    rejected = true;
  }
  Require(rejected, "corrupt package passed authentication");

  std::ifstream catalogue(XTIDAL_VALIDATION_LOCATIONS);
  Require(catalogue.good(), "validation-location catalogue is missing");
  Json::Value manifest;
  Json::CharReaderBuilder parser;
  std::string parse_errors;
  Require(Json::parseFromStream(parser, catalogue, &manifest, &parse_errors),
          "validation-location catalogue is invalid JSON");
  Require(manifest["schema_version"].asInt() == 1,
          "unexpected validation-location schema version");
  Require(manifest["locations"].isArray() && manifest["locations"].size() >= 8,
          "validation-location catalogue is too small");
  std::set<std::string> ids;
  bool has_liverpool = false;
  for (const auto& location : manifest["locations"]) {
    const auto id = location["id"].asString();
    Require(!id.empty() && ids.insert(id).second,
            "validation-location ids must be present and unique");
    Require(
        location["latitude"].isNumeric() && location["longitude"].isNumeric(),
        "validation location lacks coordinates");
    Require(location["products"].isArray() && !location["products"].empty(),
            "validation location lacks product coverage");
    has_liverpool = has_liverpool || id == "uk-liverpool-gladstone";
  }
  Require(has_liverpool, "Liverpool stress case is missing");

  std::ifstream correction_catalogue(XTIDAL_CORRECTION_CATALOGUE);
  Require(correction_catalogue.good(), "correction catalogue is missing");
  Json::Value corrections;
  Require(Json::parseFromStream(parser, correction_catalogue, &corrections,
                                &parse_errors),
          "correction catalogue is invalid JSON");
  Require(
      corrections["stations"].isArray() && corrections["stations"].size() >= 7,
      "correction catalogue is too small");
  bool correction_has_dover = false;
  for (const auto& station : corrections["stations"])
    correction_has_dover =
        correction_has_dover || station["id"].asString() == "uk-dover";
  Require(correction_has_dover, "Dover correction anchor is missing");

  std::ifstream correction_catalogue_v2(XTIDAL_CORRECTION_CATALOGUE_V2);
  Require(correction_catalogue_v2.good(), "v2 correction catalogue is missing");
  Json::Value corrections_v2;
  Require(Json::parseFromStream(parser, correction_catalogue_v2,
                                &corrections_v2, &parse_errors),
          "v2 correction catalogue is invalid JSON");
  Require(corrections_v2["schema_version"].asInt() == 2 &&
              corrections_v2["stations"].size() >= 15,
          "v2 correction catalogue is incomplete");
  std::size_t calibrated_stations = 0;
  bool has_estuary = false;
  for (const auto& station : corrections_v2["stations"]) {
    has_estuary =
        has_estuary || station["station_class"].asString() == "estuary";
    calibrated_stations += station.isMember("amplitude_scale");
    Require(station["major_support_km"].isObject() &&
                station["shallow_support_km"].isObject() &&
                station["datum_support_km"].isObject(),
            "v2 station does not separate correction and datum support");
  }
  Require(has_estuary && calibrated_stations == 5,
          "v2 station classification or accepted calibration set is wrong");

  std::ifstream dover_reference(XTIDAL_DOVER_REFERENCE);
  Require(dover_reference.good(), "frozen Dover reference is missing");
  Json::Value dover_manifest;
  Require(Json::parseFromStream(parser, dover_reference, &dover_manifest,
                                &parse_errors),
          "frozen Dover reference is invalid JSON");
  Require(dover_manifest["station"]["id"].asString() == "uk-dover" &&
              dover_manifest["events"].size() >= 3,
          "frozen Dover reference is incomplete");

  std::ifstream local_reference(XTIDAL_LOCAL_HOLYHEAD_REFERENCE);
  Require(local_reference.good(), "frozen local reference is missing");
  Json::Value local_manifest;
  Require(Json::parseFromStream(parser, local_reference, &local_manifest,
                                &parse_errors),
          "frozen local reference is invalid JSON");
  Require(local_manifest["reference"]["kind"].asString() ==
              "published_astronomical_forecast" &&
              local_manifest["reference"]["vertical_datum_id"].asString() ==
                  "chart-datum" &&
              local_manifest["events"].size() == 4,
          "frozen local reference is incomplete or datum-ambiguous");

  std::ifstream multiday_dover_reference(XTIDAL_MULTIDAY_DOVER_REFERENCE);
  Require(multiday_dover_reference.good(),
          "multi-day Dover reference is missing");
  Json::Value multiday_dover;
  Require(Json::parseFromStream(parser, multiday_dover_reference,
                                &multiday_dover, &parse_errors) &&
              multiday_dover["events"].size() >= 100,
          "multi-day Dover reference is incomplete");

  const std::complex<double> shift_fixture{0.8, -0.3};
  const auto shifted =
      eg::ShiftAtlasHarmonicCoefficient("m2", shift_fixture, 20.0 * 60.0);
  const auto restored =
      eg::ShiftAtlasHarmonicCoefficient("m2", shifted, -20.0 * 60.0);
  Require(std::abs(restored - shift_fixture) < 1e-12,
          "harmonic time translation is not reversible");

  eg::TideHeightHarmonics background;
  background.grid.latitudes = {50.0};
  background.grid.longitudes = {0.0, 0.1, 0.2};
  background.constituents = {"m2"};
  background.coefficients_m = {{1.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}};
  background.reference_level_m = 0.0;
  background.datum_id = "model-mean-sea-level";
  background.datum_name = "Model mean sea level";
  auto mosaic_primary = background;
  mosaic_primary.mask = {0, 1, 0};
  auto mosaic_fallback = background;
  mosaic_fallback.coefficients_m = {{1.2, 0.0}, {2.0, 0.5}, {0.8, 0.0}};
  const auto mosaic = xtidal::authoring::BuildConservativeHeightMosaic(
      {{"primary", mosaic_primary}, {"fallback", mosaic_fallback}});
  Require(
      mosaic.harmonics.coefficients_m[0] == mosaic_primary.coefficients_m[0] &&
          mosaic.harmonics.coefficients_m[1] ==
              mosaic_fallback.coefficients_m[1],
      "conservative mosaic changed valid primary data or missed a hole");
  Require(mosaic.quality.support_class[0] ==
                  xtidal::authoring::HeightSupportClass::kBackgroundOnly &&
              mosaic.quality.support_class[1] ==
                  xtidal::authoring::HeightSupportClass::
                      kIndependentModelFallback &&
              mosaic.quality.harmonic_sigma_m[0] > 0.10,
          "mosaic did not retain fallback identity and model spread");
  xtidal::authoring::HarmonicAnchor anchor;
  anchor.id = "test-anchor";
  anchor.name = "Test anchor";
  anchor.latitude = 50.0;
  anchor.longitude = 0.0;
  anchor.support_radius_km = 10.0;
  anchor.chart_datum_reference_m = 3.0;
  anchor.coefficients_m["m2"] = {2.0, 1.0};
  const auto corrected =
      xtidal::authoring::ApplyLocalHarmonicCorrections(background, {anchor});
  Require(corrected.constituents.size() == 2 &&
              corrected.constituents.front() == "z0",
          "correction field did not encode the Chart Datum level");
  Require(corrected.valid[0] && corrected.valid[1] && !corrected.valid[2],
          "bounded correction support is incorrect");
  Require(std::abs(corrected.coefficients_m[0].real() - 3.0) < 1e-12,
          "Chart Datum coefficient is incorrect at the anchor");
  Require(std::abs(corrected.coefficients_m[3].real() - 2.0) < 1e-12 &&
              std::abs(corrected.coefficients_m[3].imag() - 1.0) < 1e-12,
          "complex harmonic correction is incorrect at the anchor");

  eg::TideHeightHarmonics topology_background;
  topology_background.grid.latitudes = {50.0, 50.1};
  topology_background.grid.longitudes = {0.0, 0.08, 0.15};
  topology_background.constituents = {"m2", "m4"};
  topology_background.coefficients_m.assign(6, {1.0, 0.0});
  topology_background.coefficients_m.insert(
      topology_background.coefficients_m.end(), 6, {0.2, 0.0});
  topology_background.datum_id = "model-mean-sea-level";
  topology_background.datum_name = "Model mean sea level";
  auto topology_anchor = anchor;
  topology_anchor.support_radius_km = 0.0;
  topology_anchor.axis_bearing_degrees = 90.0;
  topology_anchor.major_along_support_km = 20.0;
  topology_anchor.major_cross_support_km = 6.0;
  topology_anchor.shallow_along_support_km = 8.0;
  topology_anchor.shallow_cross_support_km = 3.0;
  topology_anchor.datum_along_support_km = 12.0;
  topology_anchor.datum_cross_support_km = 4.0;
  topology_anchor.coefficients_m["m2"] = {2.0, 0.0};
  topology_anchor.coefficients_m["m4"] = {0.8, 0.0};
  const auto topology_corrected =
      xtidal::authoring::ApplyLocalHarmonicCorrections(topology_background,
                                                       {topology_anchor});
  Require(topology_corrected.valid[1] && topology_corrected.valid[2] &&
              !topology_corrected.valid[3],
          "anisotropic datum support did not follow the channel axis");
  Require(
      std::abs(topology_corrected.coefficients_m[6 + 2].real() - 2.0) < 1e-12,
      "major constituent did not propagate along the channel");
  Require(std::abs(topology_corrected.coefficients_m[12 + 1].real() - 0.8) <
                  1e-12 &&
              std::abs(topology_corrected.coefficients_m[12 + 2].real() - 0.2) <
                  1e-12,
          "shallow-water constituent did not use its shorter support");

  topology_anchor.support_polygon = {
      {-0.02, 49.98}, {0.10, 49.98}, {0.10, 50.02}, {-0.02, 50.02}};
  const auto polygon_corrected =
      xtidal::authoring::ApplyLocalHarmonicCorrections(topology_background,
                                                       {topology_anchor});
  Require(polygon_corrected.valid[1] && !polygon_corrected.valid[2],
          "hydrographic polygon did not block cross-boundary influence");

  auto complete_anchor = topology_anchor;
  complete_anchor.support_polygon.clear();
  complete_anchor.quality_weight = 1.0;
  complete_anchor.coefficients_m["m2"] = {3.0, 0.0};
  complete_anchor.coefficients_m["m4"] = {1.2, 0.0};
  auto incomplete_anchor = complete_anchor;
  incomplete_anchor.id = "incomplete-anchor";
  incomplete_anchor.coefficients_m.erase("m4");
  incomplete_anchor.coefficients_m["m2"] = {1.0, 0.0};
  const auto missing_constituent =
      xtidal::authoring::ApplyLocalHarmonicCorrections(
          topology_background, {complete_anchor, incomplete_anchor});
  Require(std::abs(missing_constituent.coefficients_m[12].real() - 1.2) < 1e-12,
          "anchor missing a constituent diluted another anchor's evidence");

  const auto ticon =
      xtidal::authoring::LoadTicon3Catalogue(XTIDAL_TICON3_NORMALIZED_FIXTURE);
  Require(ticon.stations.size() == 1 &&
              ticon.stations.front().coefficients_m.contains("m2") &&
              ticon.source_license == "CC-BY-4.0",
          "normalized TICON-3 authoring catalogue was not loaded");

  const auto datum_stations =
      xtidal::authoring::LoadChartDatumStations(XTIDAL_CHART_DATUM_FIXTURE);
  Require(datum_stations.size() == 9,
          "Chart Datum fixture did not retain its permitted stations");
  for (std::size_t index = 0; index < 8; ++index)
    Require(
        static_cast<unsigned>(datum_stations[index].realization) == index + 1,
        "national Chart Datum realization was collapsed or misclassified");

  eg::TideHeightHarmonics water_domain;
  water_domain.grid.latitudes = {50.0, 50.1, 50.2};
  water_domain.grid.longitudes = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
  water_domain.constituents = {"m2", "m4"};
  water_domain.coefficients_m.assign(water_domain.grid.size(), {1.0, 0.0});
  water_domain.coefficients_m.insert(water_domain.coefficients_m.end(),
                                     water_domain.grid.size(), {0.1, 0.0});
  water_domain.mask.assign(water_domain.grid.size(), 0);
  for (std::size_t y = 0; y < water_domain.grid.latitudes.size(); ++y)
    water_domain.mask[y * water_domain.grid.longitudes.size() + 3] = 1;
  water_domain.datum_id = "model-mean-sea-level";
  water_domain.datum_name = "Model mean sea level";
  xtidal::authoring::HarmonicObservation observation;
  observation.id = "left-basin";
  observation.gauge_type = "river";
  observation.latitude = 50.15;
  observation.longitude = 0.15;
  observation.valid_fraction = 0.95;
  observation.coefficients_m["m2"] = {2.0, 0.5};
  observation.coefficients_m["m4"] = {0.4, -0.1};
  observation.complex_standard_deviation_m["m2"] = 0.02;
  xtidal::authoring::HeightAssimilationOptions assimilation_options;
  assimilation_options.maximum_influence_km = 1000.0;
  assimilation_options.full_strength_radius_km = 5.0;
  assimilation_options.maximum_station_snap_km = 30.0;
  const auto assimilated = xtidal::authoring::AssimilateHarmonicObservations(
      water_domain, {observation}, assimilation_options);
  const auto left = water_domain.grid.longitudes.size() + 1;
  const auto tapered_left = water_domain.grid.longitudes.size() + 2;
  const auto right = water_domain.grid.longitudes.size() + 5;
  Require(std::abs(assimilated.harmonics.coefficients_m[left] -
                   std::complex<double>{2.0, 0.5}) < 1e-12,
          "TICON-3 observation is not authoritative at the gauge cell");
  Require(std::abs(assimilated.harmonics.coefficients_m[tapered_left] -
                   std::complex<double>{2.0, 0.5}) < 1e-12,
          "TICON-3 observation did not constrain every interpolation corner");
  Require(std::abs(assimilated.harmonics.coefficients_m[right] -
                   std::complex<double>{1.0, 0.0}) < 1e-12,
          "water-domain correction leaked through a land barrier");
  Require(assimilated.quality.support_class[left] ==
                  xtidal::authoring::HeightSupportClass::
                      kEstuaryObservationConstrained &&
              assimilated.quality.support_class[right] ==
                  xtidal::authoring::HeightSupportClass::kBackgroundOnly,
          "height support quality did not distinguish constrained cells");

  std::filesystem::remove(corrupt);
  std::filesystem::remove(v1);
  std::filesystem::remove(height_only);
  std::filesystem::remove(combined);
  std::filesystem::remove(coastal_node);
  std::filesystem::remove(root);
  std::cout << "X-Tidal XTD-only runtime tests passed\n";
}
