#ifndef SPI_LCD_H
#define SPI_LCD_H

#include <stdint.h>
#include <stdbool.h>

// Supported LCD controller types
typedef enum {
    LCD_CONTROLLER_ST7789
} lcd_controller_type_t;

// LCD configuration structure
typedef struct {
    lcd_controller_type_t controller_type;
    uint16_t width;
    uint16_t height;
    uint32_t spi_freq_hz;
    
    // Pin assignments (0xFF = use default)
    uint8_t pin_cs;      // Chip Select
    uint8_t pin_dc;      // Data/Command
    uint8_t pin_rst;     // Reset
    uint8_t pin_sck;     // SPI Clock
    uint8_t pin_mosi;    // SPI Data
    uint8_t pin_blk;     // Backlight Control
} lcd_config_t;

// Common LCD resolutions
#define LCD_WIDTH_240   240

// Common RGB565 colors
#define LCD_COLOR_BLACK     0x0000
#define LCD_COLOR_WHITE     0xFFFF
#define LCD_COLOR_RED       0xF800
#define LCD_COLOR_GREEN     0x07E0
#define LCD_COLOR_BLUE      0x001F
#define LCD_COLOR_YELLOW    0xFFE0
#define LCD_COLOR_MAGENTA   0xF81F
#define LCD_COLOR_CYAN      0x07FF


// Default pin assignments
#define LCD_DEFAULT_PIN_CS      5
#define LCD_DEFAULT_PIN_DC      4
#define LCD_DEFAULT_PIN_RST     3
#define LCD_DEFAULT_PIN_SCK     2
#define LCD_DEFAULT_PIN_MOSI    1
#define LCD_DEFAULT_PIN_BLK     0

// Default SPI frequency (10MHz)
#define LCD_DEFAULT_SPI_FREQ    10000000

// Function prototypes
bool spi_lcd_init(const lcd_config_t* config);
void spi_lcd_clear(uint16_t color);
void spi_lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
bool spi_lcd_update_from_framebuffer(void);
void spi_lcd_set_continuous_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// Helper function to create RGB565 color from RGB components
static inline uint16_t spi_lcd_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Pre-defined configurations for common LCD modules
static const lcd_config_t LCD_CONFIG_ST7789_240x240 = {
    .controller_type = LCD_CONTROLLER_ST7789,
    .width = 240,
    .height = 240,
    .spi_freq_hz = LCD_DEFAULT_SPI_FREQ,
    .pin_cs = 0xFF,     // Use defaults
    .pin_dc = 0xFF,
    .pin_rst = 0xFF,
    .pin_sck = 0xFF,
    .pin_mosi = 0xFF,
    .pin_blk = 0xFF
};


#endif // SPI_LCD_H