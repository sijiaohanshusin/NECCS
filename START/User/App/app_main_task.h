#ifndef APP_MAIN_TASK_H
#define APP_MAIN_TASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_FLAG_PING  (1u << 0)
#define AUDIO_FLAG_PONG  (1u << 1)

extern TaskHandle_t xAudioPreTaskHandle;
extern TaskHandle_t xAlgoTaskHandle;
extern TaskHandle_t xUITaskHandle;

extern SemaphoreHandle_t xAudioDataReadySem;
extern QueueHandle_t xPositionQueue;

extern volatile uint32_t g_audio_both_flags_count;
extern volatile uint32_t g_audio_no_flag_count;

typedef struct {
    float x_angle;
    float y_angle;
    float energy;
} Sound_Pos_t;

void Audio_Preprocess_Task(void *pvParameters);
void AI_Algorithm_Task(void *pvParameters);
void UI_Display_Task(void *pvParameters);

void App_Task_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_TASK_H */

