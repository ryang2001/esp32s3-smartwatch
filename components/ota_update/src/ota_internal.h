#pragma once
// Internal shared declarations for the ota_update component (not public API).

#include "ota_update.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Parsed manifest (ota_manifest.c)
typedef struct {
    char url[256];
    char version[32];
    char sha256[65];   // lowercase hex, optional (empty = skip read-back check)
    int  size;         // optional, informational only
    char notes[128];   // optional
} ota_manifest_t;

// Fetch OTA_MANIFEST_URL over HTTPS and parse JSON fields.
// Validates: version/url required, url must be https (or http when
// CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP is set), sha256 must be 64 hex chars.
esp_err_t ota_manifest_fetch(ota_manifest_t *out);

// Publish a status snapshot to the registered event callback (ota_update.c).
// Copies *st into the shared status slot, then invokes the callback outside
// the lock. Safe from any task.
void ota_update_notify(const ota_status_t *st);

// Semver comparison ("1.2.0" vs "1.10.0"), pure C — no IDF deps.
// Returns <0 / 0 / >0. Pre-release suffix ("-rc1") sorts below release.
int ota_version_cmp(const char *a, const char *b);
