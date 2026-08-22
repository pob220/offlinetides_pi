#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <complex>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>

#include "environmental_grib/error.h"
#include "environmental_grib/tpxo.h"
#include "xtd_test_support.h"
#include "xtidal_prediction.h"

namespace {
constexpr double kPi = 3.14159265358979323846;

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

class Tokens {
public:
  Tokens(const std::vector<std::string>& lines, std::size_t start) {
    for (std::size_t index = start; index < lines.size(); ++index) {
      auto line = lines[index];
      const auto comment = line.find('#');
      if (comment != std::string::npos) line.resize(comment);
      std::istringstream input(line);
      std::string token;
      while (input >> token) values_.push_back(token);
    }
  }

  std::string Next() {
    if (position_ >= values_.size())
      throw std::runtime_error("unexpected end of harmonic header");
    return values_[position_++];
  }
  int NextInt() { return std::stoi(Next()); }
  double NextDouble() { return std::stod(Next()); }

private:
  std::vector<std::string> values_;
  std::size_t position_{};
};

struct HarmonicHeader {
  int first_year{};
  int years{};
  std::vector<std::string> names;
  std::vector<double> speed_rad_s;
  std::vector<std::vector<double>> equilibrium_rad;
  std::vector<std::vector<double>> node;
};

struct Station {
  std::string name;
  int meridian_seconds{};
  double datum{};
  std::string units;
  std::vector<double> amplitude;
  std::vector<double> phase_rad;
};

HarmonicHeader ParseHeader(const std::vector<std::string>& lines) {
  const auto marker =
      std::find_if(lines.begin(), lines.end(), [](const auto& l) {
        return l.find("Begin congen output") != std::string::npos;
      });
  if (marker == lines.end())
    throw std::runtime_error("harmonic header marker not found");
  Tokens input(lines, static_cast<std::size_t>(marker - lines.begin() + 1));
  const int count = input.NextInt();
  HarmonicHeader result;
  result.names.resize(count);
  result.speed_rad_s.resize(count);
  for (int index = 0; index < count; ++index) {
    result.names[index] = input.Next();
    result.speed_rad_s[index] = input.NextDouble() * kPi / (180.0 * 3600.0);
  }
  result.first_year = input.NextInt();
  result.years = input.NextInt();
  result.equilibrium_rad.assign(count, std::vector<double>(result.years));
  for (int constituent = 0; constituent < count; ++constituent) {
    (void)input.Next();
    for (int year = 0; year < result.years; ++year)
      result.equilibrium_rad[constituent][year] =
          input.NextDouble() * kPi / 180.0;
  }
  if (input.Next() != "*END*")
    throw std::runtime_error("equilibrium table terminator is missing");
  const int node_years = input.NextInt();
  if (node_years != result.years)
    throw std::runtime_error("equilibrium/node year counts differ");
  result.node.assign(count, std::vector<double>(result.years));
  for (int constituent = 0; constituent < count; ++constituent) {
    (void)input.Next();
    for (int year = 0; year < result.years; ++year)
      result.node[constituent][year] = input.NextDouble();
  }
  return result;
}

std::string NextDataLine(const std::vector<std::string>& lines,
                         std::size_t& position) {
  while (++position < lines.size()) {
    const auto line = Trim(lines[position]);
    if (!line.empty() && line.front() != '#') return line;
  }
  throw std::runtime_error("unexpected end of station record");
}

int ParseMeridianSeconds(const std::string& value) {
  int hours = 0, minutes = 0;
  char separator = 0;
  std::istringstream input(value);
  input >> hours >> separator >> minutes;
  if (!input || separator != ':')
    throw std::runtime_error("invalid station meridian");
  if (hours < 0) minutes = -minutes;
  return hours * 3600 + minutes * 60;
}

Station ParseStation(const std::vector<std::string>& lines,
                     const HarmonicHeader& header, const std::string& prefix) {
  for (std::size_t position = 0; position < lines.size(); ++position) {
    const auto candidate = Trim(lines[position]);
    if (candidate.empty() || candidate.front() == '#' ||
        candidate.rfind(prefix, 0) != 0)
      continue;
    Station station;
    station.name = candidate;
    const auto meridian_line = NextDataLine(lines, position);
    station.meridian_seconds =
        ParseMeridianSeconds(meridian_line.substr(0, meridian_line.find(' ')));
    std::istringstream datum_line(NextDataLine(lines, position));
    datum_line >> station.datum >> station.units;
    if (!datum_line) throw std::runtime_error("invalid station datum line");
    station.amplitude.resize(header.names.size());
    station.phase_rad.resize(header.names.size());
    for (std::size_t index = 0; index < header.names.size(); ++index) {
      std::string name;
      double amplitude = 0.0, phase = 0.0;
      std::istringstream constituent(NextDataLine(lines, position));
      constituent >> name >> amplitude >> phase;
      if (!constituent)
        throw std::runtime_error("invalid station constituent line");
      station.amplitude[index] = amplitude;
      station.phase_rad[index] = phase * kPi / 180.0;
    }
    return station;
  }
  throw std::runtime_error("station not found: " + prefix);
}

int UtcYear(xtidal::TimePoint time) {
  const auto raw = static_cast<time_t>(time.time_since_epoch().count());
  std::tm value{};
  gmtime_r(&raw, &value);
  return value.tm_year + 1900;
}

xtidal::TimePoint UtcYearStart(int year) {
  std::tm value{};
  value.tm_year = year - 1900;
  value.tm_mon = 0;
  value.tm_mday = 1;
  return xtidal::TimePoint{std::chrono::seconds{timegm(&value)}};
}

double SourceHeight(const HarmonicHeader& header, const Station& station,
                    int year_index, xtidal::TimePoint year_start,
                    xtidal::TimePoint time) {
  const double seconds =
      std::chrono::duration<double>(time - year_start).count();
  double result = station.datum;
  for (std::size_t index = 0; index < header.names.size(); ++index) {
    result += station.amplitude[index] * header.node[index][year_index] *
              std::cos(header.speed_rad_s[index] *
                           (seconds + station.meridian_seconds) +
                       header.equilibrium_rad[index][year_index] -
                       station.phase_rad[index]);
  }
  return result;
}

std::string IsoUtc(xtidal::TimePoint time) {
  const auto raw = static_cast<time_t>(time.time_since_epoch().count());
  std::tm value{};
  gmtime_r(&raw, &value);
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 9) {
    std::cerr << "usage: xtidal_build_station_xtd HARMONICS STATION_PREFIX "
                 "LAT LON START_UNIX HOURS OUTPUT.xtd REFERENCE.json\n";
    return 2;
  }
  try {
    std::ifstream source(argv[1]);
    if (!source) throw std::runtime_error("could not open harmonic source");
    std::vector<std::string> lines;
    for (std::string line; std::getline(source, line);) lines.push_back(line);
    const auto header = ParseHeader(lines);
    auto station = ParseStation(lines, header, argv[2]);
    double unit_scale = 1.0;
    if (station.units == "feet") {
      unit_scale = 0.3048;
    } else if (station.units != "meters") {
      throw std::runtime_error("unsupported station harmonic height unit: " +
                               station.units);
    }
    station.datum *= unit_scale;
    for (auto& amplitude : station.amplitude) amplitude *= unit_scale;
    const double latitude = std::stod(argv[3]);
    const double longitude = std::stod(argv[4]);
    const xtidal::TimePoint start{std::chrono::seconds{std::stoll(argv[5])}};
    const auto duration = std::chrono::hours{std::stoi(argv[6])};
    const int year = UtcYear(start);
    const int year_index = year - header.first_year;
    if (year_index < 0 || year_index >= header.years)
      throw std::runtime_error("requested year is outside harmonic tables");
    const auto year_start = UtcYearStart(year);
    const auto calibration_time = year_start + std::chrono::hours{24 * 14};

    std::vector<std::string> names;
    std::vector<double> coefficients;
    for (std::size_t index = 0; index < header.names.size(); ++index) {
      if (station.amplitude[index] == 0.0) continue;
      const auto name = Lower(header.names[index]);
      try {
        const auto real_basis = environmental_grib::PredictAtlasHarmonicGrid(
            {name}, {std::complex<double>{1.0, 0.0}}, 1, {calibration_time},
            false)[0];
        const auto imaginary_basis =
            environmental_grib::PredictAtlasHarmonicGrid(
                {name}, {std::complex<double>{0.0, 1.0}}, 1, {calibration_time},
                false)[0];
        const double factor = std::hypot(real_basis, imaginary_basis);
        const double generic_phase = std::atan2(-imaginary_basis, real_basis);
        const double seconds =
            std::chrono::duration<double>(calibration_time - year_start)
                .count();
        const double source_phase =
            header.speed_rad_s[index] * (seconds + station.meridian_seconds) +
            header.equilibrium_rad[index][year_index] -
            station.phase_rad[index];
        const double amplitude =
            station.amplitude[index] * header.node[index][year_index] / factor;
        const double difference = source_phase - generic_phase;
        names.push_back(name);
        coefficients.push_back(amplitude * std::cos(difference));
        coefficients.push_back(amplitude * std::sin(difference));
      } catch (const environmental_grib::ValidationError&) {
        // Unsupported small constituents stay in the independent reference
        // curve, making any truncation error visible in the comparison.
      }
    }
    if (names.empty()) throw std::runtime_error("no supported constituents");

    environmental_grib::test::XtdV2FixtureOptions options;
    options.randomize_crypto = true;
    options.tide.nx = 3;
    options.tide.ny = 3;
    options.tide.tile_width = 3;
    options.tide.tile_height = 3;
    options.tide.west = longitude - 0.01;
    options.tide.east = longitude + 0.01;
    options.tide.south = latitude - 0.01;
    options.tide.north = latitude + 0.01;
    options.tide.lon_u0 = options.tide.lon_v0 = longitude - 0.01;
    options.tide.lat_u0 = options.tide.lat_v0 = latitude - 0.01;
    options.tide.lon_step = options.tide.lat_step = 0.01;
    options.tide.metadata["dataset_name"] =
        "Station-derived XTD validation package";
    options.tide.metadata["purpose"] =
        "headless validation; not for navigation";
    options.include_height = true;
    options.tile_width = 3;
    options.tile_height = 3;
    options.quantization_scale = 0.0002F;
    options.height_reference_level_m = station.datum;
    options.height_datum_id = "chart-datum";
    options.height_datum_name = "Chart Datum";
    options.height_constituents = names;
    options.height_value = [coefficients](std::size_t field, std::uint32_t,
                                          std::uint32_t) {
      return coefficients.at(field);
    };
    environmental_grib::test::WriteXtdV2Fixture(argv[7], options);
    {
      Json::Value provenance(Json::objectValue);
      provenance["schema"] = "xtd-provenance";
      provenance["schema_version"] = 1;
      provenance["xtd_file"] =
          std::filesystem::path(argv[7]).filename().string();
      provenance["package_id"] =
          environmental_grib::InspectXtdPackage(argv[7])["package_id"];
      provenance["product"] = "station-derived validation harmonics";
      provenance["validation_only"] = true;
      provenance["authoring_source"]["station"] = station.name;
      provenance["authoring_source"]["dataset"] =
          "OpenCPN HARMONICS_NO_US station constants";
      provenance["authoring_source"]["url"] =
          "https://github.com/OpenCPN/OpenCPN/tree/master/data/tcdata";
      std::ofstream sidecar(std::string(argv[7]) + ".prv");
      Json::StreamWriterBuilder sidecar_writer;
      sidecar_writer["indentation"] = "  ";
      sidecar << Json::writeString(sidecar_writer, provenance);
      if (!sidecar)
        throw std::runtime_error("could not write XTD provenance sidecar");
    }

    xtidal::HeightCurve reference_curve;
    reference_curve.latitude = latitude;
    reference_curve.longitude = longitude;
    reference_curve.datum_id = "chart-datum";
    reference_curve.datum_name = "Chart Datum";
    for (auto offset = std::chrono::minutes{0}; offset <= duration;
         offset += std::chrono::minutes{1}) {
      const auto time = start + offset;
      reference_curve.samples.push_back(
          {time, SourceHeight(header, station, year_index, year_start, time)});
    }
    const auto events =
        xtidal::PredictionService::FindHeightEvents(reference_curve);
    Json::Value manifest(Json::objectValue);
    manifest["schema_version"] = 1;
    manifest["station"]["id"] = Lower(argv[2]);
    manifest["station"]["name"] = station.name;
    manifest["station"]["latitude"] = latitude;
    manifest["station"]["longitude"] = longitude;
    manifest["reference"]["kind"] = "harmonic_engine_equivalence";
    manifest["reference"]["publisher"] = "OpenCPN bundled tide dataset";
    manifest["reference"]["source_url"] =
        "https://github.com/OpenCPN/OpenCPN/tree/master/data/tcdata";
    manifest["reference"]["time_basis"] = "UTC";
    manifest["reference"]["vertical_datum_id"] = "chart-datum";
    manifest["reference"]["vertical_datum_name"] = "Chart Datum";
    manifest["reference"]["independent_accuracy_evidence"] = false;
    for (const auto& event : events) {
      Json::Value item(Json::objectValue);
      item["type"] =
          event.type == xtidal::HeightEventType::kHighWater ? "high" : "low";
      item["utc"] = IsoUtc(event.time);
      item["unix_seconds"] = Json::Int64(event.time.time_since_epoch().count());
      item["height_m"] = event.height_m;
      manifest["events"].append(item);
    }
    std::ofstream reference_output(argv[8]);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    reference_output << Json::writeString(writer, manifest);
    if (!reference_output)
      throw std::runtime_error("could not write reference");
    std::cout << argv[7] << " constituents=" << names.size()
              << " reference_events=" << events.size() << '\n';
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
