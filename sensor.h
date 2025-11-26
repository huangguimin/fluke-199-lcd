#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化传感器模块
 *
 * 初始化ADC0和GPIO13占空比检测(使用PIO)
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool sensor_init(void);

/**
 * @brief 读取ADC0电压值
 *
 * @return float 电压值(伏特), 范围 0.0 ~ 3.3V
 */
float sensor_read_adc0_voltage(void);

/**
 * @brief 读取ADC0原始值
 *
 * @return uint16_t 12位ADC原始值 (0-4095)
 */
uint16_t sensor_read_adc0_raw(void);

/**
 * @brief 获取GPIO13上20KHz信号的占空比
 *
 * @return float 占空比百分比 (0.0 ~ 100.0), 如果无效返回 -1.0
 */
float sensor_get_duty_cycle(void);

/**
 * @brief 获取GPIO13信号频率
 *
 * @return float 频率(Hz), 如果无效返回 0.0
 */
float sensor_get_frequency(void);

/**
 * @brief 检查占空比数据是否有效
 *
 * @return true 数据有效
 * @return false 数据无效或超时
 */
bool sensor_duty_cycle_valid(void);

/**
 * @brief 获取滤波后的ADC电压值
 *
 * @return float 滤波后的电压值(伏特)
 */
float sensor_get_filtered_voltage(void);

/**
 * @brief 获取滤波后的占空比
 *
 * @return float 滤波后的占空比百分比 (0.0 ~ 100.0)
 */
float sensor_get_filtered_duty_cycle(void);

#endif // SENSOR_H
