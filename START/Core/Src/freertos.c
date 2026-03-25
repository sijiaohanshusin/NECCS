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
#include "semphr.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "pcmd3180.h"
#include "soft_i2c.h"
#include "app_camera.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "app_boot_diag.h"
#include "app_touch.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* FreeRTOS 相关编译期开关预留区 */
#define APP_DEFAULT_TASK_PRIO        1u
#define APP_DEFAULT_TASK_STACK_WORDS 256u
#define APP_INIT_TASK_PRIO           6u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/** @brief SAI 句柄 (外部定义) */
extern SAI_HandleTypeDef hsai_BlockA1;
extern SemaphoreHandle_t txMutex;
static volatile uint32_t s_boot_diag_stage = APP_BOOT_DIAG_STAGE_IDLE;
static volatile uint32_t s_boot_diag_stack_hwm_words = 0u;
static volatile uint8_t s_boot_diag_completed = 0u;
/* USER CODE END Variables */

/* Definitions for defaultTask */
TaskHandle_t defaultTaskHandle = NULL;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void PCMD3180InitTask(void *argument);
static void s_boot_diag_mark(App_BootDiag_Stage_t stage, uint8_t completed);
void App_BootDiag_GetStatus(App_BootDiag_Status_t *status);
const char *App_BootDiag_StageName(uint32_t stage);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

static void s_boot_diag_mark(App_BootDiag_Stage_t stage, uint8_t completed)
{
  UBaseType_t stack_hwm = 0u;

  s_boot_diag_stage = (uint32_t)stage;
  s_boot_diag_completed = completed;

  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    stack_hwm = uxTaskGetStackHighWaterMark(NULL);
    s_boot_diag_stack_hwm_words = (uint32_t)stack_hwm;
  }
}

void App_BootDiag_GetStatus(App_BootDiag_Status_t *status)
{
  uint32_t primask;

  if (status == NULL)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  status->stage = s_boot_diag_stage;
  status->stack_high_water_words = s_boot_diag_stack_hwm_words;
  status->completed = s_boot_diag_completed;
  if (primask == 0u)
  {
    __enable_irq();
  }
}

const char *App_BootDiag_StageName(uint32_t stage)
{
  switch ((App_BootDiag_Stage_t)stage)
  {
    case APP_BOOT_DIAG_STAGE_SOFT_I2C:     return "soft_i2c";
    case APP_BOOT_DIAG_STAGE_APP_STREAM:   return "app_stream";
    case APP_BOOT_DIAG_STAGE_APP_TASK:     return "app_task";
    case APP_BOOT_DIAG_STAGE_SAI_DMA:      return "sai_dma";
    case APP_BOOT_DIAG_STAGE_CLOCK_WAIT:   return "clock_wait";
    case APP_BOOT_DIAG_STAGE_PCMD0:        return "pcmd0";
    case APP_BOOT_DIAG_STAGE_PCMD1:        return "pcmd1";
    case APP_BOOT_DIAG_STAGE_CAMERA_INIT:  return "camera_init";
    case APP_BOOT_DIAG_STAGE_CAMERA_START: return "camera_start";
    case APP_BOOT_DIAG_STAGE_DONE:         return "done";
    case APP_BOOT_DIAG_STAGE_IDLE:
    default:
      return "idle";
  }
}

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
  BaseType_t task_ok;
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  if (txMutex == NULL)
  {
    txMutex = xSemaphoreCreateMutex();
    configASSERT(txMutex != NULL);
  }
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

  /* Create the low-priority background task using raw FreeRTOS priority units.
   * Mixing CMSIS-RTOS priorities with xTaskCreate() priorities can starve the
   * UI task if a bring-up task stalls. */
  task_ok = xTaskCreate(StartDefaultTask,
                        "defaultTask",
                        APP_DEFAULT_TASK_STACK_WORDS,
                        NULL,
                        APP_DEFAULT_TASK_PRIO,
                        &defaultTaskHandle);
  configASSERT(task_ok == pdPASS);

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
   * 优先级：原生 FreeRTOS 6（高于 Audio/UI，优先完成一次性上电初始化）
   * 堆栈：1024 字（覆盖 HAL 初始化与摄像头启动路径）
   * 生命周期：一次性任务，完成后自删除
   */
  task_ok = xTaskCreate(PCMD3180InitTask, "PCMD3180Init", 1024, NULL, APP_INIT_TASK_PRIO, NULL);
  configASSERT(task_ok == pdPASS);
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
 * @note    优先级：原生 FreeRTOS 1（始终低于 Audio/UI/Init）
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
 * 3. 启动 SAI DMA（连续输出 BCLK/FSYNC）
 * 4. 延迟 1 秒等待时钟稳定
 * 5. 配置两颗 PCMD3180
 * 6. 初始化并启动摄像头
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
 * @note    优先级：原生 FreeRTOS 3
 * @note    堆栈：512 字
 * @note    生命周期：一次性任务，完成后自删除
 */
void PCMD3180InitTask(void *argument)
{
  (void)argument;

  /* 步骤 1: 初始化软 I2C（创建并启用总线互斥量） */
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_SOFT_I2C, 0u);
  Soft_I2C_Init();
  App_Touch_Init();

  /* 步骤 2: 初始化音频算法运行态（RFFT/Hanning/SRP-PHAT） */
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_APP_STREAM, 0u);
  App_Stream_Init();

  /* 步骤 3: 创建音频与 UI 工作任务（稍后在本任务让出 CPU 后开始运行） */
  App_Task_Init();
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_APP_TASK, 0u);

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
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_SAI_DMA, 0u);
  if (HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)Mic_Rx_Buffer, DMA_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }

  /* 步骤 5: 等待时钟稳定，避免 PCMD3180 在边沿抖动期配置 */
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_CLOCK_WAIT, 0u);
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
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_PCMD0, 0u);
  PCMD3180_Init_Device(PCMD3180_ADDR_0, 0);   /* 芯片 A, slot 0 起 */
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_PCMD1, 0u);
  PCMD3180_Init_Device(PCMD3180_ADDR_1, 8);   /* 芯片 B, slot 8 起 */

  /* 步骤 7: 初始化并启动摄像头，然后自删除初始化任务 */
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_CAMERA_INIT, 0u);
  App_Camera_Init();
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_CAMERA_START, 0u);
  App_Camera_Start();
  s_boot_diag_mark(APP_BOOT_DIAG_STAGE_DONE, 1u);
  I2C_Scan();
  vTaskDelete(NULL);
}
/* USER CODE END Application */
