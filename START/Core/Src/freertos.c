/* USER CODE BEGIN Header */
/**
 * @file    freertos.c
 * @brief   FreeRTOS 应用初始化
 * @details 配置 FreeRTOS 任务、队列、信号量等
 *
 * 初始化流程：
 * 1. MX_FREERTOS_Init(): 创建默认任务和初始化任务
 * 2. PCMD3180InitTask(): 初始化音频硬件和应用任务
 * 3. StartDefaultTask(): 空闲任务 (保留)
 *
 * 任务优先级：
 * - PCMD3180InitTask: osPriorityNormal + 2 (一次性初始化任务)
 * - Audio_Pipeline_Task: 4 (音频处理)
 * - UI_Display_Task: 4 (UI 显示)
 * - StartDefaultTask: osPriorityNormal (空闲)
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
/* FreeRTOS 相关编译期开关预留区 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/** @brief SAI 句柄 (外部定义) */
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
 * @brief   FreeRTOS 初始化
 * @details 创建任务、队列、信号量等
 *
 * 初始化顺序：
 * 1. 创建默认任务 (空闲任务)
 * 2. 创建 PCMD3180 初始化任务 (一次性任务)
 *
 * @note    在 main() 中调用，FreeRTOS 启动前
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
  /**
   * 初始化任务职责：
   * 1. 在 RTOS 上下文初始化软 I2C (含互斥量)
   * 2. 初始化音频数据流状态 (FFT, SRP-PHAT)
   * 3. 创建音频/UI 任务
   * 4. 启动 SAI DMA 并配置两颗 PCMD3180
   *
   * 为什么需要初始化任务？
   * - 软 I2C 需要创建互斥量，必须在 RTOS 启动后
   * - SAI DMA 需要在任务创建后启动，避免丢失第一帧
   * - PCMD3180 配置需要 I2C 通信，必须在 RTOS 上下文
   *
   * 优先级：osPriorityNormal + 2 (高于默认任务)
   * 堆栈：512 字节 (足够 I2C 通信和任务创建)
   * 生命周期：一次性任务，完成后自删除
   */
  xTaskCreate(PCMD3180InitTask, "PCMD3180Init", 512, NULL, osPriorityNormal + 2, NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief   默认任务 (空闲任务)
 * @details 保留任务，用于系统空闲时执行
 *
 * 任务功能：
 * - 周期性延迟 1 秒
 * - 可用于 LED 闪烁、看门狗喂狗等
 *
 * @param   argument  任务参数 (未使用)
 *
 * @note    优先级：osPriorityNormal (低于音频/UI 任务)
 * @note    堆栈：1024 字节
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;

  /* 任务主循环 */
  for (;;)
  {
    /* 延迟 1 秒 */
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/**
 * @brief   PCMD3180 初始化任务
 * @details 一次性初始化任务，完成后自删除
 *
 * 初始化流程：
 * 1. 初始化软 I2C (创建互斥量)
 * 2. 初始化音频数据流 (FFT, SRP-PHAT)
 * 3. 创建音频/UI 任务
 * 4. 启动 SAI DMA (循环模式)
 * 5. 延迟 1 秒 (等待时钟稳定)
 * 6. 配置两颗 PCMD3180 芯片
 * 7. 自删除任务
 *
 * 为什么先启动 SAI DMA？
 * - SAI 配置为主模式，MCU 输出 BCLK/FSYNC
 * - PCMD3180 需要时钟才能正常工作
 * - 先启动 DMA，确保时钟输出稳定
 *
 * PCMD3180 配置：
 * - 芯片 A (地址 0x4C): TDM slot 0-7
 * - 芯片 B (地址 0x4D): TDM slot 8-15
 * - 每颗芯片 8 路 PDM 输入，输出 8 路 TDM
 *
 * @param   argument  任务参数 (未使用)
 *
 * @note    优先级：osPriorityNormal + 2
 * @note    堆栈：512 字节
 * @note    生命周期：一次性任务，完成后自删除
 */
void PCMD3180InitTask(void *argument)
{
  (void)argument;

  /* ========== 步骤 1: 初始化软 I2C ========== */
  /* 软 I2C 会创建 OS 互斥量，因此必须在调度器启动后执行 */
  /* 互斥量用于保护 I2C 总线，避免多任务同时访问 */
  Soft_I2C_Init();

  /* ========== 步骤 2: 初始化音频数据流 ========== */
  /* 在创建工作任务前，先初始化 FFT/SRP 相关运行状态 */
  /* 包括：RFFT 实例、Hanning 窗、SRP-PHAT 状态 */
  App_Stream_Init();

  /* ========== 步骤 3: 创建音频/UI 任务 ========== */
  /* 创建音频流水线任务与 UI 任务 */
  /* 任务优先级：4 (高于默认任务) */
  App_Task_Init();

  /* ========== 步骤 4: 启动 SAI DMA ========== */
  /**
   * 先启动 SAI DMA，让 MCU 主动输出 BCLK/FSYNC
   * DMA 采用循环模式，确保持续采集
   *
   * 参数：
   * - hsai_BlockA1: SAI 句柄
   * - Mic_Rx_Buffer: DMA 缓冲区 (PING/PONG 双缓冲)
   * - DMA_BUFFER_SIZE: 缓冲区大小 (16ch × 256 × 2 = 8192)
   *
   * DMA 模式：
   * - 循环模式 (Circular)
   * - 半传输中断 (Half Transfer Complete)
   * - 全传输中断 (Transfer Complete)
   */
  if (HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)Mic_Rx_Buffer, DMA_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }

  /* ========== 步骤 5: 延迟等待时钟稳定 ========== */
  /* 预留稳定时间，再配置编解码器寄存器 */
  /* 等待 BCLK/FSYNC 输出稳定，PCMD3180 锁相环稳定 */
  vTaskDelay(pdMS_TO_TICKS(1000));

  /* ========== 步骤 6: 配置 PCMD3180 芯片 ========== */
  /**
   * 双芯片配置：
   * - 芯片 A (地址 0x4C): TDM slot 0-7 (麦克风 0-7)
   * - 芯片 B (地址 0x4D): TDM slot 8-15 (麦克风 8-15)
   *
   * 配置内容：
   * - TDM 模式：16 slot, 16-bit
   * - 采样率：48kHz
   * - PDM 时钟：3.072MHz
   * - 增益：默认 0dB
   */
  PCMD3180_Init_Device(PCMD3180_ADDR_0, 0);   /* 芯片 A, slot 0 起 */
  PCMD3180_Init_Device(PCMD3180_ADDR_1, 8);   /* 芯片 B, slot 8 起 */

  /* ========== 步骤 7: 自删除任务 ========== */
  /* 一次性初始化任务，完成后自删除 */
  /* 释放任务堆栈和 TCB，节省内存 */
  vTaskDelete(NULL);
}
/* USER CODE END Application */
