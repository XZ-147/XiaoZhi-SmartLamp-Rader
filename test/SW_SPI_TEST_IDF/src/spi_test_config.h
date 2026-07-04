#pragma once

/*
 * Standalone SPI and LCD link test configuration.
 *
 * Change these GPIO numbers to match the LCD connector or test header
 * you want to probe. Keep all values as plain GPIO numbers.
 */
#define SPI_TEST_PIN_SCLK       19
#define SPI_TEST_PIN_MOSI       18
#define SPI_TEST_PIN_CS         12
#define SPI_TEST_PIN_DC         4
#define SPI_TEST_PIN_DEBUG      -1

/*
 * Select exactly one active test.
 *
 * SPI_TEST_MODE_SOFTWARE_SIGNAL:
 *   GPIO bit-banged SPI signal test. Also supports clock-only mode.
 *
 * SPI_TEST_MODE_HARDWARE_SIGNAL:
 *   ESP-IDF SPI Master signal test.
 *
 * SPI_TEST_MODE_LCD_SOFTWARE:
 *   ST77912 LCD init and color-cycle test over bit-banged SPI.
 *
 * SPI_TEST_MODE_LCD_HARDWARE:
 *   ST77912 LCD init and color-cycle test over ESP-IDF SPI Master.
 */
#define SPI_TEST_MODE_SOFTWARE_SIGNAL  1
#define SPI_TEST_MODE_HARDWARE_SIGNAL  2
#define SPI_TEST_MODE_LCD_SOFTWARE     3
#define SPI_TEST_MODE_LCD_HARDWARE     4

/* Backward-compatible aliases for the original signal tests. */
#define SPI_TEST_MODE_SOFTWARE         SPI_TEST_MODE_SOFTWARE_SIGNAL
#define SPI_TEST_MODE_HARDWARE         SPI_TEST_MODE_HARDWARE_SIGNAL

#define SPI_TEST_ACTIVE_MODE           SPI_TEST_MODE_LCD_SOFTWARE

/*
 * Software SPI sub-test selection.
 *
 * SPI_TEST_SOFT_CLOCK_ONLY:
 *   A. Continuous SCLK output at SPI_TEST_SOFT_CLOCK_HZ.
 *
 * SPI_TEST_SOFT_SEND_AA:
 *   B. Continuously sends 0xAA.
 *
 * SPI_TEST_SOFT_SEND_55:
 *   C. Continuously sends 0x55.
 *
 * SPI_TEST_SOFT_PATTERN_CYCLE:
 *   D. Cycles through 0xAA, 0x55, 0xF0, 0x0F.
 */
#define SPI_TEST_SOFT_CLOCK_ONLY    1
#define SPI_TEST_SOFT_SEND_AA       2
#define SPI_TEST_SOFT_SEND_55       3
#define SPI_TEST_SOFT_PATTERN_CYCLE 4
#define SPI_TEST_SOFTWARE_MODE      SPI_TEST_SOFT_PATTERN_CYCLE

#define SPI_TEST_SOFT_CLOCK_HZ      50000
#define SPI_TEST_PATTERN_DWELL_MS   2000
#define SPI_TEST_STATUS_PERIOD_MS   1000

/*
 * Hardware SPI configuration.
 *
 * SPI2_HOST maps to HSPI on classic ESP32 targets. For boards that need
 * another host, change this value after including driver/spi_master.h.
 */
#define SPI_TEST_HW_HOST            SPI2_HOST
#define SPI_TEST_HW_CLOCK_HZ        1000000
#define SPI_TEST_HW_MODE            0
#define SPI_TEST_HW_QUEUE_SIZE      1
#define SPI_TEST_HW_DMA_CHAN        SPI_DMA_CH_AUTO

/*
 * DC is kept at this level while sending the fixed test bytes.
 * 1 is a common "data" level for LCD interfaces.
 */
#define SPI_TEST_DC_IDLE_LEVEL      1

/*
 * Optional LCD side-band pins.
 *
 * Set to -1 if the pin is not connected or is controlled elsewhere.
 *
 * GPIO5 is used as LCD reset in this configuration. DEBUG_PIN is disabled.
 */
#define SPI_TEST_PIN_RESET          5
#define SPI_TEST_PIN_BACKLIGHT      -1
#define SPI_TEST_LCD_RESET_ACTIVE_LEVEL    0
#define SPI_TEST_LCD_BACKLIGHT_ON_LEVEL    1

/*
 * ST77912 / Truly HC236 240x240 LCD test settings.
 */
#define SPI_TEST_LCD_WIDTH          240
#define SPI_TEST_LCD_HEIGHT         240
#define SPI_TEST_LCD_X_OFFSET       40
#define SPI_TEST_LCD_Y_OFFSET       80

/*
 * LCD software SPI uses the same bit-bang engine as the signal test, but
 * it has its own default because a 50 kHz full-screen fill is very slow.
 */
#define SPI_TEST_LCD_SOFT_CLOCK_HZ  100000
#define SPI_TEST_LCD_HW_CLOCK_HZ    1000000
#define SPI_TEST_LCD_COLOR_DWELL_MS 2000
#define SPI_TEST_LCD_SCAN_DWELL_MS  4000
#define SPI_TEST_LCD_TRANSFER_CHUNK_SIZE 4096
