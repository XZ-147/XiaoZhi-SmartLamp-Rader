idf_component_register(
    SRCS "lcd_st77912.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer heap esp_rom
)
