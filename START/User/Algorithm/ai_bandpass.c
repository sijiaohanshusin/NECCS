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
    alpha = arm_sin_f32(w0) / (2.0f * 0.7071067811865f); /* Q = 1/sqrt(2) Butterworth 最平响应 */

    /* 2阶高通 Butterworth 双线性变换（BLT）系数推导：
     * 模拟域高通: H(s) = s^2 / (s^2 + sqrt(2)*s + 1)
     * 双线性预畸变后归一化到数字域 */
    b0 = (1.0f + cos_w0) / 2.0f;   /* 高通：b0 = (1 + cos_w0) / 2 */
    b1 = -(1.0f + cos_w0);         /* 高通：b1 = -(1 + cos_w0) */
    b2 = (1.0f + cos_w0) / 2.0f;   /* 高通：b2 = b0（关于直流有零点） */
    a0_inv = 1.0f / (1.0f + alpha); /* 归一化因子的倒数（避免除法乘法转换） */
    a1 = -2.0f * cos_w0;            /* 极点分母系数 a1（注意这里还未取反） */
    a2 = 1.0f - alpha;              /* 极点分母系数 a2 */

    /* 写入 CMSIS-DSP 要求格式: {b0, b1, b2, -a1, -a2}（a 系数取反后存储） */
    coeffs[0] = b0 * a0_inv;      /* 归一化 b0 */
    coeffs[1] = b1 * a0_inv;      /* 归一化 b1 */
    coeffs[2] = b2 * a0_inv;      /* 归一化 b2 */
    coeffs[3] = -a1 * a0_inv;     /* 归一化后取反 a1（CMSIS-DSP 约定正号） */
    coeffs[4] = -a2 * a0_inv;     /* 归一化后取反 a2 */
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

    /* 角频率归一化（与高通相同步骤，但分子构造不同） */
    w0 = 2.0f * BP_PI * fc / BP_SAMPLE_RATE;   /* 数字角频率 ω₀ = 2π × fc / fs */
    cos_w0 = arm_cos_f32(w0);                   /* cos(ω₀)，用于 BLT 系数计算 */
    alpha = arm_sin_f32(w0) / (2.0f * 0.7071067811865f); /* Q = 1/sqrt(2) Butterworth */

    /* 2阶低通 Butterworth 系数（低通在直流处有最大增益，在奈奎斯特处有零点） */
    b0 = (1.0f - cos_w0) / 2.0f;  /* 低通：b0（直流处 H = 1） */
    b1 = 1.0f - cos_w0;            /* 低通：b1 = 2 × b0（奈奎斯特处 H = 0） */
    b2 = (1.0f - cos_w0) / 2.0f;  /* 低通：b2 = b0 */
    a0_inv = 1.0f / (1.0f + alpha); /* 归一化因子倒数 */
    a1 = -2.0f * cos_w0;            /* 极点 a1（高通/低通极点相同，只有分子不同） */
    a2 = 1.0f - alpha;              /* 极点 a2 */

    /* 写入 CMSIS-DSP 格式：{b0, b1, b2, -a1, -a2} */
    coeffs[0] = b0 * a0_inv;      /* 归一化 b0 */
    coeffs[1] = b1 * a0_inv;      /* 归一化 b1 */
    coeffs[2] = b2 * a0_inv;      /* 归一化 b2 */
    coeffs[3] = -a1 * a0_inv;     /* 归一化后取反 a1 */
    coeffs[4] = -a2 * a0_inv;     /* 归一化后取反 a2 */
}

/**
 * @brief 重新计算系数并重初始化所有通道的 biquad 实例
 * @details 高通系数写入 Stage 0 偏移，低通系数写入 Stage 1 偏移。
 *          重初始化时清零所有状态（滤波器历史），避免频率切换时产生瞬态。
 */
static void s_recalc_and_reinit(void)
{
    uint8_t ch;

    /* Stage 0: 高通 (f_lo) — 清除 500Hz 以下的低频噪声和直流偏置 */
    s_calc_highpass_coeffs(s_f_lo, &s_coeffs[0]);

    /* Stage 1: 低通 (f_hi) — 抗混叠，截除语音频段以上的高频成分 */
    s_calc_lowpass_coeffs(s_f_hi, &s_coeffs[BP_COEFFS_PER_STAGE]);

    /* 重初始化每通道 biquad 实例（16通道共享同一套系数，但状态各自独立） */
    for (ch = 0u; ch < AI_BANDPASS_NUM_CHANNELS; ch++)
    {
        /* 清零状态缓冲（DF1 每级需要 4 个状态变量：x[n-1], x[n-2], y[n-1], y[n-2]） */
        (void)memset(s_state[ch], 0, sizeof(s_state[ch]));
        /* 初始化 CMSIS-DSP biquad 结构体（绑定系数指针和状态指针） */
        arm_biquad_cascade_df1_init_f32(
            &s_biquad[ch],              /* 目标 biquad 实例 */
            AI_BANDPASS_NUM_STAGES,     /* 级联级数 = 2（高通+低通） */
            s_coeffs,                   /* 共享系数数组 */
            s_state[ch]);              /* 通道独立的状态缓冲 */
    }
}

/* ============================================================================
 * 公开 API
 * ============================================================================ */

void AI_Bandpass_Init(void)
{
    s_f_lo = 500.0f;   /* 默认低截止频率 500 Hz（滤除低频环境噪声） */
    s_f_hi = 8000.0f;  /* 默认高截止频率 8000 Hz（覆盖主要语音频段） */
    s_enabled = 0u;    /* 初始禁用，直通模式，不消耗 CPU */

    (void)memset(s_coeffs, 0, sizeof(s_coeffs));  /* 清零系数数组（随后会被覆盖） */
    (void)memset(s_state, 0, sizeof(s_state));    /* 清零所有通道的 biquad 状态 */

    s_recalc_and_reinit();  /* 根据默认截止频率计算初始系数并绑定到 biquad 实例 */

    s_inited = 1u;  /* 标记模块已初始化，可以安全调用 Process 函数 */
}

void AI_Bandpass_SetCutoff(float f_lo, float f_hi)
{
    /* 参数范围钳位：确保频率在物理可实现范围内 */
    if (f_lo < 20.0f)   { f_lo = 20.0f; }    /* 下限 20Hz（人耳可听最低频率） */
    if (f_hi > 23000.0f) { f_hi = 23000.0f; } /* 上限 23kHz（略低于奈奎斯特频率 24kHz） */
    /* 确保高通截止频率比低通至少低 100Hz，防止带宽过窄导致系数不稳定 */
    if (f_hi - f_lo < 100.0f) { f_hi = f_lo + 100.0f; }

    s_f_lo = f_lo;  /* 更新低截止频率 */
    s_f_hi = f_hi;  /* 更新高截止频率 */

    if (s_inited != 0u)
    {
        /* 模块已初始化，立即重新计算系数并刷新所有通道的 biquad 实例，
         * 新截止频率对下一个 ProcessChannel/ProcessAll 调用立即生效 */
        s_recalc_and_reinit();
    }
    /* 未初始化时仅保存频率值，Init() 调用时会使用这些值初始化系数 */
}

void AI_Bandpass_GetCutoff(float *f_lo, float *f_hi)
{
    if (f_lo != NULL) { *f_lo = s_f_lo; }  /* 若指针非空，输出当前低截止频率 */
    if (f_hi != NULL) { *f_hi = s_f_hi; }  /* 若指针非空，输出当前高截止频率 */
}

void AI_Bandpass_SetEnabled(uint8_t enable)
{
    /* 非零值统一转换为 1，保证状态只有 0/1 两种值 */
    s_enabled = (enable != 0u) ? 1u : 0u;
    /* 禁用时 ProcessChannel 直接返回，数据原样通过（直通模式，零 CPU 开销） */
}

uint8_t AI_Bandpass_GetEnabled(void)
{
    return s_enabled;  /* 返回滤波使能标志：1=已启用，0=直通 */
}

void AI_Bandpass_ProcessChannel(uint8_t ch, float *data, uint16_t length)
{
    /* 前置条件检查：未初始化、已禁用、通道越界、空指针或零长度均直接返回 */
    if (s_inited == 0u || s_enabled == 0u) { return; }  /* 防止未初始化调用 */
    if (ch >= AI_BANDPASS_NUM_CHANNELS)     { return; }  /* 通道索引越界保护 */
    if (data == NULL || length == 0u)       { return; }  /* 空指针/零长度保护 */

    /* CMSIS-DSP biquad 就地处理：src = dst = data，结果原地覆盖输入 */
    arm_biquad_cascade_df1_f32(&s_biquad[ch], data, data, (uint32_t)length);
}

void AI_Bandpass_ProcessAll(float *ch_data[], uint16_t length)
{
    uint8_t ch;  /* 通道循环计数器 */

    if (s_inited == 0u || s_enabled == 0u) { return; }  /* 未初始化或已禁用则直通 */
    if (ch_data == NULL || length == 0u)    { return; }  /* 空指针/零长度保护 */

    /* 遍历所有通道，逐一执行 biquad 滤波 */
    for (ch = 0u; ch < AI_BANDPASS_NUM_CHANNELS; ch++)
    {
        if (ch_data[ch] != NULL)  /* 单通道指针为 NULL 时跳过（允许稀疏调用） */
        {
            /* 就地处理：src 和 dst 都是 ch_data[ch]，length 个样本被原地滤波 */
            arm_biquad_cascade_df1_f32(&s_biquad[ch],
                                       ch_data[ch], ch_data[ch],
                                       (uint32_t)length);
        }
    }
}
