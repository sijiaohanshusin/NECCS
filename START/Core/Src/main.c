/* USER CODE BEGIN Header */
/**
 * @file    main.c
 * @brief   主程序入口
 * @details STM32H7 声学相机主程序
 *
 * 系统架构：
 * - MCU: STM32H743IIT6 @ 480MHz
 * - 音频前端: 2×PCMD3180 (16路 PDM 麦克风)
 * - 显示: 800×480 RGB LCD (LTDC)
 * - 内存: 32MB SDRAM
 * - RTOS: FreeRTOS
 *
 * 初始化流程：
 * 1. MPU 配置 (内存保护)
 * 2. HAL 初始化
 * 3. 系统时钟配置 (480MHz)
 * 4. SDRAM 初始化
 * 5. 外设初始化 (GPIO, DMA, USART, SAI)
 * 6. 显示模块初始化
 * 7. 上电自检 (SDRAM 测试)
 * 8. FreeRTOS 启动
 *
 * 任务架构：
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
/** @brief 调试计数：音频任务每处理一帧加 1 */
volatile int16_t found_val = 0;

/** @brief ISR 帧事件序号：用于估算队列覆盖导致的丢帧 */
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
 * @brief   SDRAM 读写冒烟测试
 * @details 在 32MB SDRAM 上按采样步长写入并回读校验
 *
 * 测试策略：
 * - 测试范围：32MB (8M × 32-bit)
 * - 采样步长：1024 (每 4KB 测试一个点)
 * - 测试数据：地址索引 (i)
 * - 测试时间：约 100ms
 *
 * 为什么采样测试？
 * - 全量测试耗时过长 (约 10 秒)
 * - 采样测试可快速发现硬件问题
 * - 覆盖所有 SDRAM 行和列
 *
 * @return  0: 测试通过
 *          非 0: 失败字节偏移
 *
 * @note    在 main() 中调用，FreeRTOS 启动前
 */
static uint32_t sdram_test(void)
{
    /* SDRAM 基地址 (0xC0000000) */
    volatile uint32_t *pSdram = (volatile uint32_t *)BANK5_SDRAM_ADDR;
    uint32_t i;

    /* 测试大小：32MB / 4 字节 = 8M 个 32-bit 字 */
    const uint32_t test_size = 32u * 1024u * 1024u / 4u;

    /* ========== 步骤 1: 写入测试数据 ========== */
    /* 每隔 1024 个字写入一次 (采样步长 4KB) */
    for (i = 0u; i < test_size; i += 1024u)
    {
        pSdram[i] = i;  /* 写入地址索引 */
    }

    /* ========== 步骤 2: 回读校验 ========== */
    /* 按相同步长回读，检查数据是否正确 */
    for (i = 0u; i < test_size; i += 1024u)
    {
        if (pSdram[i] != i)
        {
            /* 数据不匹配，返回失败字节偏移 */
            return i * 4u;
        }
    }

    /* 测试通过 */
    return 0u;
}

/**
 * @brief   上电自检入口
 * @details 执行系统自检，检测硬件是否正常
 *
 * 自检项目：
 * - SDRAM 读写测试
 *
 * 自检结果：
 * - 通过：串口输出 "SDRAM Test PASS"
 * - 失败：串口输出 "SDRAM Test FAIL at offset 0x..."
 *
 * @note    在 main() 中调用，FreeRTOS 启动前
 * @note    可通过 ENABLE_BOOT_TESTS 宏禁用
 */
static void app_run_boot_tests(void)
{
#if (ENABLE_BOOT_TESTS != 0u)
    /* 执行 SDRAM 测试 */
    uint32_t sdram_err = sdram_test();

    /* 输出测试结果 */
    if (sdram_err == 0u)
    {
        /* 测试通过 */
        printf("SDRAM Test PASS (32MB @ 0xC0000000)\r\n");
    }
    else
    {
        /* 测试失败，输出失败偏移 */
        printf("SDRAM Test FAIL at offset 0x%08lX\r\n", sdram_err);
    }
#endif
}

/* USER CODE END 0 */

/**
 * @brief   应用程序入口点
 * @details 系统初始化和 FreeRTOS 启动
 *
 * 初始化流程：
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
 * 为什么两次 MPU 配置？
 * - MPU_Config(): HAL 默认配置 (CubeMX 生成)
 * - App_MPU_Config(): 自定义配置 (覆盖默认)
 * - 保留 CubeMX 生成的代码，方便后续维护
 *
 * 为什么先初始化显示？
 * - 显示初始化耗时较长 (约 100ms)
 * - 先初始化可提前显示启动画面
 * - SAI 初始化较快，后初始化不影响
 *
 * @return  不返回 (FreeRTOS 接管控制权)
 */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* ========== 步骤 1: MPU 配置 ========== */
  /* CubeMX 生成的默认 MPU 配置 */
  MPU_Config();

  /* ========== 步骤 2: MCU 配置 ========== */

  /* 复位所有外设，初始化 Flash 接口和 SysTick */
  /* 配置中断优先级分组 (4 位抢占优先级, 0 位子优先级) */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* ========== 步骤 3: 自定义 MPU 配置 ========== */
  /* 覆盖默认 MPU 配置，使用工程自定义内存布局 */
  /* 配置 3 个区域：SRAM1 (Non-Cacheable), SDRAM (Cacheable), 帧缓冲 (Non-Cacheable) */
  App_MPU_Config();
  /* USER CODE END Init */

  /* ========== 步骤 4: 系统时钟配置 ========== */
  /* 配置系统时钟为 480MHz */
  /* PLL1: 25MHz HSE → 480MHz SYSCLK */
  /* HCLK: 240MHz, APB1/2/3/4: 120MHz */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* ========== 步骤 5: SDRAM 初始化 ========== */
  /* 初始化 FMC 和 SDRAM 控制器 */
  /* SDRAM: W9825G6KH, 32MB, 120MHz */
  sdram_init();
  /* USER CODE END SysInit */

  /* ========== 步骤 6: 外设初始化 ========== */
  /* 初始化所有配置的外设 */
  MX_GPIO_Init();    /* GPIO 初始化 (LED, 按键等) */
  MX_DMA_Init();     /* DMA 初始化 (SAI DMA) */
  MX_USART1_UART_Init();  /* USART1 初始化 (调试串口) */

  /* ========== 步骤 7: 显示模块初始化 ========== */
  /* 保持已验证通过的初始化顺序：先显示，再 SAI */
  /* 显示初始化包括：LTDC, DMA2D, LCD 驱动 */
  App_Display_Init();

  /* SAI1 初始化 (音频接口) */
  /* 配置：主模式, TDM16, 16-bit, 48kHz */
  MX_SAI1_Init();

  /* USER CODE BEGIN 2 */
  /* ========== 步骤 8: 上电自检 ========== */
  /* 执行 SDRAM 读写测试 */
  app_run_boot_tests();

  /* USER CODE END 2 */

  /* ========== 步骤 9: FreeRTOS 初始化和启动 ========== */
  /* 初始化 FreeRTOS 内核 */
  osKernelInitialize();

  /* 调用 FreeRTOS 对象初始化函数 (cmsis_os2.c) */
  /* 创建任务、队列、信号量等 */
  MX_FREERTOS_Init();

  /* 启动 FreeRTOS 调度器 */
  /* 此函数不返回，控制权交给 FreeRTOS */
  osKernelStart();

  /* 不应该执行到这里 (调度器已接管控制权) */

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
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
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

  /** Initializes the CPU, AHB and APB buses clocks
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
 * @brief   在 ISR 中推送 DMA 半帧事件
 * @details 从 DMA 中断向音频任务发送帧事件
 *
 * 队列机制：
 * - 队列长度：1 (仅保留最新帧)
 * - 发送方式：xQueueOverwriteFromISR (覆盖旧数据)
 * - 接收方式：xQueueReceive (阻塞等待)
 *
 * 事件内容：
 * - half_id: PING/PONG 标识
 * - seq: 单调递增序号 (用于丢帧检测)
 *
 * 为什么使用 Overwrite？
 * - 实时系统，只关心最新数据
 * - 避免队列满时阻塞 ISR
 * - 任务处理慢时自动丢弃旧帧
 *
 * @param   half_id  DMA 半缓冲标识 (PING=0, PONG=1)
 *
 * @note    在 DMA 中断中调用，执行时间要短
 * @note    如果队列未创建，计数器递增 (异常情况)
 */
static void Audio_FrameEvent_Push_FromISR(uint8_t half_id)
{
    /* FreeRTOS 上下文切换标志 */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 构造帧事件 */
    Audio_FrameEvent_t event;
    event.half_id = half_id;        /* PING/PONG 标识 */
    event.reserved[0] = 0u;         /* 预留字段 (对齐) */
    event.reserved[1] = 0u;
    event.reserved[2] = 0u;
    event.seq = ++g_audio_frame_seq_isr;  /* 单调递增序号 */

    /* 检查队列是否已创建 */
    if (xAudioFrameQueue != NULL)
    {
        /* 覆盖发送 (队列长度为 1，自动覆盖旧数据) */
        if (xQueueOverwriteFromISR(xAudioFrameQueue, &event, &xHigherPriorityTaskWoken) != pdPASS)
        {
            /* 发送失败 (异常情况) */
            g_audio_no_flag_count++;
        }
    }
    else
    {
        /* 队列未创建 (启动阶段) */
        g_audio_no_flag_count++;
    }

    /* 如果唤醒了更高优先级任务，请求上下文切换 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief   SAI DMA 半传输完成回调
 * @details DMA 完成前半区传输时调用
 *
 * PING/PONG 双缓冲机制：
 * - DMA 写入 PING 区 (前半区) 时，CPU 处理 PONG 区 (后半区)
 * - DMA 写入 PONG 区 (后半区) 时，CPU 处理 PING 区 (前半区)
 * - 避免数据覆盖，实现零拷贝流水线
 *
 * 回调时机：
 * - DMA 完成前半区传输 (0 ~ DMA_BUFFER_SIZE/2-1)
 * - 此时 PING 区数据已就绪，可以处理
 * - DMA 继续写入 PONG 区
 *
 * @param   hsai  SAI 句柄 (未使用)
 *
 * @note    在 DMA 中断中调用，执行时间要短
 * @note    中断优先级：5 (低于 FreeRTOS 临界区优先级)
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    (void)hsai;  /* 未使用参数 */

    /* 推送 PING 区事件 */
    Audio_FrameEvent_Push_FromISR(AUDIO_DMA_HALF_PING);
}

/**
 * @brief   SAI DMA 全传输完成回调
 * @details DMA 完成全部传输时调用
 *
 * 回调时机：
 * - DMA 完成后半区传输 (DMA_BUFFER_SIZE/2 ~ DMA_BUFFER_SIZE-1)
 * - 此时 PONG 区数据已就绪，可以处理
 * - DMA 循环回到 PING 区继续传输
 *
 * @param   hsai  SAI 句柄 (未使用)
 *
 * @note    在 DMA 中断中调用，执行时间要短
 * @note    中断优先级：5 (低于 FreeRTOS 临界区优先级)
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    (void)hsai;  /* 未使用参数 */

    /* 推送 PONG 区事件 */
    Audio_FrameEvent_Push_FromISR(AUDIO_DMA_HALF_PONG);
}
/* USER CODE END 4 */

/**
 * @brief   MPU 配置 (CubeMX 生成)
 * @details 默认 MPU 配置，后续会被 App_MPU_Config() 覆盖
 *
 * 配置内容：
 * - Region 0: 全地址空间 (4GB)，禁止访问
 * - 子区域禁用：0x87 (禁用部分子区域)
 *
 * 为什么需要默认配置？
 * - CubeMX 自动生成，保持代码结构完整
 * - 后续会被自定义配置覆盖
 *
 * @note    在 main() 开始时调用
 * @note    会被 App_MPU_Config() 覆盖
 */
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* 禁用 MPU */
  HAL_MPU_Disable();

  /* 配置 Region 0: 全地址空间，禁止访问 */
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

  /* 启用 MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief   定时器周期回调
 * @details TIM6 中断回调，用于 HAL 时基
 *
 * 功能：
 * - 递增 HAL 时基计数器 (uwTick)
 * - 用于 HAL_Delay() 等函数
 *
 * 为什么使用 TIM6？
 * - FreeRTOS 使用 SysTick 作为时基
 * - HAL 需要独立的时基，避免冲突
 * - TIM6 作为 HAL 时基定时器
 *
 * @param   htim  定时器句柄
 *
 * @note    在 TIM6 中断中调用
 * @note    中断周期：1ms
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */

  /* 检查是否为 TIM6 中断 */
  if (htim->Instance == TIM6)
  {
    /* 递增 HAL 时基计数器 */
    HAL_IncTick();
  }

  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
 * @brief   错误处理函数
 * @details 发生错误时调用，进入死循环
 *
 * 错误场景：
 * - HAL 初始化失败
 * - 外设配置错误
 * - 断言失败
 *
 * 处理方式：
 * - 禁用所有中断
 * - 进入死循环
 * - 等待调试器连接
 *
 * 调试方法：
 * - 在此函数设置断点
 * - 查看调用栈，定位错误源
 * - 检查外设配置和硬件连接
 *
 * @note    此函数不返回
 * @note    可在此添加 LED 闪烁等错误指示
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* 用户可以添加自己的错误处理代码 */
  /* 例如：LED 闪烁、串口输出错误信息等 */

  /* 禁用所有中断 */
  __disable_irq();

  /* 死循环，等待调试器连接 */
  while (1)
  {
    /* 可在此添加错误指示代码 */
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief   断言失败报告
 * @details 报告断言失败的文件名和行号
 *
 * 断言机制：
 * - 用于参数检查和错误检测
 * - 仅在 DEBUG 模式启用
 * - Release 模式自动禁用
 *
 * 使用方法：
 * - assert_param(IS_GPIO_PIN(GPIO_Pin));
 * - 参数错误时调用此函数
 *
 * 调试方法：
 * - 在此函数设置断点
 * - 查看 file 和 line 参数
 * - 定位断言失败位置
 *
 * @param   file  源文件名指针
 * @param   line  断言失败行号
 *
 * @note    仅在 USE_FULL_ASSERT 定义时编译
 * @note    可在此添加串口输出或 LED 指示
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* 用户可以添加自己的断言失败处理代码 */
  /* 例如：printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* 未使用参数 (避免编译警告) */
  (void)file;
  (void)line;

  /* 进入死循环 */
  while (1)
  {
    /* 可在此添加错误指示代码 */
  }
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

