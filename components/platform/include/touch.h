#ifndef PLATFORM_TOUCH_H
#define PLATFORM_TOUCH_H

#include "esp_err.h"
#include "lvgl.h"

esp_err_t board_touch_init(void);
lv_indev_t *board_get_touch_indev(void);
bool touch_is_pressed(void);
void touch_debug_init(void);

#endif // PLATFORM_TOUCH_H
