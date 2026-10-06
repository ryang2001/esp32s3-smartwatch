#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include "board.h"
#include "display.h"
#include "touch.h"
#include "backlight.h"
#include "app_manager.h"
#include "apps_register.h"
#include "wifi.h"
#include "sys_ui.h"
#include "ota_update.h"
#include "esp_lvgl_port.h"
#include "mimiclaw.h"

#include "esp_heap_caps.h"

static const char *TAG = "main";

static void wifi_status_changed(bool connected)
{
    if (lvgl_port_lock(100)) {
        sys_ui_set_wifi_status(connected);
        lvgl_port_unlock();
    }
}

// OTA worker / self-test task context — sys_ui_add_notification is thread-safe
static void ota_event_handler(const ota_status_t *st)
{
    if (st->state == OTA_STATE_READY_REBOOT) {
        sys_ui_add_notification("固件已下载完成，重启后生效");
    } else if (st->state == OTA_STATE_VALIDATED) {
        char buf[128];
        snprintf(buf, sizeof(buf), "固件已更新到 v%s", st->cur_version);
        sys_ui_add_notification(buf);
    }
}

static void time_sync_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
}

static void sntp_task(void *p)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp1.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();

    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 10) {
        ESP_LOGI(TAG, "Waiting for time sync... (%d/10)", retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    time_t now;
    time(&now);
    setenv("TZ", "CST-8", 1);
    tzset();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void memory_task(void *p)
{
    while (1) {
        int fi = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int fp = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "RAM: %d.%dKB  PSRAM: %d.%dKB",
                 fi / 1024, fi % 1024, fp / 1024, fp % 1024);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Smartwatch init ===");

    // GPIO LEDs
    gpio_set_direction(LED_PIN_0, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PIN_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PIN_2, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PIN_3, GPIO_MODE_OUTPUT);

    // Hardware init
    board_backlight_init();
    board_backlight_set(0);
    board_display_init();
    board_touch_init();

    // App framework
    apps_register_all();
    app_manager_init();

    // OTA: init (NVS) + self-test (no-op unless a fresh update is pending)
    ota_update_init();
    ota_update_set_event_cb(ota_event_handler);
    ota_selftest_start(&(ota_selftest_probes_t){
        .tick_count_fn = sys_ui_tick_count,
        .wifi_ok_fn    = wifi_is_connected,
    });

    // Fade in backlight
    vTaskDelay(pdMS_TO_TICKS(300));
    board_backlight_set(60);

    // WiFi (blocking — waits for connection)
    wifi_set_status_callback(wifi_status_changed);
    start_wifi();

    // Background tasks (stacks in PSRAM to preserve internal RAM for DMA)
    mimiclaw_init();

    static StaticTask_t sntp_task_buf;
    static StackType_t *sntp_stack = NULL;
    sntp_stack = heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (sntp_stack) {
        xTaskCreateStatic(sntp_task, "sntp", 4096, NULL, 3, sntp_stack, &sntp_task_buf);
    }

    static StaticTask_t mem_task_buf;
    static StackType_t *mem_stack = NULL;
    mem_stack = heap_caps_malloc(3072 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (mem_stack) {
        xTaskCreateStatic(memory_task, "mem", 3072, NULL, 1, mem_stack, &mem_task_buf);
    }

    ESP_LOGI(TAG, "All tasks started");

    // LED blink loop
    while (1) {
        gpio_set_level(LED_PIN_0, 0); gpio_set_level(LED_PIN_1, 1);
        gpio_set_level(LED_PIN_2, 1); gpio_set_level(LED_PIN_3, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_PIN_0, 1); gpio_set_level(LED_PIN_1, 1);
        gpio_set_level(LED_PIN_2, 0); gpio_set_level(LED_PIN_3, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_PIN_0, 1); gpio_set_level(LED_PIN_1, 0);
        gpio_set_level(LED_PIN_2, 1); gpio_set_level(LED_PIN_3, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_PIN_0, 1); gpio_set_level(LED_PIN_1, 1);
        gpio_set_level(LED_PIN_2, 1); gpio_set_level(LED_PIN_3, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
