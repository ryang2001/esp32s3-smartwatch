#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE = 0,      // nothing going on
    OTA_STATE_CHECKING,      // fetching manifest / comparing versions
    OTA_STATE_UP_TO_DATE,    // manifest version <= current
    OTA_STATE_AVAILABLE,     // newer version found, not yet downloaded
    OTA_STATE_DOWNLOADING,   // esp_https_ota in progress
    OTA_STATE_READY_REBOOT,  // downloaded + verified, boot slot switched
    OTA_STATE_VALIDATED,     // post-update self-test passed (informational)
    OTA_STATE_ERROR,         // failed, see error_msg
} ota_state_t;

typedef struct {
    ota_state_t state;
    int         percent;        // 0..100; -1 = unknown total (chunked)
    int         downloaded_kb;  // KB received so far
    char        cur_version[32];
    char        new_version[32];
    char        notes[128];
    char        error_msg[96];
} ota_status_t;

// Invoked from the OTA worker / self-test task context — keep it quick,
// do NOT call LVGL from here (sys_ui_add_notification is thread-safe).
typedef void (*ota_event_cb_t)(const ota_status_t *status);

/* ── lifecycle ─────────────────────────────────────────────── */
esp_err_t ota_update_init(void);                 // idempotent nvs_flash_init + logging
void      ota_update_set_event_cb(ota_event_cb_t cb);

/* ── status (any thread, mutex-protected snapshot) ─────────── */
void        ota_update_get_status(ota_status_t *out);
bool        ota_update_is_busy(void);
const char *ota_update_current_version(void);    // esp_app_desc_t version

/* ── async triggers (one worker at a time) ─────────────────── */
esp_err_t ota_update_start_check(void);          // manifest fetch + version compare only
esp_err_t ota_update_start_download(void);       // full OTA
void      ota_update_cancel(void);               // request cancel during DOWNLOADING
void      ota_update_reboot(void);               // READY_REBOOT only: delayed esp_restart()

/* ── boot self-test (see ota_selftest.c) ───────────────────── */
typedef struct {
    uint32_t (*tick_count_fn)(void);  // LVGL heartbeat counter (sys_ui_tick_count)
    bool     (*wifi_ok_fn)(void);     // soft probe (wifi_is_connected), may be NULL
} ota_selftest_probes_t;

bool      ota_boot_is_pending_verify(void);
esp_err_t ota_selftest_start(const ota_selftest_probes_t *probes);  // no-op if not pending

#ifdef __cplusplus
}
#endif
