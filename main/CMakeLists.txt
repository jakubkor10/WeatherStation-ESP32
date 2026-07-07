idf_component_register(SRCS "network.c" "hardware.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_i2c
                    REQUIRES esp_lcd
                    REQUIRES esp_http_client
                    REQUIRES esp_wifi
                    REQUIRES nvs_flash)