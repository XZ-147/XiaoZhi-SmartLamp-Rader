#include <stdlib.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_panel_st77912.h"

#define ST77912_CMD_RAMCTRL 0xB0
#define ST77912_DATA_LITTLE_ENDIAN_BIT BIT(3)

static const char *TAG = "lcd_panel.st77912";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
    uint8_t ramctl_val_1;
    uint8_t ramctl_val_2;
    const st77912_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int reset_level: 1;
    } flags;
} st77912_panel_t;

static esp_err_t panel_st77912_del(esp_lcd_panel_t *panel);
static esp_err_t panel_st77912_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_st77912_init(esp_lcd_panel_t *panel);
static esp_err_t panel_st77912_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                           const void *color_data);
static esp_err_t panel_st77912_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_st77912_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_st77912_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_st77912_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_st77912_disp_on_off(esp_lcd_panel_t *panel, bool on_off);
static esp_err_t panel_st77912_sleep(esp_lcd_panel_t *panel, bool sleep);

esp_err_t esp_lcd_new_panel_st77912(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    st77912_panel_t *st77912 = calloc(1, sizeof(st77912_panel_t));
    ESP_GOTO_ON_FALSE(st77912, ESP_ERR_NO_MEM, err, TAG, "no mem for st77912 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure reset GPIO failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        st77912->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        st77912->madctl_val = LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported RGB element order");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16:
        st77912->colmod_val = 0x55;
        st77912->fb_bits_per_pixel = 16;
        break;
    case 18:
        st77912->colmod_val = 0x66;
        st77912->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    st77912->ramctl_val_1 = 0x00;
    st77912->ramctl_val_2 = 0xF0;
    if (panel_dev_config->data_endian == LCD_RGB_DATA_ENDIAN_LITTLE) {
        st77912->ramctl_val_2 |= ST77912_DATA_LITTLE_ENDIAN_BIT;
    }

    const st77912_vendor_config_t *vendor_config = (const st77912_vendor_config_t *)panel_dev_config->vendor_config;
    if (vendor_config) {
        st77912->init_cmds = vendor_config->init_cmds;
        st77912->init_cmds_size = vendor_config->init_cmds_size;
    }

    st77912->io = io;
    st77912->reset_gpio_num = panel_dev_config->reset_gpio_num;
    st77912->flags.reset_level = panel_dev_config->flags.reset_active_high;
    st77912->base.del = panel_st77912_del;
    st77912->base.reset = panel_st77912_reset;
    st77912->base.init = panel_st77912_init;
    st77912->base.draw_bitmap = panel_st77912_draw_bitmap;
    st77912->base.invert_color = panel_st77912_invert_color;
    st77912->base.set_gap = panel_st77912_set_gap;
    st77912->base.mirror = panel_st77912_mirror;
    st77912->base.swap_xy = panel_st77912_swap_xy;
    st77912->base.disp_on_off = panel_st77912_disp_on_off;
    st77912->base.disp_sleep = panel_st77912_sleep;

    *ret_panel = &st77912->base;
    ESP_LOGI(TAG, "new st77912 panel @%p", st77912);
    return ESP_OK;

err:
    if (st77912) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(st77912);
    }
    return ret;
}

static esp_err_t panel_st77912_del(esp_lcd_panel_t *panel)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);

    if (st77912->reset_gpio_num >= 0) {
        gpio_reset_pin(st77912->reset_gpio_num);
    }
    free(st77912);
    return ESP_OK;
}

static esp_err_t panel_st77912_reset(esp_lcd_panel_t *panel)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);

    if (st77912->reset_gpio_num >= 0) {
        gpio_set_level(st77912->reset_gpio_num, st77912->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(st77912->reset_gpio_num, !st77912->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_SWRESET, NULL, 0), TAG, "software reset failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static const st77912_lcd_init_cmd_t vendor_specific_init_default[] = {
    {LCD_CMD_SLPOUT, NULL, 0, 120},
    {LCD_CMD_NORON, NULL, 0, 10},
};

static esp_err_t panel_st77912_init(esp_lcd_panel_t *panel)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    const st77912_lcd_init_cmd_t *init_cmds = st77912->init_cmds;
    uint16_t init_cmds_size = st77912->init_cmds_size;

    if (!init_cmds || init_cmds_size == 0) {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]);
    }

    for (int i = 0; i < init_cmds_size; ++i) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, init_cmds[i].cmd,
                                                      init_cmds[i].data, init_cmds[i].data_bytes),
                            TAG, "send init command failed");
        if (init_cmds[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
        }
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_MADCTL, (uint8_t[]) {
        st77912->madctl_val,
    }, 1), TAG, "send MADCTL failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_COLMOD, (uint8_t[]) {
        st77912->colmod_val,
    }, 1), TAG, "send COLMOD failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, ST77912_CMD_RAMCTRL, (uint8_t[]) {
        st77912->ramctl_val_1, st77912->ramctl_val_2,
    }, 2), TAG, "send RAMCTRL failed");

    return ESP_OK;
}

static esp_err_t panel_st77912_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                           const void *color_data)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);

    x_start += st77912->x_gap;
    x_end += st77912->x_gap;
    y_start += st77912->y_gap;
    y_end += st77912->y_gap;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG, "send CASET failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG, "send RASET failed");

    size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * st77912->fb_bits_per_pixel / 8;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(st77912->io, LCD_CMD_RAMWR, color_data, len), TAG, "send color failed");
    return ESP_OK;
}

static esp_err_t panel_st77912_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    int command = invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, command, NULL, 0), TAG, "send invert command failed");
    return ESP_OK;
}

static esp_err_t panel_st77912_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);

    if (mirror_x) {
        st77912->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        st77912->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        st77912->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        st77912->madctl_val &= ~LCD_CMD_MY_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_MADCTL, (uint8_t[]) {
        st77912->madctl_val,
    }, 1), TAG, "send MADCTL failed");
    return ESP_OK;
}

static esp_err_t panel_st77912_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);

    if (swap_axes) {
        st77912->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        st77912->madctl_val &= ~LCD_CMD_MV_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, LCD_CMD_MADCTL, (uint8_t[]) {
        st77912->madctl_val,
    }, 1), TAG, "send MADCTL failed");
    return ESP_OK;
}

static esp_err_t panel_st77912_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    st77912->x_gap = x_gap;
    st77912->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_st77912_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, command, NULL, 0), TAG, "send display command failed");
    if (on_off) {
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

static esp_err_t panel_st77912_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    int command = sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st77912->io, command, NULL, 0), TAG, "send sleep command failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}
