#include "app_base.h"
#include "app_registry.h"
#include "app_manager.h"
#include "theme.h"
#include "board.h"
#include "touch.h"
#include <stdlib.h>

typedef struct {
    lv_obj_t    *bg;
    lv_obj_t    *dot;
    lv_obj_t    *coord_label;
    lv_obj_t    *hint;
    lv_timer_t  *timer;
} touch_test_ctx_t;

static void tick_cb(lv_timer_t *timer)
{
    touch_test_ctx_t *ctx = lv_timer_get_user_data(timer);

    lv_indev_t *indev = board_get_touch_indev();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    static lv_point_t last_point = {0};
    static bool was_pressed = false;
    bool pressed = (point.x != 0 || point.y != 0) &&
                   (point.x != last_point.x || point.y != last_point.y || was_pressed);
    last_point = point;
    was_pressed = pressed;

    if (pressed) {
        lv_obj_clear_flag(ctx->dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ctx->dot, point.x - 8, point.y - 8);

        char buf[32];
        snprintf(buf, sizeof(buf), "x: %ld  y: %ld", (long)point.x, (long)point.y);
        lv_label_set_text(ctx->coord_label, buf);

        if (point.y < STATUS_BAR_HEIGHT) {
            lv_obj_set_style_bg_color(ctx->dot, lv_color_hex(0xFF3B30), 0);
        } else {
            lv_obj_set_style_bg_color(ctx->dot, COLOR_FG, 0);
        }
    } else {
        lv_obj_add_flag(ctx->dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void *touch_test_create(lv_obj_t *parent)
{
    touch_test_ctx_t *ctx = calloc(1, sizeof(touch_test_ctx_t));

    ctx->bg = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->bg);
    lv_obj_set_pos(ctx->bg, 0, 0);
    lv_obj_set_size(ctx->bg, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_set_style_bg_color(ctx->bg, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ctx->bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ctx->bg, LV_OBJ_FLAG_SCROLLABLE);

    for (int x = 0; x <= DISPLAY_H_RES; x += 40) {
        lv_obj_t *line = lv_obj_create(ctx->bg);
        lv_obj_remove_style_all(line);
        lv_obj_set_size(line, 1, APP_AREA_H);
        lv_obj_set_pos(line, x, 0);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    }
    for (int y = 0; y <= APP_AREA_H; y += 40) {
        lv_obj_t *line = lv_obj_create(ctx->bg);
        lv_obj_remove_style_all(line);
        lv_obj_set_size(line, DISPLAY_H_RES, 1);
        lv_obj_set_pos(line, 0, y);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    }

    ctx->coord_label = lv_label_create(ctx->bg);
    lv_label_set_text(ctx->coord_label, "Touch screen");
    lv_obj_set_style_text_color(ctx->coord_label, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(ctx->coord_label, theme_font_normal(), 0);
    lv_obj_align(ctx->coord_label, LV_ALIGN_TOP_MID, 0, 8);

    ctx->dot = lv_obj_create(ctx->bg);
    lv_obj_remove_style_all(ctx->dot);
    lv_obj_set_size(ctx->dot, 16, 16);
    lv_obj_set_style_bg_color(ctx->dot, COLOR_FG, 0);
    lv_obj_set_style_bg_opa(ctx->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->dot, 8, 0);
    lv_obj_add_flag(ctx->dot, LV_OBJ_FLAG_HIDDEN);

    ctx->hint = lv_label_create(ctx->bg);
    lv_label_set_text(ctx->hint, "Swipe right to exit");
    lv_obj_set_style_text_color(ctx->hint, COLOR_DIM, 0);
    lv_obj_set_style_text_font(ctx->hint, theme_font_normal(), 0);
    lv_obj_align(ctx->hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    ctx->timer = lv_timer_create(tick_cb, 30, ctx);

    return ctx;
}

static void touch_test_show(void *ctx_)
{
    touch_test_ctx_t *ctx = ctx_;
    if (ctx->timer) lv_timer_set_period(ctx->timer, 30);
}

static void touch_test_hide(void *ctx_)
{
    touch_test_ctx_t *ctx = ctx_;
    if (ctx->timer) lv_timer_set_period(ctx->timer, 3600000);
}

static void touch_test_destroy(void *ctx_)
{
    touch_test_ctx_t *ctx = ctx_;
    if (ctx->timer) lv_timer_del(ctx->timer);
    free(ctx);
}

static bool touch_test_handle_event(void *ctx, const app_event_t *event)
{
    (void)ctx;
    if (event->type == APP_EVENT_GESTURE_SWIPE_RIGHT) {
        app_manager_go_back(true);
        return true;
    }
    return true;
}

const app_descriptor_t app_descriptor_touch_test = {
    .name          = "touch_test",
    .display_name  = "Touch Test",
    .icon          = NULL,
    .category      = APP_CAT_TOOL,
    .flags         = 0,
    .create        = touch_test_create,
    .show          = touch_test_show,
    .hide          = touch_test_hide,
    .destroy       = touch_test_destroy,
    .handle_event  = touch_test_handle_event,
    .tick          = NULL,
};
