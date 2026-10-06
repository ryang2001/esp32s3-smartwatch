#include "app_registry.h"
#include <string.h>

#define MAX_APPS 16

static const app_descriptor_t *s_apps[MAX_APPS];
static int s_count = 0;

void app_registry_init(void)
{
    s_count = 0;
}

void app_registry_register(const app_descriptor_t *desc)
{
    if (!desc || s_count >= MAX_APPS) return;
    s_apps[s_count++] = desc;
}

int app_registry_count(void)
{
    return s_count;
}

const app_descriptor_t *app_registry_get(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return s_apps[index];
}

const app_descriptor_t *app_registry_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i]->name, name) == 0) return s_apps[i];
    }
    return NULL;
}

int app_registry_get_launcher_apps(const app_descriptor_t *out[], int max_out)
{
    int count = 0;
    for (int i = 0; i < s_count && count < max_out; i++) {
        if (!(s_apps[i]->flags & APP_FLAG_HIDE_FROM_LAUNCHER)) {
            out[count++] = s_apps[i];
        }
    }
    return count;
}
