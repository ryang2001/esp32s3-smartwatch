#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H

#include "esp_err.h"
#include "lvgl.h"

esp_err_t board_display_init(void);
lv_display_t *board_get_display(void);

#endif // PLATFORM_DISPLAY_H
