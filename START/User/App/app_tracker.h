/**
 * @file    app_tracker.h
 * @brief   多声源帧间跟踪器
 * @details 基于角度距离的简单 IOU 匹配，为每个声源分配持续 ID，
 *          实现帧间关联和生命周期管理。最多同时跟踪 MULTI_SOURCE_MAX 个源。
 */
#ifndef __APP_TRACKER_H
#define __APP_TRACKER_H

#include <stdint.h>
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 无效跟踪 ID */
#define APP_TRACKER_ID_INVALID  0u

/** @brief 单个跟踪目标状态 */
typedef struct {
    uint8_t  id;              /**< 跟踪 ID (1-255, 0=无效) */
    uint8_t  age;             /**< 存活帧数 (最大 255) */
    uint8_t  missed;          /**< 连续未匹配帧数 */
    uint8_t  reserved;
    float    x_angle;         /**< 平滑后水平角 */
    float    y_angle;         /**< 平滑后垂直角 */
    float    energy;          /**< 最新能量 */
} App_TrackerTarget_t;

/** @brief 跟踪器输出 */
typedef struct {
    App_TrackerTarget_t targets[MULTI_SOURCE_MAX];
    uint8_t count;            /**< 活跃目标数 */
} App_TrackerResult_t;

/** @brief 初始化跟踪器 */
void App_Tracker_Init(void);

/**
 * @brief 更新跟踪器（每帧调用）
 * @param multi 当前帧多声源检测结果
 */
void App_Tracker_Update(const Sound_MultiPos_t *multi);

/**
 * @brief 获取当前跟踪结果
 * @param result 输出跟踪结果
 */
void App_Tracker_GetResult(App_TrackerResult_t *result);

/**
 * @brief 设置角度匹配阈值
 * @param deg 最大匹配角度距离（度），默认 10.0
 */
void App_Tracker_SetMatchThreshold(float deg);

/**
 * @brief 获取角度匹配阈值
 * @return 当前阈值（度）
 */
float App_Tracker_GetMatchThreshold(void);

/**
 * @brief 重置跟踪器（清除所有目标）
 */
void App_Tracker_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TRACKER_H */
