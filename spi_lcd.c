#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "spi_lcd.h"
#include "lcd_framebuffer.h"
#include "frame_stats.h"

// Default pin assignments (can be overridden)
static uint lcd_spi_port = 0; // SPI0 or SPI1
static uint lcd_pin_cs = 5;   // Chip Select
static uint lcd_pin_dc = 4;   // Data/Command
static uint lcd_pin_rst = 3;  // Reset
static uint lcd_pin_sck = 2;  // Clock
static uint lcd_pin_mosi = 1; // Data
static uint lcd_pin_blk = 0;  // Backlight

// LCD configuration
static lcd_config_t current_config;
static bool lcd_initialized = false;
static uint dma_channel_tx = -1;
static frame_stats_t lcd_stats;

// LUT for 1-bit to 2-byte RGB565 conversion
// 每个字节(8个1-bit像素) -> 16字节RGB565输出
static uint8_t byte_to_rgb565_lut[256][16];
// Low-level SPI functions
static inline void lcd_write_command(uint8_t cmd)
{
    gpio_put(lcd_pin_dc, 0); // Command mode
    gpio_put(lcd_pin_cs, 0); // Select LCD

    // Add small delay for signal setup
    sleep_us(1);

    int bytes_written = spi_write_blocking(spi_default, &cmd, 1);

    // Add small delay before deselect
    sleep_us(1);

    gpio_put(lcd_pin_cs, 1); // Deselect LCD
}

static inline void lcd_write_data(const uint8_t *data, size_t len)
{
    gpio_put(lcd_pin_dc, 1); // Data mode
    gpio_put(lcd_pin_cs, 0); // Select LCD
    spi_write_blocking(spi_default, data, len);
    gpio_put(lcd_pin_cs, 1); // Deselect LCD
}

static inline void lcd_write_data_byte(uint8_t data)
{
    lcd_write_data(&data, 1);
}

// Initialize LUT for fast 1-bit to RGB565 conversion
static void init_pixel_conversion_lut(void)
{
    for (int byte_val = 0; byte_val < 256; byte_val++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            // 提取第bit位的值 (0 or 1)
            uint8_t pixel_bit = (byte_val >> bit) & 0x01;

            // 转换: 0->0x0000(黑), 1->0xFFFF(白)
            uint16_t rgb565 = pixel_bit ? 0xFFFF : 0x0000;

            // 存储为big-endian格式 (MSB, LSB)
            byte_to_rgb565_lut[byte_val][bit * 2] = rgb565 >> 8;       // MSB
            byte_to_rgb565_lut[byte_val][bit * 2 + 1] = rgb565 & 0xFF; // LSB
        }
    }
}

// Hardware reset
static void lcd_hardware_reset(void)
{
    gpio_put(lcd_pin_rst, 0);
    sleep_ms(10);
    gpio_put(lcd_pin_rst, 1);
    sleep_ms(120);
}

// Set drawing window
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Column Address Set
    lcd_write_command(0x2A);
    uint8_t col_data[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    lcd_write_data(col_data, 4);

    // Row Address Set
    lcd_write_command(0x2B);
    uint8_t row_data[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    lcd_write_data(row_data, 4);

    // Memory Write
    lcd_write_command(0x2C);
}

// Set continuous window for framebuffer updates (one-time setup)
void spi_lcd_set_continuous_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (!lcd_initialized)
        return;

    printf("设置连续传输窗口: (%u,%u) to (%u,%u)\n", x0, y0, x1, y1);

    // Column Address Set
    lcd_write_command(0x2A);
    uint8_t col_data[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    lcd_write_data(col_data, 4);

    // Row Address Set
    lcd_write_command(0x2B);
    uint8_t row_data[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    lcd_write_data(row_data, 4);

    // Memory Write - 进入连续写入模式
    lcd_write_command(0x2C);

    printf("LCD已设置为连续传输模式\n");
}

// Initialize SPI LCD
bool spi_lcd_init(const lcd_config_t *config)
{
    if (!config)
    {
        return false;
    }

    // Save configuration
    memcpy(&current_config, config, sizeof(lcd_config_t));

    // Override pin assignments if provided
    if (config->pin_cs != 0xFF)
        lcd_pin_cs = config->pin_cs;
    if (config->pin_dc != 0xFF)
        lcd_pin_dc = config->pin_dc;
    if (config->pin_rst != 0xFF)
        lcd_pin_rst = config->pin_rst;
    if (config->pin_sck != 0xFF)
        lcd_pin_sck = config->pin_sck;
    if (config->pin_mosi != 0xFF)
        lcd_pin_mosi = config->pin_mosi;
    if (config->pin_blk != 0xFF)
        lcd_pin_blk = config->pin_blk;

    // Initialize SPI
    spi_init(spi0, config->spi_freq_hz);
    gpio_set_function(lcd_pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(lcd_pin_mosi, GPIO_FUNC_SPI);

    // Initialize control pins
    gpio_init(lcd_pin_cs);
    gpio_set_dir(lcd_pin_cs, GPIO_OUT);
    gpio_put(lcd_pin_cs, 1);

    gpio_init(lcd_pin_dc);
    gpio_set_dir(lcd_pin_dc, GPIO_OUT);
    gpio_put(lcd_pin_dc, 0);

    gpio_init(lcd_pin_rst);
    gpio_set_dir(lcd_pin_rst, GPIO_OUT);
    gpio_put(lcd_pin_rst, 1);

    // 背光现在由PWM控制，不在这里初始化
    // (背光PWM在wait_for_lcd_power_on()函数中初始化)

    // Initialize pixel conversion LUT
    init_pixel_conversion_lut();

    // Hardware reset
    lcd_hardware_reset();

    // Initialize LCD controller
    switch (config->controller_type)
    {
    case LCD_CONTROLLER_ST7789:
        // ST7789VW specific initialization

        // Software Reset
        lcd_write_command(0x01);
        sleep_ms(150);

        // Sleep Out
        lcd_write_command(0x11);
        sleep_ms(120);

        // Memory Access Control - ST7789VW specific
        lcd_write_command(0x36);
        lcd_write_data_byte(0x00); // Normal orientation for ST7789VW

        // Color Mode - 16bit RGB565
        lcd_write_command(0x3A);
        lcd_write_data_byte(0x05);

        // Porch Control - ST7789VW optimized
        lcd_write_command(0xB2);
        uint8_t porch_data[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        lcd_write_data(porch_data, 5);

        // Gate Control
        lcd_write_command(0xB7);
        lcd_write_data_byte(0x35);

        // VCOM Setting
        lcd_write_command(0xBB);
        lcd_write_data_byte(0x20); // Adjusted for ST7789VW

        // LCM Control
        lcd_write_command(0xC0);
        lcd_write_data_byte(0x2C);

        // VDV and VRH Command Enable
        lcd_write_command(0xC2);
        lcd_write_data_byte(0x01);

        // VRH Set
        lcd_write_command(0xC3);
        lcd_write_data_byte(0x11); // Adjusted for ST7789VW

        // VDV Set
        lcd_write_command(0xC4);
        lcd_write_data_byte(0x20);

        // Frame Rate Control in Normal Mode
        lcd_write_command(0xC6);
        lcd_write_data_byte(0x0F);

        // Power Control 1
        lcd_write_command(0xD0);
        uint8_t power_data[] = {0xA4, 0xA1};
        lcd_write_data(power_data, 2);

        // Positive Voltage Gamma Control (simplified)
        lcd_write_command(0xE0);
        uint8_t gamma_pos[] = {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D};
        lcd_write_data(gamma_pos, 14);

        // Negative Voltage Gamma Control (simplified)
        lcd_write_command(0xE1);
        uint8_t gamma_neg[] = {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31};
        lcd_write_data(gamma_neg, 14);

        // Display Inversion On
        lcd_write_command(0x21);
        sleep_ms(10);

        // Normal Display On
        lcd_write_command(0x13);
        sleep_ms(10);

        // Column Address Set - for 240x240 on ST7789VW
        lcd_write_command(0x2A);
        uint8_t col_offset[] = {0x00, 0x00, 0x00, 0xEF}; // 0 to 239
        lcd_write_data(col_offset, 4);

        // Row Address Set - for 240x240 on ST7789VW
        lcd_write_command(0x2B);
        uint8_t row_offset[] = {0x00, 0x00, 0x00, 0xEF}; // 0 to 239
        lcd_write_data(row_offset, 4);

        // Display On
        lcd_write_command(0x29);
        sleep_ms(50);

        break;

    default:
        return false;
    }

    // Clear screen
    spi_lcd_clear(0x0000);

    // Claim DMA channel for high-speed transfers
    dma_channel_tx = dma_claim_unused_channel(true);
    if (dma_channel_tx != -1)
    {
        // Configure DMA for SPI transfers
        dma_channel_config c = dma_channel_get_default_config(dma_channel_tx);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
        channel_config_set_dreq(&c, spi_get_dreq(spi_default, true));
        channel_config_set_write_increment(&c, false);
        channel_config_set_read_increment(&c, true);

        dma_channel_configure(dma_channel_tx, &c, &spi_get_hw(spi_default)->dr, NULL, 0, false);
    }

    // 初始化帧统计 (ST7789: 240x240x2 = 115.2KB RGB565数据)
    frame_stats_init(&lcd_stats, "ST7789", 115.2f);

    lcd_initialized = true;
    return true;
}

// =============================================================================
// 显示的核心功能 (集中管理)
// =============================================================================

// 清屏
void spi_lcd_clear(uint16_t color)
{
    if (!lcd_initialized)
        return;

    lcd_set_window(0, 0, current_config.width - 1, current_config.height - 1);
    uint8_t color_bytes[] = {color >> 8, color & 0xFF};

    gpio_put(lcd_pin_dc, 1);
    gpio_put(lcd_pin_cs, 0);
    for (uint32_t i = 0; i < (uint32_t)current_config.width * current_config.height; i++)
    {
        spi_write_blocking(spi_default, color_bytes, 2);
    }
    gpio_put(lcd_pin_cs, 1);
}

// 从帧缓冲区更新显示 (使用DMA批量传输+性能统计)
bool spi_lcd_update_from_framebuffer(void)
{
    if (!lcd_initialized || !lcd_framebuffer_is_render_ready())
        return false;

    // 记录开始时间
    uint32_t start_time_us = time_us_32();

    // 静态分配显示缓冲区 (240x240x2字节 = 115,200字节，32位对齐)
    static uint8_t display_buffer[LCD_FB_WIDTH * LCD_FB_HEIGHT * 2] __attribute__((aligned(4)));

    // 使用新的帧统计模块

    // 高效批量转换：1-bit -> RGB565 (使用直接数据访问)
    uint32_t conversion_start_us = time_us_32();

    const uint8_t *framebuffer_data = lcd_framebuffer_get_render_data();
    if (!framebuffer_data)
    {
        return false;
    }

    // 超高速LUT转换：直接查表替代计算
    uint32_t buffer_idx = 0;
    uint32_t total_bytes = (LCD_FB_WIDTH * LCD_FB_HEIGHT + 7) / 8;

    for (uint32_t byte_idx = 0; byte_idx < total_bytes; byte_idx++)
    {
        uint8_t byte_data = framebuffer_data[byte_idx];

        // 直接拷贝LUT中预计算的16字节结果
        memcpy(&display_buffer[buffer_idx], byte_to_rgb565_lut[byte_data], 16);
        buffer_idx += 16;

        // 边界检查
        if (buffer_idx >= LCD_FB_WIDTH * LCD_FB_HEIGHT * 2)
            break;
    }
    uint32_t conversion_end_us = time_us_32();
    uint32_t conversion_time_us = conversion_end_us - conversion_start_us;

    // 记录传输开始时间
    uint32_t transfer_start_us = time_us_32();
    // 重新发送Memory Write命令重置地址指针 (防止滚动)
    lcd_write_command(0x2C);

    // 进入数据传输模式
    gpio_put(lcd_pin_dc, 1); // 数据模式
    gpio_put(lcd_pin_cs, 0); // 选中LCD

    bool used_dma = false;

    // 使用DMA高速传输整帧数据
    if (dma_channel_tx != -1 && !dma_channel_is_busy(dma_channel_tx))
    {
        used_dma = true;

        // 恢复8位传输（32位传输与SPI硬件不兼容）
        dma_channel_transfer_from_buffer_now(dma_channel_tx, display_buffer, buffer_idx);

        // 等待DMA传输完成
        dma_channel_wait_for_finish_blocking(dma_channel_tx);
    }
    else
    {
        // DMA不可用时使用传统方式
        spi_write_blocking(spi_default, display_buffer, buffer_idx);
    }
    uint32_t transfer_end_us = time_us_32();
    uint32_t transfer_time_us = transfer_end_us - transfer_start_us;

    gpio_put(lcd_pin_cs, 1); // 取消选中LCD

    // 计算和显示详细性能统计
    uint32_t end_time_us = time_us_32();
    uint32_t total_time_us = end_time_us - start_time_us;
    float data_size_kb = 115.2f;                                                            // 240x240x2字节 = 115,200字节
    float transfer_speed_mbps = (data_size_kb / 1024.0f) / (transfer_time_us / 1000000.0f); // MB/s

    // 更新性能统计
    frame_stats_update(&lcd_stats, conversion_time_us, transfer_time_us, used_dma);

    return true;
}

// 绘制单个像素
void spi_lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!lcd_initialized || x >= current_config.width || y >= current_config.height)
        return;

    lcd_set_window(x, y, x, y);
    uint8_t color_bytes[] = {color >> 8, color & 0xFF};
    lcd_write_data(color_bytes, 2);
}
