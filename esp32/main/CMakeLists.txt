idf_component_register(
    SRCS
        "main.c"
        "robot_ble.c"
        "robot_ble_protocol.c"
        "robot_ble_protocol_self_test.c"
        "robot_protocol.c"
        "robot_protocol_self_test.c"
        "robot_uart_link.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        bt
        esp_driver_gpio
        esp_driver_uart
        nvs_flash
        spi_flash
)
