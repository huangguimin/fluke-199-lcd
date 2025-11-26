#ifndef LCD_FRAMEBUFFER_H
#define LCD_FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"


// Frame buffer structure
typedef struct {
    uint8_t* data;              // Pointer to frame data (allocated internally)
    volatile bool ready;
    volatile bool capturing;
    uint32_t frame_id;
    uint64_t timestamp_us;
} lcd_framebuffer_t;

// Initialize frame buffer system
bool lcd_framebuffer_init(void);


// Auto-capture DMA functions (zero CPU involvement)
bool lcd_framebuffer_init_auto_capture(PIO pio, uint sm);
bool lcd_framebuffer_start_auto_capture(void);
bool lcd_framebuffer_stop_auto_capture(void);
bool lcd_framebuffer_is_auto_capturing(void);
uint32_t lcd_framebuffer_get_frame_count(void);

// Safe display functions (triple buffering)
bool lcd_framebuffer_prepare_display_frame(void);

bool lcd_framebuffer_is_render_ready(void);
// High-performance direct data access (for optimized display)
const uint8_t* lcd_framebuffer_get_render_data(void);

// Frame interrupt functions
void lcd_capture_frame_irq_enable(PIO pio);

// Get frame timing information (for offset detection)
int32_t lcd_framebuffer_get_frame_to_dma_interval(void);

// Reset PIO state machine and DMA (for error recovery)
bool lcd_framebuffer_reset_capture_system(void);

// Wait for LCD power on signal (GPIO 1)
void wait_for_lcd_power_on(void);

// PWM control functions
void init_pwm_output(uint gpio, float freq_hz, float duty_cycle);
void set_pwm_duty_cycle(uint gpio, float duty_cycle);

// LCD backlight control (using PWM)
void set_lcd_backlight_brightness(float brightness);


// Constants - X3501 LCD is 240x240
#define LCD_FB_WIDTH    240
#define LCD_FB_HEIGHT   240

#endif // LCD_FRAMEBUFFER_H