#ifndef THEME_H
#define THEME_H

#include "lvgl.h"

#define COLOR_BG               lv_color_hex(0x000000)
#define COLOR_FG               lv_color_hex(0xFFFFFF)
#define COLOR_ACCENT           lv_color_hex(0x0071E3)
#define COLOR_LINK             lv_color_hex(0x2997FF)
#define COLOR_SURFACE          lv_color_hex(0x1D1D1F)
#define COLOR_SURFACE_ELEVATED lv_color_hex(0x2A2A2D)
#define COLOR_SECONDARY        lv_color_hex(0xCCCCCC)
#define COLOR_DIM              lv_color_hex(0x7A7A7A)
#define COLOR_DESTRUCTIVE      lv_color_hex(0xFF3B30)

void theme_init(void);
const lv_font_t *theme_font_normal(void);
const lv_font_t *theme_font_medium(void);
const lv_font_t *theme_font_large(void);

#endif // THEME_H
