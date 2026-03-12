/**
 * @file    pcmd3180.c
 * @brief   PCMD3180 PDM 转 TDM 芯片驱动实现
 * @details TI PCMD3180 是 8 通道 PDM 麦克风接口转 TDM 输出芯片。
 *          本工程使用两颗芯片，共接入 16 路 PDM 麦克风，通过 TDM16 输出到 SAI2。
 *
 * 硬件配置：
 * - 芯片 A (地址 0x4C): TDM Slot 0-7  (麦克风 0-7)
 * - 芯片 B (地址 0x4D): TDM Slot 8-15 (麦克风 8-15)
 * - 控制接口：软件模拟 I2C (soft_i2c.c, PE2=SCL, PE3=SDA)
 * - PDM 时钟：3.072 MHz (由 SAI 的 BCLK 分频提供)
 * - TDM 输出：16-bit, 48 kHz, 16 通道
 *
 * 初始化流程（每颗芯片独立执行）：
 * 1. 软复位 (写 0x01 到 Reg 0x01)
 * 2. 唤醒设备 (写 0x81 到 Reg 0x02，使能 PLL)
 * 3. 配置 ASI 格式 (TDM 模式，MSB 偏移 1 个 BCLK)
 * 4. 配置 TDM 槽位 (Chip A 从 slot 0 起，Chip B 从 slot 8 起)
 * 5. 配置 PDM 接口 (GPO 输出 PDM CLK，High Drive 模式)
 * 6. 配置 DSP (启用 HPF，截止频率 96Hz，去除直流偏置)
 * 7. 配置输入源 (所有通道设为 PDM 输入)
 * 8. 配置时钟模式 (Slave 模式，BCLK/FSYNC 由 SAI 提供)
 * 9. 使能输入通道、ASI 输出，上电 PLL 和 PDM 转换器
 *
 * 关键寄存器：
 * - 0x01: 软件复位 (写 0x01 触发复位)
 * - 0x02: 睡眠控制 (0x81=唤醒并使能 PLL, 0x80=睡眠)
 * - 0x07-0x09: ASI 格式配置 (TDM 模式、偏移、其他)
 * - 0x0B-0x12: TDM 槽位分配 (Ch1-Ch8 各占一个槽)
 * - 0x13-0x16: 时钟架构配置 (主从模式、时钟源)
 * - 0x1F-0x2C: PDM 接口与 GPIO 配置
 * - 0x3C+: 输入通道配置 (每通道 5 个寄存器)
 * - 0x6B: DSP 配置 (HPF 使能)
 * - 0x73: 输入通道使能 (0xFF = 全部使能)
 * - 0x74: ASI 输出使能 (0xFF = 全部使能)
 * - 0x75: 上电配置 (0x60 = PLL + PDM 转换器上电)
 */

#include "pcmd3180.h"
#include "soft_i2c.h"
#include <stdio.h>

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief   写寄存器封装
 * @details 对 PCMD_WriteReg 的简单包装，便于后续切换到硬件 I2C 时只需修改此处。
 * @param   devAddr  7-bit 设备地址 (0x4C 或 0x4D)
 * @param   reg      寄存器地址 (8-bit)
 * @param   val      写入值 (8-bit)
 */
static void Write_Reg(uint8_t devAddr, uint8_t reg, uint8_t val)
{
    PCMD_WriteReg(devAddr, reg, val);
}

/* ==================== 核心流程控制 ==================== */

/**
 * @brief   初始化单颗 PCMD3180 设备
 * @details 按照 TI 数据手册推荐流程完成完整初始化：
 *          复位 → 唤醒 → 配置 ASI/TDM/PDM/DSP/时钟 → 使能输出
 *
 * 初始化顺序说明：
 * - 复位后必须等待 10ms，让内部 POR 电路稳定
 * - 唤醒后必须等待 10ms，让 PLL 锁定
 * - 所有配置必须在 PLL 锁定后进行，否则时钟域可能不稳定
 * - 最后一步上电 (PCMD_Enable_Blocks) 才真正启动 PDM 采样
 *
 * @param   devAddr    7-bit I2C 设备地址 (0x4C=芯片A, 0x4D=芯片B)
 * @param   startSlot  TDM 起始槽位 (芯片A=0, 芯片B=8)
 * @return  0: 初始化成功
 */
uint8_t PCMD3180_Init_Device(uint8_t devAddr, uint8_t startSlot)
{
    /* 步骤 0: 软复位，清除所有寄存器到默认值 */
    PCMD_Ctrl_Reset(devAddr);
    vTaskDelay(pdMS_TO_TICKS(10));  /* 等待内部 POR 电路稳定 */

    /* 步骤 1: 唤醒设备，使能内部 PLL */
    /* 写 0x81 到 Reg 0x02：bit7=1 表示唤醒，bit0=1 表示使能 PLL */
    PCMD_Ctrl_Sleep(devAddr, 0);
    vTaskDelay(pdMS_TO_TICKS(10));  /* 等待 PLL 锁定 */

    /* ---- 配置阶段（顺序不可随意调换）---- */

    /* 步骤 2: 配置 ASI 音频格式 (TDM 模式，MSB 偏移 1 个 BCLK) */
    PCMD_Config_ASI_Format(devAddr);

    /* 步骤 3: 配置 TDM 槽位映射 (芯片A从slot 0起，芯片B从slot 8起) */
    PCMD_Config_TDM_Slots(devAddr, startSlot);

    /* 步骤 4: 配置 PDM 接口与 GPIO 复用
     * GPO1-4 输出 PDM CLK，High Drive 模式保证边沿质量 */
    PCMD_Config_PDM_IO(devAddr);

    /* 步骤 5: 配置 DSP 信号处理链 (启用 HPF，截止频率 96Hz @ 48kHz) */
    PCMD_Config_DSP(devAddr);

    /* 步骤 6: 配置输入通道源 (所有 8 路通道设为 PDM 输入) */
    PCMD_Config_Channels(devAddr);

    /* 步骤 7: 配置时钟架构 (Slave 模式，BCLK/FSYNC 由 SAI 主机提供) */
    PCMD_Config_Clock_Mode(devAddr);

    /* ---- 启动阶段 ---- */

    /* 步骤 8: 使能输入通道、ASI 输出，上电 PLL 和 PDM 转换器核心 */
    PCMD_Enable_Blocks(devAddr);

    return 0;
}


/**
 * @brief  检查设备 ID 或 I2C 通讯是否正常
 */
uint8_t PCMD3180_Check_ID(uint8_t devAddr)
{
    /* 当前工程仅保留接口；上线前建议读取只读寄存器做在线检查。 */
    (void)devAddr;
    return 0;
}

/* ==================== 子功能模块实现 ==================== */

/**
 * @brief   软件复位设备
 * @details 向 Reg 0x01 写入 0x01 触发内部软复位，所有寄存器恢复默认值。
 *          复位后需等待至少 10ms 让内部 POR 电路完成初始化。
 * @param   devAddr  7-bit I2C 设备地址 (0x4C 或 0x4D)
 */
void PCMD_Ctrl_Reset(uint8_t devAddr)
{
    Write_Reg(devAddr, PCMD_REG_SW_RESET, 0x01);
}

/**
 * @brief   控制设备睡眠/唤醒
 * @details 通过 Reg 0x02 (SLEEP_CFG) 控制设备工作状态：
 *          - 唤醒 (0x81)：bit7=1 退出睡眠，bit0=1 使能 PLL
 *          - 睡眠 (0x80)：bit7=1 进入睡眠，PLL 关闭以节省功耗
 *          唤醒后需等待约 10ms 让 PLL 完成锁定，再进行后续配置。
 * @param   devAddr  7-bit I2C 设备地址 (0x4C 或 0x4D)
 * @param   enable   1=进入睡眠模式, 0=唤醒设备
 */
void PCMD_Ctrl_Sleep(uint8_t devAddr, uint8_t enable)
{
    if (enable == 1u)
    {
        Write_Reg(devAddr, PCMD_REG_SLEEP_CFG, 0x80);  /* 进入睡眠：bit7=1, PLL 关闭 */
    }
    else
    {
        Write_Reg(devAddr, PCMD_REG_SLEEP_CFG, 0x81);  /* 唤醒：bit7=1 退出睡眠，bit0=1 使能 PLL */
    }
}

/**
 * @brief   配置 ASI (Audio Serial Interface) 格式
 * @details 配置 TDM 音频总线格式，使 PCMD3180 输出与 SAI 主机时序匹配：
 *          - ASI_CFG0 (0x07) = 0x01：非本芯片占用的时隙保持高阻态，避免总线冲突
 *          - ASI_CFG1 (0x08) = 0x01：数据 MSB 相对 FSYNC 偏移 1 个 BCLK 周期
 *            (符合 TDM 标准，STM32 SAI 默认也是此偏移)
 *          - ASI_CFG2 (0x09) = 0x00：其他 ASI 参数保持默认
 * @param   devAddr  7-bit I2C 设备地址 (0x4C 或 0x4D)
 */
void PCMD_Config_ASI_Format(uint8_t devAddr)
{
    /* ASI_CFG0: 非本芯片时隙输出高阻态，防止多芯片共享 TDM 总线时冲突 */
    Write_Reg(devAddr, PCMD_REG_ASI_CFG0, 0x01);
    /* ASI_CFG1: 数据 MSB 相对 FSYNC 偏移 1 个 BCLK，符合 TDM 标准时序 */
    Write_Reg(devAddr, PCMD_REG_ASI_CFG1, 0x01);
    /* ASI_CFG2: 其他 ASI 参数保持默认值 */
    Write_Reg(devAddr, PCMD_REG_ASI_CFG2, 0x00);
}

/**
 * @brief   配置 TDM 时隙分配
 * @details 将芯片内部 8 个通道依次映射到 TDM 总线的连续时隙上。
 *          寄存器 0x0B-0x12 (PCMD_REG_ASI_CH1 ~ CH8) 分别对应通道 1-8，
 *          写入值即为该通道在 TDM 帧中的槽位编号 (0-15)。
 *
 * 本工程映射关系：
 * - 芯片 A (startSlot=0): Ch1→slot0, Ch2→slot1, ..., Ch8→slot7
 * - 芯片 B (startSlot=8): Ch1→slot8, Ch2→slot9, ..., Ch8→slot15
 *
 * @param   devAddr    7-bit I2C 设备地址 (0x4C 或 0x4D)
 * @param   startSlot  起始时隙编号 (芯片A=0, 芯片B=8)
 */
void PCMD_Config_TDM_Slots(uint8_t devAddr, uint8_t startSlot)
{
    /* 将 Ch1-Ch8 (Reg 0x0B-0x12) 依次映射到 startSlot, startSlot+1, ..., startSlot+7 */
    for (uint8_t i = 0u; i < 8u; i++)
    {
        Write_Reg(devAddr, PCMD_REG_ASI_CH1 + i, startSlot + i);
    }
}

/**
 * @brief   配置时钟架构 (Slave 模式)
 * @details 将 PCMD3180 配置为时钟从机，BCLK 和 FSYNC 均由 STM32 SAI 主机提供。
 *          Auto Clock 模式下芯片自动检测输入时钟频率，无需手动配置分频比。
 *
 * 寄存器配置：
 * - MST_CFG0 (0x13) = 0x00：Slave 模式，BCLK/FSYNC 方向为输入
 * - MST_CFG1 (0x14) = 0x00：Auto Clock 模式，自动适配时钟频率
 * - CLK_SRC  (0x16) = 0x00：时钟源选择 BCLK 引脚 (外部输入)
 *
 * @param   devAddr  7-bit I2C 设备地址 (0x4C 或 0x4D)
 */
void PCMD_Config_Clock_Mode(uint8_t devAddr)
{
    // 写入 0x00 即可完美适配
    PCMD_WriteReg(devAddr, PCMD_REG_MST_CFG0, 0x00);
    PCMD_WriteReg(devAddr, PCMD_REG_MST_CFG1, 0x00);
    //配置时钟输入引脚
    PCMD_WriteReg(devAddr, PCMD_REG_CLK_SRC, 0x00);
}

/**
 * @brief  配置 PDM 接口与 GPIO
 * @param  devAddr  I2C 设备地址 (7-bit)
 * @note   GPO1-4 配置为 PDM CLK 输出 (High Drive)
 *         GPI 配置为 PDM 数据输入
 */
void PCMD_Config_PDM_IO(uint8_t devAddr)
{
    PCMD_WriteReg(devAddr, PCMD_REG_PDMCLK_CFG, 0x40);
    PCMD_WriteReg(devAddr, PCMD_REG_PDMIN_CFG, 0x00);
    //配置 GPIO 复用
    PCMD_WriteReg(devAddr, PCMD_REG_GPIO_CFG0, 0x00);
    // GPO1-4 (Reg 0x22-0x25) 全部设为 PDM CLK Output (通常是 0x04)
    PCMD_WriteReg(devAddr, PCMD_REG_GPO_CFG0, 0x41);
    PCMD_WriteReg(devAddr, PCMD_REG_GPO_CFG1, 0x41);
    PCMD_WriteReg(devAddr, PCMD_REG_GPO_CFG2, 0x41);
    PCMD_WriteReg(devAddr, PCMD_REG_GPO_CFG3, 0x41);
    //GPO_VAL Register跳过
    //GPIO_MON Register跳过
    //GPI配置
    PCMD_WriteReg(devAddr, PCMD_REG_GPI_CFG0, 0x45);
    PCMD_WriteReg(devAddr, PCMD_REG_GPI_CFG1, 0x67);
}

/**
 * @brief  配置 DSP 处理链
 * @param  devAddr  I2C 设备地址 (7-bit)
 * @note   启用 HPF (高通滤波器)，当前配置对应 96Hz @ 48kHz
 *         用于去除直流偏置和低频噪声
 */
void PCMD_Config_DSP(uint8_t devAddr)
{
    //启用 HPF (Reg 0x6B) -> 0x02 (96Hz for 48k)
    PCMD_WriteReg(devAddr, PCMD_REG_DSP_CFG0, 0x02);//可在图形界面配置
    PCMD_WriteReg(devAddr, PCMD_REG_DSP_CFG1, 0x00);
}

/**
 * @brief  配置 8 个输入通道
 * @param  devAddr  I2C 设备地址 (7-bit)
 * @note   将所有通道输入源设为 PDM (0x40)
 *         每个通道占 5 个寄存器 (CFG0-CFG4)
 */
void PCMD_Config_Channels(uint8_t devAddr)
{
    // 1. 配置 VREF (Reg 0x3B)
    // 假设 AVDD=3.3V, 使用 2.75V VREF
    PCMD_WriteReg(devAddr, PCMD_REG_BIAS_CFG, 0x00);

    // 2. 批量配置 8 个通道
    // 每个通道占 5 个寄存器 (CFG0 - CFG4)
    // 我们只需要改 CFG0 (开启PDM)，其他(Vol, Phase)保持默认0
    
    uint8_t ch_base;
    for (int i = 0; i < 8; i++) {
        // 计算每个通道的 CFG0 地址
        // CH1=0x3C, CH2=0x41, ... 偏移量是 5 * i
        ch_base = PCMD_REG_CH1_CFG0 + (i * 5);
        // 关键：将 Input Source 设为 PDM
        PCMD_WriteReg(devAddr, ch_base, 0x40);
        // (可选) 确保 Phase Calibration 为 0 (基地址 + 4)
        //PCMD_WriteReg(devAddr, ch_base + 4, 0x00);
    }
}

/**
 * @brief  使能所有功能模块并上电
 * @param  devAddr  I2C 设备地址 (7-bit)
 * @note   按顺序：使能输入通道 -> 使能 ASI 输出 -> 上电 PLL+PDM
 *         本工程 `PWR_CFG` 配置值为 `0x60`。
 */
void PCMD_Enable_Blocks(uint8_t devAddr)
{
    // 1. 开启输入通道 (Ch1-Ch8)
    // 对应文档 Step g: Enable input channels
    PCMD_WriteReg(devAddr, PCMD_REG_IN_CH_EN, 0xFF);

    // 2. 开启 ASI 输出 (Ch1-Ch8)
    // 对应文档 Step h: Enable ASI output
    PCMD_WriteReg(devAddr, PCMD_REG_ASI_OUT_EN, 0xFF);

    // 3. 启动 PLL 和 PDM 转换器核心电源
    // 对应文档 Step i: Power-up PDM & PLL
    PCMD_WriteReg(devAddr, PCMD_REG_PWR_CFG, 0x60);
}

/**
 * @brief  检查设备健康状态
 * @param  devAddr  I2C 设备地址 (7-bit)
 * @note   读取中断锁存寄存器 (读后自动清除)
 *         检查 ASI 时钟错误和 PLL 锁定丢失
 */
void PCMD_Check_Health(uint8_t devAddr)
{
    uint8_t status_int = 0;
    uint8_t status_pwr = 0;
    
    // 1. 检查是否有过报错 (读完会自动清除)
    PCMD_ReadReg(devAddr, PCMD_REG_INT_LTCH0, &status_int); // Reg 0x36
    
    if (status_int & 0x80) printf("[Error] ASI Bus Clock Error on 0x%02X!\r\n", devAddr);
    if (status_int & 0x40) printf("[Error] PLL Lock Lost on 0x%02X!\r\n", devAddr);
    
    // 2. 检查电源配置是否掉线
    PCMD_ReadReg(devAddr, PCMD_REG_PWR_CFG, &status_pwr);   // Reg 0x75
    if (status_pwr != 0x60) printf("[Error] Power Config Lost (Read: 0x%02X)!\r\n", status_pwr);
    // 3. (可选) 检查 GPI1 实时状态
    // 如果你在调试麦克风连接，可以取消注释下面这段
    
    uint8_t gpi_state;
    PCMD_ReadReg(devAddr, PCMD_REG_GPI_MON, &gpi_state);    // Reg 0x2F
    printf("GPI State: 0x%02X\r\n", gpi_state);
    
}


/**
 * @brief  转储关键寄存器值 (调试用)
 * @param  devAddr  I2C 设备地址 (7-bit)
 * @note   通过 UART 打印所有关键寄存器的当前值和期望值
 *         用于排查初始化问题
 */
void PCMD_Dump_Registers(uint8_t devAddr)
{
    uint8_t val;
    printf("\r\n========================================\r\n");
    printf(" PCMD3180 Register Dump | Device: 0x%02X\r\n", devAddr);
    printf("========================================\r\n");

    // --- 1. 系统与状态 (System) ---
    PCMD_ReadReg(devAddr, 0x00, &val); printf("[0x00] Page Select : 0x%02X (Expect 0x00)\r\n", val);
    PCMD_ReadReg(devAddr, 0x02, &val); printf("[0x02] SLEEP_CFG   : 0x%02X (Expect 0x01/81)\r\n", val);
    
    // --- 2. ASI 总线配置 (ASI Bus) ---
    PCMD_ReadReg(devAddr, 0x07, &val); printf("[0x07] ASI_CFG0    : 0x%02X (Expect 0x01)\r\n", val);
    PCMD_ReadReg(devAddr, 0x08, &val); printf("[0x08] ASI_CFG1    : 0x%02X (Expect 0x01)\r\n", val);
    PCMD_ReadReg(devAddr, 0x0B, &val); printf("[0x0B] CH1_SLOT    : 0x%02X\r\n", val);
    
    // --- 3. 时钟架构 (Clock) ---
    PCMD_ReadReg(devAddr, 0x13, &val); printf("[0x13] MST_CFG0    : 0x%02X (Expect 0x00)\r\n", val);
    PCMD_ReadReg(devAddr, 0x16, &val); printf("[0x16] CLK_SRC     : 0x%02X (Expect 0x00)\r\n", val);
    PCMD_ReadReg(devAddr, 0x1F, &val); printf("[0x1F] PDMCLK_CFG  : 0x%02X (Expect 0x40)\r\n", val);
    
    // --- 4. 麦克风接口与引脚 (PDM IO) ---
    PCMD_ReadReg(devAddr, 0x20, &val); printf("[0x20] PDMIN_CFG   : 0x%02X (Expect 0x00)\r\n", val);
    PCMD_ReadReg(devAddr, 0x22, &val); printf("[0x22] GPO1_CFG    : 0x%02X (Expect 0x40)\r\n", val);
    PCMD_ReadReg(devAddr, 0x2B, &val); printf("[0x2B] GPI_CFG0    : 0x%02X (Expect 0x45)\r\n", val);
    
    // --- 5. 关键状态监控 (Monitoring) ---
    // GPI_MON: 实时查看引脚电平 (排查虚焊神器)
    PCMD_ReadReg(devAddr, 0x2F, &val); printf("[0x2F] GPI_MON     : 0x%02X (Input State)\r\n", val);
    // INT_LTCH0: 错误历史记录
    PCMD_ReadReg(devAddr, 0x36, &val); printf("[0x36] INT_LTCH0   : 0x%02X (Error Log)\r\n", val);
    
    // --- 6. 通道配置 (Channel Sample) ---
    // 只看 Ch1 就够了，其他通道应该是一样的
    PCMD_ReadReg(devAddr, 0x3C, &val); printf("[0x3C] CH1_INSRC   : 0x%02X (Expect 0x40)\r\n", val);
    
    // --- 7. DSP 配置 ---
    PCMD_ReadReg(devAddr, 0x6B, &val); printf("[0x6B] DSP_CFG0    : 0x%02X (Expect 0x02)\r\n", val);
    
    // --- 8. 全局使能与电源 (Enable & Power) ---
    PCMD_ReadReg(devAddr, 0x73, &val); printf("[0x73] IN_CH_EN    : 0x%02X (Expect 0xFF)\r\n", val);
    PCMD_ReadReg(devAddr, 0x74, &val); printf("[0x74] ASI_OUT_EN  : 0x%02X (Expect 0xFF)\r\n", val);
    PCMD_ReadReg(devAddr, 0x75, &val); printf("[0x75] PWR_CFG     : 0x%02X (Expect 0x60)\r\n", val);
    
    printf("========================================\r\n\r\n");
}
