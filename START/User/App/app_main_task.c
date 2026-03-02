#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_display.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "LCD/lcd.h"

#include <stdio.h>

extern int16_t found_val;

TaskHandle_t xAudioPipelineTaskHandle = NULL;
TaskHandle_t xUITaskHandle = NULL;
QueueHandle_t xAudioFrameQueue = NULL;
QueueHandle_t xPositionQueue = NULL;

volatile uint32_t g_audio_both_flags_count = 0u;
volatile uint32_t g_audio_no_flag_count = 0u;
volatile uint32_t g_ui_render_count = 0u;
volatile uint32_t g_ui_queue_rx_count = 0u;
volatile uint32_t g_ui_queue_timeout_count = 0u;

/* ==================== 调试/测试开关 ==================== */
/* #define DEBUG_ENABLE */
#define DEBUG_THROTTLE_FRAMES   20u
#define DEBUG_MODE              3      /* 0=RMS, 1=FFT, 3=SRP */
#define DEBUG_SPECTRUM_CHANNEL  0u

/* ==================== UI 刷新参数 ==================== */
#define UI_RETRY_INIT_MS        1000u
#define UI_RENDER_PERIOD_MS     33u    /* 约 30Hz */
#define UI_DEBUG_LOG            0u

/* ==================== 任务优先级 ==================== */
#define APP_AUDIO_TASK_PRIO     4u
#define APP_UI_TASK_PRIO        4u

void App_Task_Init(void)
{
    BaseType_t task_ok;

    /* 队列长度为 1: 保留最新帧/最新定位结果，降低端到端延迟。 */
    xAudioFrameQueue = xQueueCreate(1, sizeof(Audio_FrameEvent_t));
    xPositionQueue = xQueueCreate(1, sizeof(Sound_Pos_t));
    configASSERT(xAudioFrameQueue != NULL);
    configASSERT(xPositionQueue != NULL);

    task_ok = xTaskCreate(Audio_Pipeline_Task, "Audio_Pipe", 2304, NULL, APP_AUDIO_TASK_PRIO, &xAudioPipelineTaskHandle);
    configASSERT(task_ok == pdPASS);

    task_ok = xTaskCreate(UI_Display_Task, "UI_Disp", 2048, NULL, APP_UI_TASK_PRIO, &xUITaskHandle);
    configASSERT(task_ok == pdPASS);
}

void Audio_Pipeline_Task(void *pvParameters)
{
    (void)pvParameters;

    static uint32_t s_frame_cnt = 0u;
    uint32_t s_last_seq = 0u;
    Sound_Pos_t current_pos;

    Audio_FrameEvent_t event;
    q15_t *p_current_dma_src;

    /*
     * 复用 Mic_Freq_Buffer 作为临时 q15 平面缓冲，避免额外分配大块内存。
     * 该缓冲只在当前任务内使用，不跨任务共享。
     */
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    for (;;)
    {
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        /* 统计被覆盖丢失的帧数（序号断层）。 */
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

        /* 音频/UI 同优先级时主动让出一次 CPU，降低 UI 被长期饿死的概率。 */
        taskYIELD();
    }
}

void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;

    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};
    float32_t coarse_snapshot[COARSE_TOTAL];
    uint32_t ui_frame_seq = 0u;

    TickType_t next_render_wake;
    TickType_t last_init_try = 0u;
    uint32_t last_dma2d_timeout = 0u;

    if (App_Display_IsReady() == 0u)
    {
        App_Display_Init();
    }
    last_init_try = xTaskGetTickCount();
    next_render_wake = last_init_try;

    for (;;)
    {
        if (App_Display_IsReady() == 0u)
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,
                       (unsigned long)g_display_init_error,
                       (unsigned long)g_lcd_init_stage,
                       (unsigned long)g_ltdc_init_stage);
#endif
                App_Display_Init();
                last_init_try = now;
            }
            taskYIELD();
            continue;
        }

        /* 非阻塞获取最新定位结果；若有多帧积压，仅保留最后一帧。 */
        if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
        {
            last_pos = draw_pos;
            g_ui_queue_rx_count++;

            while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;
                g_ui_queue_rx_count++;
            }
        }
        else
        {
            g_ui_queue_timeout_count++;
        }

        ui_frame_seq++;

        /*
         * SRP_Power 同时被音频任务写入。
         * 这里做一次短临界区快照，避免 UI 读到“半更新”数据。
         */
        taskENTER_CRITICAL();
        for (uint32_t i = 0u; i < COARSE_TOTAL; i++)
        {
            coarse_snapshot[i] = SRP_Power[i];
        }
        taskEXIT_CRITICAL();

        App_Display_Render(&last_pos, coarse_snapshot, ui_frame_seq);
        g_ui_render_count++;

        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)
        {
#if UI_DEBUG_LOG
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,
                   (unsigned int)g_ltdc_panel_id);
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;
        }

        vTaskDelayUntil(&next_render_wake, pdMS_TO_TICKS(UI_RENDER_PERIOD_MS));
    }
}
