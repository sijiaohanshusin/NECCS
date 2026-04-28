/**
 * @file   app_touch_test.c
 * @brief  触摸屏测试可视化模块
 * @details 在 LCD 上绘制多点触摸十字标记与连线，用于触摸功能验证。
 *          通过 APP_TOUCH_TEST_ENABLE 宏控制是否编译。
 */

#include "app_touch_test.h"

#include "app_touch.h"
#include "app_user_config.h"
#include "LCD/lcd.h"
#include "touch_gt9xxx.h"

#include <stdio.h>

#if (APP_TOUCH_TEST_ENABLE != 0u)

/** @brief 各触摸点对应的绘制颜色 */
static const uint32_t s_touch_test_colors[TOUCH_MAX_POINTS] = {
    RED, GREEN, BLUE, YELLOW, MAGENTA,
    CYAN, LIGHTBLUE, BRRED, BROWN, GRAY
};
/** @brief 触摸初始化失败时显示的提示文本 */
static char s_touch_fail_msg[] = "TP init fail";
/** @brief 触摸就绪时显示的提示文本 */
static char s_touch_idle_msg[] = "TP ready";

/** @brief 各触摸点上一次的 X 坐标 */
static uint16_t s_touch_last_x[TOUCH_MAX_POINTS];
/** @brief 各触摸点上一次的 Y 坐标 */
static uint16_t s_touch_last_y[TOUCH_MAX_POINTS];
/** @brief 各触摸点上一次坐标是否有效 */
static uint8_t s_touch_last_valid[TOUCH_MAX_POINTS];

/**
 * @brief 在指定位置绘制十字标记（含圆圈）
 * @param x     中心 X 坐标
 * @param y     中心 Y 坐标
 * @param color 绘制颜色
 */
static void s_touch_test_draw_cross(uint16_t x, uint16_t y, uint32_t color)
{
    uint16_t x0 = (x > 8u) ? (uint16_t)(x - 8u) : 0u;
    uint16_t y0 = (y > 8u) ? (uint16_t)(y - 8u) : 0u;
    uint16_t x1 = (uint16_t)(((uint32_t)x + 8u < lcddev.width) ? (x + 8u) : (lcddev.width - 1u));
    uint16_t y1 = (uint16_t)(((uint32_t)y + 8u < lcddev.height) ? (y + 8u) : (lcddev.height - 1u));

    lcd_draw_line(x0, y, x1, y, color);
    lcd_draw_line(x, y0, x, y1, color);
    lcd_draw_circle(x, y, 5u, color);
}

/**
 * @brief  渲染触摸测试界面
 * @details 显示触摸控制器状态信息，并为每个触摸点绘制十字标记和连线轨迹。
 */
void App_TouchTest_Render(void)
{
    const Touch_State_t *state;
    char line[48];
    uint8_t i;

    if (App_Touch_IsReady() == 0u)
    {
        lcd_fill(4u, 4u, 250u, 44u, WHITE);
        lcd_show_string(8u, 8u, 236u, 16u, 16u, s_touch_fail_msg, RED);
        return;
    }

    state = App_Touch_GetState();
    lcd_fill(4u, 4u, 250u, 44u, WHITE);
    (void)snprintf(line, sizeof(line), "%s %s", s_touch_idle_msg, App_Touch_ControllerName());
    lcd_show_string(8u, 8u, 236u, 16u, 16u, line, BLACK);

    if ((state == NULL) || (state->pressed == 0u) || (state->count == 0u))
    {
        if ((state != NULL) && (state->controller == TOUCH_CTRL_GT9XXX))
        {
            (void)snprintf(line,
                           sizeof(line),
                           "S:%02X P:%u V:%u",
                           (unsigned int)Touch_GT9XXX_DebugStatus(),
                           (unsigned int)Touch_GT9XXX_DebugPointNum(),
                           (unsigned int)Touch_GT9XXX_DebugValidCount());
            lcd_show_string(8u, 24u, 236u, 16u, 16u, line, BLUE);
        }
        for (i = 0u; i < TOUCH_MAX_POINTS; i++)
        {
            s_touch_last_valid[i] = 0u;
        }
        return;
    }

    (void)snprintf(line, sizeof(line), "TP %s %u", App_Touch_ControllerName(), (unsigned int)state->count);
    lcd_show_string(8u, 8u, 236u, 16u, 16u, line, BLUE);
    (void)snprintf(line,
                   sizeof(line),
                   "X:%u Y:%u",
                   (unsigned int)state->x[0],
                   (unsigned int)state->y[0]);
    lcd_show_string(8u, 24u, 236u, 16u, 16u, line, BLACK);

    for (i = 0u; (i < state->count) && (i < TOUCH_MAX_POINTS); i++)
    {
        uint16_t x = state->x[i];   /* 当前触摸点 X 坐标（像素） */
        uint16_t y = state->y[i];   /* 当前触摸点 Y 坐标（像素） */

        /* 越界检查：LCD 尺寸由 lcddev.width/height 描述（通常 1024×600），跳过越界点 */
        if ((x >= lcddev.width) || (y >= lcddev.height))
        {
            continue;
        }

        if (s_touch_last_valid[i] != 0u)
        {
            /* 从上一帧位置到本帧位置绘制连线，形成触摸轨迹效果 */
            lcd_draw_line(s_touch_last_x[i], s_touch_last_y[i], x, y, s_touch_test_colors[i]);
        }

        /* 在当前触摸坐标绘制十字标记（含圆圈），每个手指颜色不同 */
        s_touch_test_draw_cross(x, y, s_touch_test_colors[i]);
        /* 更新本触摸点的历史坐标，供下一帧绘制连线使用 */
        s_touch_last_x[i] = x;
        s_touch_last_y[i] = y;
        s_touch_last_valid[i] = 1u;   /* 标记本点历史坐标有效 */
    }
}

#else

void App_TouchTest_Render(void)
{
}

#endif
