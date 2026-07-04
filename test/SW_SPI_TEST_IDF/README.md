# SW_SPI_TEST_IDF

ESP-IDF 平台上的 SPI 信号测试与 ST77912 LCD 驱动验证项目，基于 PlatformIO 构建，目标板为 **Freenove ESP32-WROVER**。

## 项目结构

```
├── src/
│   ├── main.c               # 主程序，包含所有测试模式入口
│   ├── spi_test_config.h    # 引脚、时钟、测试模式等全部配置
│   ├── lcd_st77912.c/.h     # ST77912 LCD 驱动程序
│   └── CMakeLists.txt
├── lcd.c / lcd.h             # 参考代码（C8051F340 的 LCD 驱动，非本工程使用）
├── platformio.ini            # PlatformIO 工程配置
├── CMakeLists.txt            # 顶层 CMake（ESP-IDF 标准）
└── sdkconfig.freenove_esp32_wrover
```

## 测试模式

在 [spi_test_config.h](src/spi_test_config.h) 中通过 `SPI_TEST_ACTIVE_MODE` 宏切换，共 4 种模式：

| 模式 | 说明 |
|------|------|
| `SPI_TEST_MODE_SOFTWARE_SIGNAL` | GPIO 模拟 SPI 信号输出，支持仅时钟、固定字节(0xAA/0x55)、模式循环 |
| `SPI_TEST_MODE_HARDWARE_SIGNAL` | ESP-IDF SPI Master 硬件信号测试 |
| `SPI_TEST_MODE_LCD_SOFTWARE` | ST77912 LCD 初始化 + 彩色扫描图案，走软件 SPI |
| `SPI_TEST_MODE_LCD_HARDWARE` | ST77912 LCD 初始化 + 彩色扫描图案，走硬件 SPI |

当前激活模式：`SPI_TEST_MODE_LCD_SOFTWARE`

## 关键引脚配置

| 功能 | GPIO |
|------|------|
| SCLK | 19 |
| MOSI | 18 |
| CS | 12 |
| DC | 4 |
| RESET | 5 |
| BACKLIGHT | -1 (未使用) |
| DEBUG | -1 (未使用) |

## 硬件 SPI 参数

- 主机：`SPI2_HOST` (HSPI)
- 时钟：1 MHz
- 模式：0
- DMA：自动

## 构建与烧录

```bash
# 使用 PlatformIO CLI
pio run          # 编译
pio run -t upload # 烧录
pio device monitor # 串口监视
```

## 依赖

- **框架**: ESP-IDF (via PlatformIO `espressif32` 平台)
- **MCU**: ESP32-WROVER
- **LCD 驱动 IC**: ST77912（Truly HC236 240×240）
