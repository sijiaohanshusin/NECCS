/**
 * @file   app_touch.c
 * @brief  触摸屏轮询驱动模块
 * @details 封装触摸屏初始化与周期轮询逻辑，支持自动重试初始化。
 *          通过 APP_TOUCH_ENABLE 宏控制是否编译触摸功能。
 */

#include "app_touch.h"

#include "app_user_config.h"
#include "LCD/lcd.h"

#include <stdio.h>

/** @brief 触摸屏是否已成功初始化 */
static uint8_t s_app_touch_ready = 0u;
/** @brief 上次尝试初始化的系统时刻 (ms) */
static uint32_t s_app_touch_last_init_tick = 0u;

/** @brief 尝试初始化触摸屏，成功时设置就绪标志 */
static void s_app_touch_try_init(void)
{
    s_app_touch_last_init_tick = HAL_GetTick();

    if (Touch_Init() == 0u)
    {
        s_app_touch_ready = 1u;
        printf("Touch init OK (%s, LCD=0x%04X)\r\n",
               App_Touch_ControllerName(),
               lcddev.id);
    }
    else
    {
        s_app_touch_ready = 0u;
        printf("Touch init FAIL (LCD=0x%04X)\r\n", lcddev.id);
    }
}

/**
 * @brief  初始化触摸屏模块
 * @details 若 APP_TOUCH_ENABLE 为 0 则跳过初始化。
 */
void App_Touch_Init(void)
{
#if (APP_TOUCH_ENABLE != 0u)
    s_app_touch_try_init();
#else
    s_app_touch_ready = 0u;
#endif
}

/**
 * @brief  触摸屏周期轮询
 * @details 若尚未就绪，按 APP_TOUCH_RETRY_MS 间隔重试初始化；
 *          就绪后执行一次扫描。
 */
void App_Touch_Poll(void)
{
#if (APP_TOUCH_ENABLE != 0u)
    if (s_app_touch_ready == 0u)
    {
        if ((uint32_t)(HAL_GetTick() - s_app_touch_last_init_tick) >= APP_TOUCH_RETRY_MS)
        {
            s_app_touch_try_init();
        }
        return;
    }

    (void)Touch_Scan();
#endif
}

/**
 * @brief  查询触摸屏是否就绪
 * @return 1 = 已就绪，0 = 未就绪
 */
uint8_t App_Touch_IsReady(void)
{
    return s_app_touch_ready;
}

/**
 * @brief  获取当前触摸状态
 * @return 指向触摸状态结构体的指针
 */
const Touch_State_t *App_Touch_GetState(void)
{
    return Touch_GetState();
}

/**
 * @brief  获取触摸控制器名称字符串
 * @return 控制器名称的只读字符串指针
 */
const char *App_Touch_ControllerName(void)
{
    return Touch_ControllerName(Touch_GetState()->controller);
}
