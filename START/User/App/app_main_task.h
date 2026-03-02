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
 * @brief DMA 半缓冲标识
 */
typedef enum {
    AUDIO_DMA_HALF_PING = 0u,  /* Mic_Rx_Buffer 前半区 */
    AUDIO_DMA_HALF_PONG = 1u   /* Mic_Rx_Buffer 后半区 */
} Audio_DmaHalf_t;

/**
 * @brief 音频帧事件（DMA 中断生产，音频任务消费）
 * @note  队列长度固定为 1；ISR 使用 overwrite，仅保留最新帧。
 */
typedef struct {
    uint8_t half_id;        /* @ref Audio_DmaHalf_t */
    uint8_t reserved[3];    /* 对齐/预留 */
    uint32_t seq;           /* ISR 侧单调递增序号 */
} Audio_FrameEvent_t;

/**
 * @brief 声源定位结果
 */
typedef struct {
    float x_angle;  /* 水平角（度） */
    float y_angle;  /* 俯仰角（度） */
    float energy;   /* 归一化能量 [0, 1] */
} Sound_Pos_t;

/* 任务句柄 */
extern TaskHandle_t xAudioPipelineTaskHandle;
extern TaskHandle_t xUITaskHandle;

/* 队列句柄 */
extern QueueHandle_t xAudioFrameQueue;
extern QueueHandle_t xPositionQueue;

/* 运行时诊断计数 */
extern volatile uint32_t g_audio_both_flags_count;
extern volatile uint32_t g_audio_no_flag_count;
extern volatile uint32_t g_ui_render_count;
extern volatile uint32_t g_ui_queue_rx_count;
extern volatile uint32_t g_ui_queue_timeout_count;

/* 任务入口 */
void Audio_Pipeline_Task(void *pvParameters);
void UI_Display_Task(void *pvParameters);

/* 应用任务/队列初始化 */
void App_Task_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_TASK_H */
