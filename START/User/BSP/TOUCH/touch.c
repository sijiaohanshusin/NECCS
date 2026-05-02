/**
 * @file    touch.c
 * @brief   触摸屏高层接口实现
 * @details 实现触摸屏的统一初始化与扫描流程，
 *          根据 LCD 面板 ID 自动探测并选择 GT9XXX 或 FT5206 驱动。
 *          设计目标：上层只调用 Touch_Init/Touch_Scan，不感知底层控制器差异。
 *          探测策略：先按面板白名单优先探测，再执行兜底探测，提升跨批次兼容性。
 *
 *          当前探测顺序：
 *          1) 若 panel_id 属于 GT 面板白名单：优先 GT9XXX
 *          2) 若 panel_id 属于 FT 面板白名单：优先 FT5206，再尝试 GT9XXX
 *          3) 最终兜底：GT9XXX -> FT5206
 *
 * @note    [改进] 当前探测顺序硬编码在 if 分支中，后续可改为 probe table
 *          （控制器类型 + init 函数指针 + 适配面板列表）以便扩展新触控 IC。
 *          适用于 STM32H743 平台。
 */

#include "touch.h"

#include "touch_ft5206.h"
#include "touch_gt9xxx.h"
#include "LCD/lcd.h"

#include <stdio.h>
#include <string.h>

/** @brief 内部触摸状态实例 */
static Touch_State_t s_touch_state;

/**
 * @brief   清除触摸点数据
 * @param   state 指向触摸状态结构体的指针
 */
static void s_touch_clear_points(Touch_State_t *state)
{
    uint8_t i;

    state->pressed = 0u;      /* 默认无按下 */
    state->count = 0u;        /* 有效触点数清零 */
    state->active_mask = 0u;  /* 触点位掩码清零 */
    for (i = 0u; i < TOUCH_MAX_POINTS; i++)
    {
        state->x[i] = 0xFFFFu; /* 用 0xFFFF 作为无效坐标哨兵值 */
        state->y[i] = 0xFFFFu;
    }
}

/**
 * @brief   判断面板ID是否属于使用 GT 系列触摸控制器的面板
 * @param   panel_id LCD 面板 ID
 * @return  1: 是 GT 面板, 0: 不是
 */
static uint8_t s_touch_is_gt_panel(uint16_t panel_id)
{
    return (uint8_t)((panel_id == 0x4342u) ||
                     (panel_id == 0x4384u) ||
                     (panel_id == 0x7016u) ||
                     (panel_id == 0x5571u) ||
                     (panel_id == 0x8081u) ||
                     (panel_id == 0x1018u));
}

/**
 * @brief   判断面板ID是否属于使用 FT 系列触摸控制器的面板
 * @param   panel_id LCD 面板 ID
 * @return  1: 是 FT 面板, 0: 不是
 */
static uint8_t s_touch_is_ft_panel(uint16_t panel_id)
{
    return (uint8_t)(panel_id == 0x7084u);
}

/**
 * @brief 配置触摸状态为使用 GT9XXX 控制器
 */
static void s_touch_use_gt9xxx(void)
{
    s_touch_state.ready = 1u;
    s_touch_state.controller = TOUCH_CTRL_GT9XXX;
    s_touch_state.max_points = Touch_GT9XXX_GetMaxPoints();
}

/**
 * @brief 配置触摸状态为使用 FT5206 控制器
 */
static void s_touch_use_ft5206(void)
{
    s_touch_state.ready = 1u;
    s_touch_state.controller = TOUCH_CTRL_FT5206;
    s_touch_state.max_points = 5u;
}

/**
 * @brief   初始化触摸屏模块
 * @details 根据 LCD 面板 ID 依次尝试 GT9XXX 和 FT5206 驱动探测
 * @return  0: 成功, 1: 失败
 */
uint8_t Touch_Init(void)
{
    uint16_t panel_id;

    memset(&s_touch_state, 0, sizeof(s_touch_state)); /* 清空内部状态结构 */
    s_touch_clear_points(&s_touch_state);             /* 初始化坐标哨兵值与掩码 */
    panel_id = lcddev.id;                             /* 读取 LCD 模块识别到的面板 ID */

    /* 路径1：GT 面板白名单优先探测 GT9XXX。 */
    if (s_touch_is_gt_panel(panel_id) != 0u)
    {
        if (Touch_GT9XXX_Init() == 0u)
        {
            s_touch_use_gt9xxx();
            return 0u;
        }
        /* [注意] 该日志仅在初始化阶段打印，不在高频扫描路径中。 */
        printf("Touch GT probe failed on panel 0x%04X\r\n", panel_id);
    }

    /* 路径2：FT 面板白名单优先 FT5206，失败后退到 GT9XXX。 */
    if (s_touch_is_ft_panel(panel_id) != 0u)
    {
        if (Touch_FT5206_Init() == 0u)
        {
            s_touch_use_ft5206();
            return 0u;
        }

        if (Touch_GT9XXX_Init() == 0u)
        {
            s_touch_use_gt9xxx();
            return 0u;
        }
    }

    /* 路径3：通用兜底探测，覆盖面板 ID 异常或新批次兼容场景。 */
    if (Touch_GT9XXX_Init() == 0u)
    {
        s_touch_use_gt9xxx();
        return 0u;
    }

    if (Touch_FT5206_Init() == 0u)
    {
        s_touch_use_ft5206();
        return 0u;
    }

    s_touch_state.ready = 0u;
    s_touch_state.controller = TOUCH_CTRL_NONE;
    s_touch_state.max_points = 0u;
    return 1u;
}

/**
 * @brief   扫描触摸屏并更新内部状态
 * @return  1: 有触摸, 0: 无触摸或未就绪
 */
uint8_t Touch_Scan(void)
{
    if (s_touch_state.ready == 0u)
    {
        return 0u;
    }

    /* 每次扫描先清空上一帧触点，避免控制器读失败时残留旧坐标。 */
    s_touch_clear_points(&s_touch_state);

    switch ((Touch_Controller_t)s_touch_state.controller)
    {
        case TOUCH_CTRL_GT9XXX:
            return Touch_GT9XXX_Scan(&s_touch_state);

        case TOUCH_CTRL_FT5206:
            return Touch_FT5206_Scan(&s_touch_state);

        case TOUCH_CTRL_NONE:
        default:
            /* 控制器未就绪或枚举值异常，按无触摸处理。 */
            return 0u;
    }
}

/**
 * @brief   获取当前触摸状态
 * @return  指向内部触摸状态结构体的只读指针
 */
const Touch_State_t *Touch_GetState(void)
{
    return &s_touch_state;
}

/**
 * @brief   获取控制器名称字符串
 * @param   controller 控制器类型 @ref Touch_Controller_t
 * @return  控制器名称常量字符串
 */
const char *Touch_ControllerName(uint8_t controller)
{
    switch ((Touch_Controller_t)controller)
    {
        case TOUCH_CTRL_GT9XXX:
            return "GT9XXX";

        case TOUCH_CTRL_FT5206:
            return "FT5206";

        case TOUCH_CTRL_NONE:
        default:
            return "NONE";
    }
}
