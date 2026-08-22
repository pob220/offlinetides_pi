#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>

#include "xtidal_prediction.h"

namespace {
Json::Value ReadJson(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open reference manifest");
  Json::CharReaderBuilder builder;
  Json::Value result;
  std::string errors;
  if (!Json::parseFromStream(builder, input, &result, &errors))
    throw std::runtime_error("invalid reference manifest: " + errors);
  return result;
}

xtidal::HeightEventType EventType(const std::string& value) {
  if (value == "high") return xtidal::HeightEventType::kHighWater;
  if (value == "low") return xtidal::HeightEventType::kLowWater;
  throw std::runtime_error("reference event type must be high or low");
}

std::string EventType(xtidal::HeightEventType value) {
  return value == xtidal::HeightEventType::kHighWater ? "high" : "low";
}

std::string IsoUtc(xtidal::TimePoint time) {
  const auto raw = static_cast<time_t>(time.time_since_epoch().count());
  std::tm value{};
  gmtime_r(&raw, &value);
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: xtidal_validate_height PACKAGE.xtd REFERENCE.json\n";
    return 2;
  }
  try {
    const auto manifest = ReadJson(argv[2]);
    if (manifest["schema_version"].asInt() != 1 ||
        !manifest["events"].isArray() || manifest["events"].empty())
      throw std::runtime_error("unsupported or empty reference manifest");
    const double latitude = manifest["station"]["latitude"].asDouble();
    const double longitude = manifest["station"]["longitude"].asDouble();
    std::vector<xtidal::HeightEvent> reference;
    for (const auto& item : manifest["events"])
      reference.push_back({EventType(item["type"].asString()),
                           xtidal::TimePoint{std::chrono::seconds{
                               item["unix_seconds"].asInt64()}},
                           item["height_m"].asDouble()});

    const auto first = reference.front().time - std::chrono::hours{4};
    const auto last = reference.back().time + std::chrono::hours{4};
    xtidal::PredictionService service;
    service.Load(argv[1]);
    const auto datum = manifest["reference"]["vertical_datum_id"].asString();
    const auto comparison_mode =
        manifest["reference"]
            .get("comparison_mode", "same_vertical_datum")
            .asString();
    const auto requested_reference =
        datum == "chart-datum" && service.package().vertical_datum_available
            ? xtidal::HeightReference::kChartDatum
            : xtidal::HeightReference::kModelMeanSeaLevel;
    const std::string model_output_datum =
        requested_reference == xtidal::HeightReference::kChartDatum
            ? "chart-datum"
            : service.package().height_datum_id;
    const bool height_values_comparable =
        comparison_mode == "same_vertical_datum" ||
        model_output_datum == datum;
    if (comparison_mode != "same_vertical_datum" &&
        comparison_mode != "event_time_and_tidal_range")
      throw std::runtime_error("unsupported comparison mode");
    if (height_values_comparable && model_output_datum != datum)
      throw std::runtime_error("datum mismatch: package is '" +
                               model_output_datum +
                               "', reference is '" + datum + "'");
    const auto duration =
        std::chrono::duration_cast<std::chrono::hours>(last - first) +
        std::chrono::hours{1};
    const auto curve = service.PredictHeightCurve(
        latitude, longitude, first, duration, std::chrono::minutes{5},
        requested_reference);
    const auto predicted = xtidal::PredictionService::FindHeightEvents(curve);
    const auto metrics =
        xtidal::PredictionService::CompareHeightEvents(predicted, reference);

    Json::Value output(Json::objectValue);
    output["station_id"] = manifest["station"]["id"];
    output["source_url"] = manifest["reference"]["source_url"];
    output["model_datum_id"] = curve.datum_id;
    output["reference_datum_id"] = datum;
    output["comparison_mode"] = comparison_mode;
    output["height_values_comparable"] = height_values_comparable;
    output["reference_events"] = Json::UInt64(metrics.reference_events);
    output["curve_samples"] = Json::UInt64(curve.samples.size());
    output["predicted_events"] = Json::UInt64(predicted.size());
    for (const auto& event : predicted) {
      Json::Value item(Json::objectValue);
      item["type"] = EventType(event.type);
      item["utc"] = IsoUtc(event.time);
      item["height_m"] = event.height_m;
      output["predicted_event_list"].append(item);
    }
    output["matched_events"] = Json::UInt64(metrics.matched_events);
    output["mean_absolute_time_error_minutes"] =
        metrics.mean_absolute_time_error_minutes;
    output["maximum_absolute_time_error_minutes"] =
        metrics.maximum_absolute_time_error_minutes;
    output["mean_absolute_height_error_m"] =
        height_values_comparable
            ? Json::Value(metrics.mean_absolute_height_error_m)
            : Json::Value(Json::nullValue);
    output["maximum_absolute_height_error_m"] =
        height_values_comparable
            ? Json::Value(metrics.maximum_absolute_height_error_m)
            : Json::Value(Json::nullValue);
    for (const auto& match : metrics.matches) {
      Json::Value item(Json::objectValue);
      item["type"] = EventType(match.reference.type);
      item["reference_utc"] = IsoUtc(match.reference.time);
      item["reference_height_m"] = match.reference.height_m;
      item["predicted_utc"] = IsoUtc(match.predicted.time);
      item["predicted_height_m"] = match.predicted.height_m;
      item["signed_time_error_minutes"] = match.signed_time_error_minutes;
      item["signed_height_error_m"] =
          height_values_comparable ? Json::Value(match.signed_height_error_m)
                                   : Json::Value(Json::nullValue);
      output["matches"].append(item);
    }
    double total_range_error = 0.0;
    double maximum_range_error = 0.0;
    std::size_t range_count = 0;
    for (std::size_t index = 1; index < metrics.matches.size(); ++index) {
      const auto& before = metrics.matches[index - 1];
      const auto& after = metrics.matches[index];
      if (before.reference.type == after.reference.type) continue;
      const double reference_range =
          std::abs(after.reference.height_m - before.reference.height_m);
      const double predicted_range =
          std::abs(after.predicted.height_m - before.predicted.height_m);
      const double error = predicted_range - reference_range;
      Json::Value item(Json::objectValue);
      item["start_reference_utc"] = IsoUtc(before.reference.time);
      item["end_reference_utc"] = IsoUtc(after.reference.time);
      item["reference_range_m"] = reference_range;
      item["predicted_range_m"] = predicted_range;
      item["signed_range_error_m"] = error;
      output["tidal_ranges"].append(item);
      ++range_count;
      total_range_error += std::abs(error);
      maximum_range_error = std::max(maximum_range_error, std::abs(error));
    }
    output["tidal_range_comparisons"] = Json::UInt64(range_count);
    output["mean_absolute_tidal_range_error_m"] =
        range_count ? Json::Value(total_range_error / range_count)
                    : Json::Value(Json::nullValue);
    output["maximum_absolute_tidal_range_error_m"] =
        range_count ? Json::Value(maximum_range_error)
                    : Json::Value(Json::nullValue);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, output);
    return metrics.matched_events == reference.size() ? 0 : 1;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
