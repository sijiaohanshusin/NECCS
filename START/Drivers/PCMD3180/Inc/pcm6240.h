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

// --- Page 0: Control Registers ---
#define PCMD3180_REG_PAGE_CFG       0x0000 // 页选择寄存器 (所有页都有)
#define PCMD3180_REG_SW_RESET       0x0001 // 软件复位
#define PCMD3180_REG_PWR_CFG        0x0002 // 电源/休眠控制
#define PCMD3180_REG_ASI_CFG0       0x0007 // 音频接口格式 (TDM/I2S)
#define PCMD3180_REG_ASI_CFG1       0x0008 // 字长 (16/24/32 bits)

// 时钟配置 (PLL & Dividers)
#define PCMD3180_REG_MST_CFG0       0x0013 // Master Mode Config
#define PCMD3180_REG_MST_CFG1       0x0014
#define PCMD3180_REG_ASI_CLK_PR     0x0016 // ASI Clock source

// PDM & GPIO 配置
#define PCMD3180_REG_GPIO1_CFG      0x001F // GPIO1 功能选择
#define PCMD3180_REG_PDMCLK_CFG     0x001F // (通常复用)
#define PCMD3180_REG_IN_CH_EN       0x0073 // 输入通道使能 (Ch1-Ch4)
#define PCMD3180_REG_IN_CH_EN2      0x0074 // 输入通道使能 (Ch5-Ch8)
#define PCMD3180_REG_ASI_OUT_EN     0x0070 // ASI 输出通道使能 (Slot Enables)

// 数字音量控制 (Digital Volume) - Range: -100dB to +27dB
#define PCMD3180_REG_VOL_CH1        0x003E 
#define PCMD3180_REG_VOL_CH2        0x0043
#define PCMD3180_REG_VOL_CH3        0x0048
#define PCMD3180_REG_VOL_CH4        0x004D
#define PCMD3180_REG_VOL_CH5        0x0052
#define PCMD3180_REG_VOL_CH6        0x0057
#define PCMD3180_REG_VOL_CH7        0x005C
#define PCMD3180_REG_VOL_CH8        0x0061

/*
 * 硬件接口抽象结构体
 * 你需要实现底层的 I2C 写/读函数并赋值给这个结构体
 */
typedef struct {
    // 基础配置
    uint8_t i2c_addr;       // 设备地址 (0x4C or 0x4D)
    
    // 内部状态
    uint8_t current_page;   // 记录当前所在的 Page，避免重复写入
    
    // 硬件接口函数指针 (Porting Layer)
    // handle: 用户自定义的硬件句柄 (比如 I2C_HandleTypeDef*)
    // reg: 寄存器地址
    // val: 写入的值 / buf: 读取的缓冲区
    // len: 长度
    void *hw_handle; 
    int (*write)(void *handle, uint8_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len);
    int (*read) (void *handle, uint8_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len);

} pcmd3180_t;

/* --- API 函数声明 --- */

// 初始化结构体
void pcmd3180_struct_init(pcmd3180_t *dev, uint8_t addr, void *hw_handle,
                          int (*write_func)(void*, uint8_t, uint8_t, uint8_t*, uint16_t),
                          int (*read_func)(void*, uint8_t, uint8_t, uint8_t*, uint16_t));

// 寄存器读写 (自动处理 Page 切换)
int pcmd3180_reg_write(pcmd3180_t *dev, uint16_t reg_addr, uint8_t val);
int pcmd3180_reg_read(pcmd3180_t *dev, uint16_t reg_addr, uint8_t *val);

// 芯片初始化流程 (Reset -> Wake -> Config)
int pcmd3180_hw_init(pcmd3180_t *dev);

// 常用功能
int pcmd3180_set_volume(pcmd3180_t *dev, uint8_t channel, float db_val);

#endif