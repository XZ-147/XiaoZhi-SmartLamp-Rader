#include "custom_lcd_display.h"

#include <algorithm>
#include <vector>

#include "esp_log.h"
#include "freertos/task.h"

#define TAG "CustomLcdDisplay"

namespace {

uint16_t SwapRgb565(uint16_t color)
{
    return static_cast<uint16_t>((color << 8) | (color >> 8));
}

} // namespace

CustomLcdDisplay::CustomLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height)
    : panel_io_(panel_io), panel_(panel)
{
    width_ = width;
    height_ = height;
    mutex_ = xSemaphoreCreateMutex();
}

CustomLcdDisplay::~CustomLcdDisplay()
{
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool CustomLcdDisplay::Lock(int timeout_ms)
{
    if (mutex_ == nullptr) {
        return true;
    }
    TickType_t ticks = timeout_ms <= 0 ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(mutex_, ticks) == pdTRUE;
}

void CustomLcdDisplay::Unlock()
{
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void CustomLcdDisplay::SetupUI()
{
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping");
        return;
    }

    Display::SetupUI();
     RunColorTest();
}

void CustomLcdDisplay::FillColor(uint16_t rgb565)
{
    if (panel_ == nullptr || width_ <= 0 || height_ <= 0) {
        ESP_LOGE(TAG, "FillColor failed: invalid panel or size");
        return;
    }

    DisplayLockGuard lock(this);

    constexpr int kChunkLines = 40;
    const int lines_per_chunk = std::min(kChunkLines, height_);
    std::vector<uint16_t> line_buffer(width_ * lines_per_chunk);
    std::fill(line_buffer.begin(), line_buffer.end(), SwapRgb565(rgb565));

    for (int y = 0; y < height_; y += lines_per_chunk) {
        int y_end = std::min(y + lines_per_chunk, height_);
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y_end, line_buffer.data());
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "draw color failed at y=%d: %s", y, esp_err_to_name(err));
            return;
        }
    }
}

void CustomLcdDisplay::RunColorTest()
{
    struct ColorStep {
        const char* name;
        uint16_t rgb565;
    };

    static constexpr ColorStep kColors[] = {
        {"red", 0xF800},
        {"green", 0x07E0},
        {"blue", 0x001F},
        {"white", 0xFFFF},
        {"black", 0x0000},
    };

    ESP_LOGI(TAG, "start ST77912 color test");
    for (const auto& color : kColors) {
        ESP_LOGI(TAG, "fill %s", color.name);
        FillColor(color.rgb565);
        vTaskDelay(pdMS_TO_TICKS(800));
    }
    ESP_LOGI(TAG, "ST77912 color test done");
}

void CustomLcdDisplay::SetStatus(const char* status)
{
    ESP_LOGI(TAG, "SetStatus: %s", status ? status : "");
}

void CustomLcdDisplay::ShowNotification(const char* notification, int duration_ms)
{
    ESP_LOGI(TAG, "ShowNotification(%d ms): %s", duration_ms, notification ? notification : "");
}

void CustomLcdDisplay::SetEmotion(const char* emotion)
{
    ESP_LOGI(TAG, "SetEmotion: %s", emotion ? emotion : "");
}

void CustomLcdDisplay::SetChatMessage(const char* role, const char* content)
{
    ESP_LOGI(TAG, "SetChatMessage[%s]: %s", role ? role : "", content ? content : "");
}

void CustomLcdDisplay::ClearChatMessages()
{
    ESP_LOGI(TAG, "ClearChatMessages");
}

void CustomLcdDisplay::SetPowerSaveMode(bool on)
{
    ESP_LOGI(TAG, "SetPowerSaveMode: %d", on);
    if (panel_ != nullptr) {
        esp_lcd_panel_disp_on_off(panel_, !on);
    }
}
