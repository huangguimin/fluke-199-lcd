#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include <stdint.h>
#include <stdbool.h>

// 帧统计结构体
typedef struct {
    uint32_t frame_count;
    uint32_t total_conversion_time;
    uint32_t total_transfer_time;
    uint32_t total_frames;
    uint32_t last_print_time_ms;
    const char* display_name;  // 显示器名称，如"ST7789"或"ST75320"
    float data_size_kb;        // 数据大小 (KB)
} frame_stats_t;

// 初始化帧统计
void frame_stats_init(frame_stats_t* stats, const char* display_name, float data_size_kb);

// 更新帧统计并可选择性打印
void frame_stats_update(frame_stats_t* stats,
                       uint32_t conversion_time_us,
                       uint32_t transfer_time_us,
                       bool used_dma);

// 强制打印当前统计信息
void frame_stats_print_now(frame_stats_t* stats, bool used_dma);

// 重置统计信息
void frame_stats_reset(frame_stats_t* stats);

#endif // FRAME_STATS_H