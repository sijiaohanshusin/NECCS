/**
 * @file    app_profile.h
 * @brief   应用场景快速预设系统
 * @details 预定义 4 种检测场景 Profile，每个 Profile 批量设置
 *          频段、显示模式、灵敏度、平滑等参数。
 */
#ifndef __APP_PROFILE_H
#define __APP_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 预设场景枚举 */
typedef enum {
    APP_PROFILE_GENERAL    = 0u,  /**< 通用模式: 0.5-8 kHz, 均衡 */
    APP_PROFILE_GAS_LEAK   = 1u,  /**< 气体泄漏: 25-40 kHz, 高灵敏度 */
    APP_PROFILE_BEARING    = 2u,  /**< 轴承故障: 5-20 kHz, 中灵敏度 */
    APP_PROFILE_ELECTRICAL = 3u,  /**< 局部放电: 8-15 kHz, 高灵敏度 */
    APP_PROFILE_COUNT      = 4u   /**< 预设总数 */
} App_ProfileId_t;

/** @brief 预设参数结构体 */
typedef struct {
    const char *name;             /**< 显示名称 */
    uint16_t freq_lo_hz;          /**< 低频截止 (Hz) */
    uint16_t freq_hi_hz;          /**< 高频截止 (Hz) */
    float    noise_gate_ratio;    /**< 噪声门限比率 */
    float    gamma;               /**< Gamma 校正 */
    uint8_t  smooth_passes;       /**< 平滑次数 */
    uint8_t  fine_fusion;         /**< 精细融合使能 */
    uint8_t  display_mode;        /**< 显示模式 (0=FAST,1=BALANCED,2=CLEAN) */
    uint8_t  anomaly_enable;      /**< 异常检测使能 */
} App_ProfileDef_t;

/** @brief 初始化预设系统（默认为 GENERAL） */
void App_Profile_Init(void);

/**
 * @brief 应用指定预设
 * @param id 预设 ID
 * @details 批量设置运行时配置参数
 */
void App_Profile_Apply(App_ProfileId_t id);

/**
 * @brief 获取当前激活的预设 ID
 * @return 当前预设 ID
 */
App_ProfileId_t App_Profile_GetCurrent(void);

/**
 * @brief 获取预设定义（只读）
 * @param id 预设 ID
 * @return 预设定义结构体指针，无效 ID 返回 NULL
 */
const App_ProfileDef_t *App_Profile_GetDef(App_ProfileId_t id);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PROFILE_H */
