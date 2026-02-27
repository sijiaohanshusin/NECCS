/**
 * @file    app_data_output.h
 * @brief   VOFA+ JustFloat 调试数据输出接口
 *
 * @note    用途：通过串口将 FFT 数据发送到 VOFA+，验证麦克风焊接情况
 *
 * ======================== VOFA+ 使用步骤 ========================
 *  1. 打开 VOFA+，顶部协议选 "JustFloat"
 *  2. 选择正确的 COM 口，波特率必须设置为 921600
 *  3. 点击右上角 "添加控件" -> "波形图 (Chart)"
 *  4. 根据发送的数据类型设置通道数：
 *     - VOFA_Send_Channel_RMS()  -> 设置 16 个通道
 *     - VOFA_Send_FFT_Magnitude()-> 设置 128 个通道
 *  5. 点击"连接"即可看到实时波形
 * ===============================================================
 */

#ifndef APP_DATA_OUTPUT_H
#define APP_DATA_OUTPUT_H

#include "arm_math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 【模式1 - 快速检测】发送 16 路通道的 RMS 能量值
 *
 * 每次发送 16 个 float32_t + 4字节帧尾，共 68 字节。
 * 在 VOFA+ 波形图中，哪条线接近 0 就说明哪路麦克风没有信号（虚焊或损坏）。
 *
 * @warning 必须在 AI_FFT_Process() 调用"之前"调用！
 *          因为 RFFT 会修改 Mic_Process_Buffer 的内容。
 */
void VOFA_Send_Channel_RMS(void);

/**
 * @brief 【模式2 - 频谱查看】发送指定通道的 FFT 幅度谱
 *
 * 每次发送 128 个 float32_t + 4字节帧尾，共 516 字节。
 * 频率分辨率: 48000Hz / 256 = 187.5 Hz/bin
 * 在 VOFA+ 中可以看到该通道完整的频率分布（类似频谱仪）。
 *
 * @param channel 要查看的通道号 [0 ~ 15]
 *
 * @warning 必须在 AI_FFT_Process() 调用"之后"调用！
 *          因为需要频域数据 Mic_Freq_Buffer。
 */
void VOFA_Send_FFT_Magnitude(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_OUTPUT_H */
