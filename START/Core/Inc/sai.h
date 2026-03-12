/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sai.h
  * @brief   SAI1 audio interface initialization interface.
  * @details Exposes the `hsai_BlockA1` handle and `MX_SAI1_Init()` used by
  *          the audio capture pipeline (TDM16 + DMA circular receive).
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
#ifndef __SAI_H__
#define __SAI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern SAI_HandleTypeDef hsai_BlockA1;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

/**
 * @brief Initialize SAI1 Block A for microphone TDM capture.
 */
void MX_SAI1_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __SAI_H__ */

