#include "ticon_stations.h"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include <json/json.h>

namespace xtidal::authoring {
namespace {

double RequiredFinite(const Json::Value& object, const char* name) {
  if (!object.isMember(name) || !object[name].isNumeric())
    throw std::runtime_error(std::string("TICON-3 field is missing: ") + name);
  const double value = object[name].asDouble();
  if (!std::isfinite(value))
    throw std::runtime_error(std::string("TICON-3 field is not finite: ") +
                             name);
  return value;
}

}  // namespace

Ticon3Catalogue LoadTicon3Catalogue(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open TICON-3 catalogue");
  Json::CharReaderBuilder parser;
  parser["collectComments"] = false;
  parser["failIfExtra"] = true;
  parser["rejectDupKeys"] = true;
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(parser, input, &root, &errors) ||
      root["schema"].asString() != "xtidal-ticon3-authoring-catalogue" ||
      root["schema_version"].asInt() != 1 || !root["stations"].isArray())
    throw std::runtime_error("invalid TICON-3 authoring catalogue: " + errors);

  Ticon3Catalogue result;
  result.source_doi = root["source"]["doi"].asString();
  result.source_license = root["source"]["license"].asString();
  result.input_sha256 = root["source"]["input_sha256"].asString();
  if (result.source_doi != "10.1594/PANGAEA.951610" ||
      result.source_license.empty() || result.input_sha256.size() != 64)
    throw std::runtime_error("TICON-3 source identity is incomplete");

  for (const auto& item : root["stations"]) {
    HarmonicObservation station;
    station.id = item["id"].asString();
    station.gauge_type = item["gauge_type"].asString();
    station.latitude = RequiredFinite(item, "latitude");
    station.longitude = RequiredFinite(item, "longitude");
    station.valid_fraction =
        RequiredFinite(item["quality"], "valid_fraction");
    station.observation_count =
        static_cast<std::size_t>(item["observation_count"].asUInt64());
    station.maximum_gap_days = RequiredFinite(item, "maximum_gap_days");
    if (station.id.empty() ||
        (station.gauge_type != "coastal" && station.gauge_type != "river") ||
        station.latitude < -90.0 || station.latitude > 90.0 ||
        station.longitude < -180.0 || station.longitude > 180.0 ||
        station.valid_fraction <= 0.0 || station.valid_fraction > 1.0 ||
        !item["constituents"].isObject())
      throw std::runtime_error("invalid TICON-3 station record");
    for (const auto& name : item["constituents"].getMemberNames()) {
      const auto& value = item["constituents"][name];
      const std::complex<double> coefficient{
          RequiredFinite(value, "real_m"),
          RequiredFinite(value, "imaginary_m")};
      const double deviation = RequiredFinite(value, "complex_std_m");
      if (deviation < 0.0)
        throw std::runtime_error("negative TICON-3 coefficient uncertainty");
      station.coefficients_m.emplace(name, coefficient);
      station.complex_standard_deviation_m.emplace(name, deviation);
    }
    if (!station.coefficients_m.empty())
      result.stations.push_back(std::move(station));
  }
  if (result.stations.empty())
    throw std::runtime_error("TICON-3 catalogue contains no usable stations");
  return result;
}

}  // namespace xtidal::authoring
