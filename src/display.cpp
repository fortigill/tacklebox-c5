#include "display.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <math.h>

// Touch controller sits on its own SPI bus on the CYD
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

namespace {

// Backlight PWM. 20 kHz is far above anything the eye (or a phone camera)
// can see, so dimming adds no flicker.
const uint8_t BL_PIN = 21;
const uint32_t BL_FREQ = 20000;
const uint8_t BL_BITS = 8;

// Panel's native (rotation 0, portrait) resolution
const int16_t NATIVE_W = 240;
const int16_t NATIVE_H = 320;

const uint32_t BUF_PX = 320 * 24;
const uint16_t TS_MIN_PRESSURE = 200;
const float CAL_MAX_ERR_PX = 20.0f;
const uint32_t CAL_MAGIC = 0xCA1B0001;

// Affine map from raw touch to native panel coordinates:
//   nx = a*rx + b*ry + c,  ny = d*rx + e*ry + f
// Stored in native coordinates so one calibration works in every rotation.
struct Cal {
  uint32_t magic;
  float a, b, c, d, e, f;
};

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
Preferences prefs;

lv_disp_draw_buf_t drawBuf;
lv_color_t *buf1 = nullptr;
lv_color_t *buf2 = nullptr;
lv_disp_drv_t dispDrv;
lv_disp_t *disp = nullptr;
lv_indev_drv_t indevDrv;

uint8_t rot = display::LANDSCAPE;
bool swapRB = false;
uint8_t blPct = 100;
Cal cal{};
bool calValid = false;

// Native panel coords <-> screen coords for a TFT_eSPI rotation
void toScreen(uint8_t r, float nx, float ny, float &sx, float &sy) {
  switch (r) {
    case 1:  sx = ny;                sy = NATIVE_W - 1 - nx; break;
    case 2:  sx = NATIVE_W - 1 - nx; sy = NATIVE_H - 1 - ny; break;
    case 3:  sx = NATIVE_H - 1 - ny; sy = nx;                break;
    default: sx = nx;                sy = ny;                break;
  }
}

void toNative(uint8_t r, float sx, float sy, float &nx, float &ny) {
  switch (r) {
    case 1:  nx = NATIVE_W - 1 - sy; ny = sx;                break;
    case 2:  nx = NATIVE_W - 1 - sx; ny = NATIVE_H - 1 - sy; break;
    case 3:  nx = sy;                ny = NATIVE_H - 1 - sx; break;
    default: nx = sx;                ny = sy;                break;
  }
}

uint8_t clampBrightness(int pct) {
  if (pct < display::MIN_BRIGHTNESS) return display::MIN_BRIGHTNESS;
  if (pct > 100) return 100;
  return (uint8_t)pct;
}

void applyBrightness() {
  ledcWrite(BL_PIN, (uint32_t)blPct * ((1u << BL_BITS) - 1) / 100);
}

void applyRotation() {
  tft.setRotation(rot);
  bool landscape = rot & 1;
  dispDrv.hor_res = landscape ? NATIVE_H : NATIVE_W;
  dispDrv.ver_res = landscape ? NATIVE_W : NATIVE_H;
}

void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  uint32_t w = a->x2 - a->x1 + 1;
  uint32_t h = a->y2 - a->y1 + 1;
  if (swapRB) {  // RGB565: exchange the 5-bit red and blue fields
    uint16_t *p = (uint16_t *)&px->full;
    for (uint32_t i = 0, n = w * h; i < n; i++) {
      uint16_t c = p[i];
      p[i] = ((c & 0x001F) << 11) | (c & 0x07E0) | (c >> 11);
    }
  }
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t *)&px->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  int16_t rx, ry;
  if (calValid && display::readRaw(rx, ry)) {
    float nx = cal.a * rx + cal.b * ry + cal.c;
    float ny = cal.d * rx + cal.e * ry + cal.f;
    float sx, sy;
    toScreen(rot, nx, ny, sx, sy);
    data->point.x = constrain((int)lroundf(sx), 0, dispDrv.hor_res - 1);
    data->point.y = constrain((int)lroundf(sy), 0, dispDrv.ver_res - 1);
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Solves a 3x3 linear system with Cramer's rule
bool solve3(const double m[3][3], const double v[3], double out[3]) {
  auto det = [](const double a[3][3]) {
    return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
           a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
  };
  double d = det(m);
  if (fabs(d) < 1e-9) return false;
  for (int c = 0; c < 3; c++) {
    double t[3][3];
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) t[i][j] = (j == c) ? v[i] : m[i][j];
    out[c] = det(t) / d;
  }
  return true;
}

}  // namespace

void display::begin() {
  prefs.begin("disp", false);  // read-write: creates the namespace on first boot
  rot = prefs.getUChar("rot", LANDSCAPE) & 3;
  swapRB = prefs.getBool("swaprb", false);
  blPct = clampBrightness(prefs.getUChar("bl", 100));
  if (prefs.getBytesLength("cal") == sizeof(Cal)) {
    prefs.getBytes("cal", &cal, sizeof(Cal));
    calValid = (cal.magic == CAL_MAGIC);
  }
  prefs.end();

  tft.begin();
  // Flicker fix: the ILI9341 powers up refreshing at ~70 Hz (FRMCTR1 = 0x1B),
  // which shows as a faint shimmer. Run the panel at 100 Hz instead.
  tft.writecommand(0xB1);
  tft.writedata(0x00);
  tft.writedata(0x13);
  tft.fillScreen(TFT_BLACK);

  // TFT_eSPI switched the backlight fully on; take the pin over with PWM
  ledcAttach(BL_PIN, BL_FREQ, BL_BITS);
  applyBrightness();

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(0);  // raw readings; calibration handles orientation

  lv_init();
  buf1 = (lv_color_t *)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA);
  buf2 = (lv_color_t *)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA);
  if (!buf1 || !buf2) {
    Serial.println("[display] draw buffer allocation failed");
    while (true) delay(1000);
  }
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, BUF_PX);

  lv_disp_drv_init(&dispDrv);
  applyRotation();
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  disp = lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);

  Serial.printf("[display] rotation %u, touch calibration %s\n", rot,
                calValid ? "loaded" : "missing");
}

uint8_t display::rotation() { return rot; }

void display::setRotation(uint8_t r) {
  rot = r & 3;
  applyRotation();
  lv_disp_drv_update(disp, &dispDrv);
  prefs.begin("disp", false);
  prefs.putUChar("rot", rot);
  prefs.end();
}

uint8_t display::brightness() { return blPct; }

void display::setBrightness(uint8_t pct, bool save) {
  blPct = clampBrightness(pct);
  applyBrightness();
  if (save) {
    prefs.begin("disp", false);
    prefs.putUChar("bl", blPct);
    prefs.end();
  }
}

bool display::swapRedBlue() { return swapRB; }

void display::setSwapRedBlue(bool on) {
  swapRB = on;
  lv_obj_invalidate(lv_scr_act());  // redraw everything with the new mapping
  prefs.begin("disp", false);
  prefs.putBool("swaprb", swapRB);
  prefs.end();
}

bool display::hasCalibration() { return calValid; }

bool display::readRaw(int16_t &x, int16_t &y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < TS_MIN_PRESSURE) return false;
  x = p.x;
  y = p.y;
  return true;
}

bool display::calibrate(const CalPoint *pts, int n, float *maxErrPx) {
  if (n < 3) return false;

  // Least-squares fit of raw -> native for x and y separately
  double m[3][3] = {{0}}, vx[3] = {0}, vy[3] = {0};
  for (int k = 0; k < n; k++) {
    float nx, ny;
    toNative(rot, pts[k].sx, pts[k].sy, nx, ny);
    double r[3] = {(double)pts[k].rx, (double)pts[k].ry, 1.0};
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) m[i][j] += r[i] * r[j];
      vx[i] += r[i] * nx;
      vy[i] += r[i] * ny;
    }
  }
  double A[3], B[3];
  if (!solve3(m, vx, A) || !solve3(m, vy, B)) return false;

  Cal c{CAL_MAGIC, (float)A[0], (float)A[1], (float)A[2],
        (float)B[0], (float)B[1], (float)B[2]};

  float worst = 0;
  for (int k = 0; k < n; k++) {
    float nx, ny;
    toNative(rot, pts[k].sx, pts[k].sy, nx, ny);
    float fx = c.a * pts[k].rx + c.b * pts[k].ry + c.c;
    float fy = c.d * pts[k].rx + c.e * pts[k].ry + c.f;
    worst = fmaxf(worst, hypotf(fx - nx, fy - ny));
  }
  if (maxErrPx) *maxErrPx = worst;
  Serial.printf("[display] calibration fit, worst error %.1f px\n", worst);
  if (worst > CAL_MAX_ERR_PX) return false;

  cal = c;
  calValid = true;
  prefs.begin("disp", false);
  prefs.putBytes("cal", &cal, sizeof(Cal));
  prefs.end();
  return true;
}
