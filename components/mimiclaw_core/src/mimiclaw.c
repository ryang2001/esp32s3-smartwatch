#include "mimiclaw.h"
#include "mimiclaw_config.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"

static const char *TAG = "mimiclaw";

#define API_KEY_MAX_LEN  320
#define MODEL_MAX_LEN    64

static char s_api_key[API_KEY_MAX_LEN] = {0};
static char s_model[MODEL_MAX_LEN] = MIMICLAW_DEFAULT_MODEL;
static volatile bool s_busy = false;
static volatile bool s_cancel = false;

/* Conversation history (stored as JSON array of {role, content}) */
static cJSON *s_history = NULL;
static SemaphoreHandle_t s_mutex = NULL;

/* ── Response buffer (PSRAM) ──────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} resp_buf_t;

static esp_err_t resp_buf_init(resp_buf_t *rb, size_t initial_cap)
{
    rb->data = heap_caps_calloc(1, initial_cap, MALLOC_CAP_SPIRAM);
    if (!rb->data) {
        rb->data = calloc(1, initial_cap);
        if (!rb->data) return ESP_ERR_NO_MEM;
    }
    rb->len = 0;
    rb->cap = initial_cap;
    return ESP_OK;
}

static esp_err_t resp_buf_append(resp_buf_t *rb, const char *data, size_t len)
{
    while (rb->len + len >= rb->cap) {
        size_t new_cap = rb->cap * 2;
        char *tmp = heap_caps_realloc(rb->data, new_cap, MALLOC_CAP_SPIRAM);
        if (!tmp) {
            tmp = realloc(rb->data, new_cap);
            if (!tmp) return ESP_ERR_NO_MEM;
        }
        rb->data = tmp;
        rb->cap = new_cap;
    }
    memcpy(rb->data + rb->len, data, len);
    rb->len += len;
    rb->data[rb->len] = '\0';
    return ESP_OK;
}

static void resp_buf_free(resp_buf_t *rb)
{
    free(rb->data);
    rb->data = NULL;
    rb->len = 0;
    rb->cap = 0;
}

/* ── Chunked transfer decoding ────────────────────────────────── */

static void resp_buf_decode_chunked(resp_buf_t *rb)
{
    if (!rb->data || rb->len == 0) return;

    size_t i = 0;
    while (i < rb->len && (rb->data[i] == ' ' || rb->data[i] == '\t')) i++;
    if (i < rb->len && (rb->data[i] == '{' || rb->data[i] == '[')) return;

    char *src = rb->data;
    char *dst = rb->data;
    char *end = rb->data + rb->len;

    while (src < end) {
        char *line_end = strstr(src, "\r\n");
        if (!line_end) break;
        unsigned long chunk_size = strtoul(src, NULL, 16);
        if (chunk_size == 0) break;
        src = line_end + 2;
        if (src + chunk_size > end) {
            size_t avail = end - src;
            memmove(dst, src, avail);
            dst += avail;
            break;
        }
        memmove(dst, src, chunk_size);
        dst += chunk_size;
        src += chunk_size;
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') src += 2;
    }

    rb->len = dst - rb->data;
    rb->data[rb->len] = '\0';
}

/* ── HTTP event handler ───────────────────────────────────────── */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = (resp_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        resp_buf_append(rb, (const char *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

/* ── HTTP call to Anthropic API ───────────────────────────────── */

static esp_err_t llm_http_call(const char *post_data, resp_buf_t *rb, int *out_status)
{
    esp_http_client_config_t config = {
        .url = MIMICLAW_API_URL,
        .event_handler = http_event_handler,
        .user_data = rb,
        .timeout_ms = 120 * 1000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-api-key", s_api_key);
    esp_http_client_set_header(client, "anthropic-version", MIMICLAW_API_VERSION);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

/* ── Init ─────────────────────────────────────────────────────── */

esp_err_t mimiclaw_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_history = cJSON_CreateArray();

    /* Start with build-time defaults */
    if (MIMICLAW_SECRET_API_KEY[0] != '\0') {
        strncpy(s_api_key, MIMICLAW_SECRET_API_KEY, sizeof(s_api_key) - 1);
    }
    if (MIMICLAW_SECRET_MODEL[0] != '\0') {
        strncpy(s_model, MIMICLAW_SECRET_MODEL, sizeof(s_model) - 1);
    }

    /* NVS overrides (set at runtime) */
    nvs_handle_t nvs;
    if (nvs_open(MIMICLAW_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[API_KEY_MAX_LEN] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMICLAW_NVS_KEY_API_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_api_key, tmp, sizeof(s_api_key) - 1);
        }
        char model_tmp[MODEL_MAX_LEN] = {0};
        len = sizeof(model_tmp);
        if (nvs_get_str(nvs, MIMICLAW_NVS_KEY_MODEL, model_tmp, &len) == ESP_OK && model_tmp[0]) {
            strncpy(s_model, model_tmp, sizeof(s_model) - 1);
        }
        nvs_close(nvs);
    }

    if (s_api_key[0]) {
        ESP_LOGI(TAG, "Initialized (model: %s)", s_model);
    } else {
        ESP_LOGW(TAG, "No API key configured");
    }
    return ESP_OK;
}

/* ── Chat task ────────────────────────────────────────────────── */

typedef struct {
    char *message;
    mimiclaw_response_cb cb;
} chat_request_t;

static void chat_task(void *arg)
{
    chat_request_t *req = (chat_request_t *)arg;
    esp_err_t err = ESP_FAIL;

    if (s_api_key[0] == '\0') {
        req->cb("No API key. Set one via: mimiclaw_set_api_key()", true);
        goto done;
    }

    /* Build messages array with history */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cJSON *msgs = cJSON_Duplicate(s_history, 1);
    xSemaphoreGive(s_mutex);

    if (!msgs) msgs = cJSON_CreateArray();

    /* Append user message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", req->message);
    cJSON_AddItemToArray(msgs, user_msg);

    /* Build request body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", s_model);
    cJSON_AddNumberToObject(body, "max_tokens", MIMICLAW_MAX_TOKENS);
    cJSON_AddStringToObject(body, "system",
        "You are MimiClaw, a helpful AI assistant running on an ESP32-S3 smartwatch. "
        "Keep responses concise and clear for a small screen. "
        "Use short paragraphs. Avoid markdown formatting.");
    cJSON_AddItemToObject(body, "messages", msgs);

    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (!post_data) {
        req->cb("Failed to build request", true);
        goto done;
    }

    ESP_LOGI(TAG, "Sending request (%d bytes)", (int)strlen(post_data));

    /* HTTP call */
    resp_buf_t rb;
    if (resp_buf_init(&rb, MIMICLAW_RESP_BUF_INITIAL) != ESP_OK) {
        free(post_data);
        req->cb("Out of memory", true);
        goto done;
    }

    int status = 0;
    err = llm_http_call(post_data, &rb, &status);
    free(post_data);

    if (s_cancel) {
        resp_buf_free(&rb);
        req->cb("", true);
        goto done;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP failed: %s", esp_err_to_name(err));
        resp_buf_free(&rb);
        req->cb("Network error", true);
        goto done;
    }

    resp_buf_decode_chunked(&rb);

    if (status != 200) {
        ESP_LOGE(TAG, "API error %d: %.200s", status, rb.data ? rb.data : "");
        resp_buf_free(&rb);
        req->cb("API error", true);
        goto done;
    }

    /* Parse response */
    cJSON *root = cJSON_Parse(rb.data);
    resp_buf_free(&rb);

    if (!root) {
        req->cb("Parse error", true);
        goto done;
    }

    /* Extract text from content blocks */
    const char *response_text = NULL;
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (content && cJSON_IsArray(content)) {
        size_t total_len = 0;
        cJSON *block;
        cJSON_ArrayForEach(block, content) {
            cJSON *btype = cJSON_GetObjectItem(block, "type");
            if (btype && strcmp(btype->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text)) {
                    total_len += strlen(text->valuestring);
                }
            }
        }
        if (total_len > 0) {
            char *text_buf = calloc(1, total_len + 1);
            if (text_buf) {
                cJSON_ArrayForEach(block, content) {
                    cJSON *btype = cJSON_GetObjectItem(block, "type");
                    if (!btype || strcmp(btype->valuestring, "text") != 0) continue;
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (!text || !cJSON_IsString(text)) continue;
                    strcat(text_buf, text->valuestring);
                }
                response_text = text_buf;
            }
        }
    }

    if (response_text) {
        req->cb(response_text, true);

        /* Save to history */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        cJSON *hist_user = cJSON_CreateObject();
        cJSON_AddStringToObject(hist_user, "role", "user");
        cJSON_AddStringToObject(hist_user, "content", req->message);
        cJSON_AddItemToArray(s_history, hist_user);

        cJSON *hist_ai = cJSON_CreateObject();
        cJSON_AddStringToObject(hist_ai, "role", "assistant");
        cJSON_AddStringToObject(hist_ai, "content", response_text);
        cJSON_AddItemToArray(s_history, hist_ai);

        /* Trim history if too long */
        while (cJSON_GetArraySize(s_history) > MIMICLAW_MAX_HISTORY * 2) {
            cJSON_DeleteItemFromArray(s_history, 0);
        }
        xSemaphoreGive(s_mutex);

        free((void *)response_text);
    } else {
        req->cb("No response", true);
    }

    cJSON_Delete(root);

done:
    free(req->message);
    free(req);
    s_busy = false;
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────────────────────────── */

esp_err_t mimiclaw_chat(const char *message, mimiclaw_response_cb cb)
{
    if (s_busy) return ESP_ERR_INVALID_STATE;
    if (!message || !cb) return ESP_ERR_INVALID_ARG;

    chat_request_t *req = calloc(1, sizeof(chat_request_t));
    if (!req) return ESP_ERR_NO_MEM;

    req->message = strdup(message);
    req->cb = cb;
    if (!req->message) { free(req); return ESP_ERR_NO_MEM; }

    s_busy = true;
    s_cancel = false;

    static StaticTask_t s_task_buf;
    static StackType_t *s_task_stack = NULL;
    if (!s_task_stack) {
        s_task_stack = heap_caps_malloc(12 * 1024 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
        if (!s_task_stack) {
            s_busy = false;
            free(req->message);
            free(req);
            return ESP_ERR_NO_MEM;
        }
    }

    TaskHandle_t h = xTaskCreateStaticPinnedToCore(chat_task, "mimiclaw",
        12 * 1024, req, 5, s_task_stack, &s_task_buf, 0);
    if (!h) {
        s_busy = false;
        free(req->message);
        free(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mimiclaw_set_api_key(const char *key)
{
    if (!key) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMICLAW_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    nvs_set_str(nvs, MIMICLAW_NVS_KEY_API_KEY, key);
    nvs_commit(nvs);
    nvs_close(nvs);

    strncpy(s_api_key, key, sizeof(s_api_key) - 1);
    ESP_LOGI(TAG, "API key saved");
    return ESP_OK;
}

esp_err_t mimiclaw_set_model(const char *model)
{
    if (!model) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MIMICLAW_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    nvs_set_str(nvs, MIMICLAW_NVS_KEY_MODEL, model);
    nvs_commit(nvs);
    nvs_close(nvs);

    strncpy(s_model, model, sizeof(s_model) - 1);
    ESP_LOGI(TAG, "Model set to: %s", s_model);
    return ESP_OK;
}

bool mimiclaw_is_busy(void)
{
    return s_busy;
}

void mimiclaw_cancel(void)
{
    s_cancel = true;
}
