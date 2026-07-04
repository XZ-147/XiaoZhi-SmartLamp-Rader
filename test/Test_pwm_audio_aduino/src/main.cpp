#include <Arduino.h>

extern "C" {
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_err.h"
}

#include <cstring>

constexpr uint32_t SERIAL_BAUD_RATE = 921600;
constexpr int AUDIO_INPUT_SAMPLE_RATE = 16000;
constexpr int AUDIO_OUTPUT_SAMPLE_RATE = 16000;

constexpr gpio_num_t AUDIO_I2S_GPIO_MCLK = GPIO_NUM_3;
constexpr gpio_num_t AUDIO_I2S_GPIO_BCLK = GPIO_NUM_25;
constexpr gpio_num_t AUDIO_I2S_GPIO_WS = GPIO_NUM_33;
constexpr gpio_num_t AUDIO_I2S_GPIO_DIN = GPIO_NUM_35;
constexpr gpio_num_t AUDIO_I2S_GPIO_DOUT = GPIO_NUM_32;

constexpr i2c_port_t AUDIO_CODEC_I2C_NUM = I2C_NUM_0;
constexpr gpio_num_t AUDIO_CODEC_I2C_SCL_PIN = GPIO_NUM_27;
constexpr gpio_num_t AUDIO_CODEC_I2C_SDA_PIN = GPIO_NUM_14;
constexpr gpio_num_t AUDIO_CODEC_PA_PIN = GPIO_NUM_NC;
// ES8375 AD0 is pulled down on this PCB: 0x18.
constexpr uint8_t AUDIO_CODEC_ES8375_ADDR = 0x18;

constexpr i2s_port_t AUDIO_I2S_NUM = I2S_NUM_0;
constexpr bool AUDIO_CODEC_USE_MCLK = true;
constexpr int AUDIO_MCLK_HZ = AUDIO_OUTPUT_SAMPLE_RATE * 256;
constexpr int AUDIO_FRAME_SAMPLES = 160;  // 10 ms at 16 kHz, mono samples.
constexpr int LOOPBACK_GAIN_PERCENT = 1000;
constexpr uint8_t ES8375_PGA_GAIN_STEP = 10;  // 0..10, roughly 3 dB per step.
constexpr uint8_t ES8375_ADC_VOLUME_VALUE = 0xBF;
constexpr uint8_t ES8375_DAC_VOLUME_VALUE = 0xBF;
constexpr uint32_t STARTUP_TEST_TONE_MS = 3000;
constexpr bool ALWAYS_OUTPUT_TEST_TONE = false;  // Fixed 1 kHz speaker tone disabled while testing mic.
constexpr bool ENABLE_LED_PWM = true;
constexpr bool ENABLE_CODEC_REG_DUMP = false;    // Speaker path is verified; keep ADC logs readable.

enum class AudioDataPathMode : uint8_t {
  kToneOnly,
  kMicRawOnly,
  kVendorDriverMicRawOnly,
  kVendorDriverMicRawOnlySpeakerOff,
  kVendorDriverMicRawOnlyWithLoopback,
};

enum class AdcI2sCompareMode : uint8_t {
  kA16I2s,
  kB32SlotCodec24I2s,
  kC16I2s,
  kC16LeftJustified,
};

constexpr AudioDataPathMode AUDIO_DATA_PATH_MODE = AudioDataPathMode::kVendorDriverMicRawOnlyWithLoopback;
// Change this one line to run the requested A/B/C ADC I2S alignment tests.
constexpr AdcI2sCompareMode ADC_I2S_COMPARE_MODE = AdcI2sCompareMode::kA16I2s;
constexpr uint8_t AUDIO_DEBUG_SAMPLE_COUNT = 16;
constexpr uint8_t AUDIO_DEBUG_RX16_SAMPLE_COUNT = 32;
constexpr uint8_t AUDIO_DEBUG_RX32_SAMPLE_COUNT = 16;
constexpr uint8_t AUDIO_DEBUG_INITIAL_PRINTS = 5;
constexpr uint16_t AUDIO_DEBUG_INTERVAL_MS = 2000;
constexpr int32_t MIC_ACTIVITY_PEAK_THRESHOLD = 200;
constexpr uint16_t MIC_ACTIVITY_PRINT_INTERVAL_MS = 120;
constexpr bool ENABLE_STARTUP_MIC_RECORDING = true;
constexpr uint32_t STARTUP_MIC_RECORD_SECONDS = 10;
constexpr uint8_t STARTUP_MIC_RECORD_CHANNELS = 1;
constexpr uint8_t STARTUP_MIC_RECORD_32BIT_SHIFT = 8;

constexpr uint8_t LED_STRIP_PIN = 15;
constexpr uint8_t LED_PWM_CHANNEL = 0;
constexpr uint16_t LED_PWM_FREQUENCY_HZ = 5000;
constexpr uint8_t LED_PWM_RESOLUTION_BITS = 12;
constexpr uint16_t LED_PWM_MAX_DUTY = (1 << LED_PWM_RESOLUTION_BITS) - 1;
constexpr bool LED_PWM_ACTIVE_LOW = false;
constexpr uint16_t LED_DIM_STEP = 32;
constexpr uint16_t LED_DIM_INTERVAL_MS = 16;
constexpr uint8_t I2C_READY_LED_PIN = 21;
constexpr bool I2C_READY_LED_ACTIVE_LOW = false;
constexpr uint16_t I2C_READY_LED_BLINK_INTERVAL_MS = 250;
constexpr uint16_t POWER_RAIL_SETTLE_DELAY_MS = 300;
constexpr uint16_t POST_I2C_SETTLE_DELAY_MS = 120;
constexpr uint16_t POST_I2S_SETTLE_DELAY_MS = 120;
constexpr uint8_t TOUCH_BUTTON_PIN = 13;  // ESP32 touch channel T4.
constexpr uint8_t TOUCH_CALIBRATION_SAMPLES = 24;
constexpr uint8_t TOUCH_THRESHOLD_PERCENT = 70;
constexpr uint16_t TOUCH_LONG_PRESS_MS = 450;

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
#define ES8375_CSM2              0x10
#define ES8375_VMID_CHARGE2      0x11
#define ES8375_VMID_CHARGE3      0x12
#define ES8375_SDP_CFG           0x15
#define ES8375_ADC_OUT_SEL       0x16
#define ES8375_ADC_SRC_GAIN      0x17
#define ES8375_ADC_SYNC_RAMP     0x18
#define ES8375_ADC_OSR_GAIN      0x19
#define ES8375_ADC_VOLUME        0x1A
#define ES8375_ADC_AUTOMUTE      0x1B
#define ES8375_DAC_CTRL          0x1F
#define ES8375_DAC_SYNC_RAMP     0x20
#define ES8375_DAC_VOLUME        0x21
#define ES8375_DAC_SCALE         0x22
#define ES8375_DAC_AUTOMUTE1     0x23
#define ES8375_DAC_AUTOMUTE      0x24
#define ES8375_DAC_CAL           0x25
#define ES8375_OTP_CTRL          0x27
#define ES8375_ANALOG_SPK1       0x28
#define ES8375_ANALOG_SPK2       0x29
#define ES8375_ANALOG_SPK_BIAS   0x2A
#define ES8375_ANALOG_SPK_VOLUME 0x2B
#define ES8375_VMID_SEL          0x2D
#define ES8375_ANA_EN            0x2E
#define ES8375_ANALOG_VSEL2      0x30
#define ES8375_ANALOG_BIAS_DAC   0x31
#define ES8375_ADC_ANA_EN        0x32
#define ES8375_PGA_GAIN          0x37
#define ES8375_ADC2DAC_CLKTRI    0xF8
#define ES8375_FLAGS             0xFA
#define ES8375_CHIP_ID1          0xFD
#define ES8375_CHIP_ID0          0xFE
#define ES8375_SECURITY_CODE     0xFF
#define ES8375_SYS_CTRL2         0xF9

constexpr uint8_t ES8375_SDP_FORMAT_I2S = 0x00;       // Reg 0x15[1:0] = 00: Philips I2S.
constexpr uint8_t ES8375_SDP_FORMAT_LEFT_J = 0x01;    // Reg 0x15[1:0] = 01: left-justified.
constexpr uint8_t ES8375_SDP_WORD_LEN_24BIT = 0x00;   // Reg 0x15[4:2] = 000: 24-bit samples.
constexpr uint8_t ES8375_SDP_WORD_LEN_16BIT = 0x0C;   // Reg 0x15[4:2] = 011: 16-bit samples.
constexpr uint8_t ES8375_SDP_NORMAL_POLARITY = 0x00;  // Reg 0x15 bit5 = 0: normal LRCK polarity.
constexpr uint8_t ES8375_SDP_DAC_MUTE = 0x40;         // Reg 0x15 bit6: mute DAC serial input.
constexpr uint8_t ES8375_SDP_MASK_FORMAT = 0x23;      // Vendor regmap_update_bits mask for DAI format.
constexpr uint8_t ES8375_SDP_MASK_WORD_LEN = 0x1C;    // Reg 0x15[4:2].
constexpr uint8_t ES8375_CLK_SEL_MASK_SCLK_INV = 0x01;
constexpr uint8_t ES8375_RST_MASK_MASTER_MODE = 0x80;

constexpr uint8_t ES8375_DAC_CTRL_UNMUTED = 0x04;       // Reg 0x1F bit7/6/4 clear; bit2 DSM clip default.
constexpr uint8_t ES8375_SPK1_CLASSD_ON = 0xEC;         // Vendor value for reg 0x28.
constexpr uint8_t ES8375_SPK2_DAC_TO_SPK_ON = 0xE3;     // Vendor value for reg 0x29.
constexpr uint8_t ES8375_SPK_BIAS_NORMAL_400K = 0xA8;   // Reg 0x2A default: normal SPK bias, 384/400 kHz.
constexpr uint8_t ES8375_SPK_VOLUME_PLUS_3DB = 0xBF;    // Reg 0x2B default: SPK volume +3 dB.
constexpr uint8_t ES8375_ANALOG_DAC_SPK_ON = 0xF8;      // Reg 0x2E: analog, VREFQ, bias, DACVREF, DAC on.
constexpr uint8_t ES8375_ANALOG_VSEL2_VENDOR = 0x05;    // Vendor analog reference setting.
constexpr uint8_t ES8375_ANALOG_BIAS_DAC_VENDOR = 0xCC; // Vendor bias/DAC/VMID setting.
constexpr uint8_t ES8375_VENDOR_DAC_CAL = 0x2F;
constexpr uint8_t ES8375_VENDOR_ANALOG_EN_PROBE = 0xB8;
constexpr uint8_t ES8375_VENDOR_ADC1_PGA_GAIN_STEP = 0;  // Vendor driver default: 0 dB PGA.

static bool audio_ready = false;
static uint32_t loop_count = 0;
static uint32_t last_stats_ms = 0;
static int32_t peak_since_last_print = 0;
static int32_t left_peak_since_last_print = 0;
static int32_t right_peak_since_last_print = 0;
static int32_t tx_peak_since_last_print = 0;
static uint32_t tone_phase = 0;
static uint32_t audio_started_ms = 0;
static uint32_t last_tx_debug_ms = 0;
static uint32_t last_rx_debug_ms = 0;
static uint32_t last_mic_activity_ms = 0;
static uint32_t tx_debug_print_count = 0;
static uint32_t rx_debug_print_count = 0;
static uint16_t touch_button_threshold = 0;
static uint32_t touch_pressed_since_ms = 0;
static uint32_t last_led_dim_step_ms = 0;
static uint16_t led_brightness = 0;
static int16_t led_dim_direction = 1;
static bool touch_was_pressed = false;
static bool touch_dim_active = false;
static bool touch_dim_finished = false;
static bool i2c_ready_led_blinking = false;
static bool i2c_ready_led_state = false;
static uint32_t last_i2c_ready_led_toggle_ms = 0;

static int16_t rx_stereo[AUDIO_FRAME_SAMPLES * 2];
static int32_t rx_stereo_32[AUDIO_FRAME_SAMPLES * 2];
static int16_t record_mono[AUDIO_FRAME_SAMPLES];
static int16_t tx_stereo[AUDIO_FRAME_SAMPLES * 2];

static bool CheckEsp(esp_err_t err, const char* what) {
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    return true;
  }

  Serial.printf("%s failed: %s\r\n", what, esp_err_to_name(err));
  return false;
}

static void InitLedStripPwm() {
  ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQUENCY_HZ, LED_PWM_RESOLUTION_BITS);
  ledcAttachPin(LED_STRIP_PIN, LED_PWM_CHANNEL);
  ledcWrite(LED_PWM_CHANNEL, LED_PWM_ACTIVE_LOW ? LED_PWM_MAX_DUTY : 0);
  Serial.printf("LED PWM ready: pin=%u channel=%u\r\n", LED_STRIP_PIN, LED_PWM_CHANNEL);
}

static void SetI2cReadyLed(bool on) {
  i2c_ready_led_state = on;
  digitalWrite(I2C_READY_LED_PIN,
               on == I2C_READY_LED_ACTIVE_LOW ? LOW : HIGH);
}

static void InitI2cReadyLed() {
  pinMode(I2C_READY_LED_PIN, OUTPUT);
  i2c_ready_led_blinking = false;
  SetI2cReadyLed(false);
}

static void StartI2cReadyLedBlink() {
  i2c_ready_led_blinking = true;
  last_i2c_ready_led_toggle_ms = millis();
  SetI2cReadyLed(true);
}

static void UpdateI2cReadyLed() {
  if (!i2c_ready_led_blinking) {
    return;
  }

  const uint32_t now = millis();
  if (now - last_i2c_ready_led_toggle_ms >= I2C_READY_LED_BLINK_INTERVAL_MS) {
    last_i2c_ready_led_toggle_ms = now;
    SetI2cReadyLed(!i2c_ready_led_state);
  }
}

static void DelayWithI2cReadyLedBlink(uint32_t delay_ms) {
  const uint32_t start_ms = millis();
  while (millis() - start_ms < delay_ms) {
    UpdateI2cReadyLed();
    delay(5);
  }
}

static void WriteLedBrightness(uint16_t brightness) {
  led_brightness = brightness;
  const uint16_t output_duty = LED_PWM_ACTIVE_LOW ? LED_PWM_MAX_DUTY - led_brightness
                                                 : led_brightness;
  ledcWrite(LED_PWM_CHANNEL, output_duty);
}

static void InitTouchButton() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < TOUCH_CALIBRATION_SAMPLES; ++i) {
    sum += touchRead(TOUCH_BUTTON_PIN);
    delay(8);
  }

  const uint16_t baseline = sum / TOUCH_CALIBRATION_SAMPLES;
  touch_button_threshold = baseline * TOUCH_THRESHOLD_PERCENT / 100;
  Serial.printf("Touch button ready: pin=%u baseline=%u threshold=%u\r\n",
                TOUCH_BUTTON_PIN, baseline, touch_button_threshold);
}

static void StepLedBrightness() {
  if (touch_dim_finished) {
    return;
  }

  int32_t next = static_cast<int32_t>(led_brightness) + led_dim_direction * LED_DIM_STEP;
  if (next >= LED_PWM_MAX_DUTY) {
    next = LED_PWM_MAX_DUTY;
    led_dim_direction = -1;
  } else if (next <= 0) {
    next = 0;
    touch_dim_finished = true;
  }

  WriteLedBrightness(static_cast<uint16_t>(next));
}

static void UpdateTouchButton() {
  if (touch_button_threshold == 0) {
    return;
  }

  const uint16_t value = touchRead(TOUCH_BUTTON_PIN);
  const bool pressed = value < touch_button_threshold;
  const uint32_t now = millis();

  if (pressed && !touch_was_pressed) {
    touch_pressed_since_ms = now;
    last_led_dim_step_ms = now;
    led_dim_direction = 1;
    touch_dim_active = false;
    touch_dim_finished = false;
  } else if (!pressed && touch_was_pressed) {
    touch_pressed_since_ms = 0;
    touch_dim_active = false;
    touch_dim_finished = false;
  }

  if (pressed && !touch_dim_active && now - touch_pressed_since_ms >= TOUCH_LONG_PRESS_MS) {
    touch_dim_active = true;
    last_led_dim_step_ms = now;
  }

  if (pressed && touch_dim_active && now - last_led_dim_step_ms >= LED_DIM_INTERVAL_MS) {
    last_led_dim_step_ms = now;
    StepLedBrightness();
  }

  touch_was_pressed = pressed;
}

static bool WriteCodecReg(uint8_t reg, uint8_t value) {
  const uint8_t data[2] = {reg, value};
  esp_err_t err = i2c_master_write_to_device(
      AUDIO_CODEC_I2C_NUM,
      AUDIO_CODEC_ES8375_ADDR,
      data,
      sizeof(data),
      pdMS_TO_TICKS(1000));
  if (err != ESP_OK) {
    Serial.printf("ES8375 write reg 0x%02X=0x%02X failed: %s\r\n",
                  reg, value, esp_err_to_name(err));
    return false;
  }
  return true;
}

static bool ReadCodecReg(uint8_t reg, uint8_t& value) {
  value = 0;
  esp_err_t err = i2c_master_write_read_device(
      AUDIO_CODEC_I2C_NUM,
      AUDIO_CODEC_ES8375_ADDR,
      &reg,
      1,
      &value,
      1,
      pdMS_TO_TICKS(1000));
  if (err != ESP_OK) {
    Serial.printf("ES8375 read reg 0x%02X failed: %s\r\n", reg, esp_err_to_name(err));
    return false;
  }
  return true;
}

static bool WriteCodecRegAndReadback(uint8_t reg, uint8_t value, const char* tag) {
  if (!WriteCodecReg(reg, value)) {
    return false;
  }

  uint8_t read_value = 0;
  if (!ReadCodecReg(reg, read_value)) {
    return false;
  }

  Serial.printf("%s write/readback reg[0x%02X]=0x%02X expected=0x%02X %s\r\n",
                tag,
                reg,
                read_value,
                value,
                read_value == value ? "OK" : "MISMATCH");
  return read_value == value;
}

static bool CheckAnalogReferenceRegs(const char* tag) {
  uint8_t reg30 = 0;
  uint8_t reg31 = 0;
  const bool ok30 = ReadCodecReg(ES8375_ANALOG_VSEL2, reg30);
  const bool ok31 = ReadCodecReg(ES8375_ANALOG_BIAS_DAC, reg31);
  if (!ok30 || !ok31) {
    return false;
  }

  const bool ok = reg30 == ES8375_ANALOG_VSEL2_VENDOR &&
                  reg31 == ES8375_ANALOG_BIAS_DAC_VENDOR;
  Serial.printf("%s analog ref check: reg[0x30]=0x%02X reg[0x31]=0x%02X %s\r\n",
                tag,
                reg30,
                reg31,
                ok ? "OK" : "CHANGED");
  return ok;
}

static bool InitAnalogReferenceRegs(const char* tag) {
  bool ok = true;
  ok &= WriteCodecRegAndReadback(ES8375_ANALOG_VSEL2,
                                 ES8375_ANALOG_VSEL2_VENDOR,
                                 tag);
  ok &= WriteCodecRegAndReadback(ES8375_ANALOG_BIAS_DAC,
                                 ES8375_ANALOG_BIAS_DAC_VENDOR,
                                 tag);
  return ok;
}

static bool UpdateCodecReg(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t old_value = 0;
  if (!ReadCodecReg(reg, old_value)) {
    return false;
  }

  uint8_t new_value = (old_value & ~mask) | (value & mask);
  if (new_value == old_value) {
    return true;
  }

  return WriteCodecReg(reg, new_value);
}

static void SetPlaybackMute(bool mute) {
  UpdateCodecReg(ES8375_SDP_CFG, 0x40, mute ? 0x40 : 0x00);
}

static void SetCaptureMute(bool mute) {
  UpdateCodecReg(ES8375_ADC_OUT_SEL, 0x20, mute ? 0x20 : 0x00);
}

static int PgaGainDb(uint8_t pga_reg) {
  const uint8_t step = pga_reg & 0x0F;
  if (step <= 10) {
    return step * 3;
  }
  return 0;
}

static void DumpMicFrontendConfig(const char* title) {
  uint8_t adc_src_gain = 0;
  uint8_t adc_out_sel = 0;
  uint8_t adc_ana_en = 0;
  uint8_t pga_gain = 0;
  uint8_t adc_volume = 0;
  const bool ok = ReadCodecReg(ES8375_ADC_SRC_GAIN, adc_src_gain) &&
                  ReadCodecReg(ES8375_ADC_OUT_SEL, adc_out_sel) &&
                  ReadCodecReg(ES8375_ADC_ANA_EN, adc_ana_en) &&
                  ReadCodecReg(ES8375_PGA_GAIN, pga_gain) &&
                  ReadCodecReg(ES8375_ADC_VOLUME, adc_volume);
  if (!ok) {
    return;
  }

  const bool dmic_selected = (adc_src_gain & 0x80) != 0;
  const bool mic_input_selected = (pga_gain & 0x40) != 0;
  const bool extra_6db = (pga_gain & 0x80) != 0;
  const int pga_db = PgaGainDb(pga_gain) + (extra_6db ? 6 : 0);
  const bool adc_muted = (adc_out_sel & 0x20) != 0;

  Serial.printf("%s MIC frontend: reg[0x17]=0x%02X source=%s adc_invert=%u "
                "reg[0x37]=0x%02X route=%s pga=%ddB adc_volume=0x%02X "
                "reg[0x32]=0x%02X adc_muted=%u\r\n",
                title,
                adc_src_gain,
                dmic_selected ? "DMIC" : "AMIC",
                (adc_src_gain >> 6) & 0x01,
                pga_gain,
                mic_input_selected ? "MIC input" : "not MIC input",
                pga_db,
                adc_volume,
                adc_ana_en,
                adc_muted ? 1 : 0);
}

static void DumpCodecRegs(const char* title) {
  static const uint8_t regs[] = {
      ES8375_CLK_EN, ES8375_CLK_SEL, ES8375_PRE_DIV_DLL,
      ES8375_DMIC_DIV_DAC_CLK, ES8375_ADC_CLK_SEL, ES8375_ADCDAC_DSP_CLK,
      ES8375_ADC_OSR, ES8375_DAC_HOLD, ES8375_OSC_CTRL, ES8375_BCLK_DIV,
      ES8375_LRCK_DIV_H, ES8375_LRCK_DIV_L, ES8375_SPK_CLK_DIV,
      ES8375_PWR_STATE, ES8375_CSM2, ES8375_VMID_CHARGE2,
      ES8375_VMID_CHARGE3, ES8375_SDP_CFG, ES8375_ADC_OUT_SEL,
      ES8375_ADC_SRC_GAIN, ES8375_ADC_SYNC_RAMP, ES8375_ADC_OSR_GAIN,
      ES8375_ADC_VOLUME, ES8375_ADC_AUTOMUTE, ES8375_DAC_CTRL,
      ES8375_DAC_SYNC_RAMP, ES8375_DAC_VOLUME, ES8375_DAC_SCALE,
      ES8375_DAC_AUTOMUTE1, ES8375_DAC_AUTOMUTE, ES8375_DAC_CAL,
      ES8375_OTP_CTRL, ES8375_ANALOG_SPK1, ES8375_ANALOG_SPK2,
      ES8375_ANALOG_SPK_BIAS, ES8375_ANALOG_SPK_VOLUME, ES8375_VMID_SEL,
      ES8375_ANA_EN, ES8375_ANALOG_VSEL2, ES8375_ANALOG_BIAS_DAC,
      ES8375_ADC_ANA_EN, ES8375_PGA_GAIN,
      ES8375_ADC2DAC_CLKTRI, ES8375_SYS_CTRL2, ES8375_FLAGS,
  };

  Serial.printf("%s\r\n", title);
  for (uint8_t reg : regs) {
    uint8_t value = 0;
    if (ReadCodecReg(reg, value)) {
      Serial.printf("  reg[0x%02X]=0x%02X\r\n", reg, value);
    }
  }
}

static bool IsMicRawDataPath() {
  return AUDIO_DATA_PATH_MODE == AudioDataPathMode::kMicRawOnly ||
         AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnly ||
         AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnlySpeakerOff ||
         AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnlyWithLoopback;
}

static bool UsesVendorDriverStyleCodecInit() {
  return AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnly ||
         AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnlySpeakerOff ||
         AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnlyWithLoopback;
}

static bool ShouldDisableSpeakerDuringRecording() {
  return AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnlySpeakerOff;
}

static bool IsLoopbackMode() {
  return AUDIO_DATA_PATH_MODE == AudioDataPathMode::kVendorDriverMicRawOnlyWithLoopback;
}

static const char* AudioDataPathModeName() {
  switch (AUDIO_DATA_PATH_MODE) {
    case AudioDataPathMode::kToneOnly:
      return "tone-only TX";
    case AudioDataPathMode::kMicRawOnly:
      return "mic-raw RX, existing test init";
    case AudioDataPathMode::kVendorDriverMicRawOnly:
      return "mic-raw RX, vendor-driver-style init";
    case AudioDataPathMode::kVendorDriverMicRawOnlySpeakerOff:
      return "mic-raw RX, vendor-driver init, speaker OFF during record";
    case AudioDataPathMode::kVendorDriverMicRawOnlyWithLoopback:
      return "mic-raw RX, vendor-driver init, loopback playback after record";
  }
  return "unknown";
}

static const char* AdcI2sCompareModeName() {
  switch (ADC_I2S_COMPARE_MODE) {
    case AdcI2sCompareMode::kA16I2s:
      return "A: ESP32 16bit/16slot, ES8375 I2S S16_LE";
    case AdcI2sCompareMode::kB32SlotCodec24I2s:
      return "B: ESP32 32bit/32slot raw RX, ES8375 I2S S24_LE";
    case AdcI2sCompareMode::kC16I2s:
      return "C1: ESP32 16bit/16slot, ES8375 I2S S16_LE";
    case AdcI2sCompareMode::kC16LeftJustified:
      return "C2: ESP32 16bit/16slot, ES8375 LEFT_J S16_LE";
  }
  return "unknown";
}

static bool AdcI2sCompareUses32BitRx() {
  return ADC_I2S_COMPARE_MODE == AdcI2sCompareMode::kB32SlotCodec24I2s;
}

static i2s_bits_per_sample_t AdcI2sBitsPerSample() {
  if (AdcI2sCompareUses32BitRx()) {
    return I2S_BITS_PER_SAMPLE_32BIT;
  }
  return I2S_BITS_PER_SAMPLE_16BIT;
}

static i2s_bits_per_chan_t AdcI2sSlotBitWidth() {
  if (AdcI2sCompareUses32BitRx()) {
    return I2S_BITS_PER_CHAN_32BIT;
  }
  return I2S_BITS_PER_CHAN_16BIT;
}

static i2s_comm_format_t AdcI2sCommFormat() {
  if (ADC_I2S_COMPARE_MODE == AdcI2sCompareMode::kC16LeftJustified) {
    return I2S_COMM_FORMAT_STAND_MSB;
  }
  return I2S_COMM_FORMAT_STAND_I2S;
}

static uint8_t CodecSdpBaseForAdcI2sCompareMode() {
  uint8_t format = ES8375_SDP_FORMAT_I2S;
  uint8_t word_len = ES8375_SDP_WORD_LEN_16BIT;

  if (ADC_I2S_COMPARE_MODE == AdcI2sCompareMode::kB32SlotCodec24I2s) {
    word_len = ES8375_SDP_WORD_LEN_24BIT;
  } else if (ADC_I2S_COMPARE_MODE == AdcI2sCompareMode::kC16LeftJustified) {
    format = ES8375_SDP_FORMAT_LEFT_J;
  }

  return format | word_len | ES8375_SDP_NORMAL_POLARITY;
}

static void PrintCodecSerialPortRegs(const char* title) {
  uint8_t sdp = 0;
  uint8_t adc_out_sel = 0;
  if (!ReadCodecReg(ES8375_SDP_CFG, sdp) ||
      !ReadCodecReg(ES8375_ADC_OUT_SEL, adc_out_sel)) {
    return;
  }

  Serial.printf("%s ES8375 serial: reg[0x15]=0x%02X reg[0x16]=0x%02X mode=%s\r\n",
                title,
                sdp,
                adc_out_sel,
                AdcI2sCompareModeName());
}

static void ConfigureCodecI2sSerialPort(bool mute_playback) {
  uint8_t sdp = CodecSdpBaseForAdcI2sCompareMode();
  if (mute_playback) {
    sdp |= ES8375_SDP_DAC_MUTE;
  }
  WriteCodecReg(ES8375_SDP_CFG, sdp);
}

static void ConfigureCodecSpeakerPath() {
  // Speaker path for the PCB: ES8375 SPKP/SPKN directly drive the speaker.
  WriteCodecReg(ES8375_FLAGS, 0x00);                       // Clear latched speaker short-circuit flag.
  WriteCodecReg(ES8375_DAC_CTRL, ES8375_DAC_CTRL_UNMUTED); // DAC DSM/DEM unmuted, no RAM clear.
  WriteCodecReg(ES8375_DAC_SYNC_RAMP, 0x00);               // Normal DAC ramp behavior.
  WriteCodecReg(ES8375_DAC_VOLUME, ES8375_DAC_VOLUME_VALUE);
  WriteCodecReg(ES8375_DAC_SCALE, 0x1F);
  WriteCodecReg(ES8375_DAC_AUTOMUTE1, 0x00);               // Disable DAC automute gate.
  WriteCodecReg(ES8375_DAC_AUTOMUTE, 0x00);                // Disable DAC automute threshold/timer gate.
  WriteCodecReg(ES8375_DAC_CAL, 0x28);                     // DAC/speaker analog calibration setting.
  WriteCodecReg(ES8375_ANALOG_SPK1, ES8375_SPK1_CLASSD_ON);
  WriteCodecReg(ES8375_ANALOG_SPK2, ES8375_SPK2_DAC_TO_SPK_ON);
  WriteCodecReg(ES8375_ANALOG_SPK_BIAS, ES8375_SPK_BIAS_NORMAL_400K);
  WriteCodecReg(ES8375_ANALOG_SPK_VOLUME, ES8375_SPK_VOLUME_PLUS_3DB);
  WriteCodecReg(ES8375_VMID_SEL, 0xFE);                    // Reference-driver VMID trim.
  WriteCodecReg(ES8375_ANA_EN, ES8375_ANALOG_DAC_SPK_ON);
  WriteCodecReg(ES8375_ADC_ANA_EN, 0xF0);                  // Keep mic ADC path powered for loopback.
  WriteCodecReg(ES8375_SYS_CTRL2, 0x03);                   // Pad/domain detect path enabled.
}

static void ConfigureCodecClock() {
  WriteCodecReg(ES8375_SUB_RST, 0x00);
  WriteCodecReg(ES8375_CLK_EN, AUDIO_CODEC_USE_MCLK ? 0xFE : 0x7E);
  WriteCodecReg(ES8375_CLK_SEL, AUDIO_CODEC_USE_MCLK ? 0x00 : 0x80);

  if (!AUDIO_CODEC_USE_MCLK && AUDIO_INPUT_SAMPLE_RATE == 16000) {
    WriteCodecReg(ES8375_PRE_DIV_DLL, 0x05);
    WriteCodecReg(ES8375_DMIC_DIV_DAC_CLK, 0x34);
    WriteCodecReg(ES8375_ADC_CLK_SEL, 0xDD);
    WriteCodecReg(ES8375_ADCDAC_DSP_CLK, 0x55);
    WriteCodecReg(ES8375_ADC_OSR, 0x1F);
    WriteCodecReg(ES8375_DAC_HOLD, 0x00);
    WriteCodecReg(ES8375_OSC_CTRL, 0x15);
    WriteCodecReg(ES8375_BCLK_DIV, 0x00);
    WriteCodecReg(ES8375_LRCK_DIV_H, 0x20);
    WriteCodecReg(ES8375_LRCK_DIV_L, 0xFF);
    WriteCodecReg(ES8375_SPK_CLK_DIV, 0x00);
    WriteCodecReg(ES8375_ADC_OSR_GAIN, 0x1F);
  } else {
    // 16 kHz, MCLK = 256 * Fs = 4.096 MHz.
    WriteCodecReg(ES8375_PRE_DIV_DLL, 0x0B);
    WriteCodecReg(ES8375_DMIC_DIV_DAC_CLK, 0x01);
    WriteCodecReg(ES8375_ADC_CLK_SEL, 0x33);
    WriteCodecReg(ES8375_ADCDAC_DSP_CLK, 0x11);
    WriteCodecReg(ES8375_ADC_OSR, 0x1F);
    WriteCodecReg(ES8375_DAC_HOLD, 0x00);
    WriteCodecReg(ES8375_OSC_CTRL, 0x92);
    WriteCodecReg(ES8375_BCLK_DIV, 0x03);
    WriteCodecReg(ES8375_ADC_OSR_GAIN, 0x1F);
  }

  ConfigureCodecI2sSerialPort(true);
}

static void VendorDriverSetDaiFmtForBoard() {
  UpdateCodecReg(ES8375_RST_CTRL, ES8375_RST_MASK_MASTER_MODE, 0x00);       // ES8375 slave.
  UpdateCodecReg(ES8375_CLK_SEL, ES8375_CLK_SEL_MASK_SCLK_INV, 0x00);      // Normal BCLK.
  UpdateCodecReg(ES8375_SDP_CFG, ES8375_SDP_MASK_FORMAT,
                 CodecSdpBaseForAdcI2sCompareMode() & ES8375_SDP_MASK_FORMAT);
}

static void VendorDriverPcmHwParamsForBoard() {
  UpdateCodecReg(ES8375_SDP_CFG, ES8375_SDP_MASK_WORD_LEN,
                 CodecSdpBaseForAdcI2sCompareMode() & ES8375_SDP_MASK_WORD_LEN);
}

static bool VendorDriverSetDaiSysclkForBoard() {
  if (!AUDIO_CODEC_USE_MCLK ||
      AUDIO_INPUT_SAMPLE_RATE != 16000 ||
      AUDIO_MCLK_HZ != AUDIO_INPUT_SAMPLE_RATE * 256) {
    Serial.printf("vendor sysclk warning: only 16kHz MCLK=256*Fs is mapped, current Fs=%d MCLK=%d\r\n",
                  AUDIO_INPUT_SAMPLE_RATE,
                  AUDIO_MCLK_HZ);
    return false;
  }

  // Vendor coeff_div entry: Ratio=256, MCLK=4096000, LRCK=16000.
  WriteCodecReg(ES8375_PRE_DIV_DLL, 0x0B);
  WriteCodecReg(ES8375_DMIC_DIV_DAC_CLK, 0x01);
  WriteCodecReg(ES8375_ADC_CLK_SEL, 0x33);
  WriteCodecReg(ES8375_ADCDAC_DSP_CLK, 0x11);
  WriteCodecReg(ES8375_ADC_OSR, 0x1F);
  WriteCodecReg(ES8375_DAC_HOLD, 0x00);
  WriteCodecReg(ES8375_OSC_CTRL, 0x95);
  WriteCodecReg(ES8375_BCLK_DIV, 0x03);
  WriteCodecReg(ES8375_ADC_OSR_GAIN, 0x1F);

  constexpr uint16_t ratio = AUDIO_MCLK_HZ / AUDIO_INPUT_SAMPLE_RATE;
  WriteCodecReg(ES8375_LRCK_DIV_H, 0x20 | ((ratio - 1) >> 8));
  WriteCodecReg(ES8375_LRCK_DIV_L, (ratio - 1) & 0xFF);
  return true;
}

static bool VendorDriverProbeForBoard() {
  // This mirrors Everest_probe() without touching the vendor driver files.
  WriteCodecReg(ES8375_OSC_CTRL, 0x95);
  WriteCodecReg(ES8375_CLK_SEL, 0x48);
  WriteCodecReg(ES8375_SPK_CLK_DIV, 0x18);
  WriteCodecReg(ES8375_PRE_DIV_DLL, 0x02);
  WriteCodecReg(ES8375_DMIC_DIV_DAC_CLK, 0x05);
  WriteCodecReg(ES8375_PWR_STATE, 0x82);
  WriteCodecReg(ES8375_VMID_CHARGE2, 0x10);
  WriteCodecReg(ES8375_VMID_CHARGE3, 0x10);
  WriteCodecReg(ES8375_DAC_CAL, ES8375_VENDOR_DAC_CAL);
  WriteCodecReg(ES8375_ANALOG_SPK1, ES8375_SPK1_CLASSD_ON);
  WriteCodecReg(ES8375_ANALOG_SPK2, 0xE0);
  WriteCodecReg(ES8375_VMID_SEL, 0xFE);
  WriteCodecReg(ES8375_ANA_EN, ES8375_VENDOR_ANALOG_EN_PROBE);
  WriteCodecReg(ES8375_ANALOG_VSEL2, ES8375_ANALOG_VSEL2_VENDOR);
  WriteCodecReg(ES8375_ANALOG_BIAS_DAC, ES8375_ANALOG_BIAS_DAC_VENDOR);
  WriteCodecReg(ES8375_SYS_CTRL2, 0x03);
  WriteCodecReg(ES8375_CLK_EN, 0x16);
  WriteCodecReg(ES8375_RST_CTRL, 0x00);
  DelayWithI2cReadyLedBlink(80);

  WriteCodecReg(ES8375_CLK_SEL, 0x08);
  WriteCodecReg(ES8375_PWR_STATE, 0x86);
  WriteCodecReg(ES8375_PRE_DIV_DLL, 0x0B);
  WriteCodecReg(ES8375_DMIC_DIV_DAC_CLK, 0x00);
  WriteCodecReg(ES8375_ADC_CLK_SEL, 0x31);
  WriteCodecReg(ES8375_ADCDAC_DSP_CLK, 0x11);
  WriteCodecReg(ES8375_ADC_OSR, 0x1F);
  WriteCodecReg(ES8375_DAC_HOLD, 0x00);
  WriteCodecReg(ES8375_ADC_OSR_GAIN, 0x1F);
  WriteCodecReg(ES8375_ADC_SYNC_RAMP, 0x00);
  WriteCodecReg(ES8375_DAC_SYNC_RAMP, 0x00);
  WriteCodecReg(ES8375_ADC_VOLUME, ES8375_ADC_VOLUME_VALUE);
  WriteCodecReg(ES8375_DAC_VOLUME, ES8375_DAC_VOLUME_VALUE);
  WriteCodecReg(ES8375_OTP_CTRL, 0x88);
  WriteCodecReg(ES8375_ANALOG_SPK2, ES8375_SPK2_DAC_TO_SPK_ON);
  WriteCodecReg(ES8375_ADC_ANA_EN, 0xF0);
  WriteCodecReg(ES8375_PGA_GAIN, 0x40 | ES8375_VENDOR_ADC1_PGA_GAIN_STEP);
  WriteCodecReg(ES8375_CLK_EN, 0xFE);
  WriteCodecReg(ES8375_ADC_SRC_GAIN, 0x00);  // AMIC, no ADC invert, DMIC gain 0.
  WriteCodecReg(ES8375_ADC2DAC_CLKTRI, 0x0F);
  WriteCodecReg(ES8375_DAC_CTRL, ES8375_DAC_CTRL_UNMUTED);
  WriteCodecReg(ES8375_BCLK_DIV, 0x03);

  constexpr uint16_t ratio = AUDIO_MCLK_HZ / AUDIO_INPUT_SAMPLE_RATE;
  WriteCodecReg(ES8375_LRCK_DIV_H, 0x20 | ((ratio - 1) >> 8));
  WriteCodecReg(ES8375_LRCK_DIV_L, (ratio - 1) & 0xFF);
  WriteCodecReg(ES8375_SDP_CFG, CodecSdpBaseForAdcI2sCompareMode());
  return true;
}

static bool InitEs8375VendorDriverStyle() {
  uint8_t chip_id1 = 0;
  uint8_t chip_id0 = 0;
  uint8_t security_code = 0;
  bool id_ok = ReadCodecReg(ES8375_CHIP_ID1, chip_id1) &&
               ReadCodecReg(ES8375_CHIP_ID0, chip_id0) &&
               ReadCodecReg(ES8375_SECURITY_CODE, security_code);
  if (!id_ok) {
    Serial.println("ES8375 not detected on I2C, vendor-driver mic-raw test stopped.");
    return false;
  }

  Serial.printf("ES8375 detected: id=%02X %02X security=%02X\r\n",
                chip_id1, chip_id0, security_code);
  Serial.println("ES8375 vendor-driver-style init: Probe -> Set_dai_sysclk -> Set_dai_fmt -> Pcm_hw_params");

  WriteCodecReg(ES8375_RST_CTRL, 0x3F);
  DelayWithI2cReadyLedBlink(2);
  WriteCodecReg(ES8375_RST_CTRL, 0x00);
  DelayWithI2cReadyLedBlink(20);

  if (!VendorDriverProbeForBoard()) {
    return false;
  }
  if (!VendorDriverSetDaiSysclkForBoard()) {
    return false;
  }
  VendorDriverSetDaiFmtForBoard();
  VendorDriverPcmHwParamsForBoard();
  SetCaptureMute(true);
  SetPlaybackMute(true);

  CheckAnalogReferenceRegs("vendor init analog ref");
  PrintCodecSerialPortRegs("vendor init");
  DumpMicFrontendConfig("vendor init");
  if (ENABLE_CODEC_REG_DUMP) {
    DumpCodecRegs("ES8375 vendor-driver-style registers after init:");
  }
  return true;
}

static bool InitCodecI2c() {
  i2c_config_t conf;
  std::memset(&conf, 0, sizeof(conf));
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 100000;

  if (!CheckEsp(i2c_param_config(AUDIO_CODEC_I2C_NUM, &conf), "i2c_param_config")) {
    return false;
  }

  esp_err_t err = i2c_driver_install(AUDIO_CODEC_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("i2c_driver_install failed: %s\r\n", esp_err_to_name(err));
    return false;
  }

  Serial.printf("I2C ready: SDA=%d SCL=%d addr=0x%02X\r\n",
                AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN, AUDIO_CODEC_ES8375_ADDR);
  StartI2cReadyLedBlink();
  return true;
}

static bool InitEs8375() {
  uint8_t chip_id1 = 0;
  uint8_t chip_id0 = 0;
  uint8_t security_code = 0;
  bool id_ok = ReadCodecReg(ES8375_CHIP_ID1, chip_id1) &&
               ReadCodecReg(ES8375_CHIP_ID0, chip_id0) &&
               ReadCodecReg(ES8375_SECURITY_CODE, security_code);
  if (!id_ok) {
    Serial.println("ES8375 not detected on I2C, audio loopback stopped.");
    return false;
  }

  Serial.printf("ES8375 detected: id=%02X %02X security=%02X\r\n",
                chip_id1, chip_id0, security_code);

  WriteCodecReg(ES8375_RST_CTRL, 0x3F);
  DelayWithI2cReadyLedBlink(2);
  WriteCodecReg(ES8375_RST_CTRL, 0x00);
  DelayWithI2cReadyLedBlink(20);

  // Bring up the internal analog speaker/class-D path before enabling audio clocks.
  WriteCodecReg(ES8375_OSC_CTRL, 0x95);
  WriteCodecReg(ES8375_CLK_SEL, 0x48);
  WriteCodecReg(ES8375_SPK_CLK_DIV, 0x18);
  WriteCodecReg(ES8375_PRE_DIV_DLL, 0x02);
  WriteCodecReg(ES8375_DMIC_DIV_DAC_CLK, 0x05);
  WriteCodecReg(ES8375_PWR_STATE, 0x82);
  WriteCodecReg(ES8375_VMID_CHARGE2, 0x20);
  WriteCodecReg(ES8375_VMID_CHARGE3, 0x20);
  WriteCodecReg(ES8375_DAC_CAL, 0x28);
  WriteCodecReg(ES8375_ANALOG_SPK1, ES8375_SPK1_CLASSD_ON);
  WriteCodecReg(ES8375_ANALOG_SPK2, 0xE0);
  WriteCodecReg(ES8375_VMID_SEL, 0xFE);
  WriteCodecReg(ES8375_ANA_EN, ES8375_ANALOG_DAC_SPK_ON);
  CheckAnalogReferenceRegs("before 0x30/0x31 init");
  InitAnalogReferenceRegs("0x30/0x31 init");
  WriteCodecReg(ES8375_SYS_CTRL2, 0x03);
  WriteCodecReg(ES8375_CLK_EN, 0x16);
  DelayWithI2cReadyLedBlink(80);
  CheckAnalogReferenceRegs("after VMID delay");

  ConfigureCodecClock();
  CheckAnalogReferenceRegs("after ConfigureCodecClock");

  WriteCodecReg(ES8375_PWR_STATE, 0x86);
  WriteCodecReg(ES8375_ADC_SYNC_RAMP, 0x00);
  WriteCodecReg(ES8375_OTP_CTRL, 0x88);
  WriteCodecReg(ES8375_ADC_SRC_GAIN, 0x00);
  WriteCodecReg(ES8375_PGA_GAIN, 0x40 | ES8375_PGA_GAIN_STEP);
  WriteCodecReg(ES8375_ADC_VOLUME, ES8375_ADC_VOLUME_VALUE);
  DumpMicFrontendConfig("after ADC/PGA init");
  ConfigureCodecSpeakerPath();
  CheckAnalogReferenceRegs("after ConfigureCodecSpeakerPath during init");
  WriteCodecReg(ES8375_CLK_EN, AUDIO_CODEC_USE_MCLK ? 0xFE : 0x7E);
  ConfigureCodecI2sSerialPort(true);
  PrintCodecSerialPortRegs("after serial-port init");
  SetCaptureMute(true);
  CheckAnalogReferenceRegs("before init dump");

  if (AUDIO_CODEC_PA_PIN != GPIO_NUM_NC) {
    pinMode(AUDIO_CODEC_PA_PIN, OUTPUT);
    digitalWrite(AUDIO_CODEC_PA_PIN, LOW);
  }

  Serial.println("ES8375 initialized and muted, waiting for I2S clock.");
  if (ENABLE_CODEC_REG_DUMP) {
    DumpCodecRegs("ES8375 registers after init:");
  }
  return true;
}

static bool InitI2s() {
  i2s_config_t config;
  std::memset(&config, 0, sizeof(config));
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = AUDIO_OUTPUT_SAMPLE_RATE;
  config.bits_per_sample = AdcI2sBitsPerSample();
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = AdcI2sCommFormat();
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 6;
  config.dma_buf_len = AUDIO_FRAME_SAMPLES;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = AUDIO_CODEC_USE_MCLK ? AUDIO_MCLK_HZ : 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  config.bits_per_chan = AdcI2sSlotBitWidth();

  esp_err_t err = i2s_driver_install(AUDIO_I2S_NUM, &config, 0, nullptr);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("i2s_driver_install failed: %s\r\n", esp_err_to_name(err));
    return false;
  }

  i2s_pin_config_t pins;
  std::memset(&pins, 0, sizeof(pins));
  pins.mck_io_num = AUDIO_CODEC_USE_MCLK ? AUDIO_I2S_GPIO_MCLK : I2S_PIN_NO_CHANGE;
  pins.bck_io_num = AUDIO_I2S_GPIO_BCLK;
  pins.ws_io_num = AUDIO_I2S_GPIO_WS;
  pins.data_out_num = AUDIO_I2S_GPIO_DOUT;
  pins.data_in_num = AUDIO_I2S_GPIO_DIN;

  if (!CheckEsp(i2s_set_pin(AUDIO_I2S_NUM, &pins), "i2s_set_pin")) {
    return false;
  }
  if (!CheckEsp(i2s_zero_dma_buffer(AUDIO_I2S_NUM), "i2s_zero_dma_buffer")) {
    return false;
  }
  if (!CheckEsp(i2s_start(AUDIO_I2S_NUM), "i2s_start")) {
    return false;
  }

  const int slot_bit_width = static_cast<int>(config.bits_per_chan);
  const int expected_bclk_hz = AUDIO_OUTPUT_SAMPLE_RATE * 2 * slot_bit_width;
  Serial.printf("ADC I2S compare mode: %s\r\n", AdcI2sCompareModeName());
  Serial.printf("I2S ready: MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d fixed_mclk=%d\r\n",
                pins.mck_io_num, pins.bck_io_num, pins.ws_io_num,
                pins.data_in_num, pins.data_out_num, config.fixed_mclk);
  Serial.printf("I2S config: sample_rate=%d bits_per_sample=%d slot_bit_width=%d "
                "channel_format=%d comm_format=0x%02X mclk_hz=%d bclk_hz=%d ws_hz=%d\r\n",
                config.sample_rate,
                static_cast<int>(config.bits_per_sample),
                slot_bit_width,
                static_cast<int>(config.channel_format),
                static_cast<unsigned>(config.communication_format),
                config.fixed_mclk,
                expected_bclk_hz,
                config.sample_rate);
  Serial.printf("I2S route: ESP32 DOUT GPIO%d -> ES8375 DSDIN, "
                "ES8375 ASDOUT -> ESP32 DIN GPIO%d\r\n",
                pins.data_out_num,
                pins.data_in_num);
  return true;
}

static int32_t Abs32(int16_t value) {
  int32_t sample = value;
  return sample < 0 ? -sample : sample;
}

static int64_t Abs64(int32_t value) {
  int64_t sample = value;
  return sample < 0 ? -sample : sample;
}

static int16_t ClipToInt16(int32_t sample) {
  if (sample > 32767) {
    return 32767;
  }
  if (sample < -32768) {
    return -32768;
  }
  return static_cast<int16_t>(sample);
}

static int16_t ApplyGainAndClip(int16_t sample) {
  int32_t scaled = static_cast<int32_t>(sample) * LOOPBACK_GAIN_PERCENT / 100;
  if (scaled > 32767) {
    scaled = 32767;
  } else if (scaled < -32768) {
    scaled = -32768;
  }
  return static_cast<int16_t>(scaled);
}

static int16_t NextToneSample() {
  // 16 kHz sample rate, 1 kHz square-ish sine table: 16 samples per cycle.
  static const int16_t tone_table[16] = {
      0, 4700, 8660, 11300, 12288, 11300, 8660, 4700,
      0, -4700, -8660, -11300, -12288, -11300, -8660, -4700,
  };
  int16_t sample = tone_table[tone_phase & 0x0F];
  ++tone_phase;
  return sample;
}

static int32_t BufferPeak(const int16_t* samples, size_t sample_count) {
  int32_t peak = 0;
  for (size_t i = 0; i < sample_count; ++i) {
    const int32_t value = Abs32(samples[i]);
    if (value > peak) {
      peak = value;
    }
  }
  return peak;
}

static bool ShouldPrintAudioDebug(uint32_t& print_count, uint32_t& last_print_ms) {
  const uint32_t now = millis();
  if (print_count < AUDIO_DEBUG_INITIAL_PRINTS ||
      now - last_print_ms >= AUDIO_DEBUG_INTERVAL_MS) {
    ++print_count;
    last_print_ms = now;
    return true;
  }
  return false;
}

static void PrintSamplePrefix(const char* tag, const int16_t* samples, size_t sample_count,
                              int32_t peak) {
  const size_t count = sample_count < AUDIO_DEBUG_SAMPLE_COUNT
                           ? sample_count
                           : AUDIO_DEBUG_SAMPLE_COUNT;
  Serial.printf("%s peak=%ld first%u:",
                tag,
                static_cast<long>(peak),
                static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    Serial.printf(" %d", samples[i]);
  }
  Serial.println();
}

static void PrintRx16Debug(const int16_t* samples, size_t sample_count, size_t frames,
                           int32_t left_peak, int32_t right_peak, int64_t left_sum,
                           int64_t right_sum, uint32_t same_lr_frames) {
  const int32_t rx_peak = left_peak >= right_peak ? left_peak : right_peak;
  const int32_t left_dc = frames > 0 ? static_cast<int32_t>(left_sum / frames) : 0;
  const int32_t right_dc = frames > 0 ? static_cast<int32_t>(right_sum / frames) : 0;
  const size_t count = sample_count < AUDIO_DEBUG_RX16_SAMPLE_COUNT
                           ? sample_count
                           : AUDIO_DEBUG_RX16_SAMPLE_COUNT;

  Serial.printf("RX16 %s peak L/R/sel=%ld/%ld/%ld dc L/R=%ld/%ld "
                "lr_same=%lu/%lu first%u:",
                AdcI2sCompareModeName(),
                static_cast<long>(left_peak),
                static_cast<long>(right_peak),
                static_cast<long>(rx_peak),
                static_cast<long>(left_dc),
                static_cast<long>(right_dc),
                static_cast<unsigned long>(same_lr_frames),
                static_cast<unsigned long>(frames),
                static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    Serial.printf(" %d", samples[i]);
  }
  Serial.println();

  if (frames > 0 && same_lr_frames == frames) {
    Serial.println("RX16 note: L/R identical; with reg[0x16]=0x00 this can be ES8375 single-ADC data copied to both slots.");
  }
}

static void PrintRx32Debug(const int32_t* samples, size_t sample_count, size_t frames,
                           int64_t raw_peak, int32_t shift8_peak,
                           int32_t shift16_peak, int32_t low16_peak,
                           uint32_t same_lr_frames) {
  const size_t count = sample_count < AUDIO_DEBUG_RX32_SAMPLE_COUNT
                           ? sample_count
                           : AUDIO_DEBUG_RX32_SAMPLE_COUNT;

  Serial.printf("RX32 %s peaks raw/>>8/>>16/low16=%lld/%ld/%ld/%ld "
                "lr_same=%lu/%lu\r\n",
                AdcI2sCompareModeName(),
                static_cast<long long>(raw_peak),
                static_cast<long>(shift8_peak),
                static_cast<long>(shift16_peak),
                static_cast<long>(low16_peak),
                static_cast<unsigned long>(same_lr_frames),
                static_cast<unsigned long>(frames));

  Serial.printf("RX32 first%u raw/>>8/>>16/&FFFF:",
                static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    const int32_t raw = samples[i];
    const int32_t shifted8 = raw >> 8;
    const int32_t shifted16 = raw >> 16;
    const uint16_t low16 = static_cast<uint16_t>(raw & 0xFFFF);
    Serial.printf(" [%u]=%ld/%ld/%ld/0x%04X",
                  static_cast<unsigned>(i),
                  static_cast<long>(raw),
                  static_cast<long>(shifted8),
                  static_cast<long>(shifted16),
                  static_cast<unsigned>(low16));
  }
  Serial.println();

  if (frames > 0 && same_lr_frames == frames) {
    Serial.println("RX32 note: L/R identical; with reg[0x16]=0x00 this can be ES8375 single-ADC data copied to both slots.");
  }
}

static void FillToneTxBuffer(size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    const int16_t out = NextToneSample();
    tx_stereo[i * 2] = out;
    tx_stereo[i * 2 + 1] = out;
  }
}

static void RunToneOnlyTest() {
  const size_t frames = AUDIO_FRAME_SAMPLES;
  const size_t sample_count = frames * 2;
  FillToneTxBuffer(frames);

  const int32_t tx_peak = BufferPeak(tx_stereo, sample_count);
  if (tx_peak > tx_peak_since_last_print) {
    tx_peak_since_last_print = tx_peak;
  }

  if (ShouldPrintAudioDebug(tx_debug_print_count, last_tx_debug_ms)) {
    PrintSamplePrefix("TX before i2s_write tone-only", tx_stereo, sample_count, tx_peak);
    if (tx_peak == 0) {
      Serial.println("TX WARNING: tone buffer is all zero before i2s_write");
    }
  }

  size_t bytes_written = 0;
  const size_t bytes_to_write = sample_count * sizeof(int16_t);
  esp_err_t write_err = i2s_write(
      AUDIO_I2S_NUM,
      tx_stereo,
      bytes_to_write,
      &bytes_written,
      portMAX_DELAY);
  if (write_err != ESP_OK || bytes_written != bytes_to_write) {
    Serial.printf("i2s_write failed: %s written=%u/%u\r\n",
                  esp_err_to_name(write_err),
                  static_cast<unsigned>(bytes_written),
                  static_cast<unsigned>(bytes_to_write));
  }

  ++loop_count;
}

static void RunMicRawOnlyTest16() {
  size_t bytes_read = 0;
  esp_err_t read_err = i2s_read(
      AUDIO_I2S_NUM,
      rx_stereo,
      sizeof(rx_stereo),
      &bytes_read,
      portMAX_DELAY);
  if (read_err != ESP_OK || bytes_read == 0) {
    Serial.printf("i2s_read failed: %s\r\n", esp_err_to_name(read_err));
    delay(20);
    return;
  }

  const size_t sample_count = bytes_read / sizeof(int16_t);
  const size_t frames = sample_count / 2;
  int32_t left_peak = 0;
  int32_t right_peak = 0;
  int64_t left_sum = 0;
  int64_t right_sum = 0;
  uint32_t same_lr_frames = 0;
  for (size_t i = 0; i < frames; ++i) {
    const int16_t left_sample = rx_stereo[i * 2];
    const int16_t right_sample = rx_stereo[i * 2 + 1];
    const int32_t left = Abs32(left_sample);
    const int32_t right = Abs32(right_sample);
    left_sum += left_sample;
    right_sum += right_sample;
    if (left_sample == right_sample) {
      ++same_lr_frames;
    }
    if (left > left_peak) {
      left_peak = left;
    }
    if (right > right_peak) {
      right_peak = right;
    }
  }
  const int32_t rx_peak = left_peak >= right_peak ? left_peak : right_peak;

  if (left_peak > left_peak_since_last_print) {
    left_peak_since_last_print = left_peak;
  }
  if (right_peak > right_peak_since_last_print) {
    right_peak_since_last_print = right_peak;
  }
  if (rx_peak > peak_since_last_print) {
    peak_since_last_print = rx_peak;
  }

  if (ShouldPrintAudioDebug(rx_debug_print_count, last_rx_debug_ms)) {
    PrintRx16Debug(rx_stereo,
                   sample_count,
                   frames,
                   left_peak,
                   right_peak,
                   left_sum,
                   right_sum,
                   same_lr_frames);
  }

  const uint32_t now = millis();
  if (rx_peak >= MIC_ACTIVITY_PEAK_THRESHOLD &&
      now - last_mic_activity_ms >= MIC_ACTIVITY_PRINT_INTERVAL_MS) {
    last_mic_activity_ms = now;
    Serial.printf("MIC activity: rx_peak L/R/sel=%ld/%ld/%ld first L/R=%d/%d\r\n",
                  static_cast<long>(left_peak),
                  static_cast<long>(right_peak),
                  static_cast<long>(rx_peak),
                  frames > 0 ? rx_stereo[0] : 0,
                  frames > 0 ? rx_stereo[1] : 0);
  }

  ++loop_count;
}

static void RunMicRawOnlyTest32() {
  size_t bytes_read = 0;
  esp_err_t read_err = i2s_read(
      AUDIO_I2S_NUM,
      rx_stereo_32,
      sizeof(rx_stereo_32),
      &bytes_read,
      portMAX_DELAY);
  if (read_err != ESP_OK || bytes_read == 0) {
    Serial.printf("i2s_read 32-bit failed: %s\r\n", esp_err_to_name(read_err));
    delay(20);
    return;
  }

  const size_t sample_count = bytes_read / sizeof(int32_t);
  const size_t frames = sample_count / 2;
  int64_t raw_peak = 0;
  int32_t shift8_peak = 0;
  int32_t shift16_peak = 0;
  int32_t low16_peak = 0;
  int32_t left_peak = 0;
  int32_t right_peak = 0;
  uint32_t same_lr_frames = 0;

  for (size_t i = 0; i < sample_count; ++i) {
    const int32_t raw = rx_stereo_32[i];
    const int64_t raw_abs = Abs64(raw);
    const int32_t shifted8 = raw >> 8;
    const int32_t shifted16 = raw >> 16;
    const int16_t low16 = static_cast<int16_t>(raw & 0xFFFF);
    const int32_t shifted8_abs = static_cast<int32_t>(Abs64(shifted8));
    const int32_t shifted16_abs = static_cast<int32_t>(Abs64(shifted16));
    const int32_t low16_abs = Abs32(low16);

    if (raw_abs > raw_peak) {
      raw_peak = raw_abs;
    }
    if (shifted8_abs > shift8_peak) {
      shift8_peak = shifted8_abs;
    }
    if (shifted16_abs > shift16_peak) {
      shift16_peak = shifted16_abs;
    }
    if (low16_abs > low16_peak) {
      low16_peak = low16_abs;
    }
  }

  for (size_t i = 0; i < frames; ++i) {
    const int32_t left_raw = rx_stereo_32[i * 2];
    const int32_t right_raw = rx_stereo_32[i * 2 + 1];
    const int32_t left = static_cast<int32_t>(Abs64(left_raw >> 8));
    const int32_t right = static_cast<int32_t>(Abs64(right_raw >> 8));
    if (left_raw == right_raw) {
      ++same_lr_frames;
    }
    if (left > left_peak) {
      left_peak = left;
    }
    if (right > right_peak) {
      right_peak = right;
    }
  }
  const int32_t rx_peak = left_peak >= right_peak ? left_peak : right_peak;

  if (left_peak > left_peak_since_last_print) {
    left_peak_since_last_print = left_peak;
  }
  if (right_peak > right_peak_since_last_print) {
    right_peak_since_last_print = right_peak;
  }
  if (rx_peak > peak_since_last_print) {
    peak_since_last_print = rx_peak;
  }

  if (ShouldPrintAudioDebug(rx_debug_print_count, last_rx_debug_ms)) {
    PrintRx32Debug(rx_stereo_32,
                   sample_count,
                   frames,
                   raw_peak,
                   shift8_peak,
                   shift16_peak,
                   low16_peak,
                   same_lr_frames);
  }

  const uint32_t now = millis();
  if (rx_peak >= MIC_ACTIVITY_PEAK_THRESHOLD &&
      now - last_mic_activity_ms >= MIC_ACTIVITY_PRINT_INTERVAL_MS) {
    last_mic_activity_ms = now;
    Serial.printf("MIC activity 32-bit: peak >>8 L/R/sel=%ld/%ld/%ld first raw L/R=%ld/%ld\r\n",
                  static_cast<long>(left_peak),
                  static_cast<long>(right_peak),
                  static_cast<long>(rx_peak),
                  frames > 0 ? static_cast<long>(rx_stereo_32[0]) : 0,
                  frames > 0 ? static_cast<long>(rx_stereo_32[1]) : 0);
  }

  ++loop_count;
}

static void RunMicRawOnlyTest() {
  if (AdcI2sCompareUses32BitRx()) {
    RunMicRawOnlyTest32();
  } else {
    RunMicRawOnlyTest16();
  }
}

static void RunLoopbackTest16() {
  size_t bytes_read = 0;
  esp_err_t read_err = i2s_read(
      AUDIO_I2S_NUM,
      rx_stereo,
      sizeof(rx_stereo),
      &bytes_read,
      portMAX_DELAY);
  if (read_err != ESP_OK || bytes_read == 0) {
    Serial.printf("loopback i2s_read failed: %s\r\n", esp_err_to_name(read_err));
    delay(20);
    return;
  }

  const size_t frames = bytes_read / sizeof(int16_t) / 2;
  for (size_t i = 0; i < frames; ++i) {
    const int16_t mic_sample = rx_stereo[i * 2];
    const int16_t out = ApplyGainAndClip(mic_sample);
    tx_stereo[i * 2] = out;
    tx_stereo[i * 2 + 1] = out;
  }

  size_t bytes_written = 0;
  const size_t bytes_to_write = frames * 2 * sizeof(int16_t);
  esp_err_t write_err = i2s_write(
      AUDIO_I2S_NUM,
      tx_stereo,
      bytes_to_write,
      &bytes_written,
      portMAX_DELAY);
  if (write_err != ESP_OK || bytes_written != bytes_to_write) {
    Serial.printf("loopback i2s_write failed: %s written=%u/%u\r\n",
                  esp_err_to_name(write_err),
                  static_cast<unsigned>(bytes_written),
                  static_cast<unsigned>(bytes_to_write));
  }

  const int32_t rx_peak = BufferPeak(rx_stereo, frames * 2);
  if (rx_peak > peak_since_last_print) {
    peak_since_last_print = rx_peak;
  }

  if (ShouldPrintAudioDebug(rx_debug_print_count, last_rx_debug_ms)) {
    Serial.printf("loopback16 frames=%lu rx_peak=%ld\r\n",
                  static_cast<unsigned long>(frames),
                  static_cast<long>(rx_peak));
  }

  ++loop_count;
}

static void RunLoopbackTest32() {
  size_t bytes_read = 0;
  esp_err_t read_err = i2s_read(
      AUDIO_I2S_NUM,
      rx_stereo_32,
      sizeof(rx_stereo_32),
      &bytes_read,
      portMAX_DELAY);
  if (read_err != ESP_OK || bytes_read == 0) {
    Serial.printf("loopback32 i2s_read failed: %s\r\n", esp_err_to_name(read_err));
    delay(20);
    return;
  }

  const size_t frames = bytes_read / sizeof(int32_t) / 2;
  int32_t rx_peak = 0;
  for (size_t i = 0; i < frames; ++i) {
    const int32_t raw = rx_stereo_32[i * 2];
    const int16_t mic_sample = ClipToInt16(raw >> STARTUP_MIC_RECORD_32BIT_SHIFT);
    const int16_t out = ApplyGainAndClip(mic_sample);
    tx_stereo[i * 2] = out;
    tx_stereo[i * 2 + 1] = out;
    const int32_t abs_val = Abs32(mic_sample);
    if (abs_val > rx_peak) {
      rx_peak = abs_val;
    }
  }

  size_t bytes_written = 0;
  const size_t bytes_to_write = frames * 2 * sizeof(int16_t);
  esp_err_t write_err = i2s_write(
      AUDIO_I2S_NUM,
      tx_stereo,
      bytes_to_write,
      &bytes_written,
      portMAX_DELAY);
  if (write_err != ESP_OK || bytes_written != bytes_to_write) {
    Serial.printf("loopback32 i2s_write failed: %s written=%u/%u\r\n",
                  esp_err_to_name(write_err),
                  static_cast<unsigned>(bytes_written),
                  static_cast<unsigned>(bytes_to_write));
  }

  if (rx_peak > peak_since_last_print) {
    peak_since_last_print = rx_peak;
  }

  if (ShouldPrintAudioDebug(rx_debug_print_count, last_rx_debug_ms)) {
    Serial.printf("loopback32 frames=%lu rx_peak=%ld\r\n",
                  static_cast<unsigned long>(frames),
                  static_cast<long>(rx_peak));
  }

  ++loop_count;
}

static void RunLoopbackTest() {
  if (AdcI2sCompareUses32BitRx()) {
    RunLoopbackTest32();
  } else {
    RunLoopbackTest16();
  }
}

static bool ReadMicFramesAsMonoS16(size_t max_frames, size_t& frames_read, int32_t& peak) {
  frames_read = 0;
  peak = 0;

  if (AdcI2sCompareUses32BitRx()) {
    size_t bytes_read = 0;
    esp_err_t read_err = i2s_read(
        AUDIO_I2S_NUM,
        rx_stereo_32,
        sizeof(rx_stereo_32),
        &bytes_read,
        portMAX_DELAY);
    if (read_err != ESP_OK || bytes_read == 0) {
      Serial.printf("record i2s_read 32-bit failed: %s\r\n", esp_err_to_name(read_err));
      return false;
    }

    const size_t sample_count = bytes_read / sizeof(int32_t);
    const size_t available_frames = sample_count / 2;
    frames_read = available_frames < max_frames ? available_frames : max_frames;
    for (size_t i = 0; i < frames_read; ++i) {
      const int32_t mono = rx_stereo_32[i * 2] >> STARTUP_MIC_RECORD_32BIT_SHIFT;
      record_mono[i] = ClipToInt16(mono);
      const int32_t value = Abs32(record_mono[i]);
      if (value > peak) {
        peak = value;
      }
    }
    return true;
  }

  size_t bytes_read = 0;
  esp_err_t read_err = i2s_read(
      AUDIO_I2S_NUM,
      rx_stereo,
      sizeof(rx_stereo),
      &bytes_read,
      portMAX_DELAY);
  if (read_err != ESP_OK || bytes_read == 0) {
    Serial.printf("record i2s_read failed: %s\r\n", esp_err_to_name(read_err));
    return false;
  }

  const size_t sample_count = bytes_read / sizeof(int16_t);
  const size_t available_frames = sample_count / 2;
  frames_read = available_frames < max_frames ? available_frames : max_frames;
  for (size_t i = 0; i < frames_read; ++i) {
    record_mono[i] = rx_stereo[i * 2];
    const int32_t value = Abs32(record_mono[i]);
    if (value > peak) {
      peak = value;
    }
  }
  return true;
}

static void RecordStartupMicToSerial() {
  if (!ENABLE_STARTUP_MIC_RECORDING || !IsMicRawDataPath()) {
    return;
  }

  const uint32_t total_frames = AUDIO_INPUT_SAMPLE_RATE * STARTUP_MIC_RECORD_SECONDS;
  const uint32_t total_bytes = total_frames * sizeof(int16_t) * STARTUP_MIC_RECORD_CHANNELS;
  uint32_t frames_recorded = 0;
  int32_t peak = 0;

  const char* source = UsesVendorDriverStyleCodecInit() 
      ? (IsLoopbackMode() ? "vendor-loopback" : (ShouldDisableSpeakerDuringRecording() ? "vendor-speaker-off" : "vendor")) 
      : "current";

  Serial.printf("MIC_RECORD_BEGIN format=s16le sample_rate=%d channels=%u "
                "seconds=%lu bytes=%lu source=%s adc_mode=\"%s\"\r\n",
                AUDIO_INPUT_SAMPLE_RATE,
                STARTUP_MIC_RECORD_CHANNELS,
                static_cast<unsigned long>(STARTUP_MIC_RECORD_SECONDS),
                static_cast<unsigned long>(total_bytes),
                source,
                AdcI2sCompareModeName());
  Serial.flush();

  while (frames_recorded < total_frames) {
    UpdateI2cReadyLed();

    const size_t frames_left = total_frames - frames_recorded;
    const size_t frames_to_read = frames_left < AUDIO_FRAME_SAMPLES
                                      ? frames_left
                                      : AUDIO_FRAME_SAMPLES;
    size_t frames_read = 0;
    int32_t block_peak = 0;
    if (!ReadMicFramesAsMonoS16(frames_to_read, frames_read, block_peak)) {
      break;
    }
    if (frames_read == 0) {
      continue;
    }
    if (block_peak > peak) {
      peak = block_peak;
    }

    const size_t bytes_to_write = frames_read * sizeof(int16_t);
    Serial.write(reinterpret_cast<const uint8_t*>(record_mono), bytes_to_write);
    frames_recorded += frames_read;
  }
  Serial.flush();

  Serial.printf("\r\nMIC_RECORD_END frames=%lu bytes=%lu peak=%ld expected_frames=%lu\r\n",
                static_cast<unsigned long>(frames_recorded),
                static_cast<unsigned long>(frames_recorded * sizeof(int16_t)),
                static_cast<long>(peak),
                static_cast<unsigned long>(total_frames));
}

static void UnmuteAudioPath() {
  ConfigureCodecSpeakerPath();
  CheckAnalogReferenceRegs("after ConfigureCodecSpeakerPath during unmute");
  WriteCodecReg(ES8375_PWR_STATE, 0xA6);
  SetCaptureMute(false);
  SetPlaybackMute(false);
  DelayWithI2cReadyLedBlink(120);
  PrintCodecSerialPortRegs("after capture/playback unmute");
  DumpMicFrontendConfig("after capture unmute");
  CheckAnalogReferenceRegs("before unmute dump");

  if (AUDIO_CODEC_PA_PIN != GPIO_NUM_NC) {
    digitalWrite(AUDIO_CODEC_PA_PIN, HIGH);
  }

  if (ENABLE_CODEC_REG_DUMP) {
    DumpCodecRegs("ES8375 registers after unmute:");
  }
}

static void UnmuteVendorDriverMicRawPath() {
  // Vendor Everest_mute(false): enter working state while I2S clocks are present.
  WriteCodecReg(ES8375_PWR_STATE, 0xA6);
  SetCaptureMute(false);
  SetPlaybackMute(false);
  DelayWithI2cReadyLedBlink(120);
  PrintCodecSerialPortRegs("vendor unmute");
  DumpMicFrontendConfig("vendor unmute");
  CheckAnalogReferenceRegs("vendor unmute analog ref");

  if (AUDIO_CODEC_PA_PIN != GPIO_NUM_NC) {
    digitalWrite(AUDIO_CODEC_PA_PIN, HIGH);
  }

  if (ENABLE_CODEC_REG_DUMP) {
    DumpCodecRegs("ES8375 vendor-driver-style registers after unmute:");
  }
}

static void UnmuteVendorDriverMicRawPathSpeakerOff() {
  // Same as vendor driver init, but disable speaker path to reduce noise during recording.
  // Disable Class-D amp and speaker bias while keeping DAC and analog reference active.
  WriteCodecReg(ES8375_PWR_STATE, 0xA6);
  SetCaptureMute(false);
  SetPlaybackMute(true);
  
  WriteCodecReg(ES8375_ANALOG_SPK1, 0x00);
  WriteCodecReg(ES8375_ANALOG_SPK2, 0xE0);
  WriteCodecReg(ES8375_ANALOG_SPK_BIAS, ES8375_SPK_BIAS_NORMAL_400K);
  
  DelayWithI2cReadyLedBlink(120);
  PrintCodecSerialPortRegs("vendor unmute speaker-off");
  DumpMicFrontendConfig("vendor unmute speaker-off");
  CheckAnalogReferenceRegs("vendor unmute speaker-off analog ref");

  if (AUDIO_CODEC_PA_PIN != GPIO_NUM_NC) {
    digitalWrite(AUDIO_CODEC_PA_PIN, HIGH);
  }

  if (ENABLE_CODEC_REG_DUMP) {
    DumpCodecRegs("ES8375 vendor-driver-style registers after speaker-off unmute:");
  }
}

static bool InitSelectedCodecPath() {
  if (UsesVendorDriverStyleCodecInit()) {
    return InitEs8375VendorDriverStyle();
  }
  return InitEs8375();
}

static void UnmuteSelectedCodecPath() {
  if (ShouldDisableSpeakerDuringRecording()) {
    UnmuteVendorDriverMicRawPathSpeakerOff();
  } else if (UsesVendorDriverStyleCodecInit()) {
    UnmuteVendorDriverMicRawPath();
  } else {
    UnmuteAudioPath();
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(300);
  Serial.println();
  Serial.println("ES8375 mic-to-speaker loopback test");
  InitI2cReadyLed();
  DelayWithI2cReadyLedBlink(POWER_RAIL_SETTLE_DELAY_MS);
  if (ENABLE_LED_PWM) {
    InitLedStripPwm();
    InitTouchButton();
  }

  const bool i2c_ready = InitCodecI2c();
  if (i2c_ready) {
    DelayWithI2cReadyLedBlink(POST_I2C_SETTLE_DELAY_MS);
  }

  const bool i2s_ready = i2c_ready && InitI2s();
  if (i2s_ready) {
    DelayWithI2cReadyLedBlink(POST_I2S_SETTLE_DELAY_MS);
  }

  audio_ready = i2s_ready && InitSelectedCodecPath();
  if (audio_ready) {
    UnmuteSelectedCodecPath();
    audio_started_ms = millis();
    Serial.printf("Audio test started: %d Hz, gain=%d%%, clock=%s, startup_tone=%lums\r\n",
                  AUDIO_OUTPUT_SAMPLE_RATE,
                  LOOPBACK_GAIN_PERCENT,
                  AUDIO_CODEC_USE_MCLK ? "MCLK" : "BCLK",
                  static_cast<unsigned long>(STARTUP_TEST_TONE_MS));
    Serial.printf("Audio data-path test mode: %s\r\n", AudioDataPathModeName());
    Serial.printf("ADC I2S alignment test: %s\r\n", AdcI2sCompareModeName());
    RecordStartupMicToSerial();
  } else {
    Serial.println("Audio init failed. Check I2C/I2S pins, power, and ES8375 address.");
  }
}

void loop() {
  UpdateI2cReadyLed();

  if (ENABLE_LED_PWM) {
    UpdateTouchButton();
  }

  if (!audio_ready) {
    delay(10);
    return;
  }

  if (AUDIO_DATA_PATH_MODE == AudioDataPathMode::kToneOnly) {
    RunToneOnlyTest();
  } else if (IsLoopbackMode()) {
    RunLoopbackTest();
  } else if (IsMicRawDataPath()) {
    RunMicRawOnlyTest();
  }

  uint32_t now = millis();
  if (now - last_stats_ms >= 2000) {
    if (AUDIO_DATA_PATH_MODE == AudioDataPathMode::kToneOnly) {
      Serial.printf("tone-only alive, buffers=%lu, tx_peak=%ld\r\n",
                    static_cast<unsigned long>(loop_count),
                    static_cast<long>(tx_peak_since_last_print));
    } else if (IsLoopbackMode()) {
      Serial.printf("vendor loopback alive, buffers=%lu, rx_peak=%ld\r\n",
                    static_cast<unsigned long>(loop_count),
                    static_cast<long>(peak_since_last_print));
    } else {
      const char* mode_name = UsesVendorDriverStyleCodecInit() 
          ? (ShouldDisableSpeakerDuringRecording() ? "vendor mic-raw (speaker-off)" : "vendor mic-raw") 
          : "mic-raw";
      Serial.printf("%s alive, buffers=%lu, rx_peak L/R/sel=%ld/%ld/%ld\r\n",
                    mode_name,
                    static_cast<unsigned long>(loop_count),
                    static_cast<long>(left_peak_since_last_print),
                    static_cast<long>(right_peak_since_last_print),
                    static_cast<long>(peak_since_last_print));
    }
    last_stats_ms = now;
    left_peak_since_last_print = 0;
    right_peak_since_last_print = 0;
    peak_since_last_print = 0;
    tx_peak_since_last_print = 0;
  }
}
