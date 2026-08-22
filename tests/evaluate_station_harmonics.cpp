#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>

#include "environmental_grib/xtd_package.h"
#include "ticon_stations.h"

namespace eg = environmental_grib;
namespace xa = xtidal::authoring;

namespace {

const std::set<std::string> kEightMajor{
    "m2", "s2", "n2", "k2", "k1", "o1", "p1", "q1"};

struct Model {
  std::string label;
  std::filesystem::path path;
  std::unique_ptr<eg::XtdPackageReader> reader;
};

Json::Value Evaluate(const xa::HarmonicObservation& station, Model& model) {
  eg::RegularGrid point_grid;
  point_grid.latitudes = {station.latitude};
  point_grid.longitudes = {station.longitude};
  const auto sampled = model.reader->SampleHeightHarmonics(point_grid);

  Json::Value result(Json::objectValue);
  result["label"] = model.label;
  result["covered"] = sampled.mask.empty() || sampled.mask.front() == 0;
  if (!result["covered"].asBool()) return result;

  std::map<std::string, std::size_t> model_index;
  for (std::size_t index = 0; index < sampled.constituents.size(); ++index)
    model_index.emplace(sampled.constituents[index], index);
  double major_squared_error = 0.0;
  double major_observed_energy = 0.0;
  double all_squared_error = 0.0;
  double all_observed_energy = 0.0;
  std::size_t major_count = 0;
  std::size_t all_count = 0;
  for (const auto& [name, observed] : station.coefficients_m) {
    const auto found = model_index.find(name);
    if (found == model_index.end()) continue;
    const auto predicted = sampled.coefficients_m.at(found->second);
    if (!std::isfinite(predicted.real()) || !std::isfinite(predicted.imag()))
      continue;
    const double error = std::abs(predicted - observed);
    Json::Value constituent(Json::objectValue);
    constituent["model_real_m"] = predicted.real();
    constituent["model_imaginary_m"] = predicted.imag();
    constituent["observed_real_m"] = observed.real();
    constituent["observed_imaginary_m"] = observed.imag();
    constituent["complex_error_m"] = error;
    if (const auto sigma = station.complex_standard_deviation_m.find(name);
        sigma != station.complex_standard_deviation_m.end())
      constituent["observation_sigma_m"] = sigma->second;
    result["constituents"][name] = std::move(constituent);
    all_squared_error += error * error;
    all_observed_energy += std::norm(observed);
    ++all_count;
    if (kEightMajor.contains(name)) {
      major_squared_error += error * error;
      major_observed_energy += std::norm(observed);
      ++major_count;
    }
  }
  result["major_constituent_count"] = Json::UInt64(major_count);
  result["all_constituent_count"] = Json::UInt64(all_count);
  if (major_count) {
    result["major_rss_error_m"] = std::sqrt(major_squared_error);
    result["major_relative_error"] =
        major_observed_energy > 0.0
            ? std::sqrt(major_squared_error / major_observed_energy)
            : Json::Value::null;
  }
  if (all_count) {
    result["all_rss_error_m"] = std::sqrt(all_squared_error);
    result["all_relative_error"] =
        all_observed_energy > 0.0
            ? std::sqrt(all_squared_error / all_observed_energy)
            : Json::Value::null;
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5 || (argc - 3) % 2 != 0) {
    std::cerr << "usage: xtidal_evaluate_station_harmonics TICON.json "
                 "OUTPUT.json LABEL MODEL.xtdt [LABEL MODEL.xtdt ...]\n";
    return 2;
  }
  try {
    const auto catalogue = xa::LoadTicon3Catalogue(argv[1]);
    std::vector<Model> models;
    for (int index = 3; index < argc; index += 2) {
      Model model;
      model.label = argv[index];
      model.path = argv[index + 1];
      model.reader = std::make_unique<eg::XtdPackageReader>(model.path);
      models.push_back(std::move(model));
    }

    Json::Value output(Json::objectValue);
    output["schema"] = "xtidal-station-model-evaluation";
    output["schema_version"] = 1;
    output["ticon_source_doi"] = catalogue.source_doi;
    for (const auto& model : models) {
      output["models"][model.label]["path"] = model.path.filename().string();
      output["models"][model.label]["authenticated"] =
          model.reader->status().authenticated;
    }
    for (std::size_t index = 0; index < catalogue.stations.size(); ++index) {
      const auto& station = catalogue.stations[index];
      Json::Value item(Json::objectValue);
      item["id"] = station.id;
      item["gauge_type"] = station.gauge_type;
      item["latitude"] = station.latitude;
      item["longitude"] = station.longitude;
      item["valid_fraction"] = station.valid_fraction;
      item["observation_count"] = Json::UInt64(station.observation_count);
      item["maximum_gap_days"] = station.maximum_gap_days;
      for (auto& model : models)
        item["model_results"][model.label] = Evaluate(station, model);
      output["stations"].append(std::move(item));
      if ((index + 1) % 250 == 0)
        std::cerr << "evaluated " << index + 1 << " / "
                  << catalogue.stations.size() << " stations\n";
    }

    std::ofstream stream(argv[2]);
    if (!stream) throw std::runtime_error("could not create evaluation JSON");
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    stream << Json::writeString(writer, output) << '\n';
    if (!stream) throw std::runtime_error("could not write evaluation JSON");
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
