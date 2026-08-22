#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <json/json.h>
#include <sodium.h>

#include "environmental_grib/geo.h"
#include "environmental_grib/xtd_package.h"
#include "eot20_model.h"
#include "xtd_test_support.h"

namespace eg = environmental_grib;
namespace xa = xtidal::authoring;

namespace {

std::string Sha256(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not hash " + path.string());
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

void WriteProvenance(const std::filesystem::path& directory,
                     const std::filesystem::path& output,
                     const eg::TideHeightHarmonics& field,
                     const std::string& source_id,
                     double imaginary_sign) {
  Json::Value provenance(Json::objectValue);
  provenance["schema"] = "xtdt-provenance";
  provenance["schema_version"] = 1;
  provenance["xtdt_file"] = output.filename().string();
  provenance["xtdt_sha256"] = Sha256(output);
  provenance["xtdt_bytes"] = Json::UInt64(std::filesystem::file_size(output));
  provenance["package_id"] = eg::InspectXtdPackage(output)["package_id"];
  provenance["created_utc"] = eg::FormatUtcDateTime(
      std::chrono::floor<std::chrono::seconds>(
          std::chrono::system_clock::now()));
  provenance["product"] = "derived astronomical water-level harmonics";
  auto& source = provenance["authoring_sources"][0];
  if (source_id == "eot20") {
    source["name"] = "EOT20 ocean tide atlas";
    source["doi"] = "10.17882/79489";
    source["license"] = "CC-BY-4.0";
  } else if (source_id == "hamtide11a") {
    source["name"] = "HAMTIDE11a ocean tide atlas";
    source["citation"] =
        "Taguchi, Stammer and Zahel (2014), doi:10.1002/2013JC009766";
    source["distributor"] =
        "Integrated Climate Data Center, University of Hamburg";
    source["access"] = "unrestricted official THREDDS catalogue";
  } else {
    source["name"] = "GTSM v4.1 ocean tide harmonics";
    source["institution"] = "Deltares";
    source["license"] = "CC-BY-4.0";
    source["resolution"] = "1/16 degree source, sampled to 1/8 degree";
    source["access"] = "official Deltares THREDDS catalogue";
  }
  source["role"] = "offline authoring and ensemble validation only";
  source["imaginary_multiplier"] = imaginary_sign;
  if (source_id == "eot20") {
    source["excluded_constituents"].append("sa");
    source["excluded_constituents"].append("ssa");
    source["exclusion_reason"] =
        "EOT20 documentation warns that SA and SSA contain meteorological "
        "as well as gravitational energy";
  }
  source["quality_screen"]["maximum_absolute_component_m"] = 8.0;
  source["quality_screen"]["action"] =
      "reject complete EOT20 ensemble member at affected cell; do not clip";
  source["coastal_extension"]["method"] = "nearest original wet node";
  source["coastal_extension"]["maximum_distance_degrees"] = 0.25;
  source["coastal_extension"]["support_class"] = "model-only";
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".nc")
      continue;
    Json::Value input(Json::objectValue);
    input["name"] = entry.path().filename().string();
    input["bytes"] = Json::UInt64(std::filesystem::file_size(entry.path()));
    input["sha256"] = Sha256(entry.path());
    source["inputs"].append(std::move(input));
  }
  provenance["distribution"]["contains_raw_eot20"] = false;
  provenance["distribution"]["runtime_requires_eot20"] = false;
  provenance["grid"]["spacing_degrees"] = field.grid.spacing_deg;
  provenance["grid"]["nx"] = Json::UInt64(field.grid.nx());
  provenance["grid"]["ny"] = Json::UInt64(field.grid.ny());
  provenance["vertical_datum"]["id"] = field.datum_id;
  provenance["vertical_datum"]["name"] = field.datum_name;
  provenance["vertical_datum"]["warning"] =
      "Model mean sea level is not Chart Datum";
  for (const auto& constituent : field.constituents)
    provenance["constituents"].append(constituent);
  std::ofstream sidecar(output.string() + ".prv");
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  sidecar << Json::writeString(writer, provenance);
  if (!sidecar) throw std::runtime_error("could not write .xtdt.prv sidecar");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: xtidal_model_height_author "
                 "eot20|hamtide11a|gtsm41 OCEAN_DIRECTORY IMAGINARY_SIGN "
                 "OUTPUT.xtdt\n";
    return 2;
  }
  try {
    const std::string source_id = argv[1];
    const std::filesystem::path directory = argv[2];
    const double imaginary_sign = std::stod(argv[3]);
    const std::filesystem::path output = argv[4];
    if (output.extension() != ".xtdt")
      throw std::runtime_error("EOT20 output must use .xtdt");
    const auto field = source_id == "eot20"
                           ? xa::LoadEot20HeightModel(directory, imaginary_sign)
                       : source_id == "hamtide11a"
                           ? xa::LoadHamtide11aHeightModel(directory,
                                                          imaginary_sign)
                       : source_id == "gtsm41"
                           ? xa::LoadGtsm41HeightModel(directory,
                                                      imaginary_sign)
                           : throw std::runtime_error(
                                 "source must be eot20, hamtide11a or gtsm41");
    const auto points = field.grid.size();
    for (std::size_t constituent = 0;
         constituent < field.constituents.size(); ++constituent) {
      double maximum = 0.0;
      std::size_t maximum_point = 0;
      for (std::size_t point = 0; point < points; ++point) {
        const auto value = field.coefficients_m[constituent * points + point];
        if (std::isfinite(value.real())) {
          const double candidate =
              std::max(std::abs(value.real()), std::abs(value.imag()));
          if (candidate > maximum) {
            maximum = candidate;
            maximum_point = point;
          }
        }
      }
      std::cerr << field.constituents[constituent] << " max_component_m="
                << maximum << " lat="
                << field.grid.latitudes[maximum_point / field.grid.nx()]
                << " lon="
                << field.grid.longitudes[maximum_point % field.grid.nx()]
                << '\n';
    }

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
    options.tide.nx = static_cast<std::uint32_t>(field.grid.nx());
    options.tide.ny = static_cast<std::uint32_t>(field.grid.ny());
    options.tide.west = field.grid.longitudes.front();
    options.tide.south = field.grid.latitudes.front();
    options.tide.east = field.grid.longitudes.back();
    options.tide.north = field.grid.latitudes.back();
    options.tide.lon_u0 = options.tide.lon_v0 = field.grid.longitudes.front();
    options.tide.lat_u0 = options.tide.lat_v0 = field.grid.latitudes.front();
    options.tide.lon_step = options.tide.lat_step = field.grid.spacing_deg;
    options.tile_width = 64;
    options.tile_height = 64;
    options.quantization_scale = 0.0005F;
    options.height_reference_level_m = field.reference_level_m;
    options.height_datum_id = field.datum_id;
    options.height_datum_name = field.datum_name;
    options.height_constituents = field.constituents;
    options.valid = [&field](std::uint32_t x, std::uint32_t y) {
      const auto point = static_cast<std::size_t>(y) * field.grid.nx() + x;
      return field.mask.empty() || field.mask.at(point) == 0;
    };
    options.height_value = [&field, points](std::size_t component,
                                            std::uint32_t x,
                                            std::uint32_t y) {
      const auto point = static_cast<std::size_t>(y) * field.grid.nx() + x;
      const auto value =
          field.coefficients_m.at((component / 2) * points + point);
      if (!std::isfinite(value.real()) || !std::isfinite(value.imag()))
        return 0.0;
      return component % 2 == 0 ? value.real() : value.imag();
    };
    eg::test::WriteXtdV2Fixture(output, options);
    WriteProvenance(directory, output, field, source_id, imaginary_sign);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, eg::VerifyXtdPackage(output));
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
