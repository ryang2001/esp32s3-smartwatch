#include "app_base.h"
#include "app_registry.h"
#include "app_manager.h"
#include "theme.h"
#include "board.h"
#include <stdlib.h>

typedef struct {
    lv_obj_t    *time_label;
    lv_obj_t    *btn_start_label;
    lv_obj_t    *laps_list;
    lv_timer_t  *timer;
    bool         running;
    uint32_t     start_tick;
    uint32_t     elapsed_ms;
    int          lap_count;
} stopwatch_ctx_t;

static void update_display(stopwatch_ctx_t *ctx)
{
    uint32_t ms = ctx->elapsed_ms;
    if (ctx->running) {
        ms += lv_tick_get() - ctx->start_tick;
    }
    uint32_t min = ms / 60000;
    uint32_t sec = (ms % 60000) / 1000;
    uint32_t cs  = (ms % 1000) / 10;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu.%02lu", min, sec, cs);
    lv_label_set_text(ctx->time_label, buf);
}

static void sw_timer_cb(lv_timer_t *timer)
{
    stopwatch_ctx_t *ctx = lv_timer_get_user_data(timer);
    if (ctx->running) {
        update_display(ctx);
    }
}

static void start_stop_cb(lv_event_t *e)
{
    stopwatch_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    if (ctx->running) {
        ctx->elapsed_ms += lv_tick_get() - ctx->start_tick;
        ctx->running = false;
        lv_label_set_text(ctx->btn_start_label, "Start");
        lv_obj_set_style_bg_color(btn, COLOR_ACCENT, 0);
    } else {
        ctx->start_tick = lv_tick_get();
        ctx->running = true;
        lv_label_set_text(ctx->btn_start_label, "Stop");
        lv_obj_set_style_bg_color(btn, COLOR_DESTRUCTIVE, 0);
        if (!ctx->timer) {
            ctx->timer = lv_timer_create(sw_timer_cb, 30, ctx);
        }
    }
}

static void reset_lap_cb(lv_event_t *e)
{
    stopwatch_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx->running) {
        // Record lap
        ctx->lap_count++;
        uint32_t ms = ctx->elapsed_ms + lv_tick_get() - ctx->start_tick;
        uint32_t min = ms / 60000;
        uint32_t sec = (ms % 60000) / 1000;
        uint32_t cs  = (ms % 1000) / 10;

        lv_obj_t *lap = lv_list_add_text(ctx->laps_list, "");
        char buf[32];
        snprintf(buf, sizeof(buf), "Lap %d  %02lu:%02lu.%02lu",
                 ctx->lap_count, min, sec, cs);
        lv_label_set_text(lap, buf);
        lv_obj_set_style_text_color(lap, COLOR_SECONDARY, 0);
    } else {
        // Reset
        ctx->elapsed_ms = 0;
        ctx->lap_count = 0;
        if (ctx->timer) {
            lv_timer_delete(ctx->timer);
            ctx->timer = NULL;
        }
        update_display(ctx);
        lv_obj_clean(ctx->laps_list);
        lv_label_set_text(ctx->btn_start_label, "Start");
    }
}

static void *stopwatch_create(lv_obj_t *parent)
{
    stopwatch_ctx_t *ctx = calloc(1, sizeof(stopwatch_ctx_t));

    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_size(bg, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_set_style_bg_color(bg, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    ctx->time_label = lv_label_create(bg);
    lv_label_set_text(ctx->time_label, "00:00.00");
    lv_obj_set_style_text_color(ctx->time_label, COLOR_FG, 0);
    lv_obj_set_style_text_font(ctx->time_label, theme_font_large(), 0);
    lv_obj_align(ctx->time_label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *btn_start = lv_button_create(bg);
    lv_obj_set_size(btn_start, 100, 44);
    lv_obj_align(btn_start, LV_ALIGN_TOP_LEFT, 16, 70);
    lv_obj_set_style_bg_color(btn_start, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(btn_start, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_start, 8, 0);
    ctx->btn_start_label = lv_label_create(btn_start);
    lv_label_set_text(ctx->btn_start_label, "Start");
    lv_obj_center(ctx->btn_start_label);
    lv_obj_set_style_text_color(ctx->btn_start_label, COLOR_FG, 0);
    lv_obj_add_event_cb(btn_start, start_stop_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *btn_reset = lv_button_create(bg);
    lv_obj_set_size(btn_reset, 100, 44);
    lv_obj_align(btn_reset, LV_ALIGN_TOP_RIGHT, -16, 70);
    lv_obj_set_style_bg_color(btn_reset, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn_reset, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_reset, 8, 0);
    lv_obj_t *lbl_reset = lv_label_create(btn_reset);
    lv_label_set_text(lbl_reset, "Lap/Reset");
    lv_obj_center(lbl_reset);
    lv_obj_set_style_text_color(lbl_reset, COLOR_FG, 0);
    lv_obj_add_event_cb(btn_reset, reset_lap_cb, LV_EVENT_CLICKED, ctx);

    ctx->laps_list = lv_list_create(bg);
    lv_obj_set_pos(ctx->laps_list, 10, 124);
    lv_obj_set_size(ctx->laps_list, DISPLAY_H_RES - 20, APP_AREA_H - 132);
    lv_obj_set_style_bg_color(ctx->laps_list, COLOR_BG, 0);
    lv_obj_set_style_border_width(ctx->laps_list, 0, 0);

    return ctx;
}

static void stopwatch_show(void *ctx_)
{
    stopwatch_ctx_t *ctx = ctx_;
    /* Resume UI timer and refresh display */
    if (ctx->timer && ctx->running) {
        lv_timer_set_period(ctx->timer, 30);
    }
    update_display(ctx);
}

static void stopwatch_hide(void *ctx_)
{
    stopwatch_ctx_t *ctx = ctx_;
    /* Pause UI timer — time accumulation continues via lv_tick_get() */
    if (ctx->timer) {
        lv_timer_set_period(ctx->timer, 3600000);  /* ~1 hour: effectively paused */
    }
}

static void stopwatch_destroy(void *ctx_)
{
    stopwatch_ctx_t *ctx = ctx_;
    if (ctx->timer) lv_timer_delete(ctx->timer);
    free(ctx);
}

static bool stopwatch_handle_event(void *ctx, const app_event_t *event)
{
    (void)ctx;
    if (event->type == APP_EVENT_GESTURE_SWIPE_RIGHT) {
        app_manager_go_back(true);
        return true;
    }
    return false;
}

const app_descriptor_t app_descriptor_stopwatch = {
    .name          = "stopwatch",
    .display_name  = "Stopwatch",
    .icon          = NULL,
    .category      = APP_CAT_TOOL,
    .flags         = 0,
    .create        = stopwatch_create,
    .show          = stopwatch_show,
    .hide          = stopwatch_hide,
    .destroy       = stopwatch_destroy,
    .handle_event  = stopwatch_handle_event,
    .tick          = NULL,
};
