#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>
#include <sodium.h>

#include "environmental_grib/geo.h"
#include "environmental_grib/xtd_package.h"
#include "height_correction.h"
#include "station_harmonics.h"
#include "xtd_test_support.h"

namespace eg = environmental_grib;
namespace xa = xtidal::authoring;

namespace {
Json::Value ReadJson(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open correction catalogue");
  Json::CharReaderBuilder parser;
  Json::Value result;
  std::string errors;
  if (!Json::parseFromStream(parser, input, &result, &errors))
    throw std::runtime_error("invalid correction catalogue: " + errors);
  return result;
}

std::string Sha256(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not hash authored XTD file");
  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);
  std::array<unsigned char, 1024 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    const auto count = input.gcount();
    if (count > 0)
      crypto_hash_sha256_update(&state, buffer.data(),
                                static_cast<unsigned long long>(count));
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

void WriteProvenance(const std::filesystem::path& output,
                     const std::filesystem::path& background,
                     const std::filesystem::path& harmonic_source,
                     const Json::Value& catalogue,
                     const xa::CorrectedHeightField& corrected) {
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
      "observation-supported local Chart Datum harmonic corrections";
  provenance["method"] =
      catalogue["schema_version"].asInt() >= 2
          ? "topology-bounded anisotropic uncertainty-"
            "weighted complex harmonic interpolation"
          : "bounded complex harmonic innovation interpolation";
  provenance["background_xtd"]["file"] = background.filename().string();
  provenance["background_xtd"]["sha256"] = Sha256(background);
  provenance["station_source"]["file"] = harmonic_source.filename().string();
  provenance["station_source"]["role"] = "offline correction authoring only";
  provenance["distribution"]["contains_raw_tpxo"] = false;
  provenance["distribution"]["runtime_requires_tpxo"] = false;
  provenance["distribution"]["runtime_requires_station_source"] = false;
  provenance["field"]["unsupported_points_are_masked"] = true;
  provenance["field"]["interpolates_amplitude_phase_separately"] = false;
  provenance["field"]["datum_support_is_separate"] =
      catalogue["schema_version"].asInt() >= 2;
  provenance["field"]["constituent_specific_support"] =
      catalogue["schema_version"].asInt() >= 2;
  provenance["field"]["anchor_count"] =
      Json::UInt64(corrected.anchor_ids.size());
  provenance["field"]["valid_points"] = Json::UInt64(std::count(
      corrected.valid.begin(), corrected.valid.end(), std::uint8_t{1}));
  provenance["calibration_year"] = catalogue["calibration_year"];
  if (!catalogue["calibration"].isNull()) {
    provenance["calibration"] = catalogue["calibration"];
  }
  provenance["anchors"] = catalogue["stations"];
  provenance["validation_warning"] =
      "Developer correction product. Validate independently before navigation.";
  std::ofstream sidecar(output.string() + ".prv");
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  sidecar << Json::writeString(writer, provenance);
  if (!sidecar) throw std::runtime_error("could not write provenance sidecar");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: xtidal_build_corrected_xtd BASE.xtd HARMONICS "
                 "CORRECTIONS.json OUTPUT.xtdt\n";
    return 2;
  }
  try {
    const auto catalogue = ReadJson(argv[3]);
    const int schema_version = catalogue["schema_version"].asInt();
    if ((schema_version != 1 && schema_version != 2) ||
        !catalogue["stations"].isArray() || catalogue["stations"].empty())
      throw std::runtime_error("unsupported or empty correction catalogue");
    const eg::BoundingBox bbox{catalogue["grid"]["west"].asDouble(),
                               catalogue["grid"]["south"].asDouble(),
                               catalogue["grid"]["east"].asDouble(),
                               catalogue["grid"]["north"].asDouble()};
    const double spacing = catalogue["grid"]["spacing_deg"].asDouble();
    const int calibration_year = catalogue["calibration_year"].asInt();
    const auto grid = eg::BuildRegularGrid(bbox, spacing);
    eg::XtdPackageReader base(argv[1]);
    const auto background = base.SampleHeightHarmonics(grid);

    std::vector<xa::HarmonicAnchor> anchors;
    for (const auto& item : catalogue["stations"]) {
      const auto station = xa::LoadStationHarmonicConstants(
          argv[2], item["source_prefix"].asString(), calibration_year);
      xa::HarmonicAnchor anchor;
      anchor.id = item["id"].asString();
      anchor.name = station.name;
      anchor.latitude = item["latitude"].asDouble();
      anchor.longitude = item["longitude"].asDouble();
      anchor.support_radius_km = item["support_radius_km"].asDouble();
      if (schema_version >= 2) {
        anchor.station_class =
            item.get("station_class", "open-coast").asString();
        anchor.hydro_region = item.get("hydro_region", "").asString();
        anchor.axis_bearing_degrees =
            item.get("axis_bearing_degrees", 0.0).asDouble();
        anchor.quality_weight = item.get("quality_weight", 1.0).asDouble();
        anchor.major_correction_gain =
            item.get("major_correction_gain", 1.0).asDouble();
        anchor.shallow_correction_gain =
            item.get("shallow_correction_gain", 1.0).asDouble();
        anchor.prediction_time_shift_minutes =
            item.get("prediction_time_shift_minutes", 0.0).asDouble();
        anchor.amplitude_scale = item.get("amplitude_scale", 1.0).asDouble();
        anchor.chart_datum_offset_m =
            item.get("chart_datum_offset_m", 0.0).asDouble();
        const auto read_support = [&](const char* name, double* along,
                                      double* across) {
          const auto& support = item[name];
          if (!support.isObject()) return;
          *along = support.get("along", 0.0).asDouble();
          *across = support.get("across", 0.0).asDouble();
        };
        read_support("major_support_km", &anchor.major_along_support_km,
                     &anchor.major_cross_support_km);
        read_support("shallow_support_km", &anchor.shallow_along_support_km,
                     &anchor.shallow_cross_support_km);
        read_support("datum_support_km", &anchor.datum_along_support_km,
                     &anchor.datum_cross_support_km);
        if (item["support_polygon"].isArray()) {
          for (const auto& coordinate : item["support_polygon"]) {
            if (!coordinate.isArray() || coordinate.size() != 2)
              throw std::runtime_error("support polygon coordinate is invalid");
            anchor.support_polygon.emplace_back(coordinate[0].asDouble(),
                                                coordinate[1].asDouble());
          }
        }
      }
      anchor.chart_datum_reference_m = station.chart_datum_reference_m;
      anchor.coefficients_m = station.coefficients_m;
      anchors.push_back(std::move(anchor));
    }
    const auto corrected =
        xa::ApplyLocalHarmonicCorrections(background, anchors);
    const auto points = corrected.grid.size();

    eg::test::XtdV2FixtureOptions options;
    options.randomize_crypto = true;
    options.include_tide = false;
    options.include_residual = false;
    options.include_uncertainty = false;
    options.include_height = true;
    options.outer_metadata["package_profile"] = "xtidal-tide-v1";
    options.outer_metadata["recommended_extension"] = ".xtdt";
    options.outer_metadata["capabilities"]["water_level_harmonics"] = true;
    options.outer_metadata["capabilities"]["tidal_current_harmonics"] = false;
    options.outer_metadata["capabilities"]["water_level_quality"] = false;
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
    // Chart-Datum reference levels at large-range ports exceed the signed
    // int16 range at 0.2 mm.  Half-millimetre quantisation still makes the
    // encoding error negligible compared with the source observations.
    options.quantization_scale = 0.0005F;
    options.height_reference_level_m = 0.0;
    options.height_datum_id = "chart-datum";
    options.height_datum_name = "Chart Datum";
    options.height_constituents = corrected.constituents;
    options.valid = [&corrected](std::uint32_t x, std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * corrected.grid.longitudes.size() + x;
      return corrected.valid.at(point) != 0;
    };
    options.height_value = [&corrected, points](std::size_t field,
                                                std::uint32_t x,
                                                std::uint32_t y) {
      const auto point =
          static_cast<std::size_t>(y) * corrected.grid.longitudes.size() + x;
      const auto value = corrected.coefficients_m[(field / 2) * points + point];
      if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return 0.0;
      return field % 2 == 0 ? value.real() : value.imag();
    };
    eg::test::WriteXtdV2Fixture(argv[4], options);
    WriteProvenance(argv[4], argv[1], argv[2], catalogue, corrected);

    auto inspection = eg::InspectXtdPackage(argv[4]);
    inspection["correction_anchor_count"] = Json::UInt64(anchors.size());
    inspection["correction_valid_points"] = Json::UInt64(std::count(
        corrected.valid.begin(), corrected.valid.end(), std::uint8_t{1}));
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, inspection);
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
