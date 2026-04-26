/**
 * @file    app_storage_task.h
 * @brief   SD 卡异步存储任务 — BMP 截图与 WAV 录音 I/O 的公开接口
 * @details
 * 设计思路（异步 I/O 架构）：
 *   SD 卡写入操作（f_write、f_lseek）可能因 SD 总线、FatFS 缓存刷新等原因
 *   产生 1-50ms 的不确定延迟。若在音频任务或 UI 任务中直接调用这些操作，
 *   会影响实时性（导致丢帧或页面撕裂）。
 *
 *   因此将所有 SD I/O 封装到独立的低优先级 FreeRTOS 任务（Storage_Task，prio=2），
 *   其他任务通过命令队列（xQueueSend）发送请求，
 *   Storage_Task 异步处理，不阻塞调用方。
 *
 * 线程安全边界：
 *   - App_Storage_SendCmd()  ← 可从任意任务调用（线程安全，内部用队列传递）
 *   - App_Storage_GetState() ← 可从任意任务调用（volatile 读取，无竞态风险）
 *   - Storage_Task 内部所有 FatFS 调用 ← 仅在 Storage_Task 自身上下文中执行
 *
 * 重要约束（B1 Fix）：
 *   [注意] BMP 截图（CAPTURE_BMP）与 WAV 录音（REC_START/REC_STOP）共用
 *          同一个 FIL 句柄（s_fil）！若在录音期间触发截图，截图将返回 ERR_BUSY。
 *          调用方应先检查 App_Storage_GetState() 确认处于 IDLE 状态再截图。
 *
 * 命令流时序（BMP 截图）：
 *   调用方：App_Storage_SendCmd(STORAGE_CMD_CAPTURE_BMP, 0) → 返回 ERR_OK
 *   Storage_Task: open → write header → write rows (bottom-up) → close → IDLE
 *
 * 命令流时序（WAV 录音）：
 *   调用方：App_Storage_SendCmd(STORAGE_CMD_REC_START, mode)  → 开始录音
 *   Storage_Task: open → write init-header(data_size=0) → 开始 Feed
 *   录音中：每 20ms 定期 flush 缓冲区到 SD 卡（防止数据积压）
 *   调用方：App_Storage_SendCmd(STORAGE_CMD_REC_STOP, 0)      → 停止录音
 *   Storage_Task: flush残余 → f_lseek(0) → 回填 WAV 头（真实 data_size）→ close
 */
#ifndef __APP_STORAGE_TASK_H            /* 头文件防重复包含保护（开始）*/
#define __APP_STORAGE_TASK_H            /* 定义本文件标识宏 */

#include <stdint.h>                     /* uint8_t, uint32_t 等标准整数类型 */
#include "error_code.h"                 /* Err_t 统一错误码枚举 */

#ifdef __cplusplus                      /* C++ 兼容声明 */
extern "C" {                            /* 开始 C 链接区域 */
#endif

/**
 * @brief   存储命令类型枚举
 * @details 表示 Storage_Task 可以处理的异步 I/O 请求类型。
 *          通过 App_Storage_SendCmd() 携带在命令消息中发送到队列。
 */
typedef enum {
    STORAGE_CMD_CAPTURE_BMP  = 0u,  /**< 截取当前 LTDC 前缓冲区内容并保存为 BMP 文件 */
    STORAGE_CMD_REC_START    = 1u,  /**< 开始 WAV 多通道录音（param = App_RecorderMode_t）*/
    STORAGE_CMD_REC_STOP     = 2u   /**< 停止 WAV 录音，回填文件头，关闭文件 */
} App_StorageCmd_e;

/**
 * @brief   命令队列消息结构体
 * @details 放入命令队列（s_cmd_queue，深度 8）并由 Storage_Task 消费。
 *          消息以值传递，复制进队列，无需担心调用方变量生命周期。
 */
typedef struct {
    App_StorageCmd_e cmd;       /**< 命令类型（选择执行哪种 I/O 操作）*/
    uint32_t         param;     /**< 命令附加参数（REC_START 时传录音模式枚举值）*/
    /* [改进] 可以增加 uint32_t timestamp 记录命令入队时刻，供调试用 */
} App_StorageMsg_t;

/**
 * @brief   存储任务状态枚举
 * @details 反映 Storage_Task 当前正在执行的操作，由 App_Storage_GetState() 返回。
 *          [注意] 状态通过 volatile 变量更新（Storage_Task 写，其他任务读），
 *                 仅保证可见性，不保证原子性；但对 enum 大小（4字节对齐）的 ARM 读是原子的。
 */
typedef enum {
    STORAGE_STATE_IDLE      = 0u,   /**< 空闲：无 I/O 操作正在进行，可接收新命令 */
    STORAGE_STATE_CAPTURING = 1u,   /**< 截图中：正在写入 BMP 文件（约 200-500ms）*/
    STORAGE_STATE_RECORDING = 2u,   /**< 录音中：WAV 文件正处于打开写入状态 */
    STORAGE_STATE_ERROR     = 3u    /**< 错误：最近一次 I/O 操作失败（SD 未挂载或写入错误）*/
} App_StorageState_e;

/**
 * @brief   初始化存储模块（创建命令队列 + FreeRTOS 任务）
 * @details 内部操作：
 *          1. 创建命令队列（深度 = STORAGE_CMD_QUEUE_DEPTH = 8）
 *          2. 创建 Storage_Task（优先级 2 = osPriorityBelowNormal，栈 = 2048 words）
 * @note    必须在 App_SD_Init() 之后调用（Storage_Task 需要 SD 已就绪）。
 *          在 FreeRTOS 调度器启动前调用，xTaskCreate 内部会延迟到 vTaskStartScheduler 后执行。
 *          [注意] 若 xQueueCreate 或 xTaskCreate 返回 NULL，s_state 置 ERROR，
 *                 但此时 printf 可能未就绪，错误将被静默吞掉。
 */
void App_Storage_Init(void);

/**
 * @brief   向存储任务发送 I/O 命令（非阻塞）
 * @details 将命令消息入队（0 超时：队列满则立即返回 ERR_BUSY）。
 *          [注意] BMP 截图请求在录音期间会因 ERR_BUSY 或底层 s_fil 句柄冲突而失败，
 *                 调用方应先检查 App_Storage_GetState() == STORAGE_STATE_IDLE。
 * @param   cmd    命令类型（CAPTURE_BMP / REC_START / REC_STOP）
 * @param   param  命令参数：
 *                   - CAPTURE_BMP：忽略（传 0）
 *                   - REC_START：App_RecorderMode_t 转 uint32_t（录音通道模式）
 *                   - REC_STOP：忽略（传 0）
 * @return  ERR_OK      命令已成功入队
 * @return  ERR_NOT_INIT 队列未初始化（Init 未调用或 xQueueCreate 失败）
 * @return  ERR_BUSY    命令队列已满（Storage_Task 处理速度跟不上命令速率，极少发生）
 */
Err_t App_Storage_SendCmd(App_StorageCmd_e cmd, uint32_t param);

/**
 * @brief   查询存储任务当前状态
 * @details 读取 volatile 状态变量，反映最近一次操作结果：
 *          IDLE = 可接受新命令，CAPTURING/RECORDING = 进行中，ERROR = 需要处理错误。
 * @return  当前 App_StorageState_e 状态值
 * @note    UI 可用此函数轮询截图完成状态（轮询间隔建议 ≥ 50ms）。
 *          [改进] 可考虑增加回调机制（如 xTaskNotify）取代 UI 轮询，降低 CPU 开销。
 */
App_StorageState_e App_Storage_GetState(void);

#ifdef __cplusplus                      /* 结束 C 链接区域 */
}
#endif

#endif /* __APP_STORAGE_TASK_H */       /* 头文件防重复包含保护（结束）*/
