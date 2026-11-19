// Minimal LVGL 8.3 config for the CYD. Anything not set here uses LVGL's defaults.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0      // TFT_eSPI swaps bytes in the flush callback

// Use the system heap instead of a static pool (keeps .bss small on Arduino 3.x)
#define LV_MEM_CUSTOM          1
#define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC    malloc
#define LV_MEM_CUSTOM_FREE     free
#define LV_MEM_CUSTOM_REALLOC  realloc

#define LV_TICK_CUSTOM                1
#define LV_TICK_CUSTOM_INCLUDE        "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())

#define LV_DPI_DEF         130
#define LV_USE_LOG         0
#define LV_BUILD_EXAMPLES  0

// Fonts (built-in Montserrat includes LV_SYMBOL_* icons such as WIFI)
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

// Widgets we use
#define LV_USE_BTN       1
#define LV_USE_LABEL     1
#define LV_USE_TEXTAREA  1
#define LV_USE_BTNMATRIX 1
#define LV_USE_KEYBOARD  1
#define LV_USE_LIST      1
#define LV_USE_MSGBOX    1
#define LV_USE_SPINNER   1
#define LV_USE_ARC       1

#define LV_USE_THEME_DEFAULT   1
#define LV_THEME_DEFAULT_DARK  1

#endif // LV_CONF_H
#endif
