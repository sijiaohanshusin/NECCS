/**
 * @file    ai_bandpass.h
 * @brief   IIR 带通滤波器 —— 基于 CMSIS-DSP arm_biquad_cascade_df1_f32
 * @details 为 16 通道音频数据提供实时带通滤波，使用 4 阶 Butterworth（2 级 biquad 级联）。
 *          高通和低通各一级 biquad，总共每通道 2 级。
 *          系数由截止频率实时计算，支持 UI slider 驱动动态调整。
 *
 *          插入位置：Audio_Pipeline_Task 中 deinterleave 之后、FFT 之前。
 *          性能预算：16ch × 256样本 × 2级 biquad ≈ 0.2ms @ 480MHz。
 */
#ifndef __AI_BANDPASS_H
#define __AI_BANDPASS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 带通滤波通道数 */
#define AI_BANDPASS_NUM_CHANNELS  16u

/** @brief 每通道 biquad 级联级数 (高通 1 + 低通 1 = 2) */
#define AI_BANDPASS_NUM_STAGES    2u

/**
 * @brief 初始化带通滤波器模块
 * @details 将所有通道的 biquad 状态置零，设置默认截止频率。
 *          必须在 Audio_Pipeline_Task 启动前调用。
 */
void AI_Bandpass_Init(void);

/**
 * @brief 设置带通滤波器截止频率
 * @param f_lo  低截止频率 (Hz)，范围 [20, f_hi-100]
 * @param f_hi  高截止频率 (Hz)，范围 [f_lo+100, 23000]
 * @details 内部重新计算 Butterworth biquad 系数，并清零滤波器状态。
 *          线程安全：通过临界区保护系数写入。
 */
void AI_Bandpass_SetCutoff(float f_lo, float f_hi);

/**
 * @brief 获取当前截止频率
 * @param[out] f_lo  低截止频率指针
 * @param[out] f_hi  高截止频率指针
 */
void AI_Bandpass_GetCutoff(float *f_lo, float *f_hi);

/**
 * @brief 使能/禁用带通滤波
 * @param enable  1=启用，0=禁用（直通）
 */
void AI_Bandpass_SetEnabled(uint8_t enable);

/**
 * @brief 获取滤波使能状态
 * @return 1=启用，0=禁用
 */
uint8_t AI_Bandpass_GetEnabled(void);

/**
 * @brief 对单通道进行 IIR 带通滤波（就地处理）
 * @param ch      通道索引 [0, AI_BANDPASS_NUM_CHANNELS-1]
 * @param data    输入/输出缓冲区（float32，就地覆盖）
 * @param length  样本数
 */
void AI_Bandpass_ProcessChannel(uint8_t ch, float *data, uint16_t length);

/**
 * @brief 对全部 16 通道进行带通滤波（就地处理）
 * @param ch_data   16 个通道的数据指针数组
 * @param length    每通道样本数
 */
void AI_Bandpass_ProcessAll(float *ch_data[], uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* __AI_BANDPASS_H */
