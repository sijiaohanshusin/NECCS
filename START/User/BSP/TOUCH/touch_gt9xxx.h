/**
 * @file    touch_gt9xxx.h
 * @brief   GT9XXX 系列电容触摸控制器驱动头文件
 * @details 定义 GT9XXX 触摸 IC 的 GPIO 引脚配置、I2C 命令字、
 *          寄存器地址以及初始化/扫描/调试接口。
 *          支持 GT911、GT9147、GT1151、GT1158、GT9271、GT967 等型号。
 *          适用于 STM32H743 平台。
 */

#ifndef TOUCH_GT9XXX_H
#define TOUCH_GT9XXX_H

#include "touch.h"

/** @name GT9XXX 复位引脚定义
 * @{
 */
#define TOUCH_GT9XXX_RST_GPIO_PORT GPIOB        /**< 复位引脚所在 GPIO 端口 */
#define TOUCH_GT9XXX_RST_GPIO_PIN  GPIO_PIN_14  /**< 复位引脚编号 */
/** @} */

/** @name GT9XXX 中断引脚定义
 * @{
 */
#define TOUCH_GT9XXX_INT_GPIO_PORT GPIOH       /**< 中断引脚所在 GPIO 端口 */
#define TOUCH_GT9XXX_INT_GPIO_PIN  GPIO_PIN_7  /**< 中断引脚编号 */
/** @} */

/** @name GT9XXX I2C 命令字
 * @{
 */
#define TOUCH_GT9XXX_CMD_WR 0x28u  /**< I2C 写命令地址 */
#define TOUCH_GT9XXX_CMD_RD 0x29u  /**< I2C 读命令地址 */
/** @} */

/** @name GT9XXX 寄存器地址
 * @{
 */
#define TOUCH_GT9XXX_CTRL_REG  0x8040u  /**< 控制寄存器 */
#define TOUCH_GT9XXX_PID_REG   0x8140u  /**< 产品 ID 寄存器 */
#define TOUCH_GT9XXX_GSTID_REG 0x814Eu  /**< 触摸状态寄存器 */
#define TOUCH_GT9XXX_TP1_REG   0x8150u  /**< 第一个触摸点数据寄存器 */
/** @} */

/**
 * @brief   初始化 GT9XXX 触摸控制器
 * @details 配置 RST/INT 引脚，复位芯片，读取产品 ID 并写入控制命令
 * @return  0: 成功, 1: 失败
 */
uint8_t Touch_GT9XXX_Init(void);

/**
 * @brief   扫描 GT9XXX 触摸数据
 * @param   state 指向触摸状态结构体的指针，用于输出触摸结果
 * @return  1: 有触摸按下, 0: 无触摸
 */
uint8_t Touch_GT9XXX_Scan(Touch_State_t *state);

/**
 * @brief   获取当前控制器支持的最大触摸点数
 * @return  最大触摸点数 (GT9271 为 10，其余为 5)
 */
uint8_t Touch_GT9XXX_GetMaxPoints(void);

/**
 * @brief   获取最近一次扫描的状态寄存器值 (调试用)
 * @return  GSTID 寄存器原始值
 */
uint8_t Touch_GT9XXX_DebugStatus(void);

/**
 * @brief   获取最近一次扫描的触摸点数 (调试用)
 * @return  触摸点数 (状态寄存器低 4 位)
 */
uint8_t Touch_GT9XXX_DebugPointNum(void);

/**
 * @brief   获取最近一次扫描的有效触摸点数 (调试用)
 * @return  映射成功的有效触摸点数
 */
uint8_t Touch_GT9XXX_DebugValidCount(void);

/**
 * @brief   获取最近一次按下事件的原始 X 坐标 (调试用)
 * @return  第一个触摸点的原始 X 值
 */
uint16_t Touch_GT9XXX_DebugRawX0(void);

/**
 * @brief   获取最近一次按下事件的原始 Y 坐标 (调试用)
 * @return  第一个触摸点的原始 Y 值
 */
uint16_t Touch_GT9XXX_DebugRawY0(void);

/**
 * @brief   获取最近一次按下事件的映射 X 坐标 (调试用)
 * @return  第一个触摸点映射后的 X 值
 */
uint16_t Touch_GT9XXX_DebugMapX0(void);

/**
 * @brief   获取最近一次按下事件的映射 Y 坐标 (调试用)
 * @return  第一个触摸点映射后的 Y 值
 */
uint16_t Touch_GT9XXX_DebugMapY0(void);

/**
 * @brief   获取累计扫描次数 (调试用)
 * @return  自初始化以来的扫描调用总次数
 */
uint32_t Touch_GT9XXX_DebugScanCount(void);

#endif /* TOUCH_GT9XXX_H */
