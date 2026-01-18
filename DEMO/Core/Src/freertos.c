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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartTask(void *argument);
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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
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
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET&& MyTask2_Handler != NULL)
    {
      /* code */
      vTaskDelete(MyTask2_Handler);
      MyTask2_Handler = NULL;
    }
    
  }
}
/* USER CODE END Application */


