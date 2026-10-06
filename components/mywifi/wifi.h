#ifndef MYWIFI_WIFI_H
#define MYWIFI_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

typedef void (*wifi_status_cb_t)(bool connected);

#define WIFI_SCAN_MAX_AP  20

typedef void (*wifi_scan_done_cb_t)(wifi_ap_record_t *aps, uint16_t count);

void start_wifi(void);
void wifi_set_status_callback(wifi_status_cb_t cb);
bool wifi_is_connected(void);

void wifi_scan_start(wifi_scan_done_cb_t cb);
void wifi_connect(const char *ssid, const char *password);
void wifi_save_credentials(const char *ssid, const char *password);
bool wifi_load_credentials(char *ssid_out, size_t ssid_len,
                           char *pass_out, size_t pass_len);

#endif // MYWIFI_WIFI_H
