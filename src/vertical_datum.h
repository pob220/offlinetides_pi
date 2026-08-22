#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "environmental_grib/xtd_package.h"
#include "ticon_stations.h"

namespace xtidal::authoring {

enum class DatumRealization : std::uint8_t {
  kLowestAstronomicalTide = 1,
  kMeanLowerLowWater = 2,
  kLowerLowWaterLargeTide = 3,
  kNearlyLowestLowWater = 4,
  kMeanLowWaterSprings = 5,
  kMeanSeaLevel = 6,
  kLowestLowWater = 7,
  kOtherAuthorityChartDatum = 8,
};

enum class DatumSupportClass : std::uint8_t {
  kAuthorityGrid = 1,
  kStationConstrained = 2,
  kEstuaryStationConstrained = 3,
  kModelDerived = 4,
};

struct ChartDatumStation {
  std::string id;
  std::string name;
  std::string country;
  std::string source;
  double latitude{};
  double longitude{};
  double msl_above_chart_datum_m{};
  DatumRealization realization{DatumRealization::kLowestAstronomicalTide};
  bool estuary{};
  std::uint8_t confidence{};
};

struct VerticalDatumAuthoringOptions {
  double coastal_support_radius_km{100.0};
  double estuary_support_radius_km{35.0};
  double maximum_station_snap_km{40.0};
  double coastal_uncertainty_floor_m{0.10};
  double estuary_uncertainty_floor_m{0.16};
  double distance_uncertainty_m_per_km{0.0015};
};

struct VerticalDatumField {
  environmental_grib::RegularGrid grid;
  std::vector<double> offset_m;
  std::vector<double> uncertainty_m;
  std::vector<double> nearest_station_distance_km;
  std::vector<std::uint8_t> realization_class;
  std::vector<std::uint8_t> support_class;
  std::vector<std::uint16_t> station_count;
  std::vector<std::uint8_t> valid;

  void Validate() const;
};

// Loads public-domain reference-station datum offsets exported from a TCD
// catalogue. TICON observations are used only to classify river/estuary
// stations; they do not supply Chart Datum offsets.
std::vector<ChartDatumStation> LoadChartDatumStations(
    const std::filesystem::path& path,
    const Ticon3Catalogue* ticon_catalogue = nullptr);

VerticalDatumField BuildVerticalDatumField(
    const environmental_grib::TideHeightHarmonics& water_mask,
    const std::vector<ChartDatumStation>& stations,
    const VerticalDatumAuthoringOptions& options = {});

}  // namespace xtidal::authoring
