/**
 * @file   app_touch.h
 * @brief  触摸屏应用层接口
 * @details 封装触摸控制器的初始化、轮询及状态查询，供 LVGL 输入驱动和
 *          触摸测试页面调用。
 */
#ifndef APP_TOUCH_H
#define APP_TOUCH_H

#include "touch.h"

/** @brief 初始化触摸控制器硬件（I2C 扫描 + 驱动探测） */
void App_Touch_Init(void);

/** @brief 轮询触摸控制器，更新内部触摸状态 */
void App_Touch_Poll(void);

/**
 * @brief  查询触摸控制器是否就绪
 * @return 1=已就绪，0=尚未初始化或探测失败
 */
uint8_t App_Touch_IsReady(void);

/**
 * @brief  获取当前触摸状态指针
 * @return 指向内部 Touch_State_t 的只读指针，包含触点坐标及按下标志
 */
const Touch_State_t *App_Touch_GetState(void);

/**
 * @brief  获取探测到的触摸控制器名称
 * @return 控制器名称常量字符串（如 "GT911"、"FT5x06"），未探测到则返回 "none"
 */
const char *App_Touch_ControllerName(void);

#endif /* APP_TOUCH_H */
