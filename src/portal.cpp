#include "portal.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "board.h"
#include "net.h"
#include "ota.h"
#include "version.h"
#include "weather.h"

namespace {

const uint32_t PAIR_MS = 10UL * 60 * 1000;  // a pairing code lasts 10 minutes at most
const int PAIR_MAX_FAILS = 5;               // wrong codes before the code is withdrawn
const size_t FIELD_MAX = 200;

WebServer server(80);
bool started = false;
char code[7] = "";
bool active = false;
uint32_t pairStart = 0;
int fails = 0;
uint32_t saves = 0;  // counts successful saves from the web

String esc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

// Same colors as the Miami Vice theme
const char CSS[] =
    "body{background:#1A0B2E;color:#FDF0FF;font-family:-apple-system,system-ui,Segoe UI,Roboto,sans-serif;"
    "margin:0}"
    ".wrap{max-width:520px;margin:0 auto;padding:18px}"
    "h1{color:#FF2E97;font-size:1.4rem;margin:0}"
    "h2{font-size:1.05rem;margin:0 0 10px;color:#00E5D4}"
    ".tag{color:#00E5D4;margin:4px 0 16px}"
    ".card{background:#2D1650;border-radius:12px;padding:14px 16px;margin-bottom:14px}"
    "table{width:100%;border-collapse:collapse}"
    "td{padding:5px 0;vertical-align:top;word-break:break-word}"
    "td:first-child{color:#c9b6e4;width:38%;padding-right:8px}"
    ".btn{display:block;box-sizing:border-box;text-align:center;background:#FF2E97;color:#fff;"
    "text-decoration:none;font-weight:600;padding:12px;border-radius:10px;border:0;width:100%;font-size:1rem;"
    "cursor:pointer}"
    ".btn2{background:transparent;border:1px solid #FF2E97;color:#FF2E97;margin-top:10px}"
    "label{display:block;margin:12px 0 4px;color:#c9b6e4}"
    "input{width:100%;box-sizing:border-box;padding:11px;border-radius:8px;border:1px solid #5a3a8a;"
    "background:#1A0B2E;color:#FDF0FF;font-size:1rem}"
    ".row{display:flex;gap:8px}.row input{flex:1}"
    ".show{width:auto;padding:0 12px;border-radius:8px;border:1px solid #5a3a8a;background:#2D1650;color:#c9b6e4}"
    ".ok{color:#00E5D4;font-weight:600}.err{color:#ff6b8a;font-weight:600}"
    ".note{font-size:.85rem;color:#c9b6e4;line-height:1.4}"
    "footer{text-align:center;color:#8d77ad;font-size:.8rem;margin-top:10px}";

String page(const char *title, const String &body) {
  String h;
  h.reserve(2500 + body.length());
  h += "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'><title>";
  h += title;
  h += " - " PRODUCT_NAME "</title><style>";
  h += CSS;
  h += "</style></head><body><div class='wrap'><h1>" PRODUCT_NAME "</h1><p class='tag'>" PRODUCT_TAGLINE "</p>";
  h += body;
  h += "<footer>v" FW_VERSION " &middot; " BOARD_NAME "</footer></div></body></html>";
  return h;
}

String row(const char *k, const String &v) { return String("<tr><td>") + k + "</td><td>" + esc(v) + "</td></tr>"; }

String uptime() {
  uint32_t s = millis() / 1000;
  char b[32];
  snprintf(b, sizeof(b), "%lud %02lu:%02lu:%02lu", (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24),
           (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  return b;
}

// ---- / : About this device ----
void handleRoot() {
  bool up = net::state() == net::State::Connected;
  String wifi = up ? net::ssid() : String("not connected");
  if (up) wifi += String(" (") + (net::band() == 5 ? "5" : "2.4") + " GHz, ch " + net::channel() + ")";
  uint16_t rev = ESP.getChipRevision();
  char chip[48];
  snprintf(chip, sizeof(chip), "%s v%u.%u, %d core%s @ %lu MHz", ESP.getChipModel(), rev / 100, rev % 100,
           ESP.getChipCores(), ESP.getChipCores() == 1 ? "" : "s", (unsigned long)ESP.getCpuFreqMHz());
  char mem[64];
  snprintf(mem, sizeof(mem), "%lu KB flash, %lu KB PSRAM", (unsigned long)(ESP.getFlashChipSize() / 1024),
           (unsigned long)(ESP.getPsramSize() / 1024));
  char freeMem[48];
  snprintf(freeMem, sizeof(freeMem), "%lu KB (PSRAM %lu KB)", (unsigned long)(ESP.getFreeHeap() / 1024),
           (unsigned long)(ESP.getFreePsram() / 1024));

  String b = "<div class='card'><h2>About this device</h2><table>";
  b += row("Firmware", String("v") + FW_VERSION + " (" + __DATE__ + ")");
  b += row("Board", BOARD_NAME);
  b += row("Chip", chip);
  b += row("Memory", mem);
  b += row("Free RAM", freeMem);
  b += row("Uptime", uptime());
  b += row("MAC", WiFi.macAddress());
  b += row("Wi-Fi", wifi);
  b += row("IP", up ? net::ip() : String("-"));
  b += row("Address", ota::ready() ? String(ota::hostname()) + ".local" : String("-"));
  b += "</table></div>";
  b += "<div class='card'><h2>Settings</h2><a class='btn' href='/yolink'>YoLink configuration</a></div>";
  server.send(200, "text/html", page("About", b));
}

// ---- /yolink : enter the two keys ----
String yolinkForm(const String &msg, bool err, const String &prefillCode, bool form = true) {
  String uaid = weather::yoUaid();
  String b;
  if (msg.length()) b += String("<div class='card'><p class='") + (err ? "err" : "ok") + "'>" + esc(msg) + "</p></div>";
  b += "<div class='card'><h2>YoLink configuration</h2><table>";
  b += row("UAID", uaid.length() ? "saved (" + uaid.substring(0, 6) + "...)" : String("not set"));
  b += row("Secret Key", weather::yoHasSecret() ? "saved" : "not set");
  String st = weather::yoStatusText();
  int nl = st.indexOf('\n');
  b += row("Status", nl > 0 ? st.substring(0, nl) : st);
  b += "</table></div>";
  if (!form) return page("YoLink", b + "<a class='btn btn2' href='/'>Back</a>");

  b += "<div class='card'><form method='post' action='/yolink' autocomplete='off'>";
  if (!portal::pairingActive())
    b += "<p class='note'>On the Tackle Box, open <b>Menu &gt; Web setup</b> (or <b>YoLink &gt; Set up from "
         "phone</b>) to get a pairing code.</p>";
  b += "<label for='code'>Pairing code (shown on the Tackle Box)</label>"
       "<input id='code' name='code' inputmode='numeric' pattern='[0-9]{6}' maxlength='6' required value='";
  b += esc(prefillCode);
  b += "'><label for='uaid'>UAID</label><input id='uaid' name='uaid' maxlength='200' "
       "placeholder='Paste the UAID from the YoLink app' spellcheck='false' autocapitalize='off'>"
       "<label for='secret'>Secret Key</label><div class='row'>"
       "<input id='secret' name='secret' type='password' maxlength='200' placeholder='Paste the Secret Key' "
       "spellcheck='false' autocapitalize='off'>"
       "<button type='button' class='show' onclick=\"var s=document.getElementById('secret');"
       "s.type=s.type=='password'?'text':'password';this.textContent=s.type=='password'?'Show':'Hide'\">Show"
       "</button></div>"
       "<p class='note'>Find both in the YoLink app: <b>Account &gt; Advanced Settings &gt; Personal Access "
       "Credentials</b>. Leave a box empty to keep what's saved.</p>"
       "<button class='btn' type='submit'>Save</button></form></div>"
       "<p class='note'>This page uses plain http on your home network, like most router and printer setup "
       "pages. The pairing code makes sure only someone at the Tackle Box can change its settings.</p>"
       "<a class='btn btn2' href='/'>Back</a>";
  return page("YoLink", b);
}

String digitsOnly(const String &s) {
  String o;
  for (size_t i = 0; i < s.length() && o.length() < 6; i++)
    if (isdigit((unsigned char)s[i])) o += s[i];
  return o;
}

void handleYolinkGet() { server.send(200, "text/html", yolinkForm("", false, digitsOnly(server.arg("code")))); }

void handleYolinkPost() {
  String c = digitsOnly(server.arg("code"));
  String uaid = server.arg("uaid");
  String secret = server.arg("secret");
  uaid.trim();
  secret.trim();
  if (!portal::pairingActive()) {
    server.send(403, "text/html",
                yolinkForm("No pairing code is active. Open Web setup on the Tackle Box and try again.", true, ""));
    return;
  }
  // Compare without stopping at the first difference
  uint8_t diff = c.length() != 6;
  for (int i = 0; i < 6; i++) diff |= (uint8_t)((i < (int)c.length() ? c[i] : 0) ^ code[i]);
  if (diff) {
    if (++fails >= PAIR_MAX_FAILS) {
      portal::stopPairing();
      Serial.println("[portal] too many wrong pairing codes; code withdrawn");
      server.send(403, "text/html",
                  yolinkForm("Too many wrong codes. Open Web setup on the Tackle Box again for a new code.", true, ""));
    } else {
      server.send(403, "text/html", yolinkForm("That pairing code doesn't match the one on the Tackle Box.", true, ""));
    }
    return;
  }
  if (uaid.isEmpty() && secret.isEmpty()) {
    server.send(400, "text/html", yolinkForm("Paste the UAID and/or Secret Key first.", true, c));
    return;
  }
  if (uaid.length() > FIELD_MAX || secret.length() > FIELD_MAX) {
    server.send(400, "text/html", yolinkForm("That's too long to be a YoLink key.", true, c));
    return;
  }
  if (uaid.length()) weather::setYoUaid(uaid);
  if (secret.length()) weather::setYoSecret(secret);
  saves++;
  portal::stopPairing();  // one save per code
  Serial.printf("[portal] YoLink keys saved from %s\n", server.client().remoteIP().toString().c_str());
  server.send(200, "text/html",
              yolinkForm("Saved! The Tackle Box is signing in to YoLink now; its YoLink page fills in over the next "
                         "minute or two.",
                         false, "", false));
}

void handleNotFound() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

}  // namespace

namespace portal {

void loop() {
  if (!started) {
    if (net::state() != net::State::Connected) return;
    server.on("/", HTTP_GET, handleRoot);
    server.on("/yolink", HTTP_GET, handleYolinkGet);
    server.on("/yolink", HTTP_POST, handleYolinkPost);
    server.onNotFound(handleNotFound);
    server.begin();
    started = true;
    if (ota::ready()) MDNS.addService("http", "tcp", 80);
    Serial.printf("[portal] web setup at http://%s/\n", net::ip().c_str());
  }
  server.handleClient();
}

bool running() { return started; }

String baseUrl() {
  if (net::state() != net::State::Connected) return "";
  return "http://" + net::ip();
}

void startPairing() {
  snprintf(code, sizeof(code), "%06lu", (unsigned long)(esp_random() % 1000000UL));
  active = true;
  pairStart = millis();
  fails = 0;
}

void stopPairing() {
  active = false;
  memset(code, 0, sizeof(code));
}

bool pairingActive() {
  if (active && millis() - pairStart > PAIR_MS) stopPairing();
  return active;
}

String pairingCode() { return pairingActive() ? String(code) : String(""); }

uint32_t pairingSecondsLeft() {
  if (!pairingActive()) return 0;
  return (PAIR_MS - (millis() - pairStart)) / 1000;
}

String setupUrl() {
  String b = baseUrl();
  if (b.isEmpty() || !pairingActive()) return "";
  return b + "/yolink?code=" + code;
}

uint32_t savedAt() { return saves; }

}  // namespace portal
