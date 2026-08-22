#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>
#include <sodium.h>

#include "environmental_grib/geo.h"
#include "environmental_grib/xtd_package.h"
#include "height_assimilation.h"
#include "global_height_mosaic.h"
#include "ticon_stations.h"
#include "vertical_datum.h"
#include "xtd_test_support.h"

namespace eg = environmental_grib;
namespace xa = xtidal::authoring;

namespace {

std::string Sha256(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not hash authoring input");
  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);
  std::array<unsigned char, 1024 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (const auto count = input.gcount(); count > 0)
      crypto_hash_sha256_update(&state, buffer.data(), count);
  }
  std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
  crypto_hash_sha256_final(&state, digest.data());
  static constexpr char hex[] = "0123456789abcdef";
  std::string result(digest.size() * 2, '0');
  for (std::size_t index = 0; index < digest.size(); ++index) {
    result[index * 2] = hex[digest[index] >> 4];
    result[index * 2 + 1] = hex[digest[index] & 15];
  }
  return result;
}

Json::Value ReadDatumSourceProvenance(const std::filesystem::path& path) {
  std::ifstream input(path);
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  if (!input || !Json::parseFromStream(builder, input, &root, &errors))
    throw std::runtime_error("could not read datum catalogue provenance: " +
                             errors);
  return root.get("source_provenance", Json::Value(Json::objectValue));
}

void WriteProvenance(const std::filesystem::path& output,
                     const std::vector<std::filesystem::path>& backgrounds,
                     const xa::Ticon3Catalogue& catalogue,
                     const xa::AssimilatedHeightField& field,
                     const xa::HeightAssimilationOptions& options,
                     const std::filesystem::path& datum_catalogue_path,
                     const Json::Value& datum_source_provenance,
                     const xa::VerticalDatumField& datum_field,
                     std::size_t datum_station_count) {
  Json::Value provenance(Json::objectValue);
  provenance["schema"] = "xtdt-provenance";
  provenance["schema_version"] = 1;
  provenance["xtdt_file"] = output.filename().string();
  provenance["xtdt_sha256"] = Sha256(output);
  provenance["xtdt_bytes"] = Json::UInt64(std::filesystem::file_size(output));
  provenance["package_id"] = eg::InspectXtdPackage(output)["package_id"];
  provenance["created_utc"] =
      eg::FormatUtcDateTime(std::chrono::floor<std::chrono::seconds>(
          std::chrono::system_clock::now()));
  provenance["product"] =
      "OfflineTides global observation-constrained astronomical water-level "
      "harmonics";
  provenance["product_version"] = "1.0.0-alpha1";
  provenance["method"] =
      "primary-model mosaic with independent models used only for coverage "
      "gaps; model-spread uncertainty followed by regime-separated complex "
      "harmonic innovations on the connected water mask";
  for (std::size_t index = 0; index < backgrounds.size(); ++index) {
    Json::Value member(Json::objectValue);
    member["file"] = backgrounds[index].filename().string();
    member["sha256"] = Sha256(backgrounds[index]);
    member["role"] = index == 0 ? "primary" : "coverage-gap-fallback";
    provenance["background_members"].append(member);
  }
  provenance["observation_source"]["doi"] = catalogue.source_doi;
  provenance["observation_source"]["license"] = catalogue.source_license;
  provenance["observation_source"]["input_sha256"] = catalogue.input_sha256;
  provenance["observation_source"]["role"] = "offline authoring only";
  provenance["observation_source"]["station_records"] =
      Json::UInt64(catalogue.stations.size());
  provenance["parameters"]["maximum_influence_km"] =
      options.maximum_influence_km;
  provenance["parameters"]["river_maximum_influence_km"] =
      options.river_maximum_influence_km;
  provenance["parameters"]["full_strength_radius_km"] =
      options.full_strength_radius_km;
  provenance["parameters"]["observation_anchor_gain"] =
      options.observation_anchor_gain;
  provenance["parameters"]["residual_gain"] = options.residual_gain;
  provenance["parameters"]["maximum_station_snap_km"] =
      options.maximum_station_snap_km;
  provenance["distribution"]["contains_raw_tpxo"] = false;
  provenance["distribution"]["contains_raw_ticon3"] = false;
  provenance["distribution"]["contains_raw_eot20"] = false;
  provenance["distribution"]["contains_raw_hamtide"] = false;
  provenance["distribution"]["contains_raw_gtsm"] = false;
  provenance["distribution"]["contains_raw_fes"] = false;
  provenance["distribution"]["contains_raw_source_models"] = false;
  provenance["distribution"]["runtime_requires_tpxo"] = false;
  provenance["distribution"]["runtime_requires_ticon3"] = false;
  provenance["distribution"]["runtime_requires_raw_source_models"] = false;
  provenance["vertical_datum"]["id"] = field.harmonics.datum_id;
  provenance["vertical_datum"]["name"] = field.harmonics.datum_name;
  provenance["vertical_datum"]["warning"] =
      "Harmonic assimilation does not create Chart Datum. A separate "
      "validated datum field is required for heights above Chart Datum.";
  provenance["chart_datum_transform"]["source_file"] =
      datum_catalogue_path.filename().string();
  provenance["chart_datum_transform"]["source_sha256"] =
      Sha256(datum_catalogue_path);
  provenance["chart_datum_transform"]["source_role"] =
      "permitted reference-station offsets used only for authoring";
  provenance["chart_datum_transform"]["source_provenance"] =
      datum_source_provenance;
  provenance["chart_datum_transform"]["station_records"] =
      Json::UInt64(datum_station_count);
  provenance["chart_datum_transform"]["source_vertical_datum"] =
      "model-mean-sea-level";
  provenance["chart_datum_transform"]["target_vertical_datum"] =
      "local-authority-chart-datum";
  std::uint64_t supported_datum_cells = 0;
  for (const auto valid : datum_field.valid) supported_datum_cells += valid;
  provenance["chart_datum_transform"]["supported_cells"] =
      Json::UInt64(supported_datum_cells);
  provenance["chart_datum_transform"]["unsupported_policy"] =
      "Chart Datum unavailable; retain explicitly labelled model MSL";
  std::map<int, std::uint64_t> support_counts;
  for (const auto value : field.quality.support_class)
    ++support_counts[static_cast<int>(value)];
  for (const auto& [value, count] : support_counts)
    provenance["quality"]["support_class_cell_counts"][std::to_string(value)] =
        Json::UInt64(count);
  std::ofstream sidecar(output.string() + ".prv");
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  sidecar << Json::writeString(writer, provenance);
  if (!sidecar) throw std::runtime_error("could not write .xtdt.prv sidecar");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 10) {
    std::cerr << "usage: xtidal_build_ticon_xtdt BACKGROUND.xtdt "
                 "TICON.json DATUM_STATIONS.json SPACING WEST SOUTH EAST "
                 "NORTH OUTPUT.xtdt "
                 "[FALLBACK.xtdt ...]\n";
    return 2;
  }
  try {
    std::vector<std::filesystem::path> background_paths{argv[1]};
    for (int index = 10; index < argc; ++index)
      background_paths.emplace_back(argv[index]);
    const std::filesystem::path catalogue_path = argv[2];
    const std::filesystem::path datum_catalogue_path = argv[3];
    const double spacing = std::stod(argv[4]);
    const eg::BoundingBox bbox{std::stod(argv[5]), std::stod(argv[6]),
                               std::stod(argv[7]), std::stod(argv[8])};
    const std::filesystem::path output = argv[9];
    if (output.extension() != ".xtdt")
      throw std::runtime_error("observation-enhanced output must use .xtdt");
    const auto grid = eg::BuildRegularGrid(bbox, spacing);
    std::vector<xa::HeightModelMember> members;
    for (const auto& path : background_paths) {
      eg::XtdPackageReader reader(path);
      members.push_back(
          {path.stem().string(), reader.SampleHeightHarmonics(grid)});
    }
    const auto mosaic = xa::BuildConservativeHeightMosaic(members);
    const auto catalogue = xa::LoadTicon3Catalogue(catalogue_path);
    xa::HeightAssimilationOptions assimilation_options;
    assimilation_options.maximum_influence_km = 100.0;
    assimilation_options.river_maximum_influence_km = 30.0;
    assimilation_options.maximum_station_snap_km = 75.0;
    assimilation_options.background_sigma_by_point_m =
        mosaic.quality.harmonic_sigma_m;
    assimilation_options.background_support_class_by_point =
        mosaic.quality.support_class;
    const auto field = xa::AssimilateHarmonicObservations(
        mosaic.harmonics, catalogue.stations, assimilation_options);
    const auto datum_stations =
        xa::LoadChartDatumStations(datum_catalogue_path, &catalogue);
    const auto datum_source_provenance =
        ReadDatumSourceProvenance(datum_catalogue_path);
    const auto datum_field =
        xa::BuildVerticalDatumField(field.harmonics, datum_stations);
    const auto points = grid.size();

    eg::test::XtdV2FixtureOptions options;
    options.randomize_crypto = true;
    options.include_tide = false;
    options.include_residual = false;
    options.include_uncertainty = false;
    options.include_height = true;
    options.include_height_quality = true;
    options.include_vertical_datum = true;
    options.outer_metadata["package_profile"] = "xtidal-tide-v1";
    options.outer_metadata["recommended_extension"] = ".xtdt";
    options.outer_metadata["dataset_id"] =
        "offlinetides-global-v1.0.0-alpha1";
    options.outer_metadata["dataset_name"] =
        "OfflineTides global observation-constrained harmonics";
    options.outer_metadata["prediction_method"] = "harmonic-astronomical";
    options.outer_metadata["vertical_datum_epoch"] = "2026";
    options.outer_metadata["capabilities"]["water_level_harmonics"] = true;
    options.outer_metadata["capabilities"]["tidal_current_harmonics"] = false;
    options.outer_metadata["capabilities"]["water_level_quality"] = true;
    options.outer_metadata["capabilities"]["vertical_datum_transform"] = true;
    options.outer_metadata["capabilities"]["tidal_current_quality"] = false;
    options.tide.nx = static_cast<std::uint32_t>(grid.longitudes.size());
    options.tide.ny = static_cast<std::uint32_t>(grid.latitudes.size());
    options.tide.west = bbox.west;
    options.tide.south = bbox.south;
    options.tide.east = bbox.east;
    options.tide.north = bbox.north;
    options.tide.lon_u0 = options.tide.lon_v0 = grid.longitudes.front();
    options.tide.lat_u0 = options.tide.lat_v0 = grid.latitudes.front();
    options.tide.lon_step = options.tide.lat_step = spacing;
    options.tile_width = 64;
    options.tile_height = 64;
    options.quantization_scale = 0.0005F;
    options.height_reference_level_m = field.harmonics.reference_level_m;
    options.height_datum_id = field.harmonics.datum_id;
    options.height_datum_name = field.harmonics.datum_name;
    options.height_constituents = field.harmonics.constituents;
    options.height_metadata["assimilation_method"] =
        "four-corner-anchored-regime-nearest-residual-taper-v4";
    options.height_quality_metadata["datum_quality_note"] =
        "Harmonic support quality is independent of vertical-datum support";
    options.height_quality_metadata["harmonic_uncertainty_method"] =
        "held-out-error-floor-plus-cross-model-spread";
    options.vertical_datum_epoch = "2026";
    options.vertical_datum_metadata["method"] =
        "topology-bounded station-constrained transformation";
    options.vertical_datum_metadata["uncertainty_method"] =
        "station-spread plus distance and regime floors";
    options.valid = [&field](std::uint32_t x, std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * field.harmonics.grid.longitudes.size() +
          x;
      return field.harmonics.mask.empty() || !field.harmonics.mask.at(point);
    };
    options.height_value = [&field, points](std::size_t component,
                                            std::uint32_t x, std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * field.harmonics.grid.longitudes.size() +
          x;
      const auto value =
          field.harmonics.coefficients_m[(component / 2) * points + point];
      if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return 0.0;
      return component % 2 == 0 ? value.real() : value.imag();
    };
    options.height_quality_value = [&field](std::size_t component,
                                            std::uint32_t x, std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * field.harmonics.grid.longitudes.size() +
          x;
      if (component == 0)
        return std::min(32.767, field.quality.harmonic_sigma_m.at(point));
      if (component == 1) return 0.0;
      const double distance =
          field.quality.nearest_observation_distance_km.at(point);
      return std::isfinite(distance) ? std::min(3276.7, distance) : 3276.7;
    };
    options.height_support_class = [&field](std::uint32_t x, std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * field.harmonics.grid.longitudes.size() +
          x;
      return static_cast<std::uint8_t>(field.quality.support_class.at(point));
    };
    options.height_observation_count = [&field](std::uint32_t x,
                                                std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * field.harmonics.grid.longitudes.size() +
          x;
      const auto count = field.quality.contributing_observations.at(point);
      return count == 0 && field.quality.support_class.at(point) >=
                               xa::HeightSupportClass::kObservationConstrained
                 ? std::uint16_t{1}
                 : count;
    };
    options.vertical_datum_valid = [&datum_field](std::uint32_t x,
                                                  std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * datum_field.grid.longitudes.size() + x;
      return datum_field.valid.at(point) != 0;
    };
    options.vertical_datum_value = [&datum_field](std::size_t component,
                                                  std::uint32_t x,
                                                  std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * datum_field.grid.longitudes.size() + x;
      if (!datum_field.valid.at(point)) return 0.0;
      if (component == 0) return datum_field.offset_m.at(point);
      if (component == 1) return datum_field.uncertainty_m.at(point);
      return std::min(3276.7,
                      datum_field.nearest_station_distance_km.at(point));
    };
    options.vertical_datum_realization_class = [&datum_field](std::uint32_t x,
                                                              std::uint32_t y) {
      return datum_field.realization_class.at(
          static_cast<std::size_t>(y) * datum_field.grid.longitudes.size() + x);
    };
    options.vertical_datum_support_class = [&datum_field](std::uint32_t x,
                                                          std::uint32_t y) {
      return datum_field.support_class.at(
          static_cast<std::size_t>(y) * datum_field.grid.longitudes.size() + x);
    };
    options.vertical_datum_station_count = [&datum_field](std::uint32_t x,
                                                          std::uint32_t y) {
      return datum_field.station_count.at(
          static_cast<std::size_t>(y) * datum_field.grid.longitudes.size() + x);
    };
    eg::test::WriteXtdV2Fixture(output, options);
    WriteProvenance(output, background_paths, catalogue, field,
                    assimilation_options, datum_catalogue_path,
                    datum_source_provenance, datum_field,
                    datum_stations.size());
    auto verification = eg::VerifyXtdPackage(output);
    verification["observation_station_records"] =
        Json::UInt64(catalogue.stations.size());
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, verification);
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
