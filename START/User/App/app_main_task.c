/**
 * @file    app_main_task.c
 * @brief   FreeRTOS task bootstrap
 * @details Creates queues, starts tasks, and wires the runtime modules together.
 */
#include "app_main_task.h"

#include "app_perf.h"
#include "app_runtime.h"
#include "app_task_cfg.h"

/* ============================================================================
 * FreeRTOS 句柄 (FreeRTOS Handles)
 * ============================================================================ */

/** @brief 音频处理任务句柄 */
TaskHandle_t xAudioPipelineTaskHandle = NULL;

/** @brief UI 显示任务句柄 */
TaskHandle_t xUITaskHandle = NULL;

/** @brief 音频帧事件队列句柄 */
QueueHandle_t xAudioFrameQueue = NULL;

/** @brief 声源位置队列句柄 */
QueueHandle_t xPositionQueue = NULL;

/* ============================================================================
 * 运行时诊断计数器 (Runtime Diagnostic Counters)
 * ============================================================================ */

/** @brief 音频帧事件序号跳变计数（用于估算丢帧） */
volatile uint32_t g_audio_both_flags_count = 0u;

/** @brief 音频任务收到非法 half_id 的次数 */
volatile uint32_t g_audio_no_flag_count = 0u;

/** @brief UI 渲染帧计数 */
volatile uint32_t g_ui_render_count = 0u;

/** @brief UI 成功接收位置队列数据次数 */
volatile uint32_t g_ui_queue_rx_count = 0u;

/** @brief UI 轮询位置队列未取到数据次数 */
volatile uint32_t g_ui_queue_timeout_count = 0u;

/** @brief CLI 通过 UART 成功接收字节的次数（ISR 侧累加，可用于判断串口活跃性） */
volatile uint32_t g_ui_cli_rx_ok_count = 0u;
/** @brief CLI UART 接收发生错误的次数（溢出/帧错误/重启失败等均计入此处） */
volatile uint32_t g_ui_cli_rx_err_count = 0u;
/** @brief CLI 活跃标志：2 秒内收到过有效字节则为 1，否则为 0 */
volatile uint8_t  g_ui_cli_rx_alive = 0u;

/**
 * @brief   应用层任务与队列初始化入口
 * @details 在 FreeRTOS 调度器启动前（freertos.c 中）调用。
 *          执行顺序：
 *          1. 设置 UI 渲染后端为 Legacy（App_Display）
 *          2. 初始化性能分析器（清零统计、尝试使能 DWT）
 *          3. 同步性能统计状态到运行时配置
 *          4. 从 App_Display 回读初始配置到运行时配置（保证初始状态一致）
 *          5. 创建长度为 1 的音频帧队列和位置队列
 *          6. 创建音频处理任务和 UI 显示任务
 *
 * @note    所有 configASSERT 在 NDEBUG 模式下被优化掉，
 *          Release 版本中队列/任务创建失败将导致未定义行为，
 *          建议在 RAM 紧张时检查堆配置（configTOTAL_HEAP_SIZE）。
 */
void App_Task_Init(void)
{
    BaseType_t task_ok;  /* xTaskCreate return value: pdPASS on success. */

    /* Step 1: select the current UI renderer backend. */
    App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LEGACY);

    /* Step 2: initialize the performance profiler. */
    App_Perf_Init();

    /* Step 3 / 4: sync runtime config with perf state and display defaults. */
    App_RuntimeConfig_Init();

    /* Step 5: create depth-1 overwrite queues to keep only the latest frame. */
    xAudioFrameQueue = xQueueCreate(1, sizeof(Audio_FrameEvent_t)); /* ISR -> audio task */
    xPositionQueue   = xQueueCreate(1, sizeof(Sound_Pos_t));        /* audio task -> UI task */
    configASSERT(xAudioFrameQueue != NULL);  /* Allocation failure usually means heap is too small. */
    configASSERT(xPositionQueue   != NULL);

    /* Step 6a: create the audio pipeline task. */
    task_ok = xTaskCreate(Audio_Pipeline_Task,   /* Task entry */
                          "Audio_Pipe",         /* Task name */
                          2304,                 /* Stack size in words */
                          NULL,                 /* Unused task parameter */
                          APP_AUDIO_TASK_PRIO,  /* Priority */
                          &xAudioPipelineTaskHandle); /* Output handle */
    configASSERT(task_ok == pdPASS);

    /* Step 6b: create the UI display task. */
    task_ok = xTaskCreate(UI_Display_Task,   /* Task entry */
                          "UI_Disp",        /* Task name */
                          2048,             /* Stack size in words */
                          NULL,             /* Unused task parameter */
                          APP_UI_TASK_PRIO, /* Priority */
                          &xUITaskHandle);  /* Output handle */
    configASSERT(task_ok == pdPASS);
    /* Initialization is complete; tasks start after vTaskStartScheduler(). */
}
