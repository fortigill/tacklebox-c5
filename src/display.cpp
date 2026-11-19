#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Touch controller sits on its own SPI bus on the CYD
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Set to 1 to print raw touch readings to Serial (useful for calibration)
#define TOUCH_DEBUG 0

static const uint16_t SCREEN_W = 320;
static const uint16_t SCREEN_H = 240;

// Raw touch range -> screen. Adjust if taps land offset (see README).
static const int TS_MIN_X = 200,  TS_MAX_X = 3700;
static const int TS_MIN_Y = 240,  TS_MAX_Y = 3800;
static const int TS_MIN_PRESSURE = 200;

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t drawBuf;
static const uint32_t BUF_PX = SCREEN_W * 24;
static lv_color_t *buf1 = nullptr;  // allocated at runtime (DMA-capable heap)
static lv_color_t *buf2 = nullptr;
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  uint32_t w = a->x2 - a->x1 + 1;
  uint32_t h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t *)&px->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

static void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
#if TOUCH_DEBUG
    Serial.printf("touch raw x=%d y=%d z=%d\n", p.x, p.y, p.z);
#endif
    if (p.z >= TS_MIN_PRESSURE) {
      int x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_W - 1);
      int y = map(p.y, TS_MIN_Y, TS_MAX_Y, 0, SCREEN_H - 1);
      data->point.x = constrain(x, 0, SCREEN_W - 1);
      data->point.y = constrain(y, 0, SCREEN_H - 1);
      data->state = LV_INDEV_STATE_PR;
      return;
    }
  }
  data->state = LV_INDEV_STATE_REL;
}

void display::begin() {
  tft.begin();
  tft.setRotation(1);  // landscape, USB on the right
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  lv_init();
  buf1 = (lv_color_t *)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA);
  buf2 = (lv_color_t *)heap_caps_malloc(BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA);
  if (!buf1 || !buf2) {
    Serial.println("[display] draw buffer allocation failed");
    while (true) delay(1000);
  }
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, BUF_PX);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);
}
