#ifndef LCD_ST75320_H
#define LCD_ST75320_H

#include "pico/stdlib.h"
#include <stdbool.h>

#define LCD_WIDTH 320
#define LCD_HEIGHT 240

// 初始化LCD
void lcd_init(void);

// 清屏
void lcd_clear(void);

// 设置像素点
void lcd_set_pixel(uint16_t x, uint16_t y, bool color);

// 绘制矩形边框
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool color);

// 刷新显示
void lcd_refresh(void);

// 高效批量更新240x240区域 (从1-bit framebuffer数据)
void lcd_update_from_1bit_framebuffer(const uint8_t *src_data);

// 显示镜像控制 (硬件支持)
typedef enum {
    LCD_MIRROR_NORMAL = 0,     // 正常显示
    LCD_MIRROR_H      = 1,     // 水平镜像
    LCD_MIRROR_V      = 2,     // 垂直镜像
    LCD_MIRROR_HV     = 3      // 水平+垂直镜像 (相当于180度旋转)
} lcd_mirror_t;

void lcd_set_mirror(lcd_mirror_t mirror);

// 软件旋转控制 (需要重新排列framebuffer数据)
typedef enum {
    LCD_ROTATION_0   = 0,  // 正常方向
    LCD_ROTATION_90  = 1,  // 顺时针90度 (软件实现)
    LCD_ROTATION_180 = 2,  // 180度 (可用硬件镜像)
    LCD_ROTATION_270 = 3   // 顺时针270度 (软件实现)
} lcd_rotation_t;

void lcd_set_rotation(lcd_rotation_t rotation);

/**
 * @brief 设置LCD对比度
 *
 * @param contrast 对比度值 (0x00 ~ 0x7F, 默认 0x46)
 */
void lcd_set_contrast(uint8_t contrast);

#endif // LCD_ST75320_H