#include "forecast_export.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <json/json.h>

namespace xtidal {
namespace {

std::string Csv(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string escaped{"\""};
  for (const char character : value) {
    escaped += character;
    if (character == '"') escaped += '"';
  }
  escaped += '"';
  return escaped;
}

std::string SourceId(const PackageInfo& package) {
  for (const char* key : {"source_id", "dataset_id", "dataset_name"}) {
    if (package.metadata.isMember(key) && package.metadata[key].isString() &&
        !package.metadata[key].asString().empty())
      return package.metadata[key].asString();
  }
  return "offlinetides-authenticated-package";
}

std::string SourceName(const PackageInfo& package) {
  for (const char* key : {"dataset_name", "source_name", "product"}) {
    if (package.metadata.isMember(key) && package.metadata[key].isString() &&
        !package.metadata[key].asString().empty())
      return package.metadata[key].asString();
  }
  return "OfflineTides authenticated harmonic package";
}

}  // namespace

const char* ForecastValueStateName(ForecastValueState state) {
  switch (state) {
    case ForecastValueState::kKnown:
      return "known";
    case ForecastValueState::kMasked:
      return "masked";
    default:
      return "unknown";
  }
}

ForecastExportDocument BuildForecastExport(const HeightCurve& curve,
                                           const PackageInfo& package,
                                           TimePoint generated) {
  if (!std::isfinite(curve.latitude) || !std::isfinite(curve.longitude) ||
      curve.latitude < -90.0 || curve.latitude > 90.0 ||
      curve.longitude < -180.0 || curve.longitude > 180.0)
    throw std::invalid_argument("forecast export geometry is not valid WGS84");
  ForecastExportDocument result;
  result.generated_utc = generated;
  result.latitude_wgs84 = curve.latitude;
  result.longitude_wgs84 = curve.longitude;
  result.vertical_datum_id = curve.datum_id;
  result.vertical_datum_name = curve.datum_name;
  if (curve.vertical_datum)
    result.vertical_datum_epoch = curve.vertical_datum->epoch;
  else if (package.metadata.isMember("vertical_datum_epoch") &&
           package.metadata["vertical_datum_epoch"].isString())
    result.vertical_datum_epoch =
        package.metadata["vertical_datum_epoch"].asString();
  if (package.metadata.isMember("prediction_method") &&
      package.metadata["prediction_method"].isString() &&
      !package.metadata["prediction_method"].asString().empty())
    result.prediction_method = package.metadata["prediction_method"].asString();
  if (curve.quality) {
    result.uncertainty_m = std::hypot(curve.quality->harmonic_sigma_m,
                                      curve.quality->datum_sigma_m);
  }
  result.package_id = package.package_id;
  result.source_id = SourceId(package);
  result.source_name = SourceName(package);
  result.samples.reserve(curve.samples.size());
  for (const auto& sample : curve.samples) {
    ForecastExportSample exported;
    exported.valid_time_utc = sample.time;
    if (std::isfinite(sample.height_m)) {
      exported.state = ForecastValueState::kKnown;
      exported.water_level_m = sample.height_m;
    } else {
      exported.state = ForecastValueState::kMasked;
    }
    result.samples.push_back(exported);
  }
  return result;
}

std::string SerializeForecastJson(const ForecastExportDocument& document) {
  Json::Value root(Json::objectValue);
  root["schema"] = document.schema;
  root["schema_version"] = document.schema_version;
  root["generated_utc"] =
      environmental_grib::FormatUtcDateTime(document.generated_utc);
  root["geometry"]["type"] = "Point";
  root["geometry"]["coordinate_reference_system"] = "WGS84";
  root["geometry"]["coordinates"].append(document.longitude_wgs84);
  root["geometry"]["coordinates"].append(document.latitude_wgs84);
  root["vertical_datum"]["identifier"] = document.vertical_datum_id;
  root["vertical_datum"]["name"] = document.vertical_datum_name;
  root["vertical_datum"]["epoch"] = document.vertical_datum_epoch;
  root["prediction_method"] = document.prediction_method;
  if (document.uncertainty_m)
    root["uncertainty_m"] = *document.uncertainty_m;
  else
    root["uncertainty_m"] = Json::nullValue;
  root["package"]["id"] = document.package_id;
  root["source"]["id"] = document.source_id;
  root["source"]["name"] = document.source_name;
  root["water_level_unit"] = "m";
  for (const auto& sample : document.samples) {
    Json::Value item(Json::objectValue);
    item["valid_time_utc"] =
        environmental_grib::FormatUtcDateTime(sample.valid_time_utc);
    item["state"] = ForecastValueStateName(sample.state);
    if (sample.water_level_m)
      item["water_level_m"] = *sample.water_level_m;
    else
      item["water_level_m"] = Json::nullValue;
    root["samples"].append(item);
  }
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  return Json::writeString(writer, root) + "\n";
}

std::string SerializeForecastCsv(const ForecastExportDocument& document) {
  std::ostringstream output;
  output << "schema_version,valid_time_utc,longitude_wgs84,latitude_wgs84,"
            "state,water_level_m,vertical_datum_id,vertical_datum_name,"
            "vertical_datum_epoch,prediction_method,uncertainty_m,package_id,"
            "source_id,source_name\n";
  output << std::setprecision(10);
  for (const auto& sample : document.samples) {
    output << document.schema_version << ','
           << environmental_grib::FormatUtcDateTime(sample.valid_time_utc)
           << ',' << document.longitude_wgs84 << ',' << document.latitude_wgs84
           << ',' << ForecastValueStateName(sample.state) << ',';
    if (sample.water_level_m) output << *sample.water_level_m;
    output << ',' << Csv(document.vertical_datum_id) << ','
           << Csv(document.vertical_datum_name) << ','
           << Csv(document.vertical_datum_epoch) << ','
           << Csv(document.prediction_method) << ',';
    if (document.uncertainty_m) output << *document.uncertainty_m;
    output << ',' << Csv(document.package_id) << ',' << Csv(document.source_id)
           << ',' << Csv(document.source_name) << '\n';
  }
  return output.str();
}

}  // namespace xtidal
