#ifndef TOUCH_I2C_H
#define TOUCH_I2C_H

#include "main.h"

#define TOUCH_I2C_SCL_GPIO_PORT GPIOB
#define TOUCH_I2C_SCL_GPIO_PIN  GPIO_PIN_12

#define TOUCH_I2C_SDA_GPIO_PORT GPIOB
#define TOUCH_I2C_SDA_GPIO_PIN  GPIO_PIN_13

void Touch_I2C_Init(void);
void Touch_I2C_Start(void);
void Touch_I2C_Stop(void);
uint8_t Touch_I2C_WaitAck(void);
void Touch_I2C_SendByte(uint8_t data);
uint8_t Touch_I2C_ReadByte(uint8_t ack);

#endif /* TOUCH_I2C_H */
