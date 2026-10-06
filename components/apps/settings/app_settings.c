#include "app_base.h"
#include "app_registry.h"
#include "app_manager.h"
#include "backlight.h"
#include "theme.h"
#include "board.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "wifi.h"
#include "ota_update.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef enum {
    WIFI_SUB_STATUS,
    WIFI_SUB_SCANNING,
    WIFI_SUB_RESULTS,
    WIFI_SUB_PASSWORD,
    WIFI_SUB_CONNECTING,
} wifi_sub_page_t;

typedef struct {
    lv_obj_t *list;
    int       page;  // 0=main, 1=wifi, 2=display, 3=about, 4=performance, 5=firmware

    // WiFi sub-page state (valid only when page == 1)
    wifi_sub_page_t wifi_sub;
    lv_obj_t *wifi_btn;
    lv_obj_t *wifi_btn_lbl;
    lv_obj_t *wifi_info;
    lv_obj_t *scan_btn;

    // Scan results
    wifi_ap_record_t *scan_results;
    uint16_t          scan_count;

    // Password entry
    char      selected_ssid[33];
    bool      selected_is_open;
    lv_obj_t *pwd_textarea;
    lv_obj_t *pwd_kb;

    // Firmware page state (valid only when page == 5)
    lv_obj_t *fw_cur_lbl;
    lv_obj_t *fw_new_lbl;
    lv_obj_t *fw_notes_lbl;
    lv_obj_t *fw_bar;
    lv_obj_t *fw_pct_lbl;
    lv_obj_t *fw_btn;
    lv_obj_t *fw_btn_lbl;
    lv_obj_t *fw_status_lbl;
    uint32_t  fw_reboot_armed;  // lv_tick of arm time, 0 = not armed
} settings_ctx_t;

static settings_ctx_t *s_active_settings_ctx = NULL;

static void fw_clear_ptrs(settings_ctx_t *s);
static void settings_show_firmware(settings_ctx_t *ctx, lv_obj_t *parent);

/* ── Performance overlay (system-layer, persistent) ──────────── */

static struct {
    lv_obj_t    *label;
    lv_timer_t  *timer;
    uint32_t     frame_count;
    uint32_t     last_tick;
    uint32_t     idle_last;
    bool         active;
} s_perf;

static void perf_render_cb(lv_event_t *e)
{
    (void)e;
    s_perf.frame_count++;
}

static void perf_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_perf.label) return;

    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_perf.last_tick;
    if (elapsed < 500) return;

    uint32_t fps = s_perf.frame_count * 1000 / elapsed;
    uint32_t idle = (uint32_t)ulTaskGetIdleRunTimeCounter();
    uint32_t idle_delta = idle - s_perf.idle_last;
    int cpu_pct = 100 - (int)(idle_delta * 100 / (elapsed * 1000));
    if (cpu_pct < 0) cpu_pct = 0;
    if (cpu_pct > 100) cpu_pct = 100;

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu FPS %d%% CPU", (unsigned long)fps, cpu_pct);
    lv_label_set_text(s_perf.label, buf);

    s_perf.frame_count = 0;
    s_perf.last_tick = now;
    s_perf.idle_last = idle;
}

static void perf_overlay_start(void)
{
    if (s_perf.active) return;

    lv_display_t *disp = lv_display_get_default();
    lv_obj_t *sys_layer = lv_display_get_layer_sys(disp);

    s_perf.label = lv_label_create(sys_layer);
    lv_label_set_text(s_perf.label, "... FPS ...% CPU");
    lv_obj_set_style_text_font(s_perf.label, theme_font_normal(), 0);
    lv_obj_set_style_text_color(s_perf.label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(s_perf.label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_perf.label, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(s_perf.label, 2, 0);
    lv_obj_set_style_radius(s_perf.label, 2, 0);
    lv_obj_align(s_perf.label, LV_ALIGN_TOP_RIGHT, -2, 2);

    s_perf.frame_count = 0;
    s_perf.last_tick = lv_tick_get();
    s_perf.idle_last = (uint32_t)ulTaskGetIdleRunTimeCounter();

    lv_display_add_event_cb(disp, perf_render_cb, LV_EVENT_RENDER_READY, NULL);
    s_perf.timer = lv_timer_create(perf_timer_cb, 500, NULL);
    s_perf.active = true;
}

static void perf_overlay_stop(void)
{
    if (!s_perf.active) return;

    lv_display_remove_event_cb_with_user_data(
        lv_display_get_default(), perf_render_cb, NULL);

    if (s_perf.timer) {
        lv_timer_delete(s_perf.timer);
        s_perf.timer = NULL;
    }
    if (s_perf.label) {
        lv_obj_delete(s_perf.label);
        s_perf.label = NULL;
    }
    s_perf.active = false;
}

/* ── Settings declarations ─────────────────────────────────── */

static void settings_show_main(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_wifi(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_wifi_scanning(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_wifi_results(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_wifi_password(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_wifi_connecting(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_display(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_about(settings_ctx_t *ctx, lv_obj_t *parent);
static void settings_show_performance(settings_ctx_t *ctx, lv_obj_t *parent);
static void page_back_cb(lv_event_t *e);
static void wifi_goto_sub(settings_ctx_t *ctx, wifi_sub_page_t sub);

// --- Helpers ---

static lv_obj_t *create_back_header(lv_obj_t *parent, const char *title,
                                     settings_ctx_t *ctx)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, DISPLAY_H_RES, 36);
    lv_obj_set_style_bg_color(header, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, page_back_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_set_style_bg_opa(header, LV_OPA_30, LV_STATE_PRESSED);

    lv_obj_t *back_label = lv_label_create(header);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " ");
    lv_obj_set_style_text_color(back_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(back_label, theme_font_normal(), 0);
    lv_obj_align(back_label, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, COLOR_FG, 0);
    lv_obj_set_style_text_font(title_label, theme_font_medium(), 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 24, 0);

    return header;
}

// --- WiFi sub-page navigation ---

static void wifi_clear_ptrs(settings_ctx_t *ctx)
{
    ctx->wifi_btn = NULL;
    ctx->wifi_btn_lbl = NULL;
    ctx->wifi_info = NULL;
    ctx->scan_btn = NULL;
    ctx->pwd_textarea = NULL;
    ctx->pwd_kb = NULL;
}

static void wifi_goto_sub(settings_ctx_t *ctx, wifi_sub_page_t sub)
{
    wifi_clear_ptrs(ctx);
    ctx->wifi_sub = sub;
    lv_obj_clean(ctx->list);

    switch (sub) {
    case WIFI_SUB_STATUS:
        settings_show_wifi(ctx, ctx->list);
        break;
    case WIFI_SUB_SCANNING:
        settings_show_wifi_scanning(ctx, ctx->list);
        break;
    case WIFI_SUB_RESULTS:
        settings_show_wifi_results(ctx, ctx->list);
        break;
    case WIFI_SUB_PASSWORD:
        settings_show_wifi_password(ctx, ctx->list);
        break;
    case WIFI_SUB_CONNECTING:
        settings_show_wifi_connecting(ctx, ctx->list);
        break;
    }
}

// --- Navigation callbacks ---

static void page_back_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);

    if (ctx->page == 5) fw_clear_ptrs(ctx);

    if (ctx->page == 1 && ctx->wifi_sub != WIFI_SUB_STATUS) {
        if (ctx->wifi_sub == WIFI_SUB_PASSWORD || ctx->wifi_sub == WIFI_SUB_CONNECTING) {
            wifi_goto_sub(ctx, WIFI_SUB_RESULTS);
        } else {
            wifi_goto_sub(ctx, WIFI_SUB_STATUS);
        }
        return;
    }

    // Back to main settings menu
    wifi_clear_ptrs(ctx);
    if (ctx->scan_results) { free(ctx->scan_results); ctx->scan_results = NULL; }
    ctx->scan_count = 0;
    ctx->wifi_sub = WIFI_SUB_STATUS;
    lv_obj_clean(ctx->list);
    ctx->page = 0;
    settings_show_main(ctx, ctx->list);
}

static void wifi_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    wifi_clear_ptrs(ctx);
    ctx->wifi_sub = WIFI_SUB_STATUS;
    lv_obj_clean(ctx->list);
    ctx->page = 1;
    settings_show_wifi(ctx, ctx->list);
}

static void display_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_clean(ctx->list);
    ctx->page = 2;
    settings_show_display(ctx, ctx->list);
}

static void about_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_clean(ctx->list);
    ctx->page = 3;
    settings_show_about(ctx, ctx->list);
}

static void performance_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_clean(ctx->list);
    ctx->page = 4;
    settings_show_performance(ctx, ctx->list);
}

static void firmware_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_clean(ctx->list);
    ctx->page = 5;
    settings_show_firmware(ctx, ctx->list);
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    board_backlight_set(val);
}

// --- WiFi page callbacks ---

static void wifi_toggle_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    if (wifi_is_connected()) {
        esp_wifi_disconnect();
    } else {
        esp_wifi_disconnect();
        esp_wifi_connect();
        lv_label_set_text(ctx->wifi_btn_lbl, LV_SYMBOL_WIFI "  Connecting...");
    }
}

static void scan_done_cb(wifi_ap_record_t *aps, uint16_t count)
{
    if (!lvgl_port_lock(500)) {
        free(aps);
        return;
    }

    settings_ctx_t *ctx = s_active_settings_ctx;
    if (!ctx || ctx->page != 1) {
        lvgl_port_unlock();
        free(aps);
        return;
    }

    if (ctx->scan_results) free(ctx->scan_results);
    ctx->scan_results = aps;
    ctx->scan_count = count;

    if (ctx->wifi_sub == WIFI_SUB_SCANNING) {
        wifi_goto_sub(ctx, WIFI_SUB_RESULTS);
    }

    lvgl_port_unlock();
}

static void scan_btn_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    wifi_goto_sub(ctx, WIFI_SUB_SCANNING);
    wifi_scan_start(scan_done_cb);
}

static void ap_selected_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = s_active_settings_ctx;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!ctx || idx < 0 || idx >= ctx->scan_count) return;

    wifi_ap_record_t *ap = &ctx->scan_results[idx];
    memset(ctx->selected_ssid, 0, sizeof(ctx->selected_ssid));
    memcpy(ctx->selected_ssid, ap->ssid, sizeof(ap->ssid));
    ctx->selected_is_open = (ap->authmode == WIFI_AUTH_OPEN);

    if (ctx->selected_is_open) {
        wifi_connect(ctx->selected_ssid, "");
        wifi_save_credentials(ctx->selected_ssid, "");
        wifi_goto_sub(ctx, WIFI_SUB_CONNECTING);
    } else {
        wifi_goto_sub(ctx, WIFI_SUB_PASSWORD);
    }
}

static void pwd_ready_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    const char *pwd = lv_textarea_get_text(ctx->pwd_textarea);
    if (strlen(pwd) < 8) return;

    wifi_connect(ctx->selected_ssid, pwd);
    wifi_save_credentials(ctx->selected_ssid, pwd);
    wifi_goto_sub(ctx, WIFI_SUB_CONNECTING);
}

static void pwd_cancel_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    wifi_goto_sub(ctx, WIFI_SUB_RESULTS);
}

// --- Main menu ---

static void settings_show_main(settings_ctx_t *ctx, lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, COLOR_FG, 0);
    lv_obj_set_style_text_font(title, theme_font_medium(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *btn;

    btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_WIFI "  WiFi");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn, wifi_cb, LV_EVENT_CLICKED, ctx);

    btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_IMAGE "  Display");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn, display_cb, LV_EVENT_CLICKED, ctx);

    btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 144);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_CHARGE "  Performance");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn, performance_cb, LV_EVENT_CLICKED, ctx);

    btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 196);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LIST "  About");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn, about_cb, LV_EVENT_CLICKED, ctx);

    btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 248);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_REFRESH "  Firmware");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn, firmware_cb, LV_EVENT_CLICKED, ctx);
}

// --- WiFi sub-pages ---

static void settings_show_wifi(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "WiFi", ctx);

    bool connected = false;
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        connected = true;
    }

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_center(lbl);

    if (connected) {
        lv_obj_set_style_bg_color(btn, COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0);
        lv_label_set_text(lbl, LV_SYMBOL_WIFI "  Disconnect");
    } else {
        lv_obj_set_style_bg_color(btn, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_label_set_text(lbl, "Connect");
    }

    lv_obj_add_event_cb(btn, wifi_toggle_cb, LV_EVENT_CLICKED, ctx);
    ctx->wifi_btn = btn;
    ctx->wifi_btn_lbl = lbl;

    lv_obj_t *info = lv_label_create(parent);
    char buf[128];
    if (connected) {
        char ssid[33] = {0};
        memcpy(ssid, ap.ssid, sizeof(ap.ssid));
        snprintf(buf, sizeof(buf),
                 "SSID: %s\n"
                 "RSSI: %d dBm\n"
                 "Channel: %d",
                 ssid, ap.rssi, ap.primary);
    } else {
        snprintf(buf, sizeof(buf), "Not connected");
    }
    lv_label_set_text(info, buf);
    lv_obj_set_style_text_color(info, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(info, theme_font_normal(), 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 10, 96);
    ctx->wifi_info = info;

    // Scan button
    lv_obj_t *scan_btn = lv_button_create(parent);
    lv_obj_set_size(scan_btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(scan_btn, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(scan_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scan_btn, 8, 0);
    lv_obj_set_style_bg_color(scan_btn, COLOR_LINK, LV_STATE_PRESSED);
    lbl = lv_label_create(scan_btn);
    lv_label_set_text(lbl, LV_SYMBOL_WIFI "  Scan Networks");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, ctx);
    ctx->scan_btn = scan_btn;
}

static void settings_show_wifi_scanning(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "WiFi", ctx);

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Scanning...");
    lv_obj_set_style_text_color(lbl, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, theme_font_medium(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -10);
}

static void settings_show_wifi_results(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "Scan Results", ctx);

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_pos(cont, 0, 40);
    lv_obj_set_size(cont, DISPLAY_H_RES, APP_AREA_H - 40);
    lv_obj_set_style_bg_color(cont, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(cont, 4, 0);

    if (ctx->scan_count == 0 || !ctx->scan_results) {
        lv_obj_t *empty = lv_label_create(cont);
        lv_label_set_text(empty, "No networks found");
        lv_obj_set_style_text_color(empty, COLOR_DIM, 0);
        lv_obj_set_style_text_font(empty, theme_font_normal(), 0);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 20);
        return;
    }

    lv_obj_t *flex_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(flex_cont);
    lv_obj_set_size(flex_cont, DISPLAY_H_RES - 8, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(flex_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(flex_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(flex_cont, 4, 0);
    lv_obj_clear_flag(flex_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < ctx->scan_count; i++) {
        wifi_ap_record_t *ap = &ctx->scan_results[i];
        char ssid_str[34] = {0};
        memcpy(ssid_str, ap->ssid, sizeof(ap->ssid));

        lv_obj_t *row = lv_button_create(flex_cont);
        lv_obj_set_size(row, DISPLAY_H_RES - 16, 40);
        lv_obj_set_style_bg_color(row, COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, COLOR_SURFACE_ELEVATED, LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_add_event_cb(row, ap_selected_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        bool secured = (ap->authmode != WIFI_AUTH_OPEN);
        char line1[48];
        snprintf(line1, sizeof(line1), "%s%s%s",
                 ssid_str[0] ? ssid_str : "(hidden)",
                 !secured ? " (Open)" : "",
                 "");
        lv_obj_t *l1 = lv_label_create(row);
        lv_label_set_text(l1, line1);
        lv_obj_set_style_text_color(l1, COLOR_FG, 0);
        lv_obj_set_style_text_font(l1, theme_font_normal(), 0);
        lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 6, 3);

        char line2[32];
        snprintf(line2, sizeof(line2), "%d dBm  Ch %d", ap->rssi, ap->primary);
        lv_obj_t *l2 = lv_label_create(row);
        lv_label_set_text(l2, line2);
        lv_obj_set_style_text_color(l2, COLOR_DIM, 0);
        lv_obj_set_style_text_font(l2, theme_font_normal(), 0);
        lv_obj_align(l2, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    }
}

static void settings_show_wifi_password(settings_ctx_t *ctx, lv_obj_t *parent)
{
    // Compact header (28px)
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, DISPLAY_H_RES, 28);
    lv_obj_set_style_bg_color(header, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, pwd_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_set_style_bg_opa(header, LV_OPA_30, LV_STATE_PRESSED);

    lv_obj_t *back = lv_label_create(header);
    lv_label_set_text(back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back, COLOR_ACCENT, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 6, 0);

    char title[40];
    snprintf(title, sizeof(title), "%.20s", ctx->selected_ssid);
    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(title_lbl, theme_font_normal(), 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    // Password textarea (28px)
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, DISPLAY_H_RES - 8, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 30);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "Password");
    lv_textarea_set_max_length(ta, 63);
    lv_obj_set_style_text_font(ta, theme_font_normal(), 0);
    lv_obj_set_style_bg_color(ta, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 4, 0);
    ctx->pwd_textarea = ta;

    // Keyboard fills remaining space (252 - 58 = 194px)
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, DISPLAY_H_RES, APP_AREA_H - 58);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 58);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_color(kb, COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);

    lv_obj_add_event_cb(kb, pwd_ready_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, pwd_cancel_cb, LV_EVENT_CANCEL, ctx);
    ctx->pwd_kb = kb;
}

static void settings_show_wifi_connecting(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "WiFi", ctx);

    lv_obj_t *lbl = lv_label_create(parent);
    char buf[64];
    snprintf(buf, sizeof(buf), "Connecting to\n%.20s...", ctx->selected_ssid);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_medium(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -20);
}

// --- Display & About ---

static void settings_show_display(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "Display", ctx);

    lv_obj_t *bright_label = lv_label_create(parent);
    lv_label_set_text(bright_label, "Brightness");
    lv_obj_set_style_text_color(bright_label, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(bright_label, theme_font_normal(), 0);
    lv_obj_align(bright_label, LV_ALIGN_TOP_LEFT, 10, 50);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, DISPLAY_H_RES - 40);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 75);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, board_backlight_get(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void settings_show_about(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "About", ctx);

    int free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    char buf[128];
    lv_obj_t *info = lv_label_create(parent);
    snprintf(buf, sizeof(buf),
             "ESP32-S3 Smartwatch\n"
             "LVGL 9.x\n"
             "ESP-IDF v6.0\n\n"
             "Free RAM: %d KB\n"
             "Free PSRAM: %d KB",
             free_internal / 1024, free_psram / 1024);
    lv_label_set_text(info, buf);
    lv_obj_set_style_text_color(info, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(info, theme_font_normal(), 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 10, 44);
}

// --- Performance page ---

static void perf_toggle_cb(lv_event_t *e)
{
    if (s_perf.active) {
        perf_overlay_stop();
    } else {
        perf_overlay_start();
    }

    // Rebuild page to update button text
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_clean(ctx->list);
    settings_show_performance(ctx, ctx->list);
}

static void settings_show_performance(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "Performance", ctx);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 20, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(btn,
        s_perf.active ? COLOR_DESTRUCTIVE : COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, perf_toggle_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, s_perf.active ? "Disable Overlay" : "Enable Overlay");
    lv_obj_set_style_text_color(lbl, COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, theme_font_normal(), 0);
    lv_obj_center(lbl);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Shows FPS & CPU%\nin top-right corner");
    lv_obj_set_style_text_color(hint, COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, theme_font_normal(), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 100);
}

// --- Firmware page ---

static void fw_clear_ptrs(settings_ctx_t *s)
{
    s->fw_cur_lbl = NULL;
    s->fw_new_lbl = NULL;
    s->fw_notes_lbl = NULL;
    s->fw_bar = NULL;
    s->fw_pct_lbl = NULL;
    s->fw_btn = NULL;
    s->fw_btn_lbl = NULL;
    s->fw_status_lbl = NULL;
    s->fw_reboot_armed = 0;
}

static void fw_btn_cb(lv_event_t *e)
{
    settings_ctx_t *ctx = lv_event_get_user_data(e);
    ota_status_t st;
    ota_update_get_status(&st);

    switch (st.state) {
    case OTA_STATE_DOWNLOADING:
        ota_update_cancel();
        break;

    case OTA_STATE_READY_REBOOT:
        // two-stage confirm: arm, then confirm within 2s
        if (ctx->fw_reboot_armed && lv_tick_elaps(ctx->fw_reboot_armed) < 2000) {
            ota_update_reboot();
        } else {
            ctx->fw_reboot_armed = lv_tick_get();
            lv_label_set_text(ctx->fw_btn_lbl, "Confirm Reboot?");
            lv_obj_set_style_bg_color(ctx->fw_btn, COLOR_DESTRUCTIVE, 0);
        }
        break;

    case OTA_STATE_AVAILABLE:
        ota_update_start_download();
        break;

    default:  // IDLE / UP_TO_DATE / ERROR / VALIDATED
        if (!ota_update_is_busy()) ota_update_start_check();
        break;
    }
}

// 1s tick from settings_tick while page == 5 (LVGL timer context, no locks)
static void fw_refresh(settings_ctx_t *s)
{
    if (!s->fw_btn) return;

    ota_status_t st;
    ota_update_get_status(&st);
    char buf[160];

    // Version labels
    snprintf(buf, sizeof(buf), "Current: %s", st.cur_version);
    lv_label_set_text(s->fw_cur_lbl, buf);
    if (st.new_version[0]) {
        snprintf(buf, sizeof(buf), "Latest: %s", st.new_version);
    } else {
        snprintf(buf, sizeof(buf), "Latest: --");
    }
    lv_label_set_text(s->fw_new_lbl, buf);
    lv_label_set_text(s->fw_notes_lbl, st.notes);

    // Progress bar + percent/KB text
    if (st.state == OTA_STATE_DOWNLOADING || st.state == OTA_STATE_READY_REBOOT) {
        lv_obj_clear_flag(s->fw_bar, LV_OBJ_FLAG_HIDDEN);
        int pct = (st.state == OTA_STATE_READY_REBOOT) ? 100 : st.percent;
        lv_bar_set_value(s->fw_bar, pct >= 0 ? pct : 0, LV_ANIM_OFF);
        if (pct >= 0) {
            snprintf(buf, sizeof(buf), "%d%%  (%d KB)", pct, st.downloaded_kb);
        } else {
            snprintf(buf, sizeof(buf), "%d KB", st.downloaded_kb);
        }
    } else {
        lv_obj_add_flag(s->fw_bar, LV_OBJ_FLAG_HIDDEN);
        buf[0] = '\0';
    }
    lv_label_set_text(s->fw_pct_lbl, buf);

    // Status line
    const char *msg = "";
    lv_color_t color = COLOR_SECONDARY;
    switch (st.state) {
    case OTA_STATE_CHECKING:     msg = "Checking..."; break;
    case OTA_STATE_UP_TO_DATE:   msg = "Already up to date"; break;
    case OTA_STATE_AVAILABLE:    msg = "New version available"; color = COLOR_ACCENT; break;
    case OTA_STATE_DOWNLOADING:  msg = "Downloading..."; break;
    case OTA_STATE_READY_REBOOT: msg = "Update ready"; color = COLOR_ACCENT; break;
    case OTA_STATE_VALIDATED:    msg = "Firmware updated"; break;
    case OTA_STATE_ERROR:        msg = st.error_msg[0] ? st.error_msg : "Error";
                                 color = COLOR_DESTRUCTIVE; break;
    default: break;
    }
    lv_label_set_text(s->fw_status_lbl, msg);
    lv_obj_set_style_text_color(s->fw_status_lbl, color, 0);

    // Action button (label/color follow state unless reboot confirm armed)
    const char *bl;
    lv_color_t bc = COLOR_ACCENT;
    bool enabled = true;
    switch (st.state) {
    case OTA_STATE_CHECKING:     bl = "Checking..."; enabled = false;
                                 bc = COLOR_SURFACE; break;
    case OTA_STATE_DOWNLOADING:  bl = "Cancel"; bc = COLOR_DESTRUCTIVE; break;
    case OTA_STATE_AVAILABLE:    bl = "Download Update"; break;
    case OTA_STATE_READY_REBOOT: bl = "Reboot to Apply"; break;
    default:                     bl = "Check for Updates"; break;
    }
    if (s->fw_reboot_armed && lv_tick_elaps(s->fw_reboot_armed) >= 2000) {
        s->fw_reboot_armed = 0;  // auto-disarm
    }
    if (st.state == OTA_STATE_READY_REBOOT && s->fw_reboot_armed) {
        bl = "Confirm Reboot?";
        bc = COLOR_DESTRUCTIVE;
    }
    lv_label_set_text(s->fw_btn_lbl, bl);
    lv_obj_set_style_bg_color(s->fw_btn, bc, 0);
    if (enabled) {
        lv_obj_clear_state(s->fw_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s->fw_btn, LV_STATE_DISABLED);
    }
}

static void settings_show_firmware(settings_ctx_t *ctx, lv_obj_t *parent)
{
    create_back_header(parent, "Firmware", ctx);

    lv_obj_t *cur = lv_label_create(parent);
    lv_obj_align(cur, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_text_color(cur, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(cur, theme_font_normal(), 0);
    ctx->fw_cur_lbl = cur;

    lv_obj_t *latest = lv_label_create(parent);
    lv_obj_align(latest, LV_ALIGN_TOP_LEFT, 10, 64);
    lv_obj_set_style_text_color(latest, COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(latest, theme_font_normal(), 0);
    ctx->fw_new_lbl = latest;

    lv_obj_t *notes = lv_label_create(parent);
    lv_label_set_long_mode(notes, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(notes, DISPLAY_H_RES - 24);
    lv_obj_align(notes, LV_ALIGN_TOP_LEFT, 12, 82);
    lv_obj_set_style_text_color(notes, COLOR_DIM, 0);
    lv_obj_set_style_text_font(notes, theme_font_normal(), 0);
    ctx->fw_notes_lbl = notes;

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, DISPLAY_H_RES - 40, 8);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 138);
    lv_bar_set_range(bar, 0, 100);
    ctx->fw_bar = bar;

    lv_obj_t *pct = lv_label_create(parent);
    lv_obj_align(pct, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_text_color(pct, COLOR_DIM, 0);
    lv_obj_set_style_text_font(pct, theme_font_normal(), 0);
    ctx->fw_pct_lbl = pct;

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, DISPLAY_H_RES - 40, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_set_style_bg_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, fw_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Check for Updates");
    lv_obj_set_style_text_color(bl, COLOR_FG, 0);
    lv_obj_set_style_text_font(bl, theme_font_normal(), 0);
    lv_obj_center(bl);
    ctx->fw_btn = btn;
    ctx->fw_btn_lbl = bl;

    lv_obj_t *status = lv_label_create(parent);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, DISPLAY_H_RES - 24);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 12, 222);
    lv_obj_set_style_text_font(status, theme_font_normal(), 0);
    ctx->fw_status_lbl = status;

    fw_refresh(ctx);
}

// --- App lifecycle ---

static void *settings_create(lv_obj_t *parent)
{
    settings_ctx_t *ctx = calloc(1, sizeof(settings_ctx_t));

    ctx->list = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->list);
    lv_obj_set_pos(ctx->list, 0, 0);
    lv_obj_set_size(ctx->list, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_set_style_bg_color(ctx->list, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ctx->list, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(ctx->list, LV_DIR_VER);
    ctx->page = 0;
    ctx->wifi_sub = WIFI_SUB_STATUS;

    s_active_settings_ctx = ctx;

    settings_show_main(ctx, ctx->list);
    return ctx;
}

static void settings_show(void *ctx)
{
    (void)ctx;
}

static void settings_hide(void *ctx)
{
    (void)ctx;
}

static void settings_destroy(void *ctx)
{
    settings_ctx_t *s = (settings_ctx_t *)ctx;
    if (s->scan_results) free(s->scan_results);
    if (s_active_settings_ctx == s) s_active_settings_ctx = NULL;
    free(s);
}

static bool settings_handle_event(void *ctx, const app_event_t *event)
{
    settings_ctx_t *s = (settings_ctx_t *)ctx;
    if (event->type == APP_EVENT_GESTURE_SWIPE_RIGHT) {
        if (s->page > 0) {
            if (s->page == 5) fw_clear_ptrs(s);
            if (s->page == 1 && s->wifi_sub != WIFI_SUB_STATUS) {
                if (s->wifi_sub == WIFI_SUB_PASSWORD || s->wifi_sub == WIFI_SUB_CONNECTING) {
                    wifi_goto_sub(s, WIFI_SUB_RESULTS);
                } else {
                    wifi_goto_sub(s, WIFI_SUB_STATUS);
                }
                return true;
            }
            wifi_clear_ptrs(s);
            if (s->scan_results) { free(s->scan_results); s->scan_results = NULL; }
            s->scan_count = 0;
            s->wifi_sub = WIFI_SUB_STATUS;
            lv_obj_clean(s->list);
            s->page = 0;
            settings_show_main(s, s->list);
            return true;
        }
        app_manager_go_back(true);
        return true;
    }
    return false;
}

static void settings_tick(void *ctx)
{
    settings_ctx_t *s = (settings_ctx_t *)ctx;
    if (s->page == 5) {
        fw_refresh(s);
        return;
    }
    if (s->page != 1) return;

    switch (s->wifi_sub) {
    case WIFI_SUB_STATUS:
        if (!s->wifi_btn) return;
        {
            wifi_ap_record_t ap;
            bool connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
            if (connected) {
                lv_label_set_text(s->wifi_btn_lbl, LV_SYMBOL_WIFI "  Disconnect");
                lv_obj_set_style_bg_color(s->wifi_btn, COLOR_ACCENT, 0);
                lv_obj_set_style_bg_opa(s->wifi_btn, LV_OPA_30, 0);
                char buf[128], ssid[33] = {0};
                memcpy(ssid, ap.ssid, sizeof(ap.ssid));
                snprintf(buf, sizeof(buf),
                         "SSID: %s\nRSSI: %d dBm\nChannel: %d",
                         ssid, ap.rssi, ap.primary);
                lv_label_set_text(s->wifi_info, buf);
            } else {
                lv_label_set_text(s->wifi_btn_lbl, "Connect");
                lv_obj_set_style_bg_color(s->wifi_btn, COLOR_SURFACE, 0);
                lv_obj_set_style_bg_opa(s->wifi_btn, LV_OPA_COVER, 0);
                lv_label_set_text(s->wifi_info, "Not connected");
            }
        }
        break;

    case WIFI_SUB_CONNECTING:
        if (wifi_is_connected()) {
            wifi_goto_sub(s, WIFI_SUB_STATUS);
        }
        break;

    default:
        break;
    }
}

const app_descriptor_t app_descriptor_settings = {
    .name          = "settings",
    .display_name  = "Settings",
    .icon          = NULL,
    .category      = APP_CAT_TOOL,
    .flags         = 0,
    .create        = settings_create,
    .show          = settings_show,
    .hide          = settings_hide,
    .destroy       = settings_destroy,
    .handle_event  = settings_handle_event,
    .tick          = settings_tick,
};
