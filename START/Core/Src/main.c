/* USER CODE BEGIN Header */
/**
 * @file    main.c
 * @brief   STM32H7 声学相机主控入口
 * @details 负责硬件初始化、上电自检、RTOS 启动及关键中断回调注册。
 *
 * 系统架构：
 * - MCU: STM32H743IIT6 @ 480MHz
 * - 音频前端: 2×PCMD3180 (16路 PDM 麦克风)
 * - 显示: 800×480 RGB LCD (LTDC)
 * - 内存: 32MB SDRAM
 * - RTOS: FreeRTOS
 *
 * 启动流程：
 * 1. MPU 配置 (内存保护)
 * 2. HAL 初始化
 * 3. 系统时钟配置 (480MHz)
 * 4. SDRAM 初始化
 * 5. 外设初始化 (GPIO, DMA, USART, SAI)
 * 6. 显示模块初始化
 * 7. 上电自检 (SDRAM 测试)
 * 8. FreeRTOS 启动
 *
 * 主要任务：
 * - PCMD3180InitTask: 一次性初始化任务
 * - Audio_Pipeline_Task: 音频处理流水线
 * - UI_Display_Task: UI 显示任务
 * - StartDefaultTask: 默认空闲任务
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "dma.h"
#include "sai.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "mpu.h"
#include "sdram.h"
#include "app_main_task.h"
#include "app_display.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/** @brief 启用上电自检 (SDRAM 测试) */
#define ENABLE_BOOT_TESTS 1u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/** @brief 调试计数：用于观测音频处理帧推进情况 */
volatile int16_t found_val = 0;

/** @brief DMA 帧事件序号（ISR 侧递增，用于丢帧诊断） */
volatile uint32_t g_audio_frame_seq_isr = 0u;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief 使能 I-Cache/D-Cache，并执行同步屏障。
 * @note  仅在对应 Cache 尚未开启时执行开启操作。
 */
static void cpu_cache_enable(void)
{
  if ((SCB->CCR & SCB_CCR_IC_Msk) == 0u)
  {
    SCB_EnableICache();
  }
  if ((SCB->CCR & SCB_CCR_DC_Msk) == 0u)
  {
    SCB_EnableDCache();
  }
  __DSB();
  __ISB();
}

/**
 * @brief   SDRAM 采样读写自检
 * @details 以 4KB 步长对 32MB SDRAM 进行写入/回读一致性校验。
 *
 * 策略说明：
 * - 测试空间：32MB（8M 个 32-bit 字）
 * - 采样步长：1024 字（4KB）
 * - 写入模式：地址索引值
 * - 目标：在可接受启动时延内快速发现 SDRAM 异常
 *
 * @return  0: 测试通过
 *          非 0: 失败字节偏移
 *
 * @note    在 main() 中调用，位于 FreeRTOS 启动前
 */
static uint32_t sdram_test(void)
{
    /* SDRAM 基地址 */
    volatile uint32_t *pSdram = (volatile uint32_t *)BANK5_SDRAM_ADDR;
    uint32_t i;

    /* 32MB / 4B = 8M 个 32-bit 字 */
    const uint32_t test_size = 32u * 1024u * 1024u / 4u;

    /* 步骤 1：按采样步长写入测试模式 */
    for (i = 0u; i < test_size; i += 1024u)
    {
        pSdram[i] = i;
    }

    /* 步骤 2：按相同步长回读校验 */
    for (i = 0u; i < test_size; i += 1024u)
    {
        if (pSdram[i] != i)
        {
            /* 返回失败地址的字节偏移 */
            return i * 4u;
        }
    }

    return 0u;
}

/**
 * @brief   上电自检入口
 * @details 当前仅执行 SDRAM 采样读写测试，并通过串口输出结果。
 *
 * @note    可通过 ENABLE_BOOT_TESTS 控制开关
 * @note    在 main() 中调用，位于 FreeRTOS 启动前
 */
static void app_run_boot_tests(void)
{
#if (ENABLE_BOOT_TESTS != 0u)
    uint32_t sdram_err = sdram_test();

    if (sdram_err == 0u)
    {
        printf("SDRAM Test PASS (32MB @ 0xC0000000)\r\n");
    }
    else
    {
        printf("SDRAM Test FAIL at offset 0x%08lX\r\n", sdram_err);
    }
#endif
}

/* USER CODE END 0 */

/**
 * @brief   应用主入口
 * @details 完成底层硬件初始化后启动 FreeRTOS 调度器。
 *
 * 初始化顺序：
 * 1. MPU 配置 (默认配置，后续会覆盖)
 * 2. HAL 初始化 (中断优先级分组、SysTick)
 * 3. 自定义 MPU 配置 (覆盖默认配置)
 * 4. 系统时钟配置 (480MHz)
 * 5. SDRAM 初始化
 * 6. 外设初始化 (GPIO, DMA, USART, SAI)
 * 7. 显示模块初始化
 * 8. 上电自检
 * 9. FreeRTOS 初始化和启动
 *
 * MPU 配置说明：
 * - MPU_Config() 为 CubeMX 生成的基础配置
 * - App_MPU_Config() 施加工程自定义内存属性
 *
 * 外设顺序说明：
 * - 显示链路先初始化，便于尽早进入可视化状态
 * - SAI 随后初始化，不影响当前启动依赖关系
 *
 * @return  不返回 (FreeRTOS 接管控制权)
 */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* 步骤 1：应用 CubeMX 默认 MPU 配置 */
  MPU_Config();

  /* 步骤 2：HAL 基础初始化（外设复位、SysTick、NVIC 分组） */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* 步骤 3：应用工程自定义 MPU 布局并开启 Cache */
  App_MPU_Config();
  cpu_cache_enable();
  /* USER CODE END Init */

  /* 步骤 4：配置系统时钟（SYSCLK 480MHz） */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* 步骤 5：初始化外部 SDRAM 控制链路 */
  sdram_init();
  /* USER CODE END SysInit */

  /* 步骤 6：初始化基础外设 */
  MX_GPIO_Init();    /* GPIO 初始化 (LED, 按键等) */
  MX_DMA_Init();     /* DMA 初始化 (SAI DMA) */
  MX_USART1_UART_Init();  /* USART1 初始化 (调试串口) */

  /* 步骤 7：初始化显示链路（LTDC/DMA2D/LCD） */
  App_Display_Init();

  /* 初始化 SAI1 音频接口 */
  MX_SAI1_Init();

  /* USER CODE BEGIN 2 */
  /* 步骤 8：执行上电自检 */
  app_run_boot_tests();

  /* USER CODE END 2 */

  /* 步骤 9：初始化并启动 FreeRTOS */
  osKernelInitialize();

  /* 创建任务、队列、信号量等内核对象 */
  MX_FREERTOS_Init();

  /* 启动调度器，成功后不再返回 */
  osKernelStart();

  /* 正常情况下不会到达这里。
   * LVGL 控件请在 UI 任务中创建，例如 User/App/app_lvgl_ui.c。
   */
	
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief 系统时钟配置
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** 使能电源供电配置更新
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** 配置内部稳压器输出电压档位
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** 按 RCC_OscInitTypeDef 参数初始化振荡器与 PLL
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 1;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** 初始化 CPU、AHB 与 APB 总线时钟
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * @brief   在 ISR 中推送音频半帧事件
 * @details 使用覆盖写方式向单深度队列发布最新 DMA 半帧状态。
 *
 * 设计要点：
 * - 队列长度固定为 1，仅保留最新事件
 * - 事件携带 half_id 与递增序号 seq
 * - ISR 不阻塞；队列异常时仅记录计数
 *
 * @param   half_id  DMA 半缓冲标识（PING=0，PONG=1）
 *
 * @note    该函数运行于中断上下文，必须保持短路径执行
 */
static void Audio_FrameEvent_Push_FromISR(uint8_t half_id)
{
    /* 标记是否需要在退出 ISR 时触发任务切换 */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 组装事件载荷 */
    Audio_FrameEvent_t event;
    event.half_id = half_id;
    event.reserved[0] = 0u;         /* 预留字段，保持结构体对齐 */
    event.reserved[1] = 0u;
    event.reserved[2] = 0u;
    event.seq = ++g_audio_frame_seq_isr;

    /* 队列存在则覆盖写入最新事件 */
    if (xAudioFrameQueue != NULL)
    {
        if (xQueueOverwriteFromISR(xAudioFrameQueue, &event, &xHigherPriorityTaskWoken) != pdPASS)
        {
            /* 异常：发送失败，记录统计 */
            g_audio_no_flag_count++;
        }
    }
    else
    {
        /* 启动阶段队列未创建，记录统计 */
        g_audio_no_flag_count++;
    }

    /* 若唤醒了高优先级任务，申请在 ISR 退出时切换 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief   SAI DMA 半传输完成回调
 * @details 前半缓冲写满后上报 PING 事件，驱动音频任务处理对应半帧。
 *
 * @param   hsai  SAI 句柄（未使用）
 *
 * @note    运行于 DMA 中断上下文
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    (void)hsai;

    /* 上报前半缓冲就绪 */
    Audio_FrameEvent_Push_FromISR(AUDIO_DMA_HALF_PING);
}

/**
 * @brief   SAI DMA 全传输完成回调
 * @details 后半缓冲写满后上报 PONG 事件，驱动音频任务处理对应半帧。
 *
 * @param   hsai  SAI 句柄（未使用）
 *
 * @note    运行于 DMA 中断上下文
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    (void)hsai;

    /* 上报后半缓冲就绪 */
    Audio_FrameEvent_Push_FromISR(AUDIO_DMA_HALF_PONG);
}
/* USER CODE END 4 */

/**
 * @brief   MPU 基础配置（CubeMX 生成）
 * @details 先建立默认保护区，随后由 App_MPU_Config() 细化/覆盖。
 *
 * @note    该函数保留 CubeMX 生成结构，便于后续自动化维护
 */
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* 配置前先关闭 MPU */
  HAL_MPU_Disable();

  /* Region 0：4GB 地址空间默认禁止访问 */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* 重新启用 MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief   HAL 定时器周期回调
 * @details 当 TIM6 更新中断到来时递增 HAL 时基计数。
 *
 * @param   htim  定时器句柄
 *
 * @note    FreeRTOS 占用 SysTick，HAL 时基改由 TIM6 提供
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */

  /* 仅处理 TIM6 作为 HAL Tick 源 */
  if (htim->Instance == TIM6)
  {
    /* 递增 HAL Tick */
    HAL_IncTick();
  }

  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
 * @brief   通用错误处理
 * @details 关闭中断并停机，便于调试器接管现场。
 *
 * @note    可在 USER CODE 区扩展日志或错误指示
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* 可在此添加项目自定义错误处理，例如 LED/串口告警 */

  /* 锁定系统状态，避免错误继续扩散 */
  __disable_irq();

  /* 停机等待调试 */
  while (1)
  {
    /* 可在此输出错误码或做故障灯闪烁 */
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief   断言失败处理
 * @details 在启用 USE_FULL_ASSERT 时报告断言位置并停机。
 *
 * @param   file  触发断言的源文件名
 * @param   line  触发断言的行号
 *
 * @note    可在 USER CODE 区添加串口打印或告警指示
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* 可在此添加断言日志输出 */

  /* 当前未使用参数，避免编译警告 */
  (void)file;
  (void)line;

  /* 停机等待调试 */
  while (1)
  {
    /* 可在此输出断言错误信息 */
  }
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

