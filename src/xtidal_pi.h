#pragma once

#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <wx/wx.h>

#include "ocpn_plugin.h"
#include "native_tide_comparison.h"
#include "native_tide_host.h"
#include "xtidal_prediction.h"

class XTidalDialog;

enum class ForecastLocationSource {
  kChartCursor = 0,
  kWaypoint = 1,
  kManual = 2
};
enum class ForecastPeriod { kNextTides = 0, kSelectedDate = 1 };

struct ForecastWaypoint {
  wxString guid;
  wxString name;
  double latitude{};
  double longitude{};
};

class xtidal_pi final : public opencpn_plugin_121 {
public:
  explicit xtidal_pi(void* manager);
  ~xtidal_pi() override;

  int Init() override;
  bool DeInit() override;
  int GetAPIVersionMajor() override { return 1; }
  int GetAPIVersionMinor() override { return 21; }
  int GetPlugInVersionMajor() override { return 1; }
  int GetPlugInVersionMinor() override { return 0; }
  int GetPlugInVersionPatch() override { return 2; }
  wxBitmap* GetPlugInBitmap() override { return &icon_; }
  wxString GetCommonName() override { return "OfflineTides"; }
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;
  int GetToolbarToolCount() override { return 1; }
  void OnToolbarToolCallback(int id) override;
  void SetCursorLatLon(double latitude, double longitude) override;

  bool LoadPackage(const wxString& path, wxString* error);
  void SetPredictionTime(std::chrono::sys_seconds time);
  void ShiftPredictionTime(std::chrono::hours delta);
  void SetHeightReference(int selection);
  void SetShowNativeCurve(bool show);
  bool SetDisplayTimeZone(const wxString& time_zone_id, wxString* error);
  void SetForecastLocationSource(int selection);
  std::vector<ForecastWaypoint> ListForecastWaypoints() const;
  bool SelectForecastWaypoint(const wxString& guid, wxString* error);
  bool SetManualForecastPosition(const wxString& latitude,
                                 const wxString& longitude, wxString* error);
  void SetForecastPeriod(int selection);
  bool SetForecastDate(xtidal::ForecastDate date, wxString* error);
  bool ExportForecast(const wxString& path, bool json, wxString* error) const;
  std::chrono::sys_seconds PredictionTime() const { return prediction_time_; }
  const std::string& DisplayTimeZone() const { return display_time_zone_; }
  ForecastLocationSource LocationSource() const { return location_source_; }
  ForecastPeriod Period() const { return forecast_period_; }
  const wxString& SelectedWaypointGuid() const {
    return selected_waypoint_guid_;
  }
  xtidal::ForecastDate SelectedForecastDate() const { return forecast_date_; }
  double ManualLatitude() const { return manual_lat_; }
  double ManualLongitude() const { return manual_lon_; }
  wxString LastPackagePath() const;

private:
  void CreateIcon();
  void LoadSettings();
  void SaveSettings();
  void UpdateDialogStatus();
  void ApplyForecastPosition(double latitude, double longitude,
                             const wxString& label, bool force);
  void UpdateHeightCurve(bool force = false);
  void UpdateNativeComparison(const xtidal::HeightCurve& curve);
  void WriteNativeDatumAudit() const;
  xtidal::ForecastWindow CurrentForecastWindow() const;

  wxWindow* parent_{};
  int tool_id_{-1};
  wxBitmap icon_;
  XTidalDialog* dialog_{};
  xtidal::PredictionService prediction_;
  std::chrono::sys_seconds prediction_time_;
  int height_reference_selection_{};
  bool show_native_curve_{true};
  std::string display_time_zone_{"UTC"};
  ForecastLocationSource location_source_{ForecastLocationSource::kChartCursor};
  ForecastPeriod forecast_period_{ForecastPeriod::kNextTides};
  xtidal::ForecastDate forecast_date_{};
  wxString selected_waypoint_guid_;
  wxString forecast_position_label_{"Move over the chart to sample"};
  wxString package_path_;
  double cursor_lat_{std::numeric_limits<double>::quiet_NaN()};
  double cursor_lon_{std::numeric_limits<double>::quiet_NaN()};
  double forecast_lat_{std::numeric_limits<double>::quiet_NaN()};
  double forecast_lon_{std::numeric_limits<double>::quiet_NaN()};
  double manual_lat_{std::numeric_limits<double>::quiet_NaN()};
  double manual_lon_{std::numeric_limits<double>::quiet_NaN()};
  double last_height_lat_{std::numeric_limits<double>::quiet_NaN()};
  double last_height_lon_{std::numeric_limits<double>::quiet_NaN()};
  std::optional<xtidal::HeightCurve> last_curve_;
  std::shared_ptr<HostApi121> native_tide_api_;
  xtidal::NativeTideHost native_tide_host_;
};
