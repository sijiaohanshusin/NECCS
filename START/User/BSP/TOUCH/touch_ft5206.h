/**
 * @file    touch_ft5206.h
 * @brief   FT5206 电容触摸控制器驱动头文件
 * @details 定义 FT5206 触摸 IC 的 GPIO 引脚配置、I2C 命令字、
 *          寄存器地址以及初始化/扫描接口。适用于 STM32H743 平台。
 */

#ifndef TOUCH_FT5206_H
#define TOUCH_FT5206_H

#include "touch.h"

/** @name FT5206 复位引脚定义
 * @{
 */
#define TOUCH_FT5206_RST_GPIO_PORT GPIOB        /**< 复位引脚所在 GPIO 端口 */
#define TOUCH_FT5206_RST_GPIO_PIN  GPIO_PIN_14  /**< 复位引脚编号 */
/** @} */

/** @name FT5206 中断引脚定义
 * @{
 */
#define TOUCH_FT5206_INT_GPIO_PORT GPIOH       /**< 中断引脚所在 GPIO 端口 */
#define TOUCH_FT5206_INT_GPIO_PIN  GPIO_PIN_7  /**< 中断引脚编号 */
/** @} */

/** @name FT5206 I2C 命令字
 * @{
 */
#define TOUCH_FT5206_CMD_WR 0x70u  /**< I2C 写命令地址 */
#define TOUCH_FT5206_CMD_RD 0x71u  /**< I2C 读命令地址 */
/** @} */

/** @name FT5206 寄存器地址
 * @{
 */
#define TOUCH_FT5206_DEVIDE_MODE       0x00u  /**< 设备模式寄存器 */
#define TOUCH_FT5206_REG_NUM_FINGER    0x02u  /**< 触摸点数寄存器 */
#define TOUCH_FT5206_ID_G_LIB_VERSION  0xA1u  /**< 固件库版本寄存器 */
#define TOUCH_FT5206_ID_G_MODE         0xA4u  /**< 中断模式寄存器 */
#define TOUCH_FT5206_ID_G_THGROUP      0x80u  /**< 触摸阈值寄存器 */
#define TOUCH_FT5206_ID_G_PERIODACTIVE 0x88u  /**< 活动周期寄存器 */
/** @} */

/**
 * @brief   初始化 FT5206 触摸控制器
 * @details 配置 RST/INT 引脚，复位芯片，写入默认参数并校验固件版本
 * @return  0: 成功, 1: 失败
 */
uint8_t Touch_FT5206_Init(void);

/**
 * @brief   扫描 FT5206 触摸数据
 * @param   state 指向触摸状态结构体的指针，用于输出触摸结果
 * @return  1: 有触摸按下, 0: 无触摸
 */
uint8_t Touch_FT5206_Scan(Touch_State_t *state);

#endif /* TOUCH_FT5206_H */
