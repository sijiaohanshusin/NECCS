#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_data_stream.h"
#include "app_main_task.h"

extern int16_t found_val;

TaskHandle_t xAudioPipelineTaskHandle = NULL;
TaskHandle_t xUITaskHandle = NULL;
QueueHandle_t xAudioFrameQueue = NULL;
QueueHandle_t xPositionQueue = NULL;

volatile uint32_t g_audio_both_flags_count = 0u;
volatile uint32_t g_audio_no_flag_count = 0u;

#define DEBUG_ENABLE
#define DEBUG_THROTTLE_FRAMES   20u
/* 调试模式：0=RMS，1=FFT，3=SRP结果 */
#define DEBUG_MODE              3
#define DEBUG_SPECTRUM_CHANNEL  0u

/**
 * @brief 创建音频/UI 任务及任务间通信队列。
 * @retval 无返回值。
 */
void App_Task_Init(void)
{
    BaseType_t task_ok;

    /* 仅保留最新事件：生产快于消费时，旧帧会被覆盖。 */
    xAudioFrameQueue = xQueueCreate(1, sizeof(Audio_FrameEvent_t));
    /* UI 仅取最新定位结果。 */
    xPositionQueue = xQueueCreate(1, sizeof(Sound_Pos_t));
    configASSERT(xAudioFrameQueue != NULL);
    configASSERT(xPositionQueue != NULL);

    task_ok = xTaskCreate(Audio_Pipeline_Task, "Audio_Pipe", 2304, NULL, 4, &xAudioPipelineTaskHandle);
    configASSERT(task_ok == pdPASS);
    task_ok = xTaskCreate(UI_Display_Task, "UI_Disp", 1024, NULL, 2, &xUITaskHandle);
    configASSERT(task_ok == pdPASS);
}

/**
 * @brief 音频闭环流水任务。
 * @details 每帧处理流程：
 * 1) 等待最新 DMA 半帧事件；
 * 2) 将 int16 交织数据解交织为平面浮点缓冲；
 * 3) 执行 FFT 与 SRP-PHAT 定位；
 * 4) 将最新定位结果发布到 UI 队列。
 *
 * 本任务有意采用“仅保留最新帧”策略，以降低端到端时延。
 *
 * @param pvParameters 未使用。
 * @retval 无返回值。
 */
void Audio_Pipeline_Task(void *pvParameters)
{
    (void)pvParameters;

    static uint32_t s_frame_cnt = 0u;
    uint32_t s_last_seq = 0u;
    Sound_Pos_t current_pos;

    Audio_FrameEvent_t event;
    q15_t *p_current_dma_src;
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    for (;;)
    {
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        /* 统计在消费当前帧前，被覆盖掉的 ISR 事件数量。 */
        if ((s_last_seq != 0u) && (event.seq > (s_last_seq + 1u)))
        {
            g_audio_both_flags_count += (event.seq - s_last_seq - 1u);
        }
        s_last_seq = event.seq;

        if (event.half_id == AUDIO_DMA_HALF_PING)
        {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if (event.half_id == AUDIO_DMA_HALF_PONG)
        {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }
        else
        {
            g_audio_no_flag_count++;
            continue;
        }

        /* 可选调试心跳计数。 */
        found_val++;

        Deinterleave_Using_Matrix(p_current_dma_src,
                                  p_temp_planar,
                                  Mic_Process_Buffer,
                                  FRAME_LEN,
                                  MIC_CHANNELS);

        s_frame_cnt++;

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 0)
        if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
        {
            VOFA_Send_Channel_RMS();
        }
#endif
#endif

        AI_FFT_Process();

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 1)
        if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
        {
            VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
        }
#endif
#endif

        AI_SRP_PHAT_Process(&current_pos);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 3)
        if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
        {
            VOFA_Send_SRP_Result(&current_pos);
        }
#endif
#endif

        xQueueOverwrite(xPositionQueue, &current_pos);
    }
}

/**
 * @brief UI 刷新任务。
 * @details 约 30Hz 轮询最新定位结果；当前实现仅保留占位钩子。
 * @param pvParameters 未使用。
 * @retval 无返回值。
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;

    Sound_Pos_t draw_pos;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(33));

        if (xQueueReceive(xPositionQueue, &draw_pos, 0) == pdPASS)
        {
            (void)draw_pos;
        }
    }
}
