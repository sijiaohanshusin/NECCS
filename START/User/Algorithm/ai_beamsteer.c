/**
 * @file    ai_beamsteer.c
 * @brief   DAS 延迟求和波束控向实现
 * @details 整数样本延迟 DAS，最大延迟约 ±8 样本 (48kHz, 60mm 阵列)。
 *          维护每通道 overlap 缓冲处理帧边界效应。
 */
#include "ai_beamsteer.h"

#include "app_user_config.h"

#include <math.h>
#include <string.h>

/** @brief 最大整数延迟 (样本) */
#define BEAM_MAX_DELAY  10

/** @brief 方向平滑 EMA 系数 */
#define BEAM_DIR_EMA    0.3f

/**
 * @brief 麦克风相对于阵列中心的位置 (米)，按 TDM 通道顺序排列
 * @details 由 tools/generate_srp_lut.py 的 MIC_COORDS_MM 和 MIC_CHANNEL_MAP 推导。
 *          [ch][0] = x_rel (m), [ch][1] = y_rel (m)
 */
static const float s_mic_pos[MIC_CHANNELS][2] = {
    { -0.014670f, -0.028240f },  /* TDM ch 0 */
    { -0.007790f, +0.028970f },  /* TDM ch 1 */
    { +0.023680f, -0.015060f },  /* TDM ch 2 */
    { -0.025580f, -0.004530f },  /* TDM ch 3 */
    { -0.013550f, +0.012410f },  /* TDM ch 4 */
    { +0.015000f, +0.000000f },  /* TDM ch 5 */
    { +0.014430f, +0.018820f },  /* TDM ch 6 */
    { +0.001850f, -0.021130f },  /* TDM ch 7 */
    { -0.032520f, +0.013420f },  /* TDM ch 8 */
    { +0.031510f, +0.011510f },  /* TDM ch 9 */
    { +0.011440f, +0.036490f },  /* TDM ch 10 */
    { +0.015570f, -0.033280f },  /* TDM ch 11 */
    { +0.040120f, -0.008820f },  /* TDM ch 12 */
    { -0.034340f, -0.019900f },  /* TDM ch 13 */
    { -0.005620f, -0.043370f },  /* TDM ch 14 */
    { -0.024400f, +0.034710f },  /* TDM ch 15 */
};

/** @brief 当前整数延迟表 (样本) */
static int8_t s_delays[MIC_CHANNELS];

/** @brief 帧边界 overlap 缓冲 (每通道 BEAM_MAX_DELAY 个样本的尾部历史) */
static float s_overlap[MIC_CHANNELS][BEAM_MAX_DELAY];

/** @brief 当前波束方向 (度) */
static float s_theta_deg = 0.0f;
static float s_phi_deg = 0.0f;

/** @brief 追踪模式 */
static AI_BeamSteer_Mode_t s_mode = BEAMSTEER_MODE_AUTO;

/** @brief 使能标志 */
static uint8_t s_enabled = 0u;

/** @brief 延迟是否已计算过 */
static uint8_t s_delays_valid = 0u;

/**
 * @brief 根据目标方向计算各通道延迟
 * @details 远场平面波模型：声源无限远处，波前为平面。
 *          延迟 = dot(mic_pos, direction_unit_vector) / c × fs
 *          其中方向向量 (ux, uy) 是声源方向在 xy 平面的单位投影。
 */
static void s_compute_delays(float theta_deg, float phi_deg)
{
    uint16_t i;
    /* 角度单位换算：度 → 弧度（1度 = π/180 rad） */
    float theta_rad = theta_deg * 3.14159265f / 180.0f;  /* 水平方位角 */
    float phi_rad   = phi_deg   * 3.14159265f / 180.0f;  /* 俯仰角 */

    /* 声源方向的单位向量在 xy 平面的投影（远场平面波模型）：
     * ux = sin(θ) × cos(φ)：水平分量
     * uy = sin(φ)：垂直分量
     * cos(φ) 因子来自球坐标到直角坐标的转换 */
    float cos_phi = cosf(phi_rad);
    float ux = sinf(theta_rad) * cos_phi;  /* x 方向分量 */
    float uy = sinf(phi_rad);              /* y 方向分量 */

    for (i = 0u; i < MIC_CHANNELS; i++)
    {
        /* 传播时间差（秒）= 麦克风位置 · 方向向量 / 声速
         * 然后乘以采样率转换为样本数 */
        float tau = (s_mic_pos[i][0] * ux + s_mic_pos[i][1] * uy)
                  / SPEED_OF_SOUND * (float)SAMPLING_RATE;
        /* 四舍五入取整（比直接截断精度更高） */
        int8_t d = (int8_t)(tau + (tau >= 0.0f ? 0.5f : -0.5f));
        /* 延迟钳位到 ±BEAM_MAX_DELAY（防止超出 overlap 缓冲边界） */
        if (d > (int8_t)BEAM_MAX_DELAY)
        {
            d = (int8_t)BEAM_MAX_DELAY;   /* 正方向最大延迟钳位 */
        }
        if (d < -(int8_t)BEAM_MAX_DELAY)
        {
            d = -(int8_t)BEAM_MAX_DELAY;  /* 负方向最大延迟钳位 */
        }
        s_delays[i] = d;  /* 存储通道 i 的整数样本延迟 */
    }
    s_delays_valid = 1u;  /* 标记延迟表已有效，避免重复计算 */
}

void AI_BeamSteer_Init(void)
{
    (void)memset(s_delays, 0, sizeof(s_delays));    /* 清零延迟表（初始无延迟，正前方） */
    (void)memset(s_overlap, 0, sizeof(s_overlap));  /* 清零 overlap 缓冲（初始无历史数据） */
    s_theta_deg = 0.0f;      /* 初始水平方位角：正前方 */
    s_phi_deg = 0.0f;        /* 初始俯仰角：水平面 */
    s_mode = BEAMSTEER_MODE_AUTO;  /* 默认跟踪 SRP-PHAT 定位结果自动转向 */
    s_enabled = 0u;          /* 默认禁用，需显式启用 */
    s_delays_valid = 0u;     /* 延迟表无效，首次 Process 时会重新计算 */
}

void AI_BeamSteer_SetDirection(float theta_deg, float phi_deg)
{
    s_theta_deg = theta_deg;               /* 更新目标水平方位角（度） */
    s_phi_deg = phi_deg;                   /* 更新目标俯仰角（度） */
    s_compute_delays(theta_deg, phi_deg);  /* 立即重新计算延迟表，下帧生效 */
}

void AI_BeamSteer_GetDirection(float *theta_deg, float *phi_deg)
{
    if (theta_deg != NULL)
    {
        *theta_deg = s_theta_deg;  /* 输出当前水平方位角（度） */
    }
    if (phi_deg != NULL)
    {
        *phi_deg = s_phi_deg;  /* 输出当前俯仰角（度） */
    }
}

void AI_BeamSteer_Process(const float *input, float *output, uint16_t frame_len)
{
    uint16_t ch;     /* 通道循环计数器 */
    uint16_t n;      /* 样本循环计数器 */
    int16_t idx;     /* 经过延迟补偿后的源样本索引（可能为负，即需要 overlap 数据） */
    float sum;       /* 当前样本的各通道叠加和 */
    const float inv_n = 1.0f / (float)MIC_CHANNELS;  /* 归一化因子（等权平均） */

    /* 前置检查：无效指针或模块禁用时清零输出并返回 */
    if ((input == NULL) || (output == NULL) || (s_enabled == 0u))
    {
        if (output != NULL)
        {
            (void)memset(output, 0, sizeof(float) * frame_len);  /* 输出静音帧 */
        }
        return;
    }

    /* 延迟表未计算：按当前方向立即计算（首帧或方向改变后首次调用） */
    if (s_delays_valid == 0u)
    {
        s_compute_delays(s_theta_deg, s_phi_deg);
    }

    /* ---- DAS 主循环：逐样本叠加 ---- */
    for (n = 0u; n < frame_len; n++)
    {
        sum = 0.0f;  /* 每个输出样本重置累加器 */
        for (ch = 0u; ch < MIC_CHANNELS; ch++)
        {
            /* idx = n - delay[ch]：将当前输出时刻映射到通道 ch 的输入时刻
             * 正延迟：该通道声音到达更晚，需要提前取历史样本（overlap）
             * 负延迟：该通道声音到达更早，需要取当前帧后续样本 */
            idx = (int16_t)n - (int16_t)s_delays[ch];

            if (idx >= 0 && idx < (int16_t)frame_len)
            {
                /* 正常情况：被引用的样本在当前帧范围内，直接读取
                 * input 布局：[ch0_s0..ch0_sN, ch1_s0..ch1_sN, ...] */
                sum += input[ch * frame_len + (uint16_t)idx];
            }
            else if (idx < 0)
            {
                /* 需要上一帧尾部的 overlap 数据（idx < 0 表示当前帧之前的时刻）
                 * overlap 存储了上一帧最后 BEAM_MAX_DELAY 个样本 */
                int16_t ov_idx = BEAM_MAX_DELAY + idx;  /* idx 为负，ov_idx 从 0 开始正向 */
                if (ov_idx >= 0 && ov_idx < BEAM_MAX_DELAY)
                {
                    sum += s_overlap[ch][ov_idx];  /* 从 overlap 历史缓冲读取 */
                }
                /* else: 超过 BEAM_MAX_DELAY 范围（理论上不应发生，钳位保证了安全），视为 0 */
            }
            /* idx >= frame_len：超出当前帧末尾（负延迟时可能发生），视为 0 */
            /* 这会引入轻微的帧边界误差，[改进] 应引入超前缓冲修正 */
        }
        output[n] = sum * inv_n;  /* 归一化叠加结果（等权平均，避免幅度增益 ×16） */
    }

    /* ---- 保存当前帧尾部到 overlap 缓冲（供下帧使用） ---- */
    for (ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        uint16_t src_start;  /* 要拷贝的起始样本在 input 中的偏移 */
        uint16_t copy_len;   /* 要拷贝的样本数 */

        if (frame_len >= BEAM_MAX_DELAY)
        {
            /* 正常情况：帧长度足够，只取尾部 BEAM_MAX_DELAY 个样本 */
            src_start = frame_len - (uint16_t)BEAM_MAX_DELAY;
            copy_len = (uint16_t)BEAM_MAX_DELAY;
        }
        else
        {
            /* 异常情况：帧长度小于最大延迟（极短帧），取全部样本 */
            src_start = 0u;
            copy_len = frame_len;
        }
        /* 将当前帧尾部样本拷贝到 overlap 缓冲，供下一帧解决边界延迟问题 */
        (void)memcpy(s_overlap[ch],
                     &input[ch * frame_len + src_start],
                     sizeof(float) * copy_len);
    }
}

void AI_BeamSteer_SetMode(AI_BeamSteer_Mode_t mode)
{
    s_mode = mode;  /* 设置控向模式：AUTO=自动跟踪 SRP-PHAT 结果，MANUAL=手动指向固定方向 */
}

AI_BeamSteer_Mode_t AI_BeamSteer_GetMode(void)
{
    return s_mode;  /* 返回当前控向模式（AUTO 或 MANUAL） */
}

void AI_BeamSteer_SetEnabled(uint8_t enable)
{
    /* 非零统一转 1，保证状态只有 0/1 两种值 */
    s_enabled = (enable != 0u) ? 1u : 0u;
    /* 禁用时 Process() 清零输出，节省 CPU（不执行延迟求和） */
}

uint8_t AI_BeamSteer_GetEnabled(void)
{
    return s_enabled;  /* 1=DAS 波束成形已启用，0=禁用（直通态） */
}
