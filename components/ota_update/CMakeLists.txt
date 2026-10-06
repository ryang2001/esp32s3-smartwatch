file(GLOB SOURCES src/*.c)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS include
    REQUIRES
        esp_https_ota
        app_update
        esp_http_client
        esp-tls
        nvs_flash
        mbedtls
        esp_timer
)
