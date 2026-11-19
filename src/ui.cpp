#include "ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include <vector>

#include "ble.h"
#include "display.h"
#include "net.h"
#include "version.h"

// Note: the built-in Montserrat fonts only contain ASCII + LV_SYMBOL_* icons,
// so keep UI strings ASCII-only.

namespace {

enum class Screen {
  Desktop, Menu, Wifi, Password, Connecting, About, Display, Calibrate, Ble, BleDetail
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
lv_obj_t *wifiStatusLbl = nullptr;
lv_obj_t *wifiList = nullptr;
lv_obj_t *wifiScanBtn = nullptr;
lv_obj_t *wifiForgetBtn = nullptr;
lv_obj_t *pwTa = nullptr;
lv_obj_t *calH = nullptr, *calV = nullptr, *calRing = nullptr, *calMsg = nullptr;
lv_obj_t *bleList = nullptr, *bleStatus = nullptr, *bleToggle = nullptr, *bleDetail = nullptr;

// BLE scanner state
const uint32_t BLE_LIST_REFRESH_MS = 2000;
const size_t BLE_MAX_ROWS = 30;
const uint32_t BLE_MIN_FREE_HEAP = 20000;  // stop adding rows below this
std::vector<String> bleRowAddrs;
String bleDetailAddr;
uint32_t bleLastFill = 0;

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

// ---------- helpers ----------

void resetPtrs() {
  deskWifiIcon = deskWifiLbl = nullptr;
  wifiStatusLbl = wifiList = wifiScanBtn = wifiForgetBtn = nullptr;
  pwTa = nullptr;
  calH = calV = calRing = calMsg = nullptr;
  bleList = bleStatus = bleToggle = bleDetail = nullptr;
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

lv_obj_t *newScreen() {
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
  lv_obj_set_width(t, W() - titleX - 12 - rightReserve);
  lv_label_set_text(t, title);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, titleX, 0);
  return h;
}

void errorBox(const char *title, const char *msg) {
  lv_obj_t *m = lv_msgbox_create(nullptr, title, msg, nullptr, true);  // modal, on top layer
  lv_obj_set_width(m, W() - 40);
  lv_obj_center(m);
}

// ---------- Desktop ----------

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
      snprintf(buf, sizeof(buf), "Wi-Fi not connected");
      break;
  }
  setText(deskWifiLbl, buf);

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

  // Status bar
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, W(), 36);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 2, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *name = lv_label_create(bar);
  lv_label_set_text(name, "CYD  v" FW_VERSION);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 6, 0);

  // Wi-Fi icon: a transparent button so it's an easy stylus target
  lv_obj_t *wb = lv_btn_create(bar);
  lv_obj_set_size(wb, 52, 32);
  lv_obj_align(wb, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(wb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(wb, 0, 0);
  lv_obj_set_style_bg_opa(wb, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_add_event_cb(wb, [](lv_event_t *) { wifiBack = Screen::Desktop; showWifi(); },
                      LV_EVENT_CLICKED, nullptr);
  deskWifiIcon = lv_label_create(wb);
  lv_obj_set_style_text_font(deskWifiIcon, &lv_font_montserrat_20, 0);
  lv_label_set_text(deskWifiIcon, LV_SYMBOL_WIFI);
  lv_obj_center(deskWifiIcon);

  // Menu button
  lv_obj_t *mb = flatBtn(scr, LV_SYMBOL_LIST "  Menu", 170, 70);
  lv_obj_set_style_text_font(lv_obj_get_child(mb, 0), &lv_font_montserrat_20, 0);
  lv_obj_align(mb, LV_ALIGN_CENTER, 0, 4);
  lv_obj_add_event_cb(mb, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);

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

void showMenu() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "Menu", [](lv_event_t *) { showDesktop(); });

  lv_obj_t *list = lv_list_create(scr);
  lv_obj_set_size(list, W(), H() - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, "Network settings");
  lv_obj_add_event_cb(b, [](lv_event_t *) { wifiBack = Screen::Menu; showWifi(); },
                      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "Bluetooth scanner");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showBle(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Display & touch");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showDisplay(); }, LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "About this device");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showAbout(); }, LV_EVENT_CLICKED, nullptr);

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
  const char *v = d.hasMfg ? ble::vendorName(d.mfgId) : nullptr;
  if (v) return String(v) + " device";
  return d.addr;
}

void bleUpdateStatus(size_t count) {
  if (!bleStatus) return;
  char buf[64];
  snprintf(buf, sizeof(buf), "%s  -  %u device%s", ble::scanning() ? "Scanning" : "Stopped",
           (unsigned)count, count == 1 ? "" : "s");
  setText(bleStatus, buf);
  setText(lv_obj_get_child(bleToggle, 0), ble::scanning() ? "Stop" : "Start");
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
  bleUpdateStatus(total);

  if (devs.empty()) {
    lv_list_add_text(bleList, ble::scanning() ? "Listening for devices..." : "No devices");
    return;
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
  lv_obj_scroll_to_y(bleList, y, LV_ANIM_OFF);
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

  bleToggle = flatBtn(hdr, "Stop", btnW, 32);
  lv_obj_align(bleToggle, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(
      bleToggle,
      [](lv_event_t *) {
        if (ble::scanning()) ble::stop();
        else ble::start();
        bleUpdateStatus(bleRowAddrs.size());
        bleFill();
      },
      LV_EVENT_CLICKED, nullptr);

  bleStatus = lv_label_create(scr);
  lv_label_set_text(bleStatus, "Starting Bluetooth...");
  lv_obj_align(bleStatus, LV_ALIGN_TOP_LEFT, 8, HEADER_H + 7);

  const int listY = HEADER_H + 28;
  bleList = lv_list_create(scr);
  lv_obj_set_size(bleList, W(), H() - listY);
  lv_obj_align(bleList, LV_ALIGN_TOP_MID, 0, listY);

  load(scr, Screen::Ble);

  // Bluetooth init blocks for a moment, so let the screen draw first
  lv_timer_t *t = lv_timer_create(
      [](lv_timer_t *) {
        if (!ble::start()) {
          setText(bleStatus, "Bluetooth failed to start");
          return;
        }
        bleFill();
      },
      50, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

void refreshBleDetail() {
  if (!bleDetail) return;
  ble::Device d;
  if (!ble::find(bleDetailAddr, d)) {
    setText(bleDetail, "Device no longer seen.");
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
           "Last seen: %lus ago\n"
           "First seen: %lus ago\n"
           "Adverts: %lu",
           d.name.length() ? d.name.c_str() : "-", d.addr.c_str(),
           d.randomAddr ? "random" : "public", d.rssi, tx, mfg,
           d.service.length() ? d.service.c_str() : "-",
           (unsigned long)((millis() - d.lastSeen) / 1000),
           (unsigned long)((millis() - d.firstSeen) / 1000), (unsigned long)d.seenCount);
  setText(bleDetail, buf);
}

void showBleDetail() {
  lv_obj_t *scr = newScreen();
  ble::Device d;
  String title = ble::find(bleDetailAddr, d) ? bleTitle(d) : bleDetailAddr;
  makeHeader(scr, title.c_str(), [](lv_event_t *) { showBle(); });

  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H);
  lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);

  bleDetail = lv_label_create(box);
  lv_label_set_long_mode(bleDetail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(bleDetail, LV_PCT(100));
  lv_label_set_text(bleDetail, "");

  load(scr, Screen::BleDetail);
  refreshBleDetail();
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

  wifiStatusLbl = lv_label_create(scr);
  lv_label_set_long_mode(wifiStatusLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(wifiStatusLbl, W() - 90);
  lv_label_set_text(wifiStatusLbl, "");
  lv_obj_align(wifiStatusLbl, LV_ALIGN_TOP_LEFT, 8, HEADER_H + 9);

  wifiForgetBtn = flatBtn(scr, "Forget", 70, 26);
  lv_obj_align(wifiForgetBtn, LV_ALIGN_TOP_RIGHT, -4, HEADER_H + 3);
  lv_obj_set_style_bg_color(wifiForgetBtn, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(wifiForgetBtn, [](lv_event_t *) { net::forget(); }, LV_EVENT_CLICKED,
                      nullptr);

  const int listY = HEADER_H + 32;
  wifiList = lv_list_create(scr);
  lv_obj_set_size(wifiList, W(), H() - listY);
  lv_obj_align(wifiList, LV_ALIGN_TOP_MID, 0, listY);

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

void showAbout() {
  lv_obj_t *scr = newScreen();
  makeHeader(scr, "About", [](lv_event_t *) { showMenu(); });

  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, W(), H() - HEADER_H);
  lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, 0);
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
           "IP: %s",
           FW_VERSION, __DATE__, ESP.getChipModel(), rev / 100, rev % 100, ESP.getChipCores(),
           (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)(ESP.getFlashChipSize() / 1024),
           (unsigned long)(ESP.getFreeHeap() / 1024), WiFi.macAddress().c_str(),
           up ? net::ssid().c_str() : "not connected", up ? net::ip().c_str() : "-");

  lv_obj_t *l = lv_label_create(box);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_text(l, buf);

  load(scr, Screen::About);
}

// ---------- refresh timer ----------

void poll(lv_timer_t *) {
  switch (cur) {
    case Screen::Desktop: refreshDesktop(); break;
    case Screen::Wifi: refreshWifi(); break;
    case Screen::Connecting: pollConnecting(); break;
    case Screen::Ble:
      if (millis() - bleLastFill >= BLE_LIST_REFRESH_MS) bleFill();
      break;
    case Screen::BleDetail: refreshBleDetail(); break;
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
