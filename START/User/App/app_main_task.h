/**
 * @file app_main_task.h
 * @brief FreeRTOS 任务调度与 IPC 接口声明（调试简洁版）
 */

#ifndef APP_MAIN_TASK_H
#define APP_MAIN_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_FLAG_PING  (1 << 0)  // Bit 0 代表前半段完成
#define AUDIO_FLAG_PONG  (1 << 1)  // Bit 1 代表后半段完成

// ==========================================
// 任务句柄声明
// ==========================================
extern TaskHandle_t xAudioPreTaskHandle;
extern TaskHandle_t xAlgoTaskHandle;
extern TaskHandle_t xUITaskHandle;

// ==========================================
// IPC 通信机制声明
// ==========================================
extern SemaphoreHandle_t xAudioDataReadySem;  // 音频数据就绪信号量
extern QueueHandle_t xPositionQueue;          // 声源坐标队列

// ==========================================
// 数据结构定义
// ==========================================
typedef struct {
    float x_angle;  // 方位角 (度)
    float y_angle;  // 俯仰角 (度)
    float energy;   // 声源能量 [0.0, 1.0]
} Sound_Pos_t;

// ==========================================
// 任务函数声明
// ==========================================
void Audio_Preprocess_Task(void *pvParameters);
void AI_Algorithm_Task(void *pvParameters);
void UI_Display_Task(void *pvParameters);

// ==========================================
// 初始化函数
// ==========================================
void App_Task_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_TASK_H */