#include "app_touch_test.h"

#include "app_touch.h"
#include "app_user_config.h"
#include "LCD/lcd.h"
#include "touch_gt9xxx.h"

#include <stdio.h>

#if (APP_TOUCH_TEST_ENABLE != 0u)

static const uint32_t s_touch_test_colors[TOUCH_MAX_POINTS] = {
    RED, GREEN, BLUE, YELLOW, MAGENTA,
    CYAN, LIGHTBLUE, BRRED, BROWN, GRAY
};
static char s_touch_fail_msg[] = "TP init fail";
static char s_touch_idle_msg[] = "TP ready";

static uint16_t s_touch_last_x[TOUCH_MAX_POINTS];
static uint16_t s_touch_last_y[TOUCH_MAX_POINTS];
static uint8_t s_touch_last_valid[TOUCH_MAX_POINTS];

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
        uint16_t x = state->x[i];
        uint16_t y = state->y[i];

        if ((x >= lcddev.width) || (y >= lcddev.height))
        {
            continue;
        }

        if (s_touch_last_valid[i] != 0u)
        {
            lcd_draw_line(s_touch_last_x[i], s_touch_last_y[i], x, y, s_touch_test_colors[i]);
        }

        s_touch_test_draw_cross(x, y, s_touch_test_colors[i]);
        s_touch_last_x[i] = x;
        s_touch_last_y[i] = y;
        s_touch_last_valid[i] = 1u;
    }
}

#else

void App_TouchTest_Render(void)
{
}

#endif
