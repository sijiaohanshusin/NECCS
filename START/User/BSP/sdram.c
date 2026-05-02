/**
  ******************************************************************************
  * @file    sdram.c
  * @brief   W9825G6KH SDRAM 驱动 (适配自官方例程)
    * @details 通过 FMC 控制器初始化外部 32MB SDRAM（0xC0000000），
    *          供 LTDC 帧缓冲、大数组和 UI 缓冲等大容量数据使用。
    *
    *          初始化流程：
    *          1) HAL_SDRAM_Init：写入 FMC SDCR/SDTR 配置寄存器
    *          2) sdram_initialization_sequence：执行 JEDEC 上电命令序列
    *          3) HAL_SDRAM_ProgramRefreshRate：设置自动刷新计数器
    *
    * @note    [注意] SDRAM 参数与板级硬件和时钟强绑定，
    *          变更 HCLK/SDCLK 或更换内存颗粒后必须重新核算时序。
  * @note    SDCLK = HCLK/2 = 240/2 = 120MHz (周期 8.33ns)
  *          时序参数已针对 120MHz 重新计算
  ******************************************************************************
  */

#include "sdram.h"

SDRAM_HandleTypeDef g_sdram_handle;

/**
 * @brief  初始化 SDRAM (W9825G6KH-6)
 * @note   [改进] 当前未检查 HAL_SDRAM_Init / ProgramRefreshRate 返回值，
 *         若 FMC 或时钟异常会静默失败，建议后续改为 Err_t 返回。
 */
void sdram_init(void)
{
    FMC_SDRAM_TimingTypeDef sdram_timing;

    g_sdram_handle.Instance = FMC_SDRAM_DEVICE;               /* FMC 控制器实例 */
    g_sdram_handle.Init.SDBank             = FMC_SDRAM_BANK1; /* 使用 Bank1（映射到 0xC0000000）*/
    g_sdram_handle.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;
    g_sdram_handle.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;
    g_sdram_handle.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_16;
    g_sdram_handle.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    g_sdram_handle.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_2;
    g_sdram_handle.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    g_sdram_handle.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;       /* SDCLK = 240/2 = 120MHz */
    g_sdram_handle.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
    g_sdram_handle.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_1;

    /* 时序参数 (单位: SDCLK 周期, 1 周期 = 8.33ns @120MHz) */
    sdram_timing.LoadToActiveDelay    = 2;   /* tMRD = 2 cycles */
    sdram_timing.ExitSelfRefreshDelay = 9;   /* tXSR >= 72ns => 72/8.33 = 8.6 → 9 */
    sdram_timing.SelfRefreshTime      = 6;   /* tRAS >= 42ns => 42/8.33 = 5.0 → 6 */
    sdram_timing.RowCycleDelay        = 8;   /* tRC  >= 60ns => 60/8.33 = 7.2 → 8 */
    sdram_timing.WriteRecoveryTime    = 2;   /* tWR  = 2 cycles */
    sdram_timing.RPDelay              = 3;   /* tRP  >= 18ns => 18/8.33 = 2.2 → 3 */
    sdram_timing.RCDDelay             = 3;   /* tRCD >= 18ns => 18/8.33 = 2.2 → 3 */

    HAL_SDRAM_Init(&g_sdram_handle, &sdram_timing); /* 写 SDCR/SDTR，并触发 HAL_SDRAM_MspInit */

    sdram_initialization_sequence();               /* 执行 JEDEC 上电初始化命令序列 */

    /*
     * 刷新率计数器:
     * COUNT = SDRAM刷新周期(us) * SDCLK(MHz) / 行数 - 20
     *       = 64000 * 120 / 8192 - 20 = 917
     */
    HAL_SDRAM_ProgramRefreshRate(&g_sdram_handle, 917); /* 设置自动刷新周期，防止数据衰减 */
}

/**
 * @brief  SDRAM 上电初始化序列（JEDEC 标准流程）
 * @details 顺序为 CLK_ENABLE -> delay(>100us) -> PALL -> AUTOREFRESH -> LOAD_MODE。
 *          W9825 在上电后需先稳定时钟，再执行预充电与多次自动刷新，
 *          最后加载模式寄存器（突发长度/CAS/写突发模式）。
 */
void sdram_initialization_sequence(void)
{
    uint32_t temp = 0;

    sdram_send_cmd(0, FMC_SDRAM_CMD_CLK_ENABLE, 1, 0);      /* CMD1: 使能 SDRAM 时钟输出 */

    /* 软件延时 >= 200us (480MHz 下约 100000 个 NOP ≈ 208us) */
    for (volatile uint32_t i = 0; i < 100000; i++) { __NOP(); }
    sdram_send_cmd(0, FMC_SDRAM_CMD_PALL, 1, 0);             /* CMD2: 预充电所有 Bank */
    sdram_send_cmd(0, FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8, 0); /* CMD3: 自动刷新 8 次 */

    /* CMD4: 组装并加载 Mode Register */
    temp = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1   |
            SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      |
            SDRAM_MODEREG_CAS_LATENCY_2              |
            SDRAM_MODEREG_OPERATING_MODE_STANDARD    |
            SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    sdram_send_cmd(0, FMC_SDRAM_CMD_LOAD_MODE, 1, temp);     /* 写入模式寄存器 */
}

/**
 * @brief  FMC SDRAM 底层初始化 (GPIO + 时钟)
 * @note   由 HAL_SDRAM_Init() 自动回调
 */
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
    GPIO_InitTypeDef gpio_init_struct;
    (void)hsdram; /* 本工程仅一个 SDRAM 控制器实例，参数保留给 HAL 回调接口 */

    __HAL_RCC_FMC_CLK_ENABLE();   /* 先开 FMC 时钟，再配置 FMC 相关 GPIO */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;            /* 复用推挽输出 */
    gpio_init_struct.Pull      = GPIO_PULLUP;                /* 上拉降低空闲期抖动风险 */
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init_struct.Alternate = GPIO_AF12_FMC;              /* AF12 = FMC */

    /* PC0 - SDNWE */
    gpio_init_struct.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gpio_init_struct);

    /* PD0,1,8,9,10,14,15 - D2,D3,D13,D14,D15,D0,D1 */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                           GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio_init_struct);

    /* PE0,1,7,8,9,10,11,12,13,14,15 - NBL0,NBL1,D4~D12 */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
                           GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                           GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init_struct);

    /* PF0~5,11~15 - A0~A5,SDNRAS,A6~A9 */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                           GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
                           GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio_init_struct);

    /* PG0,1,2,4,5,8,15 - A10,A11,A12,BA0,BA1,SDCLK,SDNCAS */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
                           GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);

    /* PH2,3 - SDCKE0,SDNE0 */
    gpio_init_struct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOH, &gpio_init_struct);

    /* 使能 I/O 补偿单元，校准高速 GPIO 输出阻抗，改善高速边沿完整性。 */
    __HAL_RCC_CSI_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_EnableCompensationCell();
}

/**
 * @brief  向 SDRAM 发送命令
 * @param  bankx   0=Bank1，1=Bank2
 * @param  cmd     FMC_SDRAM_CMD_* 命令类型
 * @param  refresh 自动刷新次数（仅 AUTOREFRESH 命令有效）
 * @param  regval  Mode Register 值（仅 LOAD_MODE 命令有效）
 * @retval 0=成功，1=失败
 * @note   [注意] bankx 非 0/1 时 target_bank 保持 0，命令会失败并返回 1。
 */
uint8_t sdram_send_cmd(uint8_t bankx, uint8_t cmd, uint8_t refresh, uint16_t regval)
{
    uint32_t target_bank = 0;
    FMC_SDRAM_CommandTypeDef command;

    if (bankx == 0)
        target_bank = FMC_SDRAM_CMD_TARGET_BANK1;
    else if (bankx == 1)
        target_bank = FMC_SDRAM_CMD_TARGET_BANK2;

    command.CommandMode            = cmd;
    command.CommandTarget          = target_bank;
    command.AutoRefreshNumber      = refresh;
    command.ModeRegisterDefinition = regval;

    if (HAL_SDRAM_SendCommand(&g_sdram_handle, &command, 0x1000) == HAL_OK)
        return 0;
    else
        return 1;
}

/**
 * @brief  向 SDRAM 写入 n 个字节
 * @note   逐字节 CPU 写入，适用于小数据测试；大块数据建议 DMA/32位访问优化。
 */
void fmc_sdram_write_buffer(uint8_t *pbuf, uint32_t writeaddr, uint32_t n)
{
    for (; n != 0; n--)
    {
        *(volatile uint8_t *)(BANK5_SDRAM_ADDR + writeaddr) = *pbuf;
        writeaddr++;
        pbuf++;
    }
}

/**
 * @brief  从 SDRAM 读取 n 个字节
 * @note   与写函数同理为逐字节访问，验证连通性方便但吞吐较低。
 */
void fmc_sdram_read_buffer(uint8_t *pbuf, uint32_t readaddr, uint32_t n)
{
    for (; n != 0; n--)
    {
        *pbuf++ = *(volatile uint8_t *)(BANK5_SDRAM_ADDR + readaddr);
        readaddr++;
    }
}
