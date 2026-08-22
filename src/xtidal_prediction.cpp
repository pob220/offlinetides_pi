#include "xtidal_prediction.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "environmental_grib/model.h"
#include "time_zone_support.h"

namespace xtidal {

namespace {
std::string Trim(std::string value) {
  const auto non_space = [](unsigned char character) {
    return !std::isspace(character);
  };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), non_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(),
              value.end());
  return value;
}
}  // namespace

DisplayTime ConvertTimeForDisplay(TimePoint time,
                                  const std::string& time_zone_id) {
  try {
    const auto converted = time_zone::ToLocal(time, time_zone_id);
    const auto local_day =
        std::chrono::floor<std::chrono::days>(converted.time);
    const std::chrono::year_month_day date{local_day};
    const std::chrono::hh_mm_ss clock{converted.time - local_day};
    return {static_cast<int>(date.year()),
            static_cast<unsigned>(date.month()),
            static_cast<unsigned>(date.day()),
            static_cast<unsigned>(clock.hours().count()),
            static_cast<unsigned>(clock.minutes().count()),
            static_cast<unsigned>(clock.seconds().count()),
            converted.utc_offset_minutes,
            converted.abbreviation};
  } catch (const std::exception& exception) {
    throw std::invalid_argument("unknown display time zone '" + time_zone_id +
                                "': " + exception.what());
  }
}

ForecastWindow BuildNextTidesWindow(TimePoint selected_time) {
  return {selected_time - std::chrono::hours{24},
          selected_time + std::chrono::hours{24}, selected_time};
}

ForecastWindow BuildLocalDateWindow(ForecastDate date,
                                    const std::string& time_zone_id) {
  try {
    const std::chrono::year_month_day calendar_date{
        std::chrono::year{date.year}, std::chrono::month{date.month},
        std::chrono::day{date.day}};
    if (!calendar_date.ok()) throw std::invalid_argument("invalid date");
    const std::chrono::year_month_day next_date{
        std::chrono::sys_days{calendar_date} + std::chrono::days{1}};
    const auto convert = [&](const std::chrono::year_month_day& value,
                             unsigned hour) {
      const auto converted = time_zone::ToUtc(
          static_cast<int>(value.year()), static_cast<unsigned>(value.month()),
          static_cast<unsigned>(value.day()), hour, 0, 0, time_zone_id);
      if (converted.status == time_zone::LocalStatus::kInvalid ||
          converted.status == time_zone::LocalStatus::kNonexistent)
        throw std::invalid_argument("local calendar instant does not exist");
      return converted.time;
    };
    return {convert(calendar_date, 0), convert(next_date, 0),
            convert(calendar_date, 12)};
  } catch (const std::exception& exception) {
    throw std::invalid_argument(
        "cannot construct forecast date in time zone '" + time_zone_id +
        "': " + exception.what());
  }
}

double ParseCoordinate(const std::string& input, bool latitude) {
  std::string text = Trim(input);
  if (text.empty()) throw std::invalid_argument("coordinate is empty");

  char hemisphere = '\0';
  const auto consume_hemisphere = [&](std::size_t index) {
    const char candidate = static_cast<char>(
        std::toupper(static_cast<unsigned char>(text.at(index))));
    if (candidate != 'N' && candidate != 'S' && candidate != 'E' &&
        candidate != 'W')
      return false;
    if (hemisphere != '\0')
      throw std::invalid_argument("coordinate has more than one hemisphere");
    hemisphere = candidate;
    text.erase(index, 1);
    text = Trim(text);
    return true;
  };
  if (!text.empty()) consume_hemisphere(0);
  if (!text.empty()) consume_hemisphere(text.size() - 1);

  for (const std::string& degree_symbol :
       {std::string{"\xC2\xB0"}, std::string{"deg"}}) {
    std::size_t position = 0;
    while ((position = text.find(degree_symbol, position)) != std::string::npos)
      text.erase(position, degree_symbol.size());
  }
  text = Trim(text);
  if (hemisphere != '\0') {
    const bool correct_axis = latitude ? hemisphere == 'N' || hemisphere == 'S'
                                       : hemisphere == 'E' || hemisphere == 'W';
    if (!correct_axis)
      throw std::invalid_argument(latitude
                                      ? "latitude hemisphere must be N or S"
                                      : "longitude hemisphere must be E or W");
  }

  double value{};
  const char* first = text.data();
  const char* last = text.data() + text.size();
  if (first != last && *first == '+') ++first;
  const auto parsed = std::from_chars(first, last, value);
  if (parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite(value))
    throw std::invalid_argument("coordinate is not a finite decimal number");

  if (hemisphere != '\0') {
    const bool negative = hemisphere == 'S' || hemisphere == 'W';
    if (value < 0.0 && !negative)
      throw std::invalid_argument(
          "coordinate sign conflicts with its hemisphere");
    value = negative ? -std::abs(value) : std::abs(value);
  }
  const double limit = latitude ? 90.0 : 180.0;
  if (value < -limit || value > limit)
    throw std::invalid_argument(latitude
                                    ? "latitude must be between -90 and 90"
                                    : "longitude must be between -180 and 180");
  return value;
}

bool IsChartDatum(const HeightCurve& curve) {
  return curve.datum_id == "chart-datum" ||
         curve.datum_id.rfind("chart-datum-", 0) == 0;
}

std::string HeightReferencePhrase(const HeightCurve& curve) {
  if (IsChartDatum(curve)) return "above Chart Datum";
  if (!curve.datum_name.empty()) return "relative to " + curve.datum_name;
  return "relative to the package datum";
}

std::string FormatHeightForDisplay(const HeightCurve& curve, double height_m) {
  char value[64]{};
  std::snprintf(value, sizeof(value), "%.2f m %s", height_m,
                HeightReferencePhrase(curve).c_str());
  return value;
}

std::optional<HeightSample> InterpolateHeightSample(const HeightCurve& curve,
                                                    TimePoint time) {
  if (curve.samples.size() < 2 || time < curve.samples.front().time ||
      time > curve.samples.back().time)
    return std::nullopt;

  const auto upper =
      std::lower_bound(curve.samples.begin(), curve.samples.end(), time,
                       [](const HeightSample& sample, TimePoint candidate) {
                         return sample.time < candidate;
                       });
  if (upper == curve.samples.end()) return curve.samples.back();
  if (upper->time == time || upper == curve.samples.begin()) return *upper;

  const auto lower = std::prev(upper);
  const auto span = std::chrono::duration<double>(upper->time - lower->time);
  if (span.count() <= 0.0) return std::nullopt;
  const double fraction =
      std::chrono::duration<double>(time - lower->time).count() / span.count();
  return HeightSample{
      time, lower->height_m + fraction * (upper->height_m - lower->height_m)};
}

std::string HeightSupportDescription(const HeightQualitySample& quality) {
  switch (quality.support_class) {
    case 1:
      return "background model only";
    case 2:
      return "observation constrained";
    case 3:
      return "estuary observation constrained";
    case 4:
      return "independent-model fallback";
    default:
      return "unknown support";
  }
}

std::string VerticalDatumSupportDescription(const VerticalDatumSample& datum) {
  switch (datum.support_class) {
    case 1:
      return "authority gridded transform";
    case 2:
      return "station-constrained Chart Datum";
    case 3:
      return "estuary station-constrained Chart Datum";
    case 4:
      return "model-derived datum";
    default:
      return "unknown datum support";
  }
}

std::string VerticalDatumRealizationDescription(
    const VerticalDatumSample& datum) {
  switch (datum.realization_class) {
    case 1:
      return "LAT";
    case 2:
      return "MLLW";
    case 3:
      return "LLWLT";
    case 4:
      return "NLLW";
    case 5:
      return "MLWS";
    case 6:
      return "MSL";
    case 7:
      return "TLT/LLW";
    case 8:
      return "authority-defined";
    default:
      return "unknown";
  }
}

void PredictionService::Load(const std::filesystem::path& path) {
  auto candidate = std::make_unique<environmental_grib::XtdPackageReader>(path);
  const auto& status = candidate->status();
  if (!status.authenticated ||
      (!status.tide_available && !status.height_available)) {
    throw std::runtime_error(
        "XTD package is not authenticated or has no supported tidal component");
  }
  PackageInfo info;
  info.path = status.path;
  info.format_version = status.format_version;
  info.authenticated = status.authenticated;
  info.tide_available = status.tide_available;
  info.expected_current_available = status.climatology_available;
  info.height_available = status.height_available;
  info.height_quality_available = status.height_quality_available;
  info.vertical_datum_available = status.vertical_datum_available;
  info.metadata = status.metadata;
  info.height_datum_id = status.height_datum_id;
  info.height_datum_name = status.height_datum_name;
  info.package_id = status.package_id;
  package_ = std::move(info);
  reader_ = std::move(candidate);
}

void PredictionService::Unload() {
  reader_.reset();
  package_ = {};
}

const PackageInfo& PredictionService::package() const {
  if (!reader_) throw std::logic_error("no XTD package is loaded");
  return package_;
}

CurrentGrid PredictionService::PredictGrid(
    const std::vector<double>& latitudes, const std::vector<double>& longitudes,
    TimePoint time, OfflineCurrentMode mode) {
  if (!reader_) throw std::logic_error("no XTD package is loaded");
  if (!package_.tide_available)
    throw std::logic_error("XTD package has no tidal-current component");
  if (latitudes.empty() || longitudes.empty()) {
    throw std::invalid_argument("prediction grid cannot be empty");
  }
  if (mode == OfflineCurrentMode::kTideAndExpectedSeasonalCirculation &&
      !package_.expected_current_available) {
    throw std::invalid_argument(
        "this XTD package does not contain expected seasonal current data");
  }
  environmental_grib::RegularGrid grid;
  grid.latitudes = latitudes;
  grid.longitudes = longitudes;
  const auto fields = reader_->Predict(grid, {time}, mode);
  if (fields.size() != 1) {
    throw std::runtime_error("XTD prediction returned an unexpected result");
  }
  fields.front().Validate();
  return fields.front();
}

std::optional<CurrentSample> PredictionService::PredictPoint(
    double latitude, double longitude, TimePoint time,
    OfflineCurrentMode mode) {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
      longitude > 180.0) {
    throw std::invalid_argument("cursor coordinate is outside WGS84 bounds");
  }
  auto grid = PredictGrid({latitude}, {longitude}, time, mode);
  if ((!grid.mask.empty() && grid.mask.front()) ||
      !std::isfinite(grid.u_mps.front()) ||
      !std::isfinite(grid.v_mps.front())) {
    return std::nullopt;
  }
  const auto [speed, direction] =
      environmental_grib::ComponentsToSpeedDirection(grid.u_mps.front(),
                                                     grid.v_mps.front());
  return CurrentSample{grid.u_mps.front(), grid.v_mps.front(), speed,
                       direction};
}

std::optional<double> PredictionService::PredictHeight(double latitude,
                                                       double longitude,
                                                       TimePoint time) {
  const auto curve =
      PredictHeightCurve(latitude, longitude, time, std::chrono::hours{0});
  if (curve.samples.empty()) return std::nullopt;
  return curve.samples.front().height_m;
}

std::optional<HeightQualitySample> PredictionService::PredictHeightQuality(
    double latitude, double longitude) {
  if (!reader_) throw std::logic_error("no XTD package is loaded");
  if (!package_.height_quality_available) return std::nullopt;
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
      longitude > 180.0)
    throw std::invalid_argument("height coordinate is outside WGS84 bounds");
  environmental_grib::RegularGrid grid;
  grid.latitudes = {latitude};
  grid.longitudes = {longitude};
  const auto quality = reader_->SampleHeightQuality(grid);
  if ((!quality.mask.empty() && quality.mask.front()) ||
      !std::isfinite(quality.harmonic_sigma_m.front()) ||
      !std::isfinite(quality.datum_sigma_m.front()) ||
      !std::isfinite(quality.nearest_observation_distance_km.front()))
    return std::nullopt;
  return HeightQualitySample{
      quality.harmonic_sigma_m.front(), quality.datum_sigma_m.front(),
      quality.nearest_observation_distance_km.front(),
      quality.support_class.front(), quality.observation_count.front()};
}

std::optional<VerticalDatumSample> PredictionService::PredictVerticalDatum(
    double latitude, double longitude) {
  if (!reader_) throw std::logic_error("no XTD package is loaded");
  if (!package_.vertical_datum_available) return std::nullopt;
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
      longitude > 180.0)
    throw std::invalid_argument("height coordinate is outside WGS84 bounds");
  environmental_grib::RegularGrid grid;
  grid.latitudes = {latitude};
  grid.longitudes = {longitude};
  const auto datum = reader_->SampleVerticalDatum(grid);
  if ((!datum.mask.empty() && datum.mask.front()) ||
      !std::isfinite(datum.offset_m.front()) ||
      !std::isfinite(datum.uncertainty_m.front()) ||
      !std::isfinite(datum.nearest_station_distance_km.front()))
    return std::nullopt;
  return VerticalDatumSample{datum.offset_m.front(),
                             datum.uncertainty_m.front(),
                             datum.nearest_station_distance_km.front(),
                             datum.realization_class.front(),
                             datum.support_class.front(),
                             datum.station_count.front(),
                             datum.target_datum_id,
                             datum.target_datum_name,
                             datum.epoch};
}

HeightCurve PredictionService::PredictHeightCurve(double latitude,
                                                  double longitude,
                                                  TimePoint start,
                                                  std::chrono::seconds duration,
                                                  std::chrono::minutes step,
                                                  HeightReference reference) {
  if (!reader_) throw std::logic_error("no XTD package is loaded");
  if (!package_.height_available)
    throw std::logic_error("XTD package has no water-level height component");
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
      longitude > 180.0)
    throw std::invalid_argument("height coordinate is outside WGS84 bounds");
  if (duration < std::chrono::hours{0} || step <= std::chrono::minutes{0})
    throw std::invalid_argument("height curve duration and step are invalid");
  std::vector<TimePoint> times;
  for (auto offset = std::chrono::minutes{0}; offset <= duration;
       offset += step)
    times.push_back(start + offset);
  environmental_grib::RegularGrid grid;
  grid.latitudes = {latitude};
  grid.longitudes = {longitude};
  const auto predicted = reader_->PredictHeight(grid, times);
  HeightCurve curve{latitude,
                    longitude,
                    package_.height_datum_id,
                    package_.height_datum_name,
                    {}};
  curve.quality = PredictHeightQuality(latitude, longitude);
  if (reference == HeightReference::kChartDatum) {
    curve.vertical_datum = PredictVerticalDatum(latitude, longitude);
    if (!curve.vertical_datum)
      throw std::runtime_error(
          "Chart Datum is unavailable at this position; select model mean "
          "sea level to inspect the native prediction");
    if (curve.datum_id != "model-mean-sea-level")
      throw std::runtime_error(
          "Chart Datum conversion requires model-mean-sea-level harmonics");
    curve.datum_id = curve.vertical_datum->target_datum_id;
    curve.datum_name = curve.vertical_datum->target_datum_name;
    if (curve.quality)
      curve.quality->datum_sigma_m = curve.vertical_datum->uncertainty_m;
  }
  curve.samples.reserve(predicted.size());
  for (const auto& field : predicted) {
    field.Validate();
    if ((!field.mask.empty() && field.mask.front()) ||
        !std::isfinite(field.height_m.front()))
      continue;
    const double offset =
        curve.vertical_datum ? curve.vertical_datum->offset_m : 0.0;
    curve.samples.push_back({field.time, field.height_m.front() + offset});
  }
  return curve;
}

std::vector<HeightEvent> PredictionService::FindHeightEvents(
    const HeightCurve& curve) {
  std::vector<HeightEvent> result;
  if (curve.samples.size() < 3) return result;
  for (std::size_t index = 1; index + 1 < curve.samples.size(); ++index) {
    const auto before = curve.samples[index - 1].height_m;
    const auto value = curve.samples[index].height_m;
    const auto after = curve.samples[index + 1].height_m;
    HeightEventType type;
    if (value > before && value >= after)
      type = HeightEventType::kHighWater;
    else if (value < before && value <= after)
      type = HeightEventType::kLowWater;
    else
      continue;

    auto event_time = curve.samples[index].time;
    double event_height = value;
    const auto before_step =
        curve.samples[index].time - curve.samples[index - 1].time;
    const auto after_step =
        curve.samples[index + 1].time - curve.samples[index].time;
    if (before_step == after_step) {
      const double denominator = before - 2.0 * value + after;
      if (std::abs(denominator) > 1e-12) {
        const double offset =
            std::clamp(0.5 * (before - after) / denominator, -1.0, 1.0);
        const auto seconds =
            std::chrono::duration<double>(before_step).count() * offset;
        event_time += std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::duration<double>(seconds));
        event_height = value - 0.25 * (before - after) * offset;
      }
    }
    result.push_back({type, event_time, event_height});
  }
  return result;
}

HeightValidationMetrics PredictionService::CompareHeightEvents(
    const std::vector<HeightEvent>& predicted,
    const std::vector<HeightEvent>& reference,
    std::chrono::minutes maximum_time_difference) {
  if (maximum_time_difference < std::chrono::minutes{0})
    throw std::invalid_argument("height-event matching window is invalid");
  HeightValidationMetrics metrics;
  metrics.reference_events = reference.size();
  std::vector<bool> used(predicted.size());
  double total_time = 0.0;
  double total_height = 0.0;
  for (const auto& expected : reference) {
    std::size_t best = predicted.size();
    auto best_difference = std::chrono::seconds::max();
    for (std::size_t index = 0; index < predicted.size(); ++index) {
      if (used[index] || predicted[index].type != expected.type) continue;
      const auto difference = predicted[index].time >= expected.time
                                  ? predicted[index].time - expected.time
                                  : expected.time - predicted[index].time;
      if (difference < best_difference) {
        best = index;
        best_difference = difference;
      }
    }
    if (best == predicted.size() || best_difference > maximum_time_difference)
      continue;
    used[best] = true;
    const double signed_time_error =
        std::chrono::duration<double, std::ratio<60>>(predicted[best].time -
                                                      expected.time)
            .count();
    const double signed_height_error =
        predicted[best].height_m - expected.height_m;
    const double time_error = std::abs(signed_time_error);
    const double height_error = std::abs(signed_height_error);
    ++metrics.matched_events;
    metrics.matches.push_back(
        {expected, predicted[best], signed_time_error, signed_height_error});
    total_time += time_error;
    total_height += height_error;
    metrics.maximum_absolute_time_error_minutes =
        std::max(metrics.maximum_absolute_time_error_minutes, time_error);
    metrics.maximum_absolute_height_error_m =
        std::max(metrics.maximum_absolute_height_error_m, height_error);
  }
  if (metrics.matched_events) {
    metrics.mean_absolute_time_error_minutes =
        total_time / metrics.matched_events;
    metrics.mean_absolute_height_error_m =
        total_height / metrics.matched_events;
  }
  return metrics;
}

}  // namespace xtidal
