#ifndef GESTURE_H
#define GESTURE_H

#include "lvgl.h"

#define GESTURE_MIN_DISTANCE    40
#define GESTURE_MAX_TIME_MS     500
#define GESTURE_LONG_PRESS_MS   800
#define GESTURE_TAP_MAX_MOVE    25
#define GESTURE_TAP_MAX_TIME_MS 400

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_LONG_PRESS,
} gesture_type_t;

typedef struct {
    gesture_type_t type;
    int16_t start_x, start_y;
    int16_t end_x, end_y;
    uint32_t start_time_ms;
} gesture_event_t;

typedef void (*gesture_callback_t)(const gesture_event_t *event, void *user_data);

void gesture_init(lv_indev_t *indev, lv_obj_t *screen);
void gesture_set_callback(gesture_callback_t cb, void *user_data);

#endif // GESTURE_H
