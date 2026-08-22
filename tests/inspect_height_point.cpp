#include <cstdlib>
#include <cmath>
#include <iostream>

#include <json/json.h>

#include "environmental_grib/geo.h"
#include "environmental_grib/xtd_package.h"

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: xtidal_inspect_height_point PACKAGE LAT LON "
                 "[GLOBAL_SPACING]\n";
    return 2;
  }
  try {
    const double latitude = std::stod(argv[2]);
    const double longitude = std::stod(argv[3]);
    environmental_grib::RegularGrid grid;
    std::size_t selected = 0;
    if (argc == 5) {
      grid = environmental_grib::BuildRegularGrid({-180.0, -90.0, 180.0, 90.0},
                                                  std::stod(argv[4]));
      const auto x = static_cast<std::size_t>(
          std::llround((longitude + 180.0) / std::stod(argv[4])));
      const auto y = static_cast<std::size_t>(
          std::llround((latitude + 90.0) / std::stod(argv[4])));
      selected = y * grid.longitudes.size() + x;
    } else {
      grid.latitudes = {latitude};
      grid.longitudes = {longitude};
    }
    environmental_grib::XtdPackageReader reader(argv[1]);
    const auto sample = reader.SampleHeightHarmonics(grid);
    Json::Value result(Json::objectValue);
    result["valid"] = sample.mask.empty() || sample.mask.at(selected) == 0;
    result["datum_id"] = sample.datum_id;
    if (reader.status().height_quality_available) {
      const auto quality = reader.SampleHeightQuality(grid);
      result["quality"]["valid"] =
          quality.mask.empty() || quality.mask.at(selected) == 0;
      result["quality"]["harmonic_sigma_m"] =
          quality.harmonic_sigma_m.at(selected);
      result["quality"]["datum_sigma_m"] = quality.datum_sigma_m.at(selected);
      result["quality"]["nearest_observation_distance_km"] =
          quality.nearest_observation_distance_km.at(selected);
      result["quality"]["support_class"] = quality.support_class.at(selected);
      result["quality"]["observation_count"] =
          quality.observation_count.at(selected);
    }
    if (reader.status().vertical_datum_available) {
      const auto datum = reader.SampleVerticalDatum(grid);
      const bool valid = datum.mask.empty() || datum.mask.at(selected) == 0;
      result["vertical_datum"]["valid"] = valid;
      result["vertical_datum"]["source_datum_id"] = datum.source_datum_id;
      result["vertical_datum"]["target_datum_id"] = datum.target_datum_id;
      result["vertical_datum"]["epoch"] = datum.epoch;
      if (valid) {
        result["vertical_datum"]["offset_m"] = datum.offset_m.at(selected);
        result["vertical_datum"]["uncertainty_m"] =
            datum.uncertainty_m.at(selected);
        result["vertical_datum"]["nearest_station_distance_km"] =
            datum.nearest_station_distance_km.at(selected);
        result["vertical_datum"]["realization_class"] =
            datum.realization_class.at(selected);
        result["vertical_datum"]["support_class"] =
            datum.support_class.at(selected);
        result["vertical_datum"]["station_count"] =
            datum.station_count.at(selected);
      }
    }
    for (std::size_t index = 0; index < sample.constituents.size(); ++index) {
      result["coefficients"][sample.constituents[index]]["real_m"] =
          sample.coefficients_m[index * grid.size() + selected].real();
      result["coefficients"][sample.constituents[index]]["imaginary_m"] =
          sample.coefficients_m[index * grid.size() + selected].imag();
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::cout << Json::writeString(writer, result);
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
