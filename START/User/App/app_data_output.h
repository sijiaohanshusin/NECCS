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
#include "app_main_task.h"
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
 * @brief 【模式2 - 原始TDM诊断】直接从 DMA 缓冲区计算各槽位 AC-RMS，完全绕过解交织
 *
 * 每次发送 16 个 float32_t + 4字节帧尾，共 68 字节。
 * VOFA+ 设置 16 个通道。
 *
 * 用途：确定"ch4/ch5相同"是软件解交织的问题，还是 PCMD3180 输出的问题。
 *
 *   结果 A: TDM 槽位 4 和 5 的值不同，但软件通道 4 和 5 相同
 *            → 软件解交织存在 Bug（可以继续排查代码）
 *
 *   结果 B: TDM 槽位 4 和 5 的值本身就相同
 *            → 问题在 PCMD3180 配置或硬件（进行硬件排查）
 *
 * @note 直接读 Mic_Rx_Buffer (SRAM1, Non-Cacheable)，在预处理任务之外调用安全
 * @note 必须在 Audio_Preprocess_Task 触发后的任意时刻调用（数据已被 DMA 写入）
 */
void VOFA_Send_Raw_TDM_Slot_RMS(void);

/**
 * @brief 【模式3 - 频谱查看】发送指定通道的 FFT 幅度谱
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

/**
 * @brief 【模式4 - SRP结果】发送声源定位结果 + 粗搜功率图
 *
 * 每次发送 [θh, θv, energy, SRP_Power[0..48]] = 52 个 float + 4字节帧尾，共 212 字节。
 * VOFA+ 设置 52 个通道:
 *   - 通道 0: 水平方位角 (度)
 *   - 通道 1: 垂直俯仰角 (度)
 *   - 通道 2: 归一化能量 [0, 1]
 *   - 通道 3~51: 粗搜 49 点功率图 (可用于可视化 SRP 功率分布)
 *
 * @param pos  声源定位结果指针
 *
 * @warning 必须在 AI_SRP_PHAT_Process() 调用"之后"调用！
 */
void VOFA_Send_SRP_Result(const Sound_Pos_t *pos);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_OUTPUT_H */
