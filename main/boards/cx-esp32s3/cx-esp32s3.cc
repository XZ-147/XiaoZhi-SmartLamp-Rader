#include "wifi_board.h"
#include "audio_codec.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "i2c_device.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>

#define TAG "atk_dnesp32s3_box"

#define XL9555_INPUT_PORT0_REG      0                               /* 输入寄存器0地址 */
#define XL9555_INPUT_PORT1_REG      1                               /* 输入寄存器1地址 */
#define XL9555_OUTPUT_PORT0_REG     2                               /* 输出寄存器0地址 */
#define XL9555_OUTPUT_PORT1_REG     3                               /* 输出寄存器1地址 */
#define XL9555_INVERSION_PORT0_REG  4                               /* 极性反转寄存器0地址 */
#define XL9555_INVERSION_PORT1_REG  5                               /* 极性反转寄存器1地址 */
#define XL9555_CONFIG_PORT0_REG     6                               /* 方向配置寄存器0地址 */
#define XL9555_CONFIG_PORT1_REG     7                               /* 方向配置寄存器1地址 */

/* XL9555各个IO的功能 */
#define IO0_0                       0x0001
#define IO0_1                       0x0002
#define IO0_2                       0x0004
#define IO0_3                       0x0008
#define IO0_4                       0x0010
#define IO0_5                       0x0020
#define IO0_6                       0x0040
#define IO0_7                       0x0080

#define IO1_0                       0x0100
#define IO1_1                       0x0200
#define IO1_2                       0x0400  //背光
#define IO1_3                       0x0800
#define IO1_4                       0x1000
#define IO1_5                       0x2000
#define IO1_6                       0x4000
#define IO1_7                       0x8000

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class XL9555_IN : public I2cDevice {
public:
    XL9555_IN(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(XL9555_CONFIG_PORT0_REG, (~IO0_0)&0xFF);
        WriteReg(XL9555_CONFIG_PORT1_REG, (~((IO1_0|IO1_2|IO1_3)>>8))&0xFF);
    }

    void xl9555_cfg(void) {
        WriteReg(XL9555_CONFIG_PORT0_REG, (~IO0_0)&0xFF);
        WriteReg(XL9555_CONFIG_PORT1_REG, (~((IO1_0|IO1_2|IO1_3)>>8))&0xFF);
    }

    void SetOutputState(uint16_t pin, uint8_t level) {
        uint16_t data;
        if (pin <= IO0_7) {
            data = ReadReg(0x02);
        } else {
            data = ReadReg(0x03);
            pin = pin>>8;
        }

        if(level)
            data = data | (pin);
        else
            data = data & ~(pin);

        if (pin <= IO0_7) {
            WriteReg(0x02, data);
        } else {
            WriteReg(0x03, data);
        }
    }

    int GetPingState(uint16_t pin) {
        uint8_t data;
        if (pin <= IO0_7) {
            data = ReadReg(0x00);
            return (data & (uint8_t)(pin & 0xFF)) ? 1 : 0;
        } else {
            data = ReadReg(0x01);
            return (data & (uint8_t)((pin >> 8) & 0xFF )) ? 1 : 0;
        }

        return 0;
    }
};

class cx_esp32s3 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t xl9555_handle_;
    Button boot_button_;
    LcdDisplay* display_;
    XL9555_IN* xl9555_in_;
    
    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = GPIO_NUM_10,
            .scl_io_num = GPIO_NUM_11,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize XL9555
        xl9555_in_ = new XL9555_IN(i2c_bus_, 0x20);
        xl9555_in_->xl9555_cfg();
    }

    void ST7789_i80_Display() {
        esp_lcd_panel_handle_t lcd_panel = NULL;
        esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
        esp_lcd_i80_bus_handle_t i80_bus = NULL;
        esp_lcd_i80_bus_config_t bus_config = {
            .dc_gpio_num = LCD_NUM_DC,
            .wr_gpio_num = LCD_NUM_WR,
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .data_gpio_nums = {
                GPIO_LCD_D0,
                GPIO_LCD_D1,
                GPIO_LCD_D2,
                GPIO_LCD_D3,
                GPIO_LCD_D4,
                GPIO_LCD_D5,
                GPIO_LCD_D6,
                GPIO_LCD_D7,
            },
            .bus_width = 8,
            .max_transfer_bytes = DISPLAY_HEIGHT * 100 * sizeof(uint16_t),
            .dma_burst_size = 64,
        };
        ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));
        esp_lcd_panel_io_i80_config_t io_config = {
            .cs_gpio_num = LCD_NUM_CS,
            .pclk_hz = DISPLAY_WIDTH*DISPLAY_HEIGHT*30,
            .trans_queue_depth = 10,
            .on_color_trans_done = NULL,
            .user_ctx = NULL,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .dc_levels = {
                .dc_idle_level = 0,
                .dc_cmd_level = 0,
                .dc_dummy_level = 0,
                .dc_data_level = 1,
            },
            .flags = {
                .swap_color_bytes = 0, // Swap can be done in LvGL (default) or DMA
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &lcd_io_handle));
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GPIO_NUM_NC,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd_io_handle, &panel_config, &lcd_panel));

        xl9555_in_->SetOutputState(IO1_3, 1);  //LCD_RST
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_lcd_panel_reset(lcd_panel);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_lcd_panel_init(lcd_panel);

        esp_lcd_panel_swap_xy(lcd_panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(lcd_panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(lcd_panel, true);
        display_ = new SpiLcdDisplay(lcd_io_handle, lcd_panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        #if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                            .emoji_font = font_emoji_32_init(),
                                        #else
                                            .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
                                        #endif
                                    });
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

public:
    cx_esp32s3() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        ST7789_i80_Display();
        xl9555_in_->SetOutputState(IO0_0, 1);
        xl9555_in_->SetOutputState(IO1_2, 1);
        xl9555_in_->SetOutputState(IO1_3, 1);
        InitializeButtons();
    }

    virtual AudioCodec* GetAudioCodec() override {

            static NoAudioCodecSimplexPdm audio_codec(
                AUDIO_INPUT_SAMPLE_RATE,
                AUDIO_OUTPUT_SAMPLE_RATE,
                AUDIO_I2S_GPIO_BCLK,
                AUDIO_I2S_GPIO_WS,
                AUDIO_I2S_GPIO_DOUT,
                AUDIO_PDM_GPIO_CLK,
                AUDIO_PDM_GPIO_DATA
                );
                return &audio_codec;
    
        return NULL;
    }
    
    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(cx_esp32s3);
