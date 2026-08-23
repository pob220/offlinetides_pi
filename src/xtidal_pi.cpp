#include "xtidal_pi.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

#include <wx/fileconf.h>
#include <wx/log.h>

#include "environmental_grib/platform.h"
#include "forecast_export.h"
#include "json/json.h"
#include "xtidal_dialog.h"

extern "C" DECL_EXP opencpn_plugin* create_pi(void* manager) {
  return new xtidal_pi(manager);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* plugin) { delete plugin; }

xtidal_pi::xtidal_pi(void* manager)
    : opencpn_plugin_121(manager),
      prediction_time_(std::chrono::floor<std::chrono::seconds>(
          std::chrono::system_clock::now())) {
  CreateIcon();
}

xtidal_pi::~xtidal_pi() = default;

int xtidal_pi::Init() {
  wxLogMessage("offlinetides_pi: initializing XTD-only tidal-height runtime");
  std::shared_ptr<HostApi> host_api = std::move(GetHostApi());
  native_tide_api_ = std::dynamic_pointer_cast<HostApi121>(host_api);
  if (!native_tide_api_)
    wxLogWarning(
        "offlinetides_pi: OpenCPN API 1.21 tide comparison unavailable");
  if (native_tide_host_.Resolve())
    wxLogMessage(
        "offlinetides_pi: typed native tide datum/metric-height bridge "
        "available");
  else
    wxLogMessage(
        "offlinetides_pi: typed native datum bridge unavailable; API 1.21 "
        "timing "
        "fallback retained");
  WriteNativeDatumAudit();
  parent_ = GetOCPNCanvasWindow();
  LoadSettings();
  tool_id_ = InsertPlugInTool("", &icon_, &icon_, wxITEM_NORMAL, "OfflineTides",
                              "", nullptr, -1, 0, this);
  if (!package_path_.empty()) {
    wxString error;
    if (!LoadPackage(package_path_, &error)) {
      wxLogWarning("offlinetides_pi: saved .xtdt package was not loaded: %s",
                   error);
    }
  }
  const char* smoke_test = std::getenv("XTIDAL_UI_SMOKE_TEST");
  if (smoke_test && *smoke_test) {
    const char* smoke_latitude = std::getenv("XTIDAL_UI_SMOKE_LATITUDE");
    const char* smoke_longitude = std::getenv("XTIDAL_UI_SMOKE_LONGITUDE");
    if (smoke_latitude && *smoke_latitude && smoke_longitude &&
        *smoke_longitude) {
      try {
        manual_lat_ = xtidal::ParseCoordinate(smoke_latitude, true);
        manual_lon_ = xtidal::ParseCoordinate(smoke_longitude, false);
        location_source_ = ForecastLocationSource::kManual;
        wxLogMessage(
            "offlinetides_pi: UI smoke test using manual position %.5f, %.5f",
            manual_lat_, manual_lon_);
      } catch (const std::exception& exception) {
        wxLogWarning("offlinetides_pi: invalid UI smoke-test position: %s",
                     exception.what());
      }
    }
  }
  SetForecastLocationSource(static_cast<int>(location_source_));
  if (smoke_test && *smoke_test) {
    wxTheApp->CallAfter([this] {
      if (ShuttingDown()) return;
      wxLogMessage(
          "offlinetides_pi: opening forecast dialog for UI smoke test");
      OnToolbarToolCallback(tool_id_);
    });
  }
  wxLogMessage("offlinetides_pi: initialization complete; package_loaded=%d",
               prediction_.loaded());
  return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | WANTS_CURSOR_LATLON;
}

bool xtidal_pi::DeInit() {
  SaveSettings();
  if (tool_id_ >= 0) RemovePlugInTool(tool_id_);
  tool_id_ = -1;
  if (dialog_) {
    dialog_->Destroy();
    dialog_ = nullptr;
  }
  prediction_.Unload();
  native_tide_api_.reset();
  return true;
}

wxString xtidal_pi::GetShortDescription() {
  return "Offline tidal-height forecasts from authenticated .xtdt packages";
}

wxString xtidal_pi::GetLongDescription() {
  return "Predicts offline water-level height at the chart cursor, an "
         "OpenCPN waypoint or mark, or manual WGS84 coordinates. Forecasts "
         "can show the next tides or a selected local calendar date. Every "
         "height is labelled with its vertical datum; missing data is "
         "reported as unknown.";
}

void xtidal_pi::OnToolbarToolCallback(int) {
  if (!dialog_) {
    dialog_ = new XTidalDialog(parent_, *this);
    dialog_->SetDisplayTimeZone(wxString::FromUTF8(display_time_zone_));
    dialog_->SetPredictionTime(prediction_time_);
    dialog_->SetDisplayOptions(height_reference_selection_);
    dialog_->SetNativeCurveVisible(show_native_curve_);
    wxLogMessage("offlinetides_pi: initial height reference=%s",
                 height_reference_selection_ == 0 ? "chart-datum"
                                                  : "model-mean-sea-level");
    dialog_->SetForecastOptions(static_cast<int>(location_source_),
                                static_cast<int>(forecast_period_),
                                forecast_date_, manual_lat_, manual_lon_);
    dialog_->RefreshWaypoints();
    UpdateDialogStatus();
    dialog_->SetForecastPosition(forecast_position_label_);
    UpdateHeightCurve(true);
  }
  dialog_->Show();
  dialog_->Raise();
}

bool xtidal_pi::LoadPackage(const wxString& path, wxString* error) {
  try {
    if (std::filesystem::path(path.ToStdString()).extension() != ".xtdt")
      throw std::runtime_error(
          "OfflineTides accepts only operational .xtdt packages");
    prediction_.Load(environmental_grib::PathFromUtf8(path.ToStdString()));
    if (!prediction_.package().height_available)
      throw std::runtime_error(
          "OfflineTides requires a package with water-level heights");
    package_path_ = path;
    last_height_lat_ = std::numeric_limits<double>::quiet_NaN();
    last_height_lon_ = std::numeric_limits<double>::quiet_NaN();
    last_curve_.reset();
    UpdateDialogStatus();
    UpdateHeightCurve(true);
    SaveSettings();
    wxLogMessage("offlinetides_pi: authenticated XTD v%u height package loaded",
                 prediction_.package().format_version);
    return true;
  } catch (const std::exception& exception) {
    prediction_.Unload();
    last_curve_.reset();
    UpdateDialogStatus();
    if (error) *error = wxString::FromUTF8(exception.what());
    return false;
  }
}

void xtidal_pi::SetPredictionTime(std::chrono::sys_seconds time) {
  prediction_time_ = time;
  if (dialog_) {
    dialog_->SetPredictionTime(prediction_time_);
  }
  UpdateHeightCurve(true);
}

void xtidal_pi::ShiftPredictionTime(std::chrono::hours delta) {
  SetPredictionTime(prediction_time_ + delta);
}

void xtidal_pi::SetHeightReference(int selection) {
  height_reference_selection_ = selection == 1 ? 1 : 0;
  UpdateHeightCurve(true);
  SaveSettings();
}

void xtidal_pi::SetShowNativeCurve(bool show) {
  show_native_curve_ = show;
  if (dialog_) dialog_->SetNativeCurveVisible(show_native_curve_);
  SaveSettings();
}

bool xtidal_pi::SetDisplayTimeZone(const wxString& time_zone_id,
                                   wxString* error) {
  try {
    const auto candidate = time_zone_id.ToStdString();
    (void)xtidal::ConvertTimeForDisplay(prediction_time_, candidate);
    display_time_zone_ = candidate;
    if (dialog_) {
      dialog_->SetDisplayTimeZone(time_zone_id);
      dialog_->SetPredictionTime(prediction_time_);
    }
    UpdateHeightCurve(true);
    SaveSettings();
    return true;
  } catch (const std::exception& exception) {
    if (error) *error = wxString::FromUTF8(exception.what());
    return false;
  }
}

wxString xtidal_pi::LastPackagePath() const { return package_path_; }

void xtidal_pi::SetCursorLatLon(double latitude, double longitude) {
  cursor_lat_ = latitude;
  cursor_lon_ = longitude;
  if (location_source_ != ForecastLocationSource::kChartCursor) return;
  ApplyForecastPosition(latitude, longitude, "Chart cursor", false);
}

void xtidal_pi::SetForecastLocationSource(int selection) {
  location_source_ = selection == 1   ? ForecastLocationSource::kWaypoint
                     : selection == 2 ? ForecastLocationSource::kManual
                                      : ForecastLocationSource::kChartCursor;
  if (dialog_) dialog_->SetLocationSource(static_cast<int>(location_source_));
  if (location_source_ == ForecastLocationSource::kChartCursor) {
    if (std::isfinite(cursor_lat_) && std::isfinite(cursor_lon_))
      ApplyForecastPosition(cursor_lat_, cursor_lon_, "Chart cursor", true);
    else {
      forecast_lat_ = forecast_lon_ = std::numeric_limits<double>::quiet_NaN();
      forecast_position_label_ = "Move over the chart to sample";
      if (dialog_) {
        dialog_->SetForecastPosition(forecast_position_label_);
        dialog_->ClearHeightCurve("Move over the chart to choose a position");
      }
    }
  } else if (location_source_ == ForecastLocationSource::kWaypoint) {
    wxString error;
    if (!SelectForecastWaypoint(selected_waypoint_guid_, &error)) {
      forecast_lat_ = forecast_lon_ = std::numeric_limits<double>::quiet_NaN();
      forecast_position_label_ = "Select an OpenCPN waypoint or mark";
      if (dialog_) {
        dialog_->SetForecastPosition(forecast_position_label_);
        dialog_->ClearHeightCurve(
            error.empty() ? wxString::FromUTF8(
                                "Select an OpenCPN waypoint or mark")
                          : error);
      }
    }
  } else if (std::isfinite(manual_lat_) && std::isfinite(manual_lon_)) {
    ApplyForecastPosition(manual_lat_, manual_lon_, "Manual coordinates", true);
  } else {
    forecast_lat_ = forecast_lon_ = std::numeric_limits<double>::quiet_NaN();
    if (dialog_) {
      dialog_->SetForecastPosition("Enter latitude and longitude");
      dialog_->ClearHeightCurve("Enter valid manual coordinates");
    }
  }
  SaveSettings();
}

std::vector<ForecastWaypoint> xtidal_pi::ListForecastWaypoints() const {
  std::vector<ForecastWaypoint> result;
  for (const auto& guid : GetWaypointGUIDArray()) {
    auto waypoint = GetWaypoint_Plugin(guid);
    if (!waypoint || !std::isfinite(waypoint->m_lat) ||
        !std::isfinite(waypoint->m_lon) || waypoint->m_lat < -90.0 ||
        waypoint->m_lat > 90.0 || waypoint->m_lon < -180.0 ||
        waypoint->m_lon > 180.0)
      continue;
    result.push_back(
        {guid, waypoint->m_MarkName, waypoint->m_lat, waypoint->m_lon});
  }
  std::sort(result.begin(), result.end(),
            [](const auto& first, const auto& second) {
              const auto first_name = first.name.Lower();
              const auto second_name = second.name.Lower();
              if (first_name == second_name) return first.guid < second.guid;
              return first_name < second_name;
            });
  return result;
}

bool xtidal_pi::SelectForecastWaypoint(const wxString& guid, wxString* error) {
  if (guid.empty()) {
    if (error) *error = "Select an OpenCPN waypoint or mark";
    return false;
  }
  auto waypoint = GetWaypoint_Plugin(guid);
  if (!waypoint) {
    if (error) *error = "The selected waypoint or mark is no longer available";
    return false;
  }
  if (!std::isfinite(waypoint->m_lat) || !std::isfinite(waypoint->m_lon) ||
      waypoint->m_lat < -90.0 || waypoint->m_lat > 90.0 ||
      waypoint->m_lon < -180.0 || waypoint->m_lon > 180.0) {
    if (error) *error = "The selected waypoint has invalid WGS84 coordinates";
    return false;
  }
  selected_waypoint_guid_ = guid;
  wxString label = waypoint->m_MarkName.empty()
                       ? wxString::FromUTF8("Unnamed waypoint")
                       : waypoint->m_MarkName;
  ApplyForecastPosition(waypoint->m_lat, waypoint->m_lon, label, true);
  SaveSettings();
  return true;
}

bool xtidal_pi::SetManualForecastPosition(const wxString& latitude,
                                          const wxString& longitude,
                                          wxString* error) {
  try {
    manual_lat_ = xtidal::ParseCoordinate(latitude.ToStdString(), true);
    manual_lon_ = xtidal::ParseCoordinate(longitude.ToStdString(), false);
    ApplyForecastPosition(manual_lat_, manual_lon_, "Manual coordinates", true);
    SaveSettings();
    return true;
  } catch (const std::exception& exception) {
    if (error) *error = wxString::FromUTF8(exception.what());
    return false;
  }
}

void xtidal_pi::SetForecastPeriod(int selection) {
  forecast_period_ = selection == 1 ? ForecastPeriod::kSelectedDate
                                    : ForecastPeriod::kNextTides;
  if (dialog_) dialog_->SetForecastPeriod(static_cast<int>(forecast_period_));
  UpdateHeightCurve(true);
  SaveSettings();
}

bool xtidal_pi::SetForecastDate(xtidal::ForecastDate date, wxString* error) {
  try {
    (void)xtidal::BuildLocalDateWindow(date, display_time_zone_);
    forecast_date_ = date;
    UpdateHeightCurve(true);
    SaveSettings();
    return true;
  } catch (const std::exception& exception) {
    if (error) *error = wxString::FromUTF8(exception.what());
    return false;
  }
}

bool xtidal_pi::ExportForecast(const wxString& path, bool json,
                               wxString* error) const {
  try {
    if (!last_curve_ || !prediction_.loaded())
      throw std::runtime_error(
          "No water-level forecast is available to export");
    const auto document =
        xtidal::BuildForecastExport(*last_curve_, prediction_.package(),
                                    std::chrono::floor<std::chrono::seconds>(
                                        std::chrono::system_clock::now()));
    std::ofstream output(environmental_grib::PathFromUtf8(path.ToStdString()),
                         std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not create forecast export");
    output << (json ? xtidal::SerializeForecastJson(document)
                    : xtidal::SerializeForecastCsv(document));
    if (!output) throw std::runtime_error("Could not write forecast export");
    return true;
  } catch (const std::exception& exception) {
    if (error) *error = wxString::FromUTF8(exception.what());
    return false;
  }
}

void xtidal_pi::ApplyForecastPosition(double latitude, double longitude,
                                      const wxString& label, bool force) {
  forecast_lat_ = latitude;
  forecast_lon_ = longitude;
  forecast_position_label_ =
      wxString::Format("%s — %.5f°, %.5f°", label, latitude, longitude);
  if (dialog_) dialog_->SetForecastPosition(forecast_position_label_);
  UpdateHeightCurve(force);
}

xtidal::ForecastWindow xtidal_pi::CurrentForecastWindow() const {
  return forecast_period_ == ForecastPeriod::kSelectedDate
             ? xtidal::BuildLocalDateWindow(forecast_date_, display_time_zone_)
             : xtidal::BuildNextTidesWindow(prediction_time_);
}

void xtidal_pi::UpdateHeightCurve(bool force) {
  if (!dialog_ || !prediction_.loaded()) return;
  const double latitude = forecast_lat_;
  const double longitude = forecast_lon_;
  if (!std::isfinite(latitude) || !std::isfinite(longitude)) {
    last_curve_.reset();
    dialog_->ClearHeightCurve("Choose a forecast position");
    dialog_->ClearNativeComparison("Choose a forecast position");
    return;
  }
  if (!prediction_.package().height_available) {
    last_curve_.reset();
    dialog_->ClearHeightCurve("This .xtdt package has no water-level heights");
    return;
  }
  if (!force && std::isfinite(last_height_lat_) &&
      std::isfinite(last_height_lon_) &&
      std::hypot(latitude - last_height_lat_, longitude - last_height_lon_) <
          0.01)
    return;
  last_height_lat_ = latitude;
  last_height_lon_ = longitude;
  try {
    const auto window = CurrentForecastWindow();
    const auto curve = prediction_.PredictHeightCurve(
        latitude, longitude, window.start,
        std::chrono::duration_cast<std::chrono::seconds>(window.end -
                                                         window.start),
        std::chrono::minutes{10},
        height_reference_selection_ == 0
            ? xtidal::HeightReference::kChartDatum
            : xtidal::HeightReference::kModelMeanSeaLevel);
    last_curve_ = curve;
    dialog_->SetHeightCurve(curve,
                            xtidal::PredictionService::FindHeightEvents(curve),
                            window.selected_time);
    UpdateNativeComparison(curve);
  } catch (const std::exception& exception) {
    last_curve_.reset();
    dialog_->ClearHeightCurve(wxString::FromUTF8(exception.what()));
    dialog_->ClearNativeComparison("Native comparison unavailable");
  }
}

void xtidal_pi::UpdateNativeComparison(const xtidal::HeightCurve& curve) {
  if (!dialog_) return;
  if (!native_tide_host_.available() && !native_tide_api_) {
    dialog_->ClearNativeComparison(
        "Native comparison requires OpenCPN host API 1.21");
    return;
  }
  xtidal::NativeTideStation station;
  bool typed_host = false;
  if (native_tide_host_.available()) {
    const auto nearby =
        native_tide_host_.Nearest(curve.latitude, curve.longitude, 100.0, 1);
    if (nearby.empty()) {
      dialog_->ClearNativeComparison(
          "No native OpenCPN tide station is available within 100 NM");
      return;
    }
    const auto& host_station = nearby.front();
    typed_host = true;
    station.index = host_station.index;
    station.name = host_station.name;
    station.latitude = host_station.lat;
    station.longitude = host_station.lon;
    station.stable_id = host_station.stable_id;
    station.reference_name = host_station.reference_name;
    station.source_dataset_id = host_station.source_dataset_id;
    station.source_dataset_version = host_station.source_dataset_version;
    station.source_description = host_station.source_description;
    station.source_name = host_station.source_dataset_name;
    if (station.source_name.empty()) station.source_name = "OpenCPN native";
    station.datum_name = host_station.datum_name;
    station.datum_equivalence_key = host_station.datum_equivalence_key;
    station.datum_id = station.datum_equivalence_key.empty()
                           ? "unknown"
                           : "tcd:" + station.datum_equivalence_key;
    station.datum_status = host_station.datum_status;
    station.datum_approximate = host_station.datum_approximate;
    station.subordinate = host_station.subordinate;
    station.height_in_metres_available =
        host_station.height_in_metres_available;
    if (std::isfinite(host_station.datum_offset_m))
      station.datum_offset_m = host_station.datum_offset_m;
  }
  if (!typed_host) {
    PlugIn_TideStation host_station{};
    if (!native_tide_api_ ||
        !native_tide_api_->GetNearestTideStation(
            curve.latitude, curve.longitude, &host_station)) {
      dialog_->ClearNativeComparison(
          "No native OpenCPN tide station is available within 100 NM");
      return;
    }
    station.index = host_station.index;
    station.name = host_station.name;
    station.latitude = host_station.lat;
    station.longitude = host_station.lon;
    station.source_name =
        "OpenCPN configured tide database (dataset identity is not exposed "
        "by API 1.21)";
    station.datum_id = "unknown";
    station.datum_name = "Not exposed by OpenCPN API 1.21";
  }
  if (!xtidal::IsNativeStationWithinDistance(curve, station, 100.0)) {
    dialog_->ClearNativeComparison(
        "No native OpenCPN tide station is available within 100 NM");
    return;
  }
  xtidal::HeightCurve native_curve;
  native_curve.latitude = station.latitude;
  native_curve.longitude = station.longitude;
  native_curve.datum_id = station.datum_id;
  native_curve.datum_name = station.datum_name;
  native_curve.samples.reserve(curve.samples.size());
  for (const auto& sample : curve.samples) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        sample.time.time_since_epoch());
    std::optional<double> native_height;
    if (typed_host)
      native_height = native_tide_host_.HeightMetres(
          station.index, static_cast<time_t>(seconds.count()));
    else {
      float legacy_height{};
      if (native_tide_api_->GetTideHeight(station.index,
                                          static_cast<time_t>(seconds.count()),
                                          &legacy_height) &&
          std::isfinite(legacy_height))
        native_height = legacy_height;
    }
    if (native_height)
      native_curve.samples.push_back({sample.time, *native_height});
  }
  if (native_curve.samples.size() < 3) {
    dialog_->ClearNativeComparison(
        "The nearest native station returned insufficient forecast data");
    return;
  }
  const auto comparison = xtidal::CompareNativeTideStation(
      curve, std::move(station), std::move(native_curve));
  wxLogMessage(
      "offlinetides_pi: native station '%s' distance=%.1f_km "
      "matched_events=%zu "
      "comparison_mode=%s height_difference_suppressed=%d",
      wxString::FromUTF8(comparison.station.name), comparison.distance_km,
      comparison.matched_events,
      wxString::FromUTF8(
          xtidal::NativeCurveModeDescription(comparison.curve_mode)),
      !comparison.height_difference_comparable);
  dialog_->SetNativeComparison(comparison);
}

void xtidal_pi::WriteNativeDatumAudit() const {
  const char* output_path = std::getenv("XTIDAL_NATIVE_DATUM_AUDIT");
  if (!output_path || !*output_path) return;
  if (!native_tide_host_.available()) {
    wxLogWarning(
        "offlinetides_pi: native datum audit requested but typed host "
        "unavailable");
    return;
  }
  const auto stations = native_tide_host_.Enumerate();
  Json::Value root(Json::objectValue);
  root["schema_version"] = 1;
  root["station_count"] = Json::UInt64(stations.size());
  Json::Value summary(Json::objectValue);
  Json::Value datasets(Json::objectValue);
  Json::Value equivalence(Json::objectValue);
  Json::Value records(Json::arrayValue);
  std::uint64_t declared = 0, inherited = 0, unknown = 0, approximate = 0,
                metric = 0, z0 = 0;
  for (const auto& station : stations) {
    if (station.datum_status == 1)
      ++declared;
    else if (station.datum_status == 2)
      ++inherited;
    else
      ++unknown;
    if (station.datum_approximate) ++approximate;
    if (station.height_in_metres_available) ++metric;
    if (std::isfinite(station.datum_offset_m)) ++z0;
    datasets[station.source_dataset_name] =
        datasets.get(station.source_dataset_name, 0).asUInt64() + 1;
    const std::string key = station.datum_equivalence_key[0]
                                ? station.datum_equivalence_key
                                : "UNKNOWN";
    equivalence[key] = equivalence.get(key, 0).asUInt64() + 1;
    Json::Value record(Json::objectValue);
    record["index"] = station.index;
    record["stable_id"] = station.stable_id;
    record["name"] = station.name;
    record["latitude"] = station.lat;
    record["longitude"] = station.lon;
    record["subordinate"] = station.subordinate != 0;
    record["reference_name"] = station.reference_name;
    record["dataset_id"] = station.source_dataset_id;
    record["dataset_name"] = station.source_dataset_name;
    record["dataset_version"] = station.source_dataset_version;
    record["source"] = station.source_description;
    record["datum_name"] = station.datum_name;
    record["datum_equivalence_key"] = key;
    record["datum_status"] = station.datum_status;
    record["datum_approximate"] = station.datum_approximate != 0;
    record["level_units"] = station.level_units;
    if (std::isfinite(station.datum_offset_m))
      record["datum_offset_m"] = station.datum_offset_m;
    else
      record["datum_offset_m"] = Json::Value(Json::nullValue);
    records.append(std::move(record));
  }
  summary["declared"] = Json::UInt64(declared);
  summary["inherited"] = Json::UInt64(inherited);
  summary["unknown"] = Json::UInt64(unknown);
  summary["approximate"] = Json::UInt64(approximate);
  summary["metric_height_available"] = Json::UInt64(metric);
  summary["z0_available"] = Json::UInt64(z0);
  root["summary"] = std::move(summary);
  root["datasets"] = std::move(datasets);
  root["datum_equivalence"] = std::move(equivalence);
  root["stations"] = std::move(records);
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    wxLogWarning("offlinetides_pi: cannot write native datum audit to %s",
                 output_path);
    return;
  }
  output << Json::writeString(writer, root) << '\n';
  wxLogMessage(
      "offlinetides_pi: wrote native datum audit with %zu stations to %s",
      stations.size(), output_path);
}

void xtidal_pi::CreateIcon() {
  icon_ = wxBitmap(32, 32);
  wxMemoryDC dc(icon_);
  dc.SetBackground(wxBrush(wxColour(26, 87, 130)));
  dc.Clear();
  dc.SetPen(wxPen(*wxWHITE, 2));
  dc.DrawArc(5, 22, 27, 22, 16, 16);
  dc.DrawLine(8, 23, 25, 9);
  dc.DrawLine(25, 9, 20, 10);
  dc.DrawLine(25, 9, 24, 14);
  dc.SelectObject(wxNullBitmap);
}

void xtidal_pi::LoadSettings() {
  auto* config = GetOCPNConfigObject();
  if (!config) return;
  const auto old = config->GetPath();
  config->SetPath("/PlugIns/offlinetides_pi");
  if (!config->HasEntry("settings_schema")) {
    // One-time, read-only migration from the development identity. The old
    // group is retained so an installed X-Tidal build remains unaffected.
    config->SetPath("/PlugIns/xtidal_pi");
  }
  config->Read("package_path", &package_path_);
  long height_reference = 0;
  config->Read("height_reference", &height_reference, 0L);
  height_reference_selection_ = height_reference == 1 ? 1 : 0;
  // V1 makes an eligible comparison visible by default.  Use a versioned key
  // so development profiles which previously persisted the old opt-in false
  // value are migrated to the safer, discoverable v1 behaviour once.
  config->Read("show_native_curve_v1", &show_native_curve_, true);
  wxString display_time_zone;
  config->Read("display_time_zone", &display_time_zone, "UTC");
  try {
    const auto candidate = display_time_zone.ToStdString();
    (void)xtidal::ConvertTimeForDisplay(prediction_time_, candidate);
    display_time_zone_ = candidate;
  } catch (const std::exception&) {
    display_time_zone_ = "UTC";
    wxLogWarning("offlinetides_pi: invalid saved display time zone; using UTC");
  }
  long location_source = 0;
  long forecast_period = 0;
  config->Read("location_source", &location_source, 0L);
  config->Read("forecast_period", &forecast_period, 0L);
  location_source_ = location_source == 1 ? ForecastLocationSource::kWaypoint
                     : location_source == 2
                         ? ForecastLocationSource::kManual
                         : ForecastLocationSource::kChartCursor;
  forecast_period_ = forecast_period == 1 ? ForecastPeriod::kSelectedDate
                                          : ForecastPeriod::kNextTides;
  config->Read("selected_waypoint_guid", &selected_waypoint_guid_);
  config->Read("manual_latitude", &manual_lat_,
               std::numeric_limits<double>::quiet_NaN());
  config->Read("manual_longitude", &manual_lon_,
               std::numeric_limits<double>::quiet_NaN());
  long year = 0, month = 0, day = 0;
  config->Read("forecast_year", &year, 0L);
  config->Read("forecast_month", &month, 0L);
  config->Read("forecast_day", &day, 0L);
  forecast_date_ = {static_cast<int>(year), static_cast<unsigned>(month),
                    static_cast<unsigned>(day)};
  try {
    (void)xtidal::BuildLocalDateWindow(forecast_date_, display_time_zone_);
  } catch (const std::exception&) {
    const auto today =
        xtidal::ConvertTimeForDisplay(prediction_time_, display_time_zone_);
    forecast_date_ = {today.year, today.month, today.day};
  }
  config->SetPath(old);
}

void xtidal_pi::SaveSettings() {
  auto* config = GetOCPNConfigObject();
  if (!config) return;
  const auto old = config->GetPath();
  config->SetPath("/PlugIns/offlinetides_pi");
  config->Write("settings_schema", 1L);
  config->Write("package_path", package_path_);
  config->Write("height_reference",
                static_cast<long>(height_reference_selection_));
  config->Write("show_native_curve_v1", show_native_curve_);
  config->Write("display_time_zone", wxString::FromUTF8(display_time_zone_));
  config->Write("location_source", static_cast<long>(location_source_));
  config->Write("forecast_period", static_cast<long>(forecast_period_));
  config->Write("selected_waypoint_guid", selected_waypoint_guid_);
  if (std::isfinite(manual_lat_)) config->Write("manual_latitude", manual_lat_);
  if (std::isfinite(manual_lon_))
    config->Write("manual_longitude", manual_lon_);
  config->Write("forecast_year", static_cast<long>(forecast_date_.year));
  config->Write("forecast_month", static_cast<long>(forecast_date_.month));
  config->Write("forecast_day", static_cast<long>(forecast_date_.day));
  config->SetPath(old);
  config->Flush();
}

void xtidal_pi::UpdateDialogStatus() {
  if (!dialog_) return;
  if (!prediction_.loaded()) {
    dialog_->SetPackageStatus("No package loaded", false, false);
    dialog_->ClearHeightCurve("No .xtdt package loaded");
    return;
  }
  const auto& info = prediction_.package();
  const wxString package_id = wxString::FromUTF8(info.package_id);
  dialog_->SetPackageStatus(
      wxString::Format(
          "Authenticated XTD v%u — %s%s%s%s", info.format_version, package_id,
          info.height_available ? " — water-level heights available"
                                : " — no water-level heights",
          info.height_available ? info.height_quality_available
                                      ? " — local quality available"
                                      : " — quality unavailable"
                                : "",
          info.vertical_datum_available ? " — Chart Datum transform available"
                                        : " — no Chart Datum transform"),
      true, info.vertical_datum_available);
}
