#include "lcd_st75320.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "frame_stats.h"
#include <string.h>
#include <stdio.h>

// 缩放控制宏
#ifndef ENABLE_LCD_SCALING
#define ENABLE_LCD_SCALING 1  // 默认启用缩放 (240x240 -> 320x240)
#endif

// 引脚定义
#define PIN_A0 10   // A0（RS） 寄存器选择信号
#define PIN_RES 11  // RES 复位
#define PIN_CS 12   // CS 片选
#define PIN_MOSI 15 // SPI MOSI
#define PIN_SCK 14  // SPI SCK

#define SPI_PORT spi1
#define SPI_BAUDRATE 20000000

// 帧显存：30页 x 320列 = 9600字节
#define FB_PAGES 30
#define FB_COLS 320
#define FB_SIZE (FB_PAGES * FB_COLS)

static uint8_t framebuffer[FB_SIZE];
static int dma_chan;
static frame_stats_t lcd_stats;
static lcd_rotation_t current_rotation = LCD_ROTATION_0;

// 预计算的缩放映射表
static uint16_t scale_map_240_to_320[240];
// 预计算X坐标缩放映射表 (0°和180°用)
static uint16_t x_scale_map[240];
static bool scale_fill_map[240];  // 是否需要填充相邻像素

// 90°/270°优化预计算表
static uint8_t y_to_page[240];     // Y坐标 -> page索引
static uint8_t y_to_bit_mask[240]; // Y坐标 -> bit掩码
static uint16_t y_to_fb_offset[240]; // Y坐标 -> framebuffer偏移量基址

// 水平320像素直接查表 (超级优化)
static uint16_t horizontal_320_map[240]; // 240像素直接映射到320的位置
static uint16_t horizontal_320_fill[240]; // 对应的填充位置

static bool scale_map_initialized = false;

static void lcd_write_command(uint8_t cmd)
{
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_A0, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(PIN_CS, 1);
}

static void lcd_write_data(uint8_t data)
{
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_A0, 1);
    spi_write_blocking(SPI_PORT, &data, 1);
    gpio_put(PIN_CS, 1);
}

// 初始化缩放映射表
static void init_scale_mapping(void)
{
    if (scale_map_initialized) return;

    // 预计算240到320的映射关系 (4/3缩放)
    for (int i = 0; i < 240; i++) {
        scale_map_240_to_320[i] = (i * 4) / 3;
        x_scale_map[i] = (i * 4) / 3;
        scale_fill_map[i] = (i % 3) != 0;  // 预计算是否需要填充

        // 90°/270°预计算Y坐标相关信息
        y_to_page[i] = i / 8;
        y_to_bit_mask[i] = (1 << (i % 8));
        y_to_fb_offset[i] = (i / 8) * FB_COLS;

        // 水平320像素直接映射
        horizontal_320_map[i] = (i * 4) / 3;
        horizontal_320_fill[i] = ((i % 3) != 0) ? ((i * 4) / 3 + 1) : 0; // 填充位置或0
    }
    scale_map_initialized = true;
}


void lcd_init(void)
{
    // SPI硬件初始化
    spi_init(SPI_PORT, SPI_BAUDRATE);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    // GPIO初始化
    gpio_init(PIN_A0);
    gpio_init(PIN_RES);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_A0, GPIO_OUT);
    gpio_set_dir(PIN_RES, GPIO_OUT);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // 硬件复位
    gpio_put(PIN_RES, 1);
    gpio_put(PIN_RES, 0);
    sleep_ms(2);
    gpio_put(PIN_RES, 1);
    sleep_ms(200);

    // LCD初始化序列
    lcd_write_command(0xAE); // 显示关闭
    lcd_write_command(0xEA); // 电源放电控制
    lcd_write_data(0x00);
    lcd_write_command(0xA8); // 退出睡眠

    lcd_write_command(0xAB); // 振荡器开启
    lcd_write_command(0x69); // 温度检测开启
    lcd_write_command(0x4E); // 温度系数设置
    for (int i = 0; i < 8; i++)
    {
        lcd_write_data(0x00);
    }

    lcd_write_command(0x39); // 温度标志
    lcd_write_data(0x00);
    lcd_write_data(0x00);

    lcd_write_command(0x2B); // 帧率等级
    lcd_write_data(0x00);

    lcd_write_command(0x5F); // 设置帧频率
    lcd_write_data(0x66);
    lcd_write_data(0x66);

    lcd_write_command(0xA7); // 反显0xA6关0xA7开
    lcd_write_command(0xA4); // 禁用全像素点亮

    lcd_write_command(0xC4); // COM输出状态
    lcd_write_data(0x02);

    lcd_write_command(0xA1); // 列地址方向

    lcd_write_command(0x6D); // 显示区域
    lcd_write_data(0x07);
    lcd_write_data(0x00);

    lcd_write_command(0x84); // 显示数据输入方向

    lcd_write_command(0x36); // 设置N线
    lcd_write_data(0x1e);

    lcd_write_command(0xE4); // N线开启

    lcd_write_command(0xE7); // LCD驱动方法
    lcd_write_data(0x19);

    lcd_write_command(0x81); // 设置对比度
    lcd_write_data(0x46);
    lcd_write_data(0x01);

    lcd_write_command(0xA2); // 偏压设置
    lcd_write_data(0x0a);

    // 电源控制序列
    lcd_write_command(0x25);
    lcd_write_data(0x20);
    sleep_ms(10);
    lcd_write_command(0x25);
    lcd_write_data(0x60);
    sleep_ms(10);
    lcd_write_command(0x25);
    lcd_write_data(0x70);
    sleep_ms(10);
    lcd_write_command(0x25);
    lcd_write_data(0x78);
    sleep_ms(10);
    lcd_write_command(0x25);
    lcd_write_data(0x7c);
    sleep_ms(10);
    lcd_write_command(0x25);
    lcd_write_data(0x7e);
    sleep_ms(10);
    lcd_write_command(0x25);
    lcd_write_data(0x7f);
    sleep_ms(10);

    // 初始化DMA
    dma_chan = dma_claim_unused_channel(true);

    // 初始化缩放映射表
    init_scale_mapping();

    // 初始化帧统计 (ST75320: 240x240 = 7.2KB 显示数据)
    frame_stats_init(&lcd_stats, "ST75320", 7.2f);
    lcd_set_rotation(LCD_ROTATION_90);
    lcd_clear();
    lcd_write_command(0xAF); // 显示开启
    lcd_refresh();
}

void lcd_clear(void)
{
    memset(framebuffer, 0x00, FB_SIZE);
}

void lcd_set_pixel(uint16_t x, uint16_t y, bool color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
        return;

    uint8_t page = y / 8;
    uint8_t bit_pos = y % 8;
    uint16_t fb_index = page * FB_COLS + x;

    if (color)
    {
        framebuffer[fb_index] |= (1 << bit_pos);
    }
    else
    {
        framebuffer[fb_index] &= ~(1 << bit_pos);
    }
}

void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool color)
{
    // 绘制顶边和底边
    for (uint16_t i = 0; i < width; i++)
    {
        lcd_set_pixel(x + i, y, color);
        if (height > 1)
        {
            lcd_set_pixel(x + i, y + height - 1, color);
        }
    }

    // 绘制左边和右边
    for (uint16_t i = 1; i < height - 1; i++)
    {
        lcd_set_pixel(x, y + i, color);
        if (width > 1)
        {
            lcd_set_pixel(x + width - 1, y + i, color);
        }
    }
}

void lcd_refresh(void)
{
    for (int page = 0; page < FB_PAGES; page++)
    {
        // 设置页地址
        lcd_write_command(0xB1);
        lcd_write_data(page);

        // 设置列地址为0
        lcd_write_command(0x13);
        lcd_write_data(0x00);
        lcd_write_data(0x00);

        // 进入数据写入模式
        lcd_write_command(0x1D);

        // 使用DMA传输一页数据
        gpio_put(PIN_CS, 0);
        gpio_put(PIN_A0, 1);

        dma_channel_config c = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
        channel_config_set_dreq(&c, spi_get_dreq(SPI_PORT, true));
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);

        dma_channel_configure(
            dma_chan,
            &c,
            &spi_get_hw(SPI_PORT)->dr,
            &framebuffer[page * FB_COLS],
            FB_COLS,
            true);

        dma_channel_wait_for_finish_blocking(dma_chan);

        while (spi_is_busy(SPI_PORT))
        {
            tight_loop_contents();
        }

        gpio_put(PIN_CS, 1);
    }
}

// 辅助函数：从源数据获取像素值
static inline bool get_source_pixel(const uint8_t *src_data, int x, int y)
{
    if (x < 0 || x >= 240 || y < 0 || y >= 240)
        return false;

    int bit_index = y * 240 + x;
    int byte_index = bit_index / 8;
    int bit_pos = bit_index % 8;

    return (src_data[byte_index] >> bit_pos) & 1;
}

// 辅助函数：设置目标framebuffer像素
static inline void set_target_pixel(int x, int y, bool value)
{
    if (x < 0 || x >= 320 || y < 0 || y >= 240)  // 支持320宽度
        return;

    int page = y / 8;
    int bit_pos = y % 8;
    int fb_index = page * FB_COLS + x;

    if (value)
    {
        framebuffer[fb_index] |= (1 << bit_pos);
    }
    else
    {
        framebuffer[fb_index] &= ~(1 << bit_pos);
    }
}

// 高效批量更新240x240区域 (从1-bit framebuffer数据，支持旋转)
void lcd_update_from_1bit_framebuffer(const uint8_t *src_data)
{
    if (src_data == NULL)
        return;

    // 性能统计开始
    uint32_t start_time_us = time_us_32();

    // 数据转换开始时间
    uint32_t conversion_start_us = time_us_32();

    // 先清空整个320x240显示区域
    for (int page = 0; page < 30; page++)
    {                                                 // 240/8 = 30页
        memset(&framebuffer[page * FB_COLS], 0, 320); // 清空所有320列
    }
    // for (int page = 0; page < 30; page++)
    // {                                                         // 240/8 = 30页
    //     memset(&framebuffer[page * FB_COLS + 240], 0Xff, 80); // 清空前240列
    // }
    // 根据旋转角度进行不同的像素转换
    switch (current_rotation)
    {
    case LCD_ROTATION_0:
    {
#if ENABLE_LCD_SCALING
        // 0度：水平320直接映射 (终极优化版本)
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            int page = src_y / 8;
            int bit_pos = src_y % 8;
            uint8_t bit_mask = (1 << bit_pos);
            uint8_t *fb_base = &framebuffer[page * FB_COLS];

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 水平320直接映射，超高速查表
                if (src_byte & 0x01) {
                    fb_base[horizontal_320_map[base_src_x]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x])
                        fb_base[horizontal_320_fill[base_src_x]] |= bit_mask;
                }
                if (src_byte & 0x02) {
                    fb_base[horizontal_320_map[base_src_x + 1]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 1])
                        fb_base[horizontal_320_fill[base_src_x + 1]] |= bit_mask;
                }
                if (src_byte & 0x04) {
                    fb_base[horizontal_320_map[base_src_x + 2]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 2])
                        fb_base[horizontal_320_fill[base_src_x + 2]] |= bit_mask;
                }
                if (src_byte & 0x08) {
                    fb_base[horizontal_320_map[base_src_x + 3]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 3])
                        fb_base[horizontal_320_fill[base_src_x + 3]] |= bit_mask;
                }
                if (src_byte & 0x10) {
                    fb_base[horizontal_320_map[base_src_x + 4]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 4])
                        fb_base[horizontal_320_fill[base_src_x + 4]] |= bit_mask;
                }
                if (src_byte & 0x20) {
                    fb_base[horizontal_320_map[base_src_x + 5]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 5])
                        fb_base[horizontal_320_fill[base_src_x + 5]] |= bit_mask;
                }
                if (src_byte & 0x40) {
                    fb_base[horizontal_320_map[base_src_x + 6]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 6])
                        fb_base[horizontal_320_fill[base_src_x + 6]] |= bit_mask;
                }
                if (src_byte & 0x80) {
                    fb_base[horizontal_320_map[base_src_x + 7]] |= bit_mask;
                    if (horizontal_320_fill[base_src_x + 7])
                        fb_base[horizontal_320_fill[base_src_x + 7]] |= bit_mask;
                }
            }
        }
#else
        // 0度：240x240不缩放版本
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            int page = src_y / 8;
            int bit_pos = src_y % 8;
            uint8_t bit_mask = (1 << bit_pos);
            uint8_t *fb_base = &framebuffer[page * FB_COLS];

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 直接1:1映射，无缩放
                if (src_byte & 0x01) fb_base[base_src_x + 0] |= bit_mask;
                if (src_byte & 0x02) fb_base[base_src_x + 1] |= bit_mask;
                if (src_byte & 0x04) fb_base[base_src_x + 2] |= bit_mask;
                if (src_byte & 0x08) fb_base[base_src_x + 3] |= bit_mask;
                if (src_byte & 0x10) fb_base[base_src_x + 4] |= bit_mask;
                if (src_byte & 0x20) fb_base[base_src_x + 5] |= bit_mask;
                if (src_byte & 0x40) fb_base[base_src_x + 6] |= bit_mask;
                if (src_byte & 0x80) fb_base[base_src_x + 7] |= bit_mask;
            }
        }
#endif
        break;
    }

    case LCD_ROTATION_90:
    {
#if ENABLE_LCD_SCALING
        // 90度顺时针旋转：水平320直接映射 (终极优化版本)
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            uint16_t dst_x_base = 319 - horizontal_320_map[src_y];
            uint16_t dst_x_fill = horizontal_320_fill[src_y] ? (319 - horizontal_320_fill[src_y] + 1) : 0;

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 水平320直接映射 + Y轴查表
                if (src_byte & 0x01) {
                    int dst_y = base_src_x;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x02) {
                    int dst_y = base_src_x + 1;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x04) {
                    int dst_y = base_src_x + 2;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x08) {
                    int dst_y = base_src_x + 3;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x10) {
                    int dst_y = base_src_x + 4;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x20) {
                    int dst_y = base_src_x + 5;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x40) {
                    int dst_y = base_src_x + 6;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
                if (src_byte & 0x80) {
                    int dst_y = base_src_x + 7;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base > 0)
                        *(fb_ptr - 1) |= bit_mask;
                }
            }
        }
#else
        // 90度顺时针旋转：240x240不缩放版本
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            uint16_t dst_x = 239 - src_y;

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 简单90度旋转，无缩放
                if (src_byte & 0x01) {
                    int dst_y = base_src_x;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x02) {
                    int dst_y = base_src_x + 1;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x04) {
                    int dst_y = base_src_x + 2;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x08) {
                    int dst_y = base_src_x + 3;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x10) {
                    int dst_y = base_src_x + 4;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x20) {
                    int dst_y = base_src_x + 5;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x40) {
                    int dst_y = base_src_x + 6;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x80) {
                    int dst_y = base_src_x + 7;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
            }
        }
#endif
        break;
    }

    case LCD_ROTATION_180:
    {
#if ENABLE_LCD_SCALING
        // 180度旋转：水平320直接映射 (终极优化版本)
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            int dst_y = 239 - src_y;
            int dst_page = dst_y / 8;
            int dst_bit_pos = dst_y % 8;
            uint8_t dst_mask = (1 << dst_bit_pos);
            uint8_t *fb_base = &framebuffer[dst_page * FB_COLS];

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 水平320直接映射（180度翻转）
                if (src_byte & 0x01) {
                    int dst_x = 319 - horizontal_320_map[base_src_x];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x02) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 1];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 1] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x04) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 2];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 2] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x08) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 3];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 3] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x10) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 4];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 4] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x20) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 5];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 5] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x40) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 6];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 6] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
                if (src_byte & 0x80) {
                    int dst_x = 319 - horizontal_320_map[base_src_x + 7];
                    fb_base[dst_x] |= dst_mask;
                    if (horizontal_320_fill[base_src_x + 7] && dst_x > 0)
                        fb_base[dst_x - 1] |= dst_mask;
                }
            }
        }
#else
        // 180度旋转：240x240不缩放版本
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            int dst_y = 239 - src_y;
            int dst_page = dst_y / 8;
            int dst_bit_pos = dst_y % 8;
            uint8_t dst_mask = (1 << dst_bit_pos);
            uint8_t *fb_base = &framebuffer[dst_page * FB_COLS];

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 直接180度旋转，无缩放
                if (src_byte & 0x01) fb_base[239 - (base_src_x + 0)] |= dst_mask;
                if (src_byte & 0x02) fb_base[239 - (base_src_x + 1)] |= dst_mask;
                if (src_byte & 0x04) fb_base[239 - (base_src_x + 2)] |= dst_mask;
                if (src_byte & 0x08) fb_base[239 - (base_src_x + 3)] |= dst_mask;
                if (src_byte & 0x10) fb_base[239 - (base_src_x + 4)] |= dst_mask;
                if (src_byte & 0x20) fb_base[239 - (base_src_x + 5)] |= dst_mask;
                if (src_byte & 0x40) fb_base[239 - (base_src_x + 6)] |= dst_mask;
                if (src_byte & 0x80) fb_base[239 - (base_src_x + 7)] |= dst_mask;
            }
        }
#endif
        break;
    }

    case LCD_ROTATION_270:
    {
#if ENABLE_LCD_SCALING
        // 270度顺时针旋转：水平320直接映射 (终极优化版本)
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            uint16_t dst_x_base = horizontal_320_map[src_y];
            uint16_t dst_x_fill = horizontal_320_fill[src_y];

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 水平320直接映射 + Y轴查表
                if (src_byte & 0x01) {
                    int dst_y = 239 - base_src_x;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x02) {
                    int dst_y = 239 - (base_src_x + 1);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x04) {
                    int dst_y = 239 - (base_src_x + 2);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x08) {
                    int dst_y = 239 - (base_src_x + 3);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x10) {
                    int dst_y = 239 - (base_src_x + 4);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x20) {
                    int dst_y = 239 - (base_src_x + 5);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x40) {
                    int dst_y = 239 - (base_src_x + 6);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
                if (src_byte & 0x80) {
                    int dst_y = 239 - (base_src_x + 7);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x_base];
                    uint8_t bit_mask = y_to_bit_mask[dst_y];
                    *fb_ptr |= bit_mask;
                    if (dst_x_fill && dst_x_base < 319)
                        *(fb_ptr + 1) |= bit_mask;
                }
            }
        }
#else
        // 270度顺时针旋转：240x240不缩放版本
        const uint8_t *src_ptr = src_data;

        for (int src_y = 0; src_y < 240; src_y++)
        {
            uint16_t dst_x = src_y;

            for (int src_x_byte = 0; src_x_byte < 30; src_x_byte++)
            {
                uint8_t src_byte = *src_ptr++;
                if (src_byte == 0) continue;

                int base_src_x = src_x_byte * 8;

                // 简单270度旋转，无缩放
                if (src_byte & 0x01) {
                    int dst_y = 239 - base_src_x;
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x02) {
                    int dst_y = 239 - (base_src_x + 1);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x04) {
                    int dst_y = 239 - (base_src_x + 2);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x08) {
                    int dst_y = 239 - (base_src_x + 3);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x10) {
                    int dst_y = 239 - (base_src_x + 4);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x20) {
                    int dst_y = 239 - (base_src_x + 5);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x40) {
                    int dst_y = 239 - (base_src_x + 6);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
                if (src_byte & 0x80) {
                    int dst_y = 239 - (base_src_x + 7);
                    uint8_t *fb_ptr = &framebuffer[y_to_fb_offset[dst_y] + dst_x];
                    *fb_ptr |= y_to_bit_mask[dst_y];
                }
            }
        }
#endif
        break;
    }

    default:
        // 默认使用0度转换
        printf("警告: 未知的旋转角度，使用默认0度\n");
        current_rotation = LCD_ROTATION_0;
        lcd_update_from_1bit_framebuffer(src_data);
        return;
    }

    uint32_t conversion_end_us = time_us_32();
    uint32_t conversion_time_us = conversion_end_us - conversion_start_us;

    // 传输开始时间
    uint32_t transfer_start_us = time_us_32();

    // 刷新显示
    lcd_refresh();

    uint32_t transfer_end_us = time_us_32();
    uint32_t transfer_time_us = transfer_end_us - transfer_start_us;

    // 更新性能统计 (ST75320使用DMA传输)
    frame_stats_update(&lcd_stats, conversion_time_us, transfer_time_us, true);
}

// 硬件镜像控制
void lcd_set_mirror(lcd_mirror_t mirror)
{
    switch (mirror)
    {
    case LCD_MIRROR_NORMAL:      // 正常显示
        lcd_write_command(0xA1); // 列地址正向
        lcd_write_command(0xC0); // 行扫描正向
        break;

    case LCD_MIRROR_H:           // 水平镜像
        lcd_write_command(0xA0); // 列地址反向
        lcd_write_command(0xC0); // 行扫描正向
        break;

    case LCD_MIRROR_V:           // 垂直镜像
        lcd_write_command(0xA1); // 列地址正向
        lcd_write_command(0xC8); // 行扫描反向
        break;

    case LCD_MIRROR_HV:          // 水平+垂直镜像
        lcd_write_command(0xA0); // 列地址反向
        lcd_write_command(0xC8); // 行扫描反向
        break;

    default:
        lcd_write_command(0xA1);
        lcd_write_command(0xC0);
        break;
    }

    printf("ST75320镜像设置: %s\n",
           (mirror == LCD_MIRROR_NORMAL) ? "正常" : (mirror == LCD_MIRROR_H) ? "水平镜像"
                                                : (mirror == LCD_MIRROR_V)   ? "垂直镜像"
                                                                             : "水平+垂直镜像");
}

// 软件旋转控制 (支持所有角度的真正旋转)
void lcd_set_rotation(lcd_rotation_t rotation)
{
    current_rotation = rotation;

    switch (rotation)
    {
    case LCD_ROTATION_0: // 正常方向
        lcd_set_mirror(LCD_MIRROR_NORMAL);
        printf("ST75320旋转: 0度 (优化硬件路径)\n");
        break;

    case LCD_ROTATION_90: // 90度旋转
        lcd_set_mirror(LCD_MIRROR_NORMAL);
        printf("ST75320旋转: 90度 (软件像素重映射)\n");
        break;

    case LCD_ROTATION_180: // 180度旋转
        // 可以选择硬件镜像或软件实现
        lcd_set_mirror(LCD_MIRROR_NORMAL); // 使用软件实现保持一致性
        printf("ST75320旋转: 180度 (软件像素重映射)\n");
        break;

    case LCD_ROTATION_270: // 270度旋转
        lcd_set_mirror(LCD_MIRROR_NORMAL);
        printf("ST75320旋转: 270度 (软件像素重映射)\n");
        break;

    default:
        lcd_set_mirror(LCD_MIRROR_NORMAL);
        current_rotation = LCD_ROTATION_0;
        printf("ST75320旋转: 默认0度\n");
        break;
    }
}

void lcd_set_contrast(uint8_t contrast)
{
    // 限制对比度值范围 0x00 ~ 0x7F
    if (contrast > 0x7F) {
        contrast = 0x7F;
    }

    lcd_write_command(0x81);  // 设置对比度命令
    lcd_write_data(contrast); // 对比度值
    lcd_write_data(0x01);     // 固定参数
}