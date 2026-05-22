#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_types.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_3
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_25
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_33
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_35
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_32

#define AUDIO_CODEC_I2C_NUM     I2C_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_27
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_14
#define AUDIO_CODEC_PA_PIN      GPIO_NUM_NC
#define AUDIO_CODEC_ES8375_ADDR ES8375_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_5
#define ASR_BUTTON_GPIO         GPIO_NUM_19
#define BUILTIN_LED_GPIO        GPIO_NUM_2

#define ML307_RX_PIN            GPIO_NUM_16
#define ML307_TX_PIN            GPIO_NUM_17

#define DISPLAY_WIDTH           0
#define DISPLAY_HEIGHT          0
#define DISPLAY_MIRROR_X        true
#define DISPLAY_MIRROR_Y        true


// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

#endif // _BOARD_CONFIG_H_
