#include "theme.h"

LV_FONT_DECLARE(font_cjk_14);
LV_FONT_DECLARE(font_cjk_16);

static lv_font_t s_font_normal;
static lv_font_t s_font_medium;
static lv_font_t s_font_large;

void theme_init(void)
{
    s_font_normal = *(&lv_font_montserrat_14);
    s_font_normal.fallback = &font_cjk_14;

    s_font_medium = *(&lv_font_montserrat_20);
    s_font_medium.fallback = &font_cjk_16;

    s_font_large = *(&lv_font_montserrat_28);
    s_font_large.fallback = &font_cjk_16;
}

const lv_font_t *theme_font_normal(void)
{
    return &s_font_normal;
}

const lv_font_t *theme_font_medium(void)
{
    return &s_font_medium;
}

const lv_font_t *theme_font_large(void)
{
    return &s_font_large;
}
