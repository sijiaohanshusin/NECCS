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
#include "app_main_task.h"  // Sound_Pos_t

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

/**
 * @brief  初始化 SRP-PHAT 算法 (预计算角频率基数组)
 * @note   在 App_Stream_Init() 中调用一次
 */
void AI_SRP_PHAT_Init(void);

/**
 * @brief  SRP-PHAT 声源定位主函数
 *
 * @note   输入: Mic_Freq_Buffer (16 路 RFFT 复数频谱, 需先调用 AI_FFT_Process)
 *         输出: result->x_angle (水平方位角, 度)
 *               result->y_angle (垂直俯仰角, 度)
 *               result->energy  (归一化能量, [0, 1])
 *
 * @note   算法流程:
 *         1. GCC-PHAT: 40 对麦克风互功率谱 + PHAT 白化
 *         2. 粗搜: 7×7 网格 (±60°, 步长 20°), TDOA 从 Flash LUT 读取
 *         3. 精搜: Top-3 候选各 5×5 子网格 (±10°, 步长 5°), 运行时计算 TDOA
 *         4. 全局最大值 → 方位角 + 归一化能量
 *
 * @param  result  输出声源位置结构体指针
 */
void AI_SRP_PHAT_Process(Sound_Pos_t *result);

#ifdef __cplusplus
}
#endif

#endif /* AI_BEAMFORMING_H */
