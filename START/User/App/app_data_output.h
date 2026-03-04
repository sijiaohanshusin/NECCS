/**
 * @file    app_data_output.h
 * @brief   调试数据输出接口
 * @details 通过 UART 输出调试数据到 VOFA+ 上位机
 *
 * VOFA+ 协议：
 * - 数据格式：float32 数组 + 尾帧 (0x00 0x00 0x80 0x7F)
 * - 波特率：921600
 * - 数据位：8
 * - 停止位：1
 * - 校验位：无
 *
 * 调试模式：
 * - Mode 0: RMS (有效值)
 * - Mode 1: FFT 频谱
 * - Mode 3: SRP 结果 + 热力图
 *
 * 使用方法：
 * 1. 在 app_main_task.c 中定义 DEBUG_ENABLE
 * 2. 设置 DEBUG_MODE (0/1/3)
 * 3. 设置 DEBUG_THROTTLE_FRAMES (输出节流)
 * 4. 打开 VOFA+ 连接串口
 */

#ifndef APP_DATA_OUTPUT_H
#define APP_DATA_OUTPUT_H

#include "arm_math.h"
#include "app_main_task.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 调试输出函数 (Debug Output Functions)
 * ============================================================================ */

/**
 * @brief   发送通道 RMS (有效值)
 * @details 计算并发送 16 路麦克风的 RMS 值
 *
 * 输出格式 (VOFA+)：
 * - 数据：16 个 float32 (RMS 值)
 * - 尾帧：0x00 0x00 0x80 0x7F
 *
 * RMS 计算：
 * - 公式：RMS = sqrt(sum(x^2) / N)
 * - 使用 CMSIS-DSP：arm_rms_f32()
 *
 * 用途：
 * - 检查麦克风是否正常工作
 * - 检查信号幅度是否合理
 * - 检查通道间是否平衡
 *
 * @note    在 Audio_Pipeline_Task 中调用
 * @note    需要定义 DEBUG_ENABLE 和 DEBUG_MODE=0
 */
void VOFA_Send_Channel_RMS(void);

/**
 * @brief   发送 FFT 频谱
 * @details 计算并发送单个通道的 FFT 幅度谱
 *
 * 输出格式 (VOFA+)：
 * - 数据：128 个 float32 (幅度谱)
 * - 频率范围：0 - 24kHz (bin 0-127)
 * - 尾帧：0x00 0x00 0x80 0x7F
 *
 * 幅度计算：
 * - 使用 CMSIS-DSP：arm_cmplx_mag_f32()
 * - 输入：Mic_Freq_Buffer (复数)
 * - 输出：幅度谱 (实数)
 *
 * 用途：
 * - 检查 FFT 是否正确
 * - 分析频谱特性
 * - 检查频域噪声
 *
 * @param   channel  通道索引 (0-15)
 *
 * @note    在 Audio_Pipeline_Task 中调用
 * @note    需要定义 DEBUG_ENABLE 和 DEBUG_MODE=1
 */
void VOFA_Send_FFT_Magnitude(uint8_t channel);

/**
 * @brief   发送 SRP 结果
 * @details 发送声源定位结果和热力图数据
 *
 * 输出格式 (VOFA+)：
 * - [0]: x_angle (水平角，度)
 * - [1]: y_angle (垂直角，度)
 * - [2]: energy (归一化能量)
 * - [3 ... 3+COARSE_TOTAL-1]: 粗搜功率图 (9×9 = 81 点)
 * - 诊断信息 (base = 3 + COARSE_TOTAL = 84):
 *   - [base+0]: g_audio_both_flags_count (丢帧计数)
 *   - [base+1]: uart_tx_drop_count (串口丢包计数)
 *   - [base+2]: g_srp_invalid_count (无效结果计数)
 *   - [base+3]: coarse_power_hold_count (功率保持计数)
 *   - [base+4]: g_srp_low_contrast_count (低对比度计数)
 *   - [base+5]: g_srp_last_contrast (上次对比度)
 *   - [base+6]: g_srp_last_quality (上次质量)
 *   - [base+7]: frame_seq (帧序号)
 * - 尾帧：0x00 0x00 0x80 0x7F
 *
 * 总数据量：
 * - 3 (位置) + 81 (热力图) + 8 (诊断) = 92 个 float32
 * - 92 × 4 + 4 = 372 字节
 *
 * 用途：
 * - 实时查看声源定位结果
 * - 可视化 SRP 功率分布
 * - 监控系统运行状态
 * - 调试算法参数
 *
 * VOFA+ 配置：
 * - 协议：JustFloat
 * - 通道数：92
 * - 波特率：921600
 *
 * @param   pos  声源位置结构体指针
 *
 * @note    在 Audio_Pipeline_Task 中调用
 * @note    需要定义 DEBUG_ENABLE 和 DEBUG_MODE=3
 * @note    输出频率受 DEBUG_THROTTLE_FRAMES 控制
 */
void VOFA_Send_SRP_Result(const Sound_Pos_t *pos);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_OUTPUT_H */
