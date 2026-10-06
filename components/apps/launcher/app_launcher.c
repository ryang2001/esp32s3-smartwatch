#include "app_base.h"
#include "app_registry.h"
#include "app_manager.h"
#include "theme.h"
#include "board.h"
#include <stdlib.h>

#define GRID_COLS 4
#define CELL_W    (DISPLAY_H_RES / GRID_COLS)  // 60px
#define CELL_H    72
#define ICON_SIZE (CELL_W - 8)                 // 52px — nearly fills cell width

typedef struct {
    lv_obj_t *grid;
    int       count;
} launcher_ctx_t;

static void icon_click_cb(lv_event_t *e)
{
    const char *app_name = lv_event_get_user_data(e);
    if (app_name) {
        app_manager_launch(app_name, true);
    }
}

static void *launcher_create(lv_obj_t *parent)
{
    launcher_ctx_t *ctx = calloc(1, sizeof(launcher_ctx_t));

    ctx->grid = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->grid);
    lv_obj_set_pos(ctx->grid, 0, 0);
    lv_obj_set_size(ctx->grid, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_set_style_bg_color(ctx->grid, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ctx->grid, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(ctx->grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ctx->grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->grid, 8, 0);
    lv_obj_set_style_pad_column(ctx->grid, 0, 0);
    lv_obj_set_scroll_dir(ctx->grid, LV_DIR_VER);
    lv_obj_set_style_pad_all(ctx->grid, 4, 0);

    const app_descriptor_t *apps[16];
    int count = app_registry_get_launcher_apps(apps, 16);
    ctx->count = count;

    for (int i = 0; i < count; i++) {
        lv_obj_t *cell = lv_obj_create(ctx->grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, CELL_W, CELL_H);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(cell, 12, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(cell, COLOR_SURFACE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_80, LV_STATE_PRESSED);
        lv_obj_add_event_cb(cell, icon_click_cb, LV_EVENT_CLICKED,
                            (void *)apps[i]->name);

        lv_obj_t *label = lv_label_create(cell);
        lv_label_set_text(label, apps[i]->display_name);
        lv_obj_set_style_text_color(label, COLOR_FG, 0);
        lv_obj_set_style_text_font(label, theme_font_normal(), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);

        if (apps[i]->icon) {
            lv_obj_t *img = lv_image_create(cell);
            lv_image_set_src(img, apps[i]->icon);
            lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 2);
            lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_t *box = lv_obj_create(cell);
            lv_obj_remove_style_all(box);
            lv_obj_set_size(box, ICON_SIZE, ICON_SIZE);
            lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 2);
            lv_obj_set_style_bg_color(box, COLOR_ACCENT, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_20, 0);
            lv_obj_set_style_radius(box, ICON_SIZE / 2, 0);
            lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *letter = lv_label_create(box);
            char ch[2] = {apps[i]->display_name[0], '\0'};
            lv_label_set_text(letter, ch);
            lv_obj_set_style_text_color(letter, COLOR_FG, 0);
            lv_obj_set_style_text_font(letter, theme_font_medium(), 0);
            lv_obj_center(letter);
        }
    }

    return ctx;
}

static void launcher_show(void *ctx)
{
    (void)ctx;
}

static void launcher_hide(void *ctx)
{
    (void)ctx;
}

static void launcher_destroy(void *ctx)
{
    free(ctx);
}

static bool launcher_handle_event(void *ctx, const app_event_t *event)
{
    (void)ctx;
    if (event->type == APP_EVENT_GESTURE_SWIPE_RIGHT) {
        app_manager_go_back(true);
        return true;
    }
    return false;
}

const app_descriptor_t app_descriptor_launcher = {
    .name          = "launcher",
    .display_name  = "Apps",
    .icon          = NULL,
    .category      = APP_CAT_SYSTEM,
    .flags         = APP_FLAG_HIDE_FROM_LAUNCHER,
    .create        = launcher_create,
    .show          = launcher_show,
    .hide          = launcher_hide,
    .destroy       = launcher_destroy,
    .handle_event  = launcher_handle_event,
    .tick          = NULL,
};
