#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>

#include "environmental_grib/tpxo.h"
#include "environmental_grib/xtd_package.h"
#include "height_correction.h"
#include "station_harmonics.h"
#include "xtidal_prediction.h"

namespace eg = environmental_grib;
namespace xa = xtidal::authoring;

namespace {
Json::Value ReadJson(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open JSON input");
  Json::CharReaderBuilder parser;
  Json::Value result;
  std::string errors;
  if (!Json::parseFromStream(parser, input, &result, &errors))
    throw std::runtime_error("invalid JSON input: " + errors);
  return result;
}

xtidal::HeightEventType EventType(const std::string& value) {
  if (value == "high") return xtidal::HeightEventType::kHighWater;
  if (value == "low") return xtidal::HeightEventType::kLowWater;
  throw std::runtime_error("reference event type must be high or low");
}

struct Evaluation {
  xtidal::HeightValidationMetrics extrema;
  double mean_absolute_range_error_m{};
  double score{std::numeric_limits<double>::infinity()};
};

double RangeError(const xtidal::HeightValidationMetrics& metrics) {
  double total = 0.0;
  std::size_t count = 0;
  for (std::size_t index = 1; index < metrics.matches.size(); ++index) {
    const auto& before = metrics.matches[index - 1];
    const auto& after = metrics.matches[index];
    if (before.reference.type == after.reference.type) continue;
    const double reference =
        std::abs(after.reference.height_m - before.reference.height_m);
    const double predicted =
        std::abs(after.predicted.height_m - before.predicted.height_m);
    total += std::abs(predicted - reference);
    ++count;
  }
  return count ? total / count : std::numeric_limits<double>::infinity();
}

struct CurveComponents {
  std::vector<xtidal::TimePoint> times;
  std::vector<double> background;
  std::vector<double> major_innovation;
  std::vector<double> shallow_innovation;
  double chart_datum_reference_m{};
};

Evaluation Evaluate(const CurveComponents& components,
                    const std::vector<xtidal::HeightEvent>& reference,
                    double major_gain, double shallow_gain,
                    double prediction_time_shift_minutes = 0.0,
                    double amplitude_scale = 1.0,
                    double chart_datum_offset_m = 0.0) {
  if (reference.empty()) return {};
  xtidal::HeightCurve curve;
  curve.datum_id = "chart-datum";
  curve.datum_name = "Chart Datum";
  curve.samples.reserve(components.times.size());
  for (std::size_t index = 0; index < components.times.size(); ++index) {
    curve.samples.push_back(
        {components.times[index] +
             std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::duration<double, std::ratio<60>>(
                     prediction_time_shift_minutes)),
         components.chart_datum_reference_m + chart_datum_offset_m +
             amplitude_scale *
                 (components.background[index] +
                  major_gain * components.major_innovation[index] +
                  shallow_gain * components.shallow_innovation[index])});
  }
  Evaluation result;
  result.extrema = xtidal::PredictionService::CompareHeightEvents(
      xtidal::PredictionService::FindHeightEvents(curve), reference);
  result.mean_absolute_range_error_m = RangeError(result.extrema);
  if (result.extrema.matched_events != reference.size() ||
      !std::isfinite(result.mean_absolute_range_error_m))
    return result;
  // Ten minutes, ten centimetres of height and ten centimetres of range have
  // equal weight.  A weak regularizer avoids large gains for tiny score wins.
  result.score =
      result.extrema.mean_absolute_time_error_minutes / 10.0 +
      result.extrema.mean_absolute_height_error_m / 0.1 +
      result.mean_absolute_range_error_m / 0.1 +
      0.05 * (std::pow(major_gain - 1.0, 2) + std::pow(shallow_gain - 1.0, 2));
  return result;
}

double Median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  const auto middle = values.begin() + values.size() / 2;
  std::nth_element(values.begin(), middle, values.end());
  if (values.size() % 2) return *middle;
  const auto lower = *std::max_element(values.begin(), middle);
  return (lower + *middle) / 2.0;
}

struct StationTransform {
  double prediction_time_shift_minutes{};
  double amplitude_scale{1.0};
  double chart_datum_offset_m{};
};

StationTransform FitStationTransform(const Evaluation& uncalibrated,
                                     double chart_datum_reference_m) {
  StationTransform result;
  std::vector<double> time_shifts;
  double mean_x = 0.0;
  double mean_y = 0.0;
  for (const auto& match : uncalibrated.extrema.matches) {
    time_shifts.push_back(-match.signed_time_error_minutes);
    mean_x += match.predicted.height_m - chart_datum_reference_m;
    mean_y += match.reference.height_m;
  }
  if (uncalibrated.extrema.matches.empty()) return result;
  mean_x /= uncalibrated.extrema.matches.size();
  mean_y /= uncalibrated.extrema.matches.size();
  double covariance = 0.0;
  double variance = 0.0;
  for (const auto& match : uncalibrated.extrema.matches) {
    const double x = match.predicted.height_m - chart_datum_reference_m;
    const double y = match.reference.height_m;
    covariance += (x - mean_x) * (y - mean_y);
    variance += std::pow(x - mean_x, 2);
  }
  result.prediction_time_shift_minutes =
      std::clamp(Median(std::move(time_shifts)), -60.0, 60.0);
  if (variance > 1e-12)
    result.amplitude_scale = std::clamp(covariance / variance, 0.7, 1.3);
  const double fitted_reference = mean_y - result.amplitude_scale * mean_x;
  result.chart_datum_offset_m = fitted_reference - chart_datum_reference_m;
  return result;
}

Json::Value MetricsJson(const Evaluation& evaluation) {
  Json::Value value(Json::objectValue);
  value["matched_events"] = Json::UInt64(evaluation.extrema.matched_events);
  value["reference_events"] = Json::UInt64(evaluation.extrema.reference_events);
  value["mean_absolute_time_error_minutes"] =
      evaluation.extrema.mean_absolute_time_error_minutes;
  value["mean_absolute_height_error_m"] =
      evaluation.extrema.mean_absolute_height_error_m;
  value["mean_absolute_tidal_range_error_m"] =
      evaluation.mean_absolute_range_error_m;
  value["score"] = evaluation.score;
  return value;
}

CurveComponents BuildComponents(const eg::TideHeightHarmonics& background,
                                const xa::StationHarmonicConstants& station,
                                const std::vector<xtidal::TimePoint>& times) {
  if (!background.mask.empty() && background.mask.front())
    throw std::runtime_error("background is unavailable at station");
  std::map<std::string, std::complex<double>> base;
  for (std::size_t index = 0; index < background.constituents.size(); ++index) {
    const auto coefficient = background.coefficients_m[index];
    if (std::isfinite(coefficient.real()) && std::isfinite(coefficient.imag()))
      base[background.constituents[index]] = coefficient;
  }
  std::set<std::string> names;
  for (const auto& [name, coefficient] : base)
    if (name != "z0") names.insert(name);
  for (const auto& [name, coefficient] : station.coefficients_m)
    if (name != "z0") names.insert(name);
  std::vector<std::string> ordered(names.begin(), names.end());
  std::vector<std::complex<double>> background_coefficients;
  std::vector<std::complex<double>> major_coefficients;
  std::vector<std::complex<double>> shallow_coefficients;
  for (const auto& name : ordered) {
    const auto base_value =
        base.contains(name) ? base.at(name) : std::complex<double>{};
    const auto station_value = station.coefficients_m.contains(name)
                                   ? station.coefficients_m.at(name)
                                   : base_value;
    const auto innovation = station_value - base_value;
    background_coefficients.push_back(base_value);
    major_coefficients.push_back(xa::IsShallowWaterConstituent(name)
                                     ? std::complex<double>{}
                                     : innovation);
    shallow_coefficients.push_back(xa::IsShallowWaterConstituent(name)
                                       ? innovation
                                       : std::complex<double>{});
  }
  CurveComponents result;
  result.times = times;
  result.chart_datum_reference_m = station.chart_datum_reference_m;
  result.background = eg::PredictAtlasHarmonicGrid(
      ordered, background_coefficients, 1, times, false);
  result.major_innovation = eg::PredictAtlasHarmonicGrid(
      ordered, major_coefficients, 1, times, false);
  result.shallow_innovation = eg::PredictAtlasHarmonicGrid(
      ordered, shallow_coefficients, 1, times, false);
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "usage: xtidal_calibrate_gains BASE.xtd HARMONICS "
                 "CATALOGUE.json REFERENCE.json...\n";
    return 2;
  }
  try {
    const auto catalogue = ReadJson(argv[3]);
    const int calibration_year = catalogue["calibration_year"].asInt();
    std::map<std::string, Json::Value> stations;
    for (const auto& station : catalogue["stations"])
      stations.emplace(station["id"].asString(), station);
    eg::XtdPackageReader package(argv[1]);
    Json::Value output(Json::objectValue);
    output["schema_version"] = 1;
    output["training_days"] = 14;
    output["acceptance_days"] = 7;
    output["final_test_days"] = 7;
    output["gain_grid_step"] = 0.05;
    output["gain_grid_maximum"] = 1.5;

    for (int argument = 4; argument < argc; ++argument) {
      const auto manifest = ReadJson(argv[argument]);
      const auto id = manifest["station"]["id"].asString();
      const auto found = stations.find(id);
      if (found == stations.end()) continue;
      const auto& item = found->second;
      std::vector<xtidal::HeightEvent> training;
      std::vector<xtidal::HeightEvent> acceptance;
      std::vector<xtidal::HeightEvent> final_test;
      const auto first_seconds =
          manifest["events"][0]["unix_seconds"].asInt64();
      const auto start = std::chrono::floor<std::chrono::days>(
          xtidal::TimePoint{std::chrono::seconds{first_seconds}});
      const auto training_split = start + std::chrono::days{14};
      const auto acceptance_split = start + std::chrono::days{21};
      for (const auto& event : manifest["events"]) {
        xtidal::HeightEvent parsed{EventType(event["type"].asString()),
                                   xtidal::TimePoint{std::chrono::seconds{
                                       event["unix_seconds"].asInt64()}},
                                   event["height_m"].asDouble()};
        if (parsed.time < training_split)
          training.push_back(parsed);
        else if (parsed.time < acceptance_split)
          acceptance.push_back(parsed);
        else
          final_test.push_back(parsed);
      }
      if (training.empty() || acceptance.empty() || final_test.empty())
        throw std::runtime_error(
            "reference does not span the calibration split");
      const auto all_start = training.front().time - std::chrono::hours{4};
      const auto all_end = final_test.back().time + std::chrono::hours{4};
      std::vector<xtidal::TimePoint> times;
      for (auto time = all_start; time <= all_end;
           time += std::chrono::minutes{5})
        times.push_back(time);

      eg::RegularGrid point;
      point.latitudes = {item["latitude"].asDouble()};
      point.longitudes = {item["longitude"].asDouble()};
      const auto background = package.SampleHeightHarmonics(point);
      const auto station = xa::LoadStationHarmonicConstants(
          argv[2], item["source_prefix"].asString(), calibration_year);
      const auto components = BuildComponents(background, station, times);

      const auto uncalibrated_training =
          Evaluate(components, training, 1.0, 1.0);
      const auto transform = FitStationTransform(
          uncalibrated_training, components.chart_datum_reference_m);
      const auto best =
          Evaluate(components, training, 1.0, 1.0,
                   transform.prediction_time_shift_minutes,
                   transform.amplitude_scale, transform.chart_datum_offset_m);
      Json::Value result(Json::objectValue);
      result["station_id"] = id;
      result["major_correction_gain"] = 1.0;
      result["shallow_correction_gain"] = 1.0;
      result["prediction_time_shift_minutes"] =
          transform.prediction_time_shift_minutes;
      result["amplitude_scale"] = transform.amplitude_scale;
      result["chart_datum_offset_m"] = transform.chart_datum_offset_m;
      result["training"] = MetricsJson(best);
      const auto candidate_acceptance =
          Evaluate(components, acceptance, 1.0, 1.0,
                   transform.prediction_time_shift_minutes,
                   transform.amplitude_scale, transform.chart_datum_offset_m);
      const auto uncalibrated_acceptance =
          Evaluate(components, acceptance, 1.0, 1.0);
      const bool accepted =
          candidate_acceptance.score <= uncalibrated_acceptance.score * 0.98 &&
          candidate_acceptance.extrema.mean_absolute_time_error_minutes <=
              uncalibrated_acceptance.extrema.mean_absolute_time_error_minutes *
                  1.25 &&
          candidate_acceptance.extrema.mean_absolute_height_error_m <=
              uncalibrated_acceptance.extrema.mean_absolute_height_error_m *
                  1.25 &&
          candidate_acceptance.mean_absolute_range_error_m <=
              uncalibrated_acceptance.mean_absolute_range_error_m * 1.25;
      result["accepted"] = accepted;
      result["acceptance"] = MetricsJson(candidate_acceptance);
      result["uncalibrated_acceptance"] = MetricsJson(uncalibrated_acceptance);
      result["final_test"] = MetricsJson(
          Evaluate(components, final_test, 1.0, 1.0,
                   accepted ? transform.prediction_time_shift_minutes : 0.0,
                   accepted ? transform.amplitude_scale : 1.0,
                   accepted ? transform.chart_datum_offset_m : 0.0));
      result["uncalibrated_final_test"] =
          MetricsJson(Evaluate(components, final_test, 1.0, 1.0));
      result["uncalibrated_training"] = MetricsJson(uncalibrated_training);
      output["stations"].append(result);
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, output);
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
