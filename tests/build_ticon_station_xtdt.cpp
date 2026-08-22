#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#include "environmental_grib/error.h"
#include "environmental_grib/tpxo.h"
#include "ticon_stations.h"
#include "xtd_test_support.h"

int main(int argc, char** argv) {
  if (argc != 6 && argc != 7) {
    std::cerr << "usage: xtidal_build_ticon_station_xtdt TICON.json "
                 "STATION_ID LAT LON OUTPUT.xtdt [background15]\n";
    return 2;
  }
  try {
    const auto catalogue =
        xtidal::authoring::LoadTicon3Catalogue(argv[1]);
    const auto found = std::find_if(
        catalogue.stations.begin(), catalogue.stations.end(),
        [&](const auto& station) { return station.id == argv[2]; });
    if (found == catalogue.stations.end())
      throw std::runtime_error("TICON-3 station id not found");
    std::vector<std::string> names;
    std::vector<std::complex<double>> coefficients;
    const std::set<std::string> background15{
        "2n2", "k1", "k2", "m2", "m4", "mf", "mm", "mn4",
        "ms4", "n2", "o1", "p1", "q1", "s1", "s2"};
    const bool background_only = argc == 7 && std::string(argv[6]) ==
                                                  "background15";
    if (argc == 7 && !background_only)
      throw std::runtime_error("unknown TICON constituent profile");
    const environmental_grib::TimePoint probe{
        std::chrono::seconds{1'787'184'000}};
    for (const auto& [name, value] : found->coefficients_m) {
      if (background_only && !background15.contains(name)) continue;
      try {
        (void)environmental_grib::PredictAtlasHarmonicGrid(
            {name}, {value}, 1, {probe}, false);
        names.push_back(name);
        coefficients.push_back(value);
      } catch (const environmental_grib::ValidationError&) {
      }
    }
    if (names.empty())
      throw std::runtime_error("TICON-3 station has no runtime constituents");
    environmental_grib::test::XtdV2FixtureOptions options;
    options.randomize_crypto = true;
    options.include_tide = false;
    options.include_residual = false;
    options.include_uncertainty = false;
    options.include_height = true;
    options.outer_metadata["package_profile"] = "xtidal-tide-v1";
    options.tide.nx = 2;
    options.tide.ny = 2;
    options.tide.lon_u0 = options.tide.lon_v0 = std::stod(argv[4]) - 0.01;
    options.tide.lat_u0 = options.tide.lat_v0 = std::stod(argv[3]) - 0.01;
    options.tide.lon_step = options.tide.lat_step = 0.02;
    options.tide.west = std::stod(argv[4]) - 0.01;
    options.tide.east = std::stod(argv[4]) + 0.01;
    options.tide.south = std::stod(argv[3]) - 0.01;
    options.tide.north = std::stod(argv[3]) + 0.01;
    options.tile_width = 2;
    options.tile_height = 2;
    options.quantization_scale = 0.0005F;
    options.height_reference_level_m = 0.0;
    options.height_datum_id = "model-mean-sea-level";
    options.height_datum_name = "Model mean sea level";
    options.height_constituents = names;
    options.height_value =
        [coefficients](std::size_t field, std::uint32_t, std::uint32_t) {
          const auto value = coefficients.at(field / 2);
          return field % 2 == 0 ? value.real() : value.imag();
        };
    environmental_grib::test::WriteXtdV2Fixture(argv[5], options);
    std::cout << found->id << ' ' << names.size() << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
