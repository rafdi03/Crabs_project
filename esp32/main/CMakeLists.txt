idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES mqtt esp_wifi esp_netif esp_event nvs_flash esp_adc dht
                    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_spi)
