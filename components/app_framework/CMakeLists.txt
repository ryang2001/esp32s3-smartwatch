file(GLOB SOURCES src/*.c)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS include
    REQUIRES
        platform
        lvgl__lvgl
        espressif__esp_lvgl_port
)
