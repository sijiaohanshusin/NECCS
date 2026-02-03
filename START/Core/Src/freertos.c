/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include <stdio.h>
#include "pcmd3180.h"
#include "soft_i2c.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// 定义缓冲区大小
// 16通道 * 256个采样点 (即每个通道256个点) = 4096个 int16_t
// 这大约是 5.3ms 的数据量，足够调试用

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
TaskHandle_t StartTask_Handler;
TaskHandle_t MyTask_Handler;
TaskHandle_t MyTask2_Handler;
TaskHandle_t MyTask3_Handler;
extern SAI_HandleTypeDef hsai_BlockA1;
// 缓冲区定义
// 注意：在 STM32H7 上，为了 DMA 访问安全，建议将此数组放在 D2 域的 SRAM 中
// 或者使用 __attribute__((section(".RxDecripSection"))) 等方式指定位置
// 如果不指定，默认在 AXI SRAM，需要处理 Cache (见后文)
int16_t Rx_Buff[AUDIO_BUFFER_SIZE];
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartTask(void *argument);
void PCMD3180InitTask(void *argument);
void MyTask(void *argument);
void MyTask2(void *argument);
void MyTask3(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
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
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  xTaskCreate(PCMD3180InitTask, "PCMD3180InitTask", 512, NULL, osPriorityNormal, NULL);
  xTaskCreate(StartTask, "StartTask", 256, NULL, osPriorityNormal, &StartTask_Handler);
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
  uint8_t data;
  /* Infinite loop */
  for(;;)
  {
    /* USER CODE BEGIN 2 */
    PCMD_Dump_Registers(PCMD3180_ADDR_1);
    /* USER CODE END 2 */
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void PCMD3180InitTask(void *argument)
{
  // PCMD3180 初始化代码
  // 初始化软件 I2C
  // 1. 启动 SAI DMA 接收
    // 此时 STM32 开始输出 BCLK 和 FSYNC，但数据线是空的（全0或噪音）
    // Circular Mode (DMA_CIRCULAR) 确保数据源源不断
    if (HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)Rx_Buff, AUDIO_BUFFER_SIZE) != HAL_OK)
    {
        Error_Handler(); // 启动失败，卡死检查
    }
    // 2. 延时一小会儿，让 BCLK 稳定
  vTaskDelay(pdMS_TO_TICKS(1000));
  PCMD3180_Init_Device(PCMD3180_ADDR_0, 0); // 初始化芯片 A，起始槽位 0
  PCMD3180_Init_Device(PCMD3180_ADDR_1, 8); // 初始化芯片 B，起始槽位 8
  vTaskDelete(NULL); // 删除自身任务
}

void StartTask(void *argument)
{
  // User-defined task code goes here
  taskENTER_CRITICAL();
  // Create MyTask
  xTaskCreate(MyTask, "MyTask", 256, NULL, osPriorityNormal, &MyTask_Handler);
  xTaskCreate(MyTask2, "MyTask2", 256, NULL, osPriorityNormal, &MyTask2_Handler);
  xTaskCreate(MyTask3, "MyTask3", 256, NULL, osPriorityNormal, &MyTask3_Handler);
  taskEXIT_CRITICAL();
  vTaskDelete(StartTask_Handler);
}
void MyTask(void *argument)
{
  /* Infinite loop */
  for(;;)
  {
    // User-defined task code goes here
    osDelay(1000);
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);
  }
}

void MyTask2(void *argument)
{
  /* Infinite loop */
  for(;;)
  {
    // User-defined task code goes here
    osDelay(1000);
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
  }
}

void MyTask3(void *argument)
{
  /* Infinite loop */
  for(;;)
  {
    // User-defined task code goes here
    osDelay(10);
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET)//key0 pressed
    {
      /* code */
      vTaskSuspend(MyTask_Handler);
    }else if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_SET)
    {
      /* code */
      vTaskResume(MyTask_Handler);
    }
    
    
  }
}
/* USER CODE END Application */
