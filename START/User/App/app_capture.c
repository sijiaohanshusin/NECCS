/**
 * @file    app_capture.c
 * @brief   屏幕截图触发与状态管理
 * @details 本模块是截图功能的前端（状态机）：
 *          - Trigger() 将截图命令发送到 Storage_Task 的队列
 *          - 实际的 f_open / f_write / f_close（BMP 写入）在 Storage_Task 中执行
 *          - 通过 SetState/GetState 跟踪截图进度（IDLE→BUSY→DONE/ERROR）
 *
 * 调用方（UI 任务）的典型流程：
 *   1. App_Capture_Trigger()  ← 用户按下截图按键后调用
 *   2. 轮询 App_Capture_GetState() 直到 != CAPTURE_BUSY
 *   3. 若 CAPTURE_DONE：显示"截图成功"
 *      若 CAPTURE_ERROR：显示"截图失败"
 *
 * [注意] s_state 为 volatile 保证多任务可见性，
 *        但 CAPTURE_IDLE→CAPTURE_BUSY 赋值和 SendCmd 之间仍有极小窗口可能产生竞态，实际概率极低。
 */
#include "app_capture.h"               /* 本模块公开接口（App_Capture_* API）*/
#include "app_storage_task.h"           /* App_Storage_SendCmd（向 Storage_Task 队列发送命令）*/

/* ========== 模块状态 ========== */

static volatile App_CaptureState_t s_state = CAPTURE_IDLE;  /* 当前截图状态（volatile：Storage_Task 写，UI 任务读）*/
static volatile uint32_t s_capture_count = 0u;              /* 已成功完成的截图总计数 */
/* [改进] 可以增加 s_error_count 记录失败次数，方便调试 SD 卡写入问题 */

/**
 * @brief 初始化截图模块（清零状态）
 */
void App_Capture_Init(void)
{
    s_state = CAPTURE_IDLE;             /* 初始化为空闲，可以接受截图请求 */
    s_capture_count = 0u;               /* 计数清零（系统启动时重置）*/
}

/**
 * @brief 触发一次截图请求（非阻塞，发送命令到 Storage_Task 队列）
 */
Err_t App_Capture_Trigger(void)
{
    Err_t ret;                          /* App_Storage_SendCmd 的返回值 */

    if (s_state == CAPTURE_BUSY)        /* 防重入：上次截图还未完成，拒绝新请求 */
    {
        return ERR_BUSY;                /* 告知调用方：截图进行中，稍后再试 */
    }

    /* 向 Storage_Task 发送截图命令（非阻塞，入队失败返回 ERR_BUSY）*/
    ret = App_Storage_SendCmd(STORAGE_CMD_CAPTURE_BMP, 0u);  /* param=0 表示截图无额外参数 */
    if (ret != ERR_OK)                  /* 队列满（极少发生）或 Storage_Task 未初始化 */
    {
        return ERR_BUSY;                /* 统一上报 ERR_BUSY（UI 可提示"请稍后重试"）*/
    }

    s_state = CAPTURE_BUSY;            /* 标记截图进行中（Storage_Task 完成后会调用 SetState 更新）*/
    return ERR_OK;                      /* 命令已入队，Storage_Task 将异步执行 */
}

/**
 * @brief 查询截图状态（UI 侧轮询调用）
 */
App_CaptureState_t App_Capture_GetState(void)
{
    return s_state;                     /* volatile 读取，保证看到 Storage_Task 最新写入的状态值 */
}

/**
 * @brief 查询已成功完成的截图总数
 */
uint32_t App_Capture_GetCount(void)
{
    return s_capture_count;             /* 用于 UI 显示或 CLI 查询截图数量 */
}

/**
 * @brief 设置截图状态（由 Storage_Task 在处理过程中调用）
 * @details Storage_Task 在不同阶段调用此函数推进状态机：
 *          开始写文件时：CAPTURE_BUSY → 写入成功时：CAPTURE_DONE → 错误时：CAPTURE_ERROR
 */
void App_Capture_SetState(App_CaptureState_t state)
{
    s_state = state;                    /* 直接赋值，volatile 保证对其他任务立即可见 */
}

/**
 * @brief 递增截图成功计数（由 Storage_Task 在 BMP 文件成功关闭后调用）
 */
void App_Capture_IncrementCount(void)
{
    s_capture_count++;                  /* 计数递增（仅 Storage_Task 写入，无竞态风险）*/
    /* [注意] 若未来允许多任务触发截图，此处需要改为原子操作或互斥量保护 */
}
