/**
 * @file    app_sound_level.c
 * @brief   声级计模式实现 —— A/C/Z 加权 + Leq 积分
 * @details
 * 原理说明：
 *   声级计通过对 FFT 幅度谱应用频率加权曲线，近似人耳对不同频率的感知灵敏度，
 *   然后将加权后的功率谱求和，再转换为 dB 值（参考满幅电平）。
 *
 * A 加权（IEC 61672 标准近似）：
 *   A 加权曲线模拟人耳在中等响度下的频率敏感度：
 *   - 在 1kHz 附近增益为 0 dB（参考点）
 *   - 低频（<1kHz）和高频（>8kHz）有明显衰减
 *   - 1~4kHz 区间有约 +1～+3dB 的轻微提升
 *   使用 IEC 61672 简化公式精确计算 R A(f)，而非查表近似。
 *
 * Leq（等效连续声级）：
 *   Leq = 10 × log10(1/N × Σ p²(i)) + offset
 *   其中 p²(i) 是每帧的线性功率总和（不是 dB），N 是帧数。
 *   使用 double 精度累加防止长时间积分产生精度损失。
 *
 * [改进] 当前 A 加权使用逐 bin 的 powf 调用（计算量大），
 *        可预计算 A 加权增益 LUT 表（128 个 bin），
 *        将 Feed() 中的 powf 调用替换为表查找，提升约 5-10x 速度。
 */
#include "app_sound_level.h"           /* 本模块公开接口 */

#include <math.h>                      /* log10f, powf, sqrtf（dB 和加权计算）*/
#include <string.h>                    /* string.h（目前未直接使用，保留用于后续扩展）*/

/** @brief 参考声压级偏移（dB）：假设 FFT 满幅（最大采样值）= 94 dB SPL（1 Pa 参考声压）*/
/** 此值应根据实际麦克风灵敏度校准，否则绝对 dB 值无意义，只有相对变化有意义 */
#define SLM_DB_REF_OFFSET  94.0f      /* [改进] 应通过已知声压计测量值校准此偏移量 */

/** @brief 最小 dB 值（下限钳位）：无声或极弱信号时的默认下限 */
/** 设为 -80 dB 是因为超过 -80 dB 通常已低于麦克风本底噪声，无意义 */
#define SLM_DB_FLOOR       (-80.0f)

/** @brief 当前加权类型（默认 A 加权，模拟人耳感知）*/
static App_SLM_Weight_t s_weight = APP_SLM_WEIGHT_A;

/** @brief 最近一帧的瞬时加权声级（dB），每次 Feed 后更新 */
static float s_db_inst = SLM_DB_FLOOR;

/** @brief Leq 积分用的线性功率累加器（double 精度防止精度损失）*/
/** 存储的是线性功率之和，不是 dB 值；最终转换时才做 log10 */
static double s_leq_sum = 0.0;

/** @brief Leq 积分已累积的帧数（N = 帧数，Leq 计算时除以 N 得平均功率）*/
static uint32_t s_leq_frames = 0u;

/** @brief 测量周期内的最大瞬时声级 dB（峰值保持，ResetPeak 后清零）*/
static float s_db_max = SLM_DB_FLOOR;

/** @brief 测量周期内的最小瞬时声级 dB（首帧设为第一个有效值）*/
static float s_db_min = 0.0f;

/**
 * @brief  计算 A 加权修正量（dB）
 * @details 按 IEC 61672 标准的 R_A(f) 公式精确计算 A 加权：
 *
 *   R_A(f) = (12194² × f⁴) / [(f²+20.6²)(f²+12194²)√((f²+107.7²)(f²+737.9²))]
 *   A(f) = 20×log10(R_A(f)) + 2.0 dB （+2.0 是用于使 1kHz 处归零的修正量）
 *
 * 各截止频率物理含义：
 *   - 20.6 Hz  = 低频截止点（人耳低频灵敏度下降起点）
 *   - 107.7 Hz = 低中频次极点
 *   - 737.9 Hz = 中高频次极点
 *   - 12194 Hz = 高频截止点（人耳高频灵敏度急剧下降）
 *
 * @param  f 频率（Hz），应为正值
 * @return A 加权修正量（dB），负值表示该频率相对 1kHz 有衰减
 */
static float s_a_weight_db(float f)
{
    float f2 = f * f;          /* 频率的平方 f²（复用，避免重复计算）*/
    float num;                  /* 分子：12194² × f⁴ */
    float den;                  /* 分母：各极点乘积 */
    float ra_sq;                /* R_A(f) 的计算中间结果 */

    if (f < 10.0f)              /* 极低频（<10 Hz）：超出 A 加权有效范围，直接返回极深衰减 */
    {
        return -70.0f;          /* -70 dB 等效于将该频率成分完全消除 */
    }

    /* 分子：12194² × f⁴ = 148693636 × f² × f² */
    num = 148693636.0f * f2 * f2;

    /* 分母第一项：(f² + 20.6²) = (f² + 424.36) */
    /* 分母第二项：(f² + 12194²) = (f² + 148693636) */
    den = (f2 + 424.36f)           /* 低频极点 20.6 Hz 的贡献 */
        * (f2 + 148693636.0f);     /* 高频极点 12194 Hz 的贡献 */

    /* 分母中的平方根项：sqrt((f²+107.7²)(f²+737.9²)) */
    {
        float t1 = f2 + 11599.29f;   /* f² + 107.7² = f² + 11599.29 */
        float t2 = f2 + 544496.41f;  /* f² + 737.9² = f² + 544496.41 */
        den *= sqrtf(t1 * t2);       /* 将平方根乘积乘入分母 */
    }

    if (den < 1.0e-20f)             /* 防止除零（理论上 f>0 时 den 不为零，保护极端数值）*/
    {
        return -70.0f;
    }

    ra_sq = num / den;               /* 计算 R_A(f) 的数值（实际是 R_A² 的某种形式）*/

    /* 注意：公式中 R_A(f) 是幅度比，log10 对应功率时乘以 20 */
    return 20.0f * log10f(ra_sq) + 2.0f;  /* +2.0 dB 为标准化常数，使 1kHz 处 A(f) ≈ 0 dB */
}

/**
 * @brief  计算 C 加权修正量（dB）
 * @details C 加权比 A 加权更平坦（对低频衰减较少），用于测量高声级场景：
 *
 *   R_C(f) = (12194² × f²) / [(f²+20.6²)(f²+12194²)]
 *   C(f) = 20×log10(R_C(f)) + 0.06 dB
 *
 *   与 A 加权相比，C 加权去掉了中间两个极点（107.7 Hz 和 737.9 Hz），
 *   因此在 100Hz~10kHz 范围内更平坦。
 *
 * @param  f 频率（Hz）
 * @return C 加权修正量（dB）
 */
static float s_c_weight_db(float f)
{
    float f2 = f * f;          /* 频率的平方 */
    float num;                  /* 分子 */
    float den;                  /* 分母 */
    float rc_sq;                /* R_C(f) 计算中间值 */

    if (f < 10.0f)              /* 极低频保护 */
    {
        return -40.0f;          /* C 加权低频衰减比 A 加权少（-40 dB 而非 -70 dB）*/
    }

    /* R_C(f) 分子：12194² × f²（比 A 加权少 f²，因此比 A 更平坦）*/
    num = 148693636.0f * f2;

    /* R_C(f) 分母：(f² + 20.6²)(f² + 12194²)（与 A 加权相同）*/
    den = (f2 + 424.36f)           /* 低频极点 */
        * (f2 + 148693636.0f);     /* 高频极点 */

    if (den < 1.0e-20f)             /* 防止除零 */
    {
        return -40.0f;
    }

    rc_sq = num / den;               /* 计算 R_C(f) */
    return 20.0f * log10f(rc_sq) + 0.06f;  /* +0.06 dB 标准化至 1kHz ≈ 0 dB */
}

/**
 * @brief 初始化声级计模块（清零所有状态）
 */
void App_SLM_Init(void)
{
    s_weight = APP_SLM_WEIGHT_A;  /* 默认使用 A 加权（最接近人耳感知）*/
    s_db_inst = SLM_DB_FLOOR;     /* 瞬时级初始化为下限 */
    s_leq_sum = 0.0;              /* Leq 功率累加器清零 */
    s_leq_frames = 0u;            /* Leq 帧计数清零 */
    s_db_max = SLM_DB_FLOOR;      /* 最大值初始化为下限 */
    s_db_min = 0.0f;              /* 最小值初始化为 0（首帧会修正为实际值）*/
}

/**
 * @brief 馈送一帧 FFT 幅度数据并更新声级读数
 * @details 计算流程：
 *          1. 对每个频率 bin 查询加权修正量（A/C/Z）
 *          2. 将修正量（dB）转换为线性增益：gain = 10^(w_dB/20)
 *          3. 将加权后的幅度求功率：mag_w² = (magnitude × gain)²
 *          4. 所有 bin 的加权功率累加为 power_sum
 *          5. 转换为 dB：db = 10×log10(power_sum) + 参考偏移
 *          6. 更新 Leq 积分（线性功率累加）
 */
void App_SLM_Feed(const float *magnitude, uint16_t bin_count, float delta_f)
{
    uint16_t i;             /* 循环变量：当前 FFT bin 索引 */
    float freq;             /* 当前 bin 对应的频率（Hz）*/
    float weight_db;        /* 当前 bin 的加权修正量（dB）*/
    float mag_w;            /* 加权后的幅度值（线性）*/
    float power_sum = 0.0f; /* 所有 bin 的加权功率总和（用于计算总声级）*/
    float db;               /* 本帧计算结果（瞬时声级，dB）*/

    if ((magnitude == NULL) || (bin_count == 0u) || (delta_f <= 0.0f))  /* 参数合法性检查 */
    {
        return;             /* 任一参数无效时跳过，不更新状态 */
    }

    /* 从 bin 1 开始（跳过 DC，即 bin 0）—— DC 分量无声学意义且通常幅度异常大 */
    for (i = 1u; i < bin_count; i++)
    {
        freq = (float)i * delta_f;  /* bin i 对应的中心频率：f_i = i × Δf */

        /* 根据加权类型计算该频率的修正量（dB）*/
        switch (s_weight)
        {
        case APP_SLM_WEIGHT_A:
            weight_db = s_a_weight_db(freq);   /* A 加权：模拟人耳感知 */
            break;
        case APP_SLM_WEIGHT_C:
            weight_db = s_c_weight_db(freq);   /* C 加权：适用于高声级场景 */
            break;
        default:                                /* Z 加权（线性，无修正）*/
            weight_db = 0.0f;                  /* 0 dB 修正 = 不做任何加权 */
            break;
        }

        /* 将 dB 修正转换为线性幅度增益：gain = 10^(weight_dB/20) */
        /* （幅度之比用 /20，功率之比用 /10；此处是幅度修正）*/
        mag_w = magnitude[i] * powf(10.0f, weight_db / 20.0f);

        /* 累加加权功率（功率 = 幅度²）*/
        power_sum += mag_w * mag_w;
    }

    /* 将累积功率转换为 dB 声级 */
    /* 公式：dB = 10×log10(power_sum) + 参考偏移 */
    if (power_sum > 1.0e-20f)              /* 防止 log10(0) 返回 -inf */
    {
        db = 10.0f * log10f(power_sum) + SLM_DB_REF_OFFSET;  /* 转换为 dB SPL */
    }
    else                                    /* 功率极小（接近静音）*/
    {
        db = SLM_DB_FLOOR;                 /* 钳位到下限 -80 dB，避免异常值 */
    }

    s_db_inst = db;                        /* 更新瞬时声级 */

    /* 更新峰值/谷值记录 */
    if (db > s_db_max)                    /* 新的最大值 */
    {
        s_db_max = db;
    }
    if ((s_leq_frames == 0u) || (db < s_db_min))   /* 首帧或新的最小值 */
    {
        s_db_min = db;                    /* 首帧时强制初始化最小值（否则 0 dB 始终是"最小"）*/
    }

    /* Leq 积分：线性功率累加（用功率平均，不是 dB 平均，dB 平均无物理意义）*/
    if (power_sum > 0.0f)
    {
        s_leq_sum += (double)power_sum;   /* 用 double 防止长时积分精度丢失 */
    }
    s_leq_frames++;                        /* 帧计数递增（Leq = Σpower / frames）*/
}

/**
 * @brief 获取当前声级读数（复制到调用方提供的结构体）
 */
void App_SLM_GetReading(App_SLM_Reading_t *reading)
{
    if (reading == NULL)                  /* 空指针保护 */
    {
        return;
    }

    reading->db_inst = s_db_inst;         /* 瞬时声级（最近一帧）*/
    reading->db_max = s_db_max;           /* 峰值保持最大声级 */
    reading->db_min = s_db_min;           /* 谷值保持最小声级 */
    reading->leq_frames = s_leq_frames;  /* Leq 积分帧数（可换算为积分时长）*/

    /* Leq 计算：Leq = 10×log10(1/N × Σ p²) + 参考偏移 */
    if ((s_leq_frames > 0u) && (s_leq_sum > 0.0))  /* 有有效积分数据 */
    {
        /* 1/N × s_leq_sum = 平均线性功率 */
        reading->db_leq = 10.0f * (float)log10(s_leq_sum / (double)s_leq_frames)
                        + SLM_DB_REF_OFFSET;  /* 加参考偏移还原为 dB SPL */
    }
    else                                   /* 无有效数据（帧数为 0 或功率和为 0）*/
    {
        reading->db_leq = SLM_DB_FLOOR;   /* 返回下限值 */
    }
}

/**
 * @brief 设置加权类型（A/C/Z）
 */
void App_SLM_SetWeight(App_SLM_Weight_t weight)
{
    if (weight <= APP_SLM_WEIGHT_Z)        /* 枚举值范围检查 */
    {
        s_weight = weight;                 /* 更新加权类型（立即生效，下次 Feed 使用新加权）*/
    }
    /* [注意] 切换加权类型不会重置现有 Leq 积分；若需要一致的积分，应先 ResetLeq */
}

/**
 * @brief 查询当前加权类型
 */
App_SLM_Weight_t App_SLM_GetWeight(void)
{
    return s_weight;
}

/**
 * @brief 复位 Leq 积分（重新开始计时测量）
 */
void App_SLM_ResetLeq(void)
{
    s_leq_sum = 0.0;      /* 线性功率累加器清零 */
    s_leq_frames = 0u;    /* 帧计数清零（下次 Feed 后从头开始积分）*/
}

/**
 * @brief 复位峰值/谷值记录
 */
void App_SLM_ResetPeak(void)
{
    s_db_max = SLM_DB_FLOOR;   /* 最大值重置为下限（等待新的峰值出现）*/
    s_db_min = 0.0f;           /* 最小值重置为 0（下次 Feed 会以第一帧的值初始化）*/
}
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
