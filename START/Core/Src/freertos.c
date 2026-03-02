/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS application initialization
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "pcmd3180.h"
#include "soft_i2c.h"
#include "app_data_stream.h"
#include "app_main_task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* FreeRTOS 相关编译期开关预留区。 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern SAI_HandleTypeDef hsai_BlockA1;
/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t)osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void PCMD3180InitTask(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /*
   * 初始化任务职责：
   * 1) 在 RTOS 上下文初始化软 I2C（含互斥量）。
   * 2) 初始化音频数据流状态。
   * 3) 创建音频/UI 任务。
   * 4) 启动 SAI DMA 并配置两颗 PCMD3180。
   */
  xTaskCreate(PCMD3180InitTask, "PCMD3180Init", 512, NULL, osPriorityNormal + 2, NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;

  for (;;)
  {
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void PCMD3180InitTask(void *argument)
{
  (void)argument;

  /* Soft I2C 会创建 OS 互斥量，因此必须在调度器启动后执行。 */
  Soft_I2C_Init();

  /* 在创建工作任务前，先初始化 FFT/SRP 相关运行状态。 */
  App_Stream_Init();

  /* 创建音频流水线任务与 UI 任务。 */
  App_Task_Init();

  /*
   * 先启动 SAI DMA，让 MCU 主动输出 BCLK/FSYNC。
   * DMA 采用循环模式，确保持续采集。
   */
  if (HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)Mic_Rx_Buffer, DMA_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }

  /* 预留稳定时间，再配置编解码器寄存器。 */
  vTaskDelay(pdMS_TO_TICKS(1000));

  /* 双芯片：A 从 slot0 起，B 从 slot8 起。 */
  PCMD3180_Init_Device(PCMD3180_ADDR_0, 0);
  PCMD3180_Init_Device(PCMD3180_ADDR_1, 8);

  /* 一次性初始化任务，完成后自删除。 */
  vTaskDelete(NULL);
}
/* USER CODE END Application */
