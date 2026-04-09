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
 */
static void s_compute_delays(float theta_deg, float phi_deg)
{
    uint16_t i;
    float theta_rad = theta_deg * 3.14159265f / 180.0f;
    float phi_rad   = phi_deg   * 3.14159265f / 180.0f;

    /* 远场平面波方向向量 (单位向量在 xy 平面的投影) */
    float cos_phi = cosf(phi_rad);
    float ux = sinf(theta_rad) * cos_phi;
    float uy = sinf(phi_rad);

    for (i = 0u; i < MIC_CHANNELS; i++)
    {
        /* 时间延迟 = 点积 / 声速 × 采样率 */
        float tau = (s_mic_pos[i][0] * ux + s_mic_pos[i][1] * uy)
                  / SPEED_OF_SOUND * (float)SAMPLING_RATE;
        /* 四舍五入取整 */
        int8_t d = (int8_t)(tau + (tau >= 0.0f ? 0.5f : -0.5f));
        /* 钳位 */
        if (d > (int8_t)BEAM_MAX_DELAY)
        {
            d = (int8_t)BEAM_MAX_DELAY;
        }
        if (d < -(int8_t)BEAM_MAX_DELAY)
        {
            d = -(int8_t)BEAM_MAX_DELAY;
        }
        s_delays[i] = d;
    }
    s_delays_valid = 1u;
}

void AI_BeamSteer_Init(void)
{
    (void)memset(s_delays, 0, sizeof(s_delays));
    (void)memset(s_overlap, 0, sizeof(s_overlap));
    s_theta_deg = 0.0f;
    s_phi_deg = 0.0f;
    s_mode = BEAMSTEER_MODE_AUTO;
    s_enabled = 0u;
    s_delays_valid = 0u;
}

void AI_BeamSteer_SetDirection(float theta_deg, float phi_deg)
{
    s_theta_deg = theta_deg;
    s_phi_deg = phi_deg;
    s_compute_delays(theta_deg, phi_deg);
}

void AI_BeamSteer_GetDirection(float *theta_deg, float *phi_deg)
{
    if (theta_deg != NULL)
    {
        *theta_deg = s_theta_deg;
    }
    if (phi_deg != NULL)
    {
        *phi_deg = s_phi_deg;
    }
}

void AI_BeamSteer_Process(const float *input, float *output, uint16_t frame_len)
{
    uint16_t ch;
    uint16_t n;
    int16_t idx;
    float sum;
    const float inv_n = 1.0f / (float)MIC_CHANNELS;

    if ((input == NULL) || (output == NULL) || (s_enabled == 0u))
    {
        if (output != NULL)
        {
            (void)memset(output, 0, sizeof(float) * frame_len);
        }
        return;
    }

    if (s_delays_valid == 0u)
    {
        s_compute_delays(s_theta_deg, s_phi_deg);
    }

    for (n = 0u; n < frame_len; n++)
    {
        sum = 0.0f;
        for (ch = 0u; ch < MIC_CHANNELS; ch++)
        {
            idx = (int16_t)n - (int16_t)s_delays[ch];

            if (idx >= 0 && idx < (int16_t)frame_len)
            {
                /* 当前帧内采样 */
                sum += input[ch * frame_len + (uint16_t)idx];
            }
            else if (idx < 0)
            {
                /* 需要上一帧尾部数据 (overlap) */
                int16_t ov_idx = BEAM_MAX_DELAY + idx; /* idx 为负 */
                if (ov_idx >= 0 && ov_idx < BEAM_MAX_DELAY)
                {
                    sum += s_overlap[ch][ov_idx];
                }
                /* else: 超出 overlap 范围, 视为 0 */
            }
            /* idx >= frame_len: 超出当前帧末尾, 视为 0 (下一帧处理) */
        }
        output[n] = sum * inv_n;
    }

    /* 保存当前帧尾部到 overlap 缓冲 */
    for (ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        uint16_t src_start;
        uint16_t copy_len;

        if (frame_len >= BEAM_MAX_DELAY)
        {
            src_start = frame_len - (uint16_t)BEAM_MAX_DELAY;
            copy_len = (uint16_t)BEAM_MAX_DELAY;
        }
        else
        {
            src_start = 0u;
            copy_len = frame_len;
        }
        (void)memcpy(s_overlap[ch],
                     &input[ch * frame_len + src_start],
                     sizeof(float) * copy_len);
    }
}

void AI_BeamSteer_SetMode(AI_BeamSteer_Mode_t mode)
{
    s_mode = mode;
}

AI_BeamSteer_Mode_t AI_BeamSteer_GetMode(void)
{
    return s_mode;
}

void AI_BeamSteer_SetEnabled(uint8_t enable)
{
    s_enabled = (enable != 0u) ? 1u : 0u;
}

uint8_t AI_BeamSteer_GetEnabled(void)
{
    return s_enabled;
}
