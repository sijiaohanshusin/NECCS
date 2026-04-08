/**
 * @file    touch.h
 * @brief   触摸屏高层接口头文件
 * @details 提供触摸屏初始化、扫描、状态获取等统一接口，
 *          内部自动探测 GT9XXX / FT5206 触摸控制器并分发调用。
 *          适用于 STM32H743 平台。
 */

#ifndef TOUCH_H
#define TOUCH_H

#include "main.h"

/** @brief 支持的最大触摸点数 */
#define TOUCH_MAX_POINTS 10u

/**
 * @brief 触摸控制器类型枚举
 */
typedef enum
{
    TOUCH_CTRL_NONE   = 0u, /**< 未检测到控制器 */
    TOUCH_CTRL_GT9XXX = 1u, /**< GT9XXX 系列电容触摸控制器 */
    TOUCH_CTRL_FT5206 = 2u  /**< FT5206 电容触摸控制器 */
} Touch_Controller_t;

/**
 * @brief 触摸状态结构体
 */
typedef struct
{
    uint8_t  ready;                   /**< 触摸模块就绪标志 (1=就绪, 0=未就绪) */
    uint8_t  pressed;                 /**< 当前是否有触摸按下 (1=按下, 0=释放) */
    uint8_t  max_points;              /**< 当前控制器支持的最大触摸点数 */
    uint8_t  controller;              /**< 当前使用的控制器类型 @ref Touch_Controller_t */
    uint16_t count;                   /**< 本次扫描检测到的有效触摸点个数 */
    uint16_t active_mask;             /**< 有效触摸点位掩码，bit N=1 表示第 N 个点有效 */
    uint16_t x[TOUCH_MAX_POINTS];    /**< 各触摸点 X 坐标数组 */
    uint16_t y[TOUCH_MAX_POINTS];    /**< 各触摸点 Y 坐标数组 */
} Touch_State_t;

/**
 * @brief   初始化触摸屏模块
 * @details 自动探测 GT9XXX 和 FT5206 控制器，成功后标记就绪
 * @return  0: 成功, 1: 失败（未检测到控制器）
 */
uint8_t Touch_Init(void);

/**
 * @brief   扫描触摸屏，更新触摸状态
 * @details 根据已识别的控制器类型调用对应驱动的扫描函数
 * @return  1: 有触摸按下, 0: 无触摸或未就绪
 */
uint8_t Touch_Scan(void);

/**
 * @brief   获取当前触摸状态指针
 * @return  指向内部静态触摸状态结构体的只读指针
 */
const Touch_State_t *Touch_GetState(void);

/**
 * @brief   获取触摸控制器名称字符串
 * @param   controller 控制器类型 @ref Touch_Controller_t
 * @return  控制器名称的常量字符串指针
 */
const char *Touch_ControllerName(uint8_t controller);

#endif /* TOUCH_H */
