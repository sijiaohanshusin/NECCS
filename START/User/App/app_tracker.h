/**
 * @file    app_tracker.h
 * @brief   多声源帧间跟踪器头文件
 * @details
 * 跟踪算法原理：
 *   每帧调用 App_Tracker_Update()，采用「贪心最近邻匹配」将新检测结果与
 *   已有跟踪目标关联，实现跨帧稳定的声源 ID 分配。
 *
 * 匹配规则（贪心最近邻）：
 *   1. 对每个新检测到的声源，在已有目标中找角度距离最近的目标
 *   2. 若距离 < 阈值（默认 10°），则将两者匹配，位置用 EMA 平滑更新
 *   3. 若没有足够近的已有目标，则创建新目标，分配新 ID
 *   4. 若某已有目标连续 TRACKER_MAX_MISSED 帧未被匹配，则从列表中删除
 *
 * EMA 位置平滑：
 *   x_new = x_old × (1 - α) + x_det × α，α = 0.4（默认）
 *   α 越大，响应越快但抖动越大；α 越小，越平滑但有延迟
 *
 * ID 生命周期：
 *   新目标 ID 从 1 开始分配（0 为无效 ID），到 255 后回绕到 1（跳过 0）。
 *   由于 ID 是 uint8_t，系统长期运行约 255 个新目标后 ID 会复用，
 *   但通常不影响使用（跟踪目标不会长期超过 MULTI_SOURCE_MAX 个）。
 *
 * [改进] 当前为贪心匹配（O(N×M)），不保证全局最优，
 *        可改为匈牙利算法（最优二分图匹配），适用于目标数较多时。
 * [改进] EMA 系数固定，可改为基于速度自适应（卡尔曼滤波器更优）。
 * [注意] 此模块无线程锁，仅由 Audio_Pipeline_Task 调用，无跨任务竞争。
 */
#ifndef __APP_TRACKER_H                 /* 防止头文件被多次包含 */
#define __APP_TRACKER_H

#include <stdint.h>                     /* uint8_t, float */
#include "app_types.h"                  /* Sound_MultiPos_t, MULTI_SOURCE_MAX */

#ifdef __cplusplus
extern "C" {                            /* 允许 C++ 工程包含此头文件 */
#endif

/** @brief 无效跟踪 ID（表示目标未初始化或已被删除）*/
#define APP_TRACKER_ID_INVALID  0u

/**
 * @brief 单个跟踪目标的状态
 * @details age 统计该目标已存活的帧数（越大越稳定），
 *          missed 统计连续未匹配帧数（越大越可能是消失的目标）。
 */
typedef struct {
    uint8_t  id;              /**< 跟踪 ID（1-255，0=无效）；每次新建目标递增分配 */
    uint8_t  age;             /**< 已存活帧数（最大 255 钳位，防溢出）；越大说明目标越稳定 */
    uint8_t  missed;          /**< 连续未匹配帧数；超过 TRACKER_MAX_MISSED 则删除此目标 */
    uint8_t  reserved;        /**< 保留字段，填 0 用于 4 字节对齐 */
    float    x_angle;         /**< EMA 平滑后的水平角（度）；比瞬时检测结果更稳定 */
    float    y_angle;         /**< EMA 平滑后的垂直角（度）*/
    float    energy;          /**< 最近一帧的能量值（不平滑，反映当前声源强度）*/
} App_TrackerTarget_t;

/**
 * @brief 跟踪器输出结构体
 * @details GetResult() 将当前所有活跃目标填入此结构体。
 */
typedef struct {
    App_TrackerTarget_t targets[MULTI_SOURCE_MAX];  /**< 活跃目标数组 */
    uint8_t count;            /**< 当前活跃目标数（0..MULTI_SOURCE_MAX）*/
} App_TrackerResult_t;

/**
 * @brief  初始化跟踪器（清空目标列表，重置 ID 计数器）
 * @details 应在系统启动时或模式切换时调用。
 */
void App_Tracker_Init(void);

/**
 * @brief  更新跟踪器（每帧调用）
 * @details 执行贪心匹配：将 multi 中的新检测与已有目标关联，
 *          更新匹配目标位置（EMA），删除超时目标，创建新目标。
 * @param  multi 当前帧的多声源检测结果（来自 SRP-PHAT 峰值搜索）
 */
void App_Tracker_Update(const Sound_MultiPos_t *multi);

/**
 * @brief  获取当前跟踪结果
 * @details 将活跃目标数组和数量复制到调用方提供的结构体。
 * @param  result 输出跟踪结果（空指针安全）
 */
void App_Tracker_GetResult(App_TrackerResult_t *result);

/**
 * @brief  设置角度匹配阈值（欧氏角度距离）
 * @details 阈值越大，匹配容忍度越高（允许声源位置更大的跳动）；
 *          阈值越小，越精确（但可能将同一声源误判为新目标）。
 *          建议范围：5°~20°；默认 10°。
 * @param  deg 最大匹配角度距离（度），应 > 0
 */
void App_Tracker_SetMatchThreshold(float deg);

/**
 * @brief  获取当前角度匹配阈值
 * @return 当前阈值（度）
 */
float App_Tracker_GetMatchThreshold(void);

/**
 * @brief  重置跟踪器（清除所有目标，但保留阈值设置）
 * @details 用于模式切换或测试场景需要清除历史跟踪状态时调用。
 *          不重置 s_next_id 和 s_match_threshold（保留用户设置）。
 */
void App_Tracker_Reset(void);

#ifdef __cplusplus
}                                       /* extern "C" 结束 */
#endif

#endif /* __APP_TRACKER_H */

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
