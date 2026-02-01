#include "pcmd3180.h"
#include "soft_i2c.h" // 引入你的 I2C 驱动，用于调用 PCMD_WriteReg

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
}

// ==========================================
// 核心流程控制
// ==========================================

/**
 * @brief  初始化单个 PCMD3180 芯片
 * @param  devAddr: 芯片地址 (0x4C 或 0x4D)
 * @param  startSlot: TDM 起始槽位 (Chip A=0, Chip B=8)
 * @return 0=成功, 1=失败
 */
uint8_t PCMD3180_Init_Device(uint8_t devAddr, uint8_t startSlot)
{
    // TODO: 1. 执行软复位 (PCMD_Ctrl_Reset)
    
    // TODO: 2. 必须有延时 (HAL_Delay 10ms) 等待复位完成

    // TODO: 3. 进入睡眠模式 (PCMD_Ctrl_Sleep enable=1)
    
    // TODO: 4. 配置 ASI 格式 (PCMD_Config_ASI_Format)
    
    // TODO: 5. 配置 TDM 槽位 (PCMD_Config_TDM_Slots)
    
    // TODO: 6. 配置 PDM 接口与引脚 (PCMD_Config_PDM_IO)
    
    // TODO: 7. 配置 DSP/HPF (PCMD_Config_DSP)

    // TODO: 8. 唤醒设备 (PCMD_Ctrl_Sleep enable=0)
    
    // TODO: 9. (可选) 检查 PLL 是否锁定或 ASI 总线错误
    
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
    // TODO: 写入 SW_RESET (Reg 0x01) = 0x01
}

void PCMD_Ctrl_Sleep(uint8_t devAddr, uint8_t enable)
{
    // TODO: 如果 enable==1, SLEEP_CFG (Reg 0x02) = 0x02
    // TODO: 如果 enable==0, SLEEP_CFG (Reg 0x02) = 0x00 (Wake up)
}

void PCMD_Config_ASI_Format(uint8_t devAddr)
{
    // TODO: 配置 Reg 0x07 (TDM mode, 16-bit) -> 建议 0x81
    // TODO: 配置 Reg 0x08 (Tx Offset = 1) -> 建议 0x01
}

void PCMD_Config_TDM_Slots(uint8_t devAddr, uint8_t startSlot)
{
    // TODO: 这是一个循环或连续写入
    // 将 Ch1 - Ch8 (Reg 0x0B - 0x12) 分别映射到 startSlot, startSlot+1 ...
    
    /* 伪代码提示:
    for (int i = 0; i < 8; i++) {
        Write_Reg(devAddr, PCMD_REG_ASI_CH1 + i, startSlot + i);
    }
    */
}

void PCMD_Config_PDM_IO(uint8_t devAddr)
{
    // TODO: 1. 设置 PDMCLK_CFG (Reg 0x1F) -> 64x OSR (0x01)
    
    // TODO: 2. 配置 GPIO 复用
    // GPO1-4 (Reg 0x22-0x25) 全部设为 PDM CLK Output (通常是 0x04)
    
    // TODO: 3. (可选) 显式配置 GPI 映射 (如果默认映射不对)
}

void PCMD_Config_DSP(uint8_t devAddr)
{
    // TODO: 1. 启用 HPF (Reg 0x6B) -> 0x02 (96Hz for 48k)
    
    // TODO: 2. (可选) 设置输入增益 (Volume)
}