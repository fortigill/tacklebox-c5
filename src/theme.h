#pragma once

#include <lvgl.h>
#include <stdint.h>

// Color themes. LVGL's default theme supplies widget styling (primary and
// secondary colors, dark/light base); a small child theme layered on top sets
// the background, panel, text and keyboard colors that make each look.
namespace theme {

enum Id : uint8_t { DARK, LIGHT, MIAMI, HACKER, COUNT };

void begin();          // load the saved theme and apply it (after display::begin)
void set(Id id);       // apply now and save; rebuild the current screen afterwards
Id current();
const char *name(Id id);

lv_color_t text();     // main text color of the current theme
lv_color_t accent();   // secondary/accent color

}  // namespace theme
