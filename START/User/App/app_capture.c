/**
 * @file    app_capture.c
 * @brief   屏幕截图触发与状态管理
 * @details 触发截图请求发送到 Storage_Task 队列,
 *          实际 BMP 写入由 Storage_Task 执行。
 */
#include "app_capture.h"
#include "app_storage_task.h"

/* ========== 模块状态 ========== */

static volatile App_CaptureState_t s_state = CAPTURE_IDLE;
static volatile uint32_t s_capture_count = 0u;

void App_Capture_Init(void)
{
    s_state = CAPTURE_IDLE;
    s_capture_count = 0u;
}

Err_t App_Capture_Trigger(void)
{
    Err_t ret;

    if (s_state == CAPTURE_BUSY)
    {
        return ERR_BUSY;
    }

    ret = App_Storage_SendCmd(STORAGE_CMD_CAPTURE_BMP, 0u);
    if (ret != ERR_OK)
    {
        return ERR_BUSY;
    }

    s_state = CAPTURE_BUSY;
    return ERR_OK;
}

App_CaptureState_t App_Capture_GetState(void)
{
    return s_state;
}

uint32_t App_Capture_GetCount(void)
{
    return s_capture_count;
}

void App_Capture_SetState(App_CaptureState_t state)
{
    s_state = state;
}

void App_Capture_IncrementCount(void)
{
    s_capture_count++;
}
