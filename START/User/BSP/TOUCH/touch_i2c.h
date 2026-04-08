/**
 * @file    touch_i2c.h
 * @brief   触摸屏 I2C 软件模拟（位操作）通信接口头文件
 * @details 使用 GPIO 位操作模拟 I2C 总线协议，用于与触摸控制器
 *          (FT5206 / GT9XXX) 通信。适用于 STM32H743 平台。
 */

#ifndef TOUCH_I2C_H
#define TOUCH_I2C_H

#include "main.h"

/** @name I2C SCL 引脚定义
 * @{
 */
#define TOUCH_I2C_SCL_GPIO_PORT GPIOB        /**< SCL 引脚所在 GPIO 端口 */
#define TOUCH_I2C_SCL_GPIO_PIN  GPIO_PIN_12  /**< SCL 引脚编号 */
/** @} */

/** @name I2C SDA 引脚定义
 * @{
 */
#define TOUCH_I2C_SDA_GPIO_PORT GPIOB        /**< SDA 引脚所在 GPIO 端口 */
#define TOUCH_I2C_SDA_GPIO_PIN  GPIO_PIN_13  /**< SDA 引脚编号 */
/** @} */

/**
 * @brief   初始化触摸屏 I2C 总线
 * @details 配置 SCL/SDA GPIO 引脚，仅在首次调用时执行初始化
 */
void Touch_I2C_Init(void);

/**
 * @brief   产生 I2C 起始信号
 */
void Touch_I2C_Start(void);

/**
 * @brief   产生 I2C 停止信号
 */
void Touch_I2C_Stop(void);

/**
 * @brief   等待从设备 ACK 应答
 * @return  0: 收到 ACK, 1: 超时未收到 ACK
 */
uint8_t Touch_I2C_WaitAck(void);

/**
 * @brief   发送一个字节数据
 * @param   data 待发送的字节，MSB 先发
 */
void Touch_I2C_SendByte(uint8_t data);

/**
 * @brief   读取一个字节数据
 * @param   ack 是否发送 ACK (1=发送 ACK, 0=发送 NACK)
 * @return  读取到的字节数据
 */
uint8_t Touch_I2C_ReadByte(uint8_t ack);

#endif /* TOUCH_I2C_H */
