/**
 * @file    ai_bandpass.c
 * @brief   IIR 带通滤波器实现 —— 2阶 Butterworth (CMSIS-DSP biquad)
 * @details 每通道使用 2 级级联 DF1 biquad：Stage 0 = 高通，Stage 1 = 低通。
 *          系数由 2 阶 Butterworth 双线性变换公式实时计算。
 *          所有状态缓冲区位于 AXI SRAM（默认段），零初始化。
 */
#include "ai_bandpass.h"
#include "ai_config.h"
#include "arm_math.h"

#include <string.h>

/* ============================================================================
 * 内部常量
 * ============================================================================ */

/** @brief 采样率 (Hz) */
#define BP_SAMPLE_RATE      48000.0f

/** @brief Pi 常数 */
#define BP_PI               3.14159265358979f

/** @brief 每级 biquad 的系数数目 (b0, b1, b2, a1, a2) */
#define BP_COEFFS_PER_STAGE 5u

/** @brief 每级 biquad 的状态变量数 */
#define BP_STATE_PER_STAGE  4u

/* ============================================================================
 * 内部状态
 * ============================================================================ */

/** @brief biquad 系数：16 通道共享同一组系数 [NUM_STAGES * 5] */
static float s_coeffs[AI_BANDPASS_NUM_STAGES * BP_COEFFS_PER_STAGE];

/** @brief 每通道的 biquad 状态 [NUM_CHANNELS][NUM_STAGES * 4] */
static float s_state[AI_BANDPASS_NUM_CHANNELS][AI_BANDPASS_NUM_STAGES * BP_STATE_PER_STAGE];

/** @brief CMSIS-DSP biquad 实例（每通道一个） */
static arm_biquad_casd_df1_inst_f32 s_biquad[AI_BANDPASS_NUM_CHANNELS];

/** @brief 当前低截止频率 (Hz) */
static float s_f_lo = 500.0f;

/** @brief 当前高截止频率 (Hz) */
static float s_f_hi = 8000.0f;

/** @brief 滤波使能标志 */
static uint8_t s_enabled = 0u;

/** @brief 初始化完成标志 */
static uint8_t s_inited = 0u;

/* ============================================================================
 * 内部函数
 * ============================================================================ */

/**
 * @brief 计算 2 阶 Butterworth 高通 biquad 系数
 * @param fc    截止频率 (Hz)
 * @param coeffs 输出 5 个系数 [b0, b1, b2, a1, a2]（CMSIS-DSP 格式，a 取反）
 * @note  CMSIS-DSP DF1 差分方程:
 *        y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] + a1*y[n-1] + a2*y[n-2]
 *        其中 a1, a2 已取反（与教科书符号相反）。
 */
static void s_calc_highpass_coeffs(float fc, float *coeffs)
{
    float w0, alpha, cos_w0;
    float a0_inv;
    float b0, b1, b2, a1, a2;

    w0 = 2.0f * BP_PI * fc / BP_SAMPLE_RATE;
    cos_w0 = arm_cos_f32(w0);
    alpha = arm_sin_f32(w0) / (2.0f * 0.7071067811865f); /* Q = 1/sqrt(2) for Butterworth */

    b0 = (1.0f + cos_w0) / 2.0f;
    b1 = -(1.0f + cos_w0);
    b2 = (1.0f + cos_w0) / 2.0f;
    a0_inv = 1.0f / (1.0f + alpha);
    a1 = -2.0f * cos_w0;
    a2 = 1.0f - alpha;

    /* CMSIS-DSP 格式: {b0, b1, b2, -a1, -a2} (a 取反) */
    coeffs[0] = b0 * a0_inv;
    coeffs[1] = b1 * a0_inv;
    coeffs[2] = b2 * a0_inv;
    coeffs[3] = -a1 * a0_inv;
    coeffs[4] = -a2 * a0_inv;
}

/**
 * @brief 计算 2 阶 Butterworth 低通 biquad 系数
 * @param fc    截止频率 (Hz)
 * @param coeffs 输出 5 个系数 [b0, b1, b2, a1, a2]（CMSIS-DSP 格式）
 */
static void s_calc_lowpass_coeffs(float fc, float *coeffs)
{
    float w0, alpha, cos_w0;
    float a0_inv;
    float b0, b1, b2, a1, a2;

    w0 = 2.0f * BP_PI * fc / BP_SAMPLE_RATE;
    cos_w0 = arm_cos_f32(w0);
    alpha = arm_sin_f32(w0) / (2.0f * 0.7071067811865f);

    b0 = (1.0f - cos_w0) / 2.0f;
    b1 = 1.0f - cos_w0;
    b2 = (1.0f - cos_w0) / 2.0f;
    a0_inv = 1.0f / (1.0f + alpha);
    a1 = -2.0f * cos_w0;
    a2 = 1.0f - alpha;

    coeffs[0] = b0 * a0_inv;
    coeffs[1] = b1 * a0_inv;
    coeffs[2] = b2 * a0_inv;
    coeffs[3] = -a1 * a0_inv;
    coeffs[4] = -a2 * a0_inv;
}

/**
 * @brief 重新计算系数并重初始化所有通道的 biquad 实例
 */
static void s_recalc_and_reinit(void)
{
    uint8_t ch;

    /* Stage 0: 高通 (f_lo) */
    s_calc_highpass_coeffs(s_f_lo, &s_coeffs[0]);

    /* Stage 1: 低通 (f_hi) */
    s_calc_lowpass_coeffs(s_f_hi, &s_coeffs[BP_COEFFS_PER_STAGE]);

    /* 重初始化每通道的 biquad 实例 */
    for (ch = 0u; ch < AI_BANDPASS_NUM_CHANNELS; ch++)
    {
        (void)memset(s_state[ch], 0, sizeof(s_state[ch]));
        arm_biquad_cascade_df1_init_f32(
            &s_biquad[ch],
            AI_BANDPASS_NUM_STAGES,
            s_coeffs,
            s_state[ch]);
    }
}

/* ============================================================================
 * 公开 API
 * ============================================================================ */

void AI_Bandpass_Init(void)
{
    s_f_lo = 500.0f;
    s_f_hi = 8000.0f;
    s_enabled = 0u;

    (void)memset(s_coeffs, 0, sizeof(s_coeffs));
    (void)memset(s_state, 0, sizeof(s_state));

    s_recalc_and_reinit();

    s_inited = 1u;
}

void AI_Bandpass_SetCutoff(float f_lo, float f_hi)
{
    /* 参数范围钳位 */
    if (f_lo < 20.0f)   { f_lo = 20.0f; }
    if (f_hi > 23000.0f) { f_hi = 23000.0f; }
    if (f_hi - f_lo < 100.0f) { f_hi = f_lo + 100.0f; }

    s_f_lo = f_lo;
    s_f_hi = f_hi;

    if (s_inited != 0u)
    {
        s_recalc_and_reinit();
    }
}

void AI_Bandpass_GetCutoff(float *f_lo, float *f_hi)
{
    if (f_lo != NULL) { *f_lo = s_f_lo; }
    if (f_hi != NULL) { *f_hi = s_f_hi; }
}

void AI_Bandpass_SetEnabled(uint8_t enable)
{
    s_enabled = (enable != 0u) ? 1u : 0u;
}

uint8_t AI_Bandpass_GetEnabled(void)
{
    return s_enabled;
}

void AI_Bandpass_ProcessChannel(uint8_t ch, float *data, uint16_t length)
{
    if (s_inited == 0u || s_enabled == 0u) { return; }
    if (ch >= AI_BANDPASS_NUM_CHANNELS)     { return; }
    if (data == NULL || length == 0u)       { return; }

    /* CMSIS-DSP biquad 就地处理 (src == dst) */
    arm_biquad_cascade_df1_f32(&s_biquad[ch], data, data, (uint32_t)length);
}

void AI_Bandpass_ProcessAll(float *ch_data[], uint16_t length)
{
    uint8_t ch;

    if (s_inited == 0u || s_enabled == 0u) { return; }
    if (ch_data == NULL || length == 0u)    { return; }

    for (ch = 0u; ch < AI_BANDPASS_NUM_CHANNELS; ch++)
    {
        if (ch_data[ch] != NULL)
        {
            arm_biquad_cascade_df1_f32(&s_biquad[ch],
                                       ch_data[ch], ch_data[ch],
                                       (uint32_t)length);
        }
    }
}
