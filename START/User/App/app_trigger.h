/**
 * @file    app_trigger.h
 * @brief   瞬态捕捉触发模式 —— 能量突变检测与画面冻结
 * @details 实现 ARMED → TRIGGERED → HOLD → RE-ARM 状态机：
 *          - ARMED   : 待机，持续监测能量变化
 *          - TRIGGERED: 检测到能量突变，记录当前帧
 *          - HOLD    : 冻结显示，保持 TRIGGERED 时刻的数据
 *          - 重新 ARM 可手动或自动（超时后）
 */
#ifndef __APP_TRIGGER_H
#define __APP_TRIGGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 触发状态枚举 */
typedef enum {
    APP_TRIGGER_IDLE      = 0u,   /**< 触发模式未启用 */
    APP_TRIGGER_ARMED     = 1u,   /**< 已武装，等待触发 */
    APP_TRIGGER_TRIGGERED = 2u,   /**< 已触发，画面冻结中 */
} App_TriggerState_t;

/** @brief 触发统计信息 */
typedef struct {
    uint32_t trigger_count;       /**< 总触发次数 */
    uint32_t last_trigger_tick;   /**< 最近一次触发时的 tick (ms) */
    float    last_trigger_energy; /**< 触发瞬间能量 */
    float    last_trigger_x;      /**< 触发瞬间 x 角度 */
    float    last_trigger_y;      /**< 触发瞬间 y 角度 */
} App_TriggerStats_t;

/** @brief 触发回调函数类型 */
typedef void (*App_TriggerCallback_t)(float energy);

/** @brief 初始化触发模块 */
void App_Trigger_Init(void);

/** @brief 武装触发器（开始监测） */
void App_Trigger_Arm(void);

/** @brief 解除触发器（回到 IDLE） */
void App_Trigger_Disarm(void);

/** @brief 手动重新武装（从 TRIGGERED 回到 ARMED） */
void App_Trigger_Rearm(void);

/**
 * @brief 馈送能量值（由 UI 任务每帧调用）
 * @param energy 当前帧归一化能量 [0,1]
 * @return 1=刚触发（状态从 ARMED → TRIGGERED），0=未触发
 */
uint8_t App_Trigger_Feed(float energy);

/**
 * @brief 获取当前触发状态
 * @return 触发状态枚举
 */
App_TriggerState_t App_Trigger_GetState(void);

/**
 * @brief 设置触发阈值
 * @param threshold 能量变化阈值（如 0.15 表示 15% 突变触发）
 */
void App_Trigger_SetThreshold(float threshold);

/**
 * @brief 获取触发阈值
 * @return 当前阈值
 */
float App_Trigger_GetThreshold(void);

/**
 * @brief 记录触发瞬间的声源位置（由 UI 任务在触发时调用）
 * @param x_angle  水平角
 * @param y_angle  垂直角
 */
void App_Trigger_SetTriggeredPos(float x_angle, float y_angle);

/**
 * @brief 获取触发统计信息
 * @param[out] stats 统计信息输出
 */
void App_Trigger_GetStats(App_TriggerStats_t *stats);

/**
 * @brief 注册触发回调（触发发生时调用，可用于 SD 卡截图等）
 * @param cb 回调函数指针，NULL 取消注册
 */
void App_Trigger_RegisterCallback(App_TriggerCallback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TRIGGER_H */
