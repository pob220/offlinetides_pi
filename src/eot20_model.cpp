#include "eot20_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <netcdf.h>

namespace xtidal::authoring {
namespace {

void NcCheck(int status, const std::string& context) {
  if (status != NC_NOERR)
    throw std::runtime_error(context + ": " + nc_strerror(status));
}

class NcFile {
 public:
  explicit NcFile(const std::filesystem::path& path) : path_(path) {
    NcCheck(nc_open(path.string().c_str(), NC_NOWRITE, &id_),
            "could not open EOT20 file " + path.string());
  }
  ~NcFile() {
    if (id_ >= 0) nc_close(id_);
  }
  NcFile(const NcFile&) = delete;
  NcFile& operator=(const NcFile&) = delete;

  std::vector<double> Read1d(const std::string& name) const {
    int variable = -1;
    NcCheck(nc_inq_varid(id_, name.c_str(), &variable),
            "missing EOT20 variable " + name);
    int dimensions = 0;
    NcCheck(nc_inq_varndims(id_, variable, &dimensions),
            "could not inspect EOT20 variable " + name);
    if (dimensions != 1)
      throw std::runtime_error("EOT20 " + name + " must be one-dimensional");
    int dimension = -1;
    NcCheck(nc_inq_vardimid(id_, variable, &dimension),
            "could not inspect EOT20 dimension " + name);
    std::size_t count = 0;
    NcCheck(nc_inq_dimlen(id_, dimension, &count),
            "could not inspect EOT20 dimension length " + name);
    std::vector<double> values(count);
    NcCheck(nc_get_var_double(id_, variable, values.data()),
            "could not read EOT20 variable " + name);
    return values;
  }

  std::vector<double> Read2d(const std::string& name, std::size_t ny,
                             std::size_t nx) const {
    int variable = -1;
    NcCheck(nc_inq_varid(id_, name.c_str(), &variable),
            "missing EOT20 variable " + name);
    int dimensions = 0;
    NcCheck(nc_inq_varndims(id_, variable, &dimensions),
            "could not inspect EOT20 variable " + name);
    if (dimensions != 2)
      throw std::runtime_error("EOT20 " + name + " must be two-dimensional");
    int ids[2] = {-1, -1};
    NcCheck(nc_inq_vardimid(id_, variable, ids),
            "could not inspect EOT20 dimensions " + name);
    std::size_t shape[2] = {0, 0};
    NcCheck(nc_inq_dimlen(id_, ids[0], &shape[0]),
            "could not inspect EOT20 latitude dimension");
    NcCheck(nc_inq_dimlen(id_, ids[1], &shape[1]),
            "could not inspect EOT20 longitude dimension");
    if (shape[0] != ny || shape[1] != nx)
      throw std::runtime_error("EOT20 field dimensions differ from axes");
    std::vector<double> values(ny * nx);
    NcCheck(nc_get_var_double(id_, variable, values.data()),
            "could not read EOT20 variable " + name);
    return values;
  }

 private:
  std::filesystem::path path_;
  int id_{-1};
};

std::string ConstituentName(const std::filesystem::path& path) {
  const auto filename = path.filename().string();
  const auto suffix = std::string{"_ocean_eot20.nc"};
  if (!filename.ends_with(suffix)) return {};
  auto name = filename.substr(0, filename.size() - suffix.size());
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return name;
}

bool SameAxis(const std::vector<double>& first,
              const std::vector<double>& second) {
  if (first.size() != second.size()) return false;
  for (std::size_t index = 0; index < first.size(); ++index)
    if (std::abs(first[index] - second[index]) > 1e-10) return false;
  return true;
}

}  // namespace

environmental_grib::TideHeightHarmonics LoadEot20HeightModel(
    const std::filesystem::path& ocean_tide_directory,
    double imaginary_sign) {
  if (imaginary_sign != -1.0 && imaginary_sign != 1.0)
    throw std::invalid_argument("EOT20 imaginary sign must be -1 or +1");
  // SA and SSA contain meteorological as well as astronomical energy in EOT20
  // and are intentionally excluded from an astronomical prediction product.
  const std::set<std::string> accepted{
      "2n2", "j1", "k1", "k2", "m2", "m4", "mf", "mm",
      "n2",  "o1", "p1", "q1", "s1", "s2", "t2"};
  std::map<std::string, std::filesystem::path> files;
  for (const auto& entry :
       std::filesystem::directory_iterator(ocean_tide_directory)) {
    if (!entry.is_regular_file()) continue;
    const auto name = ConstituentName(entry.path());
    if (accepted.contains(name)) files.emplace(name, entry.path());
  }
  if (files.size() != accepted.size())
    throw std::runtime_error("EOT20 directory does not contain all 15 "
                             "accepted astronomical constituents");

  NcFile mask_file(files.at("m2"));
  const auto source_latitudes = mask_file.Read1d("lat");
  const auto source_longitudes = mask_file.Read1d("lon");
  if (source_latitudes.size() != 1441 || source_longitudes.size() != 2881 ||
      std::abs(source_latitudes.front() + 90.0) > 1e-10 ||
      std::abs(source_latitudes.back() - 90.0) > 1e-10 ||
      std::abs(source_longitudes.front()) > 1e-10 ||
      std::abs(source_longitudes.back() - 360.0) > 1e-10)
    throw std::runtime_error("unexpected EOT20 1/8-degree global grid");
  const auto m2_real = mask_file.Read2d(
      "real", source_latitudes.size(), source_longitudes.size());
  const auto m2_imaginary = mask_file.Read2d(
      "imag", source_latitudes.size(), source_longitudes.size());

  environmental_grib::TideHeightHarmonics result;
  result.grid.latitudes = source_latitudes;
  result.grid.spacing_deg = 0.125;
  result.grid.latitude_spacing_deg = 0.125;
  result.grid.longitude_spacing_deg = 0.125;
  result.grid.longitudes.resize(2881);
  for (std::size_t index = 0; index < result.grid.longitudes.size(); ++index)
    result.grid.longitudes[index] = -180.0 + 0.125 * index;
  const auto points = result.grid.size();
  result.mask.assign(points, 1);

  const auto source_x = [](std::size_t output_x) {
    const double longitude = -180.0 + 0.125 * output_x;
    const double source_longitude = longitude < 0.0 ? longitude + 360.0
                                                    : longitude;
    return static_cast<std::size_t>(std::llround(source_longitude / 0.125));
  };
  for (std::size_t y = 0; y < source_latitudes.size(); ++y) {
    for (std::size_t x = 0; x < result.grid.longitudes.size(); ++x) {
      const auto source = y * source_longitudes.size() + source_x(x);
      const auto target = y * result.grid.longitudes.size() + x;
      if (std::isfinite(m2_real[source]) &&
          std::isfinite(m2_imaginary[source]) &&
          (m2_real[source] != 0.0 || m2_imaginary[source] != 0.0))
        result.mask[target] = 0;
    }
  }

  for (const auto& [name, path] : files) {
    NcFile file(path);
    if (!SameAxis(source_latitudes, file.Read1d("lat")) ||
        !SameAxis(source_longitudes, file.Read1d("lon")))
      throw std::runtime_error("EOT20 constituent axes differ: " + name);
    const auto real = file.Read2d("real", source_latitudes.size(),
                                  source_longitudes.size());
    const auto imaginary = file.Read2d("imag", source_latitudes.size(),
                                       source_longitudes.size());
    result.constituents.push_back(name);
    result.coefficients_m.resize(result.constituents.size() * points);
    const auto constituent = result.constituents.size() - 1;
    for (std::size_t y = 0; y < source_latitudes.size(); ++y) {
      for (std::size_t x = 0; x < result.grid.longitudes.size(); ++x) {
        const auto target = y * result.grid.longitudes.size() + x;
        if (result.mask[target]) {
          result.coefficients_m[constituent * points + target] = {
              std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN()};
          continue;
        }
        const auto source = y * source_longitudes.size() + source_x(x);
        // SEANOE stores centimetres.  imaginary_sign makes the source phase
        // convention explicit and testable against withheld gauges.
        result.coefficients_m[constituent * points + target] = {
            real[source] * 0.01, imaginary_sign * imaginary[source] * 0.01};
      }
    }
  }
  // EOT20 contains a small number of isolated coastal extrapolation spikes
  // (including tens-of-metres S1/S2/T2 values).  Reject the entire ensemble
  // member at such cells; never clip or expand the wire quantisation to make
  // physically implausible source values appear valid.  The 8 m component
  // bound is above the largest accepted M2 component in this atlas and is a
  // broad corruption guard, not a tidal-range calibration.
  for (std::size_t point = 0; point < points; ++point) {
    if (result.mask[point]) continue;
    bool plausible = true;
    for (std::size_t constituent = 0;
         constituent < result.constituents.size(); ++constituent) {
      const auto value = result.coefficients_m[constituent * points + point];
      if (!std::isfinite(value.real()) || !std::isfinite(value.imag()) ||
          std::abs(value.real()) > 8.0 || std::abs(value.imag()) > 8.0) {
        plausible = false;
        break;
      }
    }
    if (!plausible) {
      result.mask[point] = 1;
      for (std::size_t constituent = 0;
           constituent < result.constituents.size(); ++constituent)
        result.coefficients_m[constituent * points + point] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
    }
  }
  // Tide-gauge coordinates commonly fall on a land cell in a 1/8-degree
  // atlas.  Model-comparison studies use a nearby valid ocean node in this
  // case. Extend at most two native cells (0.25 degrees), always copying one
  // original wet cell and never recursively extending an extension.
  const auto original_mask = result.mask;
  const auto nx = result.grid.nx();
  const auto ny = result.grid.ny();
  for (std::size_t point = 0; point < points; ++point) {
    if (!original_mask[point]) continue;
    const auto x = point % nx;
    const auto y = point / nx;
    auto best = points;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int dy = -2; dy <= 2; ++dy) {
      const auto yy = static_cast<std::ptrdiff_t>(y) + dy;
      if (yy < 0 || yy >= static_cast<std::ptrdiff_t>(ny)) continue;
      for (int dx = -2; dx <= 2; ++dx) {
        auto xx = static_cast<std::ptrdiff_t>(x) + dx;
        if (xx < 0 || xx >= static_cast<std::ptrdiff_t>(nx)) continue;
        const auto candidate = static_cast<std::size_t>(yy) * nx +
                               static_cast<std::size_t>(xx);
        if (original_mask[candidate]) continue;
        const double longitude_scale = std::max(
            0.05, std::abs(std::cos(result.grid.latitudes[y] *
                                    3.14159265358979323846 / 180.0)));
        const double distance =
            std::hypot(static_cast<double>(dy),
                       static_cast<double>(dx) * longitude_scale);
        if (distance < best_distance) {
          best_distance = distance;
          best = candidate;
        }
      }
    }
    if (best == points) continue;
    result.mask[point] = 0;
    for (std::size_t constituent = 0;
         constituent < result.constituents.size(); ++constituent)
      result.coefficients_m[constituent * points + point] =
          result.coefficients_m[constituent * points + best];
  }
  result.reference_level_m = 0.0;
  result.datum_id = "model-mean-sea-level";
  result.datum_name = "Model mean sea level";
  result.Validate();
  return result;
}

environmental_grib::TideHeightHarmonics LoadHamtide11aHeightModel(
    const std::filesystem::path& ocean_tide_directory,
    double imaginary_sign) {
  if (imaginary_sign != -1.0 && imaginary_sign != 1.0)
    throw std::invalid_argument("HAMTIDE imaginary sign must be -1 or +1");
  const std::map<std::string, std::string> expected{
      {"2n2", "2n.hamtide11a.nc"}, {"k1", "k1.hamtide11a.nc"},
      {"k2", "k2.hamtide11a.nc"}, {"m2", "m2.hamtide11a.nc"},
      {"n2", "n2.hamtide11a.nc"}, {"o1", "o1.hamtide11a.nc"},
      {"p1", "p1.hamtide11a.nc"}, {"q1", "q1.hamtide11a.nc"},
      {"s2", "s2.hamtide11a.nc"}};
  for (const auto& [name, filename] : expected) {
    (void)name;
    if (!std::filesystem::is_regular_file(ocean_tide_directory / filename))
      throw std::runtime_error("HAMTIDE directory is missing " + filename);
  }

  NcFile mask_file(ocean_tide_directory / expected.at("m2"));
  const auto source_latitudes = mask_file.Read1d("LAT");
  const auto source_longitudes = mask_file.Read1d("LON");
  if (source_latitudes.size() != 1441 || source_longitudes.size() != 2881 ||
      std::abs(source_latitudes.front() + 90.0) > 1e-10 ||
      std::abs(source_latitudes.back() - 90.0) > 1e-10 ||
      std::abs(source_longitudes.front()) > 1e-10 ||
      std::abs(source_longitudes.back() - 360.0) > 1e-10)
    throw std::runtime_error("unexpected HAMTIDE 1/8-degree global grid");
  const auto m2_real = mask_file.Read2d(
      "RE", source_latitudes.size(), source_longitudes.size());
  const auto m2_imaginary = mask_file.Read2d(
      "IM", source_latitudes.size(), source_longitudes.size());

  environmental_grib::TideHeightHarmonics result;
  result.grid.latitudes = source_latitudes;
  result.grid.spacing_deg = 0.125;
  result.grid.latitude_spacing_deg = 0.125;
  result.grid.longitude_spacing_deg = 0.125;
  result.grid.longitudes.resize(2881);
  for (std::size_t index = 0; index < result.grid.longitudes.size(); ++index)
    result.grid.longitudes[index] = -180.0 + 0.125 * index;
  const auto points = result.grid.size();
  result.mask.assign(points, 1);
  const auto source_x = [](std::size_t output_x) {
    const double longitude = -180.0 + 0.125 * output_x;
    const double source_longitude = longitude < 0.0 ? longitude + 360.0
                                                    : longitude;
    return static_cast<std::size_t>(std::llround(source_longitude / 0.125));
  };
  for (std::size_t y = 0; y < source_latitudes.size(); ++y) {
    for (std::size_t x = 0; x < result.grid.longitudes.size(); ++x) {
      const auto source = y * source_longitudes.size() + source_x(x);
      const auto target = y * result.grid.longitudes.size() + x;
      if (std::isfinite(m2_real[source]) &&
          std::isfinite(m2_imaginary[source]) &&
          m2_real[source] != -999.0 && m2_imaginary[source] != -999.0)
        result.mask[target] = 0;
    }
  }
  for (const auto& [name, filename] : expected) {
    NcFile file(ocean_tide_directory / filename);
    if (!SameAxis(source_latitudes, file.Read1d("LAT")) ||
        !SameAxis(source_longitudes, file.Read1d("LON")))
      throw std::runtime_error("HAMTIDE constituent axes differ: " + name);
    const auto real = file.Read2d("RE", source_latitudes.size(),
                                  source_longitudes.size());
    const auto imaginary = file.Read2d("IM", source_latitudes.size(),
                                       source_longitudes.size());
    result.constituents.push_back(name);
    result.coefficients_m.resize(result.constituents.size() * points);
    const auto constituent = result.constituents.size() - 1;
    for (std::size_t y = 0; y < source_latitudes.size(); ++y) {
      for (std::size_t x = 0; x < result.grid.longitudes.size(); ++x) {
        const auto target = y * result.grid.longitudes.size() + x;
        const auto source = y * source_longitudes.size() + source_x(x);
        if (result.mask[target] || real[source] == -999.0 ||
            imaginary[source] == -999.0) {
          result.coefficients_m[constituent * points + target] = {
              std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN()};
        } else {
          result.coefficients_m[constituent * points + target] = {
              real[source] * 0.01,
              imaginary_sign * imaginary[source] * 0.01};
        }
      }
    }
  }

  // Apply the same corruption guard and bounded comparison extension as the
  // EOT20 member.  Extended cells remain model-only support.
  for (std::size_t point = 0; point < points; ++point) {
    if (result.mask[point]) continue;
    bool plausible = true;
    for (std::size_t constituent = 0;
         constituent < result.constituents.size(); ++constituent) {
      const auto value = result.coefficients_m[constituent * points + point];
      if (!std::isfinite(value.real()) || !std::isfinite(value.imag()) ||
          std::abs(value.real()) > 8.0 || std::abs(value.imag()) > 8.0) {
        plausible = false;
        break;
      }
    }
    if (!plausible) {
      result.mask[point] = 1;
      for (std::size_t constituent = 0;
           constituent < result.constituents.size(); ++constituent)
        result.coefficients_m[constituent * points + point] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
    }
  }
  const auto original_mask = result.mask;
  const auto nx = result.grid.nx();
  const auto ny = result.grid.ny();
  for (std::size_t point = 0; point < points; ++point) {
    if (!original_mask[point]) continue;
    const auto x = point % nx;
    const auto y = point / nx;
    auto best = points;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int dy = -2; dy <= 2; ++dy) {
      const auto yy = static_cast<std::ptrdiff_t>(y) + dy;
      if (yy < 0 || yy >= static_cast<std::ptrdiff_t>(ny)) continue;
      for (int dx = -2; dx <= 2; ++dx) {
        const auto xx = static_cast<std::ptrdiff_t>(x) + dx;
        if (xx < 0 || xx >= static_cast<std::ptrdiff_t>(nx)) continue;
        const auto candidate = static_cast<std::size_t>(yy) * nx +
                               static_cast<std::size_t>(xx);
        if (original_mask[candidate]) continue;
        const double longitude_scale = std::max(
            0.05, std::abs(std::cos(result.grid.latitudes[y] *
                                    3.14159265358979323846 / 180.0)));
        const double distance =
            std::hypot(static_cast<double>(dy),
                       static_cast<double>(dx) * longitude_scale);
        if (distance < best_distance) {
          best_distance = distance;
          best = candidate;
        }
      }
    }
    if (best == points) continue;
    result.mask[point] = 0;
    for (std::size_t constituent = 0;
         constituent < result.constituents.size(); ++constituent)
      result.coefficients_m[constituent * points + point] =
          result.coefficients_m[constituent * points + best];
  }
  result.reference_level_m = 0.0;
  result.datum_id = "model-mean-sea-level";
  result.datum_name = "Model mean sea level";
  result.Validate();
  return result;
}

environmental_grib::TideHeightHarmonics LoadGtsm41HeightModel(
    const std::filesystem::path& ocean_tide_directory,
    double imaginary_sign) {
  if (imaginary_sign != -1.0 && imaginary_sign != 1.0)
    throw std::invalid_argument("GTSM imaginary sign must be -1 or +1");
  const std::vector<std::string> names{
      "k1", "k2", "m2", "m4", "n2", "o1", "p1", "q1", "s2"};
  const auto path_for = [&](const std::string& name) {
    auto upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char value) {
                     return static_cast<char>(std::toupper(value));
                   });
    return ocean_tide_directory / (upper + ".nc");
  };
  for (const auto& name : names)
    if (!std::filesystem::is_regular_file(path_for(name)))
      throw std::runtime_error("GTSM directory is missing " +
                               path_for(name).filename().string());

  NcFile mask_file(path_for("m2"));
  const auto source_latitudes = mask_file.Read1d("lat");
  const auto source_longitudes = mask_file.Read1d("lon");
  if (source_latitudes.size() != 2817 || source_longitudes.size() != 5761 ||
      std::abs(source_latitudes.front() + 86.0) > 1e-10 ||
      std::abs(source_latitudes.back() - 90.0) > 1e-10 ||
      std::abs(source_longitudes.front() + 180.0) > 1e-10 ||
      std::abs(source_longitudes.back() - 180.0) > 1e-10)
    throw std::runtime_error("unexpected GTSM v4.1 1/16-degree grid");
  const auto m2_real = mask_file.Read2d(
      "wl_real", source_latitudes.size(), source_longitudes.size());
  const auto m2_imaginary = mask_file.Read2d(
      "wl_imag", source_latitudes.size(), source_longitudes.size());

  environmental_grib::TideHeightHarmonics result;
  result.grid.spacing_deg = 0.125;
  result.grid.latitude_spacing_deg = 0.125;
  result.grid.longitude_spacing_deg = 0.125;
  result.grid.latitudes.resize(1409);
  result.grid.longitudes.resize(2881);
  for (std::size_t index = 0; index < result.grid.latitudes.size(); ++index)
    result.grid.latitudes[index] = -86.0 + 0.125 * index;
  for (std::size_t index = 0; index < result.grid.longitudes.size(); ++index)
    result.grid.longitudes[index] = -180.0 + 0.125 * index;
  const auto points = result.grid.size();
  result.mask.assign(points, 1);
  for (std::size_t y = 0; y < result.grid.ny(); ++y) {
    for (std::size_t x = 0; x < result.grid.nx(); ++x) {
      const auto source = (y * 2) * source_longitudes.size() + x * 2;
      const auto target = y * result.grid.nx() + x;
      if (std::isfinite(m2_real[source]) &&
          std::isfinite(m2_imaginary[source]) &&
          m2_real[source] != -999.0 && m2_imaginary[source] != -999.0)
        result.mask[target] = 0;
    }
  }
  for (const auto& name : names) {
    NcFile file(path_for(name));
    if (!SameAxis(source_latitudes, file.Read1d("lat")) ||
        !SameAxis(source_longitudes, file.Read1d("lon")))
      throw std::runtime_error("GTSM constituent axes differ: " + name);
    const auto real = file.Read2d("wl_real", source_latitudes.size(),
                                  source_longitudes.size());
    const auto imaginary = file.Read2d("wl_imag", source_latitudes.size(),
                                       source_longitudes.size());
    result.constituents.push_back(name);
    result.coefficients_m.resize(result.constituents.size() * points);
    const auto constituent = result.constituents.size() - 1;
    for (std::size_t y = 0; y < result.grid.ny(); ++y) {
      for (std::size_t x = 0; x < result.grid.nx(); ++x) {
        const auto target = y * result.grid.nx() + x;
        const auto source = (y * 2) * source_longitudes.size() + x * 2;
        if (result.mask[target] || real[source] == -999.0 ||
            imaginary[source] == -999.0) {
          result.coefficients_m[constituent * points + target] = {
              std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN()};
        } else {
          result.coefficients_m[constituent * points + target] = {
              real[source], imaginary_sign * imaginary[source]};
        }
      }
    }
  }
  for (std::size_t point = 0; point < points; ++point) {
    if (result.mask[point]) continue;
    bool plausible = true;
    for (std::size_t constituent = 0;
         constituent < result.constituents.size(); ++constituent) {
      const auto value = result.coefficients_m[constituent * points + point];
      if (!std::isfinite(value.real()) || !std::isfinite(value.imag()) ||
          std::abs(value.real()) > 8.0 || std::abs(value.imag()) > 8.0) {
        plausible = false;
        break;
      }
    }
    if (!plausible) {
      result.mask[point] = 1;
      for (std::size_t constituent = 0;
           constituent < result.constituents.size(); ++constituent)
        result.coefficients_m[constituent * points + point] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
    }
  }
  const auto original_mask = result.mask;
  const auto nx = result.grid.nx();
  const auto ny = result.grid.ny();
  for (std::size_t point = 0; point < points; ++point) {
    if (!original_mask[point]) continue;
    const auto x = point % nx;
    const auto y = point / nx;
    auto best = points;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int dy = -2; dy <= 2; ++dy) {
      const auto yy = static_cast<std::ptrdiff_t>(y) + dy;
      if (yy < 0 || yy >= static_cast<std::ptrdiff_t>(ny)) continue;
      for (int dx = -2; dx <= 2; ++dx) {
        const auto xx = static_cast<std::ptrdiff_t>(x) + dx;
        if (xx < 0 || xx >= static_cast<std::ptrdiff_t>(nx)) continue;
        const auto candidate = static_cast<std::size_t>(yy) * nx +
                               static_cast<std::size_t>(xx);
        if (original_mask[candidate]) continue;
        const double longitude_scale = std::max(
            0.05, std::abs(std::cos(result.grid.latitudes[y] *
                                    3.14159265358979323846 / 180.0)));
        const double distance =
            std::hypot(static_cast<double>(dy),
                       static_cast<double>(dx) * longitude_scale);
        if (distance < best_distance) {
          best_distance = distance;
          best = candidate;
        }
      }
    }
    if (best == points) continue;
    result.mask[point] = 0;
    for (std::size_t constituent = 0;
         constituent < result.constituents.size(); ++constituent)
      result.coefficients_m[constituent * points + point] =
          result.coefficients_m[constituent * points + best];
  }
  result.reference_level_m = 0.0;
  result.datum_id = "model-mean-sea-level";
  result.datum_name = "Model mean sea level";
  result.Validate();
  return result;
}

}  // namespace xtidal::authoring
