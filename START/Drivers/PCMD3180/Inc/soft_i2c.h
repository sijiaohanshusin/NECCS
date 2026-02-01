#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "main.h" // 包含 HAL 库定义

// --- 引脚定义 (对应你的 PE2/PE3) ---
#define I2C_SCL_PIN   GPIO_PIN_2
#define I2C_SCL_PORT  GPIOE
#define I2C_SDA_PIN   GPIO_PIN_3
#define I2C_SDA_PORT  GPIOE

// --- PCMD3180 地址 (7-bit address << 1) ---
// 根据原理图 ADDR 引脚电平，可能是以下两个地址
#define PCMD3180_ADDR_1  0x98  // ADDR 接地
#define PCMD3180_ADDR_2  0x9A  // ADDR 接 VDD

// --- 函数声明 ---
void Soft_I2C_Init(void);
void PCMD3180_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data);
uint8_t PCMD3180_ReadReg(uint8_t devAddr, uint8_t regAddr);

// 核心功能：初始化所有 PCMD3180 为 TDM 16-bit 模式
void PCMD3180_Init_TDM_16Bit(void);

#endif