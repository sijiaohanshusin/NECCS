#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "sai.h"

extern int16_t found_val;
extern SAI_HandleTypeDef hsai_BlockA1;

TaskHandle_t xAudioPreTaskHandle = NULL;
TaskHandle_t xAlgoTaskHandle = NULL;
TaskHandle_t xUITaskHandle = NULL;
SemaphoreHandle_t xAudioDataReadySem = NULL;
QueueHandle_t xPositionQueue = NULL;

volatile uint32_t g_audio_both_flags_count = 0u;
volatile uint32_t g_audio_no_flag_count = 0u;

void App_Task_Init(void)
{
    xAudioDataReadySem = xSemaphoreCreateBinary();
    xPositionQueue = xQueueCreate(1, sizeof(Sound_Pos_t));

    xTaskCreate(Audio_Preprocess_Task, "Audio_Pre", 512, NULL, 4, &xAudioPreTaskHandle);
    xTaskCreate(AI_Algorithm_Task, "AI_Algo", 2048, NULL, 3, &xAlgoTaskHandle);
    xTaskCreate(UI_Display_Task, "UI_Disp", 1024, NULL, 2, &xUITaskHandle);
}

void Audio_Preprocess_Task(void *pvParameters)
{
    (void)pvParameters;

    uint32_t ulNotifiedValue;
    q15_t *p_current_dma_src;
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    for (;;)
    {
        xTaskNotifyWait(0x00, 0xFFFFFFFF, &ulNotifiedValue, portMAX_DELAY);

        if ((ulNotifiedValue & AUDIO_FLAG_PING) && (ulNotifiedValue & AUDIO_FLAG_PONG))
        {
            uint32_t dma_remain = __HAL_DMA_GET_COUNTER(hsai_BlockA1.hdmarx);
            g_audio_both_flags_count++;

            if (dma_remain > (DMA_BUFFER_SIZE / 2u))
            {
                p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
            }
            else
            {
                p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
            }
        }
        else if ((ulNotifiedValue & AUDIO_FLAG_PING) != 0u)
        {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if ((ulNotifiedValue & AUDIO_FLAG_PONG) != 0u)
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

        xSemaphoreGive(xAudioDataReadySem);
    }
}

#define DEBUG_ENABLE
#define DEBUG_THROTTLE_FRAMES   20u
#define DEBUG_MODE              3
#define DEBUG_SPECTRUM_CHANNEL  0u

void AI_Algorithm_Task(void *pvParameters)
{
    (void)pvParameters;

    static uint32_t s_frame_cnt = 0u;
    Sound_Pos_t current_pos;

    for (;;)
    {
        if (xSemaphoreTake(xAudioDataReadySem, portMAX_DELAY) == pdTRUE)
        {
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
}

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

