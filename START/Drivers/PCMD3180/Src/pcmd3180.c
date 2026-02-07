#include "pcmd3180.h"
#include "soft_i2c.h" // 引入你的 I2C 驱动，用于调用 PCMD_WriteReg
#include <stdio.h>

// ==========================================
// 内部辅助函数
// ==========================================

/**
 * @brief  写入寄存器的简单封装 (建议实现)
 * @note   这一层封装方便后续如果换硬件I2C，只需改这里
 */
static void Write_Reg(uint8_t devAddr, uint8_t reg, uint8_t val)
{
    // TODO: 调用 soft_i2c.c 中的 PCMD_WriteReg(devAddr, reg, val);
    PCMD_WriteReg(devAddr, reg, val);
}

// ==========================================
// 核心流程控制
// ==========================================

// 核心配置逻辑：Wakeup -> Wait -> Config (High Drive) -> Enable
uint8_t PCMD3180_Init_Device(uint8_t devAddr, uint8_t startSlot)
{
    // 0. 软复位
    PCMD_Ctrl_Reset(devAddr);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 1. 唤醒设备 (关键：写 0x81 到 Reg 0x02)
    PCMD_Ctrl_Sleep(devAddr, 0); 
    vTaskDelay(pdMS_TO_TICKS(10));

    // --- 配置阶段 ---
    // ASI 格式 (Offset=1, TDM Mode)
    PCMD_Config_ASI_Format(devAddr);
    // TDM 槽位 (Chip A=0, Chip B=8)
    PCMD_Config_TDM_Slots(devAddr, startSlot);
    
    // PDM 接口与驱动能力
    // 建议保留 0x41 (High Drive) 以获得最佳波形边沿
    PCMD_Config_PDM_IO(devAddr); 
    
    // DSP (HPF Enabled 96Hz)
    PCMD_Config_DSP(devAddr);
    // 输入源设为 PDM
    PCMD_Config_Channels(devAddr);
    // 时钟架构 (Slave Mode, Auto Clock)
    PCMD_Config_Clock_Mode(devAddr);

    // --- 启动阶段 ---
    // 全局使能 (PWR_CFG=0x60: Ext Mic Power, PLL ON)
    PCMD_Enable_Blocks(devAddr);

    return 0;
}


/**
 * @brief  检查设备 ID 或 I2C 通讯是否正常
 */
uint8_t PCMD3180_Check_ID(uint8_t devAddr)
{
    // TODO: 读取某个只读寄存器 (如 Page 0 Reg 0x00 应返回 0x00，或读取 Checksum)
    // 如果 I2C 读取失败，返回错误代码
    return 0;
}

// ==========================================
// 子功能模块实现
// ==========================================

void PCMD_Ctrl_Reset(uint8_t devAddr)
{
    Write_Reg(devAddr, PCMD_REG_SW_RESET, 0x01);
}

void PCMD_Ctrl_Sleep(uint8_t devAddr, uint8_t enable)
{
    if (enable==1 )
    {
        Write_Reg(devAddr, PCMD_REG_SLEEP_CFG, 0x80); // Sleep mode
    }
    else
    {
        Write_Reg(devAddr, PCMD_REG_SLEEP_CFG, 0x81); // Wake up
    }
}

void PCMD_Config_ASI_Format(uint8_t devAddr)
{
    //SAI_CFG0: 其他时隙保持输出高阻态
    Write_Reg(devAddr, PCMD_REG_ASI_CFG0, 0x01);
    //SAI_CFG1: ASI数据MSB偏移一个BCLK周期
    Write_Reg(devAddr, PCMD_REG_ASI_CFG1, 0x01);
    //SAI_CFG2
    Write_Reg(devAddr, PCMD_REG_ASI_CFG2, 0x00);
}

void PCMD_Config_TDM_Slots(uint8_t devAddr, uint8_t startSlot)
{
    // 将 Ch1 - Ch8 (Reg 0x0B - 0x12) 分别映射到 startSlot, startSlot+1 ...
    for (uint8_t i = 0; i < 8; i++)
    {
        Write_Reg(devAddr, PCMD_REG_ASI_CH1 + i, startSlot + i);
    }
}

// 配置时钟架构
void PCMD_Config_Clock_Mode(uint8_t devAddr)
{
    // 写入 0x00 即可完美适配
    PCMD_WriteReg(devAddr, PCMD_REG_MST_CFG0, 0x00);
    PCMD_WriteReg(devAddr, PCMD_REG_MST_CFG1, 0x00);
    //配置时钟输入引脚
    PCMD_WriteReg(devAddr, PCMD_REG_CLK_SRC, 0x00);
}

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
    // TODO: 3. (可选) 显式配置 GPI 映射 (如果默认映射不对)
    //GPO_VAL Register跳过
    //GPIO_MON Register跳过
    //GPI配置
    PCMD_WriteReg(devAddr, PCMD_REG_GPI_CFG0, 0x45);
    PCMD_WriteReg(devAddr, PCMD_REG_GPI_CFG1, 0x67);
}

void PCMD_Config_DSP(uint8_t devAddr)
{
    //启用 HPF (Reg 0x6B) -> 0x02 (96Hz for 48k)
    PCMD_WriteReg(devAddr, PCMD_REG_DSP_CFG0, 0x02);//可在图形界面配置
    // TODO: 2. (可选) 设置输入增益 (Volume)
    PCMD_WriteReg(devAddr, PCMD_REG_DSP_CFG1, 0x00);
}

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
    if (status_pwr != 0xE0) printf("[Error] Power Config Lost (Read: 0x%02X)!\r\n", status_pwr);
    // 3. (可选) 检查 GPI1 实时状态
    // 如果你在调试麦克风连接，可以取消注释下面这段
    
    uint8_t gpi_state;
    PCMD_ReadReg(devAddr, PCMD_REG_GPI_MON, &gpi_state);    // Reg 0x2F
    printf("GPI State: 0x%02X\r\n", gpi_state);
    
}


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
    PCMD_ReadReg(devAddr, 0x75, &val); printf("[0x75] PWR_CFG     : 0x%02X (Expect 0xE0)\r\n", val);
    
    printf("========================================\r\n\r\n");
}