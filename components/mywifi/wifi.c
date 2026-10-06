#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "wifi.h"

// Fallback credentials — real values live in wifi_secrets.h (git-ignored).
// Credentials saved via the Settings app (NVS) always take priority.
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#define EXAMPLE_ESP_WIFI_SSID      "YOUR_WIFI_SSID"
#define EXAMPLE_ESP_WIFI_PASS      "YOUR_WIFI_PASSWORD"
#endif
#define EXAMPLE_ESP_MAXIMUM_RETRY  30

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define NVS_NAMESPACE "wifi_creds"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static wifi_status_cb_t s_status_cb = NULL;
static bool s_connected = false;
static volatile bool s_scanning = false;

static const char *TAG = "wifi station";

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_status_cb) s_status_cb(false);
        if (!s_scanning && s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else if (!s_scanning) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        if (s_status_cb) s_status_cb(true);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_set_status_callback(wifi_status_cb_t cb)
{
    s_status_cb = cb;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

void wifi_connect(const char *ssid, const char *password)
{
    s_retry_num = 0;
    esp_wifi_disconnect();

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

void wifi_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, password);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Credentials saved for: %s", ssid);
}

bool wifi_load_credentials(char *ssid_out, size_t ssid_len,
                           char *pass_out, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, ssid_out, &ssid_len);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, pass_out, &pass_len);
    nvs_close(h);
    if (e1 == ESP_OK && e2 == ESP_OK) {
        ESP_LOGI(TAG, "Loaded credentials for: %s", ssid_out);
        return true;
    }
    return false;
}

// --- WiFi Scan ---

static void scan_task(void *arg)
{
    wifi_scan_done_cb_t cb = (wifi_scan_done_cb_t)arg;

    s_scanning = true;
    s_retry_num = 0;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(1500));

    wifi_scan_config_t cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };
    esp_err_t ret = esp_wifi_scan_start(&cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(ret));
        s_scanning = false;
        cb(NULL, 0);
        vTaskDelete(NULL);
        return;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > WIFI_SCAN_MAX_AP) count = WIFI_SCAN_MAX_AP;

    wifi_ap_record_t *aps = NULL;
    if (count > 0) {
        aps = malloc(count * sizeof(wifi_ap_record_t));
        if (aps) {
            esp_wifi_scan_get_ap_records(&count, aps);
            // Sort by RSSI descending
            for (int i = 1; i < count; i++) {
                wifi_ap_record_t tmp = aps[i];
                int j = i - 1;
                while (j >= 0 && aps[j].rssi < tmp.rssi) {
                    aps[j + 1] = aps[j];
                    j--;
                }
                aps[j + 1] = tmp;
            }
        }
    }

    ESP_LOGI(TAG, "Scan found %d APs", count);
    s_scanning = false;
    cb(aps, aps ? count : 0);
    vTaskDelete(NULL);
}

void wifi_scan_start(wifi_scan_done_cb_t cb)
{
    xTaskCreate(scan_task, "wifi_scan", 4096, (void *)cb, 5, NULL);
}

// --- Init ---

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Try NVS credentials first, fallback to hardcoded
    wifi_config_t wifi_config = {0};
    char ssid[33] = {0}, pass[64] = {0};
    if (wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    } else {
        strncpy((char *)wifi_config.sta.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, EXAMPLE_ESP_WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", wifi_config.sta.ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", wifi_config.sta.ssid);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void start_wifi(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}
