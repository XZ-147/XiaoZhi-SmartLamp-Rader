#include "es8375_audio_codec.h"

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

#define TAG "Es8375AudioCodec"

// ES8375 register addresses used by this board-level driver.
#define ES8375_RST_CTRL          0x00
#define ES8375_SUB_RST           0x01
#define ES8375_CLK_EN            0x02
#define ES8375_CLK_SEL           0x03
#define ES8375_PRE_DIV_DLL       0x04
#define ES8375_DMIC_DIV_DAC_CLK  0x05
#define ES8375_ADC_CLK_SEL       0x06
#define ES8375_ADCDAC_DSP_CLK    0x07
#define ES8375_ADC_OSR           0x08
#define ES8375_DAC_HOLD          0x09
#define ES8375_OSC_CTRL          0x0A
#define ES8375_BCLK_DIV          0x0B
#define ES8375_LRCK_DIV_H        0x0C
#define ES8375_LRCK_DIV_L        0x0D
#define ES8375_SPK_CLK_DIV       0x0E
#define ES8375_PWR_STATE         0x0F
#define ES8375_SDP_CFG           0x15
#define ES8375_ADC_OUT_SEL       0x16
#define ES8375_ADC_SRC_GAIN      0x17
#define ES8375_ADC_SYNC_RAMP     0x18
#define ES8375_ADC_OSR_GAIN      0x19
#define ES8375_ADC_VOLUME        0x1A
#define ES8375_DAC_CTRL          0x1F
#define ES8375_DAC_SYNC_RAMP     0x20
#define ES8375_DAC_VOLUME        0x21
#define ES8375_DAC_SCALE         0x22
#define ES8375_OTP_CTRL          0x27
#define ES8375_SPK_EN            0x28
#define ES8375_SPK_PWR           0x29
#define ES8375_SPK_BIAS          0x2A
#define ES8375_SPK_VOL           0x2B
#define ES8375_ANA_EN            0x2E
#define ES8375_ADC_ANA_EN        0x32
#define ES8375_PGA_GAIN          0x37
#define ES8375_SYS_IO_CTRL       0xF8
#define ES8375_ISOLATE           0xF9
#define ES8375_FLAG              0xFA
#define ES8375_CHIP_ID1          0xFD
#define ES8375_CHIP_ID0          0xFE
#define ES8375_SECURITY_CODE     0xFF

Es8375AudioCodec::Es8375AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, uint8_t es8375_addr, bool use_mclk) {
    duplex_ = true;
    input_reference_ = false;
    input_channels_ = 1;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 30;
    pa_pin_ = pa_pin;

    assert(input_sample_rate_ == output_sample_rate_);

    i2c_bus_ = (i2c_master_bus_handle_t)i2c_master_handle;
    bool codec_present = ProbeCodec(es8375_addr);

    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = es8375_addr,
        .scl_speed_hz = 100 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &i2c_device_cfg, &i2c_device_));
    ESP_LOGI(TAG, "ES8375 I2C port=%d addr=0x%02x", i2c_port, es8375_addr);

    if (codec_present) {
        InitializeCodec(use_mclk);
    } else {
        codec_ready_ = false;
        ESP_LOGW(TAG, "ES8375 was not detected; skip register initialization");
    }
    CreateDuplexChannels(use_mclk ? mclk : GPIO_NUM_NC, bclk, ws, dout, din);
}

Es8375AudioCodec::~Es8375AudioCodec() {
    if (rx_handle_ != nullptr) {
        i2s_channel_disable(rx_handle_);
    }
    if (tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
    }
    if (i2c_device_ != nullptr) {
        i2c_master_bus_rm_device(i2c_device_);
    }
}

bool Es8375AudioCodec::ProbeCodec(uint8_t es8375_addr) {
    esp_err_t ret = i2c_master_probe(i2c_bus_, es8375_addr, 1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C probe OK at 0x%02x", es8375_addr);
        return true;
    }

    ESP_LOGW(TAG, "I2C probe failed at 0x%02x: %s", es8375_addr, esp_err_to_name(ret));
    return false;
}

esp_err_t Es8375AudioCodec::WriteReg(uint8_t reg, uint8_t value) {
    if (!codec_ready_) {
        return ESP_FAIL;
    }
    uint8_t data[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(i2c_device_, data, sizeof(data), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg 0x%02x=0x%02x failed: %s", reg, value, esp_err_to_name(ret));
        codec_ready_ = false;
    }
    return ret;
}

esp_err_t Es8375AudioCodec::ReadReg(uint8_t reg, uint8_t* value) {
    if (!codec_ready_) {
        return ESP_FAIL;
    }
    *value = 0;
    esp_err_t ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, value, 1, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read reg 0x%02x failed: %s", reg, esp_err_to_name(ret));
        codec_ready_ = false;
    }
    return ret;
}

void Es8375AudioCodec::UpdateReg(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t old_value = 0;
    if (ReadReg(reg, &old_value) != ESP_OK) {
        return;
    }
    uint8_t new_value = (old_value & ~mask) | (value & mask);
    if (new_value != old_value) {
        WriteReg(reg, new_value);
    }
}

void Es8375AudioCodec::DumpRegisters() {
    static const uint8_t regs[] = {
        ES8375_RST_CTRL, ES8375_SUB_RST, ES8375_CLK_EN, ES8375_CLK_SEL,
        ES8375_PRE_DIV_DLL, ES8375_DMIC_DIV_DAC_CLK, ES8375_ADC_CLK_SEL,
        ES8375_ADCDAC_DSP_CLK, ES8375_ADC_OSR, ES8375_DAC_HOLD,
        ES8375_OSC_CTRL, ES8375_BCLK_DIV, ES8375_LRCK_DIV_H, ES8375_LRCK_DIV_L,
        ES8375_PWR_STATE, ES8375_SDP_CFG, ES8375_ADC_OUT_SEL,
        ES8375_ADC_SRC_GAIN, ES8375_ADC_SYNC_RAMP, ES8375_ADC_OSR_GAIN,
        ES8375_ADC_VOLUME, ES8375_DAC_CTRL, ES8375_DAC_VOLUME,
        ES8375_DAC_SCALE, ES8375_SPK_EN, ES8375_SPK_PWR, ES8375_ANA_EN,
        ES8375_ADC_ANA_EN, ES8375_PGA_GAIN, ES8375_SYS_IO_CTRL,
        ES8375_ISOLATE, ES8375_FLAG, ES8375_CHIP_ID1, ES8375_CHIP_ID0,
        ES8375_SECURITY_CODE,
    };

    for (uint8_t reg : regs) {
        uint8_t value = 0;
        if (ReadReg(reg, &value) == ESP_OK) {
            ESP_LOGI(TAG, "reg[0x%02x]=0x%02x", reg, value);
        }
    }
}

void Es8375AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    ESP_LOGI(TAG, "I2S GPIO mclk=%d bclk=%d ws=%d dout=%d din=%d", mclk, bclk, ws, dout, din);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
}

void Es8375AudioCodec::ConfigureClock(bool use_mclk) {
    if (!use_mclk && input_sample_rate_ != 16000) {
        ESP_LOGW(TAG, "SCLK clock source is tuned for 16 kHz on this ESP32 board; current rate=%d", input_sample_rate_);
    }

    // Slave mode, normal I2S, 16-bit samples. In BCLK clock-source mode the
    // codec derives its internal main clock from BCLK; this avoids ESP32
    // classic MCLK output limits.
    WriteReg(ES8375_SUB_RST, 0x00);
    WriteReg(ES8375_CLK_EN, use_mclk ? 0xBE : 0x7E);
    WriteReg(ES8375_CLK_SEL, use_mclk ? 0x00 : 0x80);
    WriteReg(ES8375_PRE_DIV_DLL, 0x05);

    if (!use_mclk && input_sample_rate_ == 16000) {
        // BCLK = 16 kHz * 16 bits * 2 slots = 512 kHz.
        WriteReg(ES8375_DMIC_DIV_DAC_CLK, 0x34);
        WriteReg(ES8375_ADC_CLK_SEL, 0xDD);
        WriteReg(ES8375_ADCDAC_DSP_CLK, 0x55);
        WriteReg(ES8375_ADC_OSR, 0x1F);
        WriteReg(ES8375_DAC_HOLD, 0x00);
        WriteReg(ES8375_OSC_CTRL, 0x15);
        WriteReg(ES8375_BCLK_DIV, 0x00);
        WriteReg(ES8375_LRCK_DIV_H, 0x20);
        WriteReg(ES8375_LRCK_DIV_L, 0xFF);
        WriteReg(ES8375_SPK_CLK_DIV, 0x00);
        WriteReg(ES8375_ADC_OSR_GAIN, 0x1F);
    } else {
        // Common 12.288 MHz / 48 kHz style default for boards that provide MCLK.
        WriteReg(ES8375_DMIC_DIV_DAC_CLK, 0x08);
        WriteReg(ES8375_ADC_CLK_SEL, 0x10);
        WriteReg(ES8375_ADCDAC_DSP_CLK, 0x40);
        WriteReg(ES8375_ADC_OSR, 0x1F);
        WriteReg(ES8375_DAC_HOLD, 0x00);
        WriteReg(ES8375_OSC_CTRL, 0x15);
        WriteReg(ES8375_BCLK_DIV, 0x00);
    }

    UpdateReg(ES8375_SDP_CFG, 0x1C, 0x0C);
}

void Es8375AudioCodec::InitializeCodec(bool use_mclk) {
    codec_ready_ = true;

    WriteReg(ES8375_RST_CTRL, 0x3F);
    vTaskDelay(pdMS_TO_TICKS(2));
    WriteReg(ES8375_RST_CTRL, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));

    ConfigureClock(use_mclk);

    WriteReg(ES8375_ANA_EN, 0xF8);
    WriteReg(ES8375_ADC_ANA_EN, 0xF0);
    WriteReg(ES8375_OTP_CTRL, 0x80);
    WriteReg(ES8375_ADC_SRC_GAIN, 0x00);
    WriteReg(ES8375_ADC_SYNC_RAMP, 0x00);
    WriteReg(ES8375_DAC_CTRL, 0x04);
    WriteReg(ES8375_DAC_SYNC_RAMP, 0x00);
    WriteReg(ES8375_DAC_SCALE, 0x1F);
    WriteReg(ES8375_PGA_GAIN, 0x40 | 0x0A);
    WriteReg(ES8375_ADC_VOLUME, 0xBF);
    WriteReg(ES8375_DAC_VOLUME, 0xBF);
    WriteReg(ES8375_PWR_STATE, 0xA0);
    SetPlaybackMute(true);
    SetCaptureMute(true);

    uint8_t chip_id1 = 0;
    uint8_t chip_id0 = 0;
    uint8_t security_code = 0;
    esp_err_t id1_ret = ReadReg(ES8375_CHIP_ID1, &chip_id1);
    esp_err_t id0_ret = ReadReg(ES8375_CHIP_ID0, &chip_id0);
    esp_err_t sec_ret = ReadReg(ES8375_SECURITY_CODE, &security_code);
    if (id1_ret == ESP_OK && id0_ret == ESP_OK && sec_ret == ESP_OK) {
        ESP_LOGI(TAG, "ES8375 initialized, chip id: %02x %02x security %02x", chip_id1, chip_id0, security_code);
        ESP_LOGI(TAG, "reg[0x%02x]=0x%02x", ES8375_CHIP_ID1, chip_id1);
        ESP_LOGI(TAG, "reg[0x%02x]=0x%02x", ES8375_CHIP_ID0, chip_id0);
    } else {
        ESP_LOGW(TAG, "ES8375 register readback failed after init");
    }
    DumpRegisters();

    if (pa_pin_ != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << pa_pin_,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(pa_pin_, 0);
    }
}

void Es8375AudioCodec::SetCodecPower(bool enable) {
    if (!codec_ready_) {
        return;
    }
    WriteReg(ES8375_PWR_STATE, enable ? 0xA0 : 0x90);
}

void Es8375AudioCodec::SetPlaybackMute(bool mute) {
    if (!codec_ready_) {
        return;
    }
    UpdateReg(ES8375_SDP_CFG, 0x40, mute ? 0x40 : 0x00);
}

void Es8375AudioCodec::SetCaptureMute(bool mute) {
    if (!codec_ready_) {
        return;
    }
    UpdateReg(ES8375_ADC_OUT_SEL, 0x20, mute ? 0x20 : 0x00);
}

void Es8375AudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    if (codec_ready_) {
        uint8_t reg_value = (uint8_t)(volume * 0xBF / 100);
        WriteReg(ES8375_DAC_VOLUME, reg_value);
        SetPlaybackMute(volume == 0);
    }
    AudioCodec::SetOutputVolume(volume);
}

void Es8375AudioCodec::SetInputGain(float gain) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (gain < 0) {
        gain = 0;
    } else if (gain > 30) {
        gain = 30;
    }
    uint8_t gain_step = (uint8_t)(gain / 3.0f);
    if (gain_step > 10) {
        gain_step = 10;
    }
    if (codec_ready_) {
        WriteReg(ES8375_PGA_GAIN, 0x40 | gain_step);
    }
    AudioCodec::SetInputGain(gain);
}

void Es8375AudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    SetCodecPower(enable || output_enabled_);
    SetCaptureMute(!enable);
    AudioCodec::EnableInput(enable);
}

void Es8375AudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    SetCodecPower(input_enabled_ || enable);
    SetPlaybackMute(!enable || output_volume_ == 0);
    if (pa_pin_ != GPIO_NUM_NC && enable != pa_enabled_) {
        gpio_set_level(pa_pin_, enable ? 1 : 0);
        pa_enabled_ = enable;
    }
    AudioCodec::EnableOutput(enable);
}

int Es8375AudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_) {
        return 0;
    }

    std::vector<int16_t> stereo(samples * 2);
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(rx_handle_, stereo.data(), stereo.size() * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(200));
    if (ret != ESP_OK) {
        return 0;
    }

    int stereo_samples = bytes_read / sizeof(int16_t);
    int mono_samples = stereo_samples / 2;
    for (int i = 0; i < mono_samples; ++i) {
        dest[i] = stereo[i * 2];
    }
    return mono_samples;
}

int Es8375AudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_) {
        return samples;
    }

    std::vector<int16_t> stereo(samples * 2);
    for (int i = 0; i < samples; ++i) {
        stereo[i * 2] = data[i];
        stereo[i * 2 + 1] = data[i];
    }

    size_t bytes_written = 0;
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_write(tx_handle_, stereo.data(), stereo.size() * sizeof(int16_t), &bytes_written, portMAX_DELAY));
    return bytes_written / (sizeof(int16_t) * 2);
}
