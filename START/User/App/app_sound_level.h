/**
 * @file    app_sound_level.h
 * @brief   声级计模式 —— dB(A) 加权与 Leq 积分
 * @details 提供 A 加权频谱能量计算和等效连续声级 (Leq) 积分，
 *          近似 IEC 61672 标准。基于已有频谱数据（FFT 幅度），
 *          无需额外 IIR 滤波器。
 */
#ifndef __APP_SOUND_LEVEL_H
#define __APP_SOUND_LEVEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 加权类型 */
typedef enum {
    APP_SLM_WEIGHT_A = 0u,   /**< A 加权 (IEC 61672) */
    APP_SLM_WEIGHT_C = 1u,   /**< C 加权 */
    APP_SLM_WEIGHT_Z = 2u    /**< Z (无加权，线性) */
} App_SLM_Weight_t;

/** @brief 声级计读数 */
typedef struct {
    float db_inst;           /**< 瞬时声级 (dB) */
    float db_leq;            /**< 等效连续声级 Leq (dB) */
    float db_max;            /**< 最大声级 (dB) */
    float db_min;            /**< 最小声级 (dB) */
    uint32_t leq_frames;     /**< Leq 积分帧数 */
} App_SLM_Reading_t;

/** @brief 初始化声级计模块 */
void App_SLM_Init(void);

/**
 * @brief 馈送频谱帧，更新声级读数
 * @param magnitude FFT 幅度数组
 * @param bin_count bin 数量
 * @param delta_f   频率分辨率 (Hz/bin)
 */
void App_SLM_Feed(const float *magnitude, uint16_t bin_count, float delta_f);

/**
 * @brief 获取当前声级读数
 * @param reading 输出读数
 */
void App_SLM_GetReading(App_SLM_Reading_t *reading);

/**
 * @brief 设置加权类型
 * @param weight 加权枚举
 */
void App_SLM_SetWeight(App_SLM_Weight_t weight);

/**
 * @brief 获取加权类型
 * @return 当前加权类型
 */
App_SLM_Weight_t App_SLM_GetWeight(void);

/**
 * @brief 复位 Leq 积分（重新开始测量）
 */
void App_SLM_ResetLeq(void);

/**
 * @brief 复位最大/最小值
 */
void App_SLM_ResetPeak(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SOUND_LEVEL_H */
