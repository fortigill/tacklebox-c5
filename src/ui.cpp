#include "ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include <vector>

#include "net.h"

// Note: the built-in Montserrat fonts only contain ASCII + LV_SYMBOL_* icons,
// so keep UI strings ASCII-only.

namespace {

enum class Screen { Desktop, Menu, Wifi, Password, Connecting, About };

const int W = 320;
const int H = 240;
const int HEADER_H = 40;

Screen cur = Screen::Desktop;
Screen wifiBack = Screen::Menu;  // where "back" goes from Wi-Fi settings

// Widgets of the current screen (reset whenever the screen changes)
lv_obj_t *deskWifiIcon = nullptr;
lv_obj_t *deskWifiLbl = nullptr;
lv_obj_t *wifiStatusLbl = nullptr;
lv_obj_t *wifiList = nullptr;
lv_obj_t *wifiScanBtn = nullptr;
lv_obj_t *wifiForgetBtn = nullptr;
lv_obj_t *pwTa = nullptr;

net::ScanState lastScanSt = net::ScanState::Idle;
std::vector<String> listSsids;
std::vector<bool> listSecure;
String targetSsid;
bool targetSecure = true;

void showDesktop();
void showMenu();
void showWifi();
void showPassword();
void showConnecting();
void showAbout();

// ---------- helpers ----------

void resetPtrs() {
  deskWifiIcon = deskWifiLbl = nullptr;
  wifiStatusLbl = wifiList = wifiScanBtn = wifiForgetBtn = nullptr;
  pwTa = nullptr;
}

void setText(lv_obj_t *lbl, const char *txt) {
  if (lbl && strcmp(lv_label_get_text(lbl), txt) != 0) lv_label_set_text(lbl, txt);
}

lv_obj_t *newScreen() {
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

lv_obj_t *makeHeader(lv_obj_t *scr, const char *title, lv_event_cb_t backCb) {
  lv_obj_t *h = lv_obj_create(scr);
  lv_obj_set_size(h, W, HEADER_H);
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
  lv_obj_set_width(t, 170);
  lv_label_set_text(t, title);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, titleX, 0);
  return h;
}

void errorBox(const char *title, const char *msg) {
  lv_obj_t *m = lv_msgbox_create(nullptr, title, msg, nullptr, true);  // modal, on top layer
  lv_obj_set_width(m, 260);
  lv_obj_center(m);
}

// ---------- Desktop ----------

void refreshDesktop() {
  if (!deskWifiIcon) return;
  char buf[96];
  lv_color_t color;
  lv_opa_t opa = LV_OPA_COVER;

  switch (net::state()) {
    case net::State::Connected:
      color = lv_palette_main(LV_PALETTE_GREEN);
      snprintf(buf, sizeof(buf), "%s  |  %s", net::ssid().c_str(), net::ip().c_str());
      break;
    case net::State::Connecting:
      color = lv_palette_main(LV_PALETTE_AMBER);
      snprintf(buf, sizeof(buf), "Connecting to %s...", net::ssid().c_str());
      break;
    case net::State::Lost:
      color = lv_palette_main(LV_PALETTE_AMBER);
      snprintf(buf, sizeof(buf), "Reconnecting to %s...", net::ssid().c_str());
      break;
    default:
      color = lv_palette_main(LV_PALETTE_GREY);
      opa = LV_OPA_40;
      snprintf(buf, sizeof(buf), "Wi-Fi not connected");
      break;
  }
  lv_obj_set_style_text_color(deskWifiIcon, color, 0);
  lv_obj_set_style_text_opa(deskWifiIcon, opa, 0);
  setText(deskWifiLbl, buf);
}

void showDesktop() {
  lv_obj_t *scr = newScreen();

  // Status bar
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, W, 36);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 2, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *name = lv_label_create(bar);
  lv_label_set_text(name, "CYD");
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
  lv_obj_set_size(list, W, H - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, "Network settings");
  lv_obj_add_event_cb(b, [](lv_event_t *) { wifiBack = Screen::Menu; showWifi(); },
                      LV_EVENT_CLICKED, nullptr);

  b = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "About this device");
  lv_obj_add_event_cb(b, [](lv_event_t *) { showAbout(); }, LV_EVENT_CLICKED, nullptr);

  load(scr, Screen::Menu);
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

  if (net::savedSsid().length()) lv_obj_clear_flag(wifiForgetBtn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(wifiForgetBtn, LV_OBJ_FLAG_HIDDEN);

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
  lv_obj_t *hdr = makeHeader(scr, "Wi-Fi", [](lv_event_t *) {
    if (wifiBack == Screen::Desktop) showDesktop();
    else showMenu();
  });

  wifiScanBtn = flatBtn(hdr, LV_SYMBOL_REFRESH " Scan", 84, 32);
  lv_obj_align(wifiScanBtn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(wifiScanBtn, [](lv_event_t *) { net::startScan(); }, LV_EVENT_CLICKED,
                      nullptr);

  wifiStatusLbl = lv_label_create(scr);
  lv_label_set_long_mode(wifiStatusLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(wifiStatusLbl, 230);
  lv_label_set_text(wifiStatusLbl, "");
  lv_obj_align(wifiStatusLbl, LV_ALIGN_TOP_LEFT, 8, HEADER_H + 9);

  wifiForgetBtn = flatBtn(scr, "Forget", 70, 26);
  lv_obj_align(wifiForgetBtn, LV_ALIGN_TOP_RIGHT, -4, HEADER_H + 3);
  lv_obj_set_style_bg_color(wifiForgetBtn, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(wifiForgetBtn, [](lv_event_t *) { net::forget(); }, LV_EVENT_CLICKED,
                      nullptr);

  const int listY = HEADER_H + 32;
  wifiList = lv_list_create(scr);
  lv_obj_set_size(wifiList, W, H - listY);
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
  lv_obj_set_width(pwTa, W - 8 - 56);
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

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, W, H - HEADER_H - 50);
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
  lv_obj_set_width(l, W - 20);
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

  char buf[320];
  bool up = net::state() == net::State::Connected;
  snprintf(buf, sizeof(buf),
           "Chip:   %s rev %d, %d cores @ %lu MHz\n"
           "Flash:  %lu KB\n"
           "Heap:   %lu KB free\n"
           "MAC:    %s\n"
           "Wi-Fi:  %s\n"
           "IP:     %s",
           ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
           (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)(ESP.getFlashChipSize() / 1024),
           (unsigned long)(ESP.getFreeHeap() / 1024), WiFi.macAddress().c_str(),
           up ? net::ssid().c_str() : "not connected", up ? net::ip().c_str() : "-");

  lv_obj_t *l = lv_label_create(scr);
  lv_label_set_text(l, buf);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 10, HEADER_H + 10);

  load(scr, Screen::About);
}

// ---------- refresh timer ----------

void poll(lv_timer_t *) {
  switch (cur) {
    case Screen::Desktop: refreshDesktop(); break;
    case Screen::Wifi: refreshWifi(); break;
    case Screen::Connecting: pollConnecting(); break;
    default: break;
  }
}

}  // namespace

void ui::begin() {
  lv_timer_create(poll, 300, nullptr);
  showDesktop();
}
