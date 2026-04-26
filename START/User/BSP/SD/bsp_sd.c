/**
 * @file    bsp_sd.c
 * @brief   SD 卡 BSP 驱动实现 (SDMMC1, 1-bit init → 4-bit upgrade)
 * @details GPIO: PC8(D0), PC9(D1), PC10(D2), PC11(D3), PC12(CLK), PD2(CMD)
 *          时钟: SDMMC1 使用 PLL1Q (240 MHz, PLLQ=4), 分频后 30 MHz
 */
#include "bsp_sd.h"

#include <string.h>
#include <stdio.h>         /* 临时诊断: printf */
#include "stm32h7xx_hal.h" /* HAL_RCCEx_GetPeriphCLKFreq */

/** @brief SD 句柄 */
static SD_HandleTypeDef s_hsd;

/** @brief 初始化标志 */
static uint8_t s_initialized = 0u;

/**
 * @brief SDMMC1 GPIO 初始化
 * @details PC8-PC11: SDMMC1_D0-D3, PC12: SDMMC1_CK, PD2: SDMMC1_CMD
 *          全部 AF12, 高速, 上拉
 */
static void s_sd_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8..PC12 -> SDMMC1 (AF12) */
    gpio.Pin   = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO1;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD2 -> SDMMC1_CMD (AF12) */
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);
}

/**
 * @brief SDMMC1 GPIO 反初始化
 */
static void s_sd_gpio_deinit(void)
{
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                           GPIO_PIN_11 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
}

BSP_SD_Status_t BSP_SD_Init(void)
{
    HAL_StatusTypeDef hal_ret;
    uint8_t retry;

    /* 防止 FatFS disk_initialize 二次调用导致 handle 被 memset 清零 */
    if (s_initialized != 0u)
    {
        return BSP_SD_OK;
    }

    /* GPIO 初始化 */
    s_sd_gpio_init();

    /* SDMMC1 内核时钟: PLL1Q = 960 MHz VCO / 4 = 240 MHz (SystemClock_Config 中 PLLQ=4)
     * SDMMC_CK = 240 / (2 × ClockDiv) — ClockDiv=4 → 30 MHz
     *
     * 注意: 不可使用 PLL2R 作为 SDMMC 时钟源, 因为 HAL_RCCEx_PeriphCLKConfig 会
     * 停止 PLL2 并重配, 破坏 SAI1 的 PLL2P 时钟, 导致 PCMD3180 无音频. */
    __HAL_RCC_SDMMC_CONFIG(RCC_SDMMCCLKSOURCE_PLL);  /* 显式选择 PLL1Q, 防止暖启动残留 */

    printf("[SD] init: clk=%lu tick=%lu\r\n",
           HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC), HAL_GetTick());

    hal_ret = HAL_ERROR;

    for (retry = 0u; retry < 5u; retry++)
    {
        /* ---- 每次重试: RCC 硬复位 SDMMC1 外设 (清除 DPSMACT/STA 等残留) ---- */
        __HAL_RCC_SDMMC1_CLK_ENABLE();
        __HAL_RCC_SDMMC1_FORCE_RESET();
        __HAL_RCC_SDMMC1_RELEASE_RESET();

        /* 确保 SDMMC 电源关闭, 给 SD 卡内部状态机充足复位时间 */
        SDMMC1->POWER = 0u;
        HAL_Delay(200u);  /* 200ms: cold-start/warm-restart need extra time for card reset */

        /* 清零 handle 并配置参数 */
        (void)memset(&s_hsd, 0, sizeof(s_hsd));
        s_hsd.Instance                 = SDMMC1;
        s_hsd.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
        s_hsd.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
        s_hsd.Init.BusWide             = SDMMC_BUS_WIDE_1B;  /* Init 1-bit; upgrade to 4-bit post-init */
        s_hsd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
        s_hsd.Init.ClockDiv            = 4u; /* SDMMC1 CLK = kernel_clk / (2 * ClockDiv) */

        hal_ret = HAL_SD_Init(&s_hsd);
        if (hal_ret == HAL_OK)
        {
            break;
        }

        /* 诊断: 输出每次重试的外设寄存器状态 */
        printf("[SD_DIAG] try=%u err=0x%08lX STA=0x%08lX PWR=0x%08lX CLKCR=0x%08lX\r\n",
               (unsigned)retry, s_hsd.ErrorCode,
               SDMMC1->STA, SDMMC1->POWER, SDMMC1->CLKCR);

        /* 不调用 HAL_SD_DeInit (在外设异常态下可能阻塞), 
         * 下一轮 RCC FORCE_RESET 会彻底清除所有寄存器 */
    }

    if (hal_ret != HAL_OK)
    {
        s_initialized = 0u;
        return BSP_SD_ERROR;
    }

    /* 切换到 4-bit 宽总线 */
    hal_ret = HAL_SD_ConfigWideBusOperation(&s_hsd, SDMMC_BUS_WIDE_4B);
    if (hal_ret != HAL_OK)
    {
        /* 4-bit 失败，回退到 1-bit（部分卡不支持宽总线） */
        (void)HAL_SD_ConfigWideBusOperation(&s_hsd, SDMMC_BUS_WIDE_1B);
    }

    s_initialized = 1u;
    printf("[SD] init OK\r\n");
    return BSP_SD_OK;
}

BSP_SD_Status_t BSP_SD_DeInit(void)
{
    (void)HAL_SD_DeInit(&s_hsd);
    s_sd_gpio_deinit();
    __HAL_RCC_SDMMC1_CLK_DISABLE();
    s_initialized = 0u;
    return BSP_SD_OK;
}

BSP_SD_Status_t BSP_SD_ReadBlocks(uint8_t *pData, uint32_t block_addr,
                                   uint32_t num_blocks, uint32_t timeout)
{
    HAL_StatusTypeDef ret;

    if (s_initialized == 0u)
    {
        return BSP_SD_NOT_INIT;
    }

    ret = HAL_SD_ReadBlocks(&s_hsd, pData, block_addr, num_blocks, timeout);
    if (ret == HAL_OK)
    {
        /* 等待传输完成 — 带超时保护 */
        uint32_t retry = 0u;
        while (HAL_SD_GetCardState(&s_hsd) != HAL_SD_CARD_TRANSFER)
        {
            if (++retry > 200000u)  /* ~200ms @ busy-wait */
            {
                return BSP_SD_TIMEOUT;
            }
        }
        return BSP_SD_OK;
    }
    else if (ret == HAL_TIMEOUT)
    {
        return BSP_SD_TIMEOUT;
    }
    return BSP_SD_ERROR;
}

BSP_SD_Status_t BSP_SD_WriteBlocks(const uint8_t *pData, uint32_t block_addr,
                                    uint32_t num_blocks, uint32_t timeout)
{
    HAL_StatusTypeDef ret;

    if (s_initialized == 0u)
    {
        return BSP_SD_NOT_INIT;
    }

    /* HAL_SD_WriteBlocks 参数不使用 const — 转换 */
    ret = HAL_SD_WriteBlocks(&s_hsd, (uint8_t *)pData, block_addr, num_blocks, timeout);
    if (ret == HAL_OK)
    {
        uint32_t retry = 0u;
        while (HAL_SD_GetCardState(&s_hsd) != HAL_SD_CARD_TRANSFER)
        {
            if (++retry > 200000u)
            {
                return BSP_SD_TIMEOUT;
            }
        }
        return BSP_SD_OK;
    }
    else if (ret == HAL_TIMEOUT)
    {
        return BSP_SD_TIMEOUT;
    }
    return BSP_SD_ERROR;
}

BSP_SD_Status_t BSP_SD_GetCardInfo(BSP_SD_CardInfo_t *info)
{
    HAL_SD_CardInfoTypeDef hal_info;

    if ((info == NULL) || (s_initialized == 0u))
    {
        return BSP_SD_ERROR;
    }

    (void)HAL_SD_GetCardInfo(&s_hsd, &hal_info);
    info->card_type      = hal_info.CardType;
    info->block_count    = hal_info.LogBlockNbr;
    info->block_size     = hal_info.LogBlockSize;
    info->capacity_bytes = (uint64_t)hal_info.LogBlockNbr * hal_info.LogBlockSize;

    return BSP_SD_OK;
}

uint8_t BSP_SD_IsInitialized(void)
{
    return s_initialized;
}

SD_HandleTypeDef *BSP_SD_GetHandle(void)
{
    return &s_hsd;
}
