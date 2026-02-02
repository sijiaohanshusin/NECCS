#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "main.h"

// 硬件引脚定义 (根据你的档案: PE2=SCL, PE3=SDA)
#define I2C_PORT        GPIOE
#define I2C_SCL_PIN     GPIO_PIN_2
#define I2C_SDA_PIN     GPIO_PIN_3

// PCMD3180 7-bit 地址 (手册 Table 43)
// 芯片A (ADDR0=0, ADDR1=0) -> 0x4C (二进制 1001 100)
// 芯片B (ADDR0=1, ADDR1=0) -> 0x4D (二进制 1001 101)
#define PCMD_ADDR_A     0x4C
#define PCMD_ADDR_B     0x4D

// 函数声明
void Soft_I2C_Init(void);
uint8_t PCMD_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data);
uint8_t PCMD_ReadReg(uint8_t devAddr, uint8_t regAddr, uint8_t *pData);

#endif
