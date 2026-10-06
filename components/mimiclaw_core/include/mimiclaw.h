#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef void (*mimiclaw_response_cb)(const char *text, bool done);

esp_err_t mimiclaw_init(void);
esp_err_t mimiclaw_chat(const char *message, mimiclaw_response_cb cb);
esp_err_t mimiclaw_set_api_key(const char *key);
esp_err_t mimiclaw_set_model(const char *model);
bool mimiclaw_is_busy(void);
void mimiclaw_cancel(void);
