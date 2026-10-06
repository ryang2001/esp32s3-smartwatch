file(GLOB_RECURSE APP_SOURCES */*.c)
list(APPEND APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/apps_init.c)

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS .
    REQUIRES
        app_framework
        lvgl__lvgl
        platform
        esp_system
        esp_wifi
        mywifi
        mimiclaw_core
        ota_update
)
