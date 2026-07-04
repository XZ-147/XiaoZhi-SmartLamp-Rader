#include "lcd_st77912.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ST77912_CMD_CASET 0x2A
#define ST77912_CMD_RASET 0x2B
#define ST77912_CMD_RAMWR 0x2C
#define ST77912_CMD_MADCTL 0x36
#define ST77912_MAX_INIT_DATA 14

typedef struct {
    uint8_t cmd;
    uint8_t data[ST77912_MAX_INIT_DATA];
    uint8_t data_len;
    uint16_t delay_ms;
} st77912_init_cmd_t;

static const st77912_init_cmd_t s_st77912_init_table[] = {
    {0xF0, {0x01}, 1, 0},
    {0xF1, {0x01}, 1, 0},
    {0x7A, {0x83}, 1, 0},
    {0xB0, {0x5E}, 1, 0},
    {0xB1, {0x55}, 1, 0},
    {0xB2, {0x24}, 1, 0},
    {0xB4, {0xA7}, 1, 0},
    {0xB5, {0x54}, 1, 0},
    {0xB6, {0x8B}, 1, 0},
    {0xB7, {0x50}, 1, 0},
    {0xBA, {0x00}, 1, 0},
    {0xBB, {0x08}, 1, 0},
    {0xBC, {0x08}, 1, 0},
    {0xBD, {0x00}, 1, 0},
    {0xC0, {0x80}, 1, 0},
    {0xC1, {0x08}, 1, 0},
    {0xC2, {0x54}, 1, 0},
    {0xC3, {0x80}, 1, 0},
    {0xC4, {0x08}, 1, 0},
    {0xC5, {0x54}, 1, 0},
    {0xC6, {0xA9}, 1, 0},
    {0xC7, {0x41}, 1, 0},
    {0xC8, {0x51}, 1, 0},
    {0xC9, {0xA9}, 1, 0},
    {0xCA, {0x41}, 1, 0},
    {0xCB, {0x51}, 1, 0},
    {0xD0, {0x80}, 1, 0},
    {0xD1, {0xF0}, 1, 0},
    {0xD2, {0xF0}, 1, 0},
    {0xF5, {0x00, 0xA5}, 2, 0},
    {0xDD, {0x36}, 1, 0},
    {0xDE, {0x36}, 1, 0},
    {0xF0, {0x02}, 1, 0},
    {0xF1, {0x01}, 1, 0},
    {0xE0, {0xF0, 0x16, 0x1C, 0x0A, 0x0A, 0x06, 0x3E, 0x33, 0x53, 0x07, 0x14, 0x13, 0x31, 0x35}, 14, 0},
    {0xE1, {0xF0, 0x16, 0x1C, 0x0A, 0x0A, 0x06, 0x3E, 0x33, 0x53, 0x07, 0x14, 0x13, 0x31, 0x35}, 14, 0},
    {0xF0, {0x10}, 1, 0},
    {0xF3, {0x10}, 1, 0},
    {0xE0, {0x0B}, 1, 0},
    {0xE1, {0x00}, 1, 0},
    {0xE2, {0x00}, 1, 0},
    {0xE3, {0x00}, 1, 0},
    {0xE4, {0xE0}, 1, 0},
    {0xE5, {0x06}, 1, 0},
    {0xE6, {0x21}, 1, 0},
    {0xE7, {0x80}, 1, 0},
    {0xE8, {0x0A}, 1, 0},
    {0xE9, {0x00}, 1, 0},
    {0xEA, {0x04}, 1, 0},
    {0xEB, {0x00}, 1, 0},
    {0xEC, {0x00}, 1, 0},
    {0xED, {0x24}, 1, 0},
    {0xEE, {0x00}, 1, 0},
    {0xEF, {0x00}, 1, 0},
    {0xF8, {0xFF}, 1, 0},
    {0xF9, {0x00}, 1, 0},
    {0xFA, {0x00}, 1, 0},
    {0xFB, {0x30}, 1, 0},
    {0xFC, {0x00}, 1, 0},
    {0xFD, {0x00}, 1, 0},
    {0xFE, {0x00}, 1, 0},
    {0xFF, {0x00}, 1, 0},
    {0x60, {0x40}, 1, 0},
    {0x61, {0x08}, 1, 0},
    {0x62, {0x00}, 1, 0},
    {0x63, {0x41}, 1, 0},
    {0x64, {0xED}, 1, 0},
    {0x65, {0x00}, 1, 0},
    {0x66, {0x40}, 1, 0},
    {0x67, {0x00}, 1, 0},
    {0x68, {0x00}, 1, 0},
    {0x69, {0x40}, 1, 0},
    {0x6A, {0x00}, 1, 0},
    {0x6B, {0x00}, 1, 0},
    {0x70, {0x40}, 1, 0},
    {0x71, {0x07}, 1, 0},
    {0x72, {0x00}, 1, 0},
    {0x73, {0x41}, 1, 0},
    {0x74, {0xEC}, 1, 0},
    {0x75, {0x00}, 1, 0},
    {0x76, {0x40}, 1, 0},
    {0x77, {0x00}, 1, 0},
    {0x78, {0x00}, 1, 0},
    {0x79, {0x40}, 1, 0},
    {0x7A, {0x00}, 1, 0},
    {0x7B, {0x00}, 1, 0},
    {0x80, {0x48}, 1, 0},
    {0x81, {0x00}, 1, 0},
    {0x82, {0x0A}, 1, 0},
    {0x83, {0x01}, 1, 0},
    {0x84, {0xEA}, 1, 0},
    {0x85, {0x00}, 1, 0},
    {0x86, {0x00}, 1, 0},
    {0x87, {0x00}, 1, 0},
    {0x88, {0x48}, 1, 0},
    {0x89, {0x00}, 1, 0},
    {0x8A, {0x0C}, 1, 0},
    {0x8B, {0x01}, 1, 0},
    {0x8C, {0xEC}, 1, 0},
    {0x8D, {0x00}, 1, 0},
    {0x8E, {0x00}, 1, 0},
    {0x8F, {0x00}, 1, 0},
    {0x90, {0x48}, 1, 0},
    {0x91, {0x00}, 1, 0},
    {0x92, {0x0E}, 1, 0},
    {0x93, {0x01}, 1, 0},
    {0x94, {0xEE}, 1, 0},
    {0x95, {0x00}, 1, 0},
    {0x96, {0x00}, 1, 0},
    {0x97, {0x00}, 1, 0},
    {0x98, {0x48}, 1, 0},
    {0x99, {0x00}, 1, 0},
    {0x9A, {0x10}, 1, 0},
    {0x9B, {0x01}, 1, 0},
    {0x9C, {0xF0}, 1, 0},
    {0x9D, {0x00}, 1, 0},
    {0x9E, {0x00}, 1, 0},
    {0x9F, {0x00}, 1, 0},
    {0xA0, {0x48}, 1, 0},
    {0xA1, {0x00}, 1, 0},
    {0xA2, {0x09}, 1, 0},
    {0xA3, {0x01}, 1, 0},
    {0xA4, {0xE9}, 1, 0},
    {0xA5, {0x00}, 1, 0},
    {0xA6, {0x00}, 1, 0},
    {0xA7, {0x00}, 1, 0},
    {0xA8, {0x48}, 1, 0},
    {0xA9, {0x00}, 1, 0},
    {0xAA, {0x0B}, 1, 0},
    {0xAB, {0x01}, 1, 0},
    {0xAC, {0xEB}, 1, 0},
    {0xAD, {0x00}, 1, 0},
    {0xAE, {0x00}, 1, 0},
    {0xAF, {0x00}, 1, 0},
    {0xB0, {0x48}, 1, 0},
    {0xB1, {0x00}, 1, 0},
    {0xB2, {0x0D}, 1, 0},
    {0xB3, {0x01}, 1, 0},
    {0xB4, {0xED}, 1, 0},
    {0xB5, {0x00}, 1, 0},
    {0xB6, {0x00}, 1, 0},
    {0xB7, {0x00}, 1, 0},
    {0xB8, {0x48}, 1, 0},
    {0xB9, {0x00}, 1, 0},
    {0xBA, {0x0F}, 1, 0},
    {0xBB, {0x01}, 1, 0},
    {0xBC, {0xEF}, 1, 0},
    {0xBD, {0x00}, 1, 0},
    {0xBE, {0x00}, 1, 0},
    {0xBF, {0x00}, 1, 0},
    {0xC0, {0x88}, 1, 0},
    {0xC1, {0x99}, 1, 0},
    {0xC2, {0x01}, 1, 0},
    {0xC3, {0xAA}, 1, 0},
    {0xC4, {0xBB}, 1, 0},
    {0xC5, {0x74}, 1, 0},
    {0xC6, {0x65}, 1, 0},
    {0xC7, {0x56}, 1, 0},
    {0xC8, {0x47}, 1, 0},
    {0xC9, {0x10}, 1, 0},
    {0xD0, {0x88}, 1, 0},
    {0xD1, {0x99}, 1, 0},
    {0xD2, {0x01}, 1, 0},
    {0xD3, {0xAA}, 1, 0},
    {0xD4, {0xBB}, 1, 0},
    {0xD5, {0x74}, 1, 0},
    {0xD6, {0x65}, 1, 0},
    {0xD7, {0x56}, 1, 0},
    {0xD8, {0x47}, 1, 0},
    {0xD9, {0x10}, 1, 0},
    {0xF0, {0x08}, 1, 0},
    {0xF2, {0x08}, 1, 0},
    {0x71, {0x03}, 1, 0},
    {0x73, {0x30}, 1, 0},
    {0x76, {0x00}, 1, 0},
    {0x78, {0x33}, 1, 0},
    {0x79, {0x01}, 1, 0},
    {0x7B, {0xFA}, 1, 0},
    {0x7E, {0x16}, 1, 0},
    {0x86, {0x55}, 1, 0},
    {0x89, {0x61}, 1, 0},
    {0x8A, {0x00}, 1, 0},
    {0xF0, {0x01}, 1, 0},
    {0xF1, {0x01}, 1, 0},
    {0xA0, {0x0B}, 1, 0},
    {0xA3, {0x2A}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x2B}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x2C}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x2D}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x2E}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x2F}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x30}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x31}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x32}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA3, {0x33}, 1, 0},
    {0xA5, {0xC3}, 1, 1},
    {0xA0, {0x09}, 1, 0},
    {0xF0, {0x00}, 1, 0},
    {0xF1, {0x10}, 1, 0},
    {0xF2, {0x84}, 1, 0},
    {0xF3, {0x01}, 1, 0},
    {0x3A, {0x55}, 1, 0},
    {0x21, {0}, 0, 0},
    {0x11, {0}, 0, 120},
    {0x29, {0}, 0, 0},
};

static esp_err_t configure_optional_output(int gpio, int level)
{
    if (gpio < 0) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level((gpio_num_t)gpio, level);
}

static esp_err_t write_cmd(const st77912_t *lcd, uint8_t cmd)
{
    return lcd->write(lcd->user_ctx, false, &cmd, 1);
}

static esp_err_t write_data(const st77912_t *lcd, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }

    return lcd->write(lcd->user_ctx, true, data, len);
}

static esp_err_t validate_lcd(const st77912_t *lcd)
{
    if (lcd == NULL || lcd->write == NULL || lcd->width == 0 || lcd->height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

size_t st77912_init_cmd_count(void)
{
    return sizeof(s_st77912_init_table) / sizeof(s_st77912_init_table[0]);
}

esp_err_t st77912_write_param(const st77912_t *lcd, uint8_t cmd,
                              const uint8_t *data, size_t len)
{
    esp_err_t err = validate_lcd(lcd);
    if (err != ESP_OK) {
        return err;
    }

    err = write_cmd(lcd, cmd);
    if (err != ESP_OK) {
        return err;
    }

    return write_data(lcd, data, len);
}

esp_err_t st77912_set_madctl(const st77912_t *lcd, uint8_t madctl)
{
    return st77912_write_param(lcd, ST77912_CMD_MADCTL, &madctl, 1);
}

esp_err_t st77912_init(const st77912_t *lcd)
{
    esp_err_t err = validate_lcd(lcd);
    if (err != ESP_OK) {
        return err;
    }

    if (lcd->backlight_gpio >= 0) {
        err = configure_optional_output(lcd->backlight_gpio, !lcd->backlight_on_level);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (lcd->reset_gpio >= 0) {
        const int inactive_level = !lcd->reset_active_level;
        err = configure_optional_output(lcd->reset_gpio, inactive_level);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        err = gpio_set_level((gpio_num_t)lcd->reset_gpio, lcd->reset_active_level);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(800));
        err = gpio_set_level((gpio_num_t)lcd->reset_gpio, inactive_level);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(800));
    }

    for (size_t i = 0; i < st77912_init_cmd_count(); ++i) {
        const st77912_init_cmd_t *init = &s_st77912_init_table[i];

        err = write_cmd(lcd, init->cmd);
        if (err != ESP_OK) {
            return err;
        }

        err = write_data(lcd, init->data, init->data_len);
        if (err != ESP_OK) {
            return err;
        }

        if (init->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(init->delay_ms));
        }
    }

    if (lcd->backlight_gpio >= 0) {
        err = gpio_set_level((gpio_num_t)lcd->backlight_gpio, lcd->backlight_on_level);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t st77912_set_address_window(const st77912_t *lcd,
                                     uint16_t x0, uint16_t y0,
                                     uint16_t x1, uint16_t y1)
{
    esp_err_t err = validate_lcd(lcd);
    if (err != ESP_OK) {
        return err;
    }

    if (x0 > x1 || y0 > y1 || x1 >= lcd->width || y1 >= lcd->height) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t xs = x0 + lcd->x_offset;
    const uint16_t xe = x1 + lcd->x_offset;
    const uint16_t ys = y0 + lcd->y_offset;
    const uint16_t ye = y1 + lcd->y_offset;

    uint8_t data[4] = {
        (uint8_t)(xs >> 8), (uint8_t)(xs & 0xFF),
        (uint8_t)(xe >> 8), (uint8_t)(xe & 0xFF),
    };

    err = write_cmd(lcd, ST77912_CMD_CASET);
    if (err != ESP_OK) {
        return err;
    }
    err = write_data(lcd, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    data[0] = (uint8_t)(ys >> 8);
    data[1] = (uint8_t)(ys & 0xFF);
    data[2] = (uint8_t)(ye >> 8);
    data[3] = (uint8_t)(ye & 0xFF);

    err = write_cmd(lcd, ST77912_CMD_RASET);
    if (err != ESP_OK) {
        return err;
    }
    return write_data(lcd, data, sizeof(data));
}

esp_err_t st77912_fill_rect(const st77912_t *lcd,
                            uint16_t x, uint16_t y,
                            uint16_t width, uint16_t height,
                            uint16_t rgb565,
                            uint8_t *work_buf, size_t work_buf_size)
{
    esp_err_t err = validate_lcd(lcd);
    if (err != ESP_OK) {
        return err;
    }

    if (work_buf == NULL || work_buf_size < 2 || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((uint32_t)x + width > lcd->width || (uint32_t)y + height > lcd->height) {
        return ESP_ERR_INVALID_ARG;
    }

    work_buf_size &= ~(size_t)1;
    const size_t chunk_pixels = work_buf_size / 2;
    const size_t total_pixels = (size_t)width * height;

    for (size_t i = 0; i < chunk_pixels; ++i) {
        work_buf[i * 2] = (uint8_t)(rgb565 >> 8);
        work_buf[i * 2 + 1] = (uint8_t)(rgb565 & 0xFF);
    }

    err = st77912_set_address_window(lcd, x, y, x + width - 1, y + height - 1);
    if (err != ESP_OK) {
        return err;
    }

    err = write_cmd(lcd, ST77912_CMD_RAMWR);
    if (err != ESP_OK) {
        return err;
    }

    size_t remaining = total_pixels;
    while (remaining > 0) {
        const size_t pixels = remaining > chunk_pixels ? chunk_pixels : remaining;
        err = write_data(lcd, work_buf, pixels * 2);
        if (err != ESP_OK) {
            return err;
        }
        remaining -= pixels;
        vTaskDelay(0);
    }

    return ESP_OK;
}

esp_err_t st77912_fill_color(const st77912_t *lcd,
                             uint16_t rgb565,
                             uint8_t *work_buf, size_t work_buf_size)
{
    esp_err_t err = validate_lcd(lcd);
    if (err != ESP_OK) {
        return err;
    }

    return st77912_fill_rect(lcd, 0, 0, lcd->width, lcd->height,
                             rgb565, work_buf, work_buf_size);
}
