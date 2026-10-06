#include "app_manager.h"
#include "app_registry.h"
#include "gesture.h"
#include "sys_ui.h"
#include "theme.h"
#include "board.h"
#include "display.h"
#include "touch.h"
#include "board.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_mgr";

#define RETAINED_MAX 6

static struct {
    lv_obj_t     *screen;
    lv_obj_t     *content_area;
    nav_stack_t   stack;
    lv_timer_t   *tick_timer;
    volatile int  pending_sys_action;  // 0=none, 1=notifications, 2=quick_settings, 3=dismiss
    bool          pending_go_back;
    app_event_t   pending_app_event;
    bool          has_pending_app_event;
    app_base_t   *retained[RETAINED_MAX];
    int           retained_count;
} s_mgr;

static void anim_opa_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_style_opa(obj, v, 0);
}

static void fade_in(lv_obj_t *obj, uint32_t duration)
{
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void fade_out_and_del_cb(lv_anim_t *a)
{
    app_base_t *app = a->user_data;
    if (app->destroy && app->ctx) app->destroy(app->ctx);
    if (app->container) lv_obj_delete(app->container);
    free(app);
}

static void fade_out_destroy(app_base_t *app, uint32_t duration)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, app->container);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, fade_out_and_del_cb);
    a.user_data = app;
    lv_anim_start(&a);
}

static void tick_cb(lv_timer_t *timer)
{
    sys_ui_update_time();

    app_base_t *fg = nav_stack_top(&s_mgr.stack);
    if (fg && fg->state == APP_STATE_RUNNING && fg->tick) {
        fg->tick(fg->ctx);
    }

    /* Background tick for retained apps that opt in */
    for (int i = 0; i < s_mgr.retained_count; i++) {
        app_base_t *app = s_mgr.retained[i];
        if ((app->flags & APP_FLAG_BACKGROUND_TICK) && app->tick) {
            app->tick(app->ctx);
        }
    }
}

// Process deferred app actions outside indev context where event_head is safe
static void deferred_action_cb(void *arg)
{
    // System overlay actions
    int action = s_mgr.pending_sys_action;
    s_mgr.pending_sys_action = 0;
    if (action == 1) { sys_ui_show_notifications(); return; }
    if (action == 2) { sys_ui_show_quick_settings(); return; }
    if (action == 3) { sys_ui_dismiss_overlay(); return; }

    // App go-back
    if (s_mgr.pending_go_back) {
        s_mgr.pending_go_back = false;
        app_manager_go_back(true);
        return;
    }
    // App event
    if (s_mgr.has_pending_app_event) {
        s_mgr.has_pending_app_event = false;
        app_base_t *fg = nav_stack_top(&s_mgr.stack);
        if (fg && fg->handle_event) {
            fg->handle_event(fg->ctx, &s_mgr.pending_app_event);
        }
    }
}

static void on_gesture(const gesture_event_t *g, void *user_data)
{
    (void)user_data;

    if (sys_ui_overlay_is_visible()) {
        s_mgr.pending_sys_action = 3;
        lv_async_call(deferred_action_cb, NULL);
        return;
    }

    // System gestures (deferred — can't modify object tree during indev)
    if (g->start_y < STATUS_BAR_HEIGHT && g->type == GESTURE_SWIPE_DOWN) {
        s_mgr.pending_sys_action = 1;
        lv_async_call(deferred_action_cb, NULL);
        return;
    }
    if (g->start_y > (DISPLAY_V_RES - 28) && g->type == GESTURE_SWIPE_UP) {
        s_mgr.pending_sys_action = 2;
        lv_async_call(deferred_action_cb, NULL);
        return;
    }
    if (g->type == GESTURE_SWIPE_RIGHT && g->start_x < 20) {
        s_mgr.pending_go_back = true;
        lv_async_call(deferred_action_cb, NULL);
        return;
    }

    // Forward to foreground app — deferred because apps may launch/go_back
    // which modify the object tree (unsafe during indev processing)
    app_base_t *fg = nav_stack_top(&s_mgr.stack);
    if (!fg || !fg->handle_event) return;

    app_event_t evt = {0};
    switch (g->type) {
    case GESTURE_TAP:
        evt.type = APP_EVENT_GESTURE_TAP;
        evt.tap.x = g->end_x;
        evt.tap.y = g->end_y;
        break;
    case GESTURE_SWIPE_UP:
        evt.type = APP_EVENT_GESTURE_SWIPE_UP;
        evt.swipe.dx = g->end_x - g->start_x;
        evt.swipe.dy = g->end_y - g->start_y;
        break;
    case GESTURE_SWIPE_DOWN:
        evt.type = APP_EVENT_GESTURE_SWIPE_DOWN;
        evt.swipe.dx = g->end_x - g->start_x;
        evt.swipe.dy = g->end_y - g->start_y;
        break;
    case GESTURE_SWIPE_LEFT:
        evt.type = APP_EVENT_GESTURE_SWIPE_LEFT;
        evt.swipe.dx = g->end_x - g->start_x;
        evt.swipe.dy = g->end_y - g->start_y;
        break;
    case GESTURE_SWIPE_RIGHT:
        evt.type = APP_EVENT_GESTURE_SWIPE_RIGHT;
        evt.swipe.dx = g->end_x - g->start_x;
        evt.swipe.dy = g->end_y - g->start_y;
        break;
    case GESTURE_LONG_PRESS:
        evt.type = APP_EVENT_GESTURE_LONG_PRESS;
        evt.long_press.x = g->end_x;
        evt.long_press.y = g->end_y;
        break;
    default:
        return;
    }
    s_mgr.pending_app_event = evt;
    s_mgr.has_pending_app_event = true;
    lv_async_call(deferred_action_cb, NULL);
}

static void destroy_app(app_base_t *app)
{
    if (!app) return;
    if (app->destroy && app->ctx) app->destroy(app->ctx);
    if (app->container) lv_obj_delete(app->container);
    free(app);
}

/* ── Retained pool ──────────────────────────────────────────── */

static app_base_t *find_retained(const char *name)
{
    for (int i = 0; i < s_mgr.retained_count; i++) {
        if (strcmp(s_mgr.retained[i]->name, name) == 0)
            return s_mgr.retained[i];
    }
    return NULL;
}

static void remove_from_retained(app_base_t *app)
{
    for (int i = 0; i < s_mgr.retained_count; i++) {
        if (s_mgr.retained[i] == app) {
            for (int j = i; j < s_mgr.retained_count - 1; j++)
                s_mgr.retained[j] = s_mgr.retained[j + 1];
            s_mgr.retained_count--;
            return;
        }
    }
}

static void retain_app(app_base_t *app)
{
    if (s_mgr.retained_count >= RETAINED_MAX) {
        destroy_app(s_mgr.retained[0]);
        for (int i = 0; i < s_mgr.retained_count - 1; i++)
            s_mgr.retained[i] = s_mgr.retained[i + 1];
        s_mgr.retained_count--;
    }
    s_mgr.retained[s_mgr.retained_count++] = app;
}

esp_err_t app_manager_init(void)
{
    theme_init();

    s_mgr.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_mgr.screen, COLOR_BG, 0);
    lv_screen_load(s_mgr.screen);

    sys_ui_create(s_mgr.screen);

    s_mgr.content_area = lv_obj_create(s_mgr.screen);
    lv_obj_remove_style_all(s_mgr.content_area);
    lv_obj_set_pos(s_mgr.content_area, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_size(s_mgr.content_area, DISPLAY_H_RES, APP_AREA_H);
    lv_obj_clear_flag(s_mgr.content_area,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    nav_stack_init(&s_mgr.stack);

    gesture_init(board_get_touch_indev(), s_mgr.screen);
    gesture_set_callback(on_gesture, NULL);

    s_mgr.tick_timer = lv_timer_create(tick_cb, 1000, NULL);

    app_manager_launch("clock", false);

#if TOUCH_DEBUG
    touch_debug_init();
#endif

    ESP_LOGI(TAG, "App manager initialized");
    return ESP_OK;
}

esp_err_t app_manager_launch(const char *name, bool anim)
{
    const app_descriptor_t *desc = app_registry_find(name);
    if (!desc) {
        ESP_LOGE(TAG, "App not found: %s", name);
        return ESP_ERR_NOT_FOUND;
    }

    app_base_t *current = nav_stack_top(&s_mgr.stack);
    if (current && strcmp(current->name, name) == 0) return ESP_OK;

    /* Check retained pool first — reuse existing instance if available */
    app_base_t *app = find_retained(name);
    if (app) {
        remove_from_retained(app);
    } else {
        app = calloc(1, sizeof(app_base_t));
        if (!app) return ESP_ERR_NO_MEM;

        app->name = desc->name;
        app->display_name = desc->display_name;
        app->icon = desc->icon;
        app->create = desc->create;
        app->show = desc->show;
        app->hide = desc->hide;
        app->destroy = desc->destroy;
        app->handle_event = desc->handle_event;
        app->tick = desc->tick;
        app->flags = desc->flags;
        app->state = APP_STATE_UNINITIALIZED;

        app->container = lv_obj_create(s_mgr.content_area);
        lv_obj_remove_style_all(app->container);
        lv_obj_set_pos(app->container, 0, 0);
        lv_obj_set_size(app->container, DISPLAY_H_RES, APP_AREA_H);
        lv_obj_clear_flag(app->container, LV_OBJ_FLAG_SCROLLABLE);

        app->ctx = app->create(app->container);
        app->state = APP_STATE_STOPPED;
    }

    /* Hide current foreground */
    if (current) {
        if (current->hide) current->hide(current->ctx);
        current->state = APP_STATE_PAUSED;
        if (anim) {
            lv_obj_set_style_opa(current->container, LV_OPA_TRANSP, 0);
        } else {
            lv_obj_add_flag(current->container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_clear_flag(app->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(app->container, LV_OPA_COVER, 0);
    nav_stack_push(&s_mgr.stack, app);

    if (app->show) app->show(app->ctx);
    app->state = APP_STATE_RUNNING;

    if (anim) {
        fade_in(app->container, 80);
    }

    ESP_LOGI(TAG, "Launched: %s (depth: %d, retained: %d)",
             name, nav_stack_depth(&s_mgr.stack), s_mgr.retained_count);
    return ESP_OK;
}

esp_err_t app_manager_go_back(bool anim)
{
    if (nav_stack_depth(&s_mgr.stack) <= 1) return ESP_FAIL;

    app_base_t *popped = nav_stack_pop(&s_mgr.stack);
    if (popped->hide) popped->hide(popped->ctx);
    popped->state = APP_STATE_PAUSED;
    lv_obj_add_flag(popped->container, LV_OBJ_FLAG_HIDDEN);

    retain_app(popped);

    app_base_t *prev = nav_stack_top(&s_mgr.stack);
    if (prev) {
        lv_obj_clear_flag(prev->container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(prev->container, LV_OPA_COVER, 0);
        if (prev->show) prev->show(prev->ctx);
        prev->state = APP_STATE_RUNNING;
    }

    ESP_LOGI(TAG, "Back (depth: %d, retained: %d)",
             nav_stack_depth(&s_mgr.stack), s_mgr.retained_count);
    return ESP_OK;
}

esp_err_t app_manager_go_home(bool anim)
{
    while (nav_stack_depth(&s_mgr.stack) > 1) {
        app_base_t *popped = nav_stack_pop(&s_mgr.stack);
        if (popped->hide) popped->hide(popped->ctx);
        popped->state = APP_STATE_PAUSED;
        lv_obj_add_flag(popped->container, LV_OBJ_FLAG_HIDDEN);
        retain_app(popped);
    }

    app_base_t *home = nav_stack_top(&s_mgr.stack);
    if (home) {
        lv_obj_clear_flag(home->container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(home->container, LV_OPA_COVER, 0);
        if (home->show) home->show(home->ctx);
        home->state = APP_STATE_RUNNING;
    }

    ESP_LOGI(TAG, "Home (retained: %d)", s_mgr.retained_count);
    return ESP_OK;
}

const nav_stack_t *app_manager_get_stack(void)
{
    return &s_mgr.stack;
}

app_base_t *app_manager_get_foreground(void)
{
    return nav_stack_top(&s_mgr.stack);
}

void app_manager_post_event(const app_event_t *event)
{
    app_base_t *fg = nav_stack_top(&s_mgr.stack);
    if (fg && fg->handle_event) {
        fg->handle_event(fg->ctx, event);
    }
}

lv_obj_t *app_manager_get_content_area(void)
{
    return s_mgr.content_area;
}
