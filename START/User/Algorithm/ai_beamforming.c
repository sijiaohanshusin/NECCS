/**
 * @file    ai_beamforming.c
 * @brief   声源定位核心算法实现
 */

#include "ai_beamforming.h"
#include "app_data_stream.h"
#include "ai_config.h"
#include "ai_srp_lut.h"
#include <math.h>

/* =========================================================================
 * AI_FFT_Process
 *
 * 流水线 (per channel):
 *   1. 去直流: arm_mean_f32 + arm_offset_f32，消除麦克风直流偏置
 *   2. 加窗:   arm_mult_f32，乘汉宁窗，抑制频谱泄漏
 *   3. RFFT:   arm_rfft_fast_f32，256点实数 FFT
 *              输出 128 个复数 = 256 个 float32_t，存入 Mic_Freq_Buffer
 * ========================================================================= */
void AI_FFT_Process(void)
{
    float32_t mean_val;

    for (int ch = 0; ch < MIC_CHANNELS; ch++)
    {
        float32_t *p_time = &Mic_Process_Buffer[ch * FRAME_LEN];
        float32_t *p_freq = &Mic_Freq_Buffer[ch * FRAME_LEN];

        /* Step 1: 去直流
         * 计算均值后用 arm_offset_f32 整段减去，比手写 for 循环快 ~4x (SIMD) */
        arm_mean_f32(p_time, FRAME_LEN, &mean_val);
        arm_offset_f32(p_time, -mean_val, p_time, FRAME_LEN);

        /* Step 2: 汉宁窗加权
         * 逐元素乘以预计算窗函数，in-place 不额外分配内存 */
        arm_mult_f32(p_time, Hanning_Window, p_time, FRAME_LEN);

        /* Step 3: 256点实数 FFT
         * ifftFlag = 0 表示正变换 (Forward FFT)
         * 注意: arm_rfft_fast_f32 会修改 p_time 的内容，调用后不可复用 */
        arm_rfft_fast_f32(&S_Rfft, p_time, p_freq, 0);
    }
}


/* =========================================================================
 * SRP-PHAT 声源定位算法
 *
 * 整体流程:
 *   1. GCC-PHAT: 40 对麦克风的互功率谱 + PHAT 白化
 *   2. 粗搜 7×7 (49 点): 从 Flash LUT 读取 TDOA，相位旋转累积
 *   3. 精搜 Top-3 × 5×5 (75 点): 运行时计算 TDOA
 *   4. 全局最大值 → 方位角 + 归一化能量
 *
 * 性能目标: < 5ms/帧 @ 480MHz Cortex-M7
 * ========================================================================= */

/* 预计算角频率基: omega_base[k] = 2π × (k + SRP_FREQ_BIN_START) × DELTA_F */
static float32_t s_omega_base[SRP_FREQ_BINS];

/* ---- 6a. 初始化 -------------------------------------------------------- */
void AI_SRP_PHAT_Init(void)
{
    for (int k = 0; k < SRP_FREQ_BINS; k++)
    {
        s_omega_base[k] = 2.0f * 3.14159265358979f
                          * (float32_t)(k + SRP_FREQ_BIN_START) * DELTA_F;
    }
}


/* ---- 6b. GCC-PHAT 互功率谱计算 ----------------------------------------
 *
 * 对 40 对麦克风，每对处理 bin 1..32:
 *   1. 共轭: conj(Xj)
 *   2. 互功率谱: Gij = Xi × conj(Xj)
 *   3. 幅度: |Gij|
 *   4. PHAT 白化: G̃ij = Gij / (|Gij| + ε)
 *   5. 结果写入 GCC_PHAT_Buffer[pair * SRP_FREQ_BINS * 2 + k*2]
 *
 * 栈上临时变量约 640B (conj 256B + cross 256B + mag 128B)
 * ----------------------------------------------------------------------- */
static void SRP_GCC_PHAT_Compute(void)
{
    /* 临时缓冲区 (栈上, SRP_FREQ_BINS=32) */
    float32_t conj_buf[SRP_FREQ_BINS * 2];   /* 32 × 2 = 64 floats = 256B */
    float32_t cross_buf[SRP_FREQ_BINS * 2];  /* 256B */
    float32_t mag_buf[SRP_FREQ_BINS];        /* 128B */

    for (int p = 0; p < SRP_PAIR_COUNT; p++)
    {
        uint8_t mi = srp_pair_idx[p][0];
        uint8_t mj = srp_pair_idx[p][1];

        /* 指向 Mic_Freq_Buffer 中 bin 1 的起始位置
         * bin k 的复数存于 [ch * FRAME_LEN + 2*k, ch * FRAME_LEN + 2*k + 1]
         * bin 1 起始于 offset = 2 (因为 [0]=DC_re, [1]=Nyq_re) */
        const float32_t *xi = &Mic_Freq_Buffer[mi * FRAME_LEN + SRP_FREQ_BIN_START * 2];
        const float32_t *xj = &Mic_Freq_Buffer[mj * FRAME_LEN + SRP_FREQ_BIN_START * 2];

        /* Step 1: 共轭 Xj → conj_buf */
        arm_cmplx_conj_f32((float32_t *)xj, conj_buf, SRP_FREQ_BINS);

        /* Step 2: Xi × conj(Xj) → cross_buf (复数乘法) */
        arm_cmplx_mult_cmplx_f32((float32_t *)xi, conj_buf, cross_buf, SRP_FREQ_BINS);

        /* Step 3: |cross_buf| → mag_buf */
        arm_cmplx_mag_f32(cross_buf, mag_buf, SRP_FREQ_BINS);

        /* Step 4: PHAT 白化 → 直接写入 GCC_PHAT_Buffer */
        float32_t *p_out = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2];
        for (int k = 0; k < SRP_FREQ_BINS; k++)
        {
            float32_t inv_mag = 1.0f / (mag_buf[k] + PHAT_EPSILON);
            p_out[2 * k]     = cross_buf[2 * k]     * inv_mag;
            p_out[2 * k + 1] = cross_buf[2 * k + 1] * inv_mag;
        }
    }
}


/* ---- 6c. 单点 SRP 累积 (相位旋转优化) ----------------------------------
 *
 * 给定一个扫描方向对应的 40 个 TDOA 值，计算 SRP 功率:
 *   P(θ,φ) = Σ_pairs Σ_bins Re{ G̃(k) × e^{-j·ω_k·τ} }
 *
 * 采用递推相位旋转避免逐 bin 调 cosf/sinf:
 *   每对 pair 仅 4 次三角函数调用 (初始化 + 步进常量)
 *   内循环: 4 mul + 2 add (旋转) + 2 mul + 1 add (累积) = 9 ops/bin
 *
 * @param tau  40 个 TDOA 值 (秒)
 * @return     SRP 功率值
 * ----------------------------------------------------------------------- */
static float32_t SRP_Accumulate_Point(const float32_t *tau)
{
    float32_t power = 0.0f;

    for (int p = 0; p < SRP_PAIR_COUNT; p++)
    {
        const float32_t *gcc = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2];

        /* 相位步进常量: d_phi = 2π × DELTA_F × τ[p]
         * 转为度数供 arm_sin_cos_f32 使用 (输入单位是度) */
        float32_t d_phi_deg = 360.0f * DELTA_F * tau[p];
        float32_t cos_d, sin_d;
        arm_sin_cos_f32(d_phi_deg, &sin_d, &cos_d);

        /* 初始相位: φ_0 = d_phi × SRP_FREQ_BIN_START
         * 因为 SRP_FREQ_BIN_START = 1, 所以 φ_0 = d_phi, 直接复用 */
        float32_t cos_phi = cos_d;
        float32_t sin_phi = sin_d;

        float32_t pair_sum = 0.0f;

        for (int k = 0; k < SRP_FREQ_BINS; k++)
        {
            /* Re{ G̃(k) × e^{-jφ} } = G_re × cos(φ) + G_im × sin(φ) */
            pair_sum += gcc[2 * k] * cos_phi + gcc[2 * k + 1] * sin_phi;

            /* 递推旋转: φ_{k+1} = φ_k + d_phi */
            float32_t c_new = cos_phi * cos_d - sin_phi * sin_d;
            float32_t s_new = sin_phi * cos_d + cos_phi * sin_d;
            cos_phi = c_new;
            sin_phi = s_new;
        }

        power += pair_sum;
    }

    return power;
}


/* ---- 辅助: 找 Top-K 索引 ---------------------------------------------- */
static void find_top_k_indices(const float32_t *arr, uint32_t len,
                               uint32_t *top_idx, uint32_t k)
{
    /* 简单选择法, k 很小 (3), O(k×n) 可忽略 */
    float32_t top_val[FINE_TOP_K];
    for (uint32_t i = 0; i < k; i++)
    {
        top_val[i] = -1e30f;
        top_idx[i] = 0;
    }

    for (uint32_t n = 0; n < len; n++)
    {
        /* 找 top_val 中最小的槽位 */
        uint32_t min_slot = 0;
        float32_t min_val = top_val[0];
        for (uint32_t i = 1; i < k; i++)
        {
            if (top_val[i] < min_val)
            {
                min_val = top_val[i];
                min_slot = i;
            }
        }
        if (arr[n] > min_val)
        {
            top_val[min_slot] = arr[n];
            top_idx[min_slot] = n;
        }
    }
}


/* ---- 6d. SRP-PHAT 主函数 ---------------------------------------------- */

/* 精搜角度解码表 (静态变量, 避免栈溢出且保证生命周期) */
static float32_t s_fine_theta_table[FINE_TOTAL];
static float32_t s_fine_phi_table[FINE_TOTAL];

void AI_SRP_PHAT_Process(Sound_Pos_t *result)
{
    float32_t tau_buf[SRP_PAIR_COUNT];  /* 临时 TDOA 缓冲, 160B 栈 */

    /* ===== Step 1: GCC-PHAT 互功率谱计算 (~0.3ms) ===== */
    SRP_GCC_PHAT_Compute();

    /* ===== Step 2: 粗搜 7×7 = 49 个网格点 (~1.5ms) ===== */
    for (int g = 0; g < COARSE_TOTAL; g++)
    {
        SRP_Power[g] = SRP_Accumulate_Point(tdoa_coarse_lut[g]);
    }

    /* ===== Step 3: 找粗搜 Top-3 ===== */
    uint32_t top3_idx[FINE_TOP_K];
    find_top_k_indices(SRP_Power, COARSE_TOTAL, top3_idx, FINE_TOP_K);

    /* ===== Step 4: 精搜 3 × 5×5 = 75 个网格点 (~2.4ms) ===== */
    uint32_t fine_offset = COARSE_TOTAL; /* SRP_Power[49..123] 存精搜结果 */

    for (int t = 0; t < FINE_TOP_K; t++)
    {
        /* 解码粗搜候选的角度 */
        uint32_t coarse_g = top3_idx[t];
        uint32_t ti = coarse_g / COARSE_GRID_SIZE;
        uint32_t pi = coarse_g % COARSE_GRID_SIZE;
        float32_t center_theta = coarse_theta_deg[ti];
        float32_t center_phi   = coarse_phi_deg[pi];

        for (int fi = 0; fi < FINE_GRID_SIZE; fi++)
        {
            float32_t theta_h = center_theta + (-FINE_HALF_RANGE + fi * FINE_ANGLE_STEP);
            /* sin(theta_h) 只依赖 fi，提到外层避免重复计算 */
            float32_t sin_th, cos_th_unused;
            arm_sin_cos_f32(theta_h, &sin_th, &cos_th_unused);

            for (int fj = 0; fj < FINE_GRID_SIZE; fj++)
            {
                uint32_t local_idx = fi * FINE_GRID_SIZE + fj;
                uint32_t global_idx = t * FINE_TOTAL_PER_TOP + local_idx;

                float32_t theta_v = center_phi   + (-FINE_HALF_RANGE + fj * FINE_ANGLE_STEP);

                /* 存储精搜角度以便最终解码 */
                s_fine_theta_table[global_idx] = theta_h;
                s_fine_phi_table[global_idx]   = theta_v;

                /* arm_sin_cos_f32 输入单位是度，一次算出 sin 和 cos */
                float32_t sin_tv, cos_tv;
                arm_sin_cos_f32(theta_v, &sin_tv, &cos_tv);

                float32_t sin_th_cos_tv = sin_th * cos_tv;
                float32_t inv_c = 1.0f / SPEED_OF_SOUND;

                for (int p = 0; p < SRP_PAIR_COUNT; p++)
                {
                    tau_buf[p] = (srp_pair_dx[p] * sin_th_cos_tv
                                + srp_pair_dy[p] * sin_tv) * inv_c;
                }

                SRP_Power[fine_offset + global_idx] =
                    SRP_Accumulate_Point(tau_buf);
            }
        }
    }

    /* ===== Step 5: 全局最大值 ===== */
    float32_t max_val;
    uint32_t  max_idx;
    arm_max_f32(SRP_Power, SRP_GRID_TOTAL, &max_val, &max_idx);

    /* ===== Step 6: 解码方位角 ===== */
    if (max_idx < COARSE_TOTAL)
    {
        /* 最大值在粗搜网格中 */
        uint32_t ti = max_idx / COARSE_GRID_SIZE;
        uint32_t pi = max_idx % COARSE_GRID_SIZE;
        result->x_angle = coarse_theta_deg[ti];
        result->y_angle = coarse_phi_deg[pi];
    }
    else
    {
        /* 最大值在精搜网格中 */
        uint32_t fine_idx = max_idx - COARSE_TOTAL;
        result->x_angle = s_fine_theta_table[fine_idx];
        result->y_angle = s_fine_phi_table[fine_idx];
    }

    /* ===== Step 7: 能量归一化到 [0, 1] ===== */
    /* max_val 的理论最大值 = SRP_PAIR_COUNT × SRP_FREQ_BINS = 40 × 32 = 1280 */
    float32_t norm = max_val / (float32_t)(SRP_PAIR_COUNT * SRP_FREQ_BINS);
    if (norm > 1.0f) norm = 1.0f;
    if (norm < 0.0f) norm = 0.0f;
    result->energy = norm;
}
