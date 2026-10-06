// OTA manifest fetch + parse. HTTPS pattern mirrors mimiclaw_core:
// esp_http_client + certificate bundle, PSRAM response buffer.
#include "ota_internal.h"
#include "ota_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "ota_manifest";

// Accumulator for the manifest body — only one worker task runs at a time,
// so a static buffer is safe.
static char *s_body;
static int   s_body_len;
static bool  s_overflow;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !s_body) return ESP_OK;
    if (s_body_len + evt->data_len > OTA_MANIFEST_MAX_LEN) {
        s_overflow = true;
        return ESP_OK;
    }
    memcpy(s_body + s_body_len, evt->data, evt->data_len);
    s_body_len += evt->data_len;
    return ESP_OK;
}

static bool valid_sha256(const char *s)
{
    if (strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

esp_err_t ota_manifest_fetch(ota_manifest_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    s_body_len = 0;
    s_overflow = false;

    if (!s_body) {
        s_body = heap_caps_malloc(OTA_MANIFEST_MAX_LEN + 1, MALLOC_CAP_SPIRAM);
        if (!s_body) {
            s_body = heap_caps_malloc(OTA_MANIFEST_MAX_LEN + 1, MALLOC_CAP_INTERNAL);
        }
        if (!s_body) {
            ESP_LOGE(TAG, "no mem for manifest buffer");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_http_client_config_t http_cfg = {
        .url = OTA_MANIFEST_URL,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = OTA_HTTP_BUF_SIZE,
        .buffer_size_tx = 1024,
        .event_handler = on_http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "http client init failed");
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "manifest fetch failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status != 200) {
        ESP_LOGE(TAG, "manifest HTTP %d", status);
        return ESP_ERR_HTTP_BASE;
    }
    if (s_overflow) {
        ESP_LOGE(TAG, "manifest exceeds %d bytes", OTA_MANIFEST_MAX_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    s_body[s_body_len] = '\0';
    ESP_LOGI(TAG, "manifest %d bytes", s_body_len);

    cJSON *root = cJSON_Parse(s_body);
    if (!root) {
        ESP_LOGE(TAG, "manifest is not valid JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t ret = ESP_ERR_INVALID_RESPONSE;
    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *url     = cJSON_GetObjectItem(root, "url");
    cJSON *sha256  = cJSON_GetObjectItem(root, "sha256");
    cJSON *size    = cJSON_GetObjectItem(root, "size");
    cJSON *notes   = cJSON_GetObjectItem(root, "notes");

    if (!cJSON_IsString(version) || !version->valuestring[0] ||
        !cJSON_IsString(url) || !url->valuestring[0]) {
        ESP_LOGE(TAG, "manifest missing version/url");
        goto out;
    }

#if !CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
    if (strncmp(url->valuestring, "https://", 8) != 0) {
        ESP_LOGE(TAG, "manifest url must be https");
        goto out;
    }
#endif

    if (cJSON_IsString(sha256) && sha256->valuestring[0]) {
        if (!valid_sha256(sha256->valuestring)) {
            ESP_LOGE(TAG, "manifest sha256 malformed");
            goto out;
        }
        strlcpy(out->sha256, sha256->valuestring, sizeof(out->sha256));
    }

    strlcpy(out->version, version->valuestring, sizeof(out->version));
    strlcpy(out->url, url->valuestring, sizeof(out->url));
    if (cJSON_IsString(notes) && notes->valuestring[0]) {
        strlcpy(out->notes, notes->valuestring, sizeof(out->notes));
    }
    if (cJSON_IsNumber(size)) out->size = (int)size->valuedouble;

    ret = ESP_OK;

out:
    cJSON_Delete(root);
    return ret;
}
