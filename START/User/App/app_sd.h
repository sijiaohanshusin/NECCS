/**
 * @file    app_sd.h
 * @brief   SD 卡应用层抽象 — 初始化 / 状态 / 容量查询
 * @details 负责 FatFS 挂载、目录创建和容量查询。
 *          所有文件 I/O (BMP/WAV) 由 app_storage_task 内部完成,
 *          FIL 句柄不通过本头文件暴露。
 */
#ifndef __APP_SD_H
#define __APP_SD_H

#include <stdint.h>
#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SD 卡挂载状态 */
typedef enum {
    APP_SD_NOT_MOUNTED = 0u,
    APP_SD_MOUNTED     = 1u,
    APP_SD_ERROR       = 2u
} App_SD_MountState_t;

/** @brief SD 卡容量信息 */
typedef struct {
    uint32_t total_mb;        /**< 总容量 (MB) */
    uint32_t free_mb;         /**< 可用容量 (MB) */
    uint32_t used_mb;         /**< 已用容量 (MB) */
} App_SD_SpaceInfo_t;

/**
 * @brief 初始化 SD 卡子系统 (BSP + FatFS 挂载 + 目录创建)
 * @return ERR_OK 成功, ERR_IO_FAILED 失败
 */
Err_t App_SD_Init(void);

/**
 * @brief 反初始化 SD 卡子系统
 */
void App_SD_DeInit(void);

/**
 * @brief 获取挂载状态
 * @return 当前挂载状态
 */
App_SD_MountState_t App_SD_GetState(void);

/**
 * @brief 获取容量信息 (读缓存值, 非阻塞)
 * @param info 输出容量信息
 * @return ERR_OK 或 ERR_NOT_INIT
 */
Err_t App_SD_GetSpace(App_SD_SpaceInfo_t *info);

/**
 * @brief 刷新容量信息 (调用 f_getfree, 可能阻塞 50-200 ms)
 * @return ERR_OK 或 ERR_IO_FAILED
 * @note   应在 Storage_Task 中定期调用, 不可在音频/UI 任务中调用
 */
Err_t App_SD_RefreshSpace(void);

/**
 * @brief 生成自动编号文件路径
 * @param dir      目录名 (如 "CAPTURE")
 * @param prefix   文件名前缀 (如 "IMG")
 * @param ext      扩展名 (如 "bmp")
 * @param out_path 输出完整路径缓冲 (最少 64 字节)
 * @param path_len 缓冲长度
 * @return ERR_OK 或 ERR_INVALID_ARG
 * @note   格式: 0:/dir/prefix_XXXXXX.ext (6 位序号)
 */
Err_t App_SD_MakeFilePath(const char *dir, const char *prefix,
                           const char *ext, char *out_path, uint16_t path_len);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SD_H */
