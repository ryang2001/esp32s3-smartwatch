#include "touch.h"
#include "display.h"
#include "board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

static const char *TAG = "touch";
static i2c_master_dev_handle_t s_tp_dev = NULL;
static lv_indev_t *s_touch_indev = NULL;
static volatile bool s_touch_pressed = false;

#if TOUCH_DEBUG
static lv_obj_t *s_debug_dot = NULL;
static volatile int16_t s_dbg_x = 0, s_dbg_y = 0;
static volatile bool s_dbg_pressed = false;
#endif

// Returns: 1 = touch present, 0 = no touch, -1 = I2C error
static int cst816s_read_touch(uint16_t *x, uint16_t *y)
{
    // Read gesture register 0x01 first: consuming it clears the chip's
    // pending-report state, otherwise CST816S re-raises the stale report
    // every ~10s idle-wake cycle (ghost touch at the last position).
    uint8_t gest_reg = 0x01;
    uint8_t gesture = 0;
    i2c_master_transmit_receive(s_tp_dev, &gest_reg, 1, &gesture, 1, 20);

    uint8_t reg = 0x02;
    uint8_t buf[6] = {0};
    esp_err_t ret = i2c_master_transmit_receive(s_tp_dev, &reg, 1, buf, 6, 20);
    if (ret != ESP_OK) return -1;

    uint8_t point_num = buf[0] & 0x0F;
    if (point_num == 0) return 0;

    *x = ((buf[1] & 0x0F) << 8) | buf[2];
    *y = ((buf[3] & 0x0F) << 8) | buf[4];
    return 1;
}

static void IRAM_ATTR touch_isr_handler(void *arg)
{
    lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, s_touch_indev);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int16_t last_x = 0, last_y = 0;
    static bool is_pressed = false;
    static uint8_t release_count = 0;
    static uint8_t press_count = 0;

    uint16_t x = 0, y = 0;
    int result = cst816s_read_touch(&x, &y);

    if (result == 1) {
        // Reject garbage coordinates — CST816S sometimes reports 0xFFF when
        // registers aren't ready or I2C reads stale data
        if (x >= DISPLAY_H_RES || y >= DISPLAY_V_RES) {
            data->point.x = last_x;
            data->point.y = last_y;
            data->state = is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            return;
        }
        last_x = x;
        last_y = y;
        if (!is_pressed) {
            // Idle chip wake-ups can emit a single-frame ghost report of the
            // last touch; confirm a press across 2 consecutive reads before
            // reporting it. A real finger spans several read periods.
            if (press_count == 0) {
                press_count = 1;
                data->point.x = last_x;
                data->point.y = last_y;
                data->state = LV_INDEV_STATE_RELEASED;
                return;
            }
            is_pressed = true;
        }
        press_count = 0;
        release_count = 0;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        s_touch_pressed = true;
#if TOUCH_DEBUG
        s_dbg_x = x;
        s_dbg_y = y;
        s_dbg_pressed = true;
#endif
    } else if (result == 0) {
        press_count = 0;  // ghost sequence broken
        release_count++;
        if (release_count >= 2) {
            data->point.x = last_x;
            data->point.y = last_y;
            data->state = LV_INDEV_STATE_RELEASED;
            is_pressed = false;
            s_touch_pressed = false;
#if TOUCH_DEBUG
            s_dbg_pressed = false;
#endif
        } else if (is_pressed) {
            // single missed read mid-touch: hold the press
            data->point.x = last_x;
            data->point.y = last_y;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            // never actually pressed — must not fabricate a press here,
            // otherwise a 1-frame ghost becomes a click via this path
            data->point.x = last_x;
            data->point.y = last_y;
            data->state = LV_INDEV_STATE_RELEASED;
        }
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t board_touch_init(void)
{
    // I2C bus
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = TP_I2C_NUM,
        .sda_io_num = TP_PIN_SDA,
        .scl_io_num = TP_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_bus));
    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)", TP_PIN_SDA, TP_PIN_SCL);

    // I2C device at 0x15
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x15,
        .scl_speed_hz = TP_I2C_CLK_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_tp_dev));

    // RST toggle
    gpio_set_direction(TP_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(TP_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TP_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Read chip ID
    uint8_t reg = 0xA7;
    uint8_t id = 0;
    esp_err_t ret = i2c_master_transmit_receive(s_tp_dev, &reg, 1, &id, 1, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CST816S chip ID: %d", id);
    } else {
        ESP_LOGE(TAG, "CST816S read ID failed: %s", esp_err_to_name(ret));
    }

    // Configure CST816S: disable auto-sleep for better responsiveness.
    // 0xFA = disAutoSleep — the chip's periodic idle wake cycle is what
    // re-emits ghost touches at the last position every ~10s.
    {
        uint8_t nosleep[] = {0xFE, 0xFF};
        i2c_master_transmit(s_tp_dev, nosleep, sizeof(nosleep), 10);

        uint8_t dis_auto_sleep[] = {0xFA, 0xFF};
        i2c_master_transmit(s_tp_dev, dis_auto_sleep, sizeof(dis_auto_sleep), 10);

        uint8_t sens[] = {0x14, 0x1E};
        i2c_master_transmit(s_tp_dev, sens, sizeof(sens), 10);

        ESP_LOGI(TAG, "CST816S configured (no-sleep, sensitivity set)");
    }

    // INT pin — falling edge interrupt to wake LVGL task on touch
    gpio_set_direction(TP_PIN_INT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TP_PIN_INT, GPIO_PULLUP_ONLY);
    gpio_install_isr_service(0);
    gpio_set_intr_type(TP_PIN_INT, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(TP_PIN_INT, touch_isr_handler, NULL);
    gpio_intr_enable(TP_PIN_INT);
    ESP_LOGI(TAG, "Touch interrupt enabled on GPIO %d", TP_PIN_INT);

    // Register with LVGL
    s_touch_indev = lv_indev_create();
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
    lv_indev_set_display(s_touch_indev, board_get_display());
    ESP_LOGI(TAG, "Touch LVGL indev=%p", s_touch_indev);

    return ESP_OK;
}

lv_indev_t *board_get_touch_indev(void)
{
    return s_touch_indev;
}

bool touch_is_pressed(void)
{
    return s_touch_pressed;
}

#if TOUCH_DEBUG
static void debug_dot_timer_cb(lv_timer_t *timer)
{
    if (!s_debug_dot) return;
    if (s_dbg_pressed) {
        lv_obj_clear_flag(s_debug_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_debug_dot, s_dbg_x - 6, s_dbg_y - 6);
    } else {
        lv_obj_add_flag(s_debug_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

void touch_debug_init(void)
{
    lv_obj_t *layer = lv_layer_top();
    s_debug_dot = lv_obj_create(layer);
    lv_obj_set_size(s_debug_dot, 12, 12);
    lv_obj_set_style_radius(s_debug_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_debug_dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_debug_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_color(s_debug_dot, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_debug_dot, 1, 0);
    lv_obj_clear_flag(s_debug_dot,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_debug_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_shadow_width(s_debug_dot, 4, 0);
    lv_obj_set_style_shadow_color(s_debug_dot, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_debug_dot, LV_OPA_40, 0);
    lv_timer_create(debug_dot_timer_cb, 30, NULL);
    ESP_LOGI(TAG, "Touch debug dot enabled");
}
#endif
