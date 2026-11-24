#include "ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <algorithm>
#include <time.h>
#include <cmath>
#include <vector>

#include "ble.h"
#include "display.h"
#include "drone.h"
#include "net.h"
#include "ota.h"
#include "sysmon.h"
#include "theme.h"
#include "timesvc.h"
#include "ui_icons.h"
#include "version.h"
#include "board.h"
#include "weather.h"

// Note: the built-in Montserrat fonts only contain ASCII + LV_SYMBOL_* icons,
// so keep UI strings ASCII-only.

namespace {

enum class Screen {
  Desktop, Menu, Wifi, Password, Connecting, About, Display, Calibrate,
  Ble, BleDetail, BleIgnored, Hunt, DateTime, TimeZone, Weather, WeatherZip,
  WifiAnalyzer, Trackers, Ota, Drones, DroneDetail, DroneLog, DroneLogDetail,
  WxAlerts, WxAlertDetail, Buoy, BuoyPick, BuoyId, Stock, StockSym, StockDetail, Nfl, NflTeam, Quakes, QuakeDetail, Volcanoes, VolcanoDetail, Flights, AirportCode
};

const int HEADER_H = 40;

int W() { return lv_disp_get_hor_res(nullptr); }
int H() { return lv_disp_get_ver_res(nullptr); }

Screen cur = Screen::Desktop;
Screen wifiBack = Screen::Menu;   // where "back" goes from Wi-Fi settings
Screen calReturn = Screen::Desktop;

// Widgets of the current screen (reset whenever the screen changes)
lv_obj_t *deskWifiIcon = nullptr;
lv_obj_t *deskWifi5g = nullptr;  // green "5G" badge over the Wi-Fi icon (dual-band boards)
lv_obj_t *deskWifiLbl = nullptr;
lv_obj_t *deskClock = nullptr;
lv_obj_t *deskAmPm = nullptr;  // small AM/PM after the status-bar clock
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
lv_obj_t *aboutCpu = nullptr;     // live CPU load line
// Stock quote: status bar readout, desktop bubble, stock page
lv_obj_t *deskBarStock = nullptr, *deskBarStockLbl = nullptr, *deskBarStockPct = nullptr;
lv_obj_t *deskStock = nullptr, *deskStockSym = nullptr, *deskStockPrice = nullptr, *deskStockPct = nullptr;
int deskStockKey = -1;
size_t deskStockIdx = 0;      // ticker shown now; the readouts rotate every few seconds
uint32_t deskStockRotMs = 0;
const uint32_t STOCK_ROTATE_MS = 5000;
lv_obj_t *skName = nullptr, *skPrice = nullptr, *skChange = nullptr, *skDetail = nullptr, *skStatus = nullptr,
         *skTa = nullptr;
uint32_t skVer = 0;
String skDetailSym;                    // ticker on the detail page
std::vector<String> skRowSyms;         // list page: row -> ticker (stable user_data)
std::vector<lv_obj_t *> skRowVals;     // list page: price + change label per row
Screen stockBack = Screen::Menu;
// NFL: status bar readout, desktop bubble, NFL page
lv_obj_t *deskBarNfl = nullptr;
lv_obj_t *deskNfl = nullptr, *deskNflScore = nullptr, *deskNflDetail = nullptr;
lv_obj_t *deskBarMenu = nullptr;  // small menu icon when the big Menu button has no room
int deskNflKey = -1;
// Earthquakes: desktop bubble and the Earthquakes pages
lv_obj_t *deskQuake = nullptr, *deskQuakeTop = nullptr, *deskQuakeDetail = nullptr;
int deskQuakeKey = -1;
lv_obj_t *eqSummary = nullptr, *eqBig = nullptr, *eqStatus = nullptr, *eqBox = nullptr, *eqHeading = nullptr;
lv_obj_t *eqDay = nullptr;     // today vs yesterday
int eqKey = -1;                // data version + minute the page was built for
BigVec<weather::Quake> eqRows;
weather::Quake eqDetail;
Screen quakeBack = Screen::Menu;
// Volcanoes: desktop bubble, page, detail and level-change pop-ups
lv_obj_t *deskVolc = nullptr, *deskVolcTop = nullptr, *deskVolcDetail = nullptr, *deskVolcImg = nullptr;
int deskVolcKey = -1;
lv_obj_t *vcSummary = nullptr, *vcStatus = nullptr, *vcUsBox = nullptr, *vcWorldBox = nullptr, *vcWorldHdr = nullptr;
int vcKey = -1;
std::vector<weather::Volcano> vcUsRows;
BigVec<weather::Eruption> vcWorldRows;
String vcDetailTitle, vcDetailText;
lv_color_t vcDetailColor;
Screen volcBack = Screen::Menu;
lv_obj_t *volcAlertBox = nullptr;  // level-change pop-up (top layer)
// Aircraft near an airport: desktop bubble, page, airport code entry
// "Near the airport" bubble: aircraft count, then one airliner's details
lv_obj_t *deskNearCnt = nullptr;  // airliner count, left of the plane icon
lv_obj_t *deskNear = nullptr, *deskNearTop = nullptr, *deskNearL2 = nullptr, *deskNearL3 = nullptr, *deskNearL4 = nullptr,
         *deskNearImg = nullptr;
int deskNearKey = -1;
size_t deskNearIdx = 0;
uint32_t deskNearRotMs = 0;
const uint32_t NEAR_ROTATE_MS = 5000;  // several airliners: show each for 5 s
// "To / from this airport" bubble: only shown while such a flight is near
lv_obj_t *deskOurs = nullptr, *deskOursTop = nullptr, *deskOursL2 = nullptr, *deskOursL3 = nullptr, *deskOursImg = nullptr;
int deskOursKey = -1;
size_t deskOursIdx = 0;
uint32_t deskOursRotMs = 0;
lv_obj_t *flSummary = nullptr, *flStatus = nullptr, *flLiveBox = nullptr;
lv_obj_t *flTa = nullptr;
int flKey = -1;
Screen fltBack = Screen::Menu;
lv_obj_t *nfScore = nullptr, *nfTeams = nullptr, *nfDetail = nullptr, *nfStatus = nullptr, *nfGames = nullptr;
uint32_t nfVer = 0;
Screen nflBack = Screen::Menu;
lv_obj_t *deskBarWave = nullptr, *deskBarWaveLbl = nullptr;
lv_obj_t *deskBarFlt = nullptr, *deskBarFltImg = nullptr, *deskBarFltNum = nullptr;  // plane + airliner count
int deskBarFltKey = -1;
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
// The graph has one bar per 2.4 GHz channel (1-13). On 5 GHz there are too
// many channels for the screen, so each bar is an 80 MHz block (the width most
// 5 GHz routers use), labeled with its first channel.
const int WFA_CHANNELS = 13;  // most bars on either band
const int WFA_5G_BINS = 7;
const uint8_t WFA_5G_FIRST[WFA_5G_BINS] = {36, 52, 100, 116, 132, 149, 165};
const uint8_t WFA_5G_LAST[WFA_5G_BINS] = {48, 64, 112, 128, 144, 161, 177};
const size_t WFA_MAX_ROWS = 40;
lv_obj_t *wfaCols[WFA_CHANNELS + 1] = {nullptr};
lv_obj_t *wfaBars[WFA_CHANNELS + 1] = {nullptr};
lv_obj_t *wfaChLbl[WFA_CHANNELS + 1] = {nullptr};
lv_obj_t *wfaList = nullptr, *wfaStatus = nullptr, *wfaScanBtn = nullptr, *wfaBandBtn = nullptr;
uint8_t wfaBand = 2;  // band shown: 2 (2.4 GHz) or 5
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
void showStockDetail();
void showNfl();
void showNflTeam();
void showQuakes();
void showQuakeDetail();
void showVolcanoes();
void showVolcanoDetail();
void showFlights();
void showAirportCode();

// "SEA 21-17 SF" (away first, like ESPN), or "SEA @ SF" before kickoff
String nflScoreText(const weather::Game &g) {
  if (g.state == "pre") return g.away + " @ " + g.home;
  return g.away + " " + String(g.awayScore) + "-" + String(g.homeScore) + " " + g.home;
}

// Game clock / kickoff time / Final
String nflDetailText(const weather::Game &g) {
  if (g.state == "pre" && g.kickoff) {
    char b[32];
    timesvc::formatEpoch(g.kickoff, b, sizeof(b));
    return b;
  }
  return g.detail;
}

// Stock formatting shared by the status bar, desktop and stock page
// ---- Earthquake helpers ----
lv_color_t magColor(float m) {
  if (m >= 6.0f) return lv_palette_main(LV_PALETTE_RED);
  if (m >= 4.5f) return lv_palette_main(LV_PALETTE_DEEP_ORANGE);
  if (m >= 3.0f) return lv_palette_main(LV_PALETTE_AMBER);
  return lv_palette_main(LV_PALETTE_GREEN);
}

// USGS aviation color code -> screen color
lv_color_t volcColor(const char *c) {
  switch (weather::colorRank(c)) {
    case 4: return lv_palette_main(LV_PALETTE_RED);
    case 3: return lv_palette_main(LV_PALETTE_DEEP_ORANGE);
    case 2: return lv_palette_main(LV_PALETTE_AMBER);
    case 1: return lv_palette_main(LV_PALETTE_GREEN);
    default: return lv_palette_main(LV_PALETTE_GREY);
  }
}

String agoText(uint32_t t) {
  time_t now = time(nullptr);
  long s = (long)now - (long)t;
  if (now < 1700000000 || s < 0) return "";
  if (s < 90) return "just now";
  if (s < 3600) return String(s / 60) + " min ago";
  if (s < 48 * 3600) {
    long h = s / 3600, m = s % 3600 / 60;
    return h < 10 && m ? String(h) + " h " + m + " min ago" : String(h) + " h ago";
  }
  return String(s / 86400) + " d " + String(s % 86400 / 3600) + " h ago";
}

String distText(float km) {
  char b[24];
  if (weather::useF()) snprintf(b, sizeof(b), "%.0f mi", km * 0.621371f);
  else snprintf(b, sizeof(b), "%.0f km", km);
  return b;
}

String quakePlace(const weather::Quake &q) {
  if (q.place[0]) return q.place;
  char b[40];
  snprintf(b, sizeof(b), "%.2f%c %.2f%c", fabsf(q.lat), q.lat >= 0 ? 'N' : 'S', fabsf(q.lon), q.lon >= 0 ? 'E' : 'W');
  return b;
}

String quakeAreaText(int km) {
  if (km <= 0) return "worldwide";
  char b[32];
  if (weather::useF()) {
    long mi = lroundf(km * 0.621371f / 50) * 50;  // 1600 km -> 1000 mi
    if (!mi) mi = lroundf(km * 0.621371f);
    snprintf(b, sizeof(b), "within %ld mi", mi);
  } else {
    snprintf(b, sizeof(b), "within %d km", km);
  }
  return b;
}

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
  deskWifiIcon = deskWifi5g = deskWifiLbl = deskClock = deskAmPm = nullptr;
  deskWx = deskWxImg = wxImg = wxPlace = wxDesc = wxDetail = wxStatus = wxZipTa = nullptr;
  deskDrone = nullptr;
  deskDroneState = -1;
  wxaList = wxaStatus = nullptr;
  deskWaveName = nullptr;
  aboutUptime = nullptr;
  aboutCpu = nullptr;
  deskBarStock = deskBarStockLbl = deskBarStockPct = deskStock = deskStockSym = deskStockPrice = deskStockPct = nullptr;
  deskStockKey = -1;
  skName = skPrice = skChange = skDetail = skStatus = skTa = nullptr;
  skVer = 0;
  skRowVals.clear();
  deskBarNfl = deskNfl = deskNflScore = deskNflDetail = deskBarMenu = nullptr;
  deskNflKey = -1;
  deskQuake = deskQuakeTop = deskQuakeDetail = nullptr;
  deskQuakeKey = -1;
  eqSummary = eqBig = eqStatus = eqBox = eqHeading = nullptr;
  deskVolc = deskVolcTop = deskVolcDetail = deskVolcImg = nullptr;
  deskVolcKey = -1;
  vcSummary = vcStatus = vcUsBox = vcWorldBox = vcWorldHdr = nullptr;
  deskNear = deskNearTop = deskNearL2 = deskNearL3 = deskNearL4 = deskNearImg = deskNearCnt = nullptr;
  deskOurs = deskOursTop = deskOursL2 = deskOursL3 = deskOursImg = nullptr;
  deskOursKey = -1;
  deskNearKey = -1;
  flSummary = flStatus = flLiveBox = flTa = nullptr;
  flKey = -1;
  vcKey = -1;
  eqDay = nullptr;
  eqKey = -1;
  nfScore = nfTeams = nfDetail = nfStatus = nfGames = nullptr;
  nfVer = 0;
  deskBar = deskRow = deskMenu = nullptr;
  deskLayoutKey = -1;
  deskWaveKey = -1;
  deskWave = deskWaveLbl = deskBarWave = deskBarWaveLbl = deskAlerts = deskAlertsLbl = nullptr;
  deskBarFlt = deskBarFltImg = deskBarFltNum = nullptr;
  deskBarFltKey = -1;
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
  if (!deskBar || !deskRow || !deskMenu || !deskWave || !deskAlerts || !deskStock || !deskNfl || !deskQuake ||
      !deskVolc || !deskNear || !deskOurs)
    return;
  lv_obj_update_layout(deskBar);
  int barH = lv_obj_get_height(deskBar);
  bool none = lv_obj_has_flag(deskWave, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(deskAlerts, LV_OBJ_FLAG_HIDDEN) &&
              lv_obj_has_flag(deskStock, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(deskNfl, LV_OBJ_FLAG_HIDDEN) &&
              lv_obj_has_flag(deskQuake, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(deskVolc, LV_OBJ_FLAG_HIDDEN) &&
              lv_obj_has_flag(deskNear, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(deskOurs, LV_OBJ_FLAG_HIDDEN);
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
  // Keep clear of the Wi-Fi line at the bottom: shrink the Menu button; if
  // that's not enough, drop the Wi-Fi line; if still not, the Menu button
  // gives way to a small menu icon in the status bar
  int limit = H() - 30;
  bool wifiLine = true, bigMenu = true;
  if (y + minH > limit) {
    wifiLine = false;
    limit = H() - 4;
  }
  if (y + minH > limit) bigMenu = false;
  setHidden(deskWifiLbl, !wifiLine);
  setHidden(deskMenu, !bigMenu);
  setHidden(deskBarMenu, bigMenu);
  if (bigMenu) {
    lv_obj_set_height(deskMenu, std::max(minH, std::min(fullH, limit - y)));
    lv_obj_align(deskMenu, LV_ALIGN_TOP_MID, 0, y);
  }
}

void refreshDesktop() {
  if (!deskWifiIcon) return;
  char buf[96];
  // The band is part of the icon state: the 5G badge comes and goes with it
  int state = (int)net::state() * 2 + (net::band() == 5 ? 1 : 0);

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
  timesvc::formatTime(clk, sizeof(clk), false);  // AM/PM goes in its own, smaller label
  setText(deskClock, clk);
  if (deskAmPm) {
    bool amPm = timesvc::barAmPm() && !timesvc::use24h() && timesvc::valid();
    if (amPm) {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      setText(deskAmPm, t.tm_hour < 12 ? "AM" : "PM");
    }
    setHidden(deskAmPm, !amPm);
  }

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
  // Stock readouts: rotate through the tickers; rebuilt only when the shown
  // ticker or its quote changes
  bool barStock = weather::showStockBar(), deskStockOn = weather::showStockDesk();
  size_t nStocks = weather::stockCount();
  if (nStocks > 1 && millis() - deskStockRotMs >= STOCK_ROTATE_MS) {
    deskStockRotMs = millis();
    deskStockIdx = (deskStockIdx + 1) % nStocks;
  }
  if (deskStockIdx >= nStocks) deskStockIdx = 0;
  setHidden(deskBarStock, !barStock || !nStocks);
  setHidden(deskStock, !deskStockOn || !nStocks);
  int stockKey = (int)(weather::stockVersion() * 16 + deskStockIdx);
  weather::Stock stk;
  if ((barStock || deskStockOn) && nStocks && stockKey != deskStockKey && weather::stockAt(deskStockIdx, stk)) {
    deskStockKey = stockKey;
    String sym = stk.symbol;
    String price = fmtPrice(stk), pct = fmtPct(stk);
    lv_color_t pc = pctColor(stk);
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

  // NFL readouts (rebuilt when the scores or switches change)
  bool barNfl = weather::showNflBar(), deskNflOn = weather::showNflDesk();
  setHidden(deskNfl, !deskNflOn);
  int nflKey = (int)weather::nflVersion();
  if (nflKey != deskNflKey) {
    deskNflKey = nflKey;
    weather::Game g;
    String team = weather::nflTeam();
    bool have = weather::nflTeamGame(g);
    String score, detail;
    if (team.isEmpty()) score = "NFL: pick a team";
    else if (!have) score = team + ": no game";
    else {
      score = nflScoreText(g);
      detail = nflDetailText(g);
    }
    setText(deskNflScore, score.c_str());
    setText(deskNflDetail, detail.c_str());
    setHidden(deskNflDetail, detail.isEmpty());
    // Live games in the accent color
    lv_color_t col = have && g.state == "in" ? theme::accent() : theme::text();
    lv_obj_set_style_text_color(deskNflScore, col, 0);
    // Status bar: only when there's a game to show
    setHidden(deskBarNfl, !barNfl || !have);
    if (have) {
      String b = nflScoreText(g) + (g.state == "post" ? " F" : "");
      setText(deskBarNfl, b.c_str());
      lv_obj_set_style_text_color(deskBarNfl, col, 0);
    }
  }
  if (!barNfl) setHidden(deskBarNfl, true);

  // Earthquake bubble: today's count, then the newest quake if it's under an
  // hour old, otherwise the largest of the last 24 h. Rebuilt when the data
  // changes and once a minute (for the "min ago" text).
  bool deskQuakeOn = weather::showQuakeDesk();
  setHidden(deskQuake, !deskQuakeOn);
  int quakeKey = (int)(weather::quakeVersion() * 4096 + (millis() / 60000) % 4096);
  if (deskQuakeOn && quakeKey != deskQuakeKey) {
    deskQuakeKey = quakeKey;
    weather::QuakeSummary qs = weather::quakeSummary();
    char top[40];
    String detail;
    lv_color_t dcol = theme::text();
    if (!qs.valid) {
      snprintf(top, sizeof(top), "Quakes: --");
    } else {
      snprintf(top, sizeof(top), "%d quake%s today", qs.today, qs.today == 1 ? "" : "s");
      time_t now = time(nullptr);
      char m[12];
      if (qs.haveLatest && (long)now - (long)qs.latest.time < 3600) {
        snprintf(m, sizeof(m), "M%.1f ", qs.latest.mag);
        detail = String("New: ") + m + quakePlace(qs.latest) + ", " + agoText(qs.latest.time);
        dcol = magColor(qs.latest.mag);
      } else if (qs.haveBiggest) {
        snprintf(m, sizeof(m), "M%.1f ", qs.biggest24.mag);
        detail = String("Largest 24 h: ") + m + quakePlace(qs.biggest24);
      } else {
        snprintf(m, sizeof(m), "M%.1f+", weather::quakeMinMag());
        detail = String("None in 24 h (") + m + ")";
      }
    }
    setText(deskQuakeTop, top);
    setText(deskQuakeDetail, detail.c_str());
    setHidden(deskQuakeDetail, detail.isEmpty());
    lv_obj_set_style_text_color(deskQuakeDetail, dcol, 0);
  }

  // Volcano bubble: how many U.S. volcanoes are elevated, then the most
  // serious one and the worldwide count from the weekly report
  bool deskVolcOn = weather::showVolcanoDesk();
  setHidden(deskVolc, !deskVolcOn);
  int volcKey = (int)weather::volcanoVersion();
  if (deskVolcOn && volcKey != deskVolcKey) {
    deskVolcKey = volcKey;
    auto us = weather::usVolcanoes();
    size_t world = weather::eruptions().size();
    char top[40];
    String detail;
    lv_color_t tcol = theme::text();
    // Short: the volcano icon in front says what it is
    if (us.empty()) snprintf(top, sizeof(top), "No US alerts");
    else snprintf(top, sizeof(top), "%u US elevated", (unsigned)us.size());
    if (!us.empty()) {
      detail = String(us[0].name) + " " + us[0].color + "/" + us[0].alert;
      if (weather::colorRank(us[0].color) >= 3) tcol = volcColor(us[0].color);  // orange or red stands out
    }
    if (world) detail += (detail.length() ? ", " : "") + String(world) + " active worldwide";
    setText(deskVolcTop, top);
    lv_obj_set_style_text_color(deskVolcTop, tcol, 0);
    lv_obj_set_style_img_recolor(deskVolcImg, us.empty() ? theme::text() : volcColor(us[0].color), 0);
    setText(deskVolcDetail, detail.c_str());
    setHidden(deskVolcDetail, detail.isEmpty());
  }

  // Status bar: plane icon + airliner count badge (dim at zero)
  if (deskBarFlt) {
    // Hidden while no airliner is near, even with the switch on
    bool on = weather::showFlightBar() && !weather::liveFlights().empty();
    setHidden(deskBarFlt, !on);
    int k = (int)weather::flightsVersion();
    if (on && k != deskBarFltKey) {
      deskBarFltKey = k;
      size_t n = weather::liveFlights().size();
      char t[8];
      snprintf(t, sizeof(t), "%u", (unsigned)n);
      setText(deskBarFltNum, t);
      lv_obj_set_style_img_recolor(deskBarFltImg, n ? theme::accent() : theme::text(), 0);
      lv_obj_set_style_img_opa(deskBarFltImg, n ? LV_OPA_COVER : LV_OPA_50, 0);
      lv_obj_set_style_bg_color(deskBarFltNum, n ? lv_palette_main(LV_PALETTE_RED) : lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    }
  }

  // "Near the airport" bubble: how many aircraft are within 10 nm, then each
  // airliner in turn (flight, type, distance / height, climb or descent, speed)
  bool deskNearOn = weather::showNearbyDesk();
  auto liveNow = weather::liveFlights();
  setHidden(deskNear, !deskNearOn || liveNow.empty());  // hidden while no airliner is near
  if (deskNearOn && !liveNow.empty()) {
    auto &live = liveNow;
    if (live.size() > 1 && millis() - deskNearRotMs >= NEAR_ROTATE_MS) {
      deskNearRotMs = millis();
      deskNearIdx++;
    }
    if (deskNearIdx >= live.size()) deskNearIdx = 0;
    int nearKey = (int)(weather::flightsVersion() * 64 + deskNearIdx % 64);
    if (nearKey != deskNearKey) {
      deskNearKey = nearKey;
      int total = weather::flightsNearTotal();
      char top[40], l2[56], l3[48], l4[48];
      // "3 [plane] near DAB": airliners only (flight-school and private traffic isn't counted)
      char cnt[8];
      if (total < 0) snprintf(cnt, sizeof(cnt), "--");
      else snprintf(cnt, sizeof(cnt), "%u", (unsigned)live.size());
      setText(deskNearCnt, cnt);
      snprintf(top, sizeof(top), "near %s", weather::airportCode().c_str());
      l3[0] = l4[0] = 0;
      if (live.empty()) {
        snprintf(l2, sizeof(l2), total < 0 ? "Checking..." : "No airliners within 10 nm");
      } else {
        const auto &f = live[deskNearIdx];
        // Airline + callsign, then flight number / type / distance, then the numbers
        // Airline and route (the flight number is on the next line already)
        char route[24];
        if (f.routeKnown) snprintf(route, sizeof(route), "%s to %s", f.from, f.to);
        else snprintf(route, sizeof(route), "route unknown");
        if (live.size() > 1)
          snprintf(l2, sizeof(l2), "%s  %s (%u/%u)", f.airline, route, (unsigned)(deskNearIdx + 1),
                   (unsigned)live.size());
        else
          snprintf(l2, sizeof(l2), "%s  %s", f.airline, route);
        snprintf(l3, sizeof(l3), "%s  %s  %.1f nm", f.flight, f.type, f.distNm);
        if (f.phase == 3)
          snprintf(l4, sizeof(l4), "On the ground, %d kt", f.gsKt);
        else if (f.rateFpm >= 100 || f.rateFpm <= -100)
          snprintf(l4, sizeof(l4), "%ld ft  %s%d fpm  %d kt", (long)f.altFt, f.rateFpm > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                   abs(f.rateFpm), f.gsKt);
        else
          snprintf(l4, sizeof(l4), "%ld ft  level  %d kt", (long)f.altFt, f.gsKt);
      }
      setText(deskNearTop, top);
      setText(deskNearL2, l2);
      setText(deskNearL3, l3);
      setText(deskNearL4, l4);
      setHidden(deskNearL3, !l3[0]);
      setHidden(deskNearL4, !l4[0]);
      lv_obj_set_style_img_recolor(deskNearImg, live.empty() ? theme::text() : theme::accent(), 0);
    }
  }

  // "To / from this airport" bubble: airliners whose route starts or ends
  // here; hidden when there are none
  if (deskOurs) {
    std::vector<weather::LiveFlight> ours;
    if (weather::showOurFlightsDesk())
      for (const auto &f : weather::liveFlights())
        if (f.ours) ours.push_back(f);
    setHidden(deskOurs, ours.empty());
    if (!ours.empty()) {
      if (ours.size() > 1 && millis() - deskOursRotMs >= NEAR_ROTATE_MS) {
        deskOursRotMs = millis();
        deskOursIdx++;
      }
      if (deskOursIdx >= ours.size()) deskOursIdx = 0;
      int k = (int)(weather::flightsVersion() * 64 + deskOursIdx % 64);
      if (k != deskOursKey) {
        deskOursKey = k;
        const auto &f = ours[deskOursIdx];
        String ap = weather::airportCode();
        bool in = f.ours == 1;
        char top[48], l2[64], l3[48];
        snprintf(top, sizeof(top), "%s %s: %s", in ? "To" : "From", ap.c_str(), f.flight);
        const char *city = in ? f.fromCity : f.toCity;
        const char *other = in ? f.from : f.to;
        if (ours.size() > 1)
          snprintf(l2, sizeof(l2), "%s %s (%s) - %s (%u/%u)", in ? "from" : "to", city[0] ? city : other, other,
                   f.airline, (unsigned)(deskOursIdx + 1), (unsigned)ours.size());
        else
          snprintf(l2, sizeof(l2), "%s %s (%s) - %s", in ? "from" : "to", city[0] ? city : other, other, f.airline);
        if (f.phase == 3)
          snprintf(l3, sizeof(l3), "On the ground  %s", f.type);
        else if (f.rateFpm >= 100 || f.rateFpm <= -100)
          snprintf(l3, sizeof(l3), "%.1f nm  %ld ft  %s%d  %d kt", f.distNm, (long)f.altFt,
                   f.rateFpm > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, abs(f.rateFpm), f.gsKt);
        else
          snprintf(l3, sizeof(l3), "%.1f nm  %ld ft  %d kt", f.distNm, (long)f.altFt, f.gsKt);
        setText(deskOursTop, top);
        setText(deskOursL2, l2);
        setText(deskOursL3, l3);
        lv_img_set_src(deskOursImg, in ? &ui_plane_land : &ui_plane);
      }
    }
  }

  if (deskAlerts) {
    bool on = weather::showAlertDesk() && weather::configured();
    setHidden(deskAlerts, !on);
    if (on) {
      size_t n = weather::alertCount(), live = weather::unsnoozedAlertCount();
      char t[24];
      if (n) snprintf(t, sizeof(t), "%u NWS alert%s", (unsigned)n, n == 1 ? "" : "s");
      else snprintf(t, sizeof(t), "No NWS alerts");
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
  setHidden(deskWifi5g, net::band() != 5);  // band() is 0 unless connected
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
    lv_label_set_text(name, PRODUCT_SHORT);
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

  // NFL: "SEA 21-17 SF", left of the stock; tap for the NFL page
  // Airliners near the airport: plane icon with the count as a small badge
  // on its lower right corner, so it takes no more room than the icon
  deskBarFlt = lv_obj_create(ext);
  lv_obj_remove_style_all(deskBarFlt);
  {
    const lv_img_dsc_t *pi = narrow ? &ui_plane_bar_s : &ui_plane_bar;  // 20 / 26 px, a bit larger than the other bar icons
    lv_obj_set_size(deskBarFlt, pi->header.w + 8, 32);
    lv_obj_clear_flag(deskBarFlt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(deskBarFlt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(deskBarFlt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_ext_click_area(deskBarFlt, 6);
    lv_obj_add_event_cb(
        deskBarFlt,
        [](lv_event_t *) {
          fltBack = Screen::Desktop;
          showFlights();
        },
        LV_EVENT_CLICKED, nullptr);
    deskBarFltImg = lv_img_create(deskBarFlt);
    lv_img_set_src(deskBarFltImg, pi);
    lv_obj_set_style_img_recolor_opa(deskBarFltImg, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(deskBarFltImg, theme::text(), 0);
    lv_obj_clear_flag(deskBarFltImg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(deskBarFltImg, LV_ALIGN_LEFT_MID, 0, -3);
    deskBarFltNum = lv_label_create(deskBarFlt);
    // Notification-style badge: white 14 px digits on red with a white rim,
    // readable on any theme and over the icon
    lv_obj_set_style_text_font(deskBarFltNum, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(deskBarFltNum, lv_color_white(), 0);
    lv_obj_set_style_bg_color(deskBarFltNum, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(deskBarFltNum, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(deskBarFltNum, lv_color_white(), 0);
    lv_obj_set_style_border_width(deskBarFltNum, 1, 0);
    lv_obj_set_style_radius(deskBarFltNum, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(deskBarFltNum, 4, 0);
    lv_obj_set_style_pad_ver(deskBarFltNum, 0, 0);
    lv_obj_set_style_text_align(deskBarFltNum, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_min_width(deskBarFltNum, 16, 0);
    lv_obj_clear_flag(deskBarFltNum, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(deskBarFltNum, "0");
    lv_obj_align(deskBarFltNum, LV_ALIGN_BOTTOM_RIGHT, 0, -1);
  }

  deskBarNfl = lv_label_create(ext);
  lv_obj_set_style_text_font(deskBarNfl, barFont, 0);
  lv_label_set_text(deskBarNfl, "");
  lv_obj_add_flag(deskBarNfl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(deskBarNfl, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_ext_click_area(deskBarNfl, 6);
  lv_obj_add_event_cb(
      deskBarNfl,
      [](lv_event_t *) {
        nflBack = Screen::Desktop;
        showNfl();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_move_to_index(deskBarNfl, lv_obj_get_index(deskBarStock));

  // Menu icon at the far left, shown only when the widgets need the Menu
  // button's space (see layoutDesktop)
  // Compact: a bare 14 px icon in a 22 px slot, so it fits on the top line
  // without pushing the other readouts to a second row. The touch area is
  // padded out so it's still easy to hit.
  deskBarMenu = lv_btn_create(bar);
  lv_obj_set_size(deskBarMenu, 22, 32);
  lv_obj_set_style_pad_all(deskBarMenu, 0, 0);
  lv_obj_set_style_bg_opa(deskBarMenu, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(deskBarMenu, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(deskBarMenu, 0, 0);
  lv_obj_set_style_border_width(deskBarMenu, 0, 0);
  lv_obj_set_ext_click_area(deskBarMenu, 10);
  lv_obj_t *mi = lv_label_create(deskBarMenu);
  lv_obj_set_style_text_font(mi, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(mi, theme::text(), 0);
  lv_label_set_text(mi, LV_SYMBOL_LIST);
  lv_obj_center(mi);
  lv_obj_add_flag(deskBarMenu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(deskBarMenu, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_move_to_index(deskBarMenu, 0);

  // Drone Watch indicator: only shown while Watch runs; tap for Drone RID
  deskDrone = lv_img_create(ext);
  lv_img_set_src(deskDrone, &ui_drone);
  lv_obj_add_flag(deskDrone, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(deskDrone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(deskDrone, 8);
  lv_obj_set_style_img_recolor_opa(deskDrone, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(deskDrone, theme::text(), 0);
  lv_obj_add_event_cb(deskDrone, [](lv_event_t *) { showDrones(); }, LV_EVENT_CLICKED, nullptr);

  // Clock; tap it for Date & time settings. The time and a small AM/PM sit
  // together in one transparent box, the AM/PM on the time's baseline.
  lv_obj_t *clk = lv_obj_create(bar);  // stays on the top line
  lv_obj_remove_style_all(clk);
  lv_obj_set_size(clk, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(clk, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(clk, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(clk, 2, 0);
  lv_obj_clear_flag(clk, LV_OBJ_FLAG_SCROLLABLE);
  // Centered on the 32 px top line (the bar aligns its children to the top)
  lv_obj_set_style_translate_y(clk, (32 - lv_font_get_line_height(barFont)) / 2, 0);
  lv_obj_add_flag(clk, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(clk, 8);
  lv_obj_add_event_cb(clk, [](lv_event_t *) { showDateTime(); }, LV_EVENT_CLICKED, nullptr);
  deskClock = lv_label_create(clk);
  lv_obj_set_style_text_font(deskClock, barFont, 0);
  lv_label_set_text(deskClock, "--:--");
  deskAmPm = lv_label_create(clk);
  lv_obj_set_style_text_font(deskAmPm, &lv_font_montserrat_10, 0);  // same size as the 5G tag
  lv_label_set_text(deskAmPm, "");
  // Bottom-aligned boxes put the two baselines this far apart; lift it to match
  lv_obj_set_style_translate_y(deskAmPm,
                               -(lv_coord_t)(barFont->base_line - lv_font_montserrat_10.base_line), 0);
  lv_obj_add_flag(deskAmPm, LV_OBJ_FLAG_HIDDEN);

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
  if (net::dualBand()) {
    // Small green "5G" tag on the lower right of the icon while on 5 GHz
    deskWifi5g = lv_label_create(wb);
    lv_label_set_text(deskWifi5g, "5G");
    lv_obj_set_style_text_font(deskWifi5g, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(deskWifi5g, lv_color_white(), 0);
    lv_obj_set_style_bg_color(deskWifi5g, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_opa(deskWifi5g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(deskWifi5g, 2, 0);
    lv_obj_set_style_pad_hor(deskWifi5g, 1, 0);
    lv_obj_set_style_pad_ver(deskWifi5g, 0, 0);
    lv_obj_set_style_text_line_space(deskWifi5g, 0, 0);
    lv_obj_add_flag(deskWifi5g, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(deskWifi5g, LV_OBJ_FLAG_CLICKABLE);  // taps go to the Wi-Fi button
    // No button padding, so the tag sits in the button's corner, overlapping
    // the icon's lower right (the icon stays centered either way)
    lv_obj_set_style_pad_all(wb, 0, 0);
    lv_obj_align(deskWifi5g, LV_ALIGN_BOTTOM_RIGHT, -1, 0);
    lv_obj_add_flag(deskWifi5g, LV_OBJ_FLAG_HIDDEN);
  }

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

  // NFL bubble, two lines: score + ">" on top, game clock / kickoff below
  deskNfl = widget([](lv_event_t *) {
    nflBack = Screen::Desktop;
    showNfl();
  });
  lv_obj_set_flex_flow(deskNfl, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(deskNfl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(deskNfl, 0, 0);
  lv_obj_t *nline = lv_obj_create(deskNfl);
  lv_obj_remove_style_all(nline);
  lv_obj_set_size(nline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(nline, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(nline, 6, 0);
  lv_obj_clear_flag(nline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(nline, LV_OBJ_FLAG_EVENT_BUBBLE);
  deskNflScore = lv_label_create(nline);
  lv_label_set_text(deskNflScore, "");
  chevron(nline);
  deskNflDetail = lv_label_create(deskNfl);
  lv_obj_set_style_text_font(deskNflDetail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_opa(deskNflDetail, LV_OPA_80, 0);
  lv_label_set_long_mode(deskNflDetail, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(deskNflDetail, W() - 40, 0);  // long details end in "..."
  lv_obj_set_height(deskNflDetail, lv_font_get_line_height(&lv_font_montserrat_14));
  lv_label_set_text(deskNflDetail, "");

  // Earthquake bubble, two lines: count + ">" on top, newest/largest below
  deskQuake = widget([](lv_event_t *) {
    quakeBack = Screen::Desktop;
    showQuakes();
  });
  lv_obj_set_flex_flow(deskQuake, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(deskQuake, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(deskQuake, 0, 0);
  lv_obj_t *qline = lv_obj_create(deskQuake);
  lv_obj_remove_style_all(qline);
  lv_obj_set_size(qline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(qline, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(qline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(qline, 6, 0);
  lv_obj_clear_flag(qline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(qline, LV_OBJ_FLAG_EVENT_BUBBLE);
  deskQuakeTop = lv_label_create(qline);
  lv_label_set_text(deskQuakeTop, "Quakes: --");
  chevron(qline);
  deskQuakeDetail = lv_label_create(deskQuake);
  lv_obj_set_style_text_font(deskQuakeDetail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_opa(deskQuakeDetail, LV_OPA_90, 0);
  lv_label_set_long_mode(deskQuakeDetail, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(deskQuakeDetail, W() - 40, 0);  // long place names end in "..."
  lv_obj_set_height(deskQuakeDetail, lv_font_get_line_height(&lv_font_montserrat_14));
  lv_label_set_text(deskQuakeDetail, "");

  // Volcano bubble, two lines like the quake one
  deskVolc = widget([](lv_event_t *) {
    volcBack = Screen::Desktop;
    showVolcanoes();
  });
  lv_obj_set_flex_flow(deskVolc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(deskVolc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(deskVolc, 0, 0);
  lv_obj_t *vline = lv_obj_create(deskVolc);
  lv_obj_remove_style_all(vline);
  lv_obj_set_size(vline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(vline, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(vline, 6, 0);
  lv_obj_clear_flag(vline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(vline, LV_OBJ_FLAG_EVENT_BUBBLE);
  deskVolcImg = lv_img_create(vline);  // icon instead of the word "Volcanoes"
  lv_img_set_src(deskVolcImg, &ui_volcano);
  lv_obj_set_style_img_recolor_opa(deskVolcImg, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(deskVolcImg, theme::text(), 0);
  lv_obj_clear_flag(deskVolcImg, LV_OBJ_FLAG_CLICKABLE);
  deskVolcTop = lv_label_create(vline);
  lv_label_set_text(deskVolcTop, "--");
  chevron(vline);
  deskVolcDetail = lv_label_create(deskVolc);
  lv_obj_set_style_text_font(deskVolcDetail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_opa(deskVolcDetail, LV_OPA_90, 0);
  lv_label_set_long_mode(deskVolcDetail, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(deskVolcDetail, W() - 40, 0);
  lv_obj_set_height(deskVolcDetail, lv_font_get_line_height(&lv_font_montserrat_14));
  lv_label_set_text(deskVolcDetail, "");

  // "Near the airport" bubble: plane icon + count, then the airliner (airline
  // and callsign; flight, type, distance; height / climb / speed)
  deskNear = widget([](lv_event_t *) {
    fltBack = Screen::Desktop;
    showFlights();
  });
  lv_obj_set_flex_flow(deskNear, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(deskNear, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(deskNear, 0, 0);
  lv_obj_t *nline2 = lv_obj_create(deskNear);
  lv_obj_remove_style_all(nline2);
  lv_obj_set_size(nline2, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(nline2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nline2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(nline2, 6, 0);
  lv_obj_clear_flag(nline2, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(nline2, LV_OBJ_FLAG_EVENT_BUBBLE);
  deskNearCnt = lv_label_create(nline2);
  lv_label_set_text(deskNearCnt, "--");
  deskNearImg = lv_img_create(nline2);
  lv_img_set_src(deskNearImg, &ui_plane_land);
  lv_obj_set_style_img_recolor_opa(deskNearImg, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(deskNearImg, theme::text(), 0);
  lv_obj_clear_flag(deskNearImg, LV_OBJ_FLAG_CLICKABLE);
  deskNearTop = lv_label_create(nline2);
  lv_label_set_text(deskNearTop, "--");
  chevron(nline2);
  auto smallLine = [&]() {
    lv_obj_t *l = lv_label_create(deskNear);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_90, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(l, W() - 40, 0);
    lv_obj_set_height(l, lv_font_get_line_height(&lv_font_montserrat_14));
    lv_label_set_text(l, "");
    return l;
  };
  deskNearL2 = smallLine();
  deskNearL3 = smallLine();
  deskNearL4 = smallLine();

  // "To / from this airport" bubble (accent color, hidden until such a flight shows up)
  deskOurs = widget([](lv_event_t *) {
    fltBack = Screen::Desktop;
    showFlights();
  });
  lv_obj_set_flex_flow(deskOurs, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(deskOurs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(deskOurs, 0, 0);
  lv_obj_set_style_border_color(deskOurs, theme::accent(), 0);
  lv_obj_t *oline = lv_obj_create(deskOurs);
  lv_obj_remove_style_all(oline);
  lv_obj_set_size(oline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(oline, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(oline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(oline, 6, 0);
  lv_obj_clear_flag(oline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(oline, LV_OBJ_FLAG_EVENT_BUBBLE);
  deskOursImg = lv_img_create(oline);
  lv_img_set_src(deskOursImg, &ui_plane_land);
  lv_obj_set_style_img_recolor_opa(deskOursImg, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(deskOursImg, theme::accent(), 0);
  lv_obj_clear_flag(deskOursImg, LV_OBJ_FLAG_CLICKABLE);
  deskOursTop = lv_label_create(oline);
  lv_obj_set_style_text_color(deskOursTop, theme::accent(), 0);
  lv_label_set_text(deskOursTop, "");
  chevron(oline);
  auto oursLine = [&]() {
    lv_obj_t *l = lv_label_create(deskOurs);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_90, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(l, W() - 40, 0);
    lv_obj_set_height(l, lv_font_get_line_height(&lv_font_montserrat_14));
    lv_label_set_text(l, "");
    return l;
  };
  deskOursL2 = oursLine();
  deskOursL3 = oursLine();

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

  b = lv_list_add_btn(list, LV_SYMBOL_PLAY, "NFL scores");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        nflBack = Screen::Menu;
        showNfl();
      },
      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_SHUFFLE, "Earthquakes");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        quakeBack = Screen::Menu;
        showQuakes();
      },
      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_UPLOAD, "Volcanoes");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        volcBack = Screen::Menu;
        showVolcanoes();
      },
      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_GPS, "Aircraft near airport");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        fltBack = Screen::Menu;
        showFlights();
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

int wfaBins() { return wfaBand == 5 ? WFA_5G_BINS : WFA_CHANNELS; }

// Bar (1-based) a channel falls in on the band being shown; 0 = not shown
int wfaBin(uint8_t ch) {
  if (wfaBand == 5) {
    for (int i = 0; i < WFA_5G_BINS; i++)
      if (ch >= WFA_5G_FIRST[i] && ch <= WFA_5G_LAST[i]) return i + 1;
    return 0;
  }
  return ch >= 1 && ch <= WFA_CHANNELS ? ch : 0;
}

uint8_t wfaBinChannel(int bin) { return wfaBand == 5 ? WFA_5G_FIRST[bin - 1] : bin; }

// Size and label the bars for the band being shown
void wfaLayoutGraph() {
  int bins = wfaBins();
  int colW = (W() - 10) / bins;
  if (colW > 40) colW = 40;
  for (int c = 1; c <= WFA_CHANNELS; c++) {
    if (!wfaCols[c]) continue;
    setHidden(wfaCols[c], c > bins);
    if (c > bins) continue;
    lv_obj_set_width(wfaCols[c], colW);
    lv_obj_set_width(wfaBars[c], colW - 6);
    lv_label_set_text_fmt(wfaChLbl[c], "%u", wfaBinChannel(c));
    lv_bar_set_value(wfaBars[c], 0, LV_ANIM_OFF);
  }
  if (wfaBandBtn) lv_label_set_text(lv_obj_get_child(wfaBandBtn, 0), wfaBand == 5 ? "5G" : "2.4G");
}

void wfaFill() {
  if (!wfaList) return;
  const auto &aps = net::rawResults();
  bool dual = net::dualBand();

  // Rows: the band being shown (all of them on a 2.4 GHz-only board)
  wfaRows.clear();
  unsigned n24 = 0, n5 = 0;
  for (const auto &ap : aps) {
    bool five = net::is5GHz(ap.channel);
    (five ? n5 : n24)++;
    if (dual && five != (wfaBand == 5)) continue;
    if (wfaRows.size() < WFA_MAX_ROWS) wfaRows.push_back(ap);
  }

  int bins = wfaBins();
  int cnt[WFA_CHANNELS + 1] = {0};
  for (const auto &ap : aps) {
    int b = wfaBin(ap.channel);
    if (b) cnt[b]++;
  }
  int mx = 1;
  for (int c = 1; c <= bins; c++) mx = cnt[c] > mx ? cnt[c] : mx;

  int myBin = wfaBin(net::channel());
  for (int c = 1; c <= bins; c++) {
    lv_bar_set_range(wfaBars[c], 0, mx);
    lv_bar_set_value(wfaBars[c], cnt[c], LV_ANIM_OFF);
    lv_obj_set_style_bg_color(wfaBars[c], congestionColor(cnt[c]), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(wfaChLbl[c], c == myBin ? lv_palette_main(LV_PALETTE_BLUE)
                                                       : theme::text(), 0);
  }

  // Suggest the least crowded channel that needs no radar check: 1/6/11 on
  // 2.4 GHz, the 36 or 149 block on 5 GHz
  int best;
  if (wfaBand == 5) {
    best = cnt[6] < cnt[1] ? 6 : 1;
  } else {
    best = 1;
    for (int c : {6, 11})
      if (cnt[c] < cnt[best]) best = c;
  }
  char buf[112];
  if (aps.empty()) snprintf(buf, sizeof(buf), "No networks found");
  else if (dual)
    snprintf(buf, sizeof(buf), "%s: %u networks (%s: %u)  -  best ch %u%s",
             wfaBand == 5 ? "5 GHz" : "2.4 GHz", wfaBand == 5 ? n5 : n24,
             wfaBand == 5 ? "2.4 GHz" : "5 GHz", wfaBand == 5 ? n24 : n5, wfaBinChannel(best),
             myBin ? "  (blue = yours)" : "");
  else
    snprintf(buf, sizeof(buf), "%u networks  -  best channel: %d%s", (unsigned)aps.size(), best,
             myBin ? "  (blue = yours)" : "");
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
  else if (!aps.empty() && dual)
    lv_list_add_text(wfaList, wfaBand == 5 ? "No 5 GHz networks in range" : "No 2.4 GHz networks in range");
}

void pollWifiAnalyzer() {
  net::ScanState s = net::scanState();
  if (s == net::ScanState::Running) lv_obj_add_state(wfaScanBtn, LV_STATE_DISABLED);
  else lv_obj_clear_state(wfaScanBtn, LV_STATE_DISABLED);
  if (s == wfaLastSt) return;
  wfaLastSt = s;
  if (s == net::ScanState::Running)
    setText(wfaStatus, net::dualBand() ? "Scanning 2.4 + 5 GHz..." : "Scanning all channels...");
  else if (s == net::ScanState::Failed) setText(wfaStatus, "Scan failed - tap Scan to retry");
  else if (s == net::ScanState::Done) wfaFill();
}

void showWifiAnalyzer() {
  lv_obj_t *scr = newScreen();
  wfaBandBtn = nullptr;
  if (net::dualBand()) {
    // Band switch + icon-only Scan, so the title still fits in portrait
    const int scanW = 48, bandW = 60;
    lv_obj_t *hdr = makeHeader(scr, W() >= 300 ? "Analyzer" : "Wi-Fi",
                               [](lv_event_t *) { showMenu(); }, scanW + bandW + 4);
    wfaScanBtn = flatBtn(hdr, LV_SYMBOL_REFRESH, scanW, 32);
    lv_obj_align(wfaScanBtn, LV_ALIGN_RIGHT_MID, 0, 0);
    wfaBandBtn = flatBtn(hdr, "2.4G", bandW, 32);
    lv_obj_align(wfaBandBtn, LV_ALIGN_RIGHT_MID, -(scanW + 4), 0);
    lv_obj_add_event_cb(wfaBandBtn,
                        [](lv_event_t *) {
                          wfaBand = wfaBand == 5 ? 2 : 5;
                          wfaLayoutGraph();
                          if (net::scanState() == net::ScanState::Done) wfaFill();
                        },
                        LV_EVENT_CLICKED, nullptr);
  } else {
    const int btnW = 76;
    lv_obj_t *hdr = makeHeader(scr, "Wi-Fi analyzer", [](lv_event_t *) { showMenu(); }, btnW);
    wfaScanBtn = flatBtn(hdr, LV_SYMBOL_REFRESH " Scan", btnW, 32);
    lv_obj_align(wfaScanBtn, LV_ALIGN_RIGHT_MID, 0, 0);
    wfaBand = 2;
  }
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
    wfaCols[c] = col;
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
  wfaLayoutGraph();

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
  const char *region = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (!region || strcmp(region, p[i].region) != 0) {  // heading at each new region
      region = p[i].region;
      lv_list_add_text(list, region);
    }
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

// "$590.12  +0.84%" (or the error) for a list row
String stockRowText(const weather::Stock &s) {
  if (!s.valid) return s.error.length() ? s.error : String("--");
  String t = fmtPrice(s);
  String p = fmtPct(s);
  if (p.length()) t += "  " + p;
  return t;
}

void onStockRow(lv_event_t *e) {
  skDetailSym = *(String *)lv_event_get_user_data(e);
  showStockDetail();
}

void refreshStockScreen() {
  if (!skStatus) return;
  setText(skStatus, weather::stockStatus().c_str());
  if (weather::stockVersion() == skVer) return;
  skVer = weather::stockVersion();
  if (cur == Screen::Stock) {  // list page: update each row
    if (weather::stockCount() != skRowVals.size()) {  // added/removed elsewhere
      showStock();
      return;
    }
    for (size_t i = 0; i < skRowVals.size(); i++) {
      weather::Stock s;
      if (!weather::stockAt(i, s)) continue;
      setText(skRowVals[i], stockRowText(s).c_str());
      lv_obj_set_style_text_color(skRowVals[i], s.valid ? pctColor(s) : theme::text(), 0);
    }
    return;
  }
  // detail page
  weather::Stock s;
  if (!weather::findStock(skDetailSym, s)) {  // removed meanwhile
    showStock();
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
    add("Last trade %s\n", t);
  }
  if (s.error.length()) add("Last update failed: %s", s.error.c_str());
  setText(skDetail, buf);
}

// List of all tickers, plus add / display switches / refresh
void showStock() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Stocks", [](lv_event_t *) {
    if (stockBack == Screen::Desktop) showDesktop();
    else showMenu();
  });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  skStatus = lv_label_create(list);
  lv_label_set_long_mode(skStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(skStatus, LV_PCT(100));
  lv_obj_set_style_pad_all(skStatus, 6, 0);
  lv_obj_set_style_text_opa(skStatus, LV_OPA_70, 0);
  lv_label_set_text(skStatus, "");

  skRowSyms = weather::stockSymbols();
  skRowVals.clear();
  lv_list_add_text(list, "Your tickers (tap for details)");
  for (auto &sym : skRowSyms) {
    lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_UP, sym.c_str());
    lv_obj_t *v = lv_label_create(b);  // right side: price + change
    lv_label_set_text(v, "--");
    skRowVals.push_back(v);
    lv_obj_add_event_cb(b, onStockRow, LV_EVENT_CLICKED, &sym);
  }
  if (skRowSyms.size() < 8) {
    lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_PLUS, "Add ticker");
    lv_obj_add_event_cb(b, [](lv_event_t *) { showStockSym(); }, LV_EVENT_CLICKED, nullptr);
  }

  lv_list_add_text(list, "Display");
  auto toggleRow = [&](const char *label, bool on, lv_event_cb_t cb) {
    lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, label);
    lv_obj_t *s = lv_switch_create(row);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, s);
  };
  toggleRow("Stocks in status bar", weather::showStockBar(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showStockBar();
    weather::setShowStockBar(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  toggleRow("Stocks on desktop", weather::showStockDesk(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showStockDesk();
    weather::setShowStockDesk(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });

  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get stock quotes.");
          return;
        }
        if (!weather::stockActive()) {
          errorBox("Stocks are off", "Turn on the status bar or desktop stock display first.");
          return;
        }
        weather::refreshStock();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_list_add_text(list, "Quotes: Yahoo Finance, may be delayed. The status bar and desktop rotate through "
                         "your tickers every 5 s.");

  load(scr, Screen::Stock);
  skVer = weather::stockVersion() - 1;  // fill the rows now
  refreshStockScreen();
}

// One ticker in detail, with Remove
void showStockDetail() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, skDetailSym.c_str(), [](lv_event_t *) { showStock(); });

  const int btnRowH = 48;
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H - btnRowH);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, HEADER_H);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 4, 0);

  auto wrapLabel = [&](const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(box);
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

  lv_obj_t *rm = flatBtn(scr, LV_SYMBOL_TRASH "  Remove ticker", W() - 16, 38);
  lv_obj_align(rm, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(rm, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(
      rm,
      [](lv_event_t *) {
        String msg = "Stop tracking " + skDetailSym + "?";
        confirmBox("Remove ticker", msg.c_str(), "Remove", [](lv_event_t *) {
          weather::removeStockSymbol(skDetailSym);
          showStock();
        });
      },
      LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::StockDetail);
  skVer = weather::stockVersion() - 1;
  refreshStockScreen();
}

void showStockSym() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Add ticker", [](lv_event_t *) { showStock(); });

  skTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(skTa, true);
  // Letters, digits and the symbols Yahoo uses: BRK-B, BF.B, ^GSPC, GC=F
  lv_textarea_set_accepted_chars(skTa, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-^=");
  lv_textarea_set_max_length(skTa, 12);
  lv_textarea_set_placeholder_text(skTa, "e.g. AAPL, GC=F, BRK-B");
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
          String why;
          if (!weather::addStockSymbol(sym, why)) {
            errorBox("Can't add", why.c_str());
            return;
          }
          showStock();
        }
      },
      LV_EVENT_ALL, nullptr);

  load(scr, Screen::StockSym);
}

// ---------- NFL scores ----------

void refreshNflScreen() {
  if (!nfStatus) return;
  setText(nfStatus, weather::nflStatus().c_str());
  if (weather::nflVersion() == nfVer) return;
  nfVer = weather::nflVersion();

  String team = weather::nflTeam();
  weather::Game g;
  if (team.isEmpty()) {
    setText(nfScore, "");
    setText(nfTeams, "No team picked");
    setText(nfDetail, "Tap Team below to follow one. All of this week's games are listed further down.");
  } else if (!weather::nflTeamGame(g)) {
    setText(nfScore, "");
    setText(nfTeams, team.c_str());
    setText(nfDetail, "No game this week (bye), or the schedule isn't out yet.");
  } else {
    char b[48];
    if (g.state == "pre") snprintf(b, sizeof(b), "%s @ %s", g.away.c_str(), g.home.c_str());
    else snprintf(b, sizeof(b), "%d - %d", g.awayScore, g.homeScore);
    setText(nfScore, b);
    lv_obj_set_style_text_color(nfScore, g.state == "in" ? theme::accent() : theme::text(), 0);
    String t = g.away + " @ " + g.home;
    setText(nfTeams, t.c_str());
    String d = nflDetailText(g);
    if (g.state == "in") {
      if (g.downDist.length()) d += "\n" + g.downDist;
      if (g.possession.length()) d += "\n" + g.possession + " has the ball";
    }
    setText(nfDetail, d.c_str());
  }

  // This week's games
  if (nfGames) {
    lv_obj_clean(nfGames);
    auto games = weather::nflGamesList();
    if (games.empty()) {
      lv_obj_t *l = lv_label_create(nfGames);
      lv_label_set_text(l, "No games loaded yet");
    }
    for (const auto &x : games) {
      lv_obj_t *l = lv_label_create(nfGames);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(l, LV_PCT(100));
      String t = nflScoreText(x) + "   " + nflDetailText(x);
      lv_label_set_text(l, t.c_str());
      bool mine = team.length() && (x.home == team || x.away == team);
      if (x.state == "in" || mine) lv_obj_set_style_text_color(l, theme::accent(), 0);
      if (x.state == "post" && !mine) lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
    }
  }
}

void showNfl() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "NFL scores", [](lv_event_t *) {
    if (nflBack == Screen::Desktop) showDesktop();
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
  auto wrapLabel = [&](lv_obj_t *parent, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, "");
    return l;
  };
  nfScore = wrapLabel(card, &lv_font_montserrat_48);
  nfTeams = wrapLabel(card, &lv_font_montserrat_20);
  nfDetail = wrapLabel(card, nullptr);
  nfStatus = wrapLabel(card, nullptr);
  lv_obj_set_style_text_opa(nfStatus, LV_OPA_70, 0);

  String team = weather::nflTeam();
  String t = "Team: ";
  if (team.isEmpty()) t += "none";
  else
    for (size_t i = 0; i < weather::nflTeamCount(); i++)
      if (team == weather::nflTeamAbbrAt(i)) t += weather::nflTeamNameAt(i);
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EDIT, t.c_str());
  lv_obj_add_event_cb(b, [](lv_event_t *) { showNflTeam(); }, LV_EVENT_CLICKED, nullptr);

  auto toggleRow = [&](const char *label, bool on, lv_event_cb_t cb) {
    lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, label);
    lv_obj_t *s = lv_switch_create(row);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, s);
  };
  toggleRow("Score in status bar", weather::showNflBar(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showNflBar();
    weather::setShowNflBar(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  toggleRow("Score on desktop", weather::showNflDesk(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showNflDesk();
    weather::setShowNflDesk(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get scores.");
          return;
        }
        if (!weather::nflActive()) {
          errorBox("Scores are off", "Turn on the status bar or desktop score display first.");
          return;
        }
        weather::refreshNfl();
      },
      LV_EVENT_CLICKED, nullptr);

  lv_list_add_text(list, "This week");
  nfGames = lv_obj_create(list);
  lv_obj_remove_style_all(nfGames);
  lv_obj_set_size(nfGames, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(nfGames, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(nfGames, 8, 0);
  lv_obj_set_style_pad_row(nfGames, 6, 0);
  lv_obj_clear_flag(nfGames, LV_OBJ_FLAG_SCROLLABLE);
  lv_list_add_text(list, "Scores: ESPN");

  load(scr, Screen::Nfl);
  nfVer = weather::nflVersion() - 1;
  refreshNflScreen();
}

void onNflTeamPicked(lv_event_t *e) {
  weather::setNflTeam((const char *)lv_event_get_user_data(e));
  showNfl();
}

void showNflTeam() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Pick a team", [](lv_event_t *) { showNfl(); });
  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
  String cur = weather::nflTeam();
  lv_obj_t *b = lv_list_add_btn(list, cur.isEmpty() ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, "None");
  lv_obj_add_event_cb(b, onNflTeamPicked, LV_EVENT_CLICKED, (void *)"");
  for (size_t i = 0; i < weather::nflTeamCount(); i++) {
    const char *ab = weather::nflTeamAbbrAt(i);
    String t = String(ab) + "  " + weather::nflTeamNameAt(i);
    b = lv_list_add_btn(list, cur == ab ? LV_SYMBOL_OK : LV_SYMBOL_PLAY, t.c_str());
    lv_obj_add_event_cb(b, onNflTeamPicked, LV_EVENT_CLICKED, (void *)ab);
  }
  load(scr, Screen::NflTeam);
}

// ---------- Earthquakes ----------

const float EQ_MAGS[] = {1.0f, 2.5f, 4.5f, 6.0f};
const int EQ_RADII[] = {0, 1600, 800, 400, 160};  // km: worldwide, ~1000/500/250/100 mi
#if defined(BOARD_NM_CYD_C5)
const size_t EQ_MAX_ROWS = 200;  // every stored quake of the last 24 h
#else
const size_t EQ_MAX_ROWS = 30;
#endif

void onQuakeRow(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= eqRows.size()) return;
  eqDetail = eqRows[i];
  showQuakeDetail();
}

void refreshQuakeScreen() {
  if (!eqStatus) return;
  weather::quakeKeepAlive();
  setText(eqStatus, weather::quakeStatus().c_str());
  int key = (int)(weather::quakeVersion() * 4096 + (millis() / 60000) % 4096);
  if (key == eqKey) return;
  eqKey = key;

  weather::QuakeSummary qs = weather::quakeSummary();
  char m[12];
  snprintf(m, sizeof(m), "M%.1f+", weather::quakeMinMag());
  String filt = String(m) + ", " + quakeAreaText(weather::quakeRadiusKm());
  char b[160];
  if (qs.valid) snprintf(b, sizeof(b), "Today: %d so far\n%s", qs.today, filt.c_str());
  else snprintf(b, sizeof(b), "Today: --\n%s", filt.c_str());
  setText(eqSummary, b);

  // Yesterday's total, and how today compares so far
  String d;
  if (!qs.valid || qs.yesterday < 0) {
    d = qs.valid ? "Yesterday: counting..." : "Yesterday: --";
  } else {
    d = String("Yesterday: ") + qs.yesterday;
    long diff = (long)qs.today - qs.yesterday;
    if (qs.yesterday > 0 && diff > 0)
      d += String("\nToday already has ") + diff + " more than yesterday";
    else if (qs.yesterday > 0) {
      int pct = (int)lroundf(100.0f * qs.today / qs.yesterday);
      d += String("\nToday so far: ") + pct + "% of yesterday's total";
    }
  }
  setText(eqDay, d.c_str());

  if (qs.valid && qs.haveBiggest) {
    snprintf(m, sizeof(m), "M%.1f", qs.biggest24.mag);
    String bt = String("Largest in 24 h: ") + m + " " + quakePlace(qs.biggest24) + ", " + agoText(qs.biggest24.time);
    setText(eqBig, bt.c_str());
    lv_obj_set_style_text_color(eqBig, magColor(qs.biggest24.mag), 0);
  } else {
    setText(eqBig, qs.valid ? "No quakes in the last 24 h" : "");
    lv_obj_set_style_text_color(eqBig, theme::text(), 0);
  }

  // The last 24 h, newest first
  time_t now = time(nullptr);
  eqRows = weather::quakes(0, EQ_MAX_ROWS);
  if (qs.valid && (int)eqRows.size() < qs.h24)
    snprintf(b, sizeof(b), "Last 24 h: newest %u of %d", (unsigned)eqRows.size(), qs.h24);
  else
    snprintf(b, sizeof(b), qs.valid ? "Last 24 h: %d quakes, newest first" : "Last 24 h", qs.h24);
  setText(eqHeading, b);

  lv_obj_clean(eqBox);
  if (qs.valid && eqRows.empty()) {
    lv_obj_t *l = lv_label_create(eqBox);
    lv_label_set_text(l, "No quakes in the last 24 h");
  }
  for (size_t i = 0; i < eqRows.size(); i++) {
    const auto &q = eqRows[i];
    lv_obj_t *row = lv_obj_create(eqBox);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_bg_color(row, theme::accent(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, onQuakeRow, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *ml = lv_label_create(row);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ml, magColor(q.mag), 0);
    lv_obj_set_width(ml, 44);
    lv_label_set_text_fmt(ml, "%.1f", q.mag);
    lv_obj_t *tl = lv_label_create(row);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(tl, 1);
    String tx = quakePlace(q) + "\n" + agoText(q.time);
    if (!std::isnan(q.depthKm)) tx += " - " + String(q.depthKm, 0) + " km deep";
    float km;
    if (weather::quakeDistanceKm(q, km)) tx += " - " + distText(km) + " away";
    lv_label_set_text(tl, tx.c_str());
    if ((long)now - (long)q.time < 3600) lv_obj_set_style_text_color(tl, theme::accent(), 0);  // last hour
  }
}

void showQuakes() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Earthquakes", [](lv_event_t *) {
    if (quakeBack == Screen::Desktop) showDesktop();
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
  auto wrapLabel = [&](lv_obj_t *parent, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, "");
    return l;
  };
  eqSummary = wrapLabel(card, &lv_font_montserrat_20);
  eqDay = wrapLabel(card, nullptr);
  eqBig = wrapLabel(card, nullptr);
  eqStatus = wrapLabel(card, nullptr);
  lv_obj_set_style_text_opa(eqStatus, LV_OPA_70, 0);

  lv_list_add_text(list, "Settings");
  char t[48];
  snprintf(t, sizeof(t), "Minimum magnitude: %.1f", weather::quakeMinMag());
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EDIT, t);
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        float cur = weather::quakeMinMag();
        size_t n = sizeof(EQ_MAGS) / sizeof(EQ_MAGS[0]), i = 0;
        while (i < n && EQ_MAGS[i] != cur) i++;
        weather::setQuakeMinMag(EQ_MAGS[(i + 1) % n]);
        showQuakes();
      },
      LV_EVENT_CLICKED, nullptr);
  String area = "Area: " + quakeAreaText(weather::quakeRadiusKm());
  b = lv_list_add_btn(list, LV_SYMBOL_GPS, area.c_str());
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        int cur = weather::quakeRadiusKm();
        size_t n = sizeof(EQ_RADII) / sizeof(EQ_RADII[0]), i = 0;
        while (i < n && EQ_RADII[i] != cur) i++;
        weather::setQuakeRadiusKm(EQ_RADII[(i + 1) % n]);
        showQuakes();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Show on desktop");
  lv_obj_t *sw = lv_switch_create(row);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (weather::showQuakeDesk()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      row,
      [](lv_event_t *e) {
        lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !weather::showQuakeDesk();
        weather::setShowQuakeDesk(on);
        if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
        else lv_obj_clear_state(s, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);
  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get earthquake data.");
          return;
        }
        weather::refreshQuake();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_list_add_text(list, "Area uses your Weather ZIP - data: USGS");

  eqHeading = lv_list_add_text(list, "");
  eqBox = lv_obj_create(list);
  lv_obj_remove_style_all(eqBox);
  lv_obj_set_size(eqBox, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(eqBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_hor(eqBox, 8, 0);
  lv_obj_set_style_pad_row(eqBox, 2, 0);
  lv_obj_clear_flag(eqBox, LV_OBJ_FLAG_SCROLLABLE);

  load(scr, Screen::Quakes);
  // Stale or never fetched: get fresh numbers now
  weather::QuakeSummary qs = weather::quakeSummary();
  if (!qs.valid || millis() - qs.fetchedMs > 10UL * 60 * 1000) weather::refreshQuake();
  eqKey = -1;
  refreshQuakeScreen();
}

void showQuakeDetail() {
  lv_obj_t *scr = newScreen();
  const auto &q = eqDetail;
  char title[32];
  snprintf(title, sizeof(title), "M%.1f earthquake", q.mag);
  makeHeader(scr, title, [](lv_event_t *) { showQuakes(); });
  lv_obj_t *body = makeBody(scr, HEADER_H);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);  // long details scroll in landscape
  lv_obj_t *big = bodyLabel(body);
  lv_obj_set_style_text_font(big, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(big, magColor(q.mag), 0);
  lv_label_set_text(big, quakePlace(q).c_str());

  char when[40];
  timesvc::formatEpoch(q.time, when, sizeof(when));
  String t = String("Time: ") + when + " (" + agoText(q.time) + ")\n";
  char b[96];
  snprintf(b, sizeof(b), "Magnitude: %.1f %s\n", q.mag, q.magType);
  t += b;
  if (!std::isnan(q.depthKm)) {
    snprintf(b, sizeof(b), "Depth: %.1f km (%.1f mi)\n", q.depthKm, q.depthKm * 0.621371f);
    t += b;
  }
  snprintf(b, sizeof(b), "Location: %.3f%c, %.3f%c\n", fabsf(q.lat), q.lat >= 0 ? 'N' : 'S', fabsf(q.lon),
           q.lon >= 0 ? 'E' : 'W');
  t += b;
  float km;
  if (weather::quakeDistanceKm(q, km)) t += "Distance: " + distText(km) + " from your ZIP code\n";
  if (q.net[0]) t += String("Reported by: ") + q.net + " network\n";
  if (q.id[0]) t += String("USGS event ID: ") + q.id + "\n";
  t += "Source: USGS Earthquake Hazards Program";
  lv_obj_t *info = bodyLabel(body);
  lv_label_set_text(info, t.c_str());
  load(scr, Screen::QuakeDetail);
}

// ---------- Volcanoes ----------

void onUsVolcanoRow(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= vcUsRows.size()) return;
  const auto &v = vcUsRows[i];
  vcDetailTitle = v.name;
  String t = String("Aviation color code: ") + v.color + "\nVolcano alert level: " + v.alert + "\n";
  if (v.obs[0]) t += String("Observatory: ") + v.obs + "\n";
  if (v.sent) {
    char when[40];
    timesvc::formatEpoch(v.sent, when, sizeof(when));
    t += String("Latest notice: ") + when + " (" + agoText(v.sent) + ")\n";
  }
  if (v.synopsis[0]) t += String("\n") + v.synopsis + "\n";
  t += "\nThe alert level is about hazards on the ground; the color code is about ash for aircraft."
       "\n\nSource: USGS Volcano Hazards Program";
  vcDetailText = t;
  vcDetailColor = volcColor(v.color);
  showVolcanoDetail();
}

void onWorldRow(lv_event_t *e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= vcWorldRows.size()) return;
  const auto &x = vcWorldRows[i];
  vcDetailTitle = x.name;
  String t = String(x.country[0] ? x.country : "") + (x.isNew ? "\nNew activity this week" : "\nContinuing activity") + "\n";
  String wk = weather::eruptionWeek();
  if (wk.length()) t += "Report for " + wk + "\n";
  float km;
  if (weather::volcanoDistanceKm(x.lat, x.lon, km)) t += "Distance: " + distText(km) + " from your ZIP code\n";
  if (x.summary[0]) t += String("\n") + x.summary + (strlen(x.summary) >= sizeof(x.summary) - 2 ? "..." : "") + "\n";
  t += "\nSource: Smithsonian / USGS Weekly Volcanic Activity Report";
  vcDetailText = t;
  vcDetailColor = x.isNew ? lv_palette_main(LV_PALETTE_DEEP_ORANGE) : theme::text();
  showVolcanoDetail();
}

// A tappable two-line row: a colored first line and a dimmer second line
lv_obj_t *twoLineRow(lv_obj_t *parent, const String &l1, lv_color_t c1, const String &l2, lv_event_cb_t cb,
                     size_t idx) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_ver(row, 4, 0);
  lv_obj_set_style_bg_color(row, theme::accent(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  lv_obj_t *a = lv_label_create(row);
  lv_obj_set_style_text_font(a, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(a, c1, 0);
  lv_label_set_long_mode(a, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(a, LV_PCT(100));
  lv_label_set_text(a, l1.c_str());
  lv_obj_t *b = lv_label_create(row);
  lv_obj_set_style_text_opa(b, LV_OPA_80, 0);
  lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(b, LV_PCT(100));
  lv_label_set_text(b, l2.c_str());
  return row;
}

void refreshVolcanoScreen() {
  if (!vcStatus) return;
  weather::volcanoKeepAlive();
  String st = weather::volcanoStatus() + "\n" + weather::eruptionStatus();
  setText(vcStatus, st.c_str());
  int key = (int)(weather::volcanoVersion() * 4096 + (millis() / 60000) % 4096);
  if (key == vcKey) return;
  vcKey = key;

  vcUsRows = weather::usVolcanoes();
  vcWorldRows = weather::eruptions();
  int nNew = 0;
  for (const auto &x : vcWorldRows) nNew += x.isNew;
  char b[96];
  snprintf(b, sizeof(b), "US elevated: %u\nWorldwide this week: %u (%d new)", (unsigned)vcUsRows.size(),
           (unsigned)vcWorldRows.size(), nNew);
  setText(vcSummary, b);
  lv_obj_set_style_text_color(vcSummary, !vcUsRows.empty() && weather::colorRank(vcUsRows[0].color) >= 3
                                             ? volcColor(vcUsRows[0].color) : theme::text(), 0);

  lv_obj_clean(vcUsBox);
  if (vcUsRows.empty()) {
    lv_obj_t *l = lv_label_create(vcUsBox);
    lv_label_set_text(l, "No U.S. volcanoes above normal");
  }
  for (size_t i = 0; i < vcUsRows.size(); i++) {
    const auto &v = vcUsRows[i];
    String l2 = String(v.color) + " / " + v.alert;
    if (v.obs[0]) l2 += String(" - ") + v.obs;
    if (v.sent) l2 += " - " + agoText(v.sent);
    twoLineRow(vcUsBox, v.name, volcColor(v.color), l2, onUsVolcanoRow, i);
  }

  String wk = weather::eruptionWeek();
  setText(vcWorldHdr, (wk.length() ? "Worldwide: " + wk : String("Worldwide this week")).c_str());
  lv_obj_clean(vcWorldBox);
  if (vcWorldRows.empty()) {
    lv_obj_t *l = lv_label_create(vcWorldBox);
    lv_label_set_text(l, "No report loaded yet");
  }
  for (size_t i = 0; i < vcWorldRows.size(); i++) {
    const auto &x = vcWorldRows[i];
    String l2 = String(x.country) + (x.isNew ? " - NEW this week" : " - continuing");
    float km;
    if (weather::volcanoDistanceKm(x.lat, x.lon, km)) l2 += " - " + distText(km) + " away";
    twoLineRow(vcWorldBox, x.name, x.isNew ? lv_palette_main(LV_PALETTE_DEEP_ORANGE) : theme::text(), l2, onWorldRow, i);
  }
}

void showVolcanoes() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Volcanoes", [](lv_event_t *) {
    if (volcBack == Screen::Desktop) showDesktop();
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
  vcSummary = lv_label_create(card);
  lv_obj_set_style_text_font(vcSummary, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(vcSummary, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(vcSummary, LV_PCT(100));
  lv_label_set_text(vcSummary, "");
  vcStatus = lv_label_create(card);
  lv_label_set_long_mode(vcStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(vcStatus, LV_PCT(100));
  lv_obj_set_style_text_opa(vcStatus, LV_OPA_70, 0);
  lv_label_set_text(vcStatus, "");

  // Settings first, so they're never at the bottom of a long list
  lv_list_add_text(list, "Settings");
  auto toggleRow = [&](const char *label, bool on, lv_event_cb_t cb) {
    lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, label);
    lv_obj_t *s = lv_switch_create(row);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, s);
  };
  toggleRow("Show on desktop", weather::showVolcanoDesk(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::showVolcanoDesk();
    weather::setShowVolcanoDesk(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  toggleRow("Pop-up when a US level changes", weather::volcanoPopups(), [](lv_event_t *e) {
    lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
    bool on = !weather::volcanoPopups();
    weather::setVolcanoPopups(on);
    if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
    else lv_obj_clear_state(s, LV_STATE_CHECKED);
  });
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to get volcano data.");
          return;
        }
        weather::refreshVolcano();
      },
      LV_EVENT_CLICKED, nullptr);

  auto box = [&]() {
    lv_obj_t *o = lv_obj_create(list);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(o, 8, 0);
    lv_obj_set_style_pad_row(o, 2, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
  };
  lv_list_add_text(list, "US volcanoes above normal (USGS)");
  vcUsBox = box();
  vcWorldHdr = lv_list_add_text(list, "Worldwide this week");
  vcWorldBox = box();
  lv_list_add_text(list, "World: Smithsonian / USGS weekly report");

  load(scr, Screen::Volcanoes);
  if (weather::usVolcanoes().empty() && weather::eruptions().empty()) weather::refreshVolcano();
  vcKey = -1;
  refreshVolcanoScreen();
}

void showVolcanoDetail() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, vcDetailTitle.c_str(), [](lv_event_t *) { showVolcanoes(); });
  lv_obj_t *body = makeBody(scr, HEADER_H);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *big = bodyLabel(body);
  lv_obj_set_style_text_font(big, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(big, vcDetailColor, 0);
  lv_label_set_text(big, vcDetailTitle.c_str());
  lv_obj_t *info = bodyLabel(body);
  lv_label_set_text(info, vcDetailText.c_str());
  load(scr, Screen::VolcanoDetail);
}

// ---------- Airport flights ----------

void refreshFlightsScreen() {
  if (!flStatus) return;
  weather::flightsKeepAlive();
  setText(flStatus, weather::flightsStatus().c_str());
  int key = (int)(weather::flightsVersion() * 4096 + (millis() / 60000) % 4096);
  if (key == flKey) return;
  flKey = key;

  String code = weather::airportCode();
  char b[256];
  int total = weather::flightsNearTotal();
  size_t nAir = weather::liveFlights().size();
  if (total < 0) snprintf(b, sizeof(b), "Near %s: --", code.c_str());
  else snprintf(b, sizeof(b), "%u airliner%s near %s", (unsigned)nAir, nAir == 1 ? "" : "s", code.c_str());
  setText(flSummary, b);

  // Near the airport right now
  auto live = weather::liveFlights();
  lv_obj_clean(flLiveBox);
  if (live.empty()) {
    lv_obj_t *l = lv_label_create(flLiveBox);
    lv_label_set_text(l, "No airliners within 10 nm");
  }
  static const char *phaseTxt[] = {"nearby", "approaching", "departing", "on the ground"};
  for (const auto &f : live) {
    lv_obj_t *l = lv_label_create(flLiveBox);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    char route[80];
    if (f.routeKnown)
      snprintf(route, sizeof(route), "%s (%s) to %s (%s)%s", f.fromCity[0] ? f.fromCity : f.from, f.from,
               f.toCity[0] ? f.toCity : f.to, f.to, f.ours == 1 ? " - ARRIVING HERE" : f.ours == 2 ? " - DEPARTED HERE" : "");
    else
      snprintf(route, sizeof(route), "Route unknown");
    if (f.phase == 3)
      snprintf(b, sizeof(b), "%s %s (%s)\n%s\n%s - %s, %.1f nm", f.airline, f.flight, f.callsign, route, f.type,
               phaseTxt[f.phase], f.distNm);
    else
      snprintf(b, sizeof(b), "%s %s (%s)\n%s\n%s - %s, %.1f nm, %ld ft, %d kt", f.airline, f.flight, f.callsign, route,
               f.type, phaseTxt[f.phase], f.distNm, (long)f.altFt, f.gsKt);
    lv_label_set_text(l, b);
    if (f.ours) lv_obj_set_style_text_color(l, theme::accent(), 0);
  }
}

void showFlights() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Near airport", [](lv_event_t *) {
    if (fltBack == Screen::Desktop) showDesktop();
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
  flSummary = lv_label_create(card);
  lv_obj_set_style_text_font(flSummary, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(flSummary, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(flSummary, LV_PCT(100));
  lv_label_set_text(flSummary, "");
  flStatus = lv_label_create(card);
  lv_label_set_long_mode(flStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(flStatus, LV_PCT(100));
  lv_obj_set_style_text_opa(flStatus, LV_OPA_70, 0);
  lv_label_set_text(flStatus, "");

  lv_list_add_text(list, "Settings");
  String name = weather::airportName();
  String t = "Airport: " + weather::airportCode() + (name.length() ? " (" + name + ")" : String(""));
  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_EDIT, t.c_str());
  lv_obj_add_event_cb(b, [](lv_event_t *) { showAirportCode(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Show on desktop");
  lv_obj_t *sw = lv_switch_create(row);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (weather::showNearbyDesk()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      row,
      [](lv_event_t *e) {
        lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !weather::showNearbyDesk();
        weather::setShowNearbyDesk(on);
        if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
        else lv_obj_clear_state(s, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);
  row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Airliner count in status bar");
  sw = lv_switch_create(row);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (weather::showFlightBar()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      row,
      [](lv_event_t *e) {
        lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !weather::showFlightBar();
        weather::setShowFlightBar(on);
        if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
        else lv_obj_clear_state(s, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);
  String ot = "Flights to/from " + weather::airportCode() + " widget (follows the airport above)";
  row = lv_list_add_btn(list, LV_SYMBOL_IMAGE, ot.c_str());
  sw = lv_switch_create(row);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  if (weather::showOurFlightsDesk()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      row,
      [](lv_event_t *e) {
        lv_obj_t *s = (lv_obj_t *)lv_event_get_user_data(e);
        bool on = !weather::showOurFlightsDesk();
        weather::setShowOurFlightsDesk(on);
        if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
        else lv_obj_clear_state(s, LV_STATE_CHECKED);
      },
      LV_EVENT_CLICKED, sw);
  b = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Refresh now");
  lv_obj_add_event_cb(
      b,
      [](lv_event_t *) {
        if (net::state() != net::State::Connected) {
          errorBox("No Wi-Fi", "Connect to Wi-Fi to track flights.");
          return;
        }
        weather::refreshFlights();
      },
      LV_EVENT_CLICKED, nullptr);

  auto box = [&]() {
    lv_obj_t *o = lv_obj_create(list);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(o, 8, 0);
    lv_obj_set_style_pad_row(o, 4, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
  };
  lv_list_add_text(list, "Airliners within 10 nm now");
  flLiveBox = box();
  lv_list_add_text(list, "Live ADS-B data: adsb.fi (adsb.fi)");
  lv_list_add_text(list, "Routes: adsbdb.com (community data)");

  load(scr, Screen::Flights);
  weather::flightsKeepAlive();
  if (weather::liveFlights().empty()) weather::refreshFlights();
  flKey = -1;
  refreshFlightsScreen();
}

void showAirportCode() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Airport code", [](lv_event_t *) { showFlights(); });
  flTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(flTa, true);
  lv_textarea_set_accepted_chars(flTa, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
  lv_textarea_set_max_length(flTa, 4);
  lv_textarea_set_placeholder_text(flTa, "e.g. DAB, MCO or KJFK");
  lv_textarea_set_text(flTa, weather::airportCode().c_str());
  lv_obj_set_width(flTa, W() - 8);
  lv_obj_align(flTa, LV_ALIGN_TOP_MID, 0, HEADER_H + 4);
  lv_obj_add_state(flTa, LV_STATE_FOCUSED);
  int kbH = H() - HEADER_H - 50;
  if (kbH > 170) kbH = 170;
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
  lv_obj_set_size(kb, W(), kbH);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, flTa);
  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        if (c == LV_EVENT_CANCEL) {
          showFlights();
          return;
        }
        if (c != LV_EVENT_READY) return;
        String v = lv_textarea_get_text(flTa);
        v.trim();
        if (v.length() < 3 || v.length() > 4) {
          errorBox("Airport code", "Enter a 3-letter code (DAB) or a 4-letter ICAO code (KDAB).");
          return;
        }
        weather::setAirportCode(v);
        showFlights();
      },
      LV_EVENT_ALL, nullptr);
  load(scr, Screen::AirportCode);
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
    if (net::dualBand()) {
      const char *bands = ap.bands == (net::BAND_24 | net::BAND_5) ? "2.4+5G"
                          : ap.bands == net::BAND_5                 ? "5G"
                                                                    : "2.4G";
      lv_label_set_text_fmt(r, "%s %s %d%%", ap.secure ? "WPA" : "open", bands,
                            net::signalPercent(ap.rssi));
    } else {
      lv_label_set_text_fmt(r, "%s %d%%", ap.secure ? "WPA" : "open",
                            net::signalPercent(ap.rssi));
    }
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
      if (net::dualBand())
        snprintf(buf, sizeof(buf), "Connected: %s, %s GHz (%s)", net::ssid().c_str(),
                 net::band() == 5 ? "5" : "2.4", net::ip().c_str());
      else
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

  // Core 0 runs Wi-Fi and Bluetooth, core 1 the UI and app loop
  int c0 = sysmon::cpuLoad(0), c1 = sysmon::cpuLoad(1);
  if (c0 < 0) snprintf(buf, sizeof(buf), "CPU load: measuring...");
  else if (c1 < 0) snprintf(buf, sizeof(buf), "CPU load: %d%% (single core)", c0);
  else snprintf(buf, sizeof(buf), "CPU load: %d%% radio core, %d%% app core", c0, c1);
  setText(aboutCpu, buf);
  int worst = c0 > c1 ? c0 : c1;
  lv_obj_set_style_text_color(aboutCpu,
                              worst >= 90   ? lv_palette_main(LV_PALETTE_RED)
                              : worst >= 70 ? lv_palette_main(LV_PALETTE_AMBER)
                                            : theme::accent(),
                              0);
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
  String wifiTxt = up ? net::ssid() : String("not connected");
  if (up) wifiTxt += String(" (") + (net::band() == 5 ? "5" : "2.4") + " GHz ch " + net::channel() + ")";
  uint16_t rev = ESP.getChipRevision();  // e.g. 301 = v3.1
  snprintf(buf, sizeof(buf),
           "Firmware: v%s (%s)\n"
           "Board: %s\n"
           "Chip: %s v%u.%u\n"
           "CPU: %d core%s @ %lu MHz\n"
           "Flash: %lu KB, PSRAM: %lu KB\n"
           "Free RAM: %lu KB (PSRAM %lu KB)\n"
           "MAC: %s\n"
           "Wi-Fi: %s\n"
           "IP: %s\n"
           "OTA: %s",
           FW_VERSION, __DATE__, BOARD_NAME, ESP.getChipModel(), rev / 100, rev % 100, ESP.getChipCores(),
           ESP.getChipCores() == 1 ? "" : "s", (unsigned long)ESP.getCpuFreqMHz(),
           (unsigned long)(ESP.getFlashChipSize() / 1024), (unsigned long)(ESP.getPsramSize() / 1024),
           (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024),
           WiFi.macAddress().c_str(),
           wifiTxt.c_str(), up ? net::ip().c_str() : "-",
           ota::ready() ? (String(ota::hostname()) + ".local").c_str() : "waiting for Wi-Fi");

  // Product name and slogan
  lv_obj_t *pn = lv_label_create(box);
  lv_obj_set_style_text_font(pn, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(pn, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(pn, LV_PCT(100));
  lv_label_set_text(pn, PRODUCT_NAME);
  lv_obj_t *tl = lv_label_create(box);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(tl, LV_PCT(100));
  lv_obj_set_style_text_opa(tl, LV_OPA_70, 0);
  lv_label_set_text(tl, PRODUCT_TAGLINE);

  aboutUptime = lv_label_create(box);  // updated live by the UI poll
  lv_obj_set_style_text_color(aboutUptime, theme::accent(), 0);
  lv_label_set_text(aboutUptime, "");
  aboutCpu = lv_label_create(box);
  lv_label_set_long_mode(aboutCpu, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(aboutCpu, LV_PCT(100));
  lv_label_set_text(aboutCpu, "");

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

void showNextVolcanoAlert() {
  String title, msg;
  if (!weather::nextVolcanoPopup(title, msg)) return;
  static const char *btns[] = {"Close", "View", ""};
  String t = String(LV_SYMBOL_WARNING " ") + title;
  volcAlertBox = lv_msgbox_create(nullptr, t.c_str(), msg.c_str(), btns, false);
  lv_obj_set_width(volcAlertBox, W() - 24);
  lv_obj_center(volcAlertBox);
  lv_obj_add_event_cb(
      volcAlertBox,
      [](lv_event_t *e) {
        lv_obj_t *box = lv_event_get_current_target(e);
        bool view = lv_msgbox_get_active_btn(box) == 1;
        lv_msgbox_close_async(box);
        volcAlertBox = nullptr;
        if (view) {
          volcBack = Screen::Desktop;
          showVolcanoes();
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
  if (!droneAlertBox && !wxAlertBox && !volcAlertBox) showNextDroneAlert();
  if (!droneAlertBox && !wxAlertBox && !volcAlertBox) showNextWxAlert();
  if (!droneAlertBox && !wxAlertBox && !volcAlertBox) showNextVolcanoAlert();

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
    case Screen::Stock:
    case Screen::StockDetail: refreshStockScreen(); break;
    case Screen::Nfl: refreshNflScreen(); break;
    case Screen::Quakes: refreshQuakeScreen(); break;
    case Screen::Volcanoes: refreshVolcanoScreen(); break;
    case Screen::Flights: refreshFlightsScreen(); break;
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
