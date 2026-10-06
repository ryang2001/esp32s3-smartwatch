idf_component_register(SRCS "wifi.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash wpa_supplicant esp_wifi esp_netif)
