#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "xtd_test_support.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: xtidal_make_fixture OUTPUT.xtd\n";
    return 2;
  }
  environmental_grib::test::XtdV2FixtureOptions options;
  auto& tide = options.tide;
  tide.nx = 33;
  tide.ny = 29;
  tide.tile_width = 8;
  tide.tile_height = 8;
  tide.west = -12.0;
  tide.east = 4.0;
  tide.south = 48.0;
  tide.north = 62.0;
  tide.lon_u0 = -12.0;
  tide.lon_v0 = -12.0;
  tide.lon_step = 0.5;
  tide.lat_u0 = 48.0;
  tide.lat_v0 = 48.0;
  tide.lat_step = 0.5;
  tide.quantization_scale = 0.02F;
  tide.coefficient_bits = 16;
  tide.constituents = {"m2", "s2", "k1", "o1", "n2", "p1", "k2", "q1"};
  tide.metadata["dataset_name"] = "X-Tidal synthetic UK developer fixture";
  tide.metadata["purpose"] = "software testing only; not for navigation";
  tide.value = [](std::size_t constituent, std::size_t component,
                     std::uint32_t x, std::uint32_t y) {
    const double spatial = 1.0 + 0.18 * std::sin(x * 0.31) +
                           0.12 * std::cos(y * 0.27);
    const double amplitude = constituent == 0 ? 42.0 : 9.0 / (constituent + 1);
    const double phase = component == 0   ? 1.0
                         : component == 1 ? 0.35
                         : component == 2 ? 0.75
                                          : -0.25;
    return amplitude * spatial * phase;
  };
  options.tile_width = 8;
  options.tile_height = 8;
  options.include_height = true;
  options.height_reference_level_m = 5.2226;
  options.height_datum_id = "developer-chart-datum";
  options.height_datum_name =
      "Synthetic developer chart datum (not for navigation)";
  options.height_constituents = {
      "m2",  "s2",  "k1",  "o1", "n2",  "p1", "k2",
      "q1",  "2n2", "mu2", "nu2", "l2", "t2", "mf",
      "mm",  "m4",  "ms4", "mn4", "s1"};
  options.quantization_scale = 0.0002F;
  options.height_value = [](std::size_t field, std::uint32_t x,
                            std::uint32_t y) {
    static constexpr double amplitudes[]{
        3.0306, 0.9717, 0.1220, 0.1132, 0.5793, 0.0471, 0.2848,
        0.0339, 0.0680, 0.0414, 0.1324, 0.1417, 0.0520, 0.0254,
        0.0310, 0.2344, 0.1422, 0.0946, 0.0160};
    static constexpr double phases[]{
        320.74, 4.33,   191.14, 38.51, 298.05, 178.53, 3.77,
        349.64, 276.06, 36.75,  297.35, 332.31, 356.92, 204.32,
        250.16, 202.63, 245.40, 175.56, 96.45};
    const auto constituent = field / 2;
    const double radians = phases[constituent] * std::acos(-1.0) / 180.0;
    const double spatial = 1.0 + 0.025 * std::sin(x * 0.19) -
                           0.02 * std::cos(y * 0.17);
    return amplitudes[constituent] * spatial *
           (field % 2 == 0 ? std::cos(radians) : std::sin(radians));
  };
  try {
    const std::filesystem::path output(argv[1]);
    if (!output.parent_path().empty())
      std::filesystem::create_directories(output.parent_path());
    environmental_grib::test::WriteXtdV2Fixture(output, options);
    std::cout << output << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
