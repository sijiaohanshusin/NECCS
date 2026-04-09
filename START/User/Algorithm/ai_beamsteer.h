/**
 * @file    ai_beamsteer.h
 * @brief   DAS 延迟求和波束控向 —— 定向录音
 * @details 基于延迟求和 (Delay-and-Sum) 波束成形，从 16 通道麦克风
 *          信号中提取指定方向的增强单通道音频。支持自动追踪 SRP-PHAT
 *          主声源方向和手动固定方向两种模式。
 */
#ifndef __AI_BEAMSTEER_H
#define __AI_BEAMSTEER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 波束追踪模式 */
typedef enum {
    BEAMSTEER_MODE_AUTO    = 0u,  /**< 自动追踪 SRP-PHAT 主源方向 */
    BEAMSTEER_MODE_MANUAL  = 1u,  /**< 手动固定方向 */
    BEAMSTEER_MODE_TRIGGER = 2u   /**< 触发时锁定方向 */
} AI_BeamSteer_Mode_t;

/** @brief 初始化波束控向模块 */
void AI_BeamSteer_Init(void);

/**
 * @brief 设置波束指向方向
 * @param theta_deg 水平角 (度)
 * @param phi_deg   垂直角 (度)
 */
void AI_BeamSteer_SetDirection(float theta_deg, float phi_deg);

/**
 * @brief 获取当前波束方向
 * @param theta_deg 输出水平角
 * @param phi_deg   输出垂直角
 */
void AI_BeamSteer_GetDirection(float *theta_deg, float *phi_deg);

/**
 * @brief 执行 DAS 波束成形
 * @param input      多通道平面输入 (Mic_Process_Buffer, float[16][frame_len])
 * @param output     单通道增强输出 (float[frame_len])
 * @param frame_len  帧长度 (256)
 */
void AI_BeamSteer_Process(const float *input, float *output, uint16_t frame_len);

/**
 * @brief 设置追踪模式
 * @param mode 追踪模式枚举
 */
void AI_BeamSteer_SetMode(AI_BeamSteer_Mode_t mode);

/**
 * @brief 获取追踪模式
 * @return 当前追踪模式
 */
AI_BeamSteer_Mode_t AI_BeamSteer_GetMode(void);

/**
 * @brief 使能/禁用波束成形处理
 * @param enable 1=启用，0=禁用
 */
void AI_BeamSteer_SetEnabled(uint8_t enable);

/**
 * @brief 获取波束成形使能状态
 * @return 1=已启用，0=已禁用
 */
uint8_t AI_BeamSteer_GetEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __AI_BEAMSTEER_H */
