#ifndef _PCMD3180_H_
#define _PCMD3180_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* * 调试/错误码定义 
 */
#define PCMD3180_OK         0
#define PCMD3180_ERR        -1

/* * I2C 地址 (7-bit 格式)
 * 如果你的 I2C 库需要 8-bit 地址 (包含读写位), 请自行 << 1
 */
#define PCMD3180_ADDR_0     0x4C // ADDR0=L, ADDR1=L
#define PCMD3180_ADDR_1     0x4D // ADDR0=H, ADDR1=L
#define PCMD3180_ADDR_2     0x4E // ADDR0=L, ADDR1=H
#define PCMD3180_ADDR_3     0x4F // ADDR0=H, ADDR1=H

/*
 * 寄存器定义
 * 格式: 0xPPRA (PP=Page, RA=Register Address)
 * PCMD3180 的所有配置几乎都在 Page 0，但系数配置在 Page 1-4
 */

// ==============================================================================
// Page 0: Control Registers (仅包含 8-bit 地址)
// 注意：使用前请确保 Page Register (0x00) 已被设为 0x00
// ==============================================================================

// --- 1. System & Reset (系统与复位) ---
#define PCMD_REG_PAGE_CFG       0x00  // [R/W] 页面选择寄存器 (切页必须要用它)
#define PCMD_REG_SW_RESET       0x01  // [W]   软件复位 (写入 0x01 复位)
#define PCMD_REG_SLEEP_CFG      0x02  // [R/W] 睡眠模式 (0x00=Active, 0x02=Sleep)
#define PCMD_REG_SHDN_CFG       0x05  // [R/W] 关断配置

// --- 2. ASI Configuration (TDM/I2S 音频格式配置) ---
#define PCMD_REG_ASI_CFG0       0x07  // [R/W] ASI格式 (TDM, 16/24/32bit)
#define PCMD_REG_ASI_CFG1       0x08  // [R/W] ASI偏移 (Offset)
#define PCMD_REG_ASI_CFG2       0x09  // [R/W] ASI其他配置

// --- 3. ASI Output Slots (TDM 时隙分配) ---
#define PCMD_REG_ASI_CH1        0x0B  // [R/W] Channel 1 输出 Slot
#define PCMD_REG_ASI_CH2        0x0C  // [R/W] Channel 2 输出 Slot
#define PCMD_REG_ASI_CH3        0x0D  // [R/W] Channel 3 输出 Slot
#define PCMD_REG_ASI_CH4        0x0E  // [R/W] Channel 4 输出 Slot
#define PCMD_REG_ASI_CH5        0x0F  // [R/W] Channel 5 输出 Slot
#define PCMD_REG_ASI_CH6        0x10  // [R/W] Channel 6 输出 Slot
#define PCMD_REG_ASI_CH7        0x11  // [R/W] Channel 7 输出 Slot
#define PCMD_REG_ASI_CH8        0x12  // [R/W] Channel 8 输出 Slot

// --- 4. Clock & Master Mode (时钟源与主从模式) ---
#define PCMD_REG_MST_CFG0       0x13  // [R/W] 主模式配置0 (BCLK/FSYNC方向)
#define PCMD_REG_MST_CFG1       0x14  // [R/W] 主模式配置1
#define PCMD_REG_ASI_STS        0x15  // [R]   ASI 总线时钟错误状态
#define PCMD_REG_CLK_SRC        0x16  // [R/W] 时钟源配置 (PLL/BCLK)

// --- 5. PDM Microphone Interface (PDM 接口配置) ---
#define PCMD_REG_PDMCLK_CFG     0x1F  // [R/W] PDM 时钟频率配置 (64x/128x)
#define PCMD_REG_PDMIN_CFG      0x20  // [R/W] PDM 采样沿配置

// --- 6. GPIO Configuration (引脚复用) ---
#define PCMD_REG_GPIO_CFG0      0x21  // [R/W] GPIO 全局配置
#define PCMD_REG_GPO_CFG0       0x22  // [R/W] GPO1 (设为 0x04 输出 PDM CLK)
#define PCMD_REG_GPO_CFG1       0x23  // [R/W] GPO2
#define PCMD_REG_GPO_CFG2       0x24  // [R/W] GPO3
#define PCMD_REG_GPO_CFG3       0x25  // [R/W] GPO4
#define PCMD_REG_GPO_VAL        0x29  // [R/W] GPO 输出值 (GPIO模式下)
#define PCMD_REG_GPIO_MON       0x2A  // [R]   GPIO 输入状态监控
#define PCMD_REG_GPI_CFG0       0x2B  // [R/W] GPI1/GPI2 配置
#define PCMD_REG_GPI_CFG1       0x2C  // [R/W] GPI3/GPI4 配置
#define PCMD_REG_GPI_MON        0x2F  // [R]   GPI 输入监控

// --- 7. Interrupts (中断) ---
#define PCMD_REG_INT_CFG        0x32  // [R/W] 中断配置
#define PCMD_REG_INT_MASK0      0x33  // [R/W] 中断屏蔽0
#define PCMD_REG_INT_LTCH0      0x36  // [R]   中断状态锁存读取

// --- 8. Analog & Bias (麦克风偏置与 VREF) ---
#define PCMD_REG_BIAS_CFG       0x3B  // [R/W] MICBIAS & VREF (充电时间)

// --- 9. Input Channel Configuration (输入通道增益与校准) ---
// Ch1
#define PCMD_REG_CH1_CFG0       0x3C  // [R/W] Vol Control / Impedance
#define PCMD_REG_CH1_CFG1       0x3D  // [R/W] Calibration (部分保留)
#define PCMD_REG_CH1_CFG2       0x3E  // [R/W] Digital Volume
#define PCMD_REG_CH1_CFG3       0x3F  // [R/W] Gain Calibration
#define PCMD_REG_CH1_CFG4       0x40  // [R/W] Phase Calibration
// Ch2
#define PCMD_REG_CH2_CFG0       0x41  //
#define PCMD_REG_CH2_CFG2       0x43
#define PCMD_REG_CH2_CFG3       0x44
#define PCMD_REG_CH2_CFG4       0x45
// Ch3
#define PCMD_REG_CH3_CFG0       0x46  //
#define PCMD_REG_CH3_CFG2       0x48
#define PCMD_REG_CH3_CFG3       0x49
#define PCMD_REG_CH3_CFG4       0x4A
// Ch4
#define PCMD_REG_CH4_CFG0       0x4B  //
#define PCMD_REG_CH4_CFG2       0x4D
#define PCMD_REG_CH4_CFG3       0x4E
#define PCMD_REG_CH4_CFG4       0x4F
// Ch5
#define PCMD_REG_CH5_CFG0       0x50  //
#define PCMD_REG_CH5_CFG2       0x52
#define PCMD_REG_CH5_CFG3       0x53
#define PCMD_REG_CH5_CFG4       0x54
// Ch6
#define PCMD_REG_CH6_CFG0       0x55  //
#define PCMD_REG_CH6_CFG2       0x57
#define PCMD_REG_CH6_CFG3       0x58
#define PCMD_REG_CH6_CFG4       0x59
// Ch7
#define PCMD_REG_CH7_CFG0       0x5A  //
#define PCMD_REG_CH7_CFG2       0x5C
#define PCMD_REG_CH7_CFG3       0x5D
#define PCMD_REG_CH7_CFG4       0x5E
// Ch8
#define PCMD_REG_CH8_CFG0       0x5F  //
#define PCMD_REG_CH8_CFG2       0x61
#define PCMD_REG_CH8_CFG3       0x62
#define PCMD_REG_CH8_CFG4       0x63

// --- 10. DSP & Signal Processing (信号处理) ---
#define PCMD_REG_DSP_CFG0       0x6B  // [R/W] HPF Enable (高通滤波)
#define PCMD_REG_DSP_CFG1       0x6C  // [R/W] DSP 扩展配置

// --- 11. Enable Controls (全局使能) ---
#define PCMD_REG_IN_CH_EN       0x73  // [R/W] 输入通道使能 (Input Ch Enable)
#define PCMD_REG_ASI_OUT_EN     0x74  // [R/W] ASI 输出通道使能
#define PCMD_REG_PWR_CFG        0x75  // [R/W] 上电配置 (PLL/ADC Power)

// --- 12. Device Status (状态查询) ---
#define PCMD_REG_DEV_STS0       0x76  // [R]   设备状态0
#define PCMD_REG_DEV_STS1       0x77  // [R]   设备状态1
#define PCMD_REG_I2C_CKSUM      0x7E  // [R/W] I2C 校验和

// ==========================================
// 核心 API (供应用层调用)
// ==========================================

// 初始化单个设备 (包含了复位、配置、唤醒的全流程)
uint8_t PCMD3180_Init_Device(uint8_t devAddr, uint8_t startSlot);

// 检查设备是否在线 (用于启动检测)
uint8_t PCMD3180_Check_ID(uint8_t devAddr);

// ==========================================
// 功能配置 API (通常由 Init 函数内部调用，也可单独调试)
// ==========================================

// 1. 软复位
void PCMD_Ctrl_Reset(uint8_t devAddr);

// 2. 睡眠/唤醒控制 (配置寄存器前必须睡，配完必须醒)
void PCMD_Ctrl_Sleep(uint8_t devAddr, uint8_t enable);

// 3. ASI 音频接口配置 (TDM, 16bit, Offset)
void PCMD_Config_ASI_Format(uint8_t devAddr);

// 4. 时隙分配 (最为关键，区分 Chip A 和 B)
void PCMD_Config_TDM_Slots(uint8_t devAddr, uint8_t startSlot);

// 5. PDM 接口与 GPIO 复用 (设置 64x OSR, GPO 输出 PDM CLK)
void PCMD_Config_PDM_IO(uint8_t devAddr);

// 6. 信号处理配置 (HPF, 增益等)
void PCMD_Config_DSP(uint8_t devAddr);

// 7. 输入通道配置 (设置为 PDM 输入)
void PCMD_Config_Channels(uint8_t devAddr);

// 8. 时钟架构配置 (主从模式, 时钟源等)
void PCMD_Config_Clock_Mode(uint8_t devAddr);

// 9. 启用必要模块 (输入通道, ASI 输出, 核心电源)
void PCMD_Enable_Blocks(uint8_t devAddr);


#endif
