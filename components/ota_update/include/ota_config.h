#pragma once
#include "sdkconfig.h"

// OTA manifest URL (override at compile time with -DOTA_MANIFEST_URL=\"...\")
#ifndef OTA_MANIFEST_URL
#define OTA_MANIFEST_URL        CONFIG_OTA_MANIFEST_URL
#endif

// NVS storage (namespace style follows wifi_creds / mimiclaw)
#define OTA_NVS_NS              "ota"
#define OTA_NVS_KEY_BOOTTRY     "boottry"

// Limits & timing
#define OTA_MANIFEST_MAX_LEN    (4 * 1024)
#define OTA_HTTP_TIMEOUT_MS     (15 * 1000)
#define OTA_HTTP_BUF_SIZE       4096
#define OTA_TASK_STACK          (12 * 1024)   // internal RAM: task performs flash writes
#define OTA_TASK_PRIO           4
#define OTA_SELFTEST_STACK      (3 * 1024)
#define OTA_SELFTEST_PRIO       3
#define OTA_SHA_READ_CHUNK      (32 * 1024)   // PSRAM read-back chunk
#define OTA_SELFTEST_MIN_HEAP   (30 * 1024)   // internal RAM hard floor

// Self-test thresholds come from Kconfig:
//   CONFIG_OTA_SELFTEST_WINDOW_MS, CONFIG_OTA_SELFTEST_MAX_BOOT_ATTEMPTS,
//   CONFIG_OTA_DOWNLOAD_TIMEOUT_MS, CONFIG_OTA_VERIFY_SHA256
