#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_st77912.h"
#include "spi_test_config.h"

static const char *TAG = "spi_test";

static const uint8_t k_pattern[] = {0xAA, 0x55, 0xF0, 0x0F};

#define SPI_TEST_MAYBE_UNUSED __attribute__((unused))

typedef enum {
    TRANSPORT_SOFTWARE,
    TRANSPORT_HARDWARE,
} spi_transport_type_t;

typedef struct {
    spi_transport_type_t type;
    spi_device_handle_t spi;
    uint32_t clock_hz;
    uint32_t soft_half_period_us;
    uint8_t *dma_buf;
    size_t dma_buf_size;
    uint64_t transfer_count;
    uint64_t error_count;
    uint64_t byte_count;
    bool cs_asserted;
} spi_transport_t;

typedef struct {
    uint16_t width;
    uint16_t height;
} lcd_scan_size_t;

static const lcd_scan_size_t k_lcd_scan_sizes[] = {
    {240, 240},
    {240, 320},
    {320, 240},
};

static const uint16_t k_lcd_scan_offsets[] = {0, 20, 40, 80};

static const uint8_t k_lcd_scan_madctl[] = {
    0x00, 0x08, 0x60, 0x68, 0xA0, 0xA8, 0xC0, 0xC8,
};

#define LCD_COLOR_RED     0xF800
#define LCD_COLOR_GREEN   0x07E0
#define LCD_COLOR_BLUE    0x001F
#define LCD_COLOR_WHITE   0xFFFF
#define LCD_COLOR_BLACK   0x0000
#define LCD_COLOR_YELLOW  0xFFE0
#define LCD_CORNER_SIZE   40
#define LCD_CENTER_SIZE   60

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t half_period_us_from_hz(uint32_t clock_hz)
{
    if (clock_hz == 0) {
        return 1;
    }

    const uint32_t half_period = 1000000UL / (clock_hz * 2UL);
    return half_period == 0 ? 1 : half_period;
}

static uint32_t soft_half_period_us(void)
{
    return half_period_us_from_hz(SPI_TEST_SOFT_CLOCK_HZ);
}

static void configure_output_pin(int gpio, int level)
{
    if (gpio < 0) {
        return;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)gpio, level));
}

static esp_err_t set_optional_gpio_level(int gpio, int level)
{
    if (gpio < 0) {
        return ESP_OK;
    }

    return gpio_set_level((gpio_num_t)gpio, level);
}

static int get_optional_gpio_level(int gpio)
{
    if (gpio < 0) {
        return -1;
    }

    return gpio_get_level((gpio_num_t)gpio);
}

static void configure_common_gpio(void)
{
    configure_output_pin(SPI_TEST_PIN_SCLK, 1);
    configure_output_pin(SPI_TEST_PIN_MOSI, 0);
    configure_output_pin(SPI_TEST_PIN_CS, 1);
    configure_output_pin(SPI_TEST_PIN_DC, SPI_TEST_DC_IDLE_LEVEL);
    configure_output_pin(SPI_TEST_PIN_DEBUG, 0);
}

static void configure_hardware_sideband_gpio(void)
{
    configure_output_pin(SPI_TEST_PIN_CS, 1);
    configure_output_pin(SPI_TEST_PIN_DC, SPI_TEST_DC_IDLE_LEVEL);
    configure_output_pin(SPI_TEST_PIN_DEBUG, 0);
}

static void log_gpio_state(const char *event, uint8_t send_byte, uint64_t transfer_count,
                           int64_t switch_time_ms)
{
    ESP_LOGI(TAG,
             "%s time_ms=%" PRId64
             " switch_time_ms=%" PRId64
             " send_byte=0x%02X transfer_count=%" PRIu64
             " gpio[SCLK=%d MOSI=%d CS=%d DC=%d DEBUG=%d]",
             event,
             now_ms(),
             switch_time_ms,
             send_byte,
             transfer_count,
             get_optional_gpio_level(SPI_TEST_PIN_SCLK),
             get_optional_gpio_level(SPI_TEST_PIN_MOSI),
             get_optional_gpio_level(SPI_TEST_PIN_CS),
             get_optional_gpio_level(SPI_TEST_PIN_DC),
             get_optional_gpio_level(SPI_TEST_PIN_DEBUG));
}

static esp_err_t software_spi_write_bytes(const uint8_t *data, size_t len,
                                          int dc_level, uint32_t half_us)
{
    if (data == NULL && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_set_level((gpio_num_t)SPI_TEST_PIN_DC, dc_level);
    if (err != ESP_OK) {
        return err;
    }
    err = set_optional_gpio_level(SPI_TEST_PIN_DEBUG, 1);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < len; ++i) {
        const uint8_t value = data[i];
        for (int bit = 7; bit >= 0; --bit) {
            err = gpio_set_level((gpio_num_t)SPI_TEST_PIN_MOSI, (value >> bit) & 0x01);
            if (err != ESP_OK) {
                break;
            }
            err = gpio_set_level((gpio_num_t)SPI_TEST_PIN_SCLK, 0);
            if (err != ESP_OK) {
                break;
            }
            esp_rom_delay_us(half_us);
            err = gpio_set_level((gpio_num_t)SPI_TEST_PIN_SCLK, 1);
            if (err != ESP_OK) {
                break;
            }
            esp_rom_delay_us(half_us);
        }

        if (err != ESP_OK) {
            break;
        }
    }

    esp_err_t cleanup_err = set_optional_gpio_level(SPI_TEST_PIN_DEBUG, 0);
    if (err == ESP_OK) {
        err = cleanup_err;
    }

    return err;
}

static void transport_record(spi_transport_t *transport, esp_err_t err, size_t len)
{
    ++transport->transfer_count;
    if (err == ESP_OK) {
        transport->byte_count += len;
    } else {
        ++transport->error_count;
    }
}

static esp_err_t transport_assert_cs_once(spi_transport_t *transport)
{
    if (transport->cs_asserted) {
        return ESP_OK;
    }

    esp_err_t err = gpio_set_level((gpio_num_t)SPI_TEST_PIN_CS, 0);
    if (err == ESP_OK) {
        transport->cs_asserted = true;
    }

    return err;
}

static esp_err_t lcd_transport_write(void *user_ctx, bool is_data,
                                     const uint8_t *data, size_t len)
{
    spi_transport_t *transport = (spi_transport_t *)user_ctx;
    if (transport == NULL || (data == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len == 0) {
        return ESP_OK;
    }

    const int dc_level = is_data ? 1 : 0;
    esp_err_t err = transport_assert_cs_once(transport);
    if (err != ESP_OK) {
        transport_record(transport, err, len);
        return err;
    }

    if (transport->type == TRANSPORT_SOFTWARE) {
        err = software_spi_write_bytes(data, len, dc_level,
                                       transport->soft_half_period_us);
        transport_record(transport, err, len);
        return err;
    }

    err = gpio_set_level((gpio_num_t)SPI_TEST_PIN_DC, dc_level);
    if (err != ESP_OK) {
        transport_record(transport, err, len);
        return err;
    }

    size_t offset = 0;
    while (offset < len) {
        size_t chunk_len = len - offset;
        if (chunk_len > transport->dma_buf_size) {
            chunk_len = transport->dma_buf_size;
        }

        spi_transaction_t trans = {0};
        trans.length = chunk_len * 8;

        if (chunk_len <= sizeof(trans.tx_data)) {
            trans.flags = SPI_TRANS_USE_TXDATA;
            memcpy(trans.tx_data, data + offset, chunk_len);
        } else {
            if (transport->dma_buf == NULL || transport->dma_buf_size == 0) {
                err = ESP_ERR_NO_MEM;
                transport_record(transport, err, chunk_len);
                return err;
            }
            memcpy(transport->dma_buf, data + offset, chunk_len);
            trans.tx_buffer = transport->dma_buf;
        }

        err = set_optional_gpio_level(SPI_TEST_PIN_DEBUG, 1);
        if (err != ESP_OK) {
            transport_record(transport, err, chunk_len);
            return err;
        }

        err = spi_device_transmit(transport->spi, &trans);

        esp_err_t cleanup_err = set_optional_gpio_level(SPI_TEST_PIN_DEBUG, 0);
        if (err == ESP_OK) {
            err = cleanup_err;
        }

        transport_record(transport, err, chunk_len);
        if (err != ESP_OK) {
            return err;
        }

        offset += chunk_len;
    }

    return ESP_OK;
}

static void software_spi_send_byte(uint8_t data)
{
    ESP_ERROR_CHECK(software_spi_write_bytes(&data, 1, SPI_TEST_DC_IDLE_LEVEL,
                                             soft_half_period_us()));
}

static void SPI_TEST_MAYBE_UNUSED run_software_clock_only(void)
{
    const uint32_t half_us = soft_half_period_us();
    int64_t last_report_us = esp_timer_get_time();
    uint64_t cycle_count = 0;

    configure_common_gpio();

    ESP_LOGI(TAG,
             "software SPI clock-only test start: sclk_gpio=%d frequency_hz=%d half_period_us=%" PRIu32,
             SPI_TEST_PIN_SCLK,
             SPI_TEST_SOFT_CLOCK_HZ,
             half_us);

    while (true) {
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)SPI_TEST_PIN_SCLK, 0));
        esp_rom_delay_us(half_us);
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)SPI_TEST_PIN_SCLK, 1));
        esp_rom_delay_us(half_us);
        ++cycle_count;

        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_report_us >= SPI_TEST_STATUS_PERIOD_MS * 1000LL) {
            ESP_LOGI(TAG,
                     "software clock time_ms=%" PRId64
                     " clock_hz=%d cycle_count=%" PRIu64
                     " gpio[SCLK=%d MOSI=%d CS=%d DC=%d DEBUG=%d]",
                     now_ms(),
                     SPI_TEST_SOFT_CLOCK_HZ,
                     cycle_count,
                     get_optional_gpio_level(SPI_TEST_PIN_SCLK),
                     get_optional_gpio_level(SPI_TEST_PIN_MOSI),
                     get_optional_gpio_level(SPI_TEST_PIN_CS),
                     get_optional_gpio_level(SPI_TEST_PIN_DC),
                     get_optional_gpio_level(SPI_TEST_PIN_DEBUG));
            last_report_us = now_us;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void SPI_TEST_MAYBE_UNUSED run_software_fixed_byte(uint8_t data)
{
    int64_t last_report_us = esp_timer_get_time();
    int64_t last_switch_ms = now_ms();
    uint64_t transfer_count = 0;

    configure_common_gpio();

    ESP_LOGI(TAG,
             "software SPI fixed-byte test start: send_byte=0x%02X frequency_hz=%d",
             data,
             SPI_TEST_SOFT_CLOCK_HZ);
    log_gpio_state("software fixed", data, transfer_count, last_switch_ms);

    while (true) {
        software_spi_send_byte(data);
        ++transfer_count;

        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_report_us >= SPI_TEST_STATUS_PERIOD_MS * 1000LL) {
            log_gpio_state("software fixed", data, transfer_count, last_switch_ms);
            last_report_us = now_us;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void SPI_TEST_MAYBE_UNUSED run_software_pattern_cycle(void)
{
    size_t pattern_index = 0;
    int64_t last_report_us = esp_timer_get_time();
    int64_t next_switch_us = last_report_us + SPI_TEST_PATTERN_DWELL_MS * 1000LL;
    int64_t last_switch_ms = now_ms();
    uint64_t transfer_count = 0;

    configure_common_gpio();

    ESP_LOGI(TAG,
             "software SPI pattern-cycle test start: frequency_hz=%d dwell_ms=%d",
             SPI_TEST_SOFT_CLOCK_HZ,
             SPI_TEST_PATTERN_DWELL_MS);
    log_gpio_state("software cycle", k_pattern[pattern_index], transfer_count, last_switch_ms);

    while (true) {
        const uint8_t data = k_pattern[pattern_index];
        software_spi_send_byte(data);
        ++transfer_count;

        const int64_t current_us = esp_timer_get_time();
        if (current_us >= next_switch_us) {
            pattern_index = (pattern_index + 1) % (sizeof(k_pattern) / sizeof(k_pattern[0]));
            last_switch_ms = current_us / 1000;
            next_switch_us = current_us + SPI_TEST_PATTERN_DWELL_MS * 1000LL;
            log_gpio_state("software switch", k_pattern[pattern_index], transfer_count, last_switch_ms);
        }

        if (current_us - last_report_us >= SPI_TEST_STATUS_PERIOD_MS * 1000LL) {
            log_gpio_state("software cycle", k_pattern[pattern_index], transfer_count, last_switch_ms);
            last_report_us = current_us;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void SPI_TEST_MAYBE_UNUSED run_software_spi_test(void)
{
#if SPI_TEST_SOFTWARE_MODE == SPI_TEST_SOFT_CLOCK_ONLY
    run_software_clock_only();
#elif SPI_TEST_SOFTWARE_MODE == SPI_TEST_SOFT_SEND_AA
    run_software_fixed_byte(0xAA);
#elif SPI_TEST_SOFTWARE_MODE == SPI_TEST_SOFT_SEND_55
    run_software_fixed_byte(0x55);
#elif SPI_TEST_SOFTWARE_MODE == SPI_TEST_SOFT_PATTERN_CYCLE
    run_software_pattern_cycle();
#else
#error "Unsupported SPI_TEST_SOFTWARE_MODE"
#endif
}

static void init_hardware_spi_bus(spi_device_handle_t *spi, uint32_t clock_hz,
                                  int max_transfer_sz)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_TEST_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = SPI_TEST_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = max_transfer_sz,
    };

    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = SPI_TEST_HW_MODE,
        .clock_speed_hz = clock_hz,
        .spics_io_num = -1,
        .queue_size = SPI_TEST_HW_QUEUE_SIZE,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI_TEST_HW_HOST, &buscfg, SPI_TEST_HW_DMA_CHAN));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_TEST_HW_HOST, &devcfg, spi));
}

static void SPI_TEST_MAYBE_UNUSED run_hardware_spi_test(void)
{
    spi_device_handle_t spi = NULL;
    uint64_t transfer_count = 0;
    uint64_t transfer_count_last_report = 0;
    uint64_t error_count = 0;
    int64_t last_report_us = esp_timer_get_time();

    configure_hardware_sideband_gpio();
    init_hardware_spi_bus(&spi, SPI_TEST_HW_CLOCK_HZ, sizeof(k_pattern));

    ESP_LOGI(TAG,
             "hardware SPI test start: host=%d frequency_hz=%d sclk=%d mosi=%d cs=%d dc=%d debug=%d",
             SPI_TEST_HW_HOST,
             SPI_TEST_HW_CLOCK_HZ,
             SPI_TEST_PIN_SCLK,
             SPI_TEST_PIN_MOSI,
             SPI_TEST_PIN_CS,
             SPI_TEST_PIN_DC,
             SPI_TEST_PIN_DEBUG);

    while (true) {
        spi_transaction_t trans = {0};
        trans.length = sizeof(k_pattern) * 8;
        trans.flags = SPI_TRANS_USE_TXDATA;
        memcpy(trans.tx_data, k_pattern, sizeof(k_pattern));

        ESP_ERROR_CHECK(set_optional_gpio_level(SPI_TEST_PIN_DEBUG, 1));
        esp_err_t err = spi_device_transmit(spi, &trans);
        ESP_ERROR_CHECK(set_optional_gpio_level(SPI_TEST_PIN_DEBUG, 0));

        if (err == ESP_OK) {
            ++transfer_count;
        } else {
            ++error_count;
        }

        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_report_us >= SPI_TEST_STATUS_PERIOD_MS * 1000LL) {
            const uint64_t transfer_delta = transfer_count - transfer_count_last_report;
            ESP_LOGI(TAG,
                     "SPI frequency=%d transfer count=%" PRIu64
                     " transfer count last second=%" PRIu64
                     " error count=%" PRIu64,
                     SPI_TEST_HW_CLOCK_HZ,
                     transfer_count,
                     transfer_delta,
                     error_count);
            transfer_count_last_report = transfer_count;
            last_report_us = now_us;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void init_lcd_transport(spi_transport_t *transport, spi_transport_type_t type)
{
    memset(transport, 0, sizeof(*transport));
    transport->type = type;

    if (type == TRANSPORT_SOFTWARE) {
        configure_common_gpio();
        transport->clock_hz = SPI_TEST_LCD_SOFT_CLOCK_HZ;
        transport->soft_half_period_us = half_period_us_from_hz(SPI_TEST_LCD_SOFT_CLOCK_HZ);
        ESP_LOGI(TAG,
                 "LCD software SPI transport: frequency_hz=%" PRIu32 " half_period_us=%" PRIu32,
                 transport->clock_hz,
                 transport->soft_half_period_us);
        return;
    }

    configure_hardware_sideband_gpio();
    transport->clock_hz = SPI_TEST_LCD_HW_CLOCK_HZ;
    transport->dma_buf_size = SPI_TEST_LCD_TRANSFER_CHUNK_SIZE;
    transport->dma_buf = heap_caps_malloc(transport->dma_buf_size, MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(transport->dma_buf == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    init_hardware_spi_bus(&transport->spi, SPI_TEST_LCD_HW_CLOCK_HZ,
                          SPI_TEST_LCD_TRANSFER_CHUNK_SIZE);
    ESP_LOGI(TAG,
             "LCD hardware SPI transport: frequency_hz=%" PRIu32 " chunk_size=%u",
             transport->clock_hz,
             SPI_TEST_LCD_TRANSFER_CHUNK_SIZE);
}

static uint16_t min_u16(uint16_t a, uint16_t b)
{
    return a < b ? a : b;
}

static esp_err_t fill_rect_clipped(const st77912_t *lcd,
                                   uint16_t x, uint16_t y,
                                   uint16_t width, uint16_t height,
                                   uint16_t rgb565,
                                   uint8_t *work_buf,
                                   size_t work_buf_size)
{
    if (x >= lcd->width || y >= lcd->height || width == 0 || height == 0) {
        return ESP_OK;
    }

    const uint16_t clipped_width = min_u16(width, lcd->width - x);
    const uint16_t clipped_height = min_u16(height, lcd->height - y);

    return st77912_fill_rect(lcd, x, y, clipped_width, clipped_height,
                             rgb565, work_buf, work_buf_size);
}

static esp_err_t draw_lcd_scan_pattern(const st77912_t *lcd,
                                       uint8_t *work_buf,
                                       size_t work_buf_size)
{
    esp_err_t err = st77912_fill_color(lcd, LCD_COLOR_RED, work_buf, work_buf_size);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t corner = min_u16(LCD_CORNER_SIZE, min_u16(lcd->width, lcd->height));
    const uint16_t right_x = lcd->width > corner ? lcd->width - corner : 0;
    const uint16_t bottom_y = lcd->height > corner ? lcd->height - corner : 0;

    err = fill_rect_clipped(lcd, 0, 0, corner, corner,
                            LCD_COLOR_GREEN, work_buf, work_buf_size);
    if (err != ESP_OK) {
        return err;
    }

    err = fill_rect_clipped(lcd, right_x, 0, corner, corner,
                            LCD_COLOR_BLUE, work_buf, work_buf_size);
    if (err != ESP_OK) {
        return err;
    }

    err = fill_rect_clipped(lcd, 0, bottom_y, corner, corner,
                            LCD_COLOR_WHITE, work_buf, work_buf_size);
    if (err != ESP_OK) {
        return err;
    }

    err = fill_rect_clipped(lcd, right_x, bottom_y, corner, corner,
                            LCD_COLOR_BLACK, work_buf, work_buf_size);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t center_size = min_u16(LCD_CENTER_SIZE, min_u16(lcd->width, lcd->height));
    const uint16_t center_x = lcd->width > center_size ? (lcd->width - center_size) / 2 : 0;
    const uint16_t center_y = lcd->height > center_size ? (lcd->height - center_size) / 2 : 0;

    return fill_rect_clipped(lcd, center_x, center_y, center_size, center_size,
                             LCD_COLOR_YELLOW, work_buf, work_buf_size);
}

static void run_lcd_parameter_scan(const st77912_t *base_lcd,
                                   spi_transport_t *transport,
                                   uint8_t *work_buf,
                                   size_t work_buf_size)
{
    uint32_t pass = 0;

    while (true) {
        uint32_t case_no = 0;
        ESP_LOGI(TAG, "LCD scan pass=%" PRIu32 " begin", pass);

        for (size_t size_i = 0; size_i < sizeof(k_lcd_scan_sizes) / sizeof(k_lcd_scan_sizes[0]); ++size_i) {
            for (size_t x_i = 0; x_i < sizeof(k_lcd_scan_offsets) / sizeof(k_lcd_scan_offsets[0]); ++x_i) {
                for (size_t y_i = 0; y_i < sizeof(k_lcd_scan_offsets) / sizeof(k_lcd_scan_offsets[0]); ++y_i) {
                    for (size_t mad_i = 0; mad_i < sizeof(k_lcd_scan_madctl) / sizeof(k_lcd_scan_madctl[0]); ++mad_i) {
                        st77912_t lcd = *base_lcd;
                        lcd.width = k_lcd_scan_sizes[size_i].width;
                        lcd.height = k_lcd_scan_sizes[size_i].height;
                        lcd.x_offset = k_lcd_scan_offsets[x_i];
                        lcd.y_offset = k_lcd_scan_offsets[y_i];
                        const uint8_t madctl = k_lcd_scan_madctl[mad_i];

                        ESP_LOGI(TAG,
                                 "CASE %03" PRIu32 ": w=%u h=%u xoff=%u yoff=%u madctl=0x%02X",
                                 case_no,
                                 (unsigned)lcd.width,
                                 (unsigned)lcd.height,
                                 (unsigned)lcd.x_offset,
                                 (unsigned)lcd.y_offset,
                                 madctl);

                        const int64_t start_ms = now_ms();
                        esp_err_t err = st77912_set_madctl(&lcd, madctl);
                        if (err == ESP_OK) {
                            err = draw_lcd_scan_pattern(&lcd, work_buf, work_buf_size);
                        }
                        const int64_t elapsed_ms = now_ms() - start_ms;

                        if (err != ESP_OK) {
                            ++transport->error_count;
                            ESP_LOGE(TAG,
                                     "CASE %03" PRIu32 " failed: err=0x%x elapsed_ms=%" PRId64,
                                     case_no,
                                     err,
                                     elapsed_ms);
                        } else {
                            ESP_LOGI(TAG,
                                     "CASE %03" PRIu32
                                     " drawn: elapsed_ms=%" PRId64
                                     " SPI frequency=%" PRIu32
                                     " transfer count=%" PRIu64
                                     " byte count=%" PRIu64
                                     " error count=%" PRIu64,
                                     case_no,
                                     elapsed_ms,
                                     transport->clock_hz,
                                     transport->transfer_count,
                                     transport->byte_count,
                                     transport->error_count);
                        }

                        ++case_no;
                        vTaskDelay(pdMS_TO_TICKS(SPI_TEST_LCD_SCAN_DWELL_MS));
                    }
                }
            }
        }

        ++pass;
    }
}

static void SPI_TEST_MAYBE_UNUSED run_lcd_test(spi_transport_type_t type)
{
    spi_transport_t transport;
    init_lcd_transport(&transport, type);

    st77912_t lcd = {
        .write = lcd_transport_write,
        .user_ctx = &transport,
        .reset_gpio = SPI_TEST_PIN_RESET,
        .reset_active_level = SPI_TEST_LCD_RESET_ACTIVE_LEVEL,
        .backlight_gpio = SPI_TEST_PIN_BACKLIGHT,
        .backlight_on_level = SPI_TEST_LCD_BACKLIGHT_ON_LEVEL,
        .width = SPI_TEST_LCD_WIDTH,
        .height = SPI_TEST_LCD_HEIGHT,
        .x_offset = SPI_TEST_LCD_X_OFFSET,
        .y_offset = SPI_TEST_LCD_Y_OFFSET,
    };

    uint8_t *work_buf = heap_caps_malloc(SPI_TEST_LCD_TRANSFER_CHUNK_SIZE,
                                         MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(work_buf == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    ESP_LOGI(TAG,
             "ST77912 init start: init_cmd_count=%u reset=%d backlight=%d",
             (unsigned)st77912_init_cmd_count(),
             SPI_TEST_PIN_RESET,
             SPI_TEST_PIN_BACKLIGHT);
    ESP_ERROR_CHECK(st77912_init(&lcd));

    ESP_LOGI(TAG,
             "ST77912 init done: transfer_count=%" PRIu64
             " byte_count=%" PRIu64 " error_count=%" PRIu64,
             transport.transfer_count,
             transport.byte_count,
             transport.error_count);

    run_lcd_parameter_scan(&lcd, &transport, work_buf, SPI_TEST_LCD_TRANSFER_CHUNK_SIZE);
}

void app_main(void)
{
    ESP_LOGI(TAG, "standalone SPI/LCD signal test boot");

#if SPI_TEST_ACTIVE_MODE == SPI_TEST_MODE_SOFTWARE_SIGNAL
    run_software_spi_test();
#elif SPI_TEST_ACTIVE_MODE == SPI_TEST_MODE_HARDWARE_SIGNAL
    run_hardware_spi_test();
#elif SPI_TEST_ACTIVE_MODE == SPI_TEST_MODE_LCD_SOFTWARE
    run_lcd_test(TRANSPORT_SOFTWARE);
#elif SPI_TEST_ACTIVE_MODE == SPI_TEST_MODE_LCD_HARDWARE
    run_lcd_test(TRANSPORT_HARDWARE);
#else
#error "Unsupported SPI_TEST_ACTIVE_MODE"
#endif
}
