#pragma once

#include <wx/datectrl.h>
#include <wx/scrolwin.h>
#include <wx/wx.h>

#include <optional>
#include <vector>

#include "xtidal_prediction.h"
#include "native_tide_comparison.h"

class xtidal_pi;
class wxDateEvent;

class XTidalCurvePanel final : public wxPanel {
public:
  explicit XTidalCurvePanel(wxWindow* parent);
  void SetCurve(const xtidal::HeightCurve& curve,
                std::chrono::sys_seconds selected_time,
                const std::string& display_time_zone);
  void SetNativeComparison(
      const xtidal::NativeTideComparison& comparison);
  void ClearNativeComparison();
  void SetNativeCurveVisible(bool visible);
  void ClearCurve();

private:
  void OnPaint(wxPaintEvent&);
  void OnMouseMove(wxMouseEvent& event);
  void OnLeftDown(wxMouseEvent& event);
  void OnMouseLeave(wxMouseEvent& event);
  void UpdateInspection(const wxPoint& position);
  xtidal::HeightCurve curve_;
  std::optional<xtidal::NativeTideComparison> native_comparison_;
  bool native_curve_visible_{};
  std::chrono::sys_seconds selected_time_{};
  std::string display_time_zone_{"UTC"};
  std::optional<xtidal::HeightSample> inspected_sample_;
  bool inspection_locked_{};
};

class XTidalDialog final : public wxDialog {
public:
  XTidalDialog(wxWindow* parent, xtidal_pi& plugin);

  void SetPackageStatus(const wxString& text, bool package_loaded,
                        bool has_vertical_datum);
  void SetPredictionTime(std::chrono::sys_seconds utc);
  void SetDisplayTimeZone(const wxString& time_zone_id);
  void SetDisplayOptions(int height_reference);
  void SetForecastOptions(int location_source, int forecast_period,
                          xtidal::ForecastDate date, double manual_latitude,
                          double manual_longitude);
  void SetLocationSource(int selection);
  void SetForecastPeriod(int selection);
  void SetForecastPosition(const wxString& text);
  void RefreshWaypoints();
  void SetHeightCurve(const xtidal::HeightCurve& curve,
                      const std::vector<xtidal::HeightEvent>& events,
                      std::chrono::sys_seconds selected_time);
  void ClearHeightCurve(const wxString& message);
  void SetNativeComparison(const xtidal::NativeTideComparison& comparison);
  void ClearNativeComparison(const wxString& message);
  void SetNativeCurveVisible(bool visible);

private:
  void OnOpen(wxCommandEvent&);
  void OnNow(wxCommandEvent&);
  void OnEarlier(wxCommandEvent&);
  void OnLater(wxCommandEvent&);
  void OnTimeZone(wxCommandEvent&);
  void OnHeightReference(wxCommandEvent&);
  void OnLocationSource(wxCommandEvent&);
  void OnWaypoint(wxCommandEvent&);
  void OnRefreshWaypoints(wxCommandEvent&);
  void OnApplyManual(wxCommandEvent&);
  void OnForecastPeriod(wxCommandEvent&);
  void OnForecastDate(wxDateEvent&);
  void OnExportJson(wxCommandEvent&);
  void OnExportCsv(wxCommandEvent&);
  void OnShowNativeCurve(wxCommandEvent&);
  void OnClose(wxCloseEvent& event);
  void UpdateLocationControls();
  void UpdatePeriodControls();
  void UpdateScrollableLayout();

  xtidal_pi& plugin_;
  wxStaticText* package_status_{};
  wxStaticText* time_{};
  wxComboBox* time_zone_{};
  wxChoice* location_source_{};
  wxChoice* waypoint_{};
  wxTextCtrl* manual_latitude_{};
  wxTextCtrl* manual_longitude_{};
  wxButton* apply_manual_{};
  wxButton* earlier_{};
  wxButton* now_{};
  wxButton* later_{};
  wxChoice* forecast_period_{};
  wxDatePickerCtrl* forecast_date_{};
  wxChoice* height_reference_{};
  wxStaticText* forecast_position_{};
  wxStaticText* height_status_{};
  wxTextCtrl* height_events_{};
  wxStaticText* native_comparison_{};
  wxCheckBox* show_native_curve_{};
  wxButton* export_json_{};
  wxButton* export_csv_{};
  XTidalCurvePanel* height_curve_{};
  wxScrolledWindow* content_{};
  wxBoxSizer* root_sizer_{};
  std::vector<wxString> waypoint_guids_;
};
