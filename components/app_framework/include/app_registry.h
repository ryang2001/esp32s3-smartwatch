#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "app_base.h"

typedef enum {
    APP_CAT_SYSTEM = 0,
    APP_CAT_TOOL   = 1,
    APP_CAT_GAME   = 2,
    APP_CAT_MEDIA  = 3,
} app_category_t;

#define APP_FLAG_NO_AUTO_DESTROY    (1 << 0)
#define APP_FLAG_HIDE_FROM_LAUNCHER (1 << 1)
#define APP_FLAG_BACKGROUND_TICK    (1 << 2)  /* Receive tick() while in background (data-only) */

typedef struct {
    const char          *name;
    const char          *display_name;
    const void          *icon;
    app_category_t       category;
    uint32_t             flags;
    app_create_fn        create;
    app_show_fn          show;
    app_hide_fn          hide;
    app_destroy_fn       destroy;
    app_handle_event_fn  handle_event;
    app_tick_fn          tick;
} app_descriptor_t;

void app_registry_init(void);
void app_registry_register(const app_descriptor_t *desc);
int app_registry_count(void);
const app_descriptor_t *app_registry_get(int index);
const app_descriptor_t *app_registry_find(const char *name);
int app_registry_get_launcher_apps(const app_descriptor_t *out[], int max_out);

#endif // APP_REGISTRY_H
