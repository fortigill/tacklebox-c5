#include "ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "ble.h"
#include "display.h"
#include "drone.h"
#include "net.h"
#include "ota.h"
#include "theme.h"
#include "timesvc.h"
#include "ui_icons.h"
#include "version.h"
#include "weather.h"

// Note: the built-in Montserrat fonts only contain ASCII + LV_SYMBOL_* icons,
// so keep UI strings ASCII-only.

namespace {

enum class Screen {
  Desktop, Menu, Wifi, Password, Connecting, About, Display, Calibrate,
  Ble, BleDetail, BleIgnored, Hunt, DateTime, TimeZone, Weather, WeatherZip,
  WifiAnalyzer, Trackers, Ota, Drones, DroneDetail, DroneLog, DroneLogDetail,
  WxAlerts, WxAlertDetail, Buoy, BuoyPick, BuoyId, Stock, StockSym
};

const int HEADER_H = 40;

int W() { return lv_disp_get_hor_res(nullptr); }
int H() { return lv_disp_get_ver_res(nullptr); }

Screen cur = Screen::Desktop;
Screen wifiBack = Screen::Menu;   // where "back" goes from Wi-Fi settings
Screen calReturn = Screen::Desktop;

// Widgets of the current screen (reset whenever the screen changes)
lv_obj_t *deskWifiIcon = nullptr;
lv_obj_t *deskWifiLbl = nullptr;
lv_obj_t *deskClock = nullptr;
lv_obj_t *deskWx = nullptr, *deskWxImg = nullptr;
lv_obj_t *deskDrone = nullptr;  // status-bar drone icon while Drone Watch runs
int deskDroneState = -1;        // last color applied: 0 steady, 1 blue, 2 red
lv_obj_t *droneAlertBox = nullptr;  // on the top layer, survives screen changes
String droneAlertId;
lv_obj_t *wxAlertBox = nullptr;     // NWS alert pop-up (top layer)
String wxAlertId;
String wxAlertDetailId;
lv_obj_t *wxaList = nullptr, *wxaStatus = nullptr;
uint32_t wxaVer = 0;
int deskWxBlink = -1;               // last opacity state applied to the desktop weather
lv_obj_t *wxImg = nullptr, *wxPlace = nullptr, *wxDesc = nullptr, *wxDetail = nullptr;
lv_obj_t *wxStatus = nullptr, *wxZipTa = nullptr;
// Desktop widgets: wave height and alert count (tap for their pages), plus
// the wave height in the status bar
lv_obj_t *deskWave = nullptr, *deskWaveLbl = nullptr, *deskWaveName = nullptr;
lv_obj_t *deskBar = nullptr, *deskRow = nullptr, *deskMenu = nullptr;
int deskLayoutKey = -1;  // bar + widget-row heights the layout was last done for
int deskWaveKey = -1;    // buoy data version the wave texts were built from
lv_obj_t *aboutUptime = nullptr;  // live uptime line on the About screen
// Stock quote: status bar readout, desktop bubble, stock page
lv_obj_t *deskBarStock = nullptr, *deskBarStockLbl = nullptr, *deskBarStockPct = nullptr;
lv_obj_t *deskStock = nullptr, *deskStockSym = nullptr, *deskStockPrice = nullptr, *deskStockPct = nullptr;
int deskStockKey = -1;
lv_obj_t *skName = nullptr, *skPrice = nullptr, *skChange = nullptr, *skDetail = nullptr, *skStatus = nullptr,
         *skTa = nullptr;
uint32_t skVer = 0;
Screen stockBack = Screen::Menu;
lv_obj_t *deskBarWave = nullptr, *deskBarWaveLbl = nullptr;
lv_obj_t *deskAlerts = nullptr, *deskAlertsLbl = nullptr;
int deskAlertsColor = -1;  // 0 normal, 1 red (unsnoozed alerts)
Screen wxaBack = Screen::Weather;
lv_obj_t *byWave = nullptr, *byName = nullptr, *byDetail = nullptr, *byStatus = nullptr, *byIdTa = nullptr;
uint32_t byVer = 0;
Screen buoyBack = Screen::Weather;

// Temperature readout: number, a drawn degree ring (the built-in fonts have
// no degree sign) and the unit letter
struct TempW {
  lv_obj_t *num = nullptr, *unit = nullptr;
};
TempW deskTemp, wxTemp;
Screen wxBack = Screen::Menu;
lv_obj_t *dtTime = nullptr, *dtDate = nullptr, *dtStatus = nullptr;
lv_obj_t *wifiStatusLbl = nullptr;
lv_obj_t *wifiList = nullptr;
lv_obj_t *wifiScanBtn = nullptr;
lv_obj_t *wifiForgetBtn = nullptr;
lv_obj_t *pwTa = nullptr;
lv_obj_t *calH = nullptr, *calV = nullptr, *calRing = nullptr, *calMsg = nullptr;
lv_obj_t *bleList = nullptr, *bleStatus = nullptr, *bleToggle = nullptr, *bleDetail = nullptr;
lv_obj_t *huntRssiLbl = nullptr, *huntArrowLbl = nullptr, *huntStateLbl = nullptr;
lv_obj_t *huntBar = nullptr, *huntChart = nullptr;
lv_chart_series_t *huntSer = nullptr;

// Hunt screen source (shared by BLE devices, trackers and Wi-Fi APs)
enum class HuntSrc { Ble, Wifi, Drone };
HuntSrc huntSrc = HuntSrc::Ble;
String huntTitle;
String huntBleAddr;
uint8_t huntWifiBssid[6];
uint8_t huntWifiChannel = 0;
Screen huntBack = Screen::BleDetail;

// Wi-Fi analyzer
const int WFA_CHANNELS = 13;
const size_t WFA_MAX_ROWS = 40;
lv_obj_t *wfaBars[WFA_CHANNELS + 1] = {nullptr};
lv_obj_t *wfaChLbl[WFA_CHANNELS + 1] = {nullptr};
lv_obj_t *wfaList = nullptr, *wfaStatus = nullptr, *wfaScanBtn = nullptr;
net::ScanState wfaLastSt = net::ScanState::Idle;
std::vector<net::ApInfo> wfaRows;

// Tracker detector
const uint32_t TRK_INTERVAL_MS = 30000;
lv_obj_t *trkList = nullptr, *trkStatus = nullptr, *trkToggle = nullptr;
bool trkWatching = false;
uint32_t trkNextScan = 0;
ble::Mode trkLastMode = ble::Mode::Idle;
std::vector<String> trkRowAddrs;
std::vector<ble::Tracker> trkRowTypes;

// OTA
lv_obj_t *otaBar = nullptr, *otaPct = nullptr, *otaMsg = nullptr;

// Drone Remote ID
const uint32_t DRONE_REFRESH_MS = 1000;
lv_obj_t *drnList = nullptr, *drnStatus = nullptr, *drnToggle = nullptr, *drnDetail = nullptr;
lv_obj_t *drnWatchBtn = nullptr, *drnLogBtn = nullptr;
std::vector<String> drnLogKeys;
String drnLogKey;
std::vector<String> drnRowKeys;
String drnDetailKey;
String huntDroneKey;
uint32_t drnLastFill = 0;

// BLE scanner state
const uint32_t BLE_LIST_REFRESH_MS = 1500;
const size_t BLE_MAX_ROWS = 30;
const uint32_t BLE_MIN_FREE_HEAP = 20000;  // stop adding rows below this
std::vector<String> bleRowAddrs;
String bleDetailAddr;
uint32_t bleLastFill = 0;
ble::Mode bleLastMode = ble::Mode::Idle;

// Hunt mode
const uint32_t HUNT_LOST_MS = 4000;     // no report for this long = signal lost
const size_t HUNT_TREND_SPAN = 3;       // compare against the value 3 samples back
const float HUNT_TREND_DB = 2.0f;       // change needed to call it closer/farther
const uint16_t HUNT_CHART_POINTS = 40;
std::vector<float> huntHist;
uint32_t huntLastSamples = 0;

int deskIconState = -1;  // last state the desktop icon was styled for
net::ScanState lastScanSt = net::ScanState::Idle;
std::vector<String> listSsids;
std::vector<bool> listSecure;
String targetSsid;
bool targetSecure = true;

// Calibration state
const int CAL_POINTS = 4;
const int CAL_MARGIN = 24;
const int CAL_SETTLE_SAMPLES = 3;   // discard while the stylus settles
lv_timer_t *calTimer = nullptr;
display::CalPoint calPts[CAL_POINTS];
int calIdx = 0;
bool calWaitRelease = true;
int calDownTicks = 0, calUpTicks = 0;
int32_t calSumX = 0, calSumY = 0;

void showDesktop();
void showMenu();
void showWifi();
void showPassword();
void showConnecting();
void showAbout();
void showDisplay();
void showCalibration(bool firstRun);
void showBle();
void showBleDetail();
void showBleIgnored();
void showHunt();
void showWifiAnalyzer();
void showTrackers();
void showDrones();
void showDroneDetail();
void showDroneLog();
void showDroneLogDetail();
void showDateTime();
void showTimeZone();
void showWeather();
void showWeatherZip();
void showWxAlerts();
void showWxAlertDetail();
void showBuoy();
void showBuoyPick();
void showBuoyId();
void showStock();
void showStockSym();

// Stock formatting shared by the status bar, desktop and stock page
String fmtPrice(const weather::Stock &s) {
  if (!s.valid) return "--";
  char b[24];
  const char *cur = s.currency == "USD" ? "$" : "";
  if (s.price >= 10000) snprintf(b, sizeof(b), "%s%.0f", cur, s.price);
  else if (s.price < 1) snprintf(b, sizeof(b), "%s%.4f", cur, s.price);
  else snprintf(b, sizeof(b), "%s%.2f", cur, s.price);
  return b;
}

String fmtPct(const weather::Stock &s) {
  float p = weather::stockChangePct(s);
  if (std::isnan(p)) return "";
  char b[16];
  snprintf(b, sizeof(b), "%+.2f%%", p);
  return b;
}

// Light green / red / neutral for the day change
lv_color_t pctColor(const weather::Stock &s) {
  float p = weather::stockChangePct(s);
  if (std::isnan(p) || fabsf(p) < 0.005f) return theme::text();
  return lv_palette_main(p > 0 ? LV_PALETTE_LIGHT_GREEN : LV_PALETTE_RED);
}

// ---------- helpers ----------

void resetPtrs() {
  deskWifiIcon = deskWifiLbl = deskClock = nullptr;
  deskWx = deskWxImg = wxImg = wxPlace = wxDesc = wxDetail = wxStatus = wxZipTa = nullptr;
  deskDrone = nullptr;
  deskDroneState = -1;
  wxaList = wxaStatus = nullptr;
  deskWaveName = nullptr;
  aboutUptime = nullptr;
  deskBarStock = deskBarStockLbl = deskBarStockPct = deskStock = deskStockSym = deskStockPrice = deskStockPct = nullptr;
  deskStockKey = -1;
  skName = skPrice = skChange = skDetail = skStatus = skTa = nullptr;
  skVer = 0;
  deskBar = deskRow = deskMenu = nullptr;
  deskLayoutKey = -1;
  deskWaveKey = -1;
  deskWave = deskWaveLbl = deskBarWave = deskBarWaveLbl = deskAlerts = deskAlertsLbl = nullptr;
  deskAlertsColor = -1;
  byWave = byName = byDetail = byStatus = byIdTa = nullptr;
  byVer = 0;
  deskWxBlink = -1;
  deskTemp = TempW();
  wxTemp = TempW();
  dtTime = dtDate = dtStatus = nullptr;
  wifiStatusLbl = wifiList = wifiScanBtn = wifiForgetBtn = nullptr;
  pwTa = nullptr;
  calH = calV = calRing = calMsg = nullptr;
  bleList = bleStatus = bleToggle = bleDetail = nullptr;
  huntRssiLbl = huntArrowLbl = huntStateLbl = huntBar = huntChart = nullptr;
  huntSer = nullptr;
  for (auto &b : wfaBars) b = nullptr;
  for (auto &l : wfaChLbl) l = nullptr;
  wfaList = wfaStatus = wfaScanBtn = nullptr;
  trkList = trkStatus = trkToggle = nullptr;
  otaBar = otaPct = otaMsg = nullptr;
  drnList = drnStatus = drnToggle = drnDetail = drnWatchBtn = drnLogBtn = nullptr;
  deskIconState = -1;
}

void setText(lv_obj_t *lbl, const char *txt) {
  if (lbl && strcmp(lv_label_get_text(lbl), txt) != 0) lv_label_set_text(lbl, txt);
}

// Only touch the HIDDEN flag when it actually changes (adding it always invalidates)
void setHidden(lv_obj_t *o, bool hide) {
  if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == hide) return;
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// LVGL objects come from the same heap as the TLS session. Building a new
// screen while the old one still exists, during an HTTPS download, ran out of
// memory and crashed in lv_list_add_btn. So wait (max 20 s) for it to finish.
void waitForTls() {
  if (!weather::tlsActive()) return;
  lv_obj_t *note = lv_label_create(lv_layer_top());
  if (note) {
    lv_label_set_text(note, "Updating weather alerts...");
    lv_obj_set_style_bg_opa(note, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(note, lv_color_black(), 0);
    lv_obj_set_style_text_color(note, lv_color_white(), 0);
    lv_obj_set_style_pad_all(note, 6, 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_refr_now(nullptr);
  }
  uint32_t t0 = millis();
  while (weather::tlsActive() && millis() - t0 < 20000) delay(20);
  if (note) lv_obj_del(note);
}

lv_obj_t *newScreen() {
  waitForTls();
  if (calTimer) {
    lv_timer_del(calTimer);
    calTimer = nullptr;
  }
  resetPtrs();
  lv_obj_t *s = lv_obj_create(nullptr);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

// Loads the screen and deletes the previous one once the switch completes.
void load(lv_obj_t *scr, Screen s) {
  cur = s;
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

lv_obj_t *flatBtn(lv_obj_t *parent, const char *txt, int w, int h) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

// rightReserve: space kept free on the right for a header button
lv_obj_t *makeHeader(lv_obj_t *scr, const char *title, lv_event_cb_t backCb,
                     int rightReserve = 0) {
  lv_obj_t *h = lv_obj_create(scr);
  lv_obj_set_size(h, W(), HEADER_H);
  lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(h, 0, 0);
  lv_obj_set_style_border_width(h, 0, 0);
  lv_obj_set_style_pad_all(h, 4, 0);
  lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);

  int titleX = 6;
  if (backCb) {
    lv_obj_t *b = flatBtn(h, LV_SYMBOL_LEFT, 48, 32);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(b, backCb, LV_EVENT_CLICKED, nullptr);
    titleX = 58;
  }
  lv_obj_t *t = lv_label_create(h);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  // One line only: a fixed height makes LONG_DOT end a long title with "..."
  // instead of wrapping it out of the header
  lv_obj_set_size(t, W() - titleX - 12 - rightReserve,
                  lv_font_get_line_height(&lv_font_montserrat_20));
  lv_label_set_text(t, title);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, titleX, 0);
  return h;
}

void errorBox(const char *title, const char *msg) {
  lv_obj_t *m = lv_msgbox_create(nullptr, title, msg, nullptr, true);  // modal, on top layer
  lv_obj_set_width(m, W() - 40);
  lv_obj_center(m);
}

// Content area from `top` to the bottom of the screen, laid out as a column:
// info lines take the height their (wrapped) text needs and the list fills
// what's left. A long status line therefore pushes the list down instead of
// being hidden underneath it, which matters on the 240 px-wide portrait screen.
lv_obj_t *makeBody(lv_obj_t *scr, int top) {
  lv_obj_t *b = lv_obj_create(scr);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, W(), H() - top);
  lv_obj_align(b, LV_ALIGN_TOP_LEFT, 0, top);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_top(b, 4, 0);
  lv_obj_set_style_pad_row(b, 4, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// A full-width info line that wraps instead of being cut off
lv_obj_t *bodyLabel(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_pad_hor(l, 6, 0);
  lv_label_set_text(l, "");
  return l;
}

// The list that takes the remaining height of a body
lv_obj_t *bodyList(lv_obj_t *body) {
  lv_obj_t *list = lv_list_create(body);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  return list;
}

// Radio tools can't run while the drone scanner owns Wi-Fi and Bluetooth
bool radioBusy() {
  if (!drone::running()) return false;
  errorBox("Drone scan running", "Stop the drone Remote ID scanner first. It is using the "
                                 "Wi-Fi and Bluetooth radios.");
  return true;
}

TempW makeTemp(lv_obj_t *parent, const lv_font_t *font, int ring) {
  TempW t;
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(box, ring / 3 + 1, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);

  t.num = lv_label_create(box);
  lv_obj_set_style_text_font(t.num, font, 0);
  lv_label_set_text(t.num, "--");

  lv_obj_t *deg = lv_obj_create(box);
  lv_obj_remove_style_all(deg);
  lv_obj_set_size(deg, ring, ring);
  lv_obj_set_style_radius(deg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(deg, ring >= 10 ? 3 : 2, 0);
  lv_obj_set_style_border_color(deg, theme::text(), 0);
  lv_obj_set_style_border_opa(deg, LV_OPA_COVER, 0);
  lv_obj_set_style_translate_y(deg, ring / 2, 0);
  lv_obj_clear_flag(deg, LV_OBJ_FLAG_CLICKABLE);

  t.unit = lv_label_create(box);
  lv_obj_set_style_text_font(t.unit, font, 0);
  lv_label_set_text(t.unit, weather::useF() ? "F" : "C");
  return t;
}

void setTemp(TempW &t, bool valid, float c) {
  if (!t.num) return;
  char buf[8];
  if (valid) snprintf(buf, sizeof(buf), "%d", weather::temp(c));
  else snprintf(buf, sizeof(buf), "--");
  setText(t.num, buf);
  setText(t.unit, weather::useF() ? "F" : "C");
}

// Set an image only when it changes (setting it always redraws)
void setImg(lv_obj_t *img, const lv_img_dsc_t *src) {
  if (img && lv_img_get_src(img) != src) lv_img_set_src(img, src);
}

// ---------- Desktop ----------

// Places the widget row under the (possibly two-row) status bar and the Menu
// button under the widgets, keeping the Menu at its usual spot when there's room
void layoutDesktop() {
  if (!deskBar || !deskRow || !deskMenu || !deskWave || !deskAlerts || !deskStock) return;
  lv_obj_update_layout(deskBar);
  int barH = lv_obj_get_height(deskBar);
  bool none = lv_obj_has_flag(deskWave, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(deskAlerts, LV_OBJ_FLAG_HIDDEN) &&
              lv_obj_has_flag(deskStock, LV_OBJ_FLAG_HIDDEN);
  int rowH = none ? 0 : lv_obj_get_height(deskRow);
  int key = barH * 1000 + rowH;
  if (key == deskLayoutKey) return;
  deskLayoutKey = key;
  const bool portrait = H() > W();
  int rowY = barH + (portrait ? 10 : 5);
  lv_obj_align(deskRow, LV_ALIGN_TOP_MID, 0, rowY);
  const int fullH = 70, minH = 40;
  int usual = (H() - fullH) / 2 + (portrait ? 42 : 22);
  int below = (rowH ? rowY + rowH : barH) + 10;
  int y = std::max(usual, below);
  // Keep clear of the Wi-Fi line at the bottom: shrink the Menu button if needed
  int room = H() - 30 - y;
  lv_obj_set_height(deskMenu, std::max(minH, std::min(fullH, room)));
  lv_obj_align(deskMenu, LV_ALIGN_TOP_MID, 0, y);
}

void refreshDesktop() {
  if (!deskWifiIcon) return;
  char buf[96];
  int state = (int)net::state();

  switch (net::state()) {
    case net::State::Connected:
      snprintf(buf, sizeof(buf), "%s  |  %s", net::ssid().c_str(), net::ip().c_str());
      break;
    case net::State::Connecting:
      snprintf(buf, sizeof(buf), "Connecting to %s...", net::ssid().c_str());
      break;
    case net::State::Lost:
      snprintf(buf, sizeof(buf), "Reconnecting to %s...", net::ssid().c_str());
      break;
    default:
      snprintf(buf, sizeof(buf), drone::running() ? "Drone scan running - Wi-Fi paused"
                                                  : "Wi-Fi not connected");
      break;
  }
  setText(deskWifiLbl, buf);

  char clk[16];
  timesvc::formatTime(clk, sizeof(clk), timesvc::barAmPm());
  setText(deskClock, clk);

  // Drone Watch icon: steady while watching, flashing blue/red while a drone
  // has been heard within the last watch cycle
  bool watching = drone::mode() == drone::Mode::Watch;
  setHidden(deskDrone, !watching);
  if (watching && deskDrone) {
    int want = drone::watchDroneNearby() ? 1 + (int)((millis() / 400) % 2) : 0;
    if (want != deskDroneState) {
      deskDroneState = want;
      lv_obj_set_style_img_recolor(deskDrone,
                                   want == 1   ? lv_palette_main(LV_PALETTE_BLUE)
                                   : want == 2 ? lv_palette_main(LV_PALETTE_RED)
                                               : theme::text(),
                                   0);
    }
  }

  // Wave height (status bar and/or desktop widget) and the alert count widget
  bool barWave = weather::showBuoyBar(), deskWaveOn = weather::showBuoyDesk();
  setHidden(deskBarWave, !barWave);
  setHidden(deskWave, !deskWaveOn);
  // Rebuild the wave texts only when the buoy data, station or unit changed:
  // doing it every 300 ms churned the heap with String copies
  int waveKey = (int)(weather::buoyVersion() * 2 + (weather::useF() ? 1 : 0));
  if ((barWave || deskWaveOn) && waveKey != deskWaveKey) {
    deskWaveKey = waveKey;
    String wt = weather::waveText(weather::buoy().waveM);
    if (deskWaveOn) {
      setText(deskWaveLbl, wt.c_str());
      String id = weather::buoyStation();
      String nm = weather::buoyName(id);
      setText(deskWaveName, (nm.length() ? nm : "Station " + id).c_str());
    }
    if (barWave) {
      if (W() < 300) wt.replace("' ", "'");  // tighter in portrait
      setText(deskBarWaveLbl, wt.c_str());
    }
  }
  // Stock readouts (rebuilt only when the quote, ticker or switches change)
  bool barStock = weather::showStockBar(), deskStockOn = weather::showStockDesk();
  setHidden(deskBarStock, !barStock);
  setHidden(deskStock, !deskStockOn);
  int stockKey = (int)weather::stockVersion();
  if ((barStock || deskStockOn) && stockKey != deskStockKey) {
    deskStockKey = stockKey;
    weather::Stock s = weather::stock();
    String sym = s.valid ? s.symbol : weather::stockSymbol();
    String price = fmtPrice(s), pct = fmtPct(s);
    lv_color_t pc = pctColor(s);
    if (deskStockOn) {
      setText(deskStockSym, sym.c_str());
      setText(deskStockPrice, price.c_str());
      setText(deskStockPct, pct.c_str());
      lv_obj_set_style_text_color(deskStockPct, pc, 0);
    }
    if (barStock) {
      String t = sym + " " + price;
      setText(deskBarStockLbl, t.c_str());
      setText(deskBarStockPct, pct.c_str());
      lv_obj_set_style_text_color(deskBarStockPct, pc, 0);
    }
  }

  if (deskAlerts) {
    bool on = weather::showAlertDesk() && weather::configured();
    setHidden(deskAlerts, !on);
    if (on) {
      size_t n = weather::alertCount(), live = weather::unsnoozedAlertCount();
      char t[24];
      if (n) snprintf(t, sizeof(t), "%u alert%s", (unsigned)n, n == 1 ? "" : "s");
      else snprintf(t, sizeof(t), "No alerts");
      setText(deskAlertsLbl, t);
      int col = live ? 1 : 0;
      if (col != deskAlertsColor) {
        deskAlertsColor = col;
        lv_obj_set_style_text_color(deskAlerts, live ? lv_palette_main(LV_PALETTE_RED) : theme::text(), 0);
      }
    }
  }

  weather::Data wx = weather::data();
  bool wxAlert = weather::unsnoozedAlertCount() > 0;
  setHidden(deskWx, !weather::configured());
  if (wx.valid) setImg(deskWxImg, weather::icon(wx, false));
  // An unsnoozed NWS alert shows the icon even if it's turned off, and flashes it
  setHidden(deskWxImg, !wx.valid || (!weather::showIcon() && !wxAlert));
  setTemp(deskTemp, wx.valid, wx.tempC);
  int blink = wxAlert ? (int)((millis() / 500) % 2) : 2;  // 0/1 flashing, 2 steady
  if (deskWx && blink != deskWxBlink) {
    deskWxBlink = blink;
    lv_obj_set_style_opa(deskWx, blink == 0 ? LV_OPA_20 : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(deskWx, wxAlert ? lv_palette_main(LV_PALETTE_RED) : theme::text(), 0);
  }

  layoutDesktop();  // the bar may have wrapped or a widget appeared/disappeared

  // Restyle only on change: setting a style invalidates (redraws) the widget
  if (state == deskIconState) return;
  deskIconState = state;
  lv_color_t color;
  lv_opa_t opa = LV_OPA_COVER;
  switch (net::state()) {
    case net::State::Connected: color = lv_palette_main(LV_PALETTE_BLUE); break;
    case net::State::Connecting:
    case net::State::Lost: color = lv_palette_main(LV_PALETTE_AMBER); break;
    default:
      color = lv_palette_main(LV_PALETTE_GREY);
      opa = LV_OPA_40;
      break;
  }
  lv_obj_set_style_text_color(deskWifiIcon, color, 0);
  lv_obj_set_style_text_opa(deskWifiIcon, opa, 0);
}

void showDesktop() {
  lv_obj_t *scr = newScreen();

  // Status bar: [CYD] .......... [temp][icon] [clock] [wifi], right-aligned row.
  // Portrait is only 240 px wide, so it gets the smaller font.
  bool narrow = W() < 300;
  const lv_font_t *barFont = narrow ? &lv_font_montserrat_14 : &lv_font_montserrat_20;

  // The bar wraps onto a second row when its items don't fit across, and
  // grows just enough to hold it; layoutDesktop() moves the rest down to match
  lv_obj_t *bar = lv_obj_create(scr);
  deskBar = bar;
  lv_obj_set_size(bar, W(), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(bar, 36, 0);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 6, 0);
  lv_obj_set_style_pad_ver(bar, 2, 0);
  lv_obj_set_style_pad_column(bar, narrow ? 4 : 10, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Left part: everything except the clock and Wi-Fi. It wraps onto a second
  // line when it runs out of room, so the clock and Wi-Fi always stay on top.
  lv_obj_t *ext = lv_obj_create(bar);
  lv_obj_remove_style_all(ext);
  lv_obj_set_height(ext, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(ext, 1);
  lv_obj_set_flex_flow(ext, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(ext, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ext, narrow ? 4 : 10, 0);
  lv_obj_set_style_pad_row(ext, 2, 0);
  lv_obj_clear_flag(ext, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ext, LV_OBJ_FLAG_CLICKABLE);
  // Zero-width strut: makes the first line as tall as the Wi-Fi button (32 px)
  // so its items line up with the clock
  lv_obj_t *strut = lv_obj_create(ext);
  lv_obj_remove_style_all(strut);
  lv_obj_set_size(strut, 0, 32);
  lv_obj_clear_flag(strut, LV_OBJ_FLAG_CLICKABLE);

  if (!narrow) {
    lv_obj_t *name = lv_label_create(ext);
    lv_label_set_text(name, "CYD");
    lv_obj_set_flex_grow(name, 1);  // pushes everything else to the right
  }

  // Weather: temperature + forecast icon; tap for the weather screen
  deskWx = lv_obj_create(ext);
  lv_obj_remove_style_all(deskWx);
  lv_obj_set_size(deskWx, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(deskWx, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(deskWx, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(deskWx, 4, 0);
  lv_obj_add_flag(deskWx, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(deskWx, 6);
  lv_obj_add_event_cb(deskWx, [](lv_event_t *) { wxBack = Screen::Desktop; showWeather(); },
                      LV_EVENT_CLICKED, nullptr);
  deskTemp = makeTemp(deskWx, barFont, narrow ? 5 : 6);  // temperature first...
  deskWxImg = lv_img_create(deskWx);                       // ...then the icon
  lv_obj_clear_flag(deskWxImg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(deskWxImg, LV_OBJ_FLAG_HIDDEN);

  // Buoy wave height: wave icon + number; tap for the buoy page
  deskBarWave = lv_obj_create(ext);
  lv_obj_remove_style_all(deskBarWave);
  lv_obj_set_size(deskBarWave, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(deskBarWave, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(deskBarWave, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(deskBarWave, 3, 0);
  lv_obj_add_flag(deskBarWave, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(deskBarWave, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_ext_click_area(deskBarWave, 6);
  lv_obj_add_event_cb(
      deskBarWave,
      [](lv_event_t *) {
        buoyBack = Screen::Desktop;
        showBuoy();
      },
      LV_EVENT_CLICKED, nullptr);
  {
    lv_obj_t *wi = lv_img_create(deskBarWave);
    lv_img_set_src(wi, narrow ? &ui_wave_s : &ui_wave);
    lv_obj_set_style_img_recolor_opa(wi, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(wi, theme::accent(), 0);
    lv_obj_clear_flag(wi, LV_OBJ_FLAG_CLICKABLE);
    deskBarWaveLbl = lv_label_create(deskBarWave);
    lv_obj_set_style_text_font(deskBarWaveLbl, barFont, 0);
    lv_label_set_text(deskBarWaveLbl, "--");
    lv_obj_move_to_index(deskBarWaveLbl, 0);  // number first, then the icon
  }
  // Order: waves, then temperature + weather icon, then drone/clock/Wi-Fi
  lv_obj_move_to_index(deskBarWave, lv_obj_get_index(deskWx));

  // Stock: "SPY $590.12 +0.84%", left of the waves; tap for the stock page
  deskBarStock = lv_obj_create(ext);
  lv_obj_remove_style_all(deskBarStock);
  lv_obj_set_size(deskBarStock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(deskBarStock, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(deskBarStock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(deskBarStock, 3, 0);
  lv_obj_add_flag(deskBarStock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(deskBarStock, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_ext_click_area(deskBarStock, 6);
  lv_obj_add_event_cb(
      deskBarStock,
      [](lv_event_t *) {
        stockBack = Screen::Desktop;
        showStock();
      },
      LV_EVENT_CLICKED, nullptr);
  deskBarStockLbl = lv_label_create(deskBarStock);
  lv_obj_set_style_text_font(deskBarStockLbl, barFont, 0);
  lv_label_set_text(deskBarStockLbl, "");
  deskBarStockPct = lv_label_create(deskBarStock);
  lv_obj_set_style_text_font(deskBarStockPct, barFont, 0);
  lv_label_set_text(deskBarStockPct, "");
  lv_obj_move_to_index(deskBarStock, lv_obj_get_index(deskBarWave));

  // Drone Watch indicator: only shown while Watch runs; tap for Drone RID
  deskDrone = lv_img_create(ext);
  lv_img_set_src(deskDrone, &ui_drone);
  lv_obj_add_flag(deskDrone, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(deskDrone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(deskDrone, 8);
  lv_obj_set_style_img_recolor_opa(deskDrone, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(deskDrone, theme::text(), 0);
  lv_obj_add_event_cb(deskDrone, [](lv_event_t *) { showDrones(); }, LV_EVENT_CLICKED, nullptr);

  // Clock; tap it for Date & time settings
  deskClock = lv_label_create(bar);  // stays on the top line
  lv_obj_set_style_text_font(deskClock, barFont, 0);
  lv_label_set_text(deskClock, "--:--");
  // Centered on the 32 px top line (the bar aligns its children to the top)
  lv_obj_set_style_translate_y(deskClock, (32 - lv_font_get_line_height(barFont)) / 2, 0);
  lv_obj_add_flag(deskClock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(deskClock, 8);
  lv_obj_add_event_cb(deskClock, [](lv_event_t *) { showDateTime(); }, LV_EVENT_CLICKED,
                      nullptr);

  // Wi-Fi icon: a transparent button so it's an easy stylus target
  lv_obj_t *wb = lv_btn_create(bar);
  lv_obj_set_size(wb, narrow ? 40 : 46, 32);
  lv_obj_set_style_bg_opa(wb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(wb, 0, 0);
  lv_obj_set_style_bg_opa(wb, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_add_event_cb(
      wb,
      [](lv_event_t *) {
        if (radioBusy()) return;
        wifiBack = Screen::Desktop;
        showWifi();
      },
      LV_EVENT_CLICKED, nullptr);
  deskWifiIcon = lv_label_create(wb);
  lv_obj_set_style_text_font(deskWifiIcon, &lv_font_montserrat_20, 0);
  lv_label_set_text(deskWifiIcon, LV_SYMBOL_WIFI);
  lv_obj_center(deskWifiIcon);

  // Menu button
  lv_obj_t *mb = flatBtn(scr, LV_SYMBOL_LIST "  Menu", 170, 70);
  deskMenu = mb;
  lv_obj_set_style_text_font(lv_obj_get_child(mb, 0), &lv_font_montserrat_20, 0);
  // Portrait stacks the two desktop widgets, so the menu sits lower there
  const bool portrait = H() > W();
  lv_obj_align(mb, LV_ALIGN_CENTER, 0, portrait ? 42 : 22);
  lv_obj_add_event_cb(mb, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);

  // Widgets between the status bar and the menu: wave height and alert count
  lv_obj_t *wrow = lv_obj_create(scr);
  deskRow = wrow;
  lv_obj_remove_style_all(wrow);
  lv_obj_set_size(wrow, W(), LV_SIZE_CONTENT);
  // Side by side in landscape (320 px fits both); stacked in portrait, where
  // 240 px is too narrow for two bubbles and they were cut off at the edges
  lv_obj_set_flex_flow(wrow, portrait ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(wrow, 12, 0);
  lv_obj_set_style_pad_row(wrow, 8, 0);
  lv_obj_clear_flag(wrow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(wrow, LV_ALIGN_TOP_MID, 0, portrait ? 46 : 41);

  // Each widget is a rounded "bubble" (themed panel) that reacts to a press
  auto widget = [&](lv_event_cb_t cb) {
    lv_obj_t *w = lv_obj_create(wrow);
    lv_obj_set_size(w, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(w, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(w, 16, 0);
    lv_obj_set_style_border_width(w, 2, 0);
    lv_obj_set_style_pad_hor(w, 10, 0);
    lv_obj_set_style_pad_ver(w, 6, 0);
    lv_obj_set_style_pad_column(w, 6, 0);
    lv_obj_set_style_border_color(w, theme::accent(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(w, theme::accent(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(w, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_text_font(w, &lv_font_montserrat_20, 0);
    lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_ext_click_area(w, 6);
    lv_obj_add_event_cb(w, cb, LV_EVENT_CLICKED, nullptr);
    return w;
  };
  // Small dimmed ">" at the end: "tap for more"
  auto chevron = [](lv_obj_t *w) {
    lv_obj_t *c = lv_label_create(w);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_opa(c, LV_OPA_60, 0);
    lv_label_set_text(c, LV_SYMBOL_RIGHT);
  };

  deskWave = widget([](lv_event_t *) {
    buoyBack = Screen::Desktop;
    showBuoy();
  });
  // Two lines: the buoy's friendly name, then wave icon + height + ">"
  lv_obj_set_flex_flow(deskWave, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(deskWave, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(deskWave, 0, 0);
  deskWaveName = lv_label_create(deskWave);
  lv_obj_set_style_text_font(deskWaveName, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_opa(deskWaveName, LV_OPA_80, 0);
  lv_label_set_long_mode(deskWaveName, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(deskWaveName, W() - 40, 0);  // long names end in "..."
  lv_obj_set_height(deskWaveName, lv_font_get_line_height(&lv_font_montserrat_14));
  lv_label_set_text(deskWaveName, "");
  lv_obj_t *wline = lv_obj_create(deskWave);
  lv_obj_remove_style_all(wline);
  lv_obj_set_size(wline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wline, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(wline, 6, 0);
  lv_obj_clear_flag(wline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(wline, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t *wimg = lv_img_create(wline);
  lv_img_set_src(wimg, &ui_wave);
  lv_obj_set_style_img_recolor_opa(wimg, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(wimg, theme::accent(), 0);
  lv_obj_clear_flag(wimg, LV_OBJ_FLAG_CLICKABLE);
  deskWaveLbl = lv_label_create(wline);
  lv_label_set_text(deskWaveLbl, "--");
  lv_obj_move_to_index(deskWaveLbl, 0);  // number first, then the icon
  chevron(wline);

  deskAlerts = widget([](lv_event_t *) {
    wxaBack = Screen::Desktop;
    showWxAlerts();
  });
  lv_obj_t *aic = lv_label_create(deskAlerts);
  lv_label_set_text(aic, LV_SYMBOL_WARNING);
  deskAlertsLbl = lv_label_create(deskAlerts);
  lv_label_set_text(deskAlertsLbl, "");
  lv_obj_move_to_index(deskAlertsLbl, 0);  // "5 alerts" first, then the icon
  chevron(deskAlerts);

  // Stock bubble, one line: ticker (small), price, day change, ">"
  deskStock = widget([](lv_event_t *) {
    stockBack = Screen::Desktop;
    showStock();
  });
  deskStockSym = lv_label_create(deskStock);
  lv_obj_set_style_text_font(deskStockSym, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_opa(deskStockSym, LV_OPA_80, 0);
  lv_label_set_text(deskStockSym, "");
  deskStockPrice = lv_label_create(deskStock);
  lv_label_set_text(deskStockPrice, "--");
  deskStockPct = lv_label_create(deskStock);
  lv_label_set_text(deskStockPct, "");
  chevron(deskStock);

  deskWifiLbl = lv_label_create(scr);
  lv_label_set_long_mode(deskWifiLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(deskWifiLbl, W() - 16);
  lv_obj_set_style_text_align(deskWifiLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(deskWifiLbl, "");
  lv_obj_align(deskWifiLbl, LV_ALIGN_BOTTOM_MID, 0, -10);

  load(scr, Screen::Desktop);
  refreshDesktop();
}

// ---------- Main menu ----------

void addMenuItems(lv_obj_t *list) {
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, "Network settings");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (radioBusy()) return;
        wifiBack = Screen::Menu;
        showWifi();
      },
      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_GPS, "Drone Remote ID");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showDrones(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "Bluetooth scanner");
  lv_obj_add_event_cb(b, [](lv_event_t *) { if (!radioBusy()) showBle(); }, LV_EVENT_CLICKED,
                      nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_WARNING, "Tracker detector");
  lv_obj_add_event_cb(b, [](lv_event_t *) { if (!radioBusy()) showTrackers(); },
                      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_BARS, "Wi-Fi analyzer");
  lv_obj_add_event_cb(b, [](lv_event_t *) { if (!radioBusy()) showWifiAnalyzer(); },
                      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_TINT, "Weather");
  lv_obj_add_event_cb(b, [](lv_event_t *) { wxBack = Screen::Menu; showWeather(); },
                      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_UP, "Stocks");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        stockBack = Screen::Menu;
        showStock();
      },
      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_BELL, "Date & time");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showDateTime(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Display & touch");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showDisplay(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "About this device");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showAbout(); }, LV_EVENT_CLICKED, nullptr);

}

void showMenu() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Menu", [](lv_event_t *) { showDesktop(); });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
  addMenuItems(list);

  load(scr, Screen::Menu);
}

// ---------- Display & touch ----------

void onRotationClicked(lv_event_t *e) {
  uint8_t r = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (r == display::rotation()) return;
  display::setRotation(r);
  showDisplay();  // rebuild for the new dimensions
}

void showDisplay() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Display & touch", [](lv_event_t *) { showMenu(); });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_list_add_text(list, "Theme");
  for (uint8_t i = 0; i < theme::COUNT; i++) {
    bool sel = i == theme::current();
    lv_obj_t *tb = lv_list_add_btn(list, sel ? LV_SYMBOL_OK : nullptr, theme::name((theme::Id)i));
    lv_obj_add_event_cb(
        tb,
        [](lv_event_t *e) {
          theme::Id id = (theme::Id)(intptr_t)lv_event_get_user_data(e);
          if (id == theme::current()) return;
          theme::set(id);
          showDisplay();  // rebuild so every widget picks up the new styles
        },
        LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  lv_list_add_text(list, "Brightness");
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 48);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text_fmt(val, "%d%%", display::brightness());
  lv_obj_set_width(val, 44);
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);

  lv_obj_t *sl = lv_slider_create(row);
  lv_obj_set_width(sl, W() - 90);
  lv_obj_align(sl, LV_ALIGN_LEFT_MID, 18, 0);
  lv_slider_set_range(sl, display::MIN_BRIGHTNESS, 100);
  lv_slider_set_value(sl, display::brightness(), LV_ANIM_OFF);
  lv_obj_set_style_pad_all(sl, 6, LV_PART_KNOB);  // bigger knob for the stylus
  lv_obj_add_event_cb(
      sl,
      [](lv_event_t *e) {
        lv_obj_t *s = lv_event_get_target(e);
        lv_obj_t *v = (lv_obj_t *)lv_event_get_user_data(e);
        int pct = lv_slider_get_value(s);
        bool done = lv_event_get_code(e) == LV_EVENT_RELEASED;
        display::setBrightness(pct, done);  // write flash only when released
        lv_label_set_text_fmt(v, "%d%%", pct);
      },
      LV_EVENT_VALUE_CHANGED, val);
  lv_obj_add_event_cb(
      sl,
      [](lv_event_t *e) {
        display::setBrightness(lv_slider_get_value(lv_event_get_target(e)), true);
      },
      LV_EVENT_RELEASED, nullptr);

  lv_list_add_text(list, "Orientation");
  struct Opt { uint8_t rot; const char *name; };
  static const Opt opts[] = {
      {display::LANDSCAPE, "Landscape"},
      {display::PORTRAIT, "Portrait"},
      {display::LANDSCAPE_FLIP, "Landscape (flipped)"},
      {display::PORTRAIT_FLIP, "Portrait (flipped)"},
  };
  for (const auto &o : opts) {
    bool sel = o.rot == display::rotation();
    lv_obj_t *b = lv_list_add_btn(list, sel ? LV_SYMBOL_OK : nullptr, o.name);
    if (sel) lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_add_event_cb(b, onRotationClicked, LV_EVENT_CLICKED, (void *)(intptr_t)o.rot);
  }

  lv_list_add_text(list, "Colors");
  lv_obj_t *ib = lv_list_add_btn(list, display::invertColors() ? LV_SYMBOL_OK : nullptr,
                                 "Invert colors (panel fix)");
  lv_obj_add_event_cb(
      ib,
      [](lv_event_t *) {
        display::setInvertColors(!display::invertColors());
        showDisplay();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cb = lv_list_add_btn(list, display::swapRedBlue() ? LV_SYMBOL_OK : nullptr,
                                 "Swap red/blue");
  lv_obj_add_event_cb(
      cb,
      [](lv_event_t *) {
        display::setSwapRedBlue(!display::swapRedBlue());
        showDisplay();
      },
      LV_EVENT_CLICKED, nullptr);

  lv_list_add_text(list, "Touch");
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EDIT, "Calibrate touch screen");
  lv_obj_add_event_cb(
      b, [](lv_event_t *) { calReturn = Screen::Display; showCalibration(false); },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::Display);
}

// ---------- Touch calibration ----------

void calTarget(int i, int16_t &x, int16_t &y) {
  int w = W(), h = H();
  switch (i) {
    case 0: x = CAL_MARGIN;         y = CAL_MARGIN;         break;
    case 1: x = w - 1 - CAL_MARGIN; y = CAL_MARGIN;         break;
    case 2: x = w - 1 - CAL_MARGIN; y = h - 1 - CAL_MARGIN; break;
    default: x = CAL_MARGIN;        y = h - 1 - CAL_MARGIN; break;
  }
}

void calPlaceTarget() {
  int16_t x, y;
  calTarget(calIdx, x, y);
  lv_obj_set_pos(calH, x - 15, y - 1);
  lv_obj_set_pos(calV, x - 1, y - 15);
  lv_obj_set_pos(calRing, x - 9, y - 9);
  lv_label_set_text_fmt(calMsg, "Tap the center of the cross\nwith the stylus  (%d/%d)",
                        calIdx + 1, CAL_POINTS);
}

void calRestart(const char *why) {
  calIdx = 0;
  calWaitRelease = true;
  calDownTicks = calUpTicks = 0;
  calSumX = calSumY = 0;
  calPlaceTarget();
  if (why) lv_label_set_text_fmt(calMsg, "%s\nTap the cross again  (1/%d)", why, CAL_POINTS);
}

void calTick(lv_timer_t *) {
  int16_t rx, ry;
  bool down = display::readRaw(rx, ry);

  if (calWaitRelease) {  // ignore a stylus that's already down
    if (!down) calWaitRelease = false;
    return;
  }

  if (down) {
    calUpTicks = 0;
    if (++calDownTicks > CAL_SETTLE_SAMPLES) {
      calSumX += rx;
      calSumY += ry;
    }
    return;
  }

  // Debounce release: resistive panels can drop out for a sample mid-press
  if (calDownTicks == 0 || ++calUpTicks < 2) return;

  int used = calDownTicks - CAL_SETTLE_SAMPLES;
  int32_t sumX = calSumX, sumY = calSumY;
  calDownTicks = calUpTicks = 0;
  calSumX = calSumY = 0;
  if (used < 3) return;  // too brief to trust; wait for another tap

  int16_t sx, sy;
  calTarget(calIdx, sx, sy);
  calPts[calIdx] = {sx, sy, (int16_t)(sumX / used), (int16_t)(sumY / used)};
  Serial.printf("[cal] point %d screen=(%d,%d) raw=(%d,%d)\n", calIdx, sx, sy,
                calPts[calIdx].rx, calPts[calIdx].ry);

  if (++calIdx < CAL_POINTS) {
    calPlaceTarget();
    return;
  }

  float err = 0;
  if (!display::calibrate(calPts, CAL_POINTS, &err)) {
    char why[64];
    snprintf(why, sizeof(why), "Taps didn't line up (off by %d px).", (int)err);
    calRestart(why);
    return;
  }

  // Success: stop sampling, confirm, then leave
  lv_timer_del(calTimer);
  calTimer = nullptr;
  setHidden(calH, true);
  setHidden(calV, true);
  setHidden(calRing, true);
  char done[64];  // LVGL's printf has no float support, so format here
  snprintf(done, sizeof(done), LV_SYMBOL_OK " Calibration saved\n(accuracy %.1f px)", err);
  lv_label_set_text(calMsg, done);
  lv_timer_t *t = lv_timer_create(
      [](lv_timer_t *) {
        if (calReturn == Screen::Display) showDisplay();
        else showDesktop();
      },
      1200, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

lv_obj_t *calBar(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

void showCalibration(bool firstRun) {
  lv_obj_t *scr = newScreen();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_label_set_text(title, firstRun ? "Touch setup" : "Touch calibration");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

  calMsg = lv_label_create(scr);
  lv_obj_set_style_text_align(calMsg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(calMsg, W() - 70);
  lv_label_set_long_mode(calMsg, LV_LABEL_LONG_WRAP);
  lv_obj_align(calMsg, LV_ALIGN_CENTER, 0, 10);

  calRing = lv_obj_create(scr);
  lv_obj_remove_style_all(calRing);
  lv_obj_set_size(calRing, 18, 18);
  lv_obj_set_style_radius(calRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(calRing, 2, 0);
  lv_obj_set_style_border_color(calRing, lv_color_white(), 0);
  lv_obj_clear_flag(calRing, LV_OBJ_FLAG_CLICKABLE);
  calH = calBar(scr, 30, 2);
  calV = calBar(scr, 2, 30);

  load(scr, Screen::Calibrate);
  calRestart(nullptr);
  calTimer = lv_timer_create(calTick, 15, nullptr);
}

// ---------- Bluetooth scanner ----------

String bleTitle(const ble::Device &d) {
  if (d.name.length()) return d.name;
  if (d.tracker != ble::Tracker::None) return ble::trackerName(d.tracker);
  const char *v = d.hasMfg ? ble::vendorName(d.mfgId) : nullptr;
  if (v) return String(v) + " device";
  return d.addr;
}

void bleUpdateStatus() {
  if (!bleStatus) return;
  char buf[80];
  size_t n = ble::count();
  size_t ign = ble::ignoredCount();
  if (ble::mode() == ble::Mode::Scan) {
    snprintf(buf, sizeof(buf), "Scanning... %lus  -  %u found",
             (unsigned long)((ble::scanRemainingMs() + 999) / 1000), (unsigned)n);
  } else if (!ble::everScanned()) {
    snprintf(buf, sizeof(buf), "Tap Start to scan (%lus)", (unsigned long)ble::SCAN_SECS);
  } else {
    snprintf(buf, sizeof(buf), "%u found  -  %u ignored", (unsigned)n, (unsigned)ign);
  }
  setText(bleStatus, buf);
  setText(lv_obj_get_child(bleToggle, 0), ble::mode() == ble::Mode::Scan ? "Stop" : "Start");
}

void onBleRowClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= bleRowAddrs.size()) return;
  bleDetailAddr = bleRowAddrs[i];
  showBleDetail();
}

void bleFill() {
  if (!bleList) return;
  // Don't rebuild under the stylus: it would cancel a tap or scroll in progress
  lv_indev_t *in = lv_indev_get_next(nullptr);
  if (in && in->proc.state == LV_INDEV_STATE_PRESSED) return;
  bleLastFill = millis();

  lv_coord_t y = lv_obj_get_scroll_y(bleList);
  lv_obj_clean(bleList);  // free the old rows before allocating anything
  bleRowAddrs.clear();

  size_t total = 0;
  auto devs = ble::snapshot(BLE_MAX_ROWS, &total);
  bleUpdateStatus();

  if (devs.empty()) {
    const char *msg = ble::mode() == ble::Mode::Scan ? "Listening for devices..."
                      : ble::everScanned()           ? "No devices found"
                                                     : "Nothing scanned yet";
    lv_list_add_text(bleList, msg);
  }
  for (size_t i = 0; i < devs.size(); i++) {
    // LVGL crashes if an allocation fails, so stop while there's headroom
    if (ESP.getFreeHeap() < BLE_MIN_FREE_HEAP) {
      lv_list_add_text(bleList, "(more devices hidden: low memory)");
      Serial.printf("[ui] BLE list truncated at %u rows, free heap %lu\n", (unsigned)i,
                    (unsigned long)ESP.getFreeHeap());
      break;
    }
    const auto &d = devs[i];
    lv_obj_t *b = lv_list_add_btn(bleList, LV_SYMBOL_BLUETOOTH, bleTitle(d).c_str());
    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text_fmt(r, "%d dBm", d.rssi);
    lv_obj_set_style_text_opa(r, LV_OPA_60, 0);
    bleRowAddrs.push_back(d.addr);
    lv_obj_add_event_cb(b, onBleRowClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  size_t ign = ble::ignoredCount();
  if (ign) {
    char t[40];
    snprintf(t, sizeof(t), "Ignored devices (%u)", (unsigned)ign);
    lv_obj_t *b = lv_list_add_btn(bleList, LV_SYMBOL_EYE_CLOSE, t);
    lv_obj_set_style_text_opa(b, LV_OPA_70, 0);
    lv_obj_add_event_cb(b, [](lv_event_t *) { showBleIgnored(); }, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_scroll_to_y(bleList, y, LV_ANIM_OFF);
}

void pollBle() {
  ble::Mode m = ble::mode();
  bleUpdateStatus();
  // Refresh rows live while scanning, plus once when the scan finishes
  if ((m == ble::Mode::Scan && millis() - bleLastFill >= BLE_LIST_REFRESH_MS) ||
      (m != bleLastMode && m == ble::Mode::Idle))
    bleFill();
  bleLastMode = m;
}

void showBle() {
  lv_obj_t *scr = newScreen();
  const int btnW = 76;
  lv_obj_t *hdr = makeHeader(
      scr, "Bluetooth",
      [](lv_event_t *) {
        ble::stop();
        showMenu();
      },
      btnW);

  bleToggle = flatBtn(hdr, "Start", btnW, 32);
  lv_obj_align(bleToggle, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(
      bleToggle,
      [](lv_event_t *) {
        if (ble::mode() == ble::Mode::Scan) {
          ble::stop();
        } else {
          setText(bleStatus, "Starting Bluetooth...");
          lv_refr_now(nullptr);  // Bluetooth start blocks briefly; show the message first
          if (!ble::startScan()) setText(bleStatus, "Bluetooth failed to start");
        }
        bleFill();
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *body = makeBody(scr, HEADER_H);
  bleStatus = bodyLabel(body);
  bleList = bodyList(body);

  load(scr, Screen::Ble);
  bleLastMode = ble::mode();
  bleFill();
}

// --- device detail ---

void refreshBleDetail() {
  if (!bleDetail) return;
  ble::Device d;
  if (!ble::find(bleDetailAddr, d)) {
    setText(bleDetail, "Device no longer in the list.");
    return;
  }
  char mfg[40] = "-";
  if (d.hasMfg) {
    const char *v = ble::vendorName(d.mfgId);
    snprintf(mfg, sizeof(mfg), "%s (0x%04X)", v ? v : "unknown", d.mfgId);
  }
  char tx[16] = "-";
  if (d.hasTx) snprintf(tx, sizeof(tx), "%d dBm", d.tx);

  char buf[384];
  snprintf(buf, sizeof(buf),
           "Name: %s\n"
           "Address: %s (%s)\n"
           "RSSI: %d dBm\n"
           "TX power: %s\n"
           "Manufacturer: %s\n"
           "Service: %s\n"
           "Last seen: %lus ago",
           d.name.length() ? d.name.c_str() : "-", d.addr.c_str(),
           d.randomAddr ? "random" : "public", d.rssi, tx, mfg,
           d.service.length() ? d.service.c_str() : "-",
           (unsigned long)((millis() - d.lastSeen) / 1000));
  setText(bleDetail, buf);
}

void showBleDetail() {
  lv_obj_t *scr = newScreen();
  ble::Device d;
  String title = ble::find(bleDetailAddr, d) ? bleTitle(d) : bleDetailAddr;
  makeHeader(scr, title.c_str(), [](lv_event_t *) { showBle(); });

  const int btnRowH = 48;
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H - btnRowH);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, HEADER_H);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);

  bleDetail = lv_label_create(box);
  lv_label_set_long_mode(bleDetail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(bleDetail, LV_PCT(100));
  lv_label_set_text(bleDetail, "");

  int bw = (W() - 24) / 2;
  lv_obj_t *hunt = flatBtn(scr, LV_SYMBOL_GPS "  Hunt", bw, 38);
  lv_obj_align(hunt, LV_ALIGN_BOTTOM_LEFT, 8, -5);
  lv_obj_set_style_bg_color(hunt, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_add_event_cb(
      hunt,
      [](lv_event_t *) {
        ble::Device d;
        huntSrc = HuntSrc::Ble;
        huntBleAddr = bleDetailAddr;
        huntTitle = ble::find(bleDetailAddr, d) ? bleTitle(d) : bleDetailAddr;
        huntBack = Screen::BleDetail;
        showHunt();
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *ign = flatBtn(scr, LV_SYMBOL_EYE_CLOSE "  Ignore", bw, 38);
  lv_obj_align(ign, LV_ALIGN_BOTTOM_RIGHT, -8, -5);
  lv_obj_set_style_bg_color(ign, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(
      ign,
      [](lv_event_t *) {
        ble::Device d;
        String name = ble::find(bleDetailAddr, d) ? bleTitle(d) : String("");
        ble::ignore(bleDetailAddr, name);
        showBle();
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::BleDetail);
  refreshBleDetail();
}

// --- ignored devices ---

void onUnignoreClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  auto list = ble::ignoredList();
  if (i < list.size()) ble::unignore(list[i].addr);
  showBleIgnored();
}

void showBleIgnored() {
  lv_obj_t *scr = newScreen();
  const int btnW = 76;
  lv_obj_t *hdr = makeHeader(scr, "Ignored", [](lv_event_t *) { showBle(); }, btnW);

  lv_obj_t *clr = flatBtn(hdr, "Clear", btnW, 32);
  lv_obj_align(clr, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(clr, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(clr, [](lv_event_t *) { ble::clearIgnored(); showBleIgnored(); },
                      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  auto ign = ble::ignoredList();
  if (ign.empty()) {
    lv_list_add_text(list, "No ignored devices");
  } else {
    lv_list_add_text(list, "Tap a device to stop ignoring it");
  }
  for (size_t i = 0; i < ign.size(); i++) {
    String t = ign[i].name.length() && ign[i].name != ign[i].addr
                   ? ign[i].name + "  " + ign[i].addr
                   : ign[i].addr;
    lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EYE_CLOSE, t.c_str());
    lv_obj_add_event_cb(b, onUnignoreClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  load(scr, Screen::BleIgnored);
}

// --- hunt mode ---

// Restyle only on change; setting a style forces a redraw
void setHuntArrow(const char *sym, lv_palette_t pal) {
  if (strcmp(lv_label_get_text(huntArrowLbl), sym) == 0) return;
  lv_label_set_text(huntArrowLbl, sym);
  lv_obj_set_style_text_color(huntArrowLbl, lv_palette_main(pal), 0);
}

void refreshHunt() {
  if (!huntRssiLbl) return;
  HuntInfo h = huntSrc == HuntSrc::Ble    ? ble::hunt()
               : huntSrc == HuntSrc::Wifi ? net::hunt()
                                          : drone::hunt();
  uint32_t age = millis() - h.lastSeen;

  if (h.samples == 0 || age > HUNT_LOST_MS) {
    setText(huntStateLbl, h.samples == 0 ? "Searching..." : "Signal lost - keep moving");
    setHuntArrow(LV_SYMBOL_REFRESH, LV_PALETTE_GREY);
    if (h.samples == 0) setText(huntRssiLbl, "--");
    return;
  }
  if (h.samples == huntLastSamples) return;  // nothing new
  huntLastSamples = h.samples;

  // Trend: compare with the smoothed value a few samples ago
  huntHist.push_back(h.smooth);
  if (huntHist.size() > HUNT_TREND_SPAN + 1) huntHist.erase(huntHist.begin());
  float delta = huntHist.back() - huntHist.front();

  char buf[16];
  snprintf(buf, sizeof(buf), "%d", (int)lroundf(h.smooth));
  setText(huntRssiLbl, buf);

  const char *arrow, *state;
  lv_palette_t pal;
  if (huntHist.size() < 2) {
    arrow = LV_SYMBOL_MINUS; state = "Locked on"; pal = LV_PALETTE_GREY;
  } else if (delta >= HUNT_TREND_DB) {
    arrow = LV_SYMBOL_UP; state = "Getting closer"; pal = LV_PALETTE_GREEN;
  } else if (delta <= -HUNT_TREND_DB) {
    arrow = LV_SYMBOL_DOWN; state = "Moving away"; pal = LV_PALETTE_RED;
  } else {
    arrow = LV_SYMBOL_MINUS; state = "Steady"; pal = LV_PALETTE_GREY;
  }
  setHuntArrow(arrow, pal);
  char st[48];
  snprintf(st, sizeof(st), "%s  (raw %d dBm)", state, h.rssi);
  setText(huntStateLbl, st);

  lv_bar_set_value(huntBar, net::signalPercent((int32_t)lroundf(h.smooth)), LV_ANIM_ON);
  lv_chart_set_next_value(huntChart, huntSer, (lv_coord_t)lroundf(h.smooth));
}

void huntGoBack() {
  if (huntSrc == HuntSrc::Ble) ble::stop();
  else if (huntSrc == HuntSrc::Wifi) net::stopHunt();
  else drone::setHuntTarget("");  // the drone scanner itself keeps running
  switch (huntBack) {
    case Screen::DroneDetail: showDroneDetail(); break;
    case Screen::Trackers: showTrackers(); break;
    case Screen::WifiAnalyzer: showWifiAnalyzer(); break;
    default: showBleDetail(); break;
  }
}

void showHunt() {
  lv_obj_t *scr = newScreen();
  String hdrTitle = String(huntSrc == HuntSrc::Ble    ? LV_SYMBOL_BLUETOOTH " "
                           : huntSrc == HuntSrc::Wifi ? LV_SYMBOL_WIFI " "
                                                      : LV_SYMBOL_GPS " ") +
                    huntTitle;
  makeHeader(scr, hdrTitle.c_str(), [](lv_event_t *) { huntGoBack(); });

  lv_obj_t *col = lv_obj_create(scr);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, W(), H() - HEADER_H);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(col, 6, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

  // RSSI number + unit + trend arrow on one row
  lv_obj_t *row = lv_obj_create(col);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 10, 0);

  huntRssiLbl = lv_label_create(row);
  lv_obj_set_style_text_font(huntRssiLbl, &lv_font_montserrat_48, 0);
  lv_label_set_text(huntRssiLbl, "--");
  lv_obj_t *unit = lv_label_create(row);
  lv_label_set_text(unit, "dBm");
  lv_obj_set_style_pad_bottom(unit, 8, 0);
  huntArrowLbl = lv_label_create(row);
  lv_obj_set_style_text_font(huntArrowLbl, &lv_font_montserrat_48, 0);
  lv_label_set_text(huntArrowLbl, LV_SYMBOL_REFRESH);

  huntStateLbl = lv_label_create(col);
  lv_label_set_text(huntStateLbl, "Searching...");

  huntBar = lv_bar_create(col);
  lv_obj_set_size(huntBar, W() - 30, 12);
  lv_bar_set_range(huntBar, 0, 100);

  int chartH = H() - HEADER_H - 150;
  if (chartH < 50) chartH = 50;
  huntChart = lv_chart_create(col);
  lv_obj_set_size(huntChart, W() - 20, chartH);
  lv_chart_set_type(huntChart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(huntChart, HUNT_CHART_POINTS);
  lv_chart_set_update_mode(huntChart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_range(huntChart, LV_CHART_AXIS_PRIMARY_Y, -100, -30);
  lv_chart_set_div_line_count(huntChart, 3, 0);
  lv_obj_set_style_size(huntChart, 0, LV_PART_INDICATOR);  // no point markers
  huntSer = lv_chart_add_series(huntChart, lv_palette_main(LV_PALETTE_GREEN),
                                LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(huntChart, huntSer, LV_CHART_POINT_NONE);

  huntHist.clear();
  huntLastSamples = 0;
  load(scr, Screen::Hunt);

  setText(huntStateLbl, "Starting...");
  lv_refr_now(nullptr);
  if (huntSrc == HuntSrc::Ble) {
    if (!ble::startHunt(huntBleAddr)) setText(huntStateLbl, "Bluetooth failed to start");
  } else if (huntSrc == HuntSrc::Wifi) {
    net::startHunt(huntWifiBssid, huntWifiChannel);
  } else {
    drone::setHuntTarget(huntDroneKey);
  }
}

// ---------- Drone Remote ID ----------

bool useImperial() { return weather::useF(); }  // follow the weather unit setting

String droneTitle(const drone::Drone &d) {
  String t = d.uasId.length() ? d.uasId : d.key;
  if (drone::isDji(d)) t = "DJI  " + t;
  return t;
}

String fmtAlt(float m) {
  if (isnan(m)) return "-";
  char b[16];
  if (useImperial()) snprintf(b, sizeof(b), "%d ft", (int)lroundf(m * 3.2808f));
  else snprintf(b, sizeof(b), "%d m", (int)lroundf(m));
  return b;
}

String fmtSpeed(float ms) {
  if (isnan(ms)) return "-";
  char b[16];
  if (useImperial()) snprintf(b, sizeof(b), "%.1f mph", ms * 2.23694f);
  else snprintf(b, sizeof(b), "%.1f km/h", ms * 3.6f);
  return b;
}

String fmtDist(float m) {
  char b[16];
  if (useImperial()) {
    float ft = m * 3.2808f;
    if (ft < 1000) snprintf(b, sizeof(b), "%d ft", (int)lroundf(ft));
    else snprintf(b, sizeof(b), "%.2f mi", ft / 5280.0f);
  } else {
    if (m < 1000) snprintf(b, sizeof(b), "%d m", (int)lroundf(m));
    else snprintf(b, sizeof(b), "%.2f km", m / 1000.0f);
  }
  return b;
}

void onDroneClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= drnRowKeys.size()) return;
  drnDetailKey = drnRowKeys[i];
  showDroneDetail();
}

void drnUpdateStatus() {
  if (!drnStatus) return;
  char buf[96];
  drone::Mode m = drone::mode();
  unsigned secs = (unsigned)((drone::phaseRemainingMs() + 999) / 1000);
  if (m == drone::Mode::Scan) {
    auto ds = drone::list();
    size_t live = 0;
    for (const auto &d : ds)
      if (millis() - d.lastSeen < drone::STALE_MS) live++;
    snprintf(buf, sizeof(buf), "Scanning ch %u + BT  -  %u drone%s  -  %lu msgs",
             drone::currentChannel(), (unsigned)live, live == 1 ? "" : "s",
             (unsigned long)drone::frames());
  } else if (m == drone::Mode::Watch) {
    if (drone::watchExtended())
      snprintf(buf, sizeof(buf), "Watch: drone in range, scanning until it's gone  -  %u logged",
               (unsigned)drone::logCount());
    else
      snprintf(buf, sizeof(buf), drone::radioActive() ? "Watch: scanning (%us left)  -  %u logged"
                                                      : "Watch: next scan in %us  -  %u logged",
               secs, (unsigned)drone::logCount());
  } else {
    snprintf(buf, sizeof(buf), "Scan = nonstop.  Watch = 5 s every 30 s, logged.");
  }
  setText(drnStatus, buf);
  setText(lv_obj_get_child(drnToggle, 0), m == drone::Mode::Scan ? LV_SYMBOL_STOP " Stop" : "Scan");
  setText(lv_obj_get_child(drnWatchBtn, 0), m == drone::Mode::Watch ? LV_SYMBOL_STOP " Stop" : "Watch");
  char lb[20];
  snprintf(lb, sizeof(lb), "Log (%u)", (unsigned)drone::logCount());
  setText(lv_obj_get_child(drnLogBtn, 0), lb);
}

void drnFill() {
  if (!drnList) return;
  lv_indev_t *in = lv_indev_get_next(nullptr);  // don't rebuild under the stylus
  if (in && in->proc.state == LV_INDEV_STATE_PRESSED) return;
  drnLastFill = millis();

  auto ds = drone::list();
  lv_coord_t y = lv_obj_get_scroll_y(drnList);
  lv_obj_clean(drnList);
  drnRowKeys.clear();
  if (ds.empty()) {
    lv_list_add_text(drnList, drone::running() ? "Listening for Remote ID broadcasts..."
                                               : "No drones detected this session");
  }
  for (size_t i = 0; i < ds.size(); i++) {
    const auto &d = ds[i];
    bool stale = millis() - d.lastSeen >= drone::STALE_MS;
    lv_obj_t *b = lv_list_add_btn(drnList, d.status == 3 ? LV_SYMBOL_WARNING : LV_SYMBOL_GPS,
                                  droneTitle(d).c_str());
    if (stale) lv_obj_set_style_text_opa(b, LV_OPA_50, 0);
    if (d.status == 3) lv_obj_set_style_text_color(b, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text_fmt(r, "%d dBm  %lus", d.rssi, (unsigned long)((millis() - d.lastSeen) / 1000));
    lv_obj_set_style_text_opa(r, LV_OPA_70, 0);
    drnRowKeys.push_back(d.key);
    lv_obj_add_event_cb(b, onDroneClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  lv_obj_scroll_to_y(drnList, y, LV_ANIM_OFF);
}

void pollDrones() {
  drnUpdateStatus();
  if (millis() - drnLastFill >= DRONE_REFRESH_MS) drnFill();
}

void showDrones() {
  lv_obj_t *scr = newScreen();
  // Back leaves Scan/Watch running: they only stop when you tap Stop
  makeHeader(scr, "Drone RID", [](lv_event_t *) { showMenu(); });

  // [Scan] [Watch] [Log (n)]
  const int rowY = HEADER_H + 4, btnH = 34;
  int bw = (W() - 16) / 3;
  drnToggle = flatBtn(scr, "Scan", bw, btnH);
  lv_obj_align(drnToggle, LV_ALIGN_TOP_LEFT, 4, rowY);
  lv_obj_add_event_cb(
      drnToggle,
      [](lv_event_t *) {
        if (drone::mode() == drone::Mode::Scan) {
          drone::stop();
        } else {
          setText(drnStatus, "Starting...");
          lv_refr_now(nullptr);
          drone::startScan();
        }
        drnUpdateStatus();
        drnFill();
      },
      LV_EVENT_CLICKED, nullptr);

  drnWatchBtn = flatBtn(scr, "Watch", bw, btnH);
  lv_obj_align(drnWatchBtn, LV_ALIGN_TOP_MID, 0, rowY);
  lv_obj_set_style_bg_color(drnWatchBtn, theme::accent(), 0);
  lv_obj_add_event_cb(
      drnWatchBtn,
      [](lv_event_t *) {
        if (drone::mode() == drone::Mode::Watch) {
          drone::stop();
        } else {
          setText(drnStatus, "Starting...");
          lv_refr_now(nullptr);
          drone::startWatch();
        }
        drnUpdateStatus();
        drnFill();
      },
      LV_EVENT_CLICKED, nullptr);

  drnLogBtn = flatBtn(scr, "Log", bw, btnH);
  lv_obj_align(drnLogBtn, LV_ALIGN_TOP_RIGHT, -4, rowY);
  lv_obj_set_style_bg_color(drnLogBtn, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(drnLogBtn, [](lv_event_t *) { showDroneLog(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *body = makeBody(scr, rowY + btnH);
  drnStatus = bodyLabel(body);
  drnList = bodyList(body);

  load(scr, Screen::Drones);
  drnUpdateStatus();
  drnFill();
}

void refreshDroneDetail() {
  if (!drnDetail) return;
  drone::Drone d;
  if (!drone::find(drnDetailKey, d)) {
    setText(drnDetail, "Drone no longer in the list.");
    return;
  }
  String loc = "-", op = "-", sep = "-";
  char b[64];
  if (d.hasLoc) {
    snprintf(b, sizeof(b), "%.6f, %.6f", d.lat, d.lon);
    loc = b;
  }
  if (d.hasOp) {
    snprintf(b, sizeof(b), "%.6f, %.6f", d.opLat, d.opLon);
    op = b;
  }
  if (d.hasLoc && d.hasOp) sep = fmtDist(drone::distanceM(d.lat, d.lon, d.opLat, d.opLon));
  String hdg = isnan(d.heading) ? String("-") : String((int)d.heading) + " deg";
  String chan = d.transport == drone::Transport::Ble ? String("") : String(" ch ") + d.channel;

  char buf[700];
  snprintf(buf, sizeof(buf),
           "ID: %s\n"
           "ID type: %s%s\n"
           "Aircraft: %s\n"
           "Status: %s\n"
           "Position: %s\n"
           "Altitude: %s (baro %s)\n"
           "Height: %s\n"
           "Speed: %s  Heading: %s\n"
           "Climb: %s\n"
           "Operator at: %s\n"
           "Operator distance: %s\n"
           "Operator ID: %s\n"
           "Description: %s\n"
           "Signal: %d dBm via %s%s\n"
           "MAC: %s\n"
           "Last heard: %lus ago  (%lu msgs)",
           d.uasId.length() ? d.uasId.c_str() : "-", drone::idTypeName(d.idType),
           drone::isDji(d) ? " (DJI)" : "", drone::uaTypeName(d.uaType), drone::statusName(d.status),
           loc.c_str(), fmtAlt(d.altGeo).c_str(), fmtAlt(d.altBaro).c_str(), fmtAlt(d.height).c_str(),
           fmtSpeed(d.speed).c_str(), hdg.c_str(), fmtSpeed(d.vspeed).c_str(), op.c_str(), sep.c_str(),
           d.operatorId.length() ? d.operatorId.c_str() : "- (not broadcast)",
           d.selfId.length() ? d.selfId.c_str() : "-", d.rssi, drone::transportName(d.transport),
           chan.c_str(), d.key.c_str(), (unsigned long)((millis() - d.lastSeen) / 1000),
           (unsigned long)d.messages);
  setText(drnDetail, buf);
}

void showDroneDetail() {
  lv_obj_t *scr = newScreen();
  drone::Drone d;
  String title = drone::find(drnDetailKey, d) ? droneTitle(d) : drnDetailKey;
  makeHeader(scr, title.c_str(), [](lv_event_t *) { showDrones(); });

  const int btnRowH = 48;
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H - btnRowH);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, HEADER_H);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  drnDetail = lv_label_create(box);
  lv_label_set_long_mode(drnDetail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(drnDetail, LV_PCT(100));
  lv_label_set_text(drnDetail, "");

  lv_obj_t *hunt = flatBtn(scr, LV_SYMBOL_GPS "  Hunt by signal", W() - 16, 38);
  lv_obj_align(hunt, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(hunt, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_add_event_cb(
      hunt,
      [](lv_event_t *) {
        if (drone::mode() != drone::Mode::Scan) {
          errorBox("Use Scan mode", "Hunting needs the continuous Scan mode (Watch only listens "
                                    "5 s out of every 30 s).");
          return;
        }
        drone::Drone d;
        huntSrc = HuntSrc::Drone;
        huntDroneKey = drnDetailKey;
        huntTitle = drone::find(drnDetailKey, d) ? droneTitle(d) : drnDetailKey;
        huntBack = Screen::DroneDetail;
        showHunt();
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::DroneDetail);
  refreshDroneDetail();
}

// --- drone watch log ---

String logTitle(const drone::LogEntry &e) {
  String t = e.id[0] ? String(e.id) : String(e.key);
  if (e.flags & drone::LOG_DJI) t = "DJI  " + t;
  return t;
}

void onLogRowClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= drnLogKeys.size()) return;
  drnLogKey = drnLogKeys[i];
  showDroneLogDetail();
}

void confirmBox(const char *title, const char *msg, const char *yes, lv_event_cb_t onYes) {
  static const char *btns[3];
  btns[0] = yes;
  btns[1] = "Cancel";
  btns[2] = "";
  lv_obj_t *m = lv_msgbox_create(nullptr, title, msg, btns, false);
  lv_obj_set_width(m, W() - 40);
  lv_obj_center(m);
  lv_obj_add_event_cb(
      m,
      [](lv_event_t *e) {
        lv_obj_t *box = lv_event_get_current_target(e);
        bool confirmed = lv_msgbox_get_active_btn(box) == 0;
        lv_event_cb_t cb = (lv_event_cb_t)lv_event_get_user_data(e);
        lv_msgbox_close_async(box);  // deleting inside its own event is unsafe
        if (confirmed && cb) cb(e);
      },
      LV_EVENT_VALUE_CHANGED, (void *)onYes);
}

void showDroneLog() {
  lv_obj_t *scr = newScreen();
  const int btnW = 76;
  lv_obj_t *hdr = makeHeader(scr, "Drone log", [](lv_event_t *) { showDrones(); }, btnW);
  lv_obj_t *clr = flatBtn(hdr, LV_SYMBOL_TRASH " All", btnW, 32);
  lv_obj_align(clr, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(clr, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(
      clr,
      [](lv_event_t *) {
        if (!drone::logCount()) return;
        confirmBox("Delete log?", "Remove every drone from the watch log?", "Delete all",
                   [](lv_event_t *) {
                     drone::logClear();
                     showDroneLog();
                   });
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  auto lg = drone::watchLog();
  drnLogKeys.clear();
  if (lg.empty()) lv_list_add_text(list, "No drones logged yet. Start Watch to log drones.");
  else lv_list_add_text(list, "Kept until you delete them. Tap for details.");
  for (size_t i = 0; i < lg.size(); i++) {
    const auto &e = lg[i];
    lv_obj_t *b = lv_list_add_btn(list, (e.flags & drone::LOG_EMERGENCY) ? LV_SYMBOL_WARNING : LV_SYMBOL_GPS,
                                  logTitle(e).c_str());
    char when[32], right[48];
    timesvc::formatEpoch(e.last, when, sizeof(when));
    snprintf(right, sizeof(right), "x%u  %s", e.sightings, e.last ? when : "");
    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text(r, right);
    lv_obj_set_style_text_opa(r, LV_OPA_70, 0);
    drnLogKeys.push_back(e.key);
    lv_obj_add_event_cb(b, onLogRowClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  load(scr, Screen::DroneLog);
}

void showDroneLogDetail() {
  lv_obj_t *scr = newScreen();
  drone::LogEntry e;
  if (!drone::logFind(drnLogKey, e)) {
    showDroneLog();
    return;
  }
  makeHeader(scr, logTitle(e).c_str(), [](lv_event_t *) { showDroneLog(); });

  const int btnRowH = 48;
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H - btnRowH);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, HEADER_H);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);

  char first[32], last[32], pos[48] = "-", op[48] = "-", sep[24] = "-";
  timesvc::formatEpoch(e.first, first, sizeof(first));
  timesvc::formatEpoch(e.last, last, sizeof(last));
  if (e.flags & drone::LOG_HAS_LOC) snprintf(pos, sizeof(pos), "%.6f, %.6f", e.lat * 1e-7, e.lon * 1e-7);
  if (e.flags & drone::LOG_HAS_OP) snprintf(op, sizeof(op), "%.6f, %.6f", e.opLat * 1e-7, e.opLon * 1e-7);
  if ((e.flags & drone::LOG_HAS_LOC) && (e.flags & drone::LOG_HAS_OP))
    snprintf(sep, sizeof(sep), "%s",
             fmtDist(drone::distanceM(e.lat * 1e-7, e.lon * 1e-7, e.opLat * 1e-7, e.opLon * 1e-7)).c_str());
  String alt = e.alt == INT16_MIN ? String("-") : fmtAlt(e.alt);

  char buf[800];
  snprintf(buf, sizeof(buf),
           "ID: %s\n"
           "ID type: %s%s\n"
           "Aircraft: %s\n"
           "First seen: %s\n"
           "Last seen: %s\n"
           "Heard in: %u watch check%s\n"
           "Last position: %s\n"
           "Last altitude: %s\n"
           "Operator at: %s\n"
           "Operator distance: %s\n"
           "Operator ID: %s\n"
           "Description: %s\n"
           "Signal: last %d dBm, best %d dBm\n"
           "Via: %s\n"
           "MAC: %s%s",
           e.id[0] ? e.id : "-", drone::idTypeName(e.idType), (e.flags & drone::LOG_DJI) ? " (DJI)" : "",
           drone::uaTypeName(e.uaType), first, last, e.sightings, e.sightings == 1 ? "" : "s", pos,
           alt.c_str(), op, sep, e.opId[0] ? e.opId : "- (not broadcast)",
           e.selfId[0] ? e.selfId : "-", e.rssiLast, e.rssiMax, drone::transportName((drone::Transport)e.transport),
           e.key, (e.flags & drone::LOG_EMERGENCY) ? "\nReported an EMERGENCY status" : "");
  lv_obj_t *l = lv_label_create(box);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_text(l, buf);

  lv_obj_t *del = flatBtn(scr, LV_SYMBOL_TRASH "  Delete from log", W() - 16, 38);
  lv_obj_align(del, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(del, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(
      del,
      [](lv_event_t *) {
        confirmBox("Delete entry?", "Remove this drone from the watch log?", "Delete", [](lv_event_t *) {
          drone::logDelete(drnLogKey);
          showDroneLog();
        });
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::DroneLogDetail);
}

// ---------- Wi-Fi analyzer ----------

lv_color_t congestionColor(int n) {
  if (n == 0) return lv_palette_main(LV_PALETTE_GREY);
  if (n <= 2) return lv_palette_main(LV_PALETTE_GREEN);
  if (n <= 4) return lv_palette_main(LV_PALETTE_AMBER);
  return lv_palette_main(LV_PALETTE_RED);
}

void onApClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= wfaRows.size()) return;
  const auto &ap = wfaRows[i];
  huntSrc = HuntSrc::Wifi;
  memcpy(huntWifiBssid, ap.bssid, 6);
  huntWifiChannel = ap.channel;
  huntTitle = ap.ssid.length() ? ap.ssid : ap.bssidStr;
  huntBack = Screen::WifiAnalyzer;
  showHunt();
}

void wfaFill() {
  if (!wfaList) return;
  const auto &aps = net::rawResults();
  wfaRows.assign(aps.begin(), aps.begin() + (aps.size() < WFA_MAX_ROWS ? aps.size() : WFA_MAX_ROWS));

  int cnt[WFA_CHANNELS + 1] = {0};
  for (const auto &ap : aps)
    if (ap.channel >= 1 && ap.channel <= WFA_CHANNELS) cnt[ap.channel]++;
  int mx = 1;
  for (int c = 1; c <= WFA_CHANNELS; c++) mx = cnt[c] > mx ? cnt[c] : mx;

  int myCh = net::state() == net::State::Connected ? WiFi.channel() : 0;
  for (int c = 1; c <= WFA_CHANNELS; c++) {
    lv_bar_set_range(wfaBars[c], 0, mx);
    lv_bar_set_value(wfaBars[c], cnt[c], LV_ANIM_OFF);
    lv_obj_set_style_bg_color(wfaBars[c], congestionColor(cnt[c]), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(wfaChLbl[c], c == myCh ? lv_palette_main(LV_PALETTE_BLUE)
                                                      : theme::text(), 0);
  }

  // Suggest the least crowded of the non-overlapping 2.4 GHz channels
  int best = 1;
  for (int c : {6, 11})
    if (cnt[c] < cnt[best]) best = c;
  char buf[96];
  if (aps.empty()) snprintf(buf, sizeof(buf), "No networks found");
  else snprintf(buf, sizeof(buf), "%u networks  -  best channel: %d%s", (unsigned)aps.size(), best,
                myCh ? "  (blue = yours)" : "");
  setText(wfaStatus, buf);

  lv_obj_clean(wfaList);
  for (size_t i = 0; i < wfaRows.size(); i++) {
    const auto &ap = wfaRows[i];
    lv_obj_t *b = lv_list_add_btn(wfaList, ap.secure ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING,
                                  ap.ssid.length() ? ap.ssid.c_str() : "(hidden)");
    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text_fmt(r, "ch %u  %d dBm", ap.channel, (int)ap.rssi);
    lv_obj_set_style_text_opa(r, LV_OPA_60, 0);
    lv_obj_add_event_cb(b, onApClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  if (!wfaRows.empty()) lv_list_add_text(wfaList, "Tap a network to hunt it");
}

void pollWifiAnalyzer() {
  net::ScanState s = net::scanState();
  if (s == net::ScanState::Running) lv_obj_add_state(wfaScanBtn, LV_STATE_DISABLED);
  else lv_obj_clear_state(wfaScanBtn, LV_STATE_DISABLED);
  if (s == wfaLastSt) return;
  wfaLastSt = s;
  if (s == net::ScanState::Running) setText(wfaStatus, "Scanning all channels...");
  else if (s == net::ScanState::Failed) setText(wfaStatus, "Scan failed - tap Scan to retry");
  else if (s == net::ScanState::Done) wfaFill();
}

void showWifiAnalyzer() {
  lv_obj_t *scr = newScreen();
  const int btnW = 76;
  lv_obj_t *hdr = makeHeader(scr, "Wi-Fi analyzer", [](lv_event_t *) { showMenu(); }, btnW);
  wfaScanBtn = flatBtn(hdr, LV_SYMBOL_REFRESH " Scan", btnW, 32);
  lv_obj_align(wfaScanBtn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(wfaScanBtn, [](lv_event_t *) { net::startScan(true); }, LV_EVENT_CLICKED,
                      nullptr);

  // Channel congestion bars (networks per channel)
  const int graphH = 66;
  lv_obj_t *graph = lv_obj_create(scr);
  lv_obj_remove_style_all(graph);
  lv_obj_set_size(graph, W(), graphH);
  lv_obj_align(graph, LV_ALIGN_TOP_MID, 0, HEADER_H + 2);
  lv_obj_set_flex_flow(graph, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(graph, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_clear_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
  int colW = (W() - 10) / WFA_CHANNELS;
  for (int c = 1; c <= WFA_CHANNELS; c++) {
    lv_obj_t *col = lv_obj_create(graph);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, colW, graphH);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    wfaBars[c] = lv_bar_create(col);
    lv_obj_set_size(wfaBars[c], colW - 6, graphH - 18);  // taller than wide = vertical bar
    lv_bar_set_range(wfaBars[c], 0, 1);
    lv_bar_set_value(wfaBars[c], 0, LV_ANIM_OFF);
    wfaChLbl[c] = lv_label_create(col);
    lv_label_set_text_fmt(wfaChLbl[c], "%d", c);
  }

  lv_obj_t *body = makeBody(scr, HEADER_H + graphH + 2);
  wfaStatus = bodyLabel(body);
  wfaList = bodyList(body);

  load(scr, Screen::WifiAnalyzer);
  if (net::scanState() == net::ScanState::Done && !net::rawResults().empty()) {
    wfaLastSt = net::ScanState::Done;  // back from a hunt: show the last results
    wfaFill();
  } else {
    net::startScan(true);
    wfaLastSt = net::ScanState::Idle;
  }
}

// ---------- Tracker detector ----------

void onTrackerClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= trkRowAddrs.size()) return;
  huntSrc = HuntSrc::Ble;
  huntBleAddr = trkRowAddrs[i];
  huntTitle = ble::trackerName(trkRowTypes[i]);
  huntBack = Screen::Trackers;
  showHunt();
}

void trkUpdateStatus() {
  if (!trkStatus) return;
  auto ts = ble::trackers();
  size_t following = 0;
  for (const auto &t : ts)
    if (ble::isFollowing(t)) following++;
  char buf[96];
  if (!trkWatching) {
    snprintf(buf, sizeof(buf), "Stopped  -  %u tracker%s", (unsigned)ts.size(),
             ts.size() == 1 ? "" : "s");
  } else if (ble::mode() == ble::Mode::Scan) {
    snprintf(buf, sizeof(buf), "Scanning...  -  %u tracker%s", (unsigned)ts.size(),
             ts.size() == 1 ? "" : "s");
  } else {
    int32_t left = (int32_t)(trkNextScan - millis());
    snprintf(buf, sizeof(buf), "Next scan in %lds  -  %u found, %u following",
             (long)(left > 0 ? (left + 999) / 1000 : 0), (unsigned)ts.size(),
             (unsigned)following);
  }
  setText(trkStatus, buf);
  setText(lv_obj_get_child(trkToggle, 0), trkWatching ? "Stop" : "Watch");
}

void trkFill() {
  if (!trkList) return;
  auto ts = ble::trackers();
  lv_obj_clean(trkList);
  trkRowAddrs.clear();
  trkRowTypes.clear();

  if (ts.empty()) {
    lv_list_add_text(trkList, trkWatching ? "No trackers seen yet" : "Tap Watch to start");
  }
  const ble::TrackerSighting *alert = nullptr;
  for (size_t i = 0; i < ts.size(); i++) {
    const auto &t = ts[i];
    bool fol = ble::isFollowing(t);
    if (fol && !t.alerted && !alert) alert = &t;
    lv_obj_t *b = lv_list_add_btn(trkList, fol ? LV_SYMBOL_WARNING : LV_SYMBOL_BLUETOOTH,
                                  ble::trackerName(t.type));
    if (fol) lv_obj_set_style_text_color(b, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text_fmt(r, "%dm  x%lu  %d dBm", (int)((t.lastSeen - t.firstSeen) / 60000),
                          (unsigned long)t.rounds, t.rssi);
    lv_obj_set_style_text_opa(r, LV_OPA_70, 0);
    trkRowAddrs.push_back(t.addr);
    trkRowTypes.push_back(t.type);
    lv_obj_add_event_cb(b, onTrackerClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  char note[120];
  snprintf(note, sizeof(note), "Red = seen for %lu+ min in %lu+ scans. Tap one to hunt it.",
           (unsigned long)(ble::FOLLOW_MIN_MS / 60000), (unsigned long)ble::FOLLOW_MIN_ROUNDS);
  lv_list_add_text(trkList, note);
  if (!ts.empty()) {
    lv_obj_t *c = lv_list_add_btn(trkList, LV_SYMBOL_TRASH, "Clear list");
    lv_obj_add_event_cb(c, [](lv_event_t *) { ble::clearTrackers(); trkFill(); },
                        LV_EVENT_CLICKED, nullptr);
  }
  trkUpdateStatus();

  if (alert) {
    ble::markAlerted(alert->addr);
    char msg[160];
    snprintf(msg, sizeof(msg),
             "A %s has been near you for %d min (seen in %lu scans). If it isn't yours, "
             "tap it in the list to hunt it down.",
             ble::trackerName(alert->type), (int)((alert->lastSeen - alert->firstSeen) / 60000),
             (unsigned long)alert->rounds);
    errorBox(LV_SYMBOL_WARNING " Possible tracker", msg);
  }
}

void pollTrackers() {
  ble::Mode m = ble::mode();
  if (trkWatching && m == ble::Mode::Idle && (int32_t)(millis() - trkNextScan) >= 0) {
    if (ble::startScan()) trkNextScan = millis() + TRK_INTERVAL_MS;
    else trkWatching = false;
  }
  if (m != trkLastMode && m == ble::Mode::Idle) trkFill();  // a scan just finished
  trkLastMode = ble::mode();
  trkUpdateStatus();
}

void showTrackers() {
  lv_obj_t *scr = newScreen();
  const int btnW = 76;
  lv_obj_t *hdr = makeHeader(
      scr, "Trackers",
      [](lv_event_t *) {
        trkWatching = false;
        ble::stop();
        showMenu();
      },
      btnW);
  trkToggle = flatBtn(hdr, "Watch", btnW, 32);
  lv_obj_align(trkToggle, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(
      trkToggle,
      [](lv_event_t *) {
        trkWatching = !trkWatching;
        if (trkWatching) {
          setText(trkStatus, "Starting Bluetooth...");
          lv_refr_now(nullptr);
          trkNextScan = millis();  // scan right away
        } else {
          ble::stop();
        }
        trkFill();
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *body = makeBody(scr, HEADER_H);
  trkStatus = bodyLabel(body);
  trkList = bodyList(body);

  load(scr, Screen::Trackers);
  trkLastMode = ble::mode();
  trkFill();
}

// ---------- Weather ----------

void refreshWeather() {
  if (!wxStatus) return;
  weather::Data d = weather::data();
  bool cfg = weather::configured();

  setHidden(wxImg, !d.valid);
  if (d.valid) setImg(wxImg, weather::icon(d, true));
  setTemp(wxTemp, d.valid, d.tempC);

  String place = weather::place();
  setText(wxPlace, cfg ? (place.length() ? place.c_str() : weather::zip().c_str())
                       : "No location set");

  char buf[160];
  if (d.valid) {
    snprintf(buf, sizeof(buf), "Now: %s", weather::describe(d.code));
    setText(wxDesc, buf);
    snprintf(buf, sizeof(buf),
             "Today: %s\nHigh %d / Low %d %s\nWind %d %s (gusts to %d)\nChance of rain %d%%",
             weather::describe(d.dayCode), weather::temp(d.hiC), weather::temp(d.loC),
             weather::useF() ? "F" : "C", weather::wind(d.windKmh), weather::windUnit(),
             weather::wind(d.windMaxKmh), d.precipPct);
    setText(wxDetail, buf);
  } else {
    setText(wxDesc, cfg ? "" : "Tap ZIP code below to set your location.");
    setText(wxDetail, "");
  }

  switch (weather::status()) {
    case weather::Status::Busy:
      snprintf(buf, sizeof(buf), "Updating...");
      break;
    case weather::Status::Error:
      snprintf(buf, sizeof(buf), "Update failed: %s", weather::lastError().c_str());
      break;
    default:
      if (!cfg) buf[0] = 0;
      else if (net::state() != net::State::Connected) snprintf(buf, sizeof(buf), "Waiting for Wi-Fi");
      else if (d.valid)
        snprintf(buf, sizeof(buf), "Updated %lu min ago - Open-Meteo",
                 (unsigned long)((millis() - d.fetchedMs) / 60000));
      else snprintf(buf, sizeof(buf), "Waiting for first update");
  }
  setText(wxStatus, buf);
}

void showWeather() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Weather", [](lv_event_t *) {
    if (wxBack == Screen::Desktop) showDesktop();
    else showMenu();
  });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *card = lv_obj_create(list);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *top = lv_obj_create(card);
  lv_obj_remove_style_all(top);
  lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, 12, 0);
  wxImg = lv_img_create(top);
  lv_obj_add_flag(wxImg, LV_OBJ_FLAG_HIDDEN);
  wxTemp = makeTemp(top, &lv_font_montserrat_48, 12);

  wxPlace = lv_label_create(card);
  lv_obj_set_style_text_font(wxPlace, &lv_font_montserrat_20, 0);
  lv_label_set_text(wxPlace, "");
  wxDesc = lv_label_create(card);
  lv_label_set_text(wxDesc, "");
  wxDetail = lv_label_create(card);
  lv_label_set_text(wxDetail, "");
  wxStatus = lv_label_create(card);
  lv_obj_set_style_text_opa(wxStatus, LV_OPA_60, 0);
  lv_label_set_long_mode(wxStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(wxStatus, LV_PCT(100));
  lv_label_set_text(wxStatus, "");

  String z = weather::zip();
  String zt = String("ZIP code: ") + (z.length() ? z : String("not set"));
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_GPS, zt.c_str());
  lv_obj_add_event_cb(b, [](lv_event_t *) { showWeatherZip(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_LOOP,
                      weather::useF() ? "Units: Fahrenheit (tap for Celsius)"
                                      : "Units: Celsius (tap for Fahrenheit)");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        weather::setUseF(!weather::useF());
        showWeather();
      },
      LV_EVENT_CLICKED, nullptr);

  // Desktop icon toggle: the whole row toggles; the switch just shows the state
  b = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Show icon on desktop");
  lv_obj_t *sw = lv_switch_create(b);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (weather::showIcon()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !weather::showIcon();
        weather::setShowIcon(on);
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);

  // Active NWS alerts sub-menu
  {
    size_t n = weather::alertCount(), live = weather::unsnoozedAlertCount();
    char t[48];
    if (n) snprintf(t, sizeof(t), "Active alerts (%u)", (unsigned)n);
    else snprintf(t, sizeof(t), "Active alerts (none)");
    lv_obj_t *ab = lv_list_add_btn(list, LV_SYMBOL_WARNING, t);
    if (live) lv_obj_set_style_text_color(ab, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(
        ab,
        [](lv_event_t *) {
          wxaBack = Screen::Weather;
          showWxAlerts();
        },
        LV_EVENT_CLICKED, nullptr);
  }

  b = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Alert count on desktop");
  sw = lv_switch_create(b);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (weather::showAlertDesk()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !weather::showAlertDesk();
        weather::setShowAlertDesk(on);
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);

  // NOAA buoy sub-page
  {
    weather::Buoy by = weather::buoy();
    String t = "Buoy data (" + weather::buoyStation() + ")";
    if (weather::buoyActive() && by.valid) t += ": " + weather::waveText(by.waveM);
    lv_obj_t *bb = lv_list_add_btn(list, LV_SYMBOL_GPS, t.c_str());
    lv_obj_add_event_cb(
        bb,
        [](lv_event_t *) {
          buoyBack = Screen::Weather;
          showBuoy();
        },
        LV_EVENT_CLICKED, nullptr);
  }

  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (!weather::configured()) {
          errorBox("No location", "Set a ZIP code first.");
          return;
        }
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get the weather.");
          return;
        }
        weather::refresh();
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::Weather);
  refreshWeather();
}

void showWeatherZip() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "ZIP code", [](lv_event_t *) { showWeather(); });

  wxZipTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(wxZipTa, true);
  lv_textarea_set_accepted_chars(wxZipTa, "0123456789");
  lv_textarea_set_max_length(wxZipTa, 5);
  lv_textarea_set_placeholder_text(wxZipTa, "5-digit ZIP");
  lv_textarea_set_text(wxZipTa, weather::zip().c_str());
  lv_obj_set_width(wxZipTa, W() - 8);
  lv_obj_align(wxZipTa, LV_ALIGN_TOP_MID, 0, HEADER_H + 4);
  lv_obj_add_state(wxZipTa, LV_STATE_FOCUSED);

  int kbH = H() - HEADER_H - 50;
  if (kbH > 170) kbH = 170;
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_size(kb, W(), kbH);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, wxZipTa);
  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CANCEL) {
          showWeather();
        } else if (code == LV_EVENT_READY) {
          String z = lv_textarea_get_text(wxZipTa);
          if (z.length() != 5) {
            errorBox("Invalid ZIP", "Enter a 5-digit ZIP code.");
            return;
          }
          weather::setZip(z);
          showWeather();
        }
      },
      LV_EVENT_ALL, nullptr);

  load(scr, Screen::WeatherZip);
}

// ---------- NWS weather alerts ----------

lv_color_t severityColor(const String &sev) {
  switch (weather::severityRank(sev)) {
    case 4:
    case 3: return lv_palette_main(LV_PALETTE_RED);
    case 2: return lv_palette_main(LV_PALETTE_ORANGE);
    case 1: return lv_palette_main(LV_PALETTE_AMBER);
    default: return theme::text();
  }
}

String untilText(uint32_t until) {
  if (!until) return "until further notice";
  char b[32];
  timesvc::formatEpoch(until, b, sizeof(b));
  return String("until ") + b;
}

void onWxAlertClicked(lv_event_t *e) {
  wxAlertDetailId = *(String *)lv_event_get_user_data(e);
  showWxAlertDetail();
}

std::vector<String> wxaIds;  // row -> alert id (stable storage for user_data)

void wxaFill() {
  if (!wxaList) return;
  wxaVer = weather::alertsVersion();
  setText(wxaStatus, weather::alertsStatus().c_str());
  lv_obj_clean(wxaList);
  auto as = weather::alerts();
  wxaIds.clear();
  wxaIds.reserve(as.size());
  if (as.empty()) lv_list_add_text(wxaList, "No active alerts for your area");
  for (const auto &a : as) {
    wxaIds.push_back(a.id);
    lv_obj_t *b = lv_list_add_btn(wxaList, a.snoozed ? LV_SYMBOL_MUTE : LV_SYMBOL_WARNING, a.event.c_str());
    lv_obj_set_style_text_color(b, severityColor(a.severity), 0);
    if (a.snoozed) lv_obj_set_style_text_opa(b, LV_OPA_60, 0);
    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text(r, a.snoozed ? "snoozed" : untilText(a.until).c_str());
    lv_obj_set_style_text_color(r, theme::text(), 0);
    lv_obj_set_style_text_opa(r, LV_OPA_70, 0);
    lv_obj_add_event_cb(b, onWxAlertClicked, LV_EVENT_CLICKED, &wxaIds.back());
  }
  if (!as.empty()) lv_list_add_text(wxaList, "Tap an alert for details or to snooze it");
}

void pollWxAlerts() {
  if (weather::alertsVersion() != wxaVer) wxaFill();
  else setText(wxaStatus, weather::alertsStatus().c_str());
}

void showWxAlerts() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Weather alerts", [](lv_event_t *) {
    if (wxaBack == Screen::Desktop) showDesktop();
    else showWeather();
  });
  lv_obj_t *body = makeBody(scr, HEADER_H);
  wxaStatus = bodyLabel(body);
  lv_obj_set_style_text_opa(wxaStatus, LV_OPA_70, 0);
  wxaList = bodyList(body);
  load(scr, Screen::WxAlerts);
  wxaFill();
}

void showWxAlertDetail() {
  weather::Alert a;
  if (!weather::findAlert(wxAlertDetailId, a)) {  // expired meanwhile
    showWxAlerts();
    return;
  }
  lv_obj_t *scr = newScreen();
  makeHeader(scr, a.event.c_str(), [](lv_event_t *) { showWxAlerts(); });

  const int btnRowH = 48;
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H - btnRowH);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, HEADER_H);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 6, 0);

  auto para = [&](const String &txt, lv_color_t c, bool dim) {
    if (txt.isEmpty()) return;
    lv_obj_t *l = lv_label_create(box);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_color(l, c, 0);
    if (dim) lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
    lv_label_set_text(l, txt.c_str());
  };
  char from[32] = "now";
  if (a.onset) timesvc::formatEpoch(a.onset, from, sizeof(from));
  String meta = a.severity + " / " + a.urgency + " / " + a.certainty + "\nFrom " + from + ", " +
                untilText(a.until) + "\nAreas: " + a.area;
  para(a.headline, severityColor(a.severity), false);
  para(meta, theme::text(), true);
  para(a.description, theme::text(), false);
  if (a.instruction.length()) para("What to do: " + a.instruction, theme::accent(), false);
  para(a.sender, theme::text(), true);
  if (a.description.isEmpty())  // long texts aren't downloaded (RAM); point to the source
    para("Full text and safety instructions: weather.gov or a NOAA weather radio.", theme::accent(), true);

  lv_obj_t *sz = flatBtn(scr, a.snoozed ? LV_SYMBOL_BELL "  Unsnooze" : LV_SYMBOL_MUTE "  Snooze alert",
                         W() - 16, 38);
  lv_obj_align(sz, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(sz, lv_palette_main(a.snoozed ? LV_PALETTE_BLUE : LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(
      sz,
      [](lv_event_t *) {
        weather::Alert cur;
        if (weather::findAlert(wxAlertDetailId, cur)) weather::snoozeAlert(wxAlertDetailId, !cur.snoozed);
        showWxAlertDetail();  // rebuild with the new state
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::WxAlertDetail);
}

// ---------- NOAA buoy ----------

const char *compass(int deg) {
  static const char *pts[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  if (deg < 0) return "--";
  return pts[((deg * 10 + 112) / 225) % 16];
}

String fmtTemp(float c) {
  if (std::isnan(c)) return "--";
  return String(weather::temp(c)) + (weather::useF() ? " F" : " C");
}

void refreshBuoyScreen() {
  if (!byStatus) return;
  setText(byStatus, weather::buoyStatus().c_str());
  if (weather::buoyVersion() == byVer) return;
  byVer = weather::buoyVersion();

  weather::Buoy b = weather::buoy();
  String id = weather::buoyStation();
  String name = weather::buoyName(id);
  setText(byName, (name.length() ? name : String("NDBC station ") + id).c_str());
  setText(byWave, weather::waveText(b.waveM).c_str());
  if (!b.valid) {
    setText(byDetail, "");
    return;
  }
  char buf[320];
  int n = 0;
  auto add = [&](const char *fmt, auto... args) {
    if (n < (int)sizeof(buf)) n += snprintf(buf + n, sizeof(buf) - n, fmt, args...);
  };
  add("%s", "Significant wave height");
  if (b.waveDirDeg >= 0) add(", from %s (%d)", compass(b.waveDirDeg), b.waveDirDeg);
  if (!std::isnan(b.domPeriodS)) add("\nDominant period %.0f s", b.domPeriodS);
  if (!std::isnan(b.avgPeriodS)) add(", average %.1f s", b.avgPeriodS);
  if (!std::isnan(b.windMs)) {
    add("\nWind %s %.0f kt", compass(b.windDirDeg), b.windMs * 1.94384f);
    if (!std::isnan(b.gustMs)) add(", gusts %.0f kt", b.gustMs * 1.94384f);
  }
  if (!std::isnan(b.waterC) || !std::isnan(b.airC))
    add("\nWater %s, air %s", fmtTemp(b.waterC).c_str(), fmtTemp(b.airC).c_str());
  if (!std::isnan(b.pressHpa)) {
    if (weather::useF()) add("\nPressure %.2f inHg", b.pressHpa * 0.02953f);
    else add("\nPressure %.0f hPa", b.pressHpa);
  }
  char t1[32], t2[32];
  timesvc::formatEpoch(b.obsTime, t1, sizeof(t1));
  add("\nObserved %s", t1);
  if (b.waveTime && b.waveTime != b.obsTime) {
    timesvc::formatEpoch(b.waveTime, t2, sizeof(t2));
    add(" (waves %s)", t2);
  }
  setText(byDetail, buf);
}

void showBuoy() {
  lv_obj_t *scr = newScreen();
  String title = "Buoy " + weather::buoyStation();
  makeHeader(scr, title.c_str(), [](lv_event_t *) {
    if (buoyBack == Screen::Desktop) showDesktop();
    else showWeather();
  });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *card = lv_obj_create(list);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  byName = lv_label_create(card);
  lv_label_set_long_mode(byName, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(byName, LV_PCT(100));
  lv_label_set_text(byName, "");
  byWave = lv_label_create(card);
  lv_obj_set_style_text_font(byWave, &lv_font_montserrat_48, 0);
  lv_label_set_text(byWave, "--");
  byDetail = lv_label_create(card);
  lv_label_set_long_mode(byDetail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(byDetail, LV_PCT(100));
  lv_label_set_text(byDetail, "");
  byStatus = lv_label_create(card);
  lv_label_set_long_mode(byStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(byStatus, LV_PCT(100));
  lv_obj_set_style_text_opa(byStatus, LV_OPA_70, 0);
  lv_label_set_text(byStatus, "");

  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EDIT, "Choose buoy");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showBuoyPick(); }, LV_EVENT_CLICKED, nullptr);

  // Two independent switches: status bar and desktop widget (both, either or neither)
  auto toggleRow = [&](const char *label, bool on, lv_event_cb_t cb) {
    lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, label);
    lv_obj_t *s = lv_switch_create(row);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, s);
  };
  toggleRow("Waves in status bar", weather::showBuoyBar(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showBuoyBar();
    weather::setShowBuoyBar(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  toggleRow("Waves on desktop", weather::showBuoyDesk(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showBuoyDesk();
    weather::setShowBuoyDesk(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });

  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get buoy data.");
          return;
        }
        if (!weather::buoyActive()) {
          errorBox("Buoy is off", "Turn on the status bar or desktop wave display first.");
          return;
        }
        weather::refreshBuoy();
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::Buoy);
  refreshBuoyScreen();
}

void onBuoyPicked(lv_event_t *e) {
  const char *id = (const char *)lv_event_get_user_data(e);
  weather::setBuoyStation(id);
  showBuoy();
}

std::vector<String> pickFavs;  // stable storage for the favorite rows' user_data
String favToRemove;

void onFavPicked(lv_event_t *e) {
  weather::setBuoyStation(*(String *)lv_event_get_user_data(e));
  showBuoy();
}

// Long-press a favorite to remove it from the list
void onFavLongPress(lv_event_t *e) {
  favToRemove = *(String *)lv_event_get_user_data(e);
  String msg = "Remove buoy " + favToRemove + " from your favorites?";
  confirmBox("Remove favorite", msg.c_str(), "Remove", [](lv_event_t *) {
    weather::removeBuoyFavorite(favToRemove);
    showBuoyPick();
  });
}

void showBuoyPick() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Choose buoy", [](lv_event_t *) { showBuoy(); });
  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  String curId = weather::buoyStation();
  pickFavs = weather::buoyFavorites();
  if (!pickFavs.empty()) {
    lv_list_add_text(list, "Favorites (hold to remove)");
    for (auto &f : pickFavs) {
      String t = f + "  Added by you";
      lv_obj_t *b = lv_list_add_btn(list, curId == f ? LV_SYMBOL_OK : LV_SYMBOL_GPS, t.c_str());
      lv_obj_add_event_cb(b, onFavPicked, LV_EVENT_SHORT_CLICKED, &f);
      lv_obj_add_event_cb(b, onFavLongPress, LV_EVENT_LONG_PRESSED, &f);
    }
  }
  size_t n;
  const weather::BuoyPreset *p = weather::buoyPresets(n);
  lv_list_add_text(list, "NOAA buoys");
  for (size_t i = 0; i < n; i++) {
    String t = String(p[i].id) + "  " + p[i].name;
    lv_obj_t *b = lv_list_add_btn(list, curId == p[i].id ? LV_SYMBOL_OK : LV_SYMBOL_GPS, t.c_str());
    lv_obj_add_event_cb(b, onBuoyPicked, LV_EVENT_CLICKED, (void *)p[i].id);
  }
  lv_obj_t *o = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD, "Other station ID...");
  lv_obj_add_event_cb(o, [](lv_event_t *) { showBuoyId(); }, LV_EVENT_CLICKED, nullptr);
  lv_list_add_text(list, "Station IDs: ndbc.noaa.gov map");
  load(scr, Screen::BuoyPick);
}

void showBuoyId() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Buoy station ID", [](lv_event_t *) { showBuoyPick(); });

  byIdTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(byIdTa, true);
  lv_textarea_set_accepted_chars(byIdTa, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  lv_textarea_set_max_length(byIdTa, 7);
  lv_textarea_set_placeholder_text(byIdTa, "e.g. 41009");
  lv_textarea_set_text(byIdTa, weather::buoyStation().c_str());
  lv_obj_set_width(byIdTa, W() - 8);
  lv_obj_align(byIdTa, LV_ALIGN_TOP_MID, 0, HEADER_H + 4);
  lv_obj_add_state(byIdTa, LV_STATE_FOCUSED);

  int kbH = H() - HEADER_H - 50;
  if (kbH > 170) kbH = 170;
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
  lv_obj_set_size(kb, W(), kbH);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, byIdTa);
  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CANCEL) {
          showBuoyPick();
        } else if (code == LV_EVENT_READY) {
          String id = lv_textarea_get_text(byIdTa);
          id.trim();
          id.toUpperCase();
          if (id.length() < 4) {
            errorBox("Invalid ID", "Enter an NDBC station ID, for example 41009.");
            return;
          }
          weather::addBuoyFavorite(id);  // typed-in stations go to the top of the list
          weather::setBuoyStation(id);
          showBuoy();
        }
      },
      LV_EVENT_ALL, nullptr);

  load(scr, Screen::BuoyId);
}

// ---------- Stocks ----------

void refreshStockScreen() {
  if (!skStatus) return;
  setText(skStatus, weather::stockStatus().c_str());
  if (weather::stockVersion() == skVer) return;
  skVer = weather::stockVersion();
  weather::Stock s = weather::stock();
  if (!s.valid) {
    setText(skName, weather::stockSymbol().c_str());
    setText(skPrice, "--");
    setText(skChange, "");
    setText(skDetail, "");
    return;
  }
  String nm = s.symbol + (s.name.length() ? "  " + s.name : String());
  setText(skName, nm.c_str());
  setText(skPrice, fmtPrice(s).c_str());
  char buf[256];
  float pct = weather::stockChangePct(s);
  if (!std::isnan(pct)) {
    snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%) today", s.price - s.prevClose, pct);
    setText(skChange, buf);
    lv_obj_set_style_text_color(skChange, pctColor(s), 0);
  } else {
    setText(skChange, "");
  }
  int n = 0;
  auto add = [&](const char *fmt, auto... args) {
    if (n < (int)sizeof(buf)) n += snprintf(buf + n, sizeof(buf) - n, fmt, args...);
  };
  buf[0] = 0;
  if (!std::isnan(s.prevClose)) add("Previous close %.2f\n", s.prevClose);
  if (!std::isnan(s.dayLow) && !std::isnan(s.dayHigh)) add("Day range %.2f - %.2f\n", s.dayLow, s.dayHigh);
  if (s.exchange.length()) add("%s, %s\n", s.exchange.c_str(), s.currency.c_str());
  if (s.marketTime) {
    char t[32];
    timesvc::formatEpoch(s.marketTime, t, sizeof(t));
    add("Last trade %s", t);
  }
  setText(skDetail, buf);
}

void showStock() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Stocks", [](lv_event_t *) {
    if (stockBack == Screen::Desktop) showDesktop();
    else showMenu();
  });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *card = lv_obj_create(list);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  auto wrapLabel = [&](const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(card);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, "");
    return l;
  };
  skName = wrapLabel(&lv_font_montserrat_20);
  skPrice = wrapLabel(&lv_font_montserrat_48);
  skChange = wrapLabel(&lv_font_montserrat_20);
  skDetail = wrapLabel(nullptr);
  skStatus = wrapLabel(nullptr);
  lv_obj_set_style_text_opa(skStatus, LV_OPA_70, 0);

  String t = "Ticker: " + weather::stockSymbol();
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EDIT, t.c_str());
  lv_obj_add_event_cb(b, [](lv_event_t *) { showStockSym(); }, LV_EVENT_CLICKED, nullptr);

  auto toggleRow = [&](const char *label, bool on, lv_event_cb_t cb) {
    lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, label);
    lv_obj_t *s = lv_switch_create(row);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, s);
  };
  toggleRow("Stock in status bar", weather::showStockBar(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showStockBar();
    weather::setShowStockBar(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  toggleRow("Stock on desktop", weather::showStockDesk(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showStockDesk();
    weather::setShowStockDesk(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });

  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get stock quotes.");
          return;
        }
        if (!weather::stockActive()) {
          errorBox("Stock is off", "Turn on the status bar or desktop stock display first.");
          return;
        }
        weather::refreshStock();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_list_add_text(list, "Quotes: Yahoo Finance, may be delayed");

  load(scr, Screen::Stock);
  refreshStockScreen();
}

void showStockSym() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Stock ticker", [](lv_event_t *) { showStock(); });

  skTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(skTa, true);
  // Letters, digits and the symbols Yahoo uses: BRK-B, BF.B, ^GSPC, EURUSD=X
  lv_textarea_set_accepted_chars(skTa, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-^=");
  lv_textarea_set_max_length(skTa, 12);
  lv_textarea_set_placeholder_text(skTa, "e.g. AAPL, TSLA, BRK-B");
  lv_textarea_set_text(skTa, weather::stockSymbol().c_str());
  lv_obj_set_width(skTa, W() - 8);
  lv_obj_align(skTa, LV_ALIGN_TOP_MID, 0, HEADER_H + 4);
  lv_obj_add_state(skTa, LV_STATE_FOCUSED);

  int kbH = H() - HEADER_H - 50;
  if (kbH > 170) kbH = 170;
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
  lv_obj_set_size(kb, W(), kbH);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, skTa);
  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CANCEL) {
          showStock();
        } else if (code == LV_EVENT_READY) {
          String sym = lv_textarea_get_text(skTa);
          sym.trim();
          sym.toUpperCase();
          if (sym.isEmpty()) {
            errorBox("No ticker", "Enter a stock ticker, for example AAPL.");
            return;
          }
          weather::setStockSymbol(sym);
          showStock();
        }
      },
      LV_EVENT_ALL, nullptr);

  load(scr, Screen::StockSym);
}

// ---------- Date & time ----------

void refreshDateTime() {
  if (!dtTime) return;
  char buf[64];
  timesvc::formatTime(buf, sizeof(buf));
  setText(dtTime, buf);
  timesvc::formatDate(buf, sizeof(buf));
  setText(dtDate, buf);

  uint32_t last = timesvc::lastSyncMs();
  if (last) {
    uint32_t mins = (millis() - last) / 60000;
    if (mins == 0) snprintf(buf, sizeof(buf), "Synced just now (%s)", timesvc::server());
    else snprintf(buf, sizeof(buf), "Synced %lu min ago (%s)", (unsigned long)mins,
                  timesvc::server());
  } else if (net::state() != net::State::Connected) {
    snprintf(buf, sizeof(buf), "Waiting for Wi-Fi to sync");
  } else {
    snprintf(buf, sizeof(buf), "Syncing with %s...", timesvc::server());
  }
  setText(dtStatus, buf);
}

void showDateTime() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Date & time", [](lv_event_t *) { showMenu(); });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Clock card
  lv_obj_t *card = lv_obj_create(list);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(card, 6, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  dtTime = lv_label_create(card);
  lv_obj_set_style_text_font(dtTime, &lv_font_montserrat_48, 0);
  lv_label_set_text(dtTime, "");
  dtDate = lv_label_create(card);
  lv_label_set_text(dtDate, "");
  dtStatus = lv_label_create(card);
  lv_obj_set_style_text_opa(dtStatus, LV_OPA_60, 0);
  lv_label_set_text(dtStatus, "");

  // 24-hour toggle: the whole row toggles; the switch just shows the state
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_LOOP, "24-hour clock");
  lv_obj_t *sw = lv_switch_create(b);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (timesvc::use24h()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !timesvc::use24h();
        timesvc::setUse24h(on);
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
        refreshDateTime();
      },
      LV_EVENT_CLICKED, sw);

  // AM/PM after the status-bar clock (12-hour mode only; this page keeps it)
  b = lv_list_add_btn(list, LV_SYMBOL_EYE_OPEN, "AM/PM in status bar");
  sw = lv_switch_create(b);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (timesvc::barAmPm()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !timesvc::barAmPm();
        timesvc::setBarAmPm(on);
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);

  size_t n;
  const timesvc::Zone *z = timesvc::zones(&n);
  String tzText = String("Time zone: ") + z[timesvc::zoneIndex()].name;
  b = lv_list_add_btn(list, LV_SYMBOL_GPS, tzText.c_str());
  lv_obj_add_event_cb(b, [](lv_event_t *) { showTimeZone(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Sync now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi first to sync the clock.");
          return;
        }
        timesvc::syncNow();
        setText(dtStatus, "Syncing...");
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::DateTime);
  refreshDateTime();
}

void onZoneClicked(lv_event_t *e) {
  timesvc::setZone((size_t)(intptr_t)lv_event_get_user_data(e));
  showDateTime();
}

void showTimeZone() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Time zone", [](lv_event_t *) { showDateTime(); });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  size_t n;
  const timesvc::Zone *z = timesvc::zones(&n);
  lv_obj_t *selBtn = nullptr;
  for (size_t i = 0; i < n; i++) {
    bool sel = i == timesvc::zoneIndex();
    lv_obj_t *b = lv_list_add_btn(list, sel ? LV_SYMBOL_OK : nullptr, z[i].name);
    if (sel) selBtn = b;
    lv_obj_add_event_cb(b, onZoneClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  load(scr, Screen::TimeZone);
  if (selBtn) lv_obj_scroll_to_view(selBtn, LV_ANIM_OFF);
}

// ---------- Wi-Fi settings ----------

void onNetworkClicked(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= listSsids.size()) return;
  targetSsid = listSsids[i];
  targetSecure = listSecure[i];
  if (targetSecure) {
    showPassword();
  } else {
    net::connect(targetSsid, "", true);
    showConnecting();
  }
}

void fillList() {
  if (!wifiList) return;
  lv_obj_clean(wifiList);
  listSsids.clear();
  listSecure.clear();

  switch (net::scanState()) {
    case net::ScanState::Running:
      lv_list_add_text(wifiList, "Scanning...");
      return;
    case net::ScanState::Failed:
      lv_list_add_text(wifiList, "Scan failed. Tap Scan to retry.");
      return;
    case net::ScanState::Idle:
      return;
    case net::ScanState::Done:
      break;
  }

  const auto &aps = net::results();
  if (aps.empty()) {
    lv_list_add_text(wifiList, "No networks found");
    return;
  }

  bool connected = net::state() == net::State::Connected;
  for (size_t i = 0; i < aps.size(); i++) {
    const auto &ap = aps[i];
    bool isCurrent = connected && ap.ssid == net::ssid();
    lv_obj_t *b = lv_list_add_btn(wifiList, isCurrent ? LV_SYMBOL_OK : LV_SYMBOL_WIFI,
                                  ap.ssid.c_str());
    lv_obj_t *r = lv_label_create(b);  // right-hand detail
    lv_label_set_text_fmt(r, "%s %d%%", ap.secure ? "WPA" : "open",
                          net::signalPercent(ap.rssi));
    lv_obj_set_style_text_opa(r, LV_OPA_60, 0);

    listSsids.push_back(ap.ssid);
    listSecure.push_back(ap.secure);
    lv_obj_add_event_cb(b, onNetworkClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
}

void refreshWifi() {
  if (!wifiStatusLbl) return;

  char buf[96];
  switch (net::state()) {
    case net::State::Connected:
      snprintf(buf, sizeof(buf), "Connected: %s (%s)", net::ssid().c_str(), net::ip().c_str());
      break;
    case net::State::Connecting:
      snprintf(buf, sizeof(buf), "Connecting to %s...", net::ssid().c_str());
      break;
    case net::State::Lost:
      snprintf(buf, sizeof(buf), "Reconnecting to %s...", net::ssid().c_str());
      break;
    default:
      snprintf(buf, sizeof(buf), "Not connected");
      break;
  }
  setText(wifiStatusLbl, buf);
  setHidden(wifiForgetBtn, net::savedSsid().isEmpty());

  net::ScanState s = net::scanState();
  if (s == net::ScanState::Running) lv_obj_add_state(wifiScanBtn, LV_STATE_DISABLED);
  else lv_obj_clear_state(wifiScanBtn, LV_STATE_DISABLED);

  if (s != lastScanSt) {
    lastScanSt = s;
    fillList();
  }
}

void showWifi() {
  lv_obj_t *scr = newScreen();
  const int scanW = 84;
  lv_obj_t *hdr = makeHeader(
      scr, "Wi-Fi",
      [](lv_event_t *) {
        if (wifiBack == Screen::Desktop) showDesktop();
        else showMenu();
      },
      scanW);

  wifiScanBtn = flatBtn(hdr, LV_SYMBOL_REFRESH " Scan", scanW, 32);
  lv_obj_align(wifiScanBtn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(wifiScanBtn, [](lv_event_t *) { net::startScan(); }, LV_EVENT_CLICKED,
                      nullptr);

  lv_obj_t *body = makeBody(scr, HEADER_H);
  lv_obj_t *row = lv_obj_create(body);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_right(row, 4, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  wifiStatusLbl = bodyLabel(row);
  lv_obj_set_width(wifiStatusLbl, 0);
  lv_obj_set_flex_grow(wifiStatusLbl, 1);  // wraps in the space left of Forget

  wifiForgetBtn = flatBtn(row, "Forget", 70, 26);
  lv_obj_set_style_bg_color(wifiForgetBtn, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(wifiForgetBtn, [](lv_event_t *) { net::forget(); }, LV_EVENT_CLICKED,
                      nullptr);

  wifiList = bodyList(body);

  load(scr, Screen::Wifi);

  net::startScan();
  lastScanSt = net::scanState();
  fillList();
  refreshWifi();
}

// ---------- Password entry ----------

void showPassword() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, targetSsid.c_str(), [](lv_event_t *) { showWifi(); });

  pwTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(pwTa, true);
  lv_textarea_set_password_mode(pwTa, true);
  lv_textarea_set_placeholder_text(pwTa, "Password");
  lv_textarea_set_max_length(pwTa, 63);
  lv_obj_set_width(pwTa, W() - 8 - 56);
  lv_obj_align(pwTa, LV_ALIGN_TOP_LEFT, 4, HEADER_H + 4);
  lv_obj_add_state(pwTa, LV_STATE_FOCUSED);  // show cursor

  lv_obj_t *eye = flatBtn(scr, LV_SYMBOL_EYE_OPEN, 48, 38);
  lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -4, HEADER_H + 4);
  lv_obj_add_event_cb(
      eye,
      [](lv_event_t *e) {
        bool hidden = lv_textarea_get_password_mode(pwTa);
        lv_textarea_set_password_mode(pwTa, !hidden);
        lv_obj_t *l = lv_obj_get_child(lv_event_get_target(e), 0);
        lv_label_set_text(l, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
      },
      LV_EVENT_CLICKED, nullptr);

  int kbH = H() - HEADER_H - 50;
  if (kbH > 170) kbH = 170;
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, W(), kbH);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, pwTa);
  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CANCEL) {
          showWifi();
        } else if (code == LV_EVENT_READY) {
          String pass = lv_textarea_get_text(pwTa);
          if (pass.length() < 8) {
            errorBox("Password too short", "WPA passwords are at least 8 characters.");
            return;
          }
          net::connect(targetSsid, pass, true);
          showConnecting();
        }
      },
      LV_EVENT_ALL, nullptr);

  load(scr, Screen::Password);
}

// ---------- Connecting ----------

void pollConnecting() {
  switch (net::state()) {
    case net::State::Connected:
      showDesktop();
      break;
    case net::State::Failed: {
      char msg[128];
      snprintf(msg, sizeof(msg), "Couldn't join %s.%s", targetSsid.c_str(),
               targetSecure ? " Check the password and try again." : "");
      if (targetSecure) showPassword();
      else showWifi();
      errorBox("Connection failed", msg);
      break;
    }
    case net::State::Idle:
      showWifi();
      break;
    default:
      break;
  }
}

void showConnecting() {
  lv_obj_t *scr = newScreen();

  lv_obj_t *sp = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(sp, 64, 64);
  lv_obj_align(sp, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t *l = lv_label_create(scr);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, W() - 20);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text_fmt(l, "Connecting to %s...", targetSsid.c_str());
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 24);

  lv_obj_t *c = flatBtn(scr, "Cancel", 110, 36);
  lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_add_event_cb(c, [](lv_event_t *) { net::cancel(); showWifi(); }, LV_EVENT_CLICKED,
                      nullptr);

  load(scr, Screen::Connecting);
}

// ---------- About ----------

void refreshAbout() {
  if (!aboutUptime) return;
  // esp_timer counts microseconds in 64 bits, so it doesn't wrap after 49
  // days like millis()
  uint64_t s = (uint64_t)esp_timer_get_time() / 1000000ULL;
  unsigned d = (unsigned)(s / 86400), h = (unsigned)(s / 3600 % 24), m = (unsigned)(s / 60 % 60),
           sec = (unsigned)(s % 60);
  char buf[48];
  if (d) snprintf(buf, sizeof(buf), "Uptime: %u d %02u:%02u:%02u", d, h, m, sec);
  else snprintf(buf, sizeof(buf), "Uptime: %02u:%02u:%02u", h, m, sec);
  setText(aboutUptime, buf);
}

void showAbout() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "About", [](lv_event_t *) { showMenu(); });

  const int btnRowH = 50;
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H - btnRowH);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, HEADER_H);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 2, 0);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);

  char buf[384];
  bool up = net::state() == net::State::Connected;
  uint16_t rev = ESP.getChipRevision();  // e.g. 301 = v3.1
  snprintf(buf, sizeof(buf),
           "Firmware: v%s (%s)\n"
           "Chip: %s v%u.%u\n"
           "CPU: %d cores @ %lu MHz\n"
           "Flash: %lu KB\n"
           "Free heap: %lu KB\n"
           "MAC: %s\n"
           "Wi-Fi: %s\n"
           "IP: %s\n"
           "OTA: %s",
           FW_VERSION, __DATE__, ESP.getChipModel(), rev / 100, rev % 100, ESP.getChipCores(),
           (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)(ESP.getFlashChipSize() / 1024),
           (unsigned long)(ESP.getFreeHeap() / 1024), WiFi.macAddress().c_str(),
           up ? net::ssid().c_str() : "not connected", up ? net::ip().c_str() : "-",
           ota::ready() ? (String(ota::hostname()) + ".local").c_str() : "waiting for Wi-Fi");

  aboutUptime = lv_label_create(box);  // updated live by the UI poll
  lv_obj_set_style_text_color(aboutUptime, theme::accent(), 0);
  lv_label_set_text(aboutUptime, "");

  lv_obj_t *l = lv_label_create(box);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_text(l, buf);

  // Soft reboot, with a confirmation
  lv_obj_t *rb = flatBtn(scr, LV_SYMBOL_REFRESH "  Restart device", W() - 16, 40);
  lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(rb, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(
      rb,
      [](lv_event_t *) {
        confirmBox("Restart device?", "The device will reboot and reconnect to Wi-Fi. Settings are kept.",
                   "Restart", [](lv_event_t *) {
                     Serial.println("[ui] restart requested from About");
                     lv_obj_t *m = lv_msgbox_create(nullptr, "Restarting...", "See you in a few seconds.",
                                                    nullptr, false);
                     lv_obj_center(m);
                     lv_refr_now(nullptr);
                     delay(400);  // let the message show and the serial log flush
                     ESP.restart();
                   });
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::About);
  refreshAbout();
}

// ---------- refresh timer ----------

// "Drone detected" pop-up with Close / View. Either acknowledges that drone
// (no repeats while it stays around); other drones still alert right away.
void showNextDroneAlert() {
  String id;
  if (!drone::nextAlert(id)) return;
  droneAlertId = id;

  drone::LogEntry e;
  char msg[200];
  if (drone::logFind(id, e)) {
    char when[32];
    timesvc::formatEpoch(e.first, when, sizeof(when));
    snprintf(msg, sizeof(msg), "%s%s\n%s, signal %d dBm\nHeard in %u watch check%s, first %s",
             (e.flags & drone::LOG_DJI) ? "DJI  " : "", e.id[0] ? e.id : e.key,
             drone::uaTypeName(e.uaType), e.rssiLast, e.sightings, e.sightings == 1 ? "" : "s",
             e.first ? when : "this session");
  } else {
    snprintf(msg, sizeof(msg), "%s", id.c_str());
  }

  static const char *btns[] = {"Close", "View", ""};
  droneAlertBox = lv_msgbox_create(nullptr, LV_SYMBOL_WARNING " New drone detected", msg, btns, false);
  lv_obj_set_width(droneAlertBox, W() - 24);
  lv_obj_center(droneAlertBox);
  lv_obj_add_event_cb(
      droneAlertBox,
      [](lv_event_t *e) {
        lv_obj_t *box = lv_event_get_current_target(e);
        bool view = lv_msgbox_get_active_btn(box) == 1;
        drone::acknowledge(droneAlertId);  // no repeats while it stays around
        lv_msgbox_close_async(box);
        droneAlertBox = nullptr;
        if (view) {
          drnLogKey = droneAlertId;
          showDroneLogDetail();
        }
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
}

// New NWS alert pop-up: Close or View (opens the alert's details)
void showNextWxAlert() {
  String id;
  if (!weather::nextAlertPopup(id)) return;
  weather::Alert a;
  if (!weather::findAlert(id, a)) return;
  wxAlertId = id;
  String msg = a.event + "\n" + untilText(a.until) + "\n\n" + (a.headline.length() ? a.headline : a.area);
  static const char *btns[] = {"Close", "View", ""};
  wxAlertBox = lv_msgbox_create(nullptr, LV_SYMBOL_WARNING " NWS weather alert", msg.c_str(), btns, false);
  lv_obj_set_width(wxAlertBox, W() - 24);
  lv_obj_center(wxAlertBox);
  lv_obj_add_event_cb(
      wxAlertBox,
      [](lv_event_t *e) {
        lv_obj_t *box = lv_event_get_current_target(e);
        bool view = lv_msgbox_get_active_btn(box) == 1;
        lv_msgbox_close_async(box);
        wxAlertBox = nullptr;
        if (view) {
          wxAlertDetailId = wxAlertId;
          showWxAlertDetail();
        }
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
}

void poll(lv_timer_t *) {
  // While an HTTPS download runs, only a few KB of heap are free. Label
  // updates and pop-ups allocate from that same heap, and a failed LVGL
  // allocation crashes the board, so hold all screen updates until it ends
  // (a few seconds). Taps are still handled.
  if (weather::tlsActive()) return;

  // Drone Watch alerts, one at a time, on whatever screen is showing
  if (!droneAlertBox && !wxAlertBox) showNextDroneAlert();
  if (!droneAlertBox && !wxAlertBox) showNextWxAlert();

  switch (cur) {
    case Screen::Desktop: refreshDesktop(); break;
    case Screen::Wifi: refreshWifi(); break;
    case Screen::Connecting: pollConnecting(); break;
    case Screen::Ble: pollBle(); break;
    case Screen::BleDetail: refreshBleDetail(); break;
    case Screen::Hunt: refreshHunt(); break;
    case Screen::Drones: pollDrones(); break;
    case Screen::DroneDetail: refreshDroneDetail(); break;
    case Screen::WifiAnalyzer: pollWifiAnalyzer(); break;
    case Screen::Trackers: pollTrackers(); break;
    case Screen::DateTime: refreshDateTime(); break;
    case Screen::Weather: refreshWeather(); break;
    case Screen::WxAlerts: pollWxAlerts(); break;
    case Screen::Buoy: refreshBuoyScreen(); break;
    case Screen::About: refreshAbout(); break;
    case Screen::Stock: refreshStockScreen(); break;
    default: break;
  }
}

// Holding the stylus on the screen during boot forces recalibration
bool touchHeldAtBoot() {
  int16_t x, y;
  int hits = 0;
  for (int i = 0; i < 10; i++) {
    if (display::readRaw(x, y)) hits++;
    delay(20);
  }
  return hits >= 7;
}

}  // namespace

void ui::begin() {
  lv_timer_create(poll, 300, nullptr);
  bool force = touchHeldAtBoot();
  if (!display::hasCalibration() || force) {
    if (force) Serial.println("[ui] touch held at boot: recalibrating");
    calReturn = Screen::Desktop;
    showCalibration(!display::hasCalibration());
  } else {
    showDesktop();
  }
}

// ---------- OTA progress (called from ArduinoOTA hooks) ----------

namespace {
volatile int otaAnswer = -1;  // -1 waiting, 0 refused, 1 accepted
}

bool ui::otaAsk(unsigned sizeKB, uint32_t timeoutMs) {
  static const char *btns[] = {"Accept", "Decline", ""};
  char msg[160];
  snprintf(msg, sizeof(msg),
           "Someone on your network is pushing new firmware (%u KB) to this device.\n\n"
           "Allow the update?",
           sizeKB);
  otaAnswer = -1;
  lv_obj_t *m = lv_msgbox_create(nullptr, LV_SYMBOL_WARNING " Firmware update", msg, btns, false);
  lv_obj_set_width(m, W() - 20);
  lv_obj_center(m);
  lv_obj_t *bm = lv_msgbox_get_btns(m);
  lv_obj_set_height(bm, 48);  // big targets for the stylus
  // The countdown goes into the message label itself: a second label in the
  // msgbox content isn't laid out below it and overlapped the text.
  lv_obj_t *count = lv_msgbox_get_text(m);
  const String base = msg;
  lv_obj_add_event_cb(
      m,
      [](lv_event_t *e) {
        lv_obj_t *box = lv_event_get_current_target(e);
        otaAnswer = lv_msgbox_get_active_btn(box) == 0 ? 1 : 0;
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  // We're inside ArduinoOTA.handle(), outside the normal loop, so run LVGL
  // here until the user answers or time runs out.
  const uint32_t t0 = millis();
  int lastLeft = -1;
  while (otaAnswer < 0) {
    uint32_t el = millis() - t0;
    if (el >= timeoutMs) break;
    int left = (int)((timeoutMs - el + 999) / 1000);
    if (left != lastLeft) {
      lastLeft = left;
      char t[48];
      snprintf(t, sizeof(t), "\n\nDeclining automatically in %d s", left);
      lv_label_set_text(count, (base + t).c_str());
    }
    lv_timer_handler();
    delay(5);
  }
  bool ok = otaAnswer == 1;
  bool timedOut = otaAnswer < 0;
  lv_msgbox_close(m);
  if (!ok)
    errorBox("Update refused", timedOut ? "No answer in time, so the update was blocked."
                                        : "The firmware update was blocked.");
  lv_refr_now(nullptr);
  Serial.printf("[ota] user %s the update%s\n", ok ? "accepted" : "refused", timedOut ? " (no answer)" : "");
  return ok;
}

void ui::otaStart() {
  lv_obj_t *scr = newScreen();
  lv_obj_t *col = lv_obj_create(scr);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, W(), H());
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 12, 0);

  lv_obj_t *t = lv_label_create(col);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_label_set_text(t, LV_SYMBOL_DOWNLOAD "  Updating firmware");
  otaBar = lv_bar_create(col);
  lv_obj_set_size(otaBar, W() - 60, 16);
  lv_bar_set_range(otaBar, 0, 100);
  otaPct = lv_label_create(col);
  lv_obj_set_style_text_font(otaPct, &lv_font_montserrat_48, 0);
  lv_label_set_text(otaPct, "0%");
  otaMsg = lv_label_create(col);
  lv_label_set_text(otaMsg, "Don't power off the device");

  load(scr, Screen::Ota);
  lv_timer_handler();   // finish the screen switch; we're outside the normal loop
  lv_refr_now(nullptr);
}

void ui::otaProgress(int pct) {
  if (!otaBar) return;
  lv_bar_set_value(otaBar, pct, LV_ANIM_OFF);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  setText(otaPct, buf);
  lv_refr_now(nullptr);
}

void ui::otaFinished(bool ok, const char *msg) {
  if (!otaMsg) return;
  setText(otaMsg, msg);
  lv_obj_set_style_text_color(otaMsg, lv_palette_main(ok ? LV_PALETTE_GREEN : LV_PALETTE_RED), 0);
  lv_refr_now(nullptr);
  if (!ok) {  // stay on the error briefly, then go home
    lv_timer_t *t = lv_timer_create([](lv_timer_t *) { showDesktop(); }, 3000, nullptr);
    lv_timer_set_repeat_count(t, 1);
  }
}
