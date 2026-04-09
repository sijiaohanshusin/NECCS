/**
 * @file    app_capture.h
 * @brief   屏幕截图模块 — 状态管理与触发
 * @details 截图 I/O 由 Storage_Task 执行, 本模块负责触发和状态跟踪。
 */
#ifndef __APP_CAPTURE_H
#define __APP_CAPTURE_H

#include <stdint.h>
#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 截图状态 */
typedef enum {
    CAPTURE_IDLE     = 0u,
    CAPTURE_BUSY     = 1u,
    CAPTURE_DONE     = 2u,
    CAPTURE_ERROR    = 3u
} App_CaptureState_t;

/** @brief 初始化截图模块 */
void App_Capture_Init(void);

/**
 * @brief 触发一次截图 (发送命令到 Storage_Task 队列)
 * @return ERR_OK 已排入队列, ERR_BUSY 队列满或上次未完成
 */
Err_t App_Capture_Trigger(void);

/**
 * @brief 获取截图状态
 * @return 当前状态
 */
App_CaptureState_t App_Capture_GetState(void);

/**
 * @brief 获取已截图计数
 * @return 截图总数
 */
uint32_t App_Capture_GetCount(void);

/**
 * @brief 设置截图状态 (由 Storage_Task 调用)
 * @param state 新状态
 */
void App_Capture_SetState(App_CaptureState_t state);

/**
 * @brief 递增截图计数 (由 Storage_Task 在成功写入后调用)
 */
void App_Capture_IncrementCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CAPTURE_H */
