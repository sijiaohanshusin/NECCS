/**
 * @file    app_main_task.h
 * @brief   FreeRTOS 任务调度与数据结构定义
 * @details 定义音频处理流水线和 UI 显示任务的接口
 *
 * 任务架构：
 * - Audio_Pipeline_Task (优先级 4): 音频采集 → 预处理 → FFT → SRP-PHAT
 * - UI_Display_Task (优先级 4): 接收定位结果 → 渲染热力图 → 刷新显示
 *
 * 数据流：
 * - SAI DMA ISR → xAudioFrameQueue → Audio_Pipeline_Task
 * - Audio_Pipeline_Task → xPositionQueue → UI_Display_Task
 */

#ifndef APP_MAIN_TASK_H
#define APP_MAIN_TASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_perf.h"
#include "app_runtime.h"
#include "app_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FreeRTOS 句柄 (FreeRTOS Handles)
 * ============================================================================ */

/** @brief 音频处理流水线任务句柄 (优先级 4) */
extern TaskHandle_t xAudioPipelineTaskHandle;

/** @brief UI 显示任务句柄 (优先级 4) */
extern TaskHandle_t xUITaskHandle;

/** @brief 音频帧事件队列句柄 (长度 1, 覆盖模式) */
extern QueueHandle_t xAudioFrameQueue;

/** @brief 声源位置队列句柄 (长度 1, 覆盖模式) */
extern QueueHandle_t xPositionQueue;

/* ============================================================================
 * 运行时诊断计数器 (Runtime Diagnostic Counters)
 * ============================================================================ */

/** @brief 音频帧事件序号跳变累计值 (用于估算 ISR 丢帧) */
extern volatile uint32_t g_audio_both_flags_count;

/** @brief 音频任务未收到任何标志的次数 (异常情况) */
extern volatile uint32_t g_audio_no_flag_count;
/** @brief ISR 侧音频帧序号 (用于判断 SAI DMA 活跃性) */
extern volatile uint32_t g_audio_frame_seq_isr;

/** @brief UI 任务渲染帧计数 (正常运行计数) */
extern volatile uint32_t g_ui_render_count;

/** @brief UI 任务成功接收队列数据的次数 */
extern volatile uint32_t g_ui_queue_rx_count;

/** @brief UI 任务队列接收超时的次数 */
extern volatile uint32_t g_ui_queue_timeout_count;

extern volatile uint32_t g_ui_cli_rx_ok_count;
extern volatile uint32_t g_ui_cli_rx_err_count;
extern volatile uint8_t g_ui_cli_rx_alive;

/* ============================================================================
 * 任务入口函数 (Task Entry Functions)
 * ============================================================================ */

/**
 * @brief   音频处理流水线任务
 * @details 处理流程：
 *          1. 等待 DMA 中断事件 (xQueueReceive)
 *          2. 解交织 + 类型转换 (Deinterleave_Using_Matrix)
 *          3. FFT 频域变换 (AI_FFT_Process)
 *          4. SRP-PHAT 声源定位 (AI_SRP_PHAT_Process)
 *          5. 发送结果到 UI 任务 (xQueueOverwrite)
 *
 * 任务参数：
 * - 优先级：4 (与 UI 任务同级)
 * - 堆栈：2304 字节
 * - 周期：5.33ms (48kHz, 256 点)
 *
 * @param   pvParameters  FreeRTOS 任务参数 (未使用)
 */
void Audio_Pipeline_Task(void *pvParameters);

/**
 * @brief   UI 显示任务
 * @details 处理流程：
 *          1. 等待声源位置数据 (xQueueReceive, 33ms 超时)
 *          2. 渲染热力图和十字光标 (App_Display_Render)
 *          3. 刷新 LCD 显示
 *
 * 任务参数：
 * - 优先级：4 (与音频任务同级)
 * - 堆栈：2048 字节
 * - 周期：33ms (30 FPS)
 *
 * @param   pvParameters  FreeRTOS 任务参数 (未使用)
 */
void UI_Display_Task(void *pvParameters);

/* ============================================================================
 * 初始化函数 (Initialization Functions)
 * ============================================================================ */

/**
 * @brief   应用任务和队列初始化
 * @details 执行以下初始化：
 *          1. 创建音频帧事件队列 (长度 1)
 *          2. 创建声源位置队列 (长度 1)
 *          3. 创建音频处理任务 (优先级 4)
 *          4. 创建 UI 显示任务 (优先级 4)
 *
 * @note    在 FreeRTOS 启动前调用 (freertos.c 中)
 */
void App_Task_Init(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_TASK_H */
