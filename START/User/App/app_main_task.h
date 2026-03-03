/**
 * @file    app_main_task.h
 * @brief   FreeRTOS 任务调度与数据结构定义
 * @details 定义音频处理流水线和 UI 显示任务的接口
 *
 * 任务架构：
 * - Audio_Pipeline_Task (优先级 3): 音频采集 → 预处理 → FFT → SRP-PHAT
 * - UI_Display_Task (优先级 2): 接收定位结果 → 渲染热力图 → 刷新显示
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 数据结构定义 (Data Structures)
 * ============================================================================ */

/**
 * @brief   DMA 半缓冲标识
 * @details 用于标识 PING/PONG 双缓冲的哪一半已完成
 *
 * PING/PONG 双缓冲机制：
 * - DMA 写入 PING 区时，CPU 处理 PONG 区
 * - DMA 写入 PONG 区时，CPU 处理 PING 区
 * - 避免数据覆盖，实现零拷贝流水线
 */
typedef enum {
    AUDIO_DMA_HALF_PING = 0u,  /**< Mic_Rx_Buffer 前半区 (0 ~ DMA_BUFFER_SIZE/2-1) */
    AUDIO_DMA_HALF_PONG = 1u   /**< Mic_Rx_Buffer 后半区 (DMA_BUFFER_SIZE/2 ~ DMA_BUFFER_SIZE-1) */
} Audio_DmaHalf_t;

/**
 * @brief   音频帧事件
 * @details DMA 中断生产，音频任务消费
 *
 * 队列机制：
 * - 队列长度：1 (仅保留最新帧)
 * - ISR 发送：xQueueOverwrite (覆盖旧数据)
 * - 任务接收：xQueueReceive (阻塞等待)
 *
 * 为什么队列长度为 1？
 * - 实时系统，只关心最新数据
 * - 避免队列积压导致延迟
 * - 丢帧策略：丢弃旧帧，处理新帧
 */
typedef struct {
    uint8_t half_id;        /**< DMA 半缓冲标识 @ref Audio_DmaHalf_t */
    uint8_t reserved[3];    /**< 对齐/预留字段 (保证 4 字节对齐) */
    uint32_t seq;           /**< ISR 侧单调递增序号 (用于丢帧检测) */
} Audio_FrameEvent_t;

/**
 * @brief   声源定位结果
 * @details 包含声源的方位角和能量信息
 *
 * 坐标系约定：
 * - x_angle: 水平角 (方位角)，正值向右，负值向左
 * - y_angle: 垂直角 (俯仰角)，正值向上，负值向下
 * - 原点：麦克风阵列中心正前方
 *
 * 能量归一化：
 * - 范围：[0, 1]
 * - 0: 无声源或噪声
 * - 1: 强声源 (理想情况)
 * - 实际值：通常 0.3-0.8
 */
typedef struct {
    float x_angle;  /**< 水平角 (度)，范围 [-60, 60] */
    float y_angle;  /**< 垂直角 (度)，范围 [-60, 60] */
    float energy;   /**< 归一化能量 [0, 1] */
} Sound_Pos_t;

/* ============================================================================
 * FreeRTOS 句柄 (FreeRTOS Handles)
 * ============================================================================ */

/** @brief 音频处理流水线任务句柄 (优先级 3) */
extern TaskHandle_t xAudioPipelineTaskHandle;

/** @brief UI 显示任务句柄 (优先级 2) */
extern TaskHandle_t xUITaskHandle;

/** @brief 音频帧事件队列句柄 (长度 1, 覆盖模式) */
extern QueueHandle_t xAudioFrameQueue;

/** @brief 声源位置队列句柄 (长度 1, 覆盖模式) */
extern QueueHandle_t xPositionQueue;

/* ============================================================================
 * 运行时诊断计数器 (Runtime Diagnostic Counters)
 * ============================================================================ */

/** @brief 音频任务同时收到 PING 和 PONG 标志的次数 (异常情况) */
extern volatile uint32_t g_audio_both_flags_count;

/** @brief 音频任务未收到任何标志的次数 (异常情况) */
extern volatile uint32_t g_audio_no_flag_count;

/** @brief UI 任务渲染帧计数 (正常运行计数) */
extern volatile uint32_t g_ui_render_count;

/** @brief UI 任务成功接收队列数据的次数 */
extern volatile uint32_t g_ui_queue_rx_count;

/** @brief UI 任务队列接收超时的次数 */
extern volatile uint32_t g_ui_queue_timeout_count;

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
 * - 优先级：3 (高于 UI 任务)
 * - 堆栈：2048 字节
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
 * - 优先级：2 (低于音频任务)
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
 *          3. 创建音频处理任务 (优先级 3)
 *          4. 创建 UI 显示任务 (优先级 2)
 *
 * @note    在 FreeRTOS 启动前调用 (freertos.c 中)
 */
void App_Task_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_TASK_H */
