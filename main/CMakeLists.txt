idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_wifi esp_netif esp_event 
                                lcd_i2c dht11 mq2_gas mq135 buzzer button_isr
                                esp_rainmaker wifi_provisioning rmaker_app_network)