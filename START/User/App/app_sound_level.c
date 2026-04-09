/**
 * @file    app_sound_level.c
 * @brief   声级计模式实现 —— A/C/Z 加权 + Leq 积分
 * @details A 加权曲线使用简化公式近似 IEC 61672，
 *          基于频谱幅度数据计算加权声级。
 */
#include "app_sound_level.h"

#include <math.h>
#include <string.h>

/** @brief 参考声压级偏移 (假设满幅 = 94 dB SPL) */
#define SLM_DB_REF_OFFSET  94.0f

/** @brief 最小 dB 值（下限钳位） */
#define SLM_DB_FLOOR       (-80.0f)

/** @brief 当前加权类型 */
static App_SLM_Weight_t s_weight = APP_SLM_WEIGHT_A;

/** @brief 瞬时声级 (dB) */
static float s_db_inst = SLM_DB_FLOOR;

/** @brief Leq 累积的线性功率总和 */
static double s_leq_sum = 0.0;

/** @brief Leq 积分帧数 */
static uint32_t s_leq_frames = 0u;

/** @brief 最大声级 */
static float s_db_max = SLM_DB_FLOOR;

/** @brief 最小声级 */
static float s_db_min = 0.0f;

/**
 * @brief  计算 A 加权修正值 (dB)
 * @param  f 频率 (Hz)
 * @return A 加权修正量 (负值表示衰减)
 * @details 使用 IEC 61672 简化公式：
 *   RA(f) = 12194^2 * f^4 / ((f^2+20.6^2)(f^2+12194^2)*sqrt((f^2+107.7^2)(f^2+737.9^2)))
 *   A(f) = 20*log10(RA(f)) + 2.0 dB
 */
static float s_a_weight_db(float f)
{
    float f2 = f * f;
    float num;
    float den;
    float ra_sq;

    if (f < 10.0f)
    {
        return -70.0f; /* 极低频衰减 */
    }

    num = 148693636.0f * f2 * f2;  /* 12194^2 * f^4 */
    den = (f2 + 424.36f)           /* f^2 + 20.6^2 */
        * (f2 + 148693636.0f);     /* f^2 + 12194^2 */

    /* sqrt((f^2+107.7^2)(f^2+737.9^2)) */
    {
        float t1 = f2 + 11599.29f;  /* 107.7^2 */
        float t2 = f2 + 544496.41f; /* 737.9^2 */
        den *= sqrtf(t1 * t2);
    }

    if (den < 1.0e-20f)
    {
        return -70.0f;
    }

    ra_sq = num / den;
    return 20.0f * log10f(ra_sq) + 2.0f;
}

/**
 * @brief  计算 C 加权修正值 (dB)
 * @param  f 频率 (Hz)
 * @return C 加权修正量
 */
static float s_c_weight_db(float f)
{
    float f2 = f * f;
    float num;
    float den;
    float rc_sq;

    if (f < 10.0f)
    {
        return -40.0f;
    }

    num = 148693636.0f * f2;       /* 12194^2 * f^2 */
    den = (f2 + 424.36f)           /* f^2 + 20.6^2 */
        * (f2 + 148693636.0f);     /* f^2 + 12194^2 */

    if (den < 1.0e-20f)
    {
        return -40.0f;
    }

    rc_sq = num / den;
    return 20.0f * log10f(rc_sq) + 0.06f;
}

void App_SLM_Init(void)
{
    s_weight = APP_SLM_WEIGHT_A;
    s_db_inst = SLM_DB_FLOOR;
    s_leq_sum = 0.0;
    s_leq_frames = 0u;
    s_db_max = SLM_DB_FLOOR;
    s_db_min = 0.0f;
}

void App_SLM_Feed(const float *magnitude, uint16_t bin_count, float delta_f)
{
    uint16_t i;
    float freq;
    float weight_db;
    float mag_w;
    float power_sum = 0.0f;
    float db;

    if ((magnitude == NULL) || (bin_count == 0u) || (delta_f <= 0.0f))
    {
        return;
    }

    /* 从 bin 1 开始（跳过 DC） */
    for (i = 1u; i < bin_count; i++)
    {
        freq = (float)i * delta_f;

        switch (s_weight)
        {
        case APP_SLM_WEIGHT_A:
            weight_db = s_a_weight_db(freq);
            break;
        case APP_SLM_WEIGHT_C:
            weight_db = s_c_weight_db(freq);
            break;
        default: /* Z 加权 */
            weight_db = 0.0f;
            break;
        }

        /* 将 dB 修正转为线性增益 */
        mag_w = magnitude[i] * powf(10.0f, weight_db / 20.0f);
        power_sum += mag_w * mag_w;
    }

    /* 转 dB (功率 → dB) */
    if (power_sum > 1.0e-20f)
    {
        db = 10.0f * log10f(power_sum) + SLM_DB_REF_OFFSET;
    }
    else
    {
        db = SLM_DB_FLOOR;
    }

    s_db_inst = db;

    /* 更新峰值 */
    if (db > s_db_max)
    {
        s_db_max = db;
    }
    if ((s_leq_frames == 0u) || (db < s_db_min))
    {
        s_db_min = db;
    }

    /* Leq 积分：线性功率累加 */
    if (power_sum > 0.0f)
    {
        s_leq_sum += (double)power_sum;
    }
    s_leq_frames++;
}

void App_SLM_GetReading(App_SLM_Reading_t *reading)
{
    if (reading == NULL)
    {
        return;
    }

    reading->db_inst = s_db_inst;
    reading->db_max = s_db_max;
    reading->db_min = s_db_min;
    reading->leq_frames = s_leq_frames;

    /* Leq = 10*log10(1/N * Σ p²) + offset */
    if ((s_leq_frames > 0u) && (s_leq_sum > 0.0))
    {
        reading->db_leq = 10.0f * (float)log10(s_leq_sum / (double)s_leq_frames)
                        + SLM_DB_REF_OFFSET;
    }
    else
    {
        reading->db_leq = SLM_DB_FLOOR;
    }
}

void App_SLM_SetWeight(App_SLM_Weight_t weight)
{
    if (weight <= APP_SLM_WEIGHT_Z)
    {
        s_weight = weight;
    }
}

App_SLM_Weight_t App_SLM_GetWeight(void)
{
    return s_weight;
}

void App_SLM_ResetLeq(void)
{
    s_leq_sum = 0.0;
    s_leq_frames = 0u;
}

void App_SLM_ResetPeak(void)
{
    s_db_max = SLM_DB_FLOOR;
    s_db_min = 0.0f;
}
