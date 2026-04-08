/* USER CODE BEGIN Header */
/**
 * @file    sai.c
 * @brief   SAI1 Block A TDM16 音频接口初始化
 * @details 配置 SAI1 为主接收模式，通过 TDM16 帧格式接收 16 路麦克风音频数据。
 *
 * SAI 参数概览：
 * - 协议：自由协议 (Free Protocol)
 * - 工作模式：主接收 (Master RX)
 * - 数据宽度：16-bit
 * - 采样率：48kHz
 * - 帧长度：256-bit (16 slot × 16-bit)
 * - TDM slot：16 个，全部激活 (mask 0xFFFF)
 *
 * 时钟源：
 * - PLL2P 为 SAI1 内核时钟
 * - HSE 25MHz → PLL2 (M=5, N=108, P=44) → 约 12.27MHz
 * - SAI 内部分频至 BCLK ≈ 768kHz (48kHz × 16-bit/slot × 1)
 *
 * DMA 配置：
 * - DMA1 Stream0，循环模式
 * - 半字对齐 (16-bit)
 * - 优先级: Very High
 *
 * GPIO 引脚映射：
 * - PE4: SAI1_FS_A  (帧同步)
 * - PE5: SAI1_SCK_A (位时钟，与 LCD_G0 共用网络)
 * - PC1: SAI1_SD_A  (数据线)
 *
 * @note    PE5 与 LCD 共用引脚，降低翻转速率以减少 LCD 花屏
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "sai.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SAI_HandleTypeDef hsai_BlockA1;
DMA_HandleTypeDef hdma_sai1_a;

/**
 * @brief   SAI1 Block A 初始化（TDM16 主接收模式，48kHz）
 * @details 配置 SAI1_Block_A 为 TDM16 主接收，用于接收双 PCMD3180 输出的
 *          16 路音频通道数据。
 *
 * 帧结构：
 * - 帧长 256-bit = 16 slot × 16-bit
 * - 活动帧长 1-bit（FS 仅持续 1 个 BCLK 周期）
 * - FS 在第一个 bit 之前（TDM 标准时序）
 *
 * @note    PDM 直接模式已禁用，PDM 解调由 PCMD3180 硬件完成
 */
void MX_SAI1_Init(void)
{

  /* USER CODE BEGIN SAI1_Init 0 */

  /* USER CODE END SAI1_Init 0 */

  /* USER CODE BEGIN SAI1_Init 1 */

  /* USER CODE END SAI1_Init 1 */

  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.Protocol = SAI_FREE_PROTOCOL;     /* 自由协议，手动配置帧/槽位参数 */
  hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_RX;    /* 主接收模式：MCU 输出 BCLK/FSYNC */
  hsai_BlockA1.Init.DataSize = SAI_DATASIZE_16;       /* 每通道 16-bit 采样精度 */
  hsai_BlockA1.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockA1.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA1.Init.NoDivider = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA1.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;  /* 采样率 48kHz */
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.PdmInit.Activation = DISABLE;     /* 禁用 SAI 内置 PDM，由 PCMD3180 硬件解调 */
  hsai_BlockA1.Init.PdmInit.MicPairsNbr = 1;
  hsai_BlockA1.Init.PdmInit.ClockEnable = SAI_PDM_CLOCK1_ENABLE;
  hsai_BlockA1.FrameInit.FrameLength = 256;            /* 帧长 = 16 slot × 16-bit = 256-bit */
  hsai_BlockA1.FrameInit.ActiveFrameLength = 1;        /* FS 有效宽度仅 1 BCLK（TDM 标准） */
  hsai_BlockA1.FrameInit.FSDefinition = SAI_FS_STARTFRAME;  /* FS 标记帧起始 */
  hsai_BlockA1.FrameInit.FSPolarity = SAI_FS_ACTIVE_HIGH;
  hsai_BlockA1.FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;
  hsai_BlockA1.SlotInit.FirstBitOffset = 0;
  hsai_BlockA1.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockA1.SlotInit.SlotNumber = 16;               /* TDM 16 槽位 = 16 路麦克风通道 */
  hsai_BlockA1.SlotInit.SlotActive = 0x0000FFFF;       /* 全部 16 个槽位激活 (bit0-15) */
  if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SAI1_Init 2 */

  /* USER CODE END SAI1_Init 2 */

}
static uint32_t SAI1_client =0;

/**
 * @brief   SAI1 底层硬件初始化 (MSP 回调)
 * @details 由 HAL_SAI_Init() 自动调用，负责：
 *          1. 配置 SAI1 内核时钟源（PLL2P）
 *          2. 启用 SAI1 外设时钟与中断
 *          3. 配置 GPIO 引脚 (PE4/PE5/PC1)
 *          4. 初始化 DMA1_Stream0 循环接收通道
 *
 * @param   saiHandle  SAI 句柄
 *
 * @note    PE5 与 LCD_G0 共用引脚，采用中等翻转速率减少花屏干扰
 */
void HAL_SAI_MspInit(SAI_HandleTypeDef* saiHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
/* SAI1 */
    if(saiHandle->Instance==SAI1_Block_A)
    {
    /* SAI1 内核时钟配置 */

  /** 配置 SAI1 专用内核时钟 (PLL2)
   *  HSE 25MHz / M=5 = 5MHz → ×N=108 = 540MHz VCO → /P=44 ≈ 12.27MHz
   *  SAI 内部再分频至所需 BCLK */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
    PeriphClkInitStruct.PLL2.PLL2M = 5;
    PeriphClkInitStruct.PLL2.PLL2N = 108;
    PeriphClkInitStruct.PLL2.PLL2P = 44;
    PeriphClkInitStruct.PLL2.PLL2Q = 2;
    PeriphClkInitStruct.PLL2.PLL2R = 2;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    if (SAI1_client == 0)
    {
       __HAL_RCC_SAI1_CLK_ENABLE();

    /* Peripheral interrupt init*/
    HAL_NVIC_SetPriority(SAI1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SAI1_IRQn);
    }
    SAI1_client ++;

    /**SAI1_A_Block_A GPIO Configuration
    PE4     ------> SAI1_FS_A
    PE5     ------> SAI1_SCK_A
    PC1     ------> SAI1_SD_A
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* PE5 与 LCD_G0 共用网络：采用中等翻转速率，降低叠加到 LCD 显示的开关噪声 */
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 配置 SAI1 DMA 接收通道 */

    hdma_sai1_a.Instance = DMA1_Stream0;                       /* 使用 DMA1 Stream0 */
    hdma_sai1_a.Init.Request = DMA_REQUEST_SAI1_A;             /* DMA 请求源: SAI1 Block A */
    hdma_sai1_a.Init.Direction = DMA_PERIPH_TO_MEMORY;         /* 方向: 外设 → 内存 */
    hdma_sai1_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai1_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai1_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;  /* 外设端 16-bit 对齐 */
    hdma_sai1_a.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;     /* 内存端 16-bit 对齐 */
    hdma_sai1_a.Init.Mode = DMA_CIRCULAR;                            /* 循环模式: PING-PONG 双缓冲 */
    hdma_sai1_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;              /* 最高优先级: 音频实时性要求 */
    hdma_sai1_a.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_sai1_a) != HAL_OK)
    {
      Error_Handler();
    }

    /* 将 DMA 句柄绑定到 SAI 的 RX 和 TX 链路
     * 本 Block 仅用于接收，RX/TX 均指向同一 Stream */
    __HAL_LINKDMA(saiHandle,hdmarx,hdma_sai1_a);
    __HAL_LINKDMA(saiHandle,hdmatx,hdma_sai1_a);
    }
}

/**
 * @brief   SAI1 底层硬件反初始化 (MSP 反初始化回调)
 * @details 释放 SAI1 占用的 GPIO、DMA 和时钟资源。
 *          采用引用计数机制，仅在最后一个客户端释放时关闭时钟。
 *
 * @param   saiHandle  SAI 句柄
 */
void HAL_SAI_MspDeInit(SAI_HandleTypeDef* saiHandle)
{

/* SAI1 */
    if(saiHandle->Instance==SAI1_Block_A)
    {
    SAI1_client --;
    if (SAI1_client == 0)
      {
      /* Peripheral clock disable */
       __HAL_RCC_SAI1_CLK_DISABLE();
      HAL_NVIC_DisableIRQ(SAI1_IRQn);
      }

    /**SAI1_A_Block_A GPIO Configuration
    PE4     ------> SAI1_FS_A
    PE5     ------> SAI1_SCK_A
    PC1     ------> SAI1_SD_A
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_4|GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1);

    HAL_DMA_DeInit(saiHandle->hdmarx);
    HAL_DMA_DeInit(saiHandle->hdmatx);
    }
}

/**
  * @}
  */

/**
  * @}
  */
