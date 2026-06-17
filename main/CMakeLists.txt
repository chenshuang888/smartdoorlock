idf_component_register(SRCS "main.c"
                          "drivers/ble_driver.c"
                          "services/uart_service.c"
                          "hardware/uart_hw.c"
                    INCLUDE_DIRS "."
                          "drivers"
                          "services"
                          "hardware"
                    PRIV_REQUIRES bt nvs_flash driver)