// NM-CYD-C5 display driver: ST7789 through ESP-IDF's esp_lcd (officially
// supported on the ESP32-C5, unlike TFT_eSPI), and the XPT2046 resistive touch
// controller as a second device on the same SPI bus. The SPI master driver
// arbitrates the bus between the two, and touch is only polled while no pixel
// transfer is in flight.
//
// Same public API as the classic driver (display.h), so the rest of the app
// doesn't know which board it runs on.
#include "board.h"
#if defined(BOARD_NM_CYD_C5)

#include "display.h"

#include <Arduino.h>
#include <Preferences.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <lvgl.h>

#include <math.h>

#include <algorithm>
#include <utility>

namespace {

const spi_host_device_t LCD_HOST = SPI2_HOST;
const uint32_t LCD_PCLK_HZ = 40 * 1000 * 1000;
const uint32_t TOUCH_HZ = 2 * 1000 * 1000;

const uint32_t BL_FREQ = 20000;  // far above visible flicker
const uint8_t BL_BITS = 8;

const int16_t NATIVE_W = 240;  // panel's native (portrait) resolution
const int16_t NATIVE_H = 320;

// Two DMA buffers of 20 lines: LVGL renders into one while the other is sent
const uint32_t BUF_PX = 320 * 20;
const uint16_t TS_MIN_PRESSURE = 300;
const float CAL_MAX_ERR_PX = 20.0f;
const uint32_t CAL_MAGIC = 0xCA1B0001;

// Affine map raw touch -> native panel coordinates (see display.cpp)
struct Cal {
  uint32_t magic;
  float a, b, c, d, e, f;
};

esp_lcd_panel_io_handle_t io = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
spi_device_handle_t touchDev = nullptr;
Preferences prefs;

lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;
lv_disp_t *disp = nullptr;
lv_indev_drv_t indevDrv;
volatile bool flushing = false;  // a pixel transfer is in flight

uint8_t rot = DEFAULT_ROTATION;
bool swapRB = DEFAULT_SWAP_RB;
bool invert = DEFAULT_INVERT;
uint8_t blPct = DEFAULT_BRIGHTNESS;
Cal cal{};
bool calValid = false;

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

bool scrOn = true;
bool wakeHold = false;  // swallowing the touch that woke the screen

void applyBrightness() { ledcWrite(PIN_LCD_BL, scrOn ? (uint32_t)blPct * ((1u << BL_BITS) - 1) / 100 : 0); }

// Same orientation semantics as TFT_eSPI's ST7789 rotations (MADCTL MX/MY/MV)
void applyRotation() {
  bool swap = false, mx = false, my = false;
  switch (rot) {
    case 1: swap = true; mx = true; break;
    case 2: mx = true; my = true; break;
    case 3: swap = true; my = true; break;
    default: break;
  }
  esp_lcd_panel_swap_xy(panel, swap);
  esp_lcd_panel_mirror(panel, mx, my);
  bool landscape = rot & 1;
  dispDrv.hor_res = landscape ? NATIVE_H : NATIVE_W;
  dispDrv.ver_res = landscape ? NATIVE_W : NATIVE_H;
}

// Pixel transfer finished (called from the SPI ISR)
bool IRAM_ATTR onFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
  flushing = false;
  lv_disp_flush_ready((lv_disp_drv_t *)ctx);
  return false;
}

void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  if (swapRB) {  // buffer is big-endian RGB565 (LV_COLOR_16_SWAP)
    uint16_t *p = (uint16_t *)&px->full;
    uint32_t n = (uint32_t)(a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1);
    for (uint32_t i = 0; i < n; i++) {
      uint16_t c = __builtin_bswap16(p[i]);
      c = ((c & 0x001F) << 11) | (c & 0x07E0) | (c >> 11);
      p[i] = __builtin_bswap16(c);
    }
  }
  flushing = true;
  // Asynchronous DMA; onFlushDone() tells LVGL when the buffer is free again
  esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}

// One 12-bit XPT2046 conversion: command byte, then two bytes of result
uint16_t xptRead(uint8_t cmd) {
  spi_transaction_t t = {};
  t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.length = 24;
  t.tx_data[0] = cmd;
  if (spi_device_polling_transmit(touchDev, &t) != ESP_OK) return 0;
  return (((uint16_t)t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

int median3(int a, int b, int c) {
  if (a > b) std::swap(a, b);
  if (b > c) std::swap(b, c);
  if (a > b) std::swap(a, b);
  return b;
}

int16_t lastX = 0, lastY = 0;
bool lastPressed = false;

void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  int16_t rx, ry;
  // While pixels are being sent, report the previous state rather than
  // queuing a touch transaction behind (or in between) the DMA transfer
  if (!flushing) {
    lastPressed = calValid && display::readRaw(rx, ry);
    if (lastPressed) {
      float nx = cal.a * rx + cal.b * ry + cal.c;
      float ny = cal.d * rx + cal.e * ry + cal.f;
      float sx, sy;
      toScreen(rot, nx, ny, sx, sy);
      lastX = constrain((int)lroundf(sx), 0, dispDrv.hor_res - 1);
      lastY = constrain((int)lroundf(sy), 0, dispDrv.ver_res - 1);
    }
  }
  // Screen off: a touch wakes it and is not passed on (nothing gets tapped
  // blind); the same applies until that finger lifts
  if (!scrOn && lastPressed) {
    display::setScreenOn(true);
    wakeHold = true;
  }
  if (wakeHold) {
    if (!lastPressed) wakeHold = false;
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = lastPressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

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

void fail(const char *what, esp_err_t e) {
  Serial.printf("[display] %s failed: %s\n", what, esp_err_to_name(e));
}

}  // namespace

void display::begin() {
  prefs.begin("disp", false);
  rot = prefs.getUChar("rot", DEFAULT_ROTATION) & 3;
  swapRB = prefs.getBool("swaprb", DEFAULT_SWAP_RB);
  invert = prefs.getBool("inv", DEFAULT_INVERT);
  blPct = clampBrightness(prefs.getUChar("bl", DEFAULT_BRIGHTNESS));
  if (prefs.getBytesLength("cal") == sizeof(Cal)) {
    prefs.getBytes("cal", &cal, sizeof(Cal));
    calValid = (cal.magic == CAL_MAGIC);
  }
  prefs.end();

  // Keep the SD card and touch controller deselected while the bus comes up
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = PIN_SPI_SCK;
  bus.mosi_io_num = PIN_SPI_MOSI;
  bus.miso_io_num = PIN_SPI_MISO;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = BUF_PX * sizeof(lv_color_t);
  esp_err_t e = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
  if (e != ESP_OK) fail("SPI bus", e);

  esp_lcd_panel_io_spi_config_t ioCfg = {};
  ioCfg.cs_gpio_num = PIN_LCD_CS;
  ioCfg.dc_gpio_num = PIN_LCD_DC;
  ioCfg.spi_mode = 0;
  ioCfg.pclk_hz = LCD_PCLK_HZ;
  ioCfg.trans_queue_depth = 10;
  ioCfg.on_color_trans_done = onFlushDone;
  ioCfg.user_ctx = &dispDrv;
  ioCfg.lcd_cmd_bits = 8;
  ioCfg.lcd_param_bits = 8;
  e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &ioCfg, &io);
  if (e != ESP_OK) fail("panel IO", e);

  esp_lcd_panel_dev_config_t pCfg = {};
  pCfg.reset_gpio_num = PIN_LCD_RST;
  pCfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;  // per RockBase's notes (TFT_BGR)
  pCfg.bits_per_pixel = 16;
  e = esp_lcd_new_panel_st7789(io, &pCfg, &panel);
  if (e != ESP_OK) fail("ST7789", e);
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  esp_lcd_panel_invert_color(panel, invert);
  esp_lcd_panel_disp_on_off(panel, true);

  // Touch: second device on the same bus, much slower clock
  spi_device_interface_config_t tCfg = {};
  tCfg.clock_speed_hz = TOUCH_HZ;
  tCfg.mode = 0;
  tCfg.spics_io_num = PIN_TOUCH_CS;
  tCfg.queue_size = 1;
  e = spi_bus_add_device(LCD_HOST, &tCfg, &touchDev);
  if (e != ESP_OK) fail("touch SPI device", e);

  ledcAttach(PIN_LCD_BL, BL_FREQ, BL_BITS);
  applyBrightness();

  lv_init();
  lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_color_t *b2 = (lv_color_t *)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!b1) {
    Serial.println("[display] draw buffer allocation failed");
    while (true) delay(1000);
  }
  lv_disp_draw_buf_init(&drawBuf, b1, b2, BUF_PX);  // b2 may be null: single buffer

  lv_disp_drv_init(&dispDrv);
  applyRotation();
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  disp = lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);

  Serial.printf("[display] %s ST7789 via esp_lcd, rotation %u, invert %s, touch calibration %s, "
                "PSRAM %u KB\n",
                BOARD_NAME, rot, invert ? "on" : "off", calValid ? "loaded" : "missing",
                (unsigned)(ESP.getPsramSize() / 1024));
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

bool display::screenOn() { return scrOn; }

void display::setScreenOn(bool on) {
  if (on == scrOn) return;
  scrOn = on;
  applyBrightness();
  Serial.printf("[display] screen %s\n", on ? "on" : "off");
}

void display::setBrightness(uint8_t pct, bool save) {
  blPct = clampBrightness(pct);
  applyBrightness();
  if (save) {
    prefs.begin("disp", false);
    prefs.putUChar("bl", blPct);
    prefs.end();
  }
}

bool display::invertColors() { return invert; }

void display::setInvertColors(bool on) {
  invert = on;
  esp_lcd_panel_invert_color(panel, invert);
  prefs.begin("disp", false);
  prefs.putBool("inv", invert);
  prefs.end();
}

bool display::swapRedBlue() { return swapRB; }

void display::setSwapRedBlue(bool on) {
  swapRB = on;
  lv_obj_invalidate(lv_scr_act());
  prefs.begin("disp", false);
  prefs.putBool("swaprb", swapRB);
  prefs.end();
}

bool display::hasCalibration() { return calValid; }

// Pressure from Z1/Z2 like the XPT2046_Touchscreen library, then the median
// of three X and three Y readings to reject resistive-panel noise
bool display::readRaw(int16_t &x, int16_t &y) {
  if (!touchDev) return false;
  int z1 = xptRead(0xB1), z2 = xptRead(0xC1);
  int z = z1 + 4095 - z2;
  if (z1 < 1 || z < TS_MIN_PRESSURE) {
    xptRead(0xD0);  // power down between readings
    return false;
  }
  int xs[3], ys[3];
  for (int i = 0; i < 3; i++) {
    xs[i] = xptRead(0xD1);
    ys[i] = xptRead(0x91);
  }
  xptRead(0xD0);
  x = median3(xs[0], xs[1], xs[2]);
  y = median3(ys[0], ys[1], ys[2]);
  return true;
}

bool display::calibrate(const CalPoint *pts, int n, float *maxErrPx) {
  if (n < 3) return false;
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
  Cal c{CAL_MAGIC, (float)A[0], (float)A[1], (float)A[2], (float)B[0], (float)B[1], (float)B[2]};
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

#endif  // BOARD_NM_CYD_C5
