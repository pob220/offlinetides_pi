#include "xtidal_dialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <wx/dcbuffer.h>
#include <wx/dateevt.h>
#include <wx/filedlg.h>

#include "xtidal_pi.h"

namespace {
constexpr int kCurveLeft = 68;
constexpr int kCurveRight = 18;
constexpr int kCurveTop = 78;
constexpr int kCurveBottom = 30;

enum : int {
  kOpen = wxID_HIGHEST + 100,
  kNow,
  kEarlier,
  kLater,
  kTimeZone,
  kHeightReference,
  kLocationSource,
  kWaypoint,
  kRefreshWaypoints,
  kApplyManual,
  kForecastPeriod,
  kForecastDate,
  kExportJson,
  kExportCsv,
  kShowNativeCurve,
};

wxString FormatDisplayTime(xtidal::TimePoint time, const std::string& time_zone,
                           bool include_seconds, bool iso_date) {
  const auto display = xtidal::ConvertTimeForDisplay(time, time_zone);
  const auto abbreviation = wxString::FromUTF8(display.abbreviation);
  if (iso_date) {
    if (include_seconds)
      return wxString::Format("%04d-%02u-%02u %02u:%02u:%02u %s", display.year,
                              display.month, display.day, display.hour,
                              display.minute, display.second, abbreviation);
    return wxString::Format("%04d-%02u-%02u %02u:%02u %s", display.year,
                            display.month, display.day, display.hour,
                            display.minute, abbreviation);
  }
  static constexpr std::array<const char*, 12> kMonths{
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return wxString::Format("%02u %s %02u:%02u %s", display.day,
                          kMonths.at(display.month - 1), display.hour,
                          display.minute, abbreviation);
}
}  // namespace

XTidalCurvePanel::XTidalCurvePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 300),
              wxBORDER_SIMPLE) {
  SetMinSize(wxSize(-1, 270));
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetToolTip(
      "Move the pointer along the curve to inspect time and height. Click to "
      "pin a point; click again to release it.");
  Bind(wxEVT_PAINT, &XTidalCurvePanel::OnPaint, this);
  Bind(wxEVT_MOTION, &XTidalCurvePanel::OnMouseMove, this);
  Bind(wxEVT_LEFT_DOWN, &XTidalCurvePanel::OnLeftDown, this);
  Bind(wxEVT_LEAVE_WINDOW, &XTidalCurvePanel::OnMouseLeave, this);
}

void XTidalCurvePanel::SetCurve(const xtidal::HeightCurve& curve,
                                std::chrono::sys_seconds selected_time,
                                const std::string& display_time_zone) {
  curve_ = curve;
  selected_time_ = selected_time;
  display_time_zone_ = display_time_zone;
  inspected_sample_.reset();
  inspection_locked_ = false;
  Refresh();
}

void XTidalCurvePanel::SetNativeComparison(
    const xtidal::NativeTideComparison& comparison) {
  native_comparison_ = comparison;
  Refresh();
}

void XTidalCurvePanel::ClearNativeComparison() {
  native_comparison_.reset();
  Refresh();
}

void XTidalCurvePanel::SetNativeCurveVisible(bool visible) {
  native_curve_visible_ = visible;
  Refresh();
}

void XTidalCurvePanel::ClearCurve() {
  curve_ = {};
  native_comparison_.reset();
  inspected_sample_.reset();
  inspection_locked_ = false;
  Refresh();
}

void XTidalCurvePanel::UpdateInspection(const wxPoint& position) {
  const auto size = GetClientSize();
  const int width = size.x - kCurveLeft - kCurveRight;
  const int height = size.y - kCurveTop - kCurveBottom;
  if (curve_.samples.size() < 2 || width <= 0 || height <= 0 ||
      position.x < kCurveLeft || position.x > kCurveLeft + width ||
      position.y < kCurveTop || position.y > kCurveTop + height) {
    inspected_sample_.reset();
    Refresh(false);
    return;
  }

  const double fraction = static_cast<double>(position.x - kCurveLeft) / width;
  const auto first_time = curve_.samples.front().time;
  const auto last_time = curve_.samples.back().time;
  const auto span = last_time - first_time;
  const auto offset = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::duration<double>(span) * fraction);
  inspected_sample_ =
      xtidal::InterpolateHeightSample(curve_, first_time + offset);
  Refresh(false);
}

void XTidalCurvePanel::OnMouseMove(wxMouseEvent& event) {
  if (!inspection_locked_) UpdateInspection(event.GetPosition());
  event.Skip();
}

void XTidalCurvePanel::OnLeftDown(wxMouseEvent& event) {
  if (inspection_locked_) {
    inspection_locked_ = false;
    UpdateInspection(event.GetPosition());
  } else {
    UpdateInspection(event.GetPosition());
    inspection_locked_ = inspected_sample_.has_value();
  }
  Refresh(false);
}

void XTidalCurvePanel::OnMouseLeave(wxMouseEvent& event) {
  if (!inspection_locked_) {
    inspected_sample_.reset();
    Refresh(false);
  }
  event.Skip();
}

void XTidalCurvePanel::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  dc.SetBackground(wxBrush(wxColour(250, 252, 255)));
  dc.Clear();
  const auto size = GetClientSize();
  const int width = size.x - kCurveLeft - kCurveRight;
  const int height = size.y - kCurveTop - kCurveBottom;
  if (curve_.samples.size() < 2 || width <= 0 || height <= 0) {
    dc.DrawText("Water-level curve unavailable", 12, 12);
    return;
  }
  auto [minimum, maximum] =
      std::minmax_element(curve_.samples.begin(), curve_.samples.end(),
                          [](const auto& first, const auto& second) {
                            return first.height_m < second.height_m;
                          });
  double low = minimum->height_m;
  double high = maximum->height_m;
  const bool draw_native =
      native_curve_visible_ && native_comparison_ &&
      native_comparison_->station.height_in_metres_available &&
      native_comparison_->native_curve.samples.size() >= 2;
  if (draw_native) {
    for (const auto& sample : native_comparison_->native_curve.samples) {
      low = std::min(low, sample.height_m);
      high = std::max(high, sample.height_m);
    }
  }
  if (high - low < 0.01) {
    low -= 0.1;
    high += 0.1;
  }
  const auto first_time = curve_.samples.front().time;
  const auto last_time = curve_.samples.back().time;
  const double time_span =
      std::chrono::duration<double>(last_time - first_time).count();
  auto point = [&](const xtidal::HeightSample& sample) {
    const double time_fraction =
        std::chrono::duration<double>(sample.time - first_time).count() /
        time_span;
    const double height_fraction = (sample.height_m - low) / (high - low);
    return wxPoint(
        kCurveLeft + static_cast<int>(std::lround(time_fraction * width)),
        kCurveTop + height -
            static_cast<int>(std::lround(height_fraction * height)));
  };
  dc.SetPen(wxPen(wxColour(110, 120, 130), 1));
  dc.DrawLine(kCurveLeft, kCurveTop, kCurveLeft, kCurveTop + height);
  dc.DrawLine(kCurveLeft, kCurveTop + height, kCurveLeft + width,
              kCurveTop + height);
  const bool model_msl = curve_.datum_id == "model-mean-sea-level";
  const auto axis_suffix = xtidal::IsChartDatum(curve_) ? "m CD"
                           : model_msl                  ? "m MSL"
                                                        : "m";
  dc.DrawText(wxString::Format("%.2f %s", high, axis_suffix), 2, kCurveTop - 7);
  dc.DrawText(wxString::Format("%.2f %s", low, axis_suffix), 2,
              kCurveTop + height - 7);
  dc.SetTextForeground(wxColour(40, 50, 60));
  dc.DrawText(wxString::FromUTF8(xtidal::IsChartDatum(curve_)
                                     ? "Water height above Chart Datum"
                                     : "Water height — " + curve_.datum_name),
              kCurveLeft, 7);
  if (draw_native) {
    constexpr int swatch_width = 32;
    constexpr int swatch_gap = 7;
    const int maximum_label_width =
        std::max(80, width - swatch_width - swatch_gap);
    const auto fit_label = [&](wxString label) {
      if (dc.GetTextExtent(label).x <= maximum_label_width) return label;
      const wxString ellipsis = wxString::FromUTF8("…");
      while (!label.empty() &&
             dc.GetTextExtent(label + ellipsis).x > maximum_label_width)
        label.RemoveLast();
      return label + ellipsis;
    };
    const wxString native_label = fit_label(
        wxString::Format("Native station: %s — %.1f km",
                         wxString::FromUTF8(native_comparison_->station.name),
                         native_comparison_->distance_km));
    const int text_height = dc.GetTextExtent("OfflineTides forecast").y;
    constexpr int first_y = 27;
    const int second_y = first_y + text_height + 4;
    const int label_x = kCurveLeft + swatch_width + swatch_gap;
    dc.SetPen(wxPen(wxColour(20, 105, 180), 2));
    dc.DrawLine(kCurveLeft, first_y + text_height / 2,
                kCurveLeft + swatch_width, first_y + text_height / 2);
    dc.SetTextForeground(wxColour(40, 50, 60));
    dc.DrawText("OfflineTides forecast", label_x, first_y);
    dc.SetPen(wxPen(wxColour(220, 105, 15), 3, wxPENSTYLE_LONG_DASH));
    dc.DrawLine(kCurveLeft, second_y + text_height / 2,
                kCurveLeft + swatch_width, second_y + text_height / 2);
    dc.SetTextForeground(wxColour(165, 78, 10));
    dc.DrawText(native_label, label_x, second_y);
  }
  dc.SetPen(wxPen(wxColour(20, 105, 180), 2));
  for (std::size_t index = 1; index < curve_.samples.size(); ++index)
    dc.DrawLine(point(curve_.samples[index - 1]), point(curve_.samples[index]));
  if (draw_native) {
    dc.SetPen(wxPen(wxColour(220, 105, 15), 3, wxPENSTYLE_LONG_DASH));
    const auto& samples = native_comparison_->native_curve.samples;
    for (std::size_t index = 1; index < samples.size(); ++index)
      dc.DrawLine(point(samples[index - 1]), point(samples[index]));
  }

  const auto events = xtidal::PredictionService::FindHeightEvents(curve_);
  dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
  for (std::size_t index = 0; index < events.size(); ++index) {
    const auto& event = events[index];
    const auto marker = point({event.time, event.height_m});
    const bool high_water = event.type == xtidal::HeightEventType::kHighWater;
    dc.SetPen(
        wxPen(high_water ? wxColour(16, 92, 160) : wxColour(22, 125, 105), 2));
    dc.DrawCircle(marker, 4);
    const wxString label = wxString::Format(
        "%s %.2f m%s", high_water ? "HW" : "LW", event.height_m,
        xtidal::IsChartDatum(curve_) ? " CD"
        : model_msl                  ? " MSL"
                                     : "");
    const auto extent = dc.GetTextExtent(label);
    int label_x = marker.x + 6;
    if (label_x + extent.x > kCurveLeft + width)
      label_x = marker.x - extent.x - 6;
    int label_y = high_water ? marker.y - extent.y - 5 : marker.y + 5;
    label_y = std::clamp(label_y, kCurveTop, kCurveTop + height - extent.y);
    dc.SetTextForeground(wxColour(25, 45, 65));
    dc.DrawText(label, label_x, label_y);
  }
  if (selected_time_ >= first_time && selected_time_ <= last_time) {
    const double fraction =
        std::chrono::duration<double>(selected_time_ - first_time).count() /
        time_span;
    const int x = kCurveLeft + static_cast<int>(std::lround(fraction * width));
    dc.SetPen(wxPen(wxColour(210, 60, 40), 1, wxPENSTYLE_SHORT_DASH));
    dc.DrawLine(x, kCurveTop, x, kCurveTop + height);
  }
  if (inspected_sample_) {
    const auto marker = point(*inspected_sample_);
    dc.SetPen(wxPen(wxColour(120, 65, 155), 1, wxPENSTYLE_SHORT_DASH));
    dc.DrawLine(marker.x, kCurveTop, marker.x, kCurveTop + height);
    dc.SetPen(wxPen(wxColour(90, 40, 130), 2));
    dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
    dc.DrawCircle(marker, 5);

    const wxString datum_suffix = xtidal::IsChartDatum(curve_) ? "m CD"
                                  : model_msl                  ? "m MSL"
                                                               : "m";
    wxString readout = wxString::Format(
        "%s%s  —  %.2f %s", inspection_locked_ ? "Pinned: " : "",
        FormatDisplayTime(inspected_sample_->time, display_time_zone_, false,
                          true),
        inspected_sample_->height_m, datum_suffix);
    if (draw_native) {
      const auto native_sample = xtidal::InterpolateHeightSample(
          native_comparison_->native_curve, inspected_sample_->time);
      if (native_sample) {
        readout +=
            wxString::Format("  |  Native %.2f m%s", native_sample->height_m,
                             native_comparison_->curve_mode ==
                                     xtidal::NativeCurveMode::kMeanAligned
                                 ? " (aligned)"
                                 : "");
      }
    }
    const auto extent = dc.GetTextExtent(readout);
    constexpr int padding_x = 7;
    constexpr int padding_y = 4;
    int readout_x = marker.x + 9;
    if (readout_x + extent.x + 2 * padding_x > kCurveLeft + width)
      readout_x = marker.x - extent.x - 2 * padding_x - 9;
    const int maximum_readout_x =
        std::max(kCurveLeft, kCurveLeft + width - extent.x - 2 * padding_x);
    readout_x = std::clamp(readout_x, kCurveLeft, maximum_readout_x);
    const int readout_y = kCurveTop + 6;
    dc.SetPen(wxPen(wxColour(120, 65, 155), 1));
    dc.SetBrush(wxBrush(wxColour(250, 246, 255)));
    dc.DrawRoundedRectangle(readout_x, readout_y, extent.x + 2 * padding_x,
                            extent.y + 2 * padding_y, 4);
    dc.SetTextForeground(wxColour(55, 30, 80));
    dc.DrawText(readout, readout_x + padding_x, readout_y + padding_y);
  }
  const auto display =
      xtidal::ConvertTimeForDisplay(first_time, display_time_zone_);
  const auto zone_label = wxString::FromUTF8(display.abbreviation);
  const auto zone_extent = dc.GetTextExtent(zone_label);
  dc.DrawText(zone_label, kCurveLeft + width - zone_extent.x,
              kCurveTop + height + 6);
}

XTidalDialog::XTidalDialog(wxWindow* parent, xtidal_pi& plugin)
    : wxDialog(parent, wxID_ANY, "Tidal Height Forecast", wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      plugin_(plugin) {
  content_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxVSCROLL);
  content_->SetScrollRate(0, 12);
  root_sizer_ = new wxBoxSizer(wxVERTICAL);
  auto* package_box = new wxStaticBoxSizer(wxVERTICAL, content_, "XTD data");
  auto* package_row = new wxBoxSizer(wxHORIZONTAL);
  package_row->Add(new wxButton(content_, kOpen, "Open .xtdt package..."), 0,
                   wxRIGHT, 8);
  package_status_ = new wxStaticText(content_, wxID_ANY, "No package loaded");
  package_row->Add(package_status_, 1, wxALIGN_CENTER_VERTICAL);
  package_box->Add(package_row, 0, wxEXPAND | wxALL, 6);
  root_sizer_->Add(package_box, 0, wxEXPAND | wxALL, 8);

  auto* location_box =
      new wxStaticBoxSizer(wxVERTICAL, content_, "Forecast position");
  auto* source_row = new wxBoxSizer(wxHORIZONTAL);
  source_row->Add(new wxStaticText(content_, wxID_ANY, "Position from:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  wxArrayString location_sources;
  location_sources.Add("Chart cursor");
  location_sources.Add("OpenCPN waypoint or mark");
  location_sources.Add("Manual coordinates");
  location_source_ = new wxChoice(content_, kLocationSource, wxDefaultPosition,
                                  wxDefaultSize, location_sources);
  location_source_->SetSelection(0);
  source_row->Add(location_source_, 1, wxALIGN_CENTER_VERTICAL);
  location_box->Add(source_row, 0, wxEXPAND | wxALL, 6);

  auto* waypoint_row = new wxBoxSizer(wxHORIZONTAL);
  waypoint_row->Add(new wxStaticText(content_, wxID_ANY, "Waypoint or mark:"),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  waypoint_ = new wxChoice(content_, kWaypoint);
  waypoint_row->Add(waypoint_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  waypoint_row->Add(new wxButton(content_, kRefreshWaypoints, "Refresh"), 0,
                    wxALIGN_CENTER_VERTICAL);
  location_box->Add(waypoint_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

  auto* manual_row = new wxBoxSizer(wxHORIZONTAL);
  manual_row->Add(new wxStaticText(content_, wxID_ANY, "Latitude:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  manual_latitude_ = new wxTextCtrl(content_, wxID_ANY);
  manual_row->Add(manual_latitude_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  manual_row->Add(new wxStaticText(content_, wxID_ANY, "Longitude:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  manual_longitude_ = new wxTextCtrl(content_, wxID_ANY);
  manual_row->Add(manual_longitude_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  apply_manual_ = new wxButton(content_, kApplyManual, "Apply");
  manual_row->Add(apply_manual_, 0, wxALIGN_CENTER_VERTICAL);
  manual_latitude_->SetToolTip(
      "Signed decimal WGS84 latitude; N/S suffix is also accepted");
  manual_longitude_->SetToolTip(
      "Signed decimal WGS84 longitude; E/W suffix is also accepted");
  location_box->Add(manual_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  forecast_position_ =
      new wxStaticText(content_, wxID_ANY, "Move over the chart to sample");
  location_box->Add(forecast_position_, 0,
                    wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  root_sizer_->Add(location_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

  auto* time_box =
      new wxStaticBoxSizer(wxVERTICAL, content_, "Prediction time");
  time_ = new wxStaticText(content_, wxID_ANY, "");
  time_box->Add(time_, 0, wxLEFT | wxRIGHT | wxTOP, 6);
  auto* zone_row = new wxBoxSizer(wxHORIZONTAL);
  zone_row->Add(new wxStaticText(content_, wxID_ANY, "Display time zone:"), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  wxArrayString time_zones;
  time_zones.Add("UTC");
  time_zones.Add("system-local");
  time_zones.Add("Europe/London");
  time_zones.Add("Europe/Paris");
  time_zones.Add("Europe/Athens");
  time_zones.Add("America/New_York");
  time_zones.Add("America/Halifax");
  time_zones.Add("America/Los_Angeles");
  time_zones.Add("America/Sao_Paulo");
  time_zones.Add("Asia/Tokyo");
  time_zones.Add("Asia/Singapore");
  time_zones.Add("Australia/Sydney");
  time_zones.Add("Pacific/Auckland");
  time_zone_ = new wxComboBox(content_, kTimeZone, "UTC", wxDefaultPosition,
                              wxDefaultSize, time_zones,
                              wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
  time_zone_->SetToolTip(
      "Select or type an IANA time-zone name; calculations remain in UTC");
  zone_row->Add(time_zone_, 1, wxALIGN_CENTER_VERTICAL);
  time_box->Add(zone_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
  auto* period_row = new wxBoxSizer(wxHORIZONTAL);
  period_row->Add(new wxStaticText(content_, wxID_ANY, "Show:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  wxArrayString periods;
  periods.Add("Next tides (48 hours)");
  periods.Add("Tides for selected date");
  forecast_period_ = new wxChoice(content_, kForecastPeriod, wxDefaultPosition,
                                  wxDefaultSize, periods);
  forecast_period_->SetSelection(0);
  period_row->Add(forecast_period_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  period_row->Add(new wxStaticText(content_, wxID_ANY, "Date:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  forecast_date_ = new wxDatePickerCtrl(
      content_, kForecastDate, wxDefaultDateTime, wxDefaultPosition,
      wxDefaultSize, wxDP_DEFAULT | wxDP_SHOWCENTURY);
  period_row->Add(forecast_date_, 0, wxALIGN_CENTER_VERTICAL);
  time_box->Add(period_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
  auto* time_buttons = new wxBoxSizer(wxHORIZONTAL);
  earlier_ = new wxButton(content_, kEarlier, "-1 hour");
  now_ = new wxButton(content_, kNow, "Now");
  later_ = new wxButton(content_, kLater, "+1 hour");
  time_buttons->Add(earlier_, 0, wxRIGHT, 5);
  time_buttons->Add(now_, 0, wxRIGHT, 5);
  time_buttons->Add(later_, 0);
  time_box->Add(time_buttons, 0, wxALL, 6);
  root_sizer_->Add(time_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

  auto* height_box =
      new wxStaticBoxSizer(wxVERTICAL, content_, "Water level forecast");
  auto* reference_row = new wxBoxSizer(wxHORIZONTAL);
  reference_row->Add(
      new wxStaticText(content_, wxID_ANY, "Display height reference:"), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  wxArrayString height_references;
  height_references.Add("Height above Chart Datum");
  height_references.Add("Level relative to model mean sea level");
  height_reference_ =
      new wxChoice(content_, kHeightReference, wxDefaultPosition, wxDefaultSize,
                   height_references);
  height_reference_->SetSelection(0);
  height_reference_->SetToolTip(
      "Changes display reference only; the astronomical prediction and UTC "
      "times are unchanged");
  reference_row->Add(height_reference_, 1, wxALIGN_CENTER_VERTICAL);
  height_box->Add(reference_row, 0, wxEXPAND | wxALL, 6);
  height_status_ = new wxStaticText(content_, wxID_ANY,
                                    "Load an authenticated .xtdt package");
  height_curve_ = new XTidalCurvePanel(content_);
  height_events_ = new wxTextCtrl(
      content_, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 118),
      wxTE_READONLY | wxTE_MULTILINE | wxTE_DONTWRAP | wxBORDER_SIMPLE);
  height_events_->SetMinSize(wxSize(-1, 100));
  height_box->Add(height_status_, 0, wxEXPAND | wxALL, 6);
  height_box->Add(height_curve_, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);
  height_box->Add(height_events_, 0, wxEXPAND | wxALL, 6);
  auto* export_row = new wxBoxSizer(wxHORIZONTAL);
  export_json_ = new wxButton(content_, kExportJson, "Export JSON...");
  export_csv_ = new wxButton(content_, kExportCsv, "Export CSV...");
  export_json_->Disable();
  export_csv_->Disable();
  export_row->Add(export_json_, 0, wxRIGHT, 6);
  export_row->Add(export_csv_, 0);
  height_box->Add(export_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
  root_sizer_->Add(height_box, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

  auto* comparison_box = new wxStaticBoxSizer(
      wxVERTICAL, content_, "Nearest native OpenCPN tide station");
  show_native_curve_ =
      new wxCheckBox(content_, kShowNativeCurve,
                     "Show native station curve on the tidal-height graph");
  show_native_curve_->Disable();
  show_native_curve_->SetToolTip(
      "Uses absolute heights only when datum equivalence is established; "
      "otherwise the graph identifies the applied transform or alignment");
  comparison_box->Add(show_native_curve_, 0, wxEXPAND | wxALL, 6);
  native_comparison_ = new wxStaticText(
      content_, wxID_ANY, "Choose a forecast position to compare");
  comparison_box->Add(native_comparison_, 0,
                      wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  root_sizer_->Add(comparison_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                   8);

  root_sizer_->Add(
      new wxStaticText(
          content_, wxID_ANY,
          "Offline XTD prediction only. Missing or masked data is shown "
          "as unknown."),
      0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
  content_->SetSizer(root_sizer_);
  UpdateScrollableLayout();
  auto* dialog_sizer = new wxBoxSizer(wxVERTICAL);
  dialog_sizer->Add(content_, 1, wxEXPAND);
  SetSizer(dialog_sizer);
  const wxRect work_area = wxGetClientDisplayRect();
  const wxSize content_min = root_sizer_->CalcMin();
  const int available_width = std::max(720, work_area.width - 80);
  const int available_height = std::max(640, work_area.height - 80);
  SetSize(wxSize(std::min(std::max(900, content_min.x), available_width),
                 std::min(content_min.y, available_height)));
  SetMinSize(wxSize(720, std::min(640, available_height)));
  UpdateScrollableLayout();
  content_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
    UpdateScrollableLayout();
    event.Skip();
  });

  Bind(wxEVT_BUTTON, &XTidalDialog::OnOpen, this, kOpen);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnNow, this, kNow);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnEarlier, this, kEarlier);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnLater, this, kLater);
  Bind(wxEVT_COMBOBOX, &XTidalDialog::OnTimeZone, this, kTimeZone);
  Bind(wxEVT_TEXT_ENTER, &XTidalDialog::OnTimeZone, this, kTimeZone);
  Bind(wxEVT_CHOICE, &XTidalDialog::OnHeightReference, this, kHeightReference);
  Bind(wxEVT_CHOICE, &XTidalDialog::OnLocationSource, this, kLocationSource);
  Bind(wxEVT_CHOICE, &XTidalDialog::OnWaypoint, this, kWaypoint);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnRefreshWaypoints, this,
       kRefreshWaypoints);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnApplyManual, this, kApplyManual);
  Bind(wxEVT_CHOICE, &XTidalDialog::OnForecastPeriod, this, kForecastPeriod);
  Bind(wxEVT_DATE_CHANGED, &XTidalDialog::OnForecastDate, this, kForecastDate);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnExportJson, this, kExportJson);
  Bind(wxEVT_BUTTON, &XTidalDialog::OnExportCsv, this, kExportCsv);
  Bind(wxEVT_CHECKBOX, &XTidalDialog::OnShowNativeCurve, this,
       kShowNativeCurve);
  Bind(wxEVT_CLOSE_WINDOW, &XTidalDialog::OnClose, this);
  UpdateLocationControls();
  UpdatePeriodControls();
}

void XTidalDialog::SetPackageStatus(const wxString& text, bool package_loaded,
                                    bool has_vertical_datum) {
  package_status_->SetLabel(text);
  if (package_loaded && !has_vertical_datum &&
      height_reference_->GetSelection() == 0) {
    height_reference_->SetSelection(1);
    plugin_.SetHeightReference(1);
  }
  height_reference_->Enable(true);
  UpdateScrollableLayout();
}

void XTidalDialog::SetPredictionTime(std::chrono::sys_seconds utc) {
  if (forecast_period_ && forecast_period_->GetSelection() == 1) {
    const auto date = forecast_date_->GetValue();
    time_->SetLabel(wxString::Format(
        "Tides for %04d-%02d-%02d (%s; prediction sampled in UTC)",
        date.GetYear(), static_cast<int>(date.GetMonth()) + 1, date.GetDay(),
        wxString::FromUTF8(plugin_.DisplayTimeZone())));
    return;
  }
  time_->SetLabel(
      FormatDisplayTime(utc, plugin_.DisplayTimeZone(), true, true) + " (" +
      wxString::FromUTF8(plugin_.DisplayTimeZone()) + ")");
}

void XTidalDialog::SetDisplayTimeZone(const wxString& time_zone_id) {
  time_zone_->SetValue(time_zone_id);
}

void XTidalDialog::SetDisplayOptions(int height_reference) {
  height_reference_->SetSelection(height_reference == 1 ? 1 : 0);
}

void XTidalDialog::SetForecastOptions(int location_source, int forecast_period,
                                      xtidal::ForecastDate date,
                                      double manual_latitude,
                                      double manual_longitude) {
  location_source_->SetSelection(std::clamp(location_source, 0, 2));
  forecast_period_->SetSelection(forecast_period == 1 ? 1 : 0);
  wxDateTime selected_date;
  selected_date.Set(static_cast<wxDateTime::wxDateTime_t>(date.day),
                    static_cast<wxDateTime::Month>(date.month - 1), date.year);
  if (selected_date.IsValid()) forecast_date_->SetValue(selected_date);
  if (std::isfinite(manual_latitude))
    manual_latitude_->SetValue(wxString::Format("%.6f", manual_latitude));
  if (std::isfinite(manual_longitude))
    manual_longitude_->SetValue(wxString::Format("%.6f", manual_longitude));
  UpdateLocationControls();
  UpdatePeriodControls();
}

void XTidalDialog::SetLocationSource(int selection) {
  location_source_->SetSelection(std::clamp(selection, 0, 2));
  UpdateLocationControls();
}

void XTidalDialog::SetForecastPeriod(int selection) {
  forecast_period_->SetSelection(selection == 1 ? 1 : 0);
  UpdatePeriodControls();
}

void XTidalDialog::SetForecastPosition(const wxString& text) {
  forecast_position_->SetLabel(text);
  UpdateScrollableLayout();
}

void XTidalDialog::RefreshWaypoints() {
  const auto selected_guid = plugin_.SelectedWaypointGuid();
  waypoint_->Clear();
  waypoint_guids_.clear();
  int selected_index = wxNOT_FOUND;
  for (const auto& waypoint : plugin_.ListForecastWaypoints()) {
    const wxString name = waypoint.name.empty()
                              ? wxString::FromUTF8("Unnamed waypoint")
                              : waypoint.name;
    waypoint_->Append(wxString::Format("%s — %.5f°, %.5f°", name,
                                       waypoint.latitude, waypoint.longitude));
    waypoint_guids_.push_back(waypoint.guid);
    if (waypoint.guid == selected_guid)
      selected_index = static_cast<int>(waypoint_guids_.size() - 1);
  }
  if (selected_index != wxNOT_FOUND) waypoint_->SetSelection(selected_index);
  waypoint_->SetToolTip(
      waypoint_guids_.empty()
          ? "No OpenCPN waypoints or marks are currently available"
          : "Select from the waypoints and marks in the current OpenCPN "
            "profile");
}

void XTidalDialog::UpdateLocationControls() {
  const int source = location_source_->GetSelection();
  waypoint_->Enable(source == 1);
  manual_latitude_->Enable(source == 2);
  manual_longitude_->Enable(source == 2);
  apply_manual_->Enable(source == 2);
}

void XTidalDialog::UpdatePeriodControls() {
  const bool selected_date = forecast_period_->GetSelection() == 1;
  forecast_date_->Enable(selected_date);
  earlier_->Enable(!selected_date);
  now_->Enable(!selected_date);
  later_->Enable(!selected_date);
  SetPredictionTime(plugin_.PredictionTime());
}

void XTidalDialog::UpdateScrollableLayout() {
  if (!content_ || !root_sizer_) return;
  const wxSize minimum = root_sizer_->CalcMin();
  const int width = std::max(content_->GetClientSize().x, minimum.x);
  const int height = std::max(content_->GetClientSize().y, minimum.y);
  content_->SetVirtualSize(width, height);
  root_sizer_->SetDimension(0, 0, width, height);
}

void XTidalDialog::SetHeightCurve(
    const xtidal::HeightCurve& curve,
    const std::vector<xtidal::HeightEvent>& events,
    std::chrono::sys_seconds selected_time) {
  if (curve.samples.empty()) {
    ClearHeightCurve("Height unknown at this position");
    return;
  }
  const auto nearest =
      std::min_element(curve.samples.begin(), curve.samples.end(),
                       [&](const auto& first, const auto& second) {
                         return std::abs((first.time - selected_time).count()) <
                                std::abs((second.time - selected_time).count());
                       });
  wxString status = wxString::Format(
      "Selected time: %s", wxString::FromUTF8(xtidal::FormatHeightForDisplay(
                               curve, nearest->height_m)));
  if (curve.quality) {
    status += wxString::Format(
        "\nLocal support: %s — harmonic uncertainty about ±%.2f m — "
        "nearest observation %.0f km",
        wxString::FromUTF8(xtidal::HeightSupportDescription(*curve.quality)),
        curve.quality->harmonic_sigma_m,
        curve.quality->nearest_observation_distance_km);
    if (curve.quality->observation_count)
      status += wxString::Format(
          " — %u contributing observation%s", curve.quality->observation_count,
          curve.quality->observation_count == 1 ? "" : "s");
  } else {
    status += "\nLocal quality information unavailable";
  }
  if (curve.vertical_datum) {
    status += wxString::Format(
        "\nVertical reference: %s (%s) — offset +%.2f m from model MSL — "
        "uncertainty about ±%.2f m — nearest datum station %.0f km",
        wxString::FromUTF8(
            xtidal::VerticalDatumSupportDescription(*curve.vertical_datum)),
        wxString::FromUTF8(
            xtidal::VerticalDatumRealizationDescription(*curve.vertical_datum)),
        curve.vertical_datum->offset_m, curve.vertical_datum->uncertainty_m,
        curve.vertical_datum->nearest_station_distance_km);
  }
  if (!xtidal::IsChartDatum(curve))
    status +=
        "\nNot Chart Datum — do not apply this absolute height directly to "
        "charted depth or under-keel-clearance calculations.";
  height_status_->SetLabel(status);
  height_status_->Wrap(std::max(300, content_->GetClientSize().x - 40));
  wxString summary;
  for (const auto& event : events) {
    if (!summary.empty()) summary += "\n";
    summary += wxString::Format(
        "%s  %s  —  %s",
        event.type == xtidal::HeightEventType::kHighWater ? "HW" : "LW",
        FormatDisplayTime(event.time, plugin_.DisplayTimeZone(), false, false),
        wxString::FromUTF8(
            xtidal::FormatHeightForDisplay(curve, event.height_m)));
  }
  height_events_->SetValue(summary);
  height_curve_->SetCurve(curve, selected_time, plugin_.DisplayTimeZone());
  export_json_->Enable();
  export_csv_->Enable();
  UpdateScrollableLayout();
  if (const char* smoke_test = std::getenv("XTIDAL_UI_SMOKE_TEST");
      smoke_test && *smoke_test)
    wxLogMessage(
        "offlinetides_pi: forecast layout curve_height_px=%d "
        "curve_min_height_px=%d virtual_height_px=%d",
        height_curve_->GetSize().y, height_curve_->GetMinSize().y,
        content_->GetVirtualSize().y);
}

void XTidalDialog::ClearHeightCurve(const wxString& message) {
  height_status_->SetLabel(message);
  height_events_->Clear();
  height_curve_->ClearCurve();
  export_json_->Disable();
  export_csv_->Disable();
  UpdateScrollableLayout();
}

void XTidalDialog::SetNativeComparison(
    const xtidal::NativeTideComparison& comparison) {
  wxString text = wxString::Format(
      "%s — %.1f km from the OfflineTides forecast point\n"
      "Native source: %s\nNative datum: %s%s\nOfflineTides datum: %s (%s)\n",
      wxString::FromUTF8(comparison.station.name), comparison.distance_km,
      wxString::FromUTF8(comparison.station.source_name),
      wxString::FromUTF8(comparison.station.datum_name),
      comparison.station.subordinate ? " (inherited from reference station)"
                                     : "",
      wxString::FromUTF8(comparison.xtidal_datum_name),
      wxString::FromUTF8(comparison.xtidal_datum_id));
  if (!comparison.station.source_description.empty())
    text += "Native provenance: " +
            wxString::FromUTF8(comparison.station.source_description) + "\n";
  if (!comparison.station.source_dataset_version.empty())
    text += "Dataset version: " +
            wxString::FromUTF8(comparison.station.source_dataset_version) +
            "\n";
  if (comparison.station.datum_offset_m)
    text += wxString::Format("Native harmonic Z0: %.3f m\n",
                             *comparison.station.datum_offset_m);
  text += "Graph mode: " +
          wxString::FromUTF8(
              xtidal::NativeCurveModeDescription(comparison.curve_mode)) +
          "\n";
  if (comparison.mean_absolute_event_time_difference_minutes)
    text += wxString::Format(
        "Matched HW/LW events: %zu — mean absolute time difference %.1f min\n",
        comparison.matched_events,
        *comparison.mean_absolute_event_time_difference_minutes);
  else
    text += "No comparable native HW/LW events in this forecast window\n";
  if (comparison.mean_absolute_height_difference_m)
    text += wxString::Format("Mean absolute height difference: %.2f m",
                             *comparison.mean_absolute_height_difference_m);
  else
    text += "Height difference " +
            wxString::FromUTF8(comparison.height_comparison_reason);
  native_comparison_->SetLabel(text);
  show_native_curve_->Enable(comparison.station.height_in_metres_available &&
                             comparison.native_curve.samples.size() >= 2);
  height_curve_->SetNativeComparison(comparison);
  height_curve_->SetNativeCurveVisible(show_native_curve_->GetValue());
  native_comparison_->Wrap(std::max(300, content_->GetClientSize().x - 40));
  UpdateScrollableLayout();
}

void XTidalDialog::ClearNativeComparison(const wxString& message) {
  native_comparison_->SetLabel(message);
  show_native_curve_->Disable();
  height_curve_->ClearNativeComparison();
  UpdateScrollableLayout();
}

void XTidalDialog::SetNativeCurveVisible(bool visible) {
  show_native_curve_->SetValue(visible);
  height_curve_->SetNativeCurveVisible(visible);
}

void XTidalDialog::OnOpen(wxCommandEvent&) {
  wxFileDialog picker(this, "Open authenticated OfflineTides package", "", "",
                      "OfflineTides packages (*.xtdt)|*.xtdt|All files|*.*",
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  const auto previous = plugin_.LastPackagePath();
  if (!previous.empty()) picker.SetPath(previous);
  if (picker.ShowModal() != wxID_OK) return;
  wxString error;
  if (!plugin_.LoadPackage(picker.GetPath(), &error)) {
    wxMessageBox(error, "Could not open .xtdt package", wxOK | wxICON_ERROR,
                 this);
  }
}

void XTidalDialog::OnExportJson(wxCommandEvent&) {
  wxFileDialog picker(this, "Export OfflineTides forecast as JSON", "", "",
                      "JSON forecast (*.json)|*.json|All files|*.*",
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (picker.ShowModal() != wxID_OK) return;
  wxString error;
  if (!plugin_.ExportForecast(picker.GetPath(), true, &error))
    wxMessageBox(error, "Could not export forecast", wxOK | wxICON_ERROR, this);
}

void XTidalDialog::OnExportCsv(wxCommandEvent&) {
  wxFileDialog picker(this, "Export OfflineTides forecast as CSV", "", "",
                      "CSV forecast (*.csv)|*.csv|All files|*.*",
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (picker.ShowModal() != wxID_OK) return;
  wxString error;
  if (!plugin_.ExportForecast(picker.GetPath(), false, &error))
    wxMessageBox(error, "Could not export forecast", wxOK | wxICON_ERROR, this);
}

void XTidalDialog::OnShowNativeCurve(wxCommandEvent&) {
  plugin_.SetShowNativeCurve(show_native_curve_->GetValue());
}

void XTidalDialog::OnNow(wxCommandEvent&) {
  plugin_.SetPredictionTime(std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now()));
}
void XTidalDialog::OnEarlier(wxCommandEvent&) {
  plugin_.ShiftPredictionTime(std::chrono::hours{-1});
}
void XTidalDialog::OnLater(wxCommandEvent&) {
  plugin_.ShiftPredictionTime(std::chrono::hours{1});
}
void XTidalDialog::OnTimeZone(wxCommandEvent&) {
  wxString error;
  if (!plugin_.SetDisplayTimeZone(time_zone_->GetValue(), &error)) {
    wxMessageBox(error, "Invalid display time zone", wxOK | wxICON_ERROR, this);
    time_zone_->SetValue(wxString::FromUTF8(plugin_.DisplayTimeZone()));
  }
}
void XTidalDialog::OnHeightReference(wxCommandEvent&) {
  plugin_.SetHeightReference(height_reference_->GetSelection());
}
void XTidalDialog::OnLocationSource(wxCommandEvent&) {
  UpdateLocationControls();
  plugin_.SetForecastLocationSource(location_source_->GetSelection());
}
void XTidalDialog::OnWaypoint(wxCommandEvent&) {
  const int selection = waypoint_->GetSelection();
  if (selection < 0 || selection >= static_cast<int>(waypoint_guids_.size()))
    return;
  wxString error;
  if (!plugin_.SelectForecastWaypoint(waypoint_guids_[selection], &error))
    wxMessageBox(error, "Could not select waypoint", wxOK | wxICON_ERROR, this);
}
void XTidalDialog::OnRefreshWaypoints(wxCommandEvent&) { RefreshWaypoints(); }
void XTidalDialog::OnApplyManual(wxCommandEvent&) {
  wxString error;
  if (!plugin_.SetManualForecastPosition(manual_latitude_->GetValue(),
                                         manual_longitude_->GetValue(), &error))
    wxMessageBox(error, "Invalid coordinates", wxOK | wxICON_ERROR, this);
}
void XTidalDialog::OnForecastPeriod(wxCommandEvent&) {
  UpdatePeriodControls();
  plugin_.SetForecastPeriod(forecast_period_->GetSelection());
}
void XTidalDialog::OnForecastDate(wxDateEvent&) {
  const auto date = forecast_date_->GetValue();
  wxString error;
  if (!plugin_.SetForecastDate(
          {date.GetYear(), static_cast<unsigned>(date.GetMonth()) + 1,
           static_cast<unsigned>(date.GetDay())},
          &error))
    wxMessageBox(error, "Invalid forecast date", wxOK | wxICON_ERROR, this);
  SetPredictionTime(plugin_.PredictionTime());
}
void XTidalDialog::OnClose(wxCloseEvent&) { Hide(); }
