#ifndef SYS_UI_H
#define SYS_UI_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void sys_ui_create(lv_obj_t *screen);
void sys_ui_update_time(void);
void sys_ui_set_wifi_status(bool connected);
void sys_ui_set_battery(int percent, bool charging);
void sys_ui_show_notifications(void);
void sys_ui_show_quick_settings(void);
void sys_ui_dismiss_overlay(void);
bool sys_ui_overlay_is_visible(void);

/* ── notifications (thread-safe, callable from any task) ───── */
void     sys_ui_add_notification(const char *text);  // ≤127 chars, FIFO max 8
void     sys_ui_clear_notifications(void);
int      sys_ui_notification_count(void);
uint32_t sys_ui_tick_count(void);  // +1/s from sys_ui_update_time (heartbeat)

#endif // SYS_UI_H
