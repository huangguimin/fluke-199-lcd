#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "lcd_framebuffer.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"

// LCD frame specifications - X3501 is 240x240, 1-bit per pixel
#define LCD_WIDTH 240
#define LCD_HEIGHT 240
#define LCD_BITS_PER_PIXEL 1
#define LCD_BYTES_PER_LINE ((LCD_WIDTH * LCD_BITS_PER_PIXEL + 7) / 8) // 30 bytes per line
#define LCD_FRAME_SIZE (LCD_BYTES_PER_LINE * LCD_HEIGHT)              // 7,200 bytes per frame

// Internal frame buffer structure
typedef struct
{
    uint8_t data[LCD_FRAME_SIZE];
    volatile bool ready;
    volatile bool capturing;
    uint32_t frame_id;
    uint64_t timestamp_us;
    int32_t frame_to_dma_interval_us; // 帧开始到DMA完成的时间间隔(微秒)
} internal_framebuffer_t;

// Triple buffer system for safe display (zero-copy implementation)
static internal_framebuffer_t frame_buffers[3];
static volatile uint8_t active_buffer = 0;  // Buffer being written by DMA
static volatile uint8_t display_buffer = 1; // Buffer ready for display
static volatile uint8_t render_buffer = 2;  // Buffer being used by display system
static critical_section_t buffer_mutex;

// =============================================================================
// DMA和自动捕获配置 (集中管理)
// =============================================================================
static uint dma_channel = -1; // 用于PIO数据捕获
static bool framebuffer_initialized = false;

// Auto-capture DMA configuration
static PIO pio_instance = NULL;
static uint pio_sm = 0;
static volatile bool auto_capture_enabled = false;
static volatile uint32_t frame_counter = 0;
static volatile uint32_t frame_sync_errors = 0;

// Initialize frame buffer system
bool lcd_framebuffer_init(void)
{
    if (framebuffer_initialized)
    {
        return true;
    }

    // Initialize critical section for thread safety
    critical_section_init(&buffer_mutex);

    // Clear frame buffers
    memset(frame_buffers, 0, sizeof(frame_buffers));

    // Initialize buffer states (triple buffer)
    frame_buffers[0].ready = false;
    frame_buffers[0].capturing = false;
    frame_buffers[0].frame_id = 0;

    frame_buffers[1].ready = false;
    frame_buffers[1].capturing = false;
    frame_buffers[1].frame_id = 0;

    frame_buffers[2].ready = false;
    frame_buffers[2].capturing = false;
    frame_buffers[2].frame_id = 0;

    // Claim DMA channel for PIO data capture
    dma_channel = dma_claim_unused_channel(true);
    if (dma_channel == -1)
    {
        return false;
    }

    framebuffer_initialized = true;
    return true;
}
// =============================================================================
// 帧中断捕获
// =============================================================================
static uint64_t frame_start_time = 0;       // 用于记录帧开始时间
static uint64_t last_dma_complete_time = 0; // 用于记录上一次DMA完成时间

static void pio_irq_handler(void)
{
    // 清除PIO中断标志
    pio_interrupt_clear(pio_instance, 0);
    frame_start_time = time_us_64();
}

void lcd_capture_frame_irq_enable(PIO pio)
{
    // 获取PIO的IRQ编号 (PIO0->IRQ_PIO0_0, PIO1->IRQ_PIO1_0)
    uint irq_num = (pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0;

    irq_set_exclusive_handler(irq_num, pio_irq_handler);
    irq_set_enabled(irq_num, true);
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
}
// =============================================================================
// DMA中断处理和自动捕获系统 (集中管理)
// =============================================================================
// DMA中断处理函数 - 处理帧完成和缓冲区轮换
static void dma_capture_irq_handler(void)
{
    if (dma_channel_get_irq0_status(dma_channel))
    {
        dma_channel_acknowledge_irq0(dma_channel);

        if (!auto_capture_enabled)
        {
            return;
        }

        // 计算从帧开始到DMA完成的时间间隔
        uint64_t dma_complete_time = time_us_64();
        int32_t frame_to_dma_interval = 0;
        if (frame_start_time != 0)
        {
            int64_t interval = (int64_t)dma_complete_time - (int64_t)frame_start_time;
            // 确保间隔在合理范围内（避免异常值）
            if (interval >= 0 && interval <= 100000) // 最大100毫秒
            {
                frame_to_dma_interval = (int32_t)interval;
            }
        }

        // 重置PIO状态机确保同步
        // pio_sm_set_enabled(pio_instance, pio_sm, false);
        // pio_sm_clear_fifos(pio_instance, pio_sm);
        // pio_sm_restart(pio_instance, pio_sm);
        // pio_sm_set_enabled(pio_instance, pio_sm, true);

        critical_section_enter_blocking(&buffer_mutex);

        // 完成当前缓冲区
        frame_buffers[active_buffer].capturing = false;
        frame_buffers[active_buffer].ready = true;
        frame_buffers[active_buffer].frame_id = ++frame_counter;
        frame_buffers[active_buffer].timestamp_us = time_us_64();
        frame_buffers[active_buffer].frame_to_dma_interval_us = frame_to_dma_interval;

        // 三重缓冲区轮换：完成的缓冲区变成新的display_buffer
        uint8_t completed_buffer = active_buffer;

        // 寻找下一个可用的缓冲区作为新的active_buffer
        // (不能是render_buffer，因为显示系统可能在使用)
        for (int i = 0; i < 3; i++)
        {
            if (i != render_buffer && i != completed_buffer)
            {
                active_buffer = i;
                break;
            }
        }

        display_buffer = completed_buffer;

        // 准备下一个缓冲区
        frame_buffers[active_buffer].capturing = true;
        frame_buffers[active_buffer].ready = false;
        // 重新配置DMA到新缓冲区
        dma_channel_set_write_addr(dma_channel, frame_buffers[active_buffer].data, false);
        dma_channel_set_trans_count(dma_channel, LCD_FRAME_SIZE / 4, true);

        critical_section_exit(&buffer_mutex);
    }
}

// 初始化自动捕获DMA系统
bool lcd_framebuffer_init_auto_capture(PIO pio, uint sm)
{
    if (!framebuffer_initialized || auto_capture_enabled)
    {
        return false;
    }

    pio_instance = pio;
    pio_sm = sm;

    // 配置DMA通道
    dma_channel_config config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_dreq(&config, pio_get_dreq(pio, sm, false));
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);

    // 设置初始传输到活动缓冲区
    critical_section_enter_blocking(&buffer_mutex);
    frame_buffers[active_buffer].capturing = true;
    frame_buffers[active_buffer].ready = false;
    frame_buffers[active_buffer].timestamp_us = time_us_64();

    dma_channel_configure(
        dma_channel, &config,
        frame_buffers[active_buffer].data,
        &pio->rxf[sm],
        LCD_FRAME_SIZE / 4,
        false);
    critical_section_exit(&buffer_mutex);

    // 设置DMA中断
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_capture_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    auto_capture_enabled = true;
    return true;
}

// DMA控制函数组
bool lcd_framebuffer_start_auto_capture(void)
{
    if (!auto_capture_enabled)
        return false;

    // 先启动DMA，确保数据接收准备就绪
    dma_channel_start(dma_channel);

    // 再启动PIO状态机，避免数据丢失
    pio_sm_set_enabled(pio_instance, pio_sm, true);

    printf("✅ DMA和PIO状态机按正确顺序启动完成\n");
    return true;
}

bool lcd_framebuffer_stop_auto_capture(void)
{
    if (!auto_capture_enabled)
        return false;

    dma_channel_abort(dma_channel);
    dma_channel_set_irq0_enabled(dma_channel, false);

    critical_section_enter_blocking(&buffer_mutex);
    frame_buffers[active_buffer].capturing = false;
    critical_section_exit(&buffer_mutex);

    auto_capture_enabled = false;
    return true;
}

bool lcd_framebuffer_is_auto_capturing(void)
{
    return auto_capture_enabled && dma_channel_is_busy(dma_channel);
}

uint32_t lcd_framebuffer_get_frame_count(void) { return frame_counter; }

// 准备安全的显示帧 (三重缓冲指针轮换，无数据拷贝)
bool lcd_framebuffer_prepare_display_frame(void)
{
    if (!framebuffer_initialized)
        return false;

    critical_section_enter_blocking(&buffer_mutex);

    // 检查显示缓冲区是否有新数据
    if (!frame_buffers[display_buffer].ready)
    {
        critical_section_exit(&buffer_mutex);
        return false;
    }

    // 简单的指针轮换：display_buffer -> render_buffer
    // 无需数据拷贝，零开销
    render_buffer = display_buffer;

    // 现在render_buffer指向有效数据，保持ready状态
    // 不需要修改ready标志，因为数据本来就是ready的

    // 注意：不要设置frame_buffers[display_buffer].ready = false
    // 因为render_buffer现在指向同一个缓冲区

    critical_section_exit(&buffer_mutex);
    return true;
}

// =============================================================================
// 显示缓冲区管理和像素访问 (集中管理)
// =============================================================================
// 检查渲染缓冲区是否就绪（由prepare_display_frame准备）
bool lcd_framebuffer_is_render_ready(void)
{
    return framebuffer_initialized && frame_buffers[render_buffer].ready;
}
// 高性能直接数据访问（用于优化显示）
const uint8_t *lcd_framebuffer_get_render_data(void)
{
    if (!framebuffer_initialized)
        return NULL;

    const internal_framebuffer_t *buffer = &frame_buffers[render_buffer];
    if (!buffer->ready)
        return NULL;

    return buffer->data;
}

// 获取帧时序信息用于偏移检测
int32_t lcd_framebuffer_get_frame_to_dma_interval(void)
{
    if (!framebuffer_initialized)
        return 0;

    const internal_framebuffer_t *buffer = &frame_buffers[render_buffer];
    if (!buffer->ready)
        return 0;

    return buffer->frame_to_dma_interval_us;
}

// 重启PIO状态机和DMA系统（错误恢复）
bool lcd_framebuffer_reset_capture_system(void)
{
    if (!framebuffer_initialized || !auto_capture_enabled)
        return false;

    printf("⚠️  检测到帧异常，正在重启捕获系统...\n");

    critical_section_enter_blocking(&buffer_mutex);

    // 1. 停止并重置DMA
    dma_channel_abort(dma_channel);
    dma_channel_set_irq0_enabled(dma_channel, false);

    // 2. 停止PIO状态机
    pio_sm_set_enabled(pio_instance, pio_sm, false);

    // 3. 清除所有PIO状态
    pio_interrupt_clear(pio_instance, 0);
    pio_interrupt_clear(pio_instance, 1);
    pio_sm_clear_fifos(pio_instance, pio_sm);

    // 4. 彻底重启PIO状态机到初始状态
    pio_sm_restart(pio_instance, pio_sm);

    // 5. 手动跳转到wait_frame (确保从等待FRAME开始)
    pio_sm_exec(pio_instance, pio_sm, pio_encode_jmp(0));

    // 6. 清理缓冲区状态
    for (int i = 0; i < 3; i++)
    {
        frame_buffers[i].capturing = false;
        frame_buffers[i].ready = false;
        frame_buffers[i].frame_to_dma_interval_us = 0;
    }

    // 7. 重置缓冲区指针到初始状态
    active_buffer = 0;
    display_buffer = 1;
    render_buffer = 2;

    // 8. 重新配置DMA到活动缓冲区
    dma_channel_config config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_dreq(&config, pio_get_dreq(pio_instance, pio_sm, false));
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);

    dma_channel_configure(
        dma_channel, &config,
        frame_buffers[active_buffer].data,
        &pio_instance->rxf[pio_sm],
        LCD_FRAME_SIZE / 4,
        false);

    // 9. 标记活动缓冲区为捕获状态
    frame_buffers[active_buffer].capturing = true;
    frame_buffers[active_buffer].ready = false;

    // 10. 重新启用DMA中断
    dma_channel_set_irq0_enabled(dma_channel, true);

    // 11. 先启动DMA（确保数据接收准备就绪）
    dma_channel_start(dma_channel);

    // 12. 重新启用PIO帧中断
    pio_set_irq0_source_enabled(pio_instance, pis_interrupt0, true);

    // 13. 最后启动PIO状态机（此时DMA已经准备好接收数据）
    pio_sm_set_enabled(pio_instance, pio_sm, true);

    // 14. 重置时间戳
    frame_start_time = 0;
    last_dma_complete_time = 0;

    critical_section_exit(&buffer_mutex);

    printf("✅ 捕获系统重启完成，恢复正常工作\n");
    return true;
}

// 等待LCD电源开关信号 (GPIO 1)
void wait_for_lcd_power_on(void)
{
    // 初始化GPIO 1为输入模式
    gpio_init(1);
    gpio_set_dir(1, GPIO_IN);
    gpio_set_pulls(1, false, true); // 启用下拉电阻

    printf("等待LCD开关信号 (GPIO 1) 变为高电平...\n");
    printf("请确保LCD设备已通电并开启\n");

    // 等待GPIO 1变为高电平
    while (!gpio_get(1))
    {
        sleep_ms(100); // 每100ms检查一次
        printf(".");   // 显示等待进度
        fflush(stdout);
    }

    printf("\n✅ LCD开关信号检测到高电平，LCD已准备就绪\n");
    set_lcd_backlight_brightness(0.8);
    printf("📱 SPI LCD背光PWM已开启 (80%%亮度)\n");
    printf("开始启动捕获系统...\n");
}

// 初始化PWM输出
void init_pwm_output(uint gpio, float freq_hz, float duty_cycle)
{
    // 设置GPIO为PWM功能
    gpio_set_function(gpio, GPIO_FUNC_PWM);

    // 获取PWM切片编号
    uint slice_num = pwm_gpio_to_slice_num(gpio);

    // 计算分频器和顶值以达到目标频率
    // 系统时钟通常是125MHz
    float clock_freq = 125000000.0f;

    // 计算所需的总计数值 (分频器 * wrap值)
    float total_counts = clock_freq / freq_hz;

    // 选择合适的分频器和wrap值
    uint16_t divider = 1;
    uint16_t wrap = (uint16_t)(total_counts / divider);

    // 如果wrap太大，增加分频器
    while (wrap > 65535 && divider < 255)
    {
        divider++;
        wrap = (uint16_t)(total_counts / divider);
    }

    // 配置PWM
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, (float)divider);
    pwm_config_set_wrap(&config, wrap);

    // 应用配置
    pwm_init(slice_num, &config, true);

    // 设置初始占空比
    set_pwm_duty_cycle(gpio, duty_cycle);

    printf("📡 PWM初始化完成: GPIO %u, 频率=%.1f Hz, 占空比=%.1f%%, 分频器=%u, wrap=%u\n",
           gpio, freq_hz, duty_cycle * 100.0f, divider, wrap);
}

// 设置PWM占空比
void set_pwm_duty_cycle(uint gpio, float duty_cycle)
{
    // 限制占空比范围 0-1
    if (duty_cycle < 0.0f)
        duty_cycle = 0.0f;
    if (duty_cycle > 1.0f)
        duty_cycle = 1.0f;

    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);

    // 获取当前wrap值
    uint16_t wrap = pwm_hw->slice[slice_num].top + 1;

    // 计算占空比对应的计数值
    uint16_t level = (uint16_t)(duty_cycle * wrap);

    // 设置PWM电平
    pwm_set_chan_level(slice_num, channel, level);
}

// LCD背光亮度控制 (GPIO 21)
void set_lcd_backlight_brightness(float brightness)
{
    // 限制亮度范围 0-1
    if (brightness < 0.0f)
        brightness = 0.0f;
    if (brightness > 1.0f)
        brightness = 1.0f;

    // 调用通用PWM控制函数
    set_pwm_duty_cycle(21, brightness);
}
