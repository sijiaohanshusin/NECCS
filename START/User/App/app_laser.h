/**
 * @file    app_laser.h
 * @brief   激光瞄准器与夜间模式控制
 * @details 激光二极管通过 GPIO PB0 驱动（推挽输出）。
 *          夜间模式联动：摄像头关闭 + 激光开启 + gamma 降低 + UI 高对比度。
 */
#ifndef __APP_LASER_H
#define __APP_LASER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 激光状态 */
typedef enum {
    APP_LASER_OFF = 0u,
    APP_LASER_ON  = 1u
} App_LaserState_t;

/** @brief 夜间模式状态 */
typedef enum {
    APP_NIGHTMODE_OFF = 0u,
    APP_NIGHTMODE_ON  = 1u
} App_NightModeState_t;

/** @brief 初始化激光 GPIO（PB0 推挽输出，默认低电平） */
void App_Laser_Init(void);

/** @brief 设置激光状态 */
void App_Laser_SetState(App_LaserState_t state);

/** @brief 切换激光开关 */
void App_Laser_Toggle(void);

/** @brief 获取激光当前状态 */
App_LaserState_t App_Laser_GetState(void);

/** @brief 启用夜间模式（联动激光+摄像头+gamma） */
void App_NightMode_Enable(void);

/** @brief 禁用夜间模式 */
void App_NightMode_Disable(void);

/** @brief 切换夜间模式 */
void App_NightMode_Toggle(void);

/** @brief 获取夜间模式状态 */
App_NightModeState_t App_NightMode_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LASER_H */
