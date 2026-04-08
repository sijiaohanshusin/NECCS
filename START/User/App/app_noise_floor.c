/**
 * @file    app_noise_floor.c
 * @brief   自适应背景噪声底估计与频域减噪实现
 */
#include "app_noise_floor.h"

#include "app_user_config.h"

#include <string.h>

/** @brief 最大支持的 bin 数 */
#define NOISE_FLOOR_MAX_BINS  (FRAME_LEN / 2u)

/** @brief 默认 EMA 系数 */
#define NOISE_FLOOR_DEFAULT_ALPHA  0.02f

/** @brief 噪声底估计数组 */
static float s_floor[NOISE_FLOOR_MAX_BINS];

/** @brief EMA 平滑系数 */
static float s_alpha = NOISE_FLOOR_DEFAULT_ALPHA;

/** @brief 是否已初始化（第一帧直接赋值） */
static uint8_t s_initialized = 0u;

/** @brief 噪声减除使能 */
static uint8_t s_enabled = 0u;

void App_NoiseFloor_Init(void)
{
    (void)memset(s_floor, 0, sizeof(s_floor));
    s_alpha = NOISE_FLOOR_DEFAULT_ALPHA;
    s_initialized = 0u;
    s_enabled = 0u;
}

void App_NoiseFloor_Update(const float *magnitude, uint16_t bin_count)
{
    uint16_t i;
    uint16_t n;

    if (magnitude == NULL)
    {
        return;
    }

    n = (bin_count > NOISE_FLOOR_MAX_BINS) ? NOISE_FLOOR_MAX_BINS : bin_count;

    if (s_initialized == 0u)
    {
        /* 第一帧：直接赋值 */
        for (i = 0u; i < n; i++)
        {
            s_floor[i] = magnitude[i];
        }
        s_initialized = 1u;
        return;
    }

    /* EMA 更新：取 min(current, ema) 的思路确保噪声底紧贴最低值 */
    for (i = 0u; i < n; i++)
    {
        if (magnitude[i] < s_floor[i])
        {
            /* 快速下降：直接跟踪 */
            s_floor[i] = magnitude[i];
        }
        else
        {
            /* 缓慢上升：EMA */
            s_floor[i] = s_floor[i] * (1.0f - s_alpha) + magnitude[i] * s_alpha;
        }
    }
}

void App_NoiseFloor_Get(float *out_floor, uint16_t bin_count)
{
    uint16_t n;

    if (out_floor == NULL)
    {
        return;
    }

    n = (bin_count > NOISE_FLOOR_MAX_BINS) ? NOISE_FLOOR_MAX_BINS : bin_count;
    (void)memcpy(out_floor, s_floor, (size_t)n * sizeof(float));
}

void App_NoiseFloor_Calibrate(const float *magnitude, uint16_t bin_count)
{
    uint16_t i;
    uint16_t n;

    if (magnitude == NULL)
    {
        return;
    }

    n = (bin_count > NOISE_FLOOR_MAX_BINS) ? NOISE_FLOOR_MAX_BINS : bin_count;
    for (i = 0u; i < n; i++)
    {
        s_floor[i] = magnitude[i];
    }
    s_initialized = 1u;
}

void App_NoiseFloor_SetAlpha(float alpha)
{
    if ((alpha > 0.0f) && (alpha < 1.0f))
    {
        s_alpha = alpha;
    }
}

float App_NoiseFloor_GetAlpha(void)
{
    return s_alpha;
}

void App_NoiseFloor_SetEnabled(uint8_t enable)
{
    s_enabled = (enable != 0u) ? 1u : 0u;
}

uint8_t App_NoiseFloor_GetEnabled(void)
{
    return s_enabled;
}
