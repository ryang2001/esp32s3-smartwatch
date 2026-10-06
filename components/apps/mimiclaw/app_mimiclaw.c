#include "app_base.h"
#include "app_registry.h"
#include "app_manager.h"
#include "theme.h"
#include "board.h"
#include "mimiclaw.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_mimiclaw";

typedef struct {
    lv_obj_t    *bg;
    lv_obj_t    *chat_cont;
    lv_obj_t    *input_bar;
    lv_obj_t    *input_field;
    lv_obj_t    *send_btn;
    lv_obj_t    *status_label;
    lv_obj_t    *kb;
    lv_obj_t    *kb_bg;
    bool         kb_visible;
    bool         visible;
    char        *pending_response;   /* Queued AI response while hidden */
    bool         pending_done;
} mimiclaw_ctx_t;

#define TITLE_BAR_H   28
#define INPUT_BAR_H   40
#define KB_HEIGHT     140
#define CHAT_PAD      4
#define BUBBLE_W      (DISPLAY_H_RES - CHAT_PAD * 2 - 16)

static mimiclaw_ctx_t *s_ctx = NULL;
static void on_response(const char *text, bool done);

/* ── Helpers ──────────────────────────────────────────────────── */

static void add_bubble(mimiclaw_ctx_t *ctx, const char *text, bool is_user)
{
    lv_obj_t *bubble = lv_label_create(ctx->chat_cont);
    lv_label_set_text(bubble, text);
    lv_obj_set_style_text_font(bubble, theme_font_normal(), 0);
    lv_obj_set_width(bubble, BUBBLE_W);
    lv_obj_set_style_text_color(bubble, COLOR_FG, 0);
    lv_obj_set_style_bg_color(bubble,
        is_user ? COLOR_ACCENT : COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bubble, 8, 0);
    lv_obj_set_style_radius(bubble, 12, 0);
    lv_obj_set_style_pad_row(bubble, 4, 0);
}

static void scroll_to_bottom(mimiclaw_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->chat_cont);
    lv_coord_t scroll_y = lv_obj_get_scroll_y(ctx->chat_cont);
    lv_obj_scroll_to_y(ctx->chat_cont, scroll_y + 200, LV_ANIM_OFF);
}

/* ── Layout ──────────────────────────────────────────────────── */

static void layout_normal(mimiclaw_ctx_t *ctx)
{
    lv_coord_t chat_h = APP_AREA_H - TITLE_BAR_H - INPUT_BAR_H - 8;
    lv_obj_set_size(ctx->chat_cont, DISPLAY_H_RES, chat_h);
    lv_obj_set_pos(ctx->chat_cont, 0, TITLE_BAR_H);
    lv_obj_set_size(ctx->input_bar, DISPLAY_H_RES, INPUT_BAR_H);
    lv_obj_set_style_align(ctx->input_bar, LV_ALIGN_TOP_LEFT, 0);
    lv_obj_set_pos(ctx->input_bar, 0, APP_AREA_H - INPUT_BAR_H);
}

static void layout_keyboard(mimiclaw_ctx_t *ctx)
{
    lv_coord_t kb_top = APP_AREA_H - KB_HEIGHT;
    lv_coord_t input_y = kb_top - INPUT_BAR_H;
    lv_coord_t chat_h = input_y - TITLE_BAR_H;

    lv_obj_set_size(ctx->chat_cont, DISPLAY_H_RES, chat_h > 0 ? chat_h : 0);
    lv_obj_set_pos(ctx->chat_cont, 0, TITLE_BAR_H);
    lv_obj_set_style_align(ctx->input_bar, LV_ALIGN_TOP_LEFT, 0);
    lv_obj_set_size(ctx->input_bar, DISPLAY_H_RES, INPUT_BAR_H);
    lv_obj_set_pos(ctx->input_bar, 0, input_y);
}

/* ── Keyboard ─────────────────────────────────────────────────── */

static void kb_close(mimiclaw_ctx_t *ctx)
{
    if (!ctx->kb_visible) return;
    lv_obj_del(ctx->kb);
    lv_obj_del(ctx->kb_bg);
    ctx->kb = NULL;
    ctx->kb_bg = NULL;
    ctx->kb_visible = false;
    layout_normal(ctx);
    scroll_to_bottom(ctx);
}

static void kb_open(mimiclaw_ctx_t *ctx);

static void kb_bg_click_cb(lv_event_t *e)
{
    mimiclaw_ctx_t *ctx = lv_event_get_user_data(e);
    kb_close(ctx);
}

static void kb_event_cb(lv_event_t *e)
{
    mimiclaw_ctx_t *ctx = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        const char *text = lv_textarea_get_text(ctx->input_field);
        if (text && text[0]) {
            char *msg = strdup(text);
            lv_textarea_set_text(ctx->input_field, "");
            kb_close(ctx);

            add_bubble(ctx, msg, true);
            scroll_to_bottom(ctx);

            ctx->status_label = lv_label_create(ctx->chat_cont);
            lv_label_set_text(ctx->status_label, "Thinking...");
            lv_obj_set_style_text_color(ctx->status_label, COLOR_DIM, 0);
            lv_obj_set_style_text_font(ctx->status_label, theme_font_normal(), 0);
            scroll_to_bottom(ctx);

            mimiclaw_chat(msg, on_response);
            free(msg);
        }
    } else if (code == LV_EVENT_CANCEL) {
        kb_close(ctx);
    }
}

static void kb_open(mimiclaw_ctx_t *ctx)
{
    if (ctx->kb_visible) return;

    ctx->kb_bg = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(ctx->kb_bg);
    lv_obj_set_size(ctx->kb_bg, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_set_pos(ctx->kb_bg, 0, 0);
    lv_obj_set_style_bg_color(ctx->kb_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->kb_bg, LV_OPA_50, 0);
    lv_obj_add_flag(ctx->kb_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ctx->kb_bg, kb_bg_click_cb,
        LV_EVENT_CLICKED, ctx);

    ctx->kb = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(ctx->kb, DISPLAY_H_RES, KB_HEIGHT);
    lv_obj_align(ctx->kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(ctx->kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ctx->kb, ctx->input_field);
    lv_obj_add_event_cb(ctx->kb, kb_event_cb, LV_EVENT_ALL, ctx);
    ctx->kb_visible = true;

    layout_keyboard(ctx);
    scroll_to_bottom(ctx);
}

/* ── Input callbacks ──────────────────────────────────────────── */

static void input_focus_cb(lv_event_t *e)
{
    mimiclaw_ctx_t *ctx = lv_event_get_user_data(e);
    kb_open(ctx);
}

static void send_btn_cb(lv_event_t *e)
{
    mimiclaw_ctx_t *ctx = lv_event_get_user_data(e);
    const char *text = lv_textarea_get_text(ctx->input_field);
    if (!text || !text[0]) return;

    char *msg = strdup(text);
    kb_close(ctx);
    lv_textarea_set_text(ctx->input_field, "");

    add_bubble(ctx, msg, true);
    scroll_to_bottom(ctx);

    ctx->status_label = lv_label_create(ctx->chat_cont);
    lv_label_set_text(ctx->status_label, "Thinking...");
    lv_obj_set_style_text_color(ctx->status_label, COLOR_DIM, 0);
    lv_obj_set_style_text_font(ctx->status_label, theme_font_normal(), 0);
    scroll_to_bottom(ctx);

    mimiclaw_chat(msg, on_response);
    free(msg);
}

/* ── Response callback (called from chat task on core 1) ──────── */

static void on_response(const char *text, bool done)
{
    if (!s_ctx) return;
    if (!lvgl_port_lock(100)) return;

    if (!s_ctx->visible) {
        /* App is in background — queue response for show() */
        if (s_ctx->status_label) {
            lv_obj_del(s_ctx->status_label);
            s_ctx->status_label = NULL;
        }
        if (s_ctx->pending_response) free(s_ctx->pending_response);
        s_ctx->pending_response = (done && text && text[0]) ? strdup(text) : NULL;
        s_ctx->pending_done = done;
        lvgl_port_unlock();
        return;
    }

    if (s_ctx->status_label) {
        lv_obj_del(s_ctx->status_label);
        s_ctx->status_label = NULL;
    }

    if (done && text && text[0]) {
        add_bubble(s_ctx, text, false);
        scroll_to_bottom(s_ctx);
    } else if (done) {
        add_bubble(s_ctx, "No response", false);
        scroll_to_bottom(s_ctx);
    }

    lvgl_port_unlock();
}

/* ── App lifecycle ────────────────────────────────────────────── */

static void *mimiclaw_create(lv_obj_t *parent)
{
    mimiclaw_ctx_t *ctx = calloc(1, sizeof(mimiclaw_ctx_t));
    if (!ctx) return NULL;

    ctx->bg = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->bg);
    lv_obj_set_size(ctx->bg, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_set_pos(ctx->bg, 0, 0);
    lv_obj_set_style_bg_color(ctx->bg, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ctx->bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ctx->bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(ctx->bg);
    lv_label_set_text(title, "AI Chat");
    lv_obj_set_style_text_color(title, COLOR_FG, 0);
    lv_obj_set_style_text_font(title, theme_font_medium(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 4);

    ctx->chat_cont = lv_obj_create(ctx->bg);
    lv_obj_remove_style_all(ctx->chat_cont);
    lv_obj_set_style_bg_opa(ctx->chat_cont, 0, 0);
    lv_obj_add_flag(ctx->chat_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ctx->chat_cont, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(ctx->chat_cont, LV_DIR_VER);
    lv_obj_set_style_pad_all(ctx->chat_cont, CHAT_PAD, 0);
    lv_obj_set_flex_flow(ctx->chat_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->chat_cont,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(ctx->chat_cont, 8, 0);

    ctx->input_bar = lv_obj_create(ctx->bg);
    lv_obj_remove_style_all(ctx->input_bar);
    lv_obj_set_style_bg_color(ctx->input_bar, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(ctx->input_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(ctx->input_bar, 8, 0);
    lv_obj_set_style_pad_ver(ctx->input_bar, 4, 0);
    lv_obj_clear_flag(ctx->input_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctx->input_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->input_bar,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->input_bar, 8, 0);

    ctx->input_field = lv_textarea_create(ctx->input_bar);
    lv_textarea_set_one_line(ctx->input_field, true);
    lv_textarea_set_placeholder_text(ctx->input_field, "Type a message...");
    lv_obj_set_style_text_font(ctx->input_field, theme_font_normal(), 0);
    lv_obj_set_flex_grow(ctx->input_field, 1);
    lv_obj_set_style_bg_color(ctx->input_field, COLOR_SURFACE_ELEVATED, 0);
    lv_obj_set_style_bg_opa(ctx->input_field, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ctx->input_field, COLOR_FG, 0);
    lv_obj_set_style_border_color(ctx->input_field, COLOR_DIM, 0);
    lv_obj_set_style_border_width(ctx->input_field, 1, 0);
    lv_obj_set_style_radius(ctx->input_field, 8, 0);
    lv_obj_set_style_pad_all(ctx->input_field, 6, 0);
    lv_obj_add_event_cb(ctx->input_field, input_focus_cb,
        LV_EVENT_FOCUSED, ctx);

    ctx->send_btn = lv_btn_create(ctx->input_bar);
    lv_obj_set_size(ctx->send_btn, 36, 32);
    lv_obj_set_style_bg_color(ctx->send_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(ctx->send_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->send_btn, 8, 0);
    lv_obj_add_event_cb(ctx->send_btn, send_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *btn_label = lv_label_create(ctx->send_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(btn_label, COLOR_FG, 0);
    lv_obj_center(btn_label);

    layout_normal(ctx);
    s_ctx = ctx;
    return ctx;
}

static void mimiclaw_show(void *ctx)
{
    mimiclaw_ctx_t *mctx = (mimiclaw_ctx_t *)ctx;
    mctx->visible = true;

    /* Apply any queued response from background */
    if (mctx->pending_done) {
        if (mctx->pending_response) {
            add_bubble(mctx, mctx->pending_response, false);
            free(mctx->pending_response);
            mctx->pending_response = NULL;
        } else {
            add_bubble(mctx, "No response", false);
        }
        mctx->pending_done = false;
        scroll_to_bottom(mctx);
    }
}

static void mimiclaw_hide(void *ctx)
{
    mimiclaw_ctx_t *mctx = (mimiclaw_ctx_t *)ctx;
    mctx->visible = false;
    kb_close(mctx);
}

static void mimiclaw_destroy(void *ctx)
{
    mimiclaw_ctx_t *mctx = (mimiclaw_ctx_t *)ctx;
    if (mimiclaw_is_busy()) {
        mimiclaw_cancel();
    }
    s_ctx = NULL;
    if (mctx->pending_response) free(mctx->pending_response);
    free(mctx);
}

static bool mimiclaw_handle_event(void *ctx, const app_event_t *event)
{
    if (!event) return false;
    if (event->type == APP_EVENT_GESTURE_SWIPE_RIGHT) {
        app_manager_go_back(true);
        return true;
    }
    return false;
}

const app_descriptor_t app_descriptor_mimiclaw = {
    .name          = "mimiclaw",
    .display_name  = "AI Chat",
    .icon          = NULL,
    .category      = APP_CAT_TOOL,
    .flags         = 0,
    .create        = mimiclaw_create,
    .show          = mimiclaw_show,
    .hide          = mimiclaw_hide,
    .destroy       = mimiclaw_destroy,
    .handle_event  = mimiclaw_handle_event,
    .tick          = NULL,
};
