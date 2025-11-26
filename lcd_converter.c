#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "lcd_capture.pio.h"
#include "spi_lcd.h"
#include "lcd_framebuffer.h"
#include "lcd_config.h"
#include "sensor.h"

#ifdef USE_ST75320_LCD
#include "lcd_st75320.h"
#endif

// 配置
#define LCD_CAPTURE_PIO pio0
#define LCD_CAPTURE_SM 0 // 单状态机

// 当前IO口配置
#define X3501_FRAME_PIN 2
#define X3501_LINECLK_PIN 3
#define X3501_DATACLK_PIN 4
#define X3501_DATA_BASE_PIN 5 // LCDAT0-3基地址 (GPIO 5,6,7,8)

// X3501 LCD规格常量
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

// PIO初始化
static bool init_capture_pio(void)
{
    printf("初始化DSTN全硬件捕获PIO...\n");

    // 加载lcd_capture程序（根据用户修改的文件）
    printf("加载PIO程序...\n");
    uint offset = pio_add_program(LCD_CAPTURE_PIO, &lcd_capture_program);
    printf("LCD_CAPTURE程序偏移: %u\n", offset);

    // 初始化状态机
    printf("初始化状态机，IO配置:\n");
    printf("  数据基脚(LCDAT0-3): GPIO %d-%d\n", X3501_DATA_BASE_PIN, X3501_DATA_BASE_PIN + 3);
    printf("  时钟(DATACLK0): GPIO %d\n", X3501_DATACLK_PIN);
    printf("  行时钟(LINECLK): GPIO %d\n", X3501_LINECLK_PIN);
    printf("  帧信号(FRAME): GPIO %d\n", X3501_FRAME_PIN);

    printf("调用PIO初始化函数...\n");
    lcd_capture_program_init(LCD_CAPTURE_PIO, LCD_CAPTURE_SM, offset);
    printf("PIO初始化函数返回\n");

    printf("DSTN全硬件捕获初始化完成\n");
    return true;
}

// 初始化SPI LCD
static bool init_spi_lcd(void)
{
#ifdef USE_ST75320_LCD
    printf("初始化ST75320 LCD...\n");
    lcd_init();
    printf("ST75320 LCD初始化成功\n");
    return true;
#else
    printf("初始化ST7789 SPI LCD...\n");

    lcd_config_t config = LCD_CONFIG_ST7789_240x240;
    config.spi_freq_hz = 80000000; // 80MHz

    // 自定义引脚配置
    config.pin_cs = 17;
    config.pin_dc = 16;
    config.pin_rst = 20;
    config.pin_sck = 18;
    config.pin_mosi = 19;
    config.pin_blk = 21;

    if (!spi_lcd_init(&config))
    {
        printf("错误: SPI LCD初始化失败\n");
        return false;
    }

    // // 显示红绿蓝三色测试图案
    // spi_lcd_clear(LCD_COLOR_BLACK);

    // // 红色区域 (0-79像素宽)
    // for(int y = 0; y < 240; y++) {
    //     for(int x = 0; x < 80; x++) {
    //         spi_lcd_draw_pixel(x, y, LCD_COLOR_RED);
    //     }
    // }

    // // 绿色区域 (80-159像素宽)
    // for(int y = 0; y < 240; y++) {
    //     for(int x = 80; x < 160; x++) {
    //         spi_lcd_draw_pixel(x, y, LCD_COLOR_GREEN);
    //     }
    // }

    // // 蓝色区域 (160-239像素宽)
    // for(int y = 0; y < 240; y++) {
    //     for(int x = 160; x < 240; x++) {
    //         spi_lcd_draw_pixel(x, y, LCD_COLOR_BLUE);
    //     }
    // }

    printf("ST7789 SPI LCD初始化成功\n");
    return true;
#endif
}

// 高效显示framebuffer到SPI LCD (使用DMA批量传输)
static void display_framebuffer_to_lcd(void)
{
#ifdef USE_ST75320_LCD
    // ST75320和捕获的framebuffer都是1-bit单色，使用优化的批量更新
    const uint8_t *framebuffer_data = lcd_framebuffer_get_render_data();
    if (framebuffer_data != NULL)
    {
        lcd_update_from_1bit_framebuffer(framebuffer_data);
    }
#else
    // 直接使用spi_lcd的高效framebuffer更新函数
    if (!spi_lcd_update_from_framebuffer())
    {
        printf("显示失败 - framebuffer未就绪\n");
    }
#endif
}
static void display_frame_check(void)
{
    static uint32_t last_status_time = 0;
    // 每1秒打印状态信息并检测异常
    if (last_status_time <= time_us_32())
    {
        last_status_time = time_us_32() + 1000 * 100;
        int32_t frame_to_dma_interval = lcd_framebuffer_get_frame_to_dma_interval();

        // printf(">>> 帧时序: frame_to_dma_interval = %d us (用于偏移检测)\n",
        //        frame_to_dma_interval);

        // 连续3次检测到frame_to_dma_interval不在13800-13810范围内就重置
        static uint8_t error_count = 0;
        const uint32_t TARGET_MIN = 13799;
        const uint32_t TARGET_MAX = 13810;

        if (frame_to_dma_interval < TARGET_MIN || frame_to_dma_interval > TARGET_MAX)
        {
            error_count++;
            printf(">>> 时序异常检测: %d/%d (范围: %u-%u us)\n",
                   error_count, 3, TARGET_MIN, TARGET_MAX);

            if (error_count >= 3)
            {
                printf(">>> 连续3次时序异常，重置捕获系统\n");
                lcd_framebuffer_reset_capture_system();
                error_count = 0; // 重置计数器
            }
        }
        else
        {
            // 时序正常，重置错误计数
            if (error_count > 0)
            {
                printf(">>> 时序恢复正常，重置错误计数\n");
                error_count = 0;
            }
        }
    }
}

int main()
{
    // 初始化
    stdio_init_all();
    // sleep_ms(2000);

    // 使用PWM控制SPI LCD背光 (GPIO 21) - 在检测到LCD开关信号后才开启
    init_pwm_output(21, 1000.0f, 0); // 1kHz, 灭屏亮度

    printf("DSTN零CPU参与帧捕获器启动...\n");
    printf("目标: X3501 LCD 240x240像素帧捕获\n");
    printf("使用新的lcd_framebuffer模块实现零CPU参与\n");
    printf("当前LCD驱动: %s (%dx%d, %d-bit)\n",
           LCD_TYPE_NAME, LCD_DISPLAY_WIDTH, LCD_DISPLAY_HEIGHT, LCD_COLOR_DEPTH);

    // 初始化SPI LCD
    if (!init_spi_lcd())
    {
        printf("LCD初始化失败\n");
        return -1;
    }

#ifndef USE_ST75320_LCD
    // 设置SPI LCD为连续内存传输模式 (一次性设置窗口)
    printf("设置LCD连续传输窗口 (0,0)-(239,239)...\n");
    spi_lcd_set_continuous_window(0, 0, 239, 239);
#else
    printf("ST75320 LCD无需设置连续传输窗口\n");
#endif

    // 初始化framebuffer系统
    if (!lcd_framebuffer_init())
    {
        printf("framebuffer初始化失败\n");
        return -1;
    }

    // 初始化捕获PIO
    if (!init_capture_pio())
    {
        printf("PIO初始化失败\n");
        return -1;
    }

    // 初始化自动捕获DMA
    if (!lcd_framebuffer_init_auto_capture(LCD_CAPTURE_PIO, LCD_CAPTURE_SM))
    {
        printf("自动捕获DMA初始化失败\n");
        return -1;
    }

    // 启动零CPU参与的自动捕获
    if (!lcd_framebuffer_start_auto_capture())
    {
        printf("启动自动捕获失败\n");
        return -1;
    }
    // 启动帧中断
    lcd_capture_frame_irq_enable(LCD_CAPTURE_PIO);

    printf("零CPU参与的自动捕获已启动！\n");
    printf("===========================================\n");

    // 等待LCD电源开关信号才开始主循环
    wait_for_lcd_power_on();

    sensor_init(); // 初始化传感器

    static uint64_t last_sensor_read_time = 0;
    const uint64_t sensor_read_interval_us = 200000; // 200ms

    while (true)
    {
        // 准备安全的显示帧（拷贝到专用渲染缓冲区）
        if (lcd_framebuffer_prepare_display_frame())
        {
            // 现在可以安全地显示，数据不会被采集覆盖
            display_framebuffer_to_lcd();
        }

        display_frame_check();

        // 每200ms读取一次传感器数据
        uint64_t current_time = time_us_64();
        if (current_time - last_sensor_read_time >= sensor_read_interval_us)
        {
            last_sensor_read_time = current_time;

            // 获取滤波后的数据
            float voltage = sensor_get_filtered_voltage();
            float duty = sensor_get_filtered_duty_cycle();

            // 根据占空比设置背光亮度（两档固定值，带滞后避免抖动）
            static float last_brightness = -1.0f;
            static bool last_high_brightness = false;
            float brightness;
            bool high_brightness;

            if (duty < 0.0f) {
                // 无信号，保持当前亮度
                brightness = last_brightness;
                high_brightness = last_high_brightness;
            } else {
                // 带滞后的阈值判断：上升阈值 20%，下降阈值 10%
                if (last_high_brightness) {
                    // 当前是高亮度，低于 10% 才切换到低亮度
                    high_brightness = (duty >= 10.0f);
                } else {
                    // 当前是低亮度，高于 20% 才切换到高亮度
                    high_brightness = (duty >= 20.0f);
                }

                brightness = high_brightness ? 1.0f : 0.20f;
            }

            // 直接设置亮度，无渐变
            if (duty >= 0.0f && brightness != last_brightness) {
                set_pwm_duty_cycle(21, brightness);
                last_brightness = brightness;
                last_high_brightness = high_brightness;
            }

#ifdef USE_ST75320_LCD
            // 根据电压设置对比度（电压高→对比度低，反向映射）
            // 电压范围: 1.1V ~ 2.3V
            // 对比度范围: 0x7F(最高) ~ 0x30(最低)
            static uint8_t last_contrast = 0xFF;
            uint8_t contrast;

            if (voltage < 1.1f) {
                contrast = 0x7F;  // 电压低→对比度高
            } else if (voltage > 2.3f) {
                contrast = 0x30;  // 电压高→对比度低
            } else {
                // 反向线性映射: 1.1V→0x7F, 2.3V→0x30
                contrast = (uint8_t)(0x7F - (voltage - 1.1f) / (2.3f - 1.1f) * (0x7F - 0x30));
            }

            // 步进为 1：对比度值变化才更新，避免频繁写入但保证平滑
            if (last_contrast == 0xFF || contrast != last_contrast) {
                lcd_set_contrast(contrast);
                last_contrast = contrast;
            }
#endif

            // 打印调试信息
            if (duty >= 0.0f) {
                float freq = sensor_get_frequency();
                printf("电压: %.2fV (对比度:0x%02X), 占空比: %.2f%% (亮度:%s), 频率: %.0fHz\n",
                       voltage, last_contrast, duty, high_brightness ? "高" : "中", freq);
            } else {
                printf("电压: %.2fV, 占空比: 无信号\n", voltage);
            }
        }

        // CPU可以完全休眠，一切都是自动的！
        // sleep_ms(1);
    }

    return 0;
}