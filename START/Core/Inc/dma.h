/* USER CODE BEGIN Header */
/**
  * @file    dma.h
  * @brief   DMA 初始化接口声明
  * @details 当前工程主要使用 DMA1_Stream0 为 SAI1_RX 提供循环搬运。
  *
  * 设计约束：
  * - 中断优先级设置为 5，兼容 FreeRTOS 临界区策略。
  * - 采用循环模式，持续接收音频帧数据。
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DMA_H__
#define __DMA_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* DMA memory to memory transfer handles -------------------------------------*/

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

/**
 * @brief 初始化 DMA 控制器与相关中断。
 */
void MX_DMA_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __DMA_H__ */

