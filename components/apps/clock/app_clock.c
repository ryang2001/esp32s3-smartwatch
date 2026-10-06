#include "app_base.h"
#include "app_registry.h"
#include "app_manager.h"
#include "theme.h"
#include "board.h"
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    lv_obj_t              *bg;
    lv_obj_t              *needle_h;
    lv_obj_t              *needle_m;
    lv_obj_t              *needle_s;
    lv_obj_t              *center_dot;
    lv_obj_t              *date_label;
    lv_point_precise_t    pts_h[2];
    lv_point_precise_t    pts_m[2];
    lv_point_precise_t    pts_s[2];
} clock_ctx_t;

#define CLOCK_CX    (DISPLAY_H_RES / 2)
#define CLOCK_CY    (APP_AREA_H / 2)
#define R_OUTER     92

static void set_line_endpoints(lv_point_precise_t *pts, int cx, int cy,
                               float angle_deg, int r_start, int r_end)
{
    float a = (angle_deg - 90) * (float)M_PI / 180.0f;
    pts[0].x = cx + (int)(r_start * cosf(a));
    pts[0].y = cy + (int)(r_start * sinf(a));
    pts[1].x = cx + (int)(r_end * cosf(a));
    pts[1].y = cy + (int)(r_end * sinf(a));
}

static void *clock_create(lv_obj_t *parent)
{
    clock_ctx_t *ctx = calloc(1, sizeof(clock_ctx_t));
    int cx = CLOCK_CX;
    int cy = CLOCK_CY;

    ctx->bg = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->bg);
    lv_obj_set_pos(ctx->bg, 0, 0);
    lv_obj_set_size(ctx->bg, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_set_style_bg_color(ctx->bg, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ctx->bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ctx->bg, LV_OBJ_FLAG_SCROLLABLE);

    // Outer ring using arc
    lv_obj_t *dial = lv_arc_create(ctx->bg);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, R_OUTER * 2 + 4, R_OUTER * 2 + 4);
    lv_obj_set_pos(dial, cx - R_OUTER - 2, cy - R_OUTER - 2);
    lv_obj_set_style_arc_color(dial, COLOR_SURFACE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(dial, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(dial, 0, 0);
    lv_arc_set_bg_angles(dial, 0, 360);
    lv_arc_set_angles(dial, 0, 360);
    lv_obj_clear_flag(dial, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // 12 hour tick marks only
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * (float)M_PI / 180.0f;
        int x1 = cx + (int)(R_OUTER * cosf(angle));
        int y1 = cy + (int)(R_OUTER * sinf(angle));
        int x2 = cx + (int)((R_OUTER - 10) * cosf(angle));
        int y2 = cy + (int)((R_OUTER - 10) * sinf(angle));

        int left = x1 < x2 ? x1 : x2;
        int top = y1 < y2 ? y1 : y2;
        int rw = abs(x2 - x1) + 2;
        int rh = abs(y2 - y1) + 2;
        if (rw < 2) rw = 2;
        if (rh < 2) rh = 2;

        lv_obj_t *tick = lv_obj_create(ctx->bg);
        lv_obj_remove_style_all(tick);
        lv_obj_set_pos(tick, left, top);
        lv_obj_set_size(tick, rw, rh);
        lv_obj_set_style_bg_color(tick, COLOR_SECONDARY, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tick, 0, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    // Needle lines
    ctx->needle_h = lv_line_create(ctx->bg);
    lv_obj_set_style_line_width(ctx->needle_h, 4, 0);
    lv_obj_set_style_line_color(ctx->needle_h, COLOR_FG, 0);
    lv_obj_set_style_line_rounded(ctx->needle_h, true, 0);

    ctx->needle_m = lv_line_create(ctx->bg);
    lv_obj_set_style_line_width(ctx->needle_m, 3, 0);
    lv_obj_set_style_line_color(ctx->needle_m, COLOR_FG, 0);
    lv_obj_set_style_line_rounded(ctx->needle_m, true, 0);

    ctx->needle_s = lv_line_create(ctx->bg);
    lv_obj_set_style_line_width(ctx->needle_s, 1, 0);
    lv_obj_set_style_line_color(ctx->needle_s, COLOR_ACCENT, 0);
    lv_obj_set_style_line_rounded(ctx->needle_s, false, 0);

    // Center dot
    ctx->center_dot = lv_obj_create(ctx->bg);
    lv_obj_remove_style_all(ctx->center_dot);
    lv_obj_set_size(ctx->center_dot, 8, 8);
    lv_obj_set_pos(ctx->center_dot, cx - 4, cy - 4);
    lv_obj_set_style_bg_color(ctx->center_dot, COLOR_FG, 0);
    lv_obj_set_style_bg_opa(ctx->center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->center_dot, 4, 0);
    lv_obj_clear_flag(ctx->center_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Date label
    ctx->date_label = lv_label_create(ctx->bg);
    lv_obj_set_style_text_color(ctx->date_label, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(ctx->date_label, theme_font_normal(), 0);
    lv_obj_align(ctx->date_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    return ctx;
}

static void clock_show(void *ctx) { (void)ctx; }
static void clock_hide(void *ctx) { (void)ctx; }
static void clock_destroy(void *ctx) { free(ctx); }

static bool clock_handle_event(void *ctx, const app_event_t *event)
{
    (void)ctx;
    if (event->type == APP_EVENT_GESTURE_TAP) {
        int dx = event->tap.x - CLOCK_CX;
        int dy = event->tap.y - (APP_AREA_H / 2);
        if (dx * dx + dy * dy < 60 * 60) {
            app_manager_launch("launcher", true);
            return true;
        }
    }
    return false;
}

static void clock_tick(void *ctx_)
{
    clock_ctx_t *ctx = ctx_;
    int cx = CLOCK_CX;
    int cy = CLOCK_CY;

    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    int hour_val = (t.tm_hour % 12) * 5 + t.tm_min / 12;
    float hour_angle = hour_val * 6.0f;
    float min_angle = t.tm_min * 6.0f;
    float sec_angle = t.tm_sec * 6.0f;

    set_line_endpoints(ctx->pts_h, cx, cy, hour_angle, -10, 55);
    lv_line_set_points(ctx->needle_h, ctx->pts_h, 2);

    set_line_endpoints(ctx->pts_m, cx, cy, min_angle, -10, 75);
    lv_line_set_points(ctx->needle_m, ctx->pts_m, 2);

    set_line_endpoints(ctx->pts_s, cx, cy, sec_angle, -15, 82);
    lv_line_set_points(ctx->needle_s, ctx->pts_s, 2);

    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d/%d", days[t.tm_wday], t.tm_mon + 1, t.tm_mday);
    lv_label_set_text(ctx->date_label, buf);
}

const app_descriptor_t app_descriptor_clock = {
    .name          = "clock",
    .display_name  = "Clock",
    .icon          = NULL,
    .category      = APP_CAT_SYSTEM,
    .flags         = APP_FLAG_NO_AUTO_DESTROY | APP_FLAG_HIDE_FROM_LAUNCHER,
    .create        = clock_create,
    .show          = clock_show,
    .hide          = clock_hide,
    .destroy       = clock_destroy,
    .handle_event  = clock_handle_event,
    .tick          = clock_tick,
};
