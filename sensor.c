#include "sensor.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/sync.h"
#include "duty_cycle.pio.h"
#include <stdio.h>

// ADC 配置
#define ADC_CHANNEL 0        // ADC0 对应 GPIO26
#define ADC_VREF 3.3f        // ADC 参考电压
#define ADC_RESOLUTION 4096  // 12位 ADC

// 占空比检测配置
#define DUTY_CYCLE_GPIO 13
#define EXPECTED_FREQ 20000  // 20KHz
#define MEASUREMENT_TIMEOUT_US 100000  // 100ms 超时

// PIO 配置
static PIO pio = NULL;
static uint sm = 0;
static volatile uint32_t high_cycles = 0;
static volatile uint32_t total_cycles = 0;
static volatile uint64_t last_update_time = 0;
static critical_section_t duty_cycle_mutex;

bool sensor_init(void) {
    printf("传感器初始化: ADC0(GPIO26) + 占空比测量(GPIO%d, PIO1)...\n", DUTY_CYCLE_GPIO);

    // 初始化 ADC
    adc_init();
    adc_gpio_init(26);  // GPIO26 是 ADC0
    adc_select_input(ADC_CHANNEL);

    // 初始化互斥锁
    critical_section_init(&duty_cycle_mutex);

    // 使用 PIO1（避免与 lcd_capture 的 PIO0 冲突）
    pio = pio1;

    // 尝试申请状态机
    int sm_result = pio_claim_unused_sm(pio, false);
    if (sm_result < 0) {
        printf("错误: PIO1 没有可用的状态机\n");
        return false;
    }
    sm = (uint)sm_result;

    // 检查是否能加载 PIO 程序
    if (!pio_can_add_program(pio, &duty_cycle_measure_program)) {
        printf("错误: PIO1 空间不足\n");
        pio_sm_unclaim(pio, sm);
        return false;
    }

    // 加载 PIO 程序
    uint offset = pio_add_program(pio, &duty_cycle_measure_program);

    // 使用生成的初始化函数
    duty_cycle_measure_program_init(pio, sm, offset, DUTY_CYCLE_GPIO);

    // 等待 PIO 稳定
    sleep_ms(10);

    printf("传感器初始化完成\n");

    return true;
}

float sensor_read_adc0_voltage(void) {
    uint16_t raw = sensor_read_adc0_raw();
    return (raw * ADC_VREF) / ADC_RESOLUTION;
}

uint16_t sensor_read_adc0_raw(void) {
    adc_select_input(ADC_CHANNEL);
    return adc_read();
}

float sensor_get_duty_cycle(void) {
    static uint32_t debug_read_count = 0;
    static bool first_read = true;

    // 检查 FIFO 状态（调试已关闭）
    if (first_read) {
        first_read = false;
    }

    // 尝试从 PIO FIFO 读取新数据
    while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        uint32_t high_raw = pio_sm_get(pio, sm);  // 读取高电平计数

        // 等待低电平计数也准备好
        if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            uint32_t low_raw = pio_sm_get(pio, sm);   // 读取低电平计数

            critical_section_enter_blocking(&duty_cycle_mutex);

            // PIO 从 0xFFFFFFFF 倒计数，计算实际周期数
            high_cycles = 0xFFFFFFFF - high_raw;
            uint32_t low_cycles = 0xFFFFFFFF - low_raw;
            total_cycles = high_cycles + low_cycles;

            last_update_time = time_us_64();

            critical_section_exit(&duty_cycle_mutex);

            // 调试信息已关闭
            debug_read_count++;
        } else {
            // FIFO 数据不完整，等待下次
            break;
        }
    }

    if (!sensor_duty_cycle_valid()) {
        return -1.0f;
    }

    critical_section_enter_blocking(&duty_cycle_mutex);
    uint32_t h_cyc = high_cycles;
    uint32_t t_cyc = total_cycles;
    critical_section_exit(&duty_cycle_mutex);

    if (t_cyc == 0) {
        return -1.0f;
    }

    // 占空比 = 高电平时间 / 总周期时间 * 100
    float duty = ((float)h_cyc / (float)t_cyc) * 100.0f;

    // 修正：如果占空比 > 50%，说明高低电平测反了
    if (duty > 50.0f) {
        duty = 100.0f - duty;
    }

    return duty;
}

float sensor_get_frequency(void) {
    // 先更新数据
    sensor_get_duty_cycle();

    if (!sensor_duty_cycle_valid()) {
        return 0.0f;
    }

    critical_section_enter_blocking(&duty_cycle_mutex);
    uint32_t t_cyc = total_cycles;
    critical_section_exit(&duty_cycle_mutex);

    if (t_cyc == 0) {
        return 0.0f;
    }

    // 频率 = (系统时钟 / 分频系数) / (总周期计数 * 每次循环的指令数)
    // PIO 程序每个循环执行 2 条指令 (jmp pin + jmp x--)
    uint32_t clock_freq = clock_get_hz(clk_sys);
    float pio_clock = clock_freq / 4.0f;  // 分频系数是 4
    float freq = pio_clock / ((float)t_cyc * 2.0f);  // 每次循环 2 个时钟周期

    return freq;
}

bool sensor_duty_cycle_valid(void) {
    uint64_t current_time = time_us_64();
    uint64_t last_time;

    critical_section_enter_blocking(&duty_cycle_mutex);
    last_time = last_update_time;
    critical_section_exit(&duty_cycle_mutex);

    // 检查数据是否在超时时间内更新
    return (last_time > 0) && ((current_time - last_time) < MEASUREMENT_TIMEOUT_US);
}

// 滤波器配置
#define VOLTAGE_FILTER_SIZE 16    // 电压滤波窗口（增强滤波）
#define DUTY_CYCLE_FILTER_SIZE 3  // 占空比滤波窗口（快速响应）

static float voltage_buffer[VOLTAGE_FILTER_SIZE] = {0};
static float duty_cycle_buffer[DUTY_CYCLE_FILTER_SIZE] = {0};
static uint8_t voltage_filter_index = 0;
static uint8_t duty_filter_index = 0;
static bool voltage_filter_filled = false;
static bool duty_filter_filled = false;

float sensor_get_filtered_voltage(void) {
    // 读取新的电压值
    float new_voltage = sensor_read_adc0_voltage();

    // 存入环形缓冲区
    voltage_buffer[voltage_filter_index] = new_voltage;

    // 更新索引
    voltage_filter_index++;
    if (voltage_filter_index >= VOLTAGE_FILTER_SIZE) {
        voltage_filter_index = 0;
        voltage_filter_filled = true;
    }

    // 计算平均值
    float sum = 0.0f;
    uint8_t count = voltage_filter_filled ? VOLTAGE_FILTER_SIZE : voltage_filter_index;

    for (uint8_t i = 0; i < count; i++) {
        sum += voltage_buffer[i];
    }

    return sum / count;
}

float sensor_get_filtered_duty_cycle(void) {
    // 读取新的占空比值
    float new_duty = sensor_get_duty_cycle();

    // 如果无效，返回上次滤波结果
    if (new_duty < 0.0f) {
        if (!duty_filter_filled && duty_filter_index == 0) {
            return -1.0f;  // 还没有任何有效数据
        }
        // 使用上次的值
        uint8_t last_idx = (duty_filter_index == 0) ? (DUTY_CYCLE_FILTER_SIZE - 1) : (duty_filter_index - 1);
        return duty_cycle_buffer[last_idx];
    }

    // 存入环形缓冲区
    duty_cycle_buffer[duty_filter_index] = new_duty;

    // 更新索引
    duty_filter_index++;
    if (duty_filter_index >= DUTY_CYCLE_FILTER_SIZE) {
        duty_filter_index = 0;
        duty_filter_filled = true;
    }

    // 使用众数滤波（占空比只有两个固定值）
    uint8_t count = duty_filter_filled ? DUTY_CYCLE_FILTER_SIZE : duty_filter_index;

    // 简单策略：计算平均值，更快响应
    float sum = 0.0f;
    for (uint8_t i = 0; i < count; i++) {
        sum += duty_cycle_buffer[i];
    }

    return sum / count;
}
