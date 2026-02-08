#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "main.h"

// 硬件引脚定义 (根据你的档案: PE2=SCL, PE3=SDA)
#define I2C_PORT        GPIOE
#define I2C_SCL_PIN     GPIO_PIN_2
#define I2C_SDA_PIN     GPIO_PIN_3


// 函数声明
void Soft_I2C_Init(void);
uint8_t PCMD_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data);
uint8_t PCMD_ReadReg(uint8_t devAddr, uint8_t regAddr, uint8_t *pData);
void I2C_Scan(void);

#endif
