/**
 * @file    bsp_sd.c
 * @brief   SD 卡 BSP 驱动实现 (SDMMC1, 4-bit mode)
 * @details GPIO: PC8(D0), PC9(D1), PC10(D2), PC11(D3), PC12(CLK), PD2(CMD)
 *          时钟: SDMMC1 使用 PLL1Q (最高 240 MHz), 分频后 ≤ 50 MHz
 */
#include "bsp_sd.h"

#include <string.h>

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
    RCC_PeriphCLKInitTypeDef clk_init = {0};

    /* GPIO 初始化 */
    s_sd_gpio_init();

    /* 配置 SDMMC1 内核时钟源为 PLL2R (270 MHz)
     * PLL2 已在 SAI MspInit 中配置: HSE/5 × 108 = 540 MHz VCO, R=2 → 270 MHz
     * SDMMC_CK = 270 / (2 × ClockDiv) = 270 / 8 = 33.75 MHz */
    clk_init.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    clk_init.SdmmcClockSelection  = RCC_SDMMCCLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&clk_init) != HAL_OK)
    {
        return BSP_SD_ERROR;
    }

    /* 使能 SDMMC1 时钟 */
    __HAL_RCC_SDMMC1_CLK_ENABLE();

    /* 配置 SDMMC1：4-bit, 起始低速 (识别阶段), 后续提速 */
    (void)memset(&s_hsd, 0, sizeof(s_hsd));
    s_hsd.Instance                 = SDMMC1;
    s_hsd.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    s_hsd.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    s_hsd.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    s_hsd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    s_hsd.Init.ClockDiv            = 4u; /* SDMMC1 CLK = kernel_clk / (2 * ClockDiv) */

    hal_ret = HAL_SD_Init(&s_hsd);
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
