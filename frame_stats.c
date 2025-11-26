#include "frame_stats.h"
#include "pico/stdlib.h"
#include <stdio.h>

// 初始化帧统计
void frame_stats_init(frame_stats_t* stats, const char* display_name, float data_size_kb)
{
    if (stats == NULL) return;

    stats->frame_count = 0;
    stats->total_conversion_time = 0;
    stats->total_transfer_time = 0;
    stats->total_frames = 0;
    stats->last_print_time_ms = 0;
    stats->display_name = display_name;
    stats->data_size_kb = data_size_kb;
}

// 更新帧统计并可选择性打印
void frame_stats_update(frame_stats_t* stats,
                       uint32_t conversion_time_us,
                       uint32_t transfer_time_us,
                       bool used_dma)
{
    if (stats == NULL) return;

    // 累计统计数据
    stats->frame_count++;
    stats->total_conversion_time += conversion_time_us;
    stats->total_transfer_time += transfer_time_us;
    stats->total_frames++;

    // 检查是否需要打印（每秒一次）
    uint32_t current_time_ms = time_us_64() / 1000;   // 转换为毫秒
    if (current_time_ms - stats->last_print_time_ms >= 1000) // 1000ms = 1秒
    {
        frame_stats_print_now(stats, used_dma);

        // 更新时间并重置统计
        uint32_t time_duration = current_time_ms - stats->last_print_time_ms;
        stats->last_print_time_ms = current_time_ms;
        stats->frame_count = 0;
        stats->total_conversion_time = 0;
        stats->total_transfer_time = 0;
        stats->total_frames = 0;
    }
}

// 强制打印当前统计信息
void frame_stats_print_now(frame_stats_t* stats, bool used_dma)
{
    if (stats == NULL || stats->total_frames == 0) return;

    // 计算平均值
    uint32_t avg_conversion_time = stats->total_conversion_time / stats->total_frames;
    uint32_t avg_transfer_time = stats->total_transfer_time / stats->total_frames;
    uint32_t avg_total_time = avg_conversion_time + avg_transfer_time;
    float avg_transfer_speed_mbps = (stats->data_size_kb / 1024.0f) / (avg_transfer_time / 1000000.0f);

    uint32_t current_time_ms = time_us_64() / 1000;
    uint32_t time_duration = current_time_ms - stats->last_print_time_ms;
    if (time_duration == 0) time_duration = 1; // 避免除零

    printf("📊 %s 帧传输性能统计 (过去%lu帧, %lu秒):\n",
           stats->display_name, stats->frame_count, time_duration / 1000);
    printf("  • 平均转换: %luμs (1-bit数据处理)\n", avg_conversion_time);
    printf("  • 平均%s传输: %luμs (%.1fKB)\n",
           used_dma ? "DMA" : "SPI阻塞", avg_transfer_time, stats->data_size_kb);
    printf("  • 平均总耗时: %luμs, 平均速率: %.1fMB/s\n",
           avg_total_time, avg_transfer_speed_mbps);
    printf("  • 帧率: %.1f FPS, 数据处理: 240x240 ⇒ %.1fKB\n",
           (float)stats->frame_count * 1000.0f / time_duration, stats->data_size_kb);
}

// 重置统计信息
void frame_stats_reset(frame_stats_t* stats)
{
    if (stats == NULL) return;

    stats->frame_count = 0;
    stats->total_conversion_time = 0;
    stats->total_transfer_time = 0;
    stats->total_frames = 0;
    stats->last_print_time_ms = time_us_64() / 1000;
}