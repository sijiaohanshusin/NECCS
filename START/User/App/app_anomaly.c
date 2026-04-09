/**
 * @file    app_anomaly.c
 * @brief   异常声音检测与历史日志实现
 */
#include "app_anomaly.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/** @brief 默认异常阈值（噪声底的 3 倍视为异常） */
#define ANOMALY_DEFAULT_THRESHOLD  3.0f

/** @brief 冷却帧数（避免同一事件重复记录） */
#define ANOMALY_COOLDOWN_FRAMES    10u

/** @brief 环形日志缓冲 */
static App_AnomalyEntry_t s_log[APP_ANOMALY_LOG_SIZE];

/** @brief 写指针（下一次写入的位置） */
static uint32_t s_write_idx = 0u;

/** @brief 已写入条目总数（含覆盖） */
static uint32_t s_total_count = 0u;

/** @brief 异常阈值倍数 */
static float s_threshold = ANOMALY_DEFAULT_THRESHOLD;

/** @brief 使能标志 */
static uint8_t s_enabled = 0u;

/** @brief 冷却计数器 */
static uint32_t s_cooldown = 0u;

void App_Anomaly_Init(void)
{
    (void)memset(s_log, 0, sizeof(s_log));
    s_write_idx = 0u;
    s_total_count = 0u;
    s_threshold = ANOMALY_DEFAULT_THRESHOLD;
    s_enabled = 0u;
    s_cooldown = 0u;
}

uint8_t App_Anomaly_Feed(const float *magnitude, const float *floor,
                         uint16_t bin_count,
                         float x_angle, float y_angle, float energy)
{
    uint16_t i;
    float max_dev = 0.0f;
    uint16_t max_bin = 0u;
    float ratio;

    if (s_enabled == 0u)
    {
        return 0u;
    }

    if ((magnitude == NULL) || (floor == NULL) || (bin_count == 0u))
    {
        return 0u;
    }

    /* 冷却中，递减后跳过 */
    if (s_cooldown > 0u)
    {
        s_cooldown--;
        return 0u;
    }

    /* 找到偏离最大的 bin */
    for (i = 0u; i < bin_count; i++)
    {
        if (floor[i] > 1.0e-10f)
        {
            ratio = magnitude[i] / floor[i];
            if (ratio > max_dev)
            {
                max_dev = ratio;
                max_bin = i;
            }
        }
    }

    /* 判定异常 */
    if (max_dev >= s_threshold)
    {
        App_AnomalyEntry_t *entry = &s_log[s_write_idx];
        entry->tick      = (uint32_t)xTaskGetTickCount();
        entry->x_angle   = x_angle;
        entry->y_angle   = y_angle;
        entry->energy    = energy;
        entry->deviation = max_dev;
        entry->peak_bin  = max_bin;
        entry->reserved  = 0u;

        s_write_idx++;
        if (s_write_idx >= APP_ANOMALY_LOG_SIZE)
        {
            s_write_idx = 0u;
        }
        s_total_count++;
        s_cooldown = ANOMALY_COOLDOWN_FRAMES;
        return 1u;
    }

    return 0u;
}

void App_Anomaly_SetThreshold(float ratio)
{
    if (ratio > 1.0f)
    {
        s_threshold = ratio;
    }
}

float App_Anomaly_GetThreshold(void)
{
    return s_threshold;
}

uint8_t App_Anomaly_GetEntry(uint32_t index, App_AnomalyEntry_t *entry)
{
    uint32_t valid;
    uint32_t actual_idx;

    if (entry == NULL)
    {
        return 0u;
    }

    valid = (s_total_count < APP_ANOMALY_LOG_SIZE) ? s_total_count : APP_ANOMALY_LOG_SIZE;
    if (index >= valid)
    {
        return 0u;
    }

    /* index=0 表示最新，映射到 (s_write_idx - 1 - index) */
    if (s_write_idx >= (index + 1u))
    {
        actual_idx = s_write_idx - 1u - index;
    }
    else
    {
        actual_idx = APP_ANOMALY_LOG_SIZE + s_write_idx - 1u - index;
    }

    *entry = s_log[actual_idx];
    return 1u;
}

uint32_t App_Anomaly_GetCount(void)
{
    if (s_total_count < APP_ANOMALY_LOG_SIZE)
    {
        return s_total_count;
    }
    return APP_ANOMALY_LOG_SIZE;
}

void App_Anomaly_ClearLog(void)
{
    (void)memset(s_log, 0, sizeof(s_log));
    s_write_idx = 0u;
    s_total_count = 0u;
}

void App_Anomaly_SetEnabled(uint8_t enable)
{
    s_enabled = (enable != 0u) ? 1u : 0u;
}

uint8_t App_Anomaly_GetEnabled(void)
{
    return s_enabled;
}
