#ifndef APP_BASE_H
#define APP_BASE_H

#include "lvgl.h"
#include <stdbool.h>

typedef struct app_base app_base_t;

typedef enum {
    APP_STATE_UNINITIALIZED = 0,
    APP_STATE_STOPPED,
    APP_STATE_PAUSED,
    APP_STATE_RUNNING,
} app_state_t;

typedef enum {
    APP_EVENT_GESTURE_SWIPE_UP    = 0x01,
    APP_EVENT_GESTURE_SWIPE_DOWN  = 0x02,
    APP_EVENT_GESTURE_SWIPE_LEFT  = 0x04,
    APP_EVENT_GESTURE_SWIPE_RIGHT = 0x08,
    APP_EVENT_GESTURE_TAP         = 0x10,
    APP_EVENT_GESTURE_LONG_PRESS  = 0x20,
    APP_EVENT_TIMEOUT             = 0x80,
    APP_EVENT_MESSAGE             = 0x100,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        struct { int16_t x; int16_t y; } tap;
        struct { int dx; int dy; } swipe;
        struct { int16_t x; int16_t y; } long_press;
        struct { uint32_t msg_id; void *data; } message;
    };
} app_event_t;

typedef void *(*app_create_fn)(lv_obj_t *parent);
typedef void  (*app_show_fn)(void *ctx);
typedef void  (*app_hide_fn)(void *ctx);
typedef void  (*app_destroy_fn)(void *ctx);
typedef bool  (*app_handle_event_fn)(void *ctx, const app_event_t *event);
typedef void  (*app_tick_fn)(void *ctx);

struct app_base {
    const char         *name;
    const char         *display_name;
    const void         *icon;
    app_create_fn       create;
    app_show_fn         show;
    app_hide_fn         hide;
    app_destroy_fn      destroy;
    app_handle_event_fn handle_event;
    app_tick_fn         tick;
    uint32_t            flags;

    // Runtime state (managed by app_manager)
    app_state_t         state;
    void               *ctx;
    lv_obj_t           *container;
};

#endif // APP_BASE_H
