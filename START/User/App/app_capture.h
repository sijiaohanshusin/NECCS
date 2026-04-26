/**
 * @file    app_capture.h
 * @brief   屏幕截图模块头文件 — 状态管理与触发接口
 * @details
 * 架构说明：
 *   截图功能跨越两个任务：
 *     - UI_Task（调用方）：检测用户触发，调用 App_Capture_Trigger() 发送命令到队列
 *     - Storage_Task（执行方）：从队列取命令，读帧缓冲，写 BMP 文件，更新状态
 *
 *   本模块只提供状态跟踪和触发的接口，不直接操作文件系统和帧缓冲。
 *   这是典型的「命令模式」调度：触发者和执行者解耦。
 *
 * 状态机：
 *   IDLE ──[Trigger()]──> BUSY ──[Storage写完]──> DONE
 *                                └─[Storage失败]──> ERROR
 *   DONE/ERROR ──[下次Trigger()]──> BUSY（会自动复位）
 *
 * [注意] App_Capture_SetState() 和 App_Capture_IncrementCount() 由
 *        Storage_Task 从另一个 FreeRTOS 任务调用，属于跨任务写操作。
 *        目前靠 volatile 保证可见性（非原子），对大多数场景够用，
 *        但高频触发可能出现极小的竞态窗口（见 app_capture.c 中的说明）。
 */
#ifndef __APP_CAPTURE_H                 /* 防止头文件被多次包含 */
#define __APP_CAPTURE_H

#include <stdint.h>                     /* uint32_t 等基础整数类型 */
#include "error_code.h"                 /* Err_t 统一错误码（ERR_OK / ERR_BUSY 等）*/

#ifdef __cplusplus
extern "C" {                            /* 允许 C++ 工程混用此头文件 */
#endif

/**
 * @brief 截图状态枚举
 * @details 状态由 Storage_Task 在写入过程中更新，UI_Task 轮询读取显示。
 *          枚举值后缀 `u` 表示无符号（ARM Compiler 5 风格，避免视为有符号值）。
 */
typedef enum {
    CAPTURE_IDLE     = 0u,  /**< 空闲：无截图请求，可接受新触发 */
    CAPTURE_BUSY     = 1u,  /**< 忙：Storage_Task 正在写 BMP 文件 */
    CAPTURE_DONE     = 2u,  /**< 完成：本次截图成功写入 SD 卡 */
    CAPTURE_ERROR    = 3u   /**< 出错：写入失败（SD 卡未插/空间不足/FAT 错误）*/
} App_CaptureState_t;

/**
 * @brief 初始化截图模块
 * @details 将状态复位为 IDLE，计数清零。
 *          应在 FreeRTOS 任务创建前或 app_main_task 任务开始时调用一次。
 */
void App_Capture_Init(void);

/**
 * @brief 触发一次截图（发送命令到 Storage_Task 队列）
 * @details 触发后本函数立即返回，实际写文件在 Storage_Task 中异步完成。
 *          如果当前状态为 BUSY，说明上一次截图尚未完成，触发会失败。
 * @return ERR_OK   命令已成功排入 Storage_Task 队列
 * @return ERR_BUSY 上次截图仍在执行中，或命令队列已满
 */
Err_t App_Capture_Trigger(void);

/**
 * @brief 查询当前截图状态
 * @details UI 层可以轮询此函数，在 DONE 时显示截图成功提示，在 ERROR 时显示失败提示。
 * @return 当前截图状态（IDLE/BUSY/DONE/ERROR）
 */
App_CaptureState_t App_Capture_GetState(void);

/**
 * @brief 查询已完成的截图总数
 * @details 用于在 UI 上显示 "Screenshot #N saved" 类提示，或用于文件命名。
 *          每次截图成功后由 Storage_Task 调用 IncrementCount() 自增。
 * @return 截图成功总次数（系统上电后累积，不持久化）
 */
uint32_t App_Capture_GetCount(void);

/**
 * @brief 更新截图状态（仅由 Storage_Task 调用）
 * @details Storage_Task 在写入开始时设为 BUSY，写入完成时设为 DONE/ERROR。
 *          [注意] 此函数从 Storage_Task 上下文写入，UI_Task 读取，属于跨任务访问。
 *                 当前依赖 volatile 保证可见性，未使用互斥锁。
 * @param state 新状态（应为 BUSY/DONE/ERROR 之一）
 */
void App_Capture_SetState(App_CaptureState_t state);

/**
 * @brief 截图成功计数 +1（仅由 Storage_Task 在成功写入后调用）
 * @details [注意] 同上，跨任务写操作。若引入多线程并发（目前只有 Storage_Task 写），
 *                 需改用 __atomic_fetch_add 或 taskENTER_CRITICAL 保护。
 */
void App_Capture_IncrementCount(void);

#ifdef __cplusplus
}                                       /* extern "C" 结束 */
#endif

#endif /* __APP_CAPTURE_H */
