#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef esp_err_t (*st77912_write_fn_t)(void *user_ctx, bool is_data,
                                        const uint8_t *data, size_t len);

typedef struct {
    st77912_write_fn_t write;
    void *user_ctx;
    int reset_gpio;
    int reset_active_level;
    int backlight_gpio;
    int backlight_on_level;
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
} st77912_t;

size_t st77912_init_cmd_count(void);
esp_err_t st77912_init(const st77912_t *lcd);
esp_err_t st77912_write_param(const st77912_t *lcd, uint8_t cmd,
                              const uint8_t *data, size_t len);
esp_err_t st77912_set_madctl(const st77912_t *lcd, uint8_t madctl);
esp_err_t st77912_set_address_window(const st77912_t *lcd,
                                     uint16_t x0, uint16_t y0,
                                     uint16_t x1, uint16_t y1);
esp_err_t st77912_fill_rect(const st77912_t *lcd,
                            uint16_t x, uint16_t y,
                            uint16_t width, uint16_t height,
                            uint16_t rgb565,
                            uint8_t *work_buf, size_t work_buf_size);
esp_err_t st77912_fill_color(const st77912_t *lcd,
                             uint16_t rgb565,
                             uint8_t *work_buf, size_t work_buf_size);
