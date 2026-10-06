#include "gesture.h"
#include "touch.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "gesture";

static struct {
    bool               touching;
    int16_t            start_x, start_y;
    uint32_t           start_time_ms;
    lv_indev_t        *indev;
    lv_timer_t        *poll_timer;
    gesture_callback_t callback;
    void              *user_data;
} s_gesture;

static gesture_event_t classify_gesture(int16_t sx, int16_t sy,
                                         int16_t ex, int16_t ey,
                                         uint32_t elapsed_ms)
{
    gesture_event_t g = {0};
    g.start_x = sx;
    g.start_y = sy;
    g.end_x = ex;
    g.end_y = ey;
    g.start_time_ms = s_gesture.start_time_ms;

    int dx = ex - sx;
    int dy = ey - sy;
    int adx = abs(dx);
    int ady = abs(dy);

    if (adx < GESTURE_TAP_MAX_MOVE && ady < GESTURE_TAP_MAX_MOVE) {
        if (elapsed_ms > GESTURE_LONG_PRESS_MS) {
            g.type = GESTURE_LONG_PRESS;
        } else if (elapsed_ms < GESTURE_TAP_MAX_TIME_MS) {
            g.type = GESTURE_TAP;
        } else {
            g.type = GESTURE_NONE;
        }
    } else if (elapsed_ms < GESTURE_MAX_TIME_MS) {
        if (adx > ady) {
            g.type = (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
        } else {
            g.type = (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
        }
        if ((adx < GESTURE_MIN_DISTANCE) && (ady < GESTURE_MIN_DISTANCE)) {
            g.type = GESTURE_NONE;
        }
    } else {
        g.type = GESTURE_NONE;
    }

    return g;
}

static void gesture_poll_cb(lv_timer_t *timer)
{
    if (!s_gesture.indev) return;

    lv_point_t point;
    lv_indev_get_point(s_gesture.indev, &point);

    bool pressed = touch_is_pressed();

    if (pressed && !s_gesture.touching) {
        // Touch down
        s_gesture.touching = true;
        s_gesture.start_x = point.x;
        s_gesture.start_y = point.y;
        s_gesture.start_time_ms = lv_tick_get();
    } else if (!pressed && s_gesture.touching) {
        // Touch up
        s_gesture.touching = false;
        gesture_event_t g = classify_gesture(
            s_gesture.start_x, s_gesture.start_y,
            point.x, point.y,
            lv_tick_get() - s_gesture.start_time_ms);
        if (g.type != GESTURE_NONE && s_gesture.callback) {
            s_gesture.callback(&g, s_gesture.user_data);
        }
    }
}

void gesture_init(lv_indev_t *indev, lv_obj_t *screen)
{
    s_gesture.indev = indev;
    s_gesture.poll_timer = lv_timer_create(gesture_poll_cb, 50, NULL);
    ESP_LOGI(TAG, "Gesture recognizer installed (poll-based)");
}

void gesture_set_callback(gesture_callback_t cb, void *user_data)
{
    s_gesture.callback = cb;
    s_gesture.user_data = user_data;
}
