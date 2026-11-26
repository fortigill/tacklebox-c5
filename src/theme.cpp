#include "theme.h"
#include "board.h"

#include <Preferences.h>

namespace theme {
namespace {

struct Palette {
  const char *name;
  bool dark;
  uint32_t bg;       // screens
  uint32_t surface;  // panels, lists, headers, text areas
  uint32_t section;  // list section headings background
  uint32_t border;
  uint32_t text;
  uint32_t primary;  // buttons, sliders, switches
  uint32_t accent;   // secondary color, section headings
  uint32_t btnText;  // text on primary-colored buttons
};

const Palette PALETTES[COUNT] = {
    // Dark: LVGL's dark look, blue accents
    {"Dark", true, 0x15171A, 0x282B30, 0x1E2024, 0x3A3E45, 0xFFFFFF, 0x2196F3, 0x26A69A, 0xFFFFFF},
    // Light: soft grey background, white panels, dark text
    {"Light", false, 0xEEF1F5, 0xFFFFFF, 0xE2E7EE, 0xC9D1DC, 0x1E2328, 0x1976D2, 0x00897B, 0xFFFFFF},
    // Miami Vice: midnight purple, hot pink and aqua neon
    {"Miami Vice", true, 0x1A0B2E, 0x2D1650, 0x22103D, 0xFF2E97, 0xFDF0FF, 0xFF2E97, 0x00E5D4, 0xFFFFFF},
    // Cyber hacker: black terminal, phosphor green
    {"Cyber Hacker", true, 0x000000, 0x041208, 0x020A04, 0x0F5A24, 0x39FF6A, 0x00C853, 0x00E5FF, 0x000000},
};

Preferences prefs;
Id cur = DARK;
lv_theme_t child;
bool childInited = false;

lv_style_t stScr, stSurface, stSection, stBtn, stKbMain, stKbItems;

void initStyles(const Palette &p) {
  lv_style_t *all[] = {&stScr, &stSurface, &stSection, &stBtn, &stKbMain, &stKbItems};
  static bool inited = false;
  for (lv_style_t *s : all) {
    if (inited) lv_style_reset(s);
    lv_style_init(s);
  }
  inited = true;

  lv_style_set_bg_color(&stScr, lv_color_hex(p.bg));
  lv_style_set_text_color(&stScr, lv_color_hex(p.text));

  lv_style_set_bg_color(&stSurface, lv_color_hex(p.surface));
  lv_style_set_border_color(&stSurface, lv_color_hex(p.border));
  lv_style_set_text_color(&stSurface, lv_color_hex(p.text));

  lv_style_set_bg_color(&stSection, lv_color_hex(p.section));
  lv_style_set_text_color(&stSection, lv_color_hex(p.accent));

  lv_style_set_text_color(&stBtn, lv_color_hex(p.btnText));

  lv_style_set_bg_color(&stKbMain, lv_color_hex(p.bg));
  lv_style_set_bg_color(&stKbItems, lv_color_hex(p.surface));
  lv_style_set_text_color(&stKbItems, lv_color_hex(p.text));
  lv_style_set_border_color(&stKbItems, lv_color_hex(p.border));
}

// Runs after the default theme has styled a new object
void applyCb(lv_theme_t *, lv_obj_t *obj) {
  if (lv_obj_get_parent(obj) == nullptr) {  // a screen
    lv_obj_add_style(obj, &stScr, 0);
    return;
  }
  if (lv_obj_check_type(obj, &lv_list_text_class)) {
    lv_obj_add_style(obj, &stSection, 0);
  } else if (lv_obj_check_type(obj, &lv_keyboard_class)) {
    lv_obj_add_style(obj, &stKbMain, 0);
    lv_obj_add_style(obj, &stKbItems, LV_PART_ITEMS);
  } else if (lv_obj_check_type(obj, &lv_btn_class)) {
    lv_obj_add_style(obj, &stBtn, 0);
  } else if (lv_obj_check_type(obj, &lv_obj_class) || lv_obj_check_type(obj, &lv_list_class) ||
             lv_obj_check_type(obj, &lv_list_btn_class) ||
             lv_obj_check_type(obj, &lv_textarea_class) ||
             lv_obj_check_type(obj, &lv_msgbox_class) || lv_obj_check_type(obj, &lv_chart_class)) {
    lv_obj_add_style(obj, &stSurface, 0);
  }
}

void apply(Id id) {
  const Palette &p = PALETTES[id];
  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *base = lv_theme_default_init(disp, lv_color_hex(p.primary), lv_color_hex(p.accent),
                                           p.dark, LV_FONT_DEFAULT);
  initStyles(p);
  if (!childInited) {
    child = *base;
    lv_theme_set_parent(&child, base);
    lv_theme_set_apply_cb(&child, applyCb);
    childInited = true;
  }
  lv_disp_set_theme(disp, &child);
  lv_obj_report_style_change(nullptr);  // refresh anything already on screen
}

}  // namespace

void begin() {
  prefs.begin("disp", false);
  uint8_t id = prefs.getUChar("theme", DEFAULT_THEME);
  prefs.end();
  cur = id < COUNT ? (Id)id : DARK;
  apply(cur);
}

void set(Id id) {
  if (id >= COUNT) return;
  cur = id;
  apply(cur);
  prefs.begin("disp", false);
  prefs.putUChar("theme", cur);
  prefs.end();
}

Id current() { return cur; }
const char *name(Id id) { return id < COUNT ? PALETTES[id].name : "?"; }
lv_color_t text() { return lv_color_hex(PALETTES[cur].text); }
lv_color_t accent() { return lv_color_hex(PALETTES[cur].accent); }

}  // namespace theme
