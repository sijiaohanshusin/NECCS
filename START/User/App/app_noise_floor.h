/**
 * @file    app_noise_floor.h
 * @brief   自适应背景噪声底估计与频域减噪
 * @details 维护 128-bin 长期 EMA 噪声底，提供频域减噪和"一键校零"功能。
 *          在 SRP-PHAT 的 GCC-PHAT 阶段后、SRP 累加前应用。
 */
#ifndef __APP_NOISE_FLOOR_H
#define __APP_NOISE_FLOOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化噪声底模块 */
void App_NoiseFloor_Init(void);

/**
 * @brief 更新噪声底估计
 * @param magnitude 当前帧各 bin 的幅度数组
 * @param bin_count 数组长度
 */
void App_NoiseFloor_Update(const float *magnitude, uint16_t bin_count);

/**
 * @brief 获取噪声底估计值
 * @param out_floor 输出噪声底数组（长度需 >= bin_count）
 * @param bin_count 请求的 bin 数
 */
void App_NoiseFloor_Get(float *out_floor, uint16_t bin_count);

/**
 * @brief "一键校零"：将当前噪声底复位为当前帧的幅度
 * @param magnitude 当前帧各 bin 的幅度数组
 * @param bin_count 数组长度
 */
void App_NoiseFloor_Calibrate(const float *magnitude, uint16_t bin_count);

/**
 * @brief 设置噪声底 EMA 平滑系数
 * @param alpha EMA 系数 (0.001 ~ 0.1)，越小越慢
 */
void App_NoiseFloor_SetAlpha(float alpha);

/**
 * @brief 获取 EMA 平滑系数
 * @return 当前 alpha 值
 */
float App_NoiseFloor_GetAlpha(void);

/**
 * @brief 使能/禁用噪声减除
 * @param enable 1=启用，0=禁用
 */
void App_NoiseFloor_SetEnabled(uint8_t enable);

/**
 * @brief 获取噪声减除使能状态
 * @return 1=已启用，0=已禁用
 */
uint8_t App_NoiseFloor_GetEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_NOISE_FLOOR_H */
