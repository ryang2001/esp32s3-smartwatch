#pragma once

/* MimiClaw Watch Configuration */

/* Build-time secrets (highest priority).
 * Real key lives in mimiclaw_secrets.h (git-ignored); without it the key
 * can still be set at runtime via mimiclaw_set_api_key() (stored in NVS). */
#if __has_include("mimiclaw_secrets.h")
#include "mimiclaw_secrets.h"
#else
#define MIMICLAW_SECRET_API_KEY   ""
#endif
#define MIMICLAW_SECRET_MODEL     "glm-5.1"

/* LLM API */
#define MIMICLAW_DEFAULT_MODEL    "glm-5.1"
#define MIMICLAW_API_URL          "https://open.bigmodel.cn/api/anthropic/v1/messages"
#define MIMICLAW_API_VERSION      "2023-06-01"
#define MIMICLAW_MAX_TOKENS       1024
#define MIMICLAW_RESP_BUF_INITIAL (16 * 1024)

/* NVS */
#define MIMICLAW_NVS_NS           "mimiclaw"
#define MIMICLAW_NVS_KEY_API_KEY  "api_key"
#define MIMICLAW_NVS_KEY_MODEL    "model"

/* Chat history */
#define MIMICLAW_MAX_HISTORY      10
