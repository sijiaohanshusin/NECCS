/**
 * @file    ai_beamforming.h
 * @brief   声源定位核心算法接口
 *
 * @note    数据流: Mic_Process_Buffer (时域) -> AI_FFT_Process -> Mic_Freq_Buffer (频域)
 *                  Mic_Freq_Buffer (频域) -> AI_SRP_PHAT -> Sound_Pos_t (方位角)
 */

#ifndef AI_BEAMFORMING_H
#define AI_BEAMFORMING_H

#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  对 Mic_Process_Buffer 中的 16 路时域数据执行完整的频域预处理：
 *         去直流 -> 乘汉宁窗 -> 512点 RFFT
 *
 * @note   输入:  Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN] (DTCM, float32_t)
 *         输出:  Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN]    (DTCM, float32_t, 复数交织格式)
 *
 * @note   RFFT 输出格式 (arm_rfft_fast_f32 约定):
 *         - [0]     = 直流分量 (实部，虚部恒为 0)
 *         - [1]     = 奈奎斯特分量 (实部，虚部恒为 0)
 *         - [2k]    = 第 k 个频率 bin 的实部  (k = 1 .. FRAME_LEN/2-1)
 *         - [2k+1]  = 第 k 个频率 bin 的虚部
 *
 * @note   每次调用完毕后，Mic_Process_Buffer 的内容会被 CMSIS-DSP 修改，
 *         请勿依赖其中的时域数据。
 */
void AI_FFT_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_BEAMFORMING_H */
