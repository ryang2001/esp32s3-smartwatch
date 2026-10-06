// OTA state machine + worker task. Stack lives in internal RAM (not PSRAM)
// because the worker performs flash writes, which stall the flash cache.
#include "ota_update.h"
#include "ota_config.h"
#include "ota_internal.h"

#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>
#include <strings.h>

static const char *TAG = "ota";

static ota_status_t s_status;
static SemaphoreHandle_t s_lock;
static volatile bool s_busy;
static volatile bool s_cancel;
static ota_event_cb_t s_event_cb;

static void ota_worker(void *arg);
static esp_err_t spawn_worker(bool download);

/* ── status helpers ─────────────────────────────────────────── */

static void status_get_locked(ota_status_t *out)
{
    *out = s_status;
}

static void status_set(ota_state_t st)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_status.state = st;
        ota_status_t snap = s_status;
        xSemaphoreGive(s_lock);
        if (s_event_cb) s_event_cb(&snap);
    }
}

void ota_update_notify(const ota_status_t *st)
{
    if (!st) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_status = *st;
        ota_status_t snap = s_status;
        xSemaphoreGive(s_lock);
        if (s_event_cb) s_event_cb(&snap);
    }
}

static void set_error(const char *msg)
{
    ESP_LOGE(TAG, "%s", msg);
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_status.state = OTA_STATE_ERROR;
        s_status.percent = 0;
        strlcpy(s_status.error_msg, msg, sizeof(s_status.error_msg));
        ota_status_t snap = s_status;
        xSemaphoreGive(s_lock);
        if (s_event_cb) s_event_cb(&snap);
    }
}

/* ── public API ─────────────────────────────────────────────── */

esp_err_t ota_update_init(void)
{
    if (s_lock) return ESP_OK;  // idempotent

    // NVS must be ready before self-test boot counting; start_wifi() also
    // calls nvs_flash_init() later — the second call is a no-op.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_STATE_IDLE;
    s_status.percent = -1;
    strlcpy(s_status.cur_version, ota_update_current_version(),
            sizeof(s_status.cur_version));

#if CONFIG_OTA_VERIFY_SHA256
    psa_crypto_init();
#endif

    ESP_LOGI(TAG, "ota_update ready, current version %s", s_status.cur_version);
    return ESP_OK;
}

void ota_update_set_event_cb(ota_event_cb_t cb)
{
    s_event_cb = cb;
}

void ota_update_get_status(ota_status_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        status_get_locked(out);
        xSemaphoreGive(s_lock);
    } else {
        memset(out, 0, sizeof(*out));
        out->state = OTA_STATE_ERROR;
        out->percent = -1;
        strlcpy(out->error_msg, "status lock timeout", sizeof(out->error_msg));
    }
}

bool ota_update_is_busy(void)
{
    return s_busy;
}

const char *ota_update_current_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return desc ? desc->version : "unknown";
}

esp_err_t ota_update_start_check(void)
{
    return spawn_worker(false);
}

esp_err_t ota_update_start_download(void)
{
    return spawn_worker(true);
}

void ota_update_cancel(void)
{
    s_cancel = true;
}

void ota_update_reboot(void)
{
    ota_status_t st;
    ota_update_get_status(&st);
    if (st.state != OTA_STATE_READY_REBOOT) return;

    ESP_LOGI(TAG, "rebooting to apply update in 800ms");
    const esp_timer_create_args_t args = {
        .callback = (esp_timer_cb_t)esp_restart,
        .name = "ota_reboot",
    };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, 800 * 1000);
    } else {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }
}

/* ── sha256 read-back verification (optional) ───────────────── */

static bool verify_sha_readback(const esp_partition_t *part, size_t len,
                                const char *expect_hex)
{
    uint8_t *chunk = heap_caps_malloc(OTA_SHA_READ_CHUNK, MALLOC_CAP_SPIRAM);
    if (!chunk) chunk = heap_caps_malloc(OTA_SHA_READ_CHUNK, MALLOC_CAP_INTERNAL);
    if (!chunk) {
        ESP_LOGE(TAG, "no mem for sha read-back");
        return false;
    }

    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_status_t ps = psa_hash_setup(&op, PSA_ALG_SHA_256);
    bool ok = (ps == PSA_SUCCESS);

    for (size_t off = 0; ok && off < len; ) {
        size_t n = len - off;
        if (n > OTA_SHA_READ_CHUNK) n = OTA_SHA_READ_CHUNK;
        if (esp_partition_read(part, off, chunk, n) != ESP_OK) {
            ESP_LOGE(TAG, "partition read failed @0x%x", (unsigned)off);
            ok = false;
            break;
        }
        ps = psa_hash_update(&op, chunk, n);
        if (ps != PSA_SUCCESS) { ok = false; break; }
        off += n;
    }

    if (ok) {
        uint8_t hash[32];
        size_t hash_len = 0;
        ps = psa_hash_finish(&op, hash, sizeof(hash), &hash_len);
        ok = (ps == PSA_SUCCESS && hash_len == sizeof(hash));
        if (ok) {
            char hex[65];
            for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", hash[i]);
            ok = (strcasecmp(hex, expect_hex) == 0);
            if (!ok) ESP_LOGE(TAG, "sha mismatch: got %s", hex);
        }
    } else {
        psa_hash_abort(&op);
    }

    free(chunk);
    return ok;
}

/* ── worker task ────────────────────────────────────────────── */

static esp_err_t spawn_worker(bool download)
{
    if (s_busy) {
        ESP_LOGW(TAG, "worker already running");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_status.state = OTA_STATE_CHECKING;
        s_status.percent = -1;
        s_status.downloaded_kb = 0;
        s_status.error_msg[0] = '\0';
        xSemaphoreGive(s_lock);
    }
    s_cancel = false;
    s_busy = true;

    if (xTaskCreatePinnedToCore(ota_worker, "ota", OTA_TASK_STACK,
                                (void *)(uintptr_t)download,
                                OTA_TASK_PRIO, NULL, 0) != pdPASS) {
        s_busy = false;
        ESP_LOGE(TAG, "failed to create ota worker");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void map_begin_err(esp_err_t err, char *buf, size_t len)
{
    switch (err) {
    case ESP_ERR_OTA_VALIDATE_FAILED:
        strlcpy(buf, "固件校验失败", len);
        break;
    case ESP_ERR_OTA_ROLLBACK_INVALID_STATE:
        strlcpy(buf, "设备自检中，请稍后重试", len);
        break;
    default:
        strlcpy(buf, "无法连接更新服务器", len);
        break;
    }
}

static void ota_worker(void *arg)
{
    bool download = (bool)(uintptr_t)arg;
    ESP_LOGI(TAG, "worker start: %s", download ? "download" : "check");
    ota_manifest_t m;
    esp_err_t err;
    char errbuf[96];
    const char *cur = ota_update_current_version();

    // 1) manifest
    if (ota_manifest_fetch(&m) != ESP_OK) {
        set_error("无法获取更新信息");
        goto done;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        strlcpy(s_status.new_version, m.version, sizeof(s_status.new_version));
        strlcpy(s_status.notes, m.notes, sizeof(s_status.notes));
        xSemaphoreGive(s_lock);
    }

    // 2) version compare
    int cmp = ota_version_cmp(cur, m.version);
    ESP_LOGI(TAG, "version: cur=%s new=%s cmp=%d", cur, m.version, cmp);
    if (cmp >= 0) {
        status_set(OTA_STATE_UP_TO_DATE);
        goto done;
    }
    if (!download) {  // check-only mode stops here
        status_set(OTA_STATE_AVAILABLE);
        goto done;
    }
    status_set(OTA_STATE_DOWNLOADING);

    // 3) pending-verify guard (esp_ota_begin would refuse otherwise)
    if (ota_boot_is_pending_verify()) {
        set_error("设备自检中，请稍后重试");
        goto done;
    }

    // 4) begin
    esp_http_client_config_t http_cfg = {
        .url = m.url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = OTA_HTTP_BUF_SIZE,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    esp_https_ota_handle_t h = NULL;
    err = esp_https_ota_begin(&ota_cfg, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota begin: %s", esp_err_to_name(err));
        map_begin_err(err, errbuf, sizeof(errbuf));
        set_error(errbuf);
        goto done;
    }

    // 5) cross-check image self-described version against manifest
    esp_app_desc_t img_desc;
    if (esp_https_ota_get_img_desc(h, &img_desc) == ESP_OK &&
        strcmp(img_desc.version, m.version) != 0) {
        ESP_LOGE(TAG, "image version %s != manifest %s", img_desc.version, m.version);
        esp_https_ota_abort(h);
        set_error("镜像与清单不符");
        goto done;
    }

    // 6) streaming download
    int64_t deadline = esp_timer_get_time() +
                       (int64_t)CONFIG_OTA_DOWNLOAD_TIMEOUT_MS * 1000;
    err = ESP_OK;
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (s_cancel) {
            esp_https_ota_abort(h);
            status_set(OTA_STATE_IDLE);
            goto done;
        }
        if (esp_timer_get_time() > deadline) {
            esp_https_ota_abort(h);
            set_error("下载超时");
            goto done;
        }

        int total = esp_https_ota_get_image_size(h);
        int read = esp_https_ota_get_image_len_read(h);
        ota_status_t snap;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_status.percent = (total > 0) ? read * 100 / total : -1;
            s_status.downloaded_kb = read / 1024;
            snap = s_status;
            xSemaphoreGive(s_lock);
            if (s_event_cb) s_event_cb(&snap);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        ESP_LOGE(TAG, "download ended: %s complete=%d", esp_err_to_name(err),
                 esp_https_ota_is_complete_data_received(h));
        if (err != ESP_OK) esp_https_ota_abort(h);
        set_error("下载中断");
        goto done;
    }

    // 7) optional read-back sha256 (guards against a wrong-but-valid image)
#if CONFIG_OTA_VERIFY_SHA256
    if (m.sha256[0]) {
        const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
        int read = esp_https_ota_get_image_len_read(h);
        if (!target || !verify_sha_readback(target, (size_t)read, m.sha256)) {
            esp_https_ota_abort(h);
            set_error("校验失败");
            goto done;
        }
        ESP_LOGI(TAG, "sha256 read-back OK");
    }
#endif

    // 8) finish: full image validation + set boot partition
    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota finish: %s", esp_err_to_name(err));
        map_begin_err(err, errbuf, sizeof(errbuf));
        set_error(errbuf);
        goto done;
    }

    ESP_LOGI(TAG, "update %s ready, reboot to apply", m.version);
    status_set(OTA_STATE_READY_REBOOT);

done:
    s_busy = false;
    vTaskDelete(NULL);
}
