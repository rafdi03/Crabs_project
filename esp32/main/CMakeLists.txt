idf_component_register(SRCS "main.c" "lcd_tft.c" "relay_ctrl.c" "flash_eks.c"
                    INCLUDE_DIRS "."
                    REQUIRES mqtt esp_wifi esp_netif esp_event nvs_flash esp_adc dht fatfs vfs sdmmc
                    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_spi esp_driver_sdspi)
