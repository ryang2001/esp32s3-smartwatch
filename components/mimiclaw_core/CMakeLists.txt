file(GLOB SOURCES src/*.c)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS include
    REQUIRES
        esp_http_client
        nvs_flash
        esp-tls
)
