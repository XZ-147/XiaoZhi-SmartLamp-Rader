#include "wifi_board.h"
#include "codecs/es8375_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "display/display.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>

#define TAG "ESP32-MarsbearSupport"

class CompactWifiBoard : public WifiBoard {
private:
    Button boot_button_;
    Button touch_button_;
    Button asr_button_;

    i2c_master_bus_handle_t codec_i2c_bus_ = nullptr;
    Display* display_ = nullptr;

    void LogExistingI2cBus(const char* stage) {
        i2c_master_bus_handle_t existing_bus = nullptr;
        esp_err_t ret = i2c_master_get_bus_handle(AUDIO_CODEC_I2C_NUM, &existing_bus);
        ESP_LOGI(TAG, "I2C port %d %s: %s handle=%p",
            AUDIO_CODEC_I2C_NUM, stage, esp_err_to_name(ret), existing_bus);
    }

    void PrepareCodecI2cPins() {
        ESP_ERROR_CHECK(gpio_reset_pin(AUDIO_CODEC_I2C_SDA_PIN));
        ESP_ERROR_CHECK(gpio_reset_pin(AUDIO_CODEC_I2C_SCL_PIN));
        ESP_ERROR_CHECK(gpio_set_direction(AUDIO_CODEC_I2C_SDA_PIN, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_direction(AUDIO_CODEC_I2C_SCL_PIN, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_pull_mode(AUDIO_CODEC_I2C_SDA_PIN, GPIO_PULLUP_ONLY));
        ESP_ERROR_CHECK(gpio_set_pull_mode(AUDIO_CODEC_I2C_SCL_PIN, GPIO_PULLUP_ONLY));
        ESP_LOGI(TAG, "Codec I2C idle level before init: sda=%d scl=%d",
            gpio_get_level(AUDIO_CODEC_I2C_SDA_PIN),
            gpio_get_level(AUDIO_CODEC_I2C_SCL_PIN));
    }

    bool DiagnosticScanI2cBus(gpio_num_t sda, gpio_num_t scl, const char* label) {
        ESP_LOGI(TAG, "Diagnostic I2C scan (%s): port=%d sda=%d scl=%d",
            label, AUDIO_CODEC_I2C_NUM, sda, scl);

        ESP_ERROR_CHECK(gpio_reset_pin(sda));
        ESP_ERROR_CHECK(gpio_reset_pin(scl));
        ESP_ERROR_CHECK(gpio_set_direction(sda, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_direction(scl, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_pull_mode(sda, GPIO_PULLUP_ONLY));
        ESP_ERROR_CHECK(gpio_set_pull_mode(scl, GPIO_PULLUP_ONLY));
        ESP_LOGI(TAG, "Diagnostic I2C idle level (%s): sda=%d scl=%d",
            label, gpio_get_level(sda), gpio_get_level(scl));

        i2c_master_bus_handle_t scan_bus = nullptr;
        i2c_master_bus_config_t bus_config = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        esp_err_t ret = i2c_new_master_bus(&bus_config, &scan_bus);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Diagnostic I2C scan (%s) bus init failed: %s", label, esp_err_to_name(ret));
            return false;
        }

        bool found = false;
        for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
            if (i2c_master_probe(scan_bus, addr, 50) == ESP_OK) {
                ESP_LOGI(TAG, "Diagnostic I2C scan (%s) found device at 0x%02x", label, addr);
                found = true;
            }
        }
        if (!found) {
            ESP_LOGW(TAG, "Diagnostic I2C scan (%s) found no devices", label);
        }

        ESP_ERROR_CHECK(i2c_del_master_bus(scan_bus));
        ESP_ERROR_CHECK(gpio_reset_pin(sda));
        ESP_ERROR_CHECK(gpio_reset_pin(scl));
        return found;
    }

    void RunCodecI2cDiagnostics() {
        LogExistingI2cBus("before diagnostic scan");
        bool normal_found = DiagnosticScanI2cBus(AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN, "normal");
        LogExistingI2cBus("after normal diagnostic scan");

        if (!normal_found) {
            bool swapped_found = DiagnosticScanI2cBus(AUDIO_CODEC_I2C_SCL_PIN, AUDIO_CODEC_I2C_SDA_PIN, "sda/scl swapped");
            if (swapped_found) {
                ESP_LOGW(TAG, "I2C device was found only when SDA/SCL were swapped; check schematic labels or config.h pin order");
            }
            LogExistingI2cBus("after swapped diagnostic scan");
        }
    }

    void InitializeCodecI2c() {
        ESP_LOGI(TAG, "Initialize codec I2C port=%d sda=%d scl=%d", AUDIO_CODEC_I2C_NUM, AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
        RunCodecI2cDiagnostics();
        PrepareCodecI2cPins();
        LogExistingI2cBus("before codec bus init");
        i2c_master_bus_config_t bus_config = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        esp_err_t ret = i2c_new_master_bus(&bus_config, &codec_i2c_bus_);
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "I2C port %d was already initialized before codec setup; remove stale display I2C init or use a free I2C port", AUDIO_CODEC_I2C_NUM);
        }
        ESP_ERROR_CHECK(ret);
        LogExistingI2cBus("after codec bus init");
    }

    void InitializeButtons() {
        
        // 配置 GPIO
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << BUILTIN_LED_GPIO,  // 设置需要配置的 GPIO 引脚
            .mode = GPIO_MODE_OUTPUT,           // 设置为输出模式
            .pull_up_en = GPIO_PULLUP_DISABLE,  // 禁用上拉
            .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 禁用下拉
            .intr_type = GPIO_INTR_DISABLE      // 禁用中断
        };
        gpio_config(&io_conf);  // 应用配置

        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            gpio_set_level(BUILTIN_LED_GPIO, 1);
            app.ToggleChatState();
        });

        asr_button_.OnClick([this]() {
            std::string wake_word="你好小智";
            Application::GetInstance().WakeWordInvoke(wake_word);
        });

        touch_button_.OnPressDown([this]() {
            gpio_set_level(BUILTIN_LED_GPIO, 1);
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            gpio_set_level(BUILTIN_LED_GPIO, 0);
            Application::GetInstance().StopListening();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    CompactWifiBoard() : WifiBoard(), boot_button_(BOOT_BUTTON_GPIO), touch_button_(TOUCH_BUTTON_GPIO), asr_button_(ASR_BUTTON_GPIO)
    {
        display_ = new NoDisplay();
        InitializeCodecI2c();
        InitializeButtons();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override 
    {
        static Es8375AudioCodec audio_codec(
            codec_i2c_bus_,
            AUDIO_CODEC_I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8375_ADDR,
            true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        if (display_ == nullptr) {
            static NoDisplay no_display;
            return &no_display;
        }
        return display_;
    }

};

DECLARE_BOARD(CompactWifiBoard);
