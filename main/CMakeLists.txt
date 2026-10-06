idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES platform app_framework apps mywifi mimiclaw_core ota_update
                             esp_driver_gpio esp_driver_ledc espressif__esp_lvgl_port)
