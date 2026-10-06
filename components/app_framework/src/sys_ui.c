#include "sys_ui.h"
#include "theme.h"
#include "backlight.h"
#include "board.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static lv_obj_t *s_status_bar;
static lv_obj_t *s_time_label;
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_content;
static bool s_overlay_visible = false;

/* ── notification store (written from any task, read in LVGL context) ── */

#define SYS_NOTIF_MAX    8
#define SYS_NOTIF_LEN    128

static char          s_notif[SYS_NOTIF_MAX][SYS_NOTIF_LEN];
static int           s_notif_count = 0;
static portMUX_TYPE  s_notif_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t      s_tick_count = 0;

static void slide_y_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, (lv_coord_t)v);
}

static void overlay_slide_in(lv_obj_t *obj, int32_t start_y, int32_t end_y,
                              uint32_t duration)
{
    lv_obj_set_y(obj, start_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, slide_y_cb);
    lv_anim_set_values(&a, start_y, end_y);
    lv_anim_set_time(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void overlay_fade_out_cb(lv_anim_t *a)
{
    lv_obj_t *obj = a->var;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(obj, 0);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    s_overlay_visible = false;
}

static void fade_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void overlay_fade_out(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, fade_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 100);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, overlay_fade_out_cb);
    lv_anim_start(&a);
}

// Delete old content before creating new overlay content
static void overlay_clear_content(void)
{
    if (s_overlay_content) {
        lv_obj_del(s_overlay_content);
    }
    s_overlay_content = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_overlay_content);
    lv_obj_set_pos(s_overlay_content, 0, 0);
    lv_obj_set_size(s_overlay_content, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_clear_flag(s_overlay_content, LV_OBJ_FLAG_SCROLLABLE);
}

void sys_ui_create(lv_obj_t *screen)
{
    s_status_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(s_status_bar);
    lv_obj_set_pos(s_status_bar, 0, 0);
    lv_obj_set_size(s_status_bar, DISPLAY_H_RES, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_color(s_status_bar, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_90, 0);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_status_bar, 2, 0);

    s_time_label = lv_label_create(s_status_bar);
    lv_label_set_text(s_time_label, "00:00");
    lv_obj_set_style_text_color(s_time_label, COLOR_FG, 0);
    lv_obj_set_style_text_font(s_time_label, theme_font_normal(), 0);
    lv_obj_align(s_time_label, LV_ALIGN_LEFT_MID, 4, 0);

    s_wifi_label = lv_label_create(s_status_bar);
    lv_label_set_text(s_wifi_label, "");
    lv_obj_set_style_text_color(s_wifi_label, COLOR_SECONDARY, 0);
    lv_obj_align(s_wifi_label, LV_ALIGN_RIGHT_MID, -4, 0);

    // Overlay container (hidden by default)
    s_overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_set_style_bg_color(s_overlay, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Content container inside overlay (children are added here, replaced each show)
    s_overlay_content = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_overlay_content);
    lv_obj_set_pos(s_overlay_content, 0, 0);
    lv_obj_set_size(s_overlay_content, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_clear_flag(s_overlay_content, LV_OBJ_FLAG_SCROLLABLE);
}

void sys_ui_update_time(void)
{
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(s_time_label, buf);
    s_tick_count++;  // heartbeat for OTA boot self-test
}

uint32_t sys_ui_tick_count(void)
{
    return s_tick_count;
}

void sys_ui_add_notification(const char *text)
{
    if (!text || !text[0]) return;

    taskENTER_CRITICAL(&s_notif_mux);
    if (s_notif_count >= SYS_NOTIF_MAX) {
        // FIFO: drop the oldest
        memmove(s_notif[0], s_notif[1], (SYS_NOTIF_MAX - 1) * SYS_NOTIF_LEN);
        s_notif_count = SYS_NOTIF_MAX - 1;
    }
    strlcpy(s_notif[s_notif_count], text, SYS_NOTIF_LEN);
    s_notif_count++;
    taskEXIT_CRITICAL(&s_notif_mux);
}

void sys_ui_clear_notifications(void)
{
    taskENTER_CRITICAL(&s_notif_mux);
    s_notif_count = 0;
    taskEXIT_CRITICAL(&s_notif_mux);
}

int sys_ui_notification_count(void)
{
    int count;
    taskENTER_CRITICAL(&s_notif_mux);
    count = s_notif_count;
    taskEXIT_CRITICAL(&s_notif_mux);
    return count;
}

void sys_ui_set_wifi_status(bool connected)
{
    lv_label_set_text(s_wifi_label, connected ? LV_SYMBOL_WIFI : "");
}

void sys_ui_show_notifications(void)
{
    lv_obj_move_foreground(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    overlay_clear_content();
    lv_obj_set_style_opa(s_overlay, LV_OPA_90, 0);

    lv_obj_t *title = lv_label_create(s_overlay_content);
    lv_label_set_text(title, "Notifications");
    lv_obj_set_style_text_color(title, COLOR_FG, 0);
    lv_obj_set_style_text_font(title, theme_font_medium(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Snapshot the store outside the critical section (LVGL work below)
    static char snapshot[SYS_NOTIF_MAX][SYS_NOTIF_LEN];
    int count;
    taskENTER_CRITICAL(&s_notif_mux);
    memcpy(snapshot, s_notif, sizeof(char) * SYS_NOTIF_MAX * SYS_NOTIF_LEN);
    count = s_notif_count;
    taskEXIT_CRITICAL(&s_notif_mux);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_overlay_content);
        lv_label_set_text(empty, "No notifications");
        lv_obj_set_style_text_color(empty, COLOR_DIM, 0);
        lv_obj_set_style_text_font(empty, theme_font_normal(), 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, -10);
    } else {
        lv_obj_t *list = lv_obj_create(s_overlay_content);
        lv_obj_remove_style_all(list);
        lv_obj_set_pos(list, 12, 50);
        lv_obj_set_size(list, DISPLAY_H_RES - 24, DISPLAY_V_RES - 100);
        lv_obj_set_style_pad_all(list, 0, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        for (int i = 0; i < count; i++) {
            lv_obj_t *item = lv_label_create(list);
            lv_label_set_text(item, snapshot[i]);
            lv_obj_set_width(item, DISPLAY_H_RES - 40);
            lv_label_set_long_mode(item, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(item, COLOR_FG, 0);
            lv_obj_set_style_text_font(item, theme_font_normal(), 0);
        }
    }

    lv_obj_t *hint = lv_label_create(s_overlay_content);
    lv_label_set_text(hint, "Tap to dismiss");
    lv_obj_set_style_text_color(hint, COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, theme_font_normal(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

    overlay_slide_in(s_overlay, -DISPLAY_V_RES, 0, 150);
    s_overlay_visible = true;
}

static void qs_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    board_backlight_set(val);
}

void sys_ui_show_quick_settings(void)
{
    lv_obj_move_foreground(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    overlay_clear_content();
    lv_obj_set_style_opa(s_overlay, LV_OPA_90, 0);

    lv_obj_t *title = lv_label_create(s_overlay_content);
    lv_label_set_text(title, "Quick Settings");
    lv_obj_set_style_text_color(title, COLOR_FG, 0);
    lv_obj_set_style_text_font(title, theme_font_medium(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Brightness
    lv_obj_t *bright_label = lv_label_create(s_overlay_content);
    lv_label_set_text(bright_label, LV_SYMBOL_IMAGE " Brightness");
    lv_obj_set_style_text_color(bright_label, COLOR_FG, 0);
    lv_obj_set_style_text_font(bright_label, theme_font_normal(), 0);
    lv_obj_align(bright_label, LV_ALIGN_TOP_LEFT, 12, 50);

    lv_obj_t *slider = lv_slider_create(s_overlay_content);
    lv_obj_set_width(slider, DISPLAY_H_RES - 40);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 74);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, board_backlight_get(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, qs_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Hint
    lv_obj_t *hint = lv_label_create(s_overlay_content);
    lv_label_set_text(hint, "Tap to dismiss");
    lv_obj_set_style_text_color(hint, COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, theme_font_normal(), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

    overlay_slide_in(s_overlay, DISPLAY_V_RES, 0, 150);
    s_overlay_visible = true;
}

void sys_ui_dismiss_overlay(void)
{
    overlay_fade_out();
}

bool sys_ui_overlay_is_visible(void)
{
    return s_overlay_visible;
}
