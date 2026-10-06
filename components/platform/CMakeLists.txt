file(GLOB SOURCES src/*.c)
file(GLOB FONTS fonts/*.c)

idf_component_register(
    SRCS ${SOURCES} ${FONTS}
    INCLUDE_DIRS include
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
        esp_driver_ledc
        esp_lcd
        espressif__esp_lvgl_port
        lvgl__lvgl
        driver
)
