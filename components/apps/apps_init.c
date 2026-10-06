#include "app_registry.h"

extern const app_descriptor_t app_descriptor_clock;
extern const app_descriptor_t app_descriptor_launcher;
extern const app_descriptor_t app_descriptor_settings;
extern const app_descriptor_t app_descriptor_stopwatch;
extern const app_descriptor_t app_descriptor_touch_test;
extern const app_descriptor_t app_descriptor_mimiclaw;

void apps_register_all(void)
{
    app_registry_register(&app_descriptor_clock);
    app_registry_register(&app_descriptor_launcher);
    app_registry_register(&app_descriptor_settings);
    app_registry_register(&app_descriptor_stopwatch);
    app_registry_register(&app_descriptor_touch_test);
    app_registry_register(&app_descriptor_mimiclaw);
}
