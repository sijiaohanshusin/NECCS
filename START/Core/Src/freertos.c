/* USER CODE BEGIN Header */
/**
 * @file    freertos.c
 * @brief   FreeRTOS 应用入口与任务编排
 * @details 负责创建基础线程，并启动一次性音频初始化流程
 *
 * 全局启动时序：
 * 1. MX_FREERTOS_Init() 创建 defaultTask 与 PCMD3180InitTask
 * 2. PCMD3180InitTask() 完成外设与音频链路初始化后自删除
 * 3. StartDefaultTask() 常驻运行，作为保留空闲任务
 *
 * 线程安全约定：
 * - I2C 总线访问依赖 Soft_I2C_Init() 内部互斥量保护
 * - 本文件不直接共享可变全局状态，不引入额外锁
 * - 初始化任务仅执行一次，避免重复初始化同一硬件资源
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
#include "app_camera.h"
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
 * @details 创建系统基础线程，并派发一次性初始化任务
 *
 * 初始化顺序：
 * 1. 创建默认任务（低负载保留线程）
 * 2. 创建 PCMD3180 初始化任务（高优先级、一次性）
 *
 * @note    在 main() 中调用，位于内核调度启动前
 * @note    本函数仅负责“创建任务”，不执行耗时硬件初始化
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
   * PCMD3180InitTask 职责边界：
   * 1. 在 RTOS 上下文完成软 I2C 初始化（含互斥量）
   * 2. 初始化音频算法状态（FFT/SRP-PHAT）
   * 3. 创建音频与 UI 工作任务
   * 4. 启动 SAI DMA，等待时钟稳定后配置双芯片 PCMD3180
   *
   * 时序与并发约束：
   * - 软 I2C 依赖内核对象，必须在调度启动后执行
   * - 先创建工作任务，再启动 DMA，确保消费者已就绪
   * - PCMD3180 配置依赖 I2C 与稳定 BCLK/FSYNC，不可提前
   *
   * 优先级：osPriorityNormal + 2（高于默认任务）
   * 堆栈：512 字（用于一次性初始化路径）
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
 * @brief   默认保留任务
 * @details 低优先级常驻线程，当前仅周期延时
 *
 * 当前职责：
 * - 提供可观测的空闲循环占位
 * - 为后续低实时性功能预留挂载点
 *
 * @param   argument  任务参数 (未使用)
 *
 * @note    优先级：osPriorityNormal（低于音频处理任务）
 * @note    不访问共享可变资源，无额外线程安全要求
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
 * @details 一次性初始化任务，按既定时序完成音频链路上电
 *
 * 初始化流程：
 * 1. 初始化软 I2C（建立总线互斥保护）
 * 2. 初始化音频数据流状态（FFT、SRP-PHAT）
 * 3. 创建音频/UI 工作任务
 * 4. 启动 SAI DMA（连续输出 BCLK/FSYNC）
 * 5. 延迟 1 秒等待时钟稳定
 * 6. 配置两颗 PCMD3180
 * 7. 任务自删除
 *
 * 关键时序原因：
 * - SAI 为主模式，时钟由 MCU 侧输出
 * - PCMD3180 依赖外部 BCLK/FSYNC 进入稳定工作态
 * - 先启 DMA 再配芯片，可降低上电初期失配风险
 *
 * 设备映射：
 * - 芯片 A (地址 0x4C): TDM slot 0-7
 * - 芯片 B (地址 0x4D): TDM slot 8-15
 * - 每颗芯片 8 路 PDM 输入，输出 8 路 TDM
 *
 * 线程安全说明：
 * - I2C 访问通过 Soft_I2C_Init() 建立的互斥量串行化
 * - 本任务为单次执行，不与自身并发重入
 * - DMA 启动后的数据并发由后续音频任务处理
 *
 * @param   argument  任务参数 (未使用)
 *
 * @note    优先级：osPriorityNormal + 2
 * @note    堆栈：512 字
 * @note    生命周期：一次性任务，完成后自删除
 */
void PCMD3180InitTask(void *argument)
{
  (void)argument;

  /* 步骤 1: 初始化软 I2C（创建并启用总线互斥量） */
  Soft_I2C_Init();

  /* 步骤 2: 初始化音频算法运行态（RFFT/Hanning/SRP-PHAT） */
  App_Stream_Init();

  /* 步骤 3: 创建音频与 UI 工作任务（优先级高于 defaultTask） */
  App_Task_Init();

  /* 步骤 4: 启动 SAI DMA，开始稳定输出采样时钟 */
  /**
   * DMA 连续接收麦克风 TDM 数据，采用循环模式
   * Mic_Rx_Buffer 为双缓冲，配合半传输/全传输中断消费
   *
   * 参数：
   * - hsai_BlockA1: SAI 句柄
   * - Mic_Rx_Buffer: DMA 缓冲区（PING/PONG）
   * - DMA_BUFFER_SIZE: 缓冲大小（16ch x 256 x 2 = 8192）
   */
  if (HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)Mic_Rx_Buffer, DMA_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }

  /* 步骤 5: 等待时钟稳定，避免 PCMD3180 在边沿抖动期配置 */
  vTaskDelay(pdMS_TO_TICKS(1000));

  /* 步骤 6: 配置双 PCMD3180，建立 16 路麦克风到 TDM slot 映射 */
  /**
   * 双芯片配置：
   * - 芯片 A (地址 0x4C): TDM slot 0-7 (麦克风 0-7)
   * - 芯片 B (地址 0x4D): TDM slot 8-15 (麦克风 8-15)
   *
   * 关键参数：
   * - TDM 模式：16 slot, 16-bit
   * - 采样率：48kHz
   * - PDM 时钟：3.072MHz
   * - 增益：默认 0dB
   */
  PCMD3180_Init_Device(PCMD3180_ADDR_0, 0);   /* 芯片 A, slot 0 起 */
  PCMD3180_Init_Device(PCMD3180_ADDR_1, 8);   /* 芯片 B, slot 8 起 */

  /* 步骤 7: 自删除初始化任务，释放其栈与 TCB */
  App_Camera_Init();
  App_Camera_Start();
  vTaskDelete(NULL);
}
/* USER CODE END Application */
