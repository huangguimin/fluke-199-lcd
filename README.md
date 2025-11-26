# Fluke 199 LCD 信号转换器

这是一个基于 Raspberry Pi Pico 的项目，用于捕获 Fluke 199 万用表 X3501 LCD 的并行显示信号，并将其转换为 SPI LCD（ST7789 或 ST75320）的显示输出。

## 项目概述

本项目实现了一个高性能的 LCD 信号转换器，通过硬件 PIO 和 DMA 实现零 CPU 开销的信号捕获，能够实时显示 Fluke 199 万用表的 LCD 内容。

### 主要特性

- **硬件加速捕获**: 使用 Pico 的 PIO（可编程 I/O）和 DMA 实现全硬件信号捕获
- **双 LCD 支持**: 支持 ST7789（240x240 彩色）和 ST75320（320x240 单色）两种 LCD 驱动
- **三重缓冲**: 实现三重缓冲机制，确保显示流畅无撕裂
- **实时显示**: 低延迟实时显示捕获的 LCD 内容
- **传感器支持**: 集成 ADC 和占空比检测功能
- **帧统计**: 提供详细的帧率和性能统计信息

## 硬件要求

### 必需硬件

- Raspberry Pi Pico 2（或兼容的 Pico 开发板）
- Fluke 199 万用表（或兼容的 X3501 LCD 设备）
- ST7789 或 ST75320 SPI LCD 显示屏
- 连接线材和面包板（用于信号连接）

### 引脚连接

#### X3501 LCD 输入信号（监听模式）

| X3501 LCD 模块 | Pico GPIO | 说明 |
|---------------|-----------|------|
| LCDONOFF (pin 15) | GPIO 1 | LCD 开关信号 |
| FRAME (pin 5) | GPIO 2 | 帧同步信号 |
| LINECLK (pin 7) | GPIO 3 | 行时钟信号 |
| DATACLK0 (pin 14) | GPIO 4 | 数据时钟 |
| LCDAT0 (pin 8) | GPIO 5 | 数据位 0 |
| LCDAT1 (pin 10) | GPIO 6 | 数据位 1 |
| LCDAT2 (pin 11) | GPIO 7 | 数据位 2 |
| LCDAT3 (pin 13) | GPIO 8 | 数据位 3 |

#### ST7789 SPI LCD 输出

| ST7789 LCD | Pico GPIO | 功能 |
|-----------|-----------|------|
| VCC | 3V3 | 电源 (3.3V) |
| GND | GND | 地线 |
| CS | GPIO 17 | 片选信号 |
| DC/RS | GPIO 16 | 数据/命令选择 |
| RST | GPIO 20 | 复位信号 |
| BLK | GPIO 21 | 背光控制（高电平点亮） |
| SCK/CLK | GPIO 18 | SPI 时钟 (20MHz) |
| SDA/MOSI | GPIO 19 | SPI 数据输出 |

#### ST75320 SPI LCD 输出

| ST75320 LCD | Pico GPIO | 功能 |
|------------|-----------|------|
| VCC | 3V3 | 电源 (3.3V) |
| GND | GND | 地线 |
| CS | GPIO 12 | 片选信号 |
| A0/RS | GPIO 10 | 寄存器选择信号 |
| RES | GPIO 11 | 复位信号 |
| MOSI | GPIO 15 | SPI 数据输出 |
| SCK | GPIO 14 | SPI 时钟 |

> 详细的连接指南请参考 [LCD_CONNECTION_GUIDE.md](LCD_CONNECTION_GUIDE.md)

## 软件要求

- Raspberry Pi Pico SDK 2.2.0 或更高版本
- CMake 3.13 或更高版本
- 支持 ARM GCC 的工具链
- Python 3（用于 picotool，可选）

## 编译和构建

### 方法一：使用 VS Code 官方插件（推荐）

这是最简单的方式，适合初学者和日常开发。

#### 1. 安装 VS Code 插件

在 VS Code 中安装官方插件：
- 打开 VS Code
- 进入扩展市场（Ctrl+Shift+X 或 Cmd+Shift+X）
- 搜索并安装 **"Raspberry Pi Pico"** 官方插件（由 Raspberry Pi 发布）

#### 2. 配置项目

插件安装后会自动：
- 下载并配置 Raspberry Pi Pico SDK
- 配置 CMake 和工具链
- 设置项目构建环境

#### 3. 配置 LCD 类型

在 `lcd_config.h` 中配置要使用的 LCD 类型：

```c
// 使用 ST75320 LCD（单色）
#define USE_ST75320_LCD 1

// 或使用 ST7789 LCD（彩色，默认）
// #define USE_ST75320_LCD 1  // 注释掉这行
```

#### 4. 编译和烧录

- **编译**: 按 `F7` 或点击状态栏的构建按钮
- **烧录**: 
  - 按住 Pico 上的 BOOTSEL 按钮
  - 连接 USB 到电脑
  - 在 VS Code 中按 `F5` 或点击状态栏的烧录按钮
  - 插件会自动将 `.uf2` 文件烧录到 Pico

#### 5. 查看串口输出

- 在 VS Code 底部状态栏点击串口监视器图标
- 或使用插件提供的串口终端功能
- 波特率：115200

### 方法二：命令行编译（高级用户）

适合熟悉命令行工具的用户。

#### 1. 安装 Raspberry Pi Pico SDK

确保已安装 Raspberry Pi Pico SDK。如果使用 VS Code 扩展，SDK 会自动配置。

#### 2. 配置 LCD 类型

在 `lcd_config.h` 中配置要使用的 LCD 类型：

```c
// 使用 ST75320 LCD（单色）
#define USE_ST75320_LCD 1

// 或使用 ST7789 LCD（彩色，默认）
// #define USE_ST75320_LCD 1  // 注释掉这行
```

#### 3. 编译项目

```bash
mkdir build
cd build
cmake ..
make
```

#### 4. 烧录到 Pico

将生成的 `lcd_converter.uf2` 文件拖拽到 Pico 的 USB 存储设备中，或使用 picotool：

```bash
picotool load lcd_converter.uf2
picotool reboot
```

## 项目结构

```
fluke-199-lcd/
├── CMakeLists.txt              # CMake 构建配置
├── lcd_converter.c             # 主程序入口
├── lcd_config.h                # LCD 类型配置
├── lcd_capture.pio             # PIO 程序（信号捕获）
├── duty_cycle.pio              # PIO 程序（占空比检测）
├── lcd_framebuffer.c/h         # 帧缓冲管理（三重缓冲）
├── lcd_st75320.c/h             # ST75320 LCD 驱动
├── spi_lcd.c/h                 # ST7789 SPI LCD 驱动
├── frame_stats.c/h             # 帧统计功能
├── sensor.c/h                  # 传感器读取（ADC、占空比）
├── LCD_CONNECTION_GUIDE.md     # 详细连接指南
└── README.md                   # 本文件
```

## 工作原理

### 信号捕获流程

1. **PIO 捕获**: PIO 状态机监听 X3501 LCD 的并行信号（FRAME、LINECLK、DATACLK、DATA0-3）
2. **DMA 传输**: 捕获的数据通过 DMA 直接传输到内存中的帧缓冲区
3. **三重缓冲**: 使用三个缓冲区实现无撕裂显示：
   - 捕获缓冲区：PIO/DMA 正在写入
   - 渲染缓冲区：CPU 正在处理
   - 显示缓冲区：LCD 正在显示
4. **格式转换**: 将 1-bit 单色数据转换为目标 LCD 格式（RGB565 或 1-bit）
5. **SPI 传输**: 通过 SPI 接口将数据发送到目标 LCD

### 性能特性

- **零 CPU 开销捕获**: PIO 和 DMA 完全在硬件层面工作
- **高帧率**: 支持实时显示，帧率取决于 LCD 刷新率
- **低延迟**: 三重缓冲确保最小延迟
- **自动同步**: 自动检测帧同步信号，无需手动校准

## 功能说明

### LCD 驱动支持

#### ST7789（彩色 LCD）
- 分辨率：240x240
- 颜色深度：16-bit RGB565
- SPI 频率：最高 80MHz
- 支持背光 PWM 控制

#### ST75320（单色 LCD）
- 分辨率：320x240
- 颜色深度：1-bit 单色
- 支持硬件镜像（水平/垂直）
- 支持软件旋转（90/180/270 度）
- 可调对比度

### 传感器功能

- **ADC 读取**: 通过 ADC0 读取电压值（0-3.3V）
- **占空比检测**: 通过 PIO 检测 GPIO13 上 20KHz 信号的占空比
- **数据滤波**: 提供滤波后的传感器数据

### 帧统计

项目包含详细的性能统计功能：
- 帧计数
- 转换时间
- 传输时间
- 数据大小
- 平均帧率

## 使用说明

1. **硬件连接**: 按照连接指南连接所有信号线
2. **烧录固件**: 将编译好的 `.uf2` 文件烧录到 Pico
3. **上电启动**: 连接 USB 后，程序会自动开始捕获和显示
4. **查看日志**: 通过 USB 串口（115200 波特率）查看调试信息

## 调试

### USB 串口输出

程序通过 USB CDC 输出调试信息，可以使用以下工具查看：

- VS Code 的串口监视器
- PuTTY（Windows）
- minicom（Linux）
- screen（macOS/Linux）

### 常见问题

1. **无显示输出**
   - 检查 LCD 连接是否正确
   - 确认 LCD 类型配置（`lcd_config.h`）
   - 检查背光控制引脚

2. **显示异常**
   - 检查信号线连接
   - 确认 GPIO 引脚配置
   - 查看串口输出的错误信息

3. **帧率低**
   - 检查 SPI 时钟频率设置
   - 确认 DMA 配置正确
   - 查看帧统计信息

## 技术细节

### PIO 程序

- `lcd_capture.pio`: 实现并行信号捕获的状态机
- `duty_cycle.pio`: 实现占空比检测的状态机

### 内存管理

- 使用三重缓冲机制
- 每个缓冲区大小：240x240x1 bit = 7.2 KB
- 总内存占用：约 22 KB（仅帧缓冲）

### 时序要求

- X3501 LCD 时钟频率：约 1-2 MHz
- ST7789 SPI 频率：最高 80 MHz
- 帧率：取决于源 LCD 刷新率（通常 30-60 FPS）

## 许可证

本项目为开源项目，请参考项目根目录的许可证文件。

## 参考资料

- [Raspberry Pi Pico SDK 文档](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf)
- [ST7789 数据手册](ST7789VW_datasheet.pdf)
- [ST75320 数据手册](ST75320.pdf)
- [Fluke 199 服务手册](192_196_199_smeng0200.pdf)

## 贡献

欢迎提交 Issue 和 Pull Request！

## 作者

本项目由社区开发和维护。

---

**注意**: 本项目仅用于教育和研究目的。使用本设备时请遵守相关法律法规和安全规范。

