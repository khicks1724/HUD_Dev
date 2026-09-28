idf_component_register(
    SRCS
        "src/lcd_st7789.c"
        "src/qmi8658.c"
        "src/mmc5983ma.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_timer freertos
)
