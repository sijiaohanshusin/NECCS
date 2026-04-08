/**
 * @file    app_trigger.c
 * @brief   瞬态捕捉触发模式 —— 状态机实现
 */
#include "app_trigger.h"

#include <math.h>

/** @brief 默认触发阈值（能量变化 15%） */
#define TRIGGER_DEFAULT_THRESHOLD  0.15f

/** @brief EMA 平滑系数（用于计算基线能量） */
#define TRIGGER_EMA_ALPHA          0.05f

/** @brief 当前状态 */
static App_TriggerState_t s_state = APP_TRIGGER_IDLE;

/** @brief 触发阈值 */
static float s_threshold = TRIGGER_DEFAULT_THRESHOLD;

/** @brief EMA 基线能量 */
static float s_baseline = 0.0f;

/** @brief 基线是否已稳定 */
static uint8_t s_baseline_valid = 0u;

/** @brief 稳定计数 */
static uint32_t s_warmup_count = 0u;

/** @brief 稳定帧数要求 */
#define TRIGGER_WARMUP_FRAMES 20u

void App_Trigger_Init(void)
{
    s_state = APP_TRIGGER_IDLE;
    s_threshold = TRIGGER_DEFAULT_THRESHOLD;
    s_baseline = 0.0f;
    s_baseline_valid = 0u;
    s_warmup_count = 0u;
}

void App_Trigger_Arm(void)
{
    s_state = APP_TRIGGER_ARMED;
    s_baseline = 0.0f;
    s_baseline_valid = 0u;
    s_warmup_count = 0u;
}

void App_Trigger_Disarm(void)
{
    s_state = APP_TRIGGER_IDLE;
}

void App_Trigger_Rearm(void)
{
    if (s_state == APP_TRIGGER_TRIGGERED)
    {
        App_Trigger_Arm();
    }
}

uint8_t App_Trigger_Feed(float energy)
{
    float delta;

    if (s_state != APP_TRIGGER_ARMED)
    {
        return 0u;
    }

    /* 预热阶段：建立基线 */
    if (s_baseline_valid == 0u)
    {
        if (s_warmup_count == 0u)
        {
            s_baseline = energy;
        }
        else
        {
            s_baseline = s_baseline * (1.0f - TRIGGER_EMA_ALPHA)
                       + energy * TRIGGER_EMA_ALPHA;
        }
        s_warmup_count++;
        if (s_warmup_count >= TRIGGER_WARMUP_FRAMES)
        {
            s_baseline_valid = 1u;
        }
        return 0u;
    }

    /* 计算能量变化 */
    delta = energy - s_baseline;

    /* 更新基线（缓慢跟踪） */
    s_baseline = s_baseline * (1.0f - TRIGGER_EMA_ALPHA)
               + energy * TRIGGER_EMA_ALPHA;

    /* 检测突变 */
    if (delta > s_threshold)
    {
        s_state = APP_TRIGGER_TRIGGERED;
        return 1u;
    }

    return 0u;
}

App_TriggerState_t App_Trigger_GetState(void)
{
    return s_state;
}

void App_Trigger_SetThreshold(float threshold)
{
    if (threshold > 0.0f)
    {
        s_threshold = threshold;
    }
}

float App_Trigger_GetThreshold(void)
{
    return s_threshold;
}
