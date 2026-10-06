#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "app_base.h"
#include "nav_stack.h"
#include "esp_err.h"

esp_err_t app_manager_init(void);
esp_err_t app_manager_launch(const char *name, bool anim);
esp_err_t app_manager_go_back(bool anim);
esp_err_t app_manager_go_home(bool anim);
const nav_stack_t *app_manager_get_stack(void);
app_base_t *app_manager_get_foreground(void);
void app_manager_post_event(const app_event_t *event);
lv_obj_t *app_manager_get_content_area(void);

#endif // APP_MANAGER_H
