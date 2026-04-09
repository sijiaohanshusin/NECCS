/**
 * @file    app_storage_task.h
 * @brief   SD 卡异步存储任务 — BMP 截图与 WAV 录音 I/O
 * @details 所有 FatFS 文件操作集中在 Storage_Task 中执行,
 *          其他任务通过命令队列触发 I/O 请求。
 */
#ifndef __APP_STORAGE_TASK_H
#define __APP_STORAGE_TASK_H

#include <stdint.h>
#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 存储命令类型 */
typedef enum {
    STORAGE_CMD_CAPTURE_BMP  = 0u,  /**< 截取当前 LTDC 前缓冲为 BMP */
    STORAGE_CMD_REC_START    = 1u,  /**< 开始 WAV 录音 */
    STORAGE_CMD_REC_STOP     = 2u   /**< 停止 WAV 录音并回填头 */
} App_StorageCmd_e;

/** @brief 存储命令消息 */
typedef struct {
    App_StorageCmd_e cmd;       /**< 命令类型 */
    uint32_t         param;     /**< 命令参数 (如 REC_START 的录音模式) */
} App_StorageMsg_t;

/** @brief 存储任务状态 */
typedef enum {
    STORAGE_STATE_IDLE      = 0u,
    STORAGE_STATE_CAPTURING = 1u,
    STORAGE_STATE_RECORDING = 2u,
    STORAGE_STATE_ERROR     = 3u
} App_StorageState_e;

/**
 * @brief 初始化存储任务 (创建队列 + FreeRTOS 任务)
 * @note  在 App_SD_Init() 之后调用
 */
void App_Storage_Init(void);

/**
 * @brief 发送存储命令
 * @param cmd   命令类型
 * @param param 命令参数
 * @return ERR_OK 已入队, ERR_BUSY 队列满
 */
Err_t App_Storage_SendCmd(App_StorageCmd_e cmd, uint32_t param);

/**
 * @brief 获取存储任务状态
 * @return 当前状态
 */
App_StorageState_e App_Storage_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_STORAGE_TASK_H */
