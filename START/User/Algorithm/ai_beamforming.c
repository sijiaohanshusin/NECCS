/**
 * @file    ai_beamforming.c
 * @brief   声源定位核心算法实现
 */

#include "ai_beamforming.h"
#include "app_data_stream.h"
#include "ai_config.h"

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
