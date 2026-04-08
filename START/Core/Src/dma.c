/* USER CODE BEGIN Header */
/**
 * @file    dma.c
 * @brief   DMA 控制器初始化
 * @details 启用 DMA1 时钟并配置 SAI1 循环接收所用的中断线 (DMA1 Stream0)。
 *
 * DMA 通道分配：
 * - DMA1 Stream0: SAI1 Block A RX（16 路麦克风 TDM 数据接收）
 *
 * 中断配置：
 * - DMA1_Stream0 中断优先级: 5/0（匹配 FreeRTOS configMAX_SYSCALL 阈值）
 * - 支持半传输和全传输完成中断，实现 PING-PONG 双缓冲机制
 *
 * @note    必须在 SAI 初始化之前调用，以确保 DMA 时钟已就绪
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "dma.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure DMA                                                              */
/*----------------------------------------------------------------------------*/

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
 * @brief   初始化 DMA 控制器时钟与中断
 * @details 启用 DMA1 外设时钟，并配置 DMA1_Stream0 中断线，
 *          用于 SAI1 音频数据的循环 DMA 接收。
 *
 * @note    中断优先级 5/0 允许在 FreeRTOS 临界区内调用 xQueueOverwriteFromISR
 */
void MX_DMA_Init(void)
{

  /* 启用 DMA1 外设时钟 */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* 配置 DMA1_Stream0 中断：用于 SAI1 RX DMA 半传输/全传输完成通知 */
  /* 优先级 5/0：满足 FreeRTOS API 安全调用要求 (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5) */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */

