#ifndef APP_MAIN_TASK_H
#define APP_MAIN_TASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SAI 循环 DMA 接收的半缓冲区标识。
 */
typedef enum {
    /** Mic_Rx_Buffer 前半区 */
    AUDIO_DMA_HALF_PING = 0u,
    /** Mic_Rx_Buffer 后半区 */
    AUDIO_DMA_HALF_PONG = 1u
} Audio_DmaHalf_t;

/**
 * @brief 由 DMA 中断产生、由音频流水任务消费的音频帧事件。
 * @note 队列长度为 1，ISR 使用覆盖写入，语义为“仅保留最新帧”。
 */
typedef struct {
    /** @ref Audio_DmaHalf_t 取值 */
    uint8_t half_id;
    /** 预留字段（用于未来扩展/对齐） */
    uint8_t reserved[3];
    /** 单调递增的帧序号，在 ISR 中自增 */
    uint32_t seq;
} Audio_FrameEvent_t;

/** 音频流水任务句柄（解交织 + FFT + SRP）。 */
extern TaskHandle_t xAudioPipelineTaskHandle;
/** UI 刷新任务句柄。 */
extern TaskHandle_t xUITaskHandle;

/** 单槽队列：承载最新的 @ref Audio_FrameEvent_t。 */
extern QueueHandle_t xAudioFrameQueue;
/** 单槽队列：承载供 UI 使用的最新定位结果。 */
extern QueueHandle_t xPositionQueue;

/**
 * @brief 诊断计数：因“仅保留最新帧”策略被跳过的旧帧数量。
 */
extern volatile uint32_t g_audio_both_flags_count;

/**
 * @brief 诊断计数：ISR 入队失败或非法 half_id 次数。
 */
extern volatile uint32_t g_audio_no_flag_count;

/**
 * @brief 声源定位结果。
 */
typedef struct {
    /** 水平角（单位：度） */
    float x_angle;
    /** 垂直角（单位：度） */
    float y_angle;
    /** 归一化能量/置信度，范围 [0, 1] */
    float energy;
} Sound_Pos_t;

/**
 * @brief 音频处理任务入口函数。
 * @param pvParameters 未使用。
 * @retval 无返回值。
 */
void Audio_Pipeline_Task(void *pvParameters);

/**
 * @brief UI 任务入口函数。
 * @param pvParameters 未使用。
 * @retval 无返回值。
 */
void UI_Display_Task(void *pvParameters);

/**
 * @brief 创建应用任务与通信队列。
 * @retval 无返回值。
 */
void App_Task_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_TASK_H */
