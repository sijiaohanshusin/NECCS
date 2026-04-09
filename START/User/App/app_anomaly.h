/**
 * @file    app_anomaly.h
 * @brief   异常声音检测与历史日志
 * @details 基于噪声底偏离检测异常声音事件，维护 64 条环形日志。
 *          当某 bin 的当前能量超过噪声底 N 倍标准差时标记为异常。
 */
#ifndef __APP_ANOMALY_H
#define __APP_ANOMALY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 异常日志容量 */
#define APP_ANOMALY_LOG_SIZE  64u

/** @brief 单条异常记录 */
typedef struct {
    uint32_t tick;            /**< 发生时刻 (FreeRTOS tick, ms) */
    float    x_angle;         /**< 声源水平角 (度) */
    float    y_angle;         /**< 声源垂直角 (度) */
    float    energy;          /**< 触发瞬间能量 */
    float    deviation;       /**< 偏离倍数 (当前/噪声底) */
    uint16_t peak_bin;        /**< 峰值频率 bin */
    uint16_t reserved;        /**< 对齐 */
} App_AnomalyEntry_t;

/** @brief 初始化异常检测模块 */
void App_Anomaly_Init(void);

/**
 * @brief 馈送频谱帧，检测异常
 * @param magnitude  当前帧幅度数组
 * @param floor      噪声底数组
 * @param bin_count  bin 数量
 * @param x_angle    当前声源水平角
 * @param y_angle    当前声源垂直角
 * @param energy     当前帧能量
 * @return 1=检测到异常，0=正常
 */
uint8_t App_Anomaly_Feed(const float *magnitude, const float *floor,
                         uint16_t bin_count,
                         float x_angle, float y_angle, float energy);

/**
 * @brief 设置异常检测阈值（偏离倍数）
 * @param ratio 阈值倍数，默认 3.0（即 3σ）
 */
void App_Anomaly_SetThreshold(float ratio);

/**
 * @brief 获取异常检测阈值
 * @return 当前阈值倍数
 */
float App_Anomaly_GetThreshold(void);

/**
 * @brief 获取异常日志条目
 * @param index 条目索引（0 = 最新）
 * @param entry 输出条目指针
 * @return 1=有效, 0=无效（索引超出范围或无数据）
 */
uint8_t App_Anomaly_GetEntry(uint32_t index, App_AnomalyEntry_t *entry);

/**
 * @brief 获取日志中有效条目数
 * @return 有效条目数 [0, APP_ANOMALY_LOG_SIZE]
 */
uint32_t App_Anomaly_GetCount(void);

/**
 * @brief 清空异常日志
 */
void App_Anomaly_ClearLog(void);

/**
 * @brief 使能/禁用异常检测
 * @param enable 1=启用，0=禁用
 */
void App_Anomaly_SetEnabled(uint8_t enable);

/**
 * @brief 获取异常检测使能状态
 * @return 1=已启用，0=已禁用
 */
uint8_t App_Anomaly_GetEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_ANOMALY_H */
