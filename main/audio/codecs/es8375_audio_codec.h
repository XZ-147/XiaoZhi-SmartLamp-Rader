#ifndef _ES8375_AUDIO_CODEC_H
#define _ES8375_AUDIO_CODEC_H

#include "audio_codec.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <mutex>

#define ES8375_CODEC_DEFAULT_ADDR (0x18)

class Es8375AudioCodec : public AudioCodec {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_device_ = nullptr;
    gpio_num_t pa_pin_ = GPIO_NUM_NC;
    bool pa_enabled_ = false;
    bool codec_ready_ = false;
    std::mutex data_if_mutex_;

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);
    void InitializeCodec(bool use_mclk);
    void ConfigureClock(bool use_mclk);
    void DumpRegisters();
    void SetCodecPower(bool enable);
    void SetPlaybackMute(bool mute);
    void SetCaptureMute(bool mute);

    bool ProbeCodec(uint8_t es8375_addr);
    esp_err_t WriteReg(uint8_t reg, uint8_t value);
    esp_err_t ReadReg(uint8_t reg, uint8_t* value);
    void UpdateReg(uint8_t reg, uint8_t mask, uint8_t value);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    Es8375AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        gpio_num_t pa_pin, uint8_t es8375_addr = ES8375_CODEC_DEFAULT_ADDR, bool use_mclk = false);
    virtual ~Es8375AudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void SetInputGain(float gain) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif // _ES8375_AUDIO_CODEC_H
