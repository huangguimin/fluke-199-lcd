#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

// =============================================================================
// LCD驱动选择配置
// =============================================================================

// LCD驱动类型选择
// 1. ST7789 240x240 彩色LCD (默认) - 使用SPI接口
// 2. ST75320 320x240 单色LCD - 使用SPI接口
//
// 要使用ST75320 LCD，请取消下面一行的注释：
#define USE_ST75320_LCD 1

// 要使用ST7789 LCD（默认），请保持上面一行注释状态

// =============================================================================
// 对应的引脚配置
// =============================================================================

#ifdef USE_ST75320_LCD
    // ST75320 LCD 引脚配置 (在 lcd_st75320.c 中定义)
    // PIN_A0   = 10   (A0/RS 寄存器选择信号)
    // PIN_RES  = 11   (RES 复位)
    // PIN_CS   = 12   (CS 片选)
    // PIN_MOSI = 15   (SPI MOSI)
    // PIN_SCK  = 14   (SPI SCK)
    // SPI_PORT = spi1

    #define LCD_TYPE_NAME "ST75320 320x240 单色LCD"
    #define LCD_DISPLAY_WIDTH 320
    #define LCD_DISPLAY_HEIGHT 240
    #define LCD_COLOR_DEPTH 1  // 1-bit 单色

#else
    // ST7789 LCD 引脚配置 (在 lcd_converter.c 中定义)
    // pin_cs   = 17   (片选)
    // pin_dc   = 16   (数据/命令)
    // pin_rst  = 20   (复位)
    // pin_sck  = 18   (时钟)
    // pin_mosi = 19   (数据)
    // pin_blk  = 21   (背光)
    // SPI_PORT = spi0

    #define LCD_TYPE_NAME "ST7789 240x240 彩色LCD"
    #define LCD_DISPLAY_WIDTH 240
    #define LCD_DISPLAY_HEIGHT 240
    #define LCD_COLOR_DEPTH 16  // 16-bit RGB565

#endif

#endif // LCD_CONFIG_H