// Post-update boot self-test + crash-loop rollback.
//
// After an OTA reboot the running image sits in ESP_OTA_IMG_PENDING_VERIFY.
// If nobody confirms it, the bootloader does NOT roll back by itself —
// this module counts boots (NVS "ota/boottry") and, if the image keeps
// dying before the self-test window completes, rolls back explicitly.
#include "ota_update.h"
#include "ota_config.h"
#include "ota_internal.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ota_selftest";

static ota_selftest_probes_t s_probes;

static uint8_t boottry_bump(void)
{
    nvs_handle_t h;
    uint8_t tries = 0;
    if (nvs_open(OTA_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_get_u8(h, OTA_NVS_KEY_BOOTTRY, &tries);
    tries++;
    nvs_set_u8(h, OTA_NVS_KEY_BOOTTRY, tries);
    nvs_commit(h);
    nvs_close(h);
    return tries;
}

static void boottry_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(OTA_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, OTA_NVS_KEY_BOOTTRY, 0);
    nvs_commit(h);
    nvs_close(h);
}

bool ota_boot_is_pending_verify(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK) return false;
    return st == ESP_OTA_IMG_PENDING_VERIFY;
}

// Runs OTA_SELFTEST_WINDOW_MS after boot, then judges the new image.
static void selftest_task(void *arg)
{
    const uint32_t window_ms = CONFIG_OTA_SELFTEST_WINDOW_MS;
    const uint32_t t0 = s_probes.tick_count_fn ? s_probes.tick_count_fn() : 0;

    vTaskDelay(pdMS_TO_TICKS(window_ms));

    bool pass = true;

    // Hard 1: LVGL heartbeat must have advanced (render pipeline alive).
    // sys_ui_update_time() ticks once per second; allow 50% loss tolerance.
    if (s_probes.tick_count_fn) {
        uint32_t delta = s_probes.tick_count_fn() - t0;
        uint32_t need = window_ms / 2000;
        ESP_LOGI(TAG, "heartbeat delta=%u need>=%u", (unsigned)delta, (unsigned)need);
        if (delta < need) pass = false;
    }

    // Hard 2: internal heap above floor (runaway allocation guard).
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "internal free=%u", (unsigned)internal_free);
    if (internal_free < OTA_SELFTEST_MIN_HEAP) pass = false;

    // Soft: WiFi not connected is NOT a failure — rolling back would lock the
    // user out of re-downloading; report only.
    if (s_probes.wifi_ok_fn && !s_probes.wifi_ok_fn()) {
        ESP_LOGW(TAG, "wifi not connected during self-test window (soft)");
    }

    if (pass) {
        esp_ota_mark_app_valid_cancel_rollback();
        boottry_reset();
        ESP_LOGI(TAG, "self-test passed, image confirmed");

        ota_status_t st = { .state = OTA_STATE_VALIDATED, .percent = 100 };
        strlcpy(st.cur_version, ota_update_current_version(), sizeof(st.cur_version));
        strlcpy(st.notes, "固件已更新", sizeof(st.notes));
        ota_update_notify(&st);
    } else {
        ESP_LOGE(TAG, "self-test FAILED, rolling back");
        vTaskDelay(pdMS_TO_TICKS(500));  // let the log drain
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }

    vTaskDelete(NULL);
}

esp_err_t ota_selftest_start(const ota_selftest_probes_t *probes)
{
    if (!ota_boot_is_pending_verify()) return ESP_OK;  // normal boot, no-op

    uint8_t tries = boottry_bump();
    ESP_LOGW(TAG, "pending-verify boot #%d", tries);

    // Crash-loop guard: covers crashes before this task even starts.
    if (tries >= CONFIG_OTA_SELFTEST_MAX_BOOT_ATTEMPTS) {
        ESP_LOGE(TAG, "boot attempts exhausted, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return ESP_FAIL;  // only reached if reboot failed
    }

    memset(&s_probes, 0, sizeof(s_probes));
    if (probes) s_probes = *probes;

    if (xTaskCreate(selftest_task, "ota_chk", OTA_SELFTEST_STACK,
                    NULL, OTA_SELFTEST_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create selftest task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
