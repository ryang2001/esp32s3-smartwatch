#ifndef PLATFORM_BACKLIGHT_H
#define PLATFORM_BACKLIGHT_H

#include "esp_err.h"

esp_err_t board_backlight_init(void);
void board_backlight_set(int percent);
int board_backlight_get(void);

#endif // PLATFORM_BACKLIGHT_H
