#pragma once
// LVGL's object memory in PSRAM on boards that have it (the NM-CYD-C5), so
// the chip's internal RAM stays free for Wi-Fi and HTTPS. Falls back to the
// normal heap if PSRAM runs out. Draw buffers are separate (internal, DMA).
#include <stddef.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

static inline void *lv_psram_alloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
static inline void *lv_psram_realloc(void *p, size_t n) {
  void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return q ? q : realloc(p, n);
}
static inline void lv_psram_free(void *p) { heap_caps_free(p); }
