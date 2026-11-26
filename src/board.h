#pragma once

// Hardware differences between the supported boards. Build with
// -DBOARD_NM_CYD_C5 for the RockBase NM-CYD-C5; otherwise the classic CYD
// (ESP32-2432S028R) is assumed.

#if defined(BOARD_NM_CYD_C5)
// RockBase NM-CYD-C5: ESP32-C5 (single-core RISC-V), 16 MB flash, 8 MB PSRAM,
// ST7789 2.8" 240x320, XPT2046 resistive touch sharing the display's SPI bus
// (polled, T_IRQ not connected), WS2812 status LED.
#define BOARD_NAME "NM-CYD-C5"
#define PIN_SPI_SCK 6
#define PIN_SPI_MOSI 7
#define PIN_SPI_MISO 2  // shared: display + touch + SD
#define PIN_LCD_CS 23
#define PIN_LCD_DC 24
#define PIN_LCD_RST -1  // not connected; soft reset
#define PIN_LCD_BL 25   // strapping pin, HIGH = on (PWM after boot)
#define PIN_TOUCH_CS 1
#define PIN_SD_CS 10    // kept high (deselected) so the card stays off the bus
#define PIN_RGB_LED 27  // WS2812
#define PIN_BOOT_BTN 28 // BOOT button on the back (LOW when pressed)
#define DEFAULT_INVERT false  // this panel shows correct colors without inversion
// Out-of-the-box display settings (used until changed in Display & touch)
#define DEFAULT_ROTATION 0    // portrait (display::PORTRAIT)
#define DEFAULT_SWAP_RB true  // Colors > Swap red/blue on
#define DEFAULT_THEME 2       // Miami Vice (theme::MIAMI)
#define DEFAULT_BRIGHTNESS 100
#define BOARD_DUAL_BAND 1     // 2.4 + 5 GHz Wi-Fi
#else
// Classic CYD: ESP32-WROOM-32, ILI9341, XPT2046 on its own SPI bus, 3-pin RGB LED
#define BOARD_NAME "ESP32-2432S028R"
#define DEFAULT_INVERT true
#define DEFAULT_ROTATION 1    // landscape (display::LANDSCAPE)
#define DEFAULT_SWAP_RB false
#define DEFAULT_THEME 0       // Dark (theme::DARK)
#define DEFAULT_BRIGHTNESS 100
#define PIN_BOOT_BTN 0  // BOOT button on the back (LOW when pressed)
#define BOARD_DUAL_BAND 0     // 2.4 GHz only
#endif
