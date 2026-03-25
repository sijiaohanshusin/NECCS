#include "touch.h"

#include "touch_ft5206.h"
#include "touch_gt9xxx.h"
#include "LCD/lcd.h"

#include <stdio.h>
#include <string.h>

static Touch_State_t s_touch_state;

static void s_touch_clear_points(Touch_State_t *state)
{
    uint8_t i;

    state->pressed = 0u;
    state->count = 0u;
    state->active_mask = 0u;
    for (i = 0u; i < TOUCH_MAX_POINTS; i++)
    {
        state->x[i] = 0xFFFFu;
        state->y[i] = 0xFFFFu;
    }
}

static uint8_t s_touch_is_gt_panel(uint16_t panel_id)
{
    return (uint8_t)((panel_id == 0x4342u) ||
                     (panel_id == 0x4384u) ||
                     (panel_id == 0x5571u) ||
                     (panel_id == 0x8081u) ||
                     (panel_id == 0x1018u));
}

static uint8_t s_touch_is_ft_panel(uint16_t panel_id)
{
    return (uint8_t)((panel_id == 0x7084u) ||
                     (panel_id == 0x7016u));
}

static void s_touch_use_gt9xxx(void)
{
    s_touch_state.ready = 1u;
    s_touch_state.controller = TOUCH_CTRL_GT9XXX;
    s_touch_state.max_points = Touch_GT9XXX_GetMaxPoints();
}

static void s_touch_use_ft5206(void)
{
    s_touch_state.ready = 1u;
    s_touch_state.controller = TOUCH_CTRL_FT5206;
    s_touch_state.max_points = 5u;
}

uint8_t Touch_Init(void)
{
    uint16_t panel_id;

    memset(&s_touch_state, 0, sizeof(s_touch_state));
    s_touch_clear_points(&s_touch_state);
    panel_id = lcddev.id;

    if (s_touch_is_gt_panel(panel_id) != 0u)
    {
        if (Touch_GT9XXX_Init() == 0u)
        {
            s_touch_use_gt9xxx();
            return 0u;
        }
        printf("Touch GT probe failed on panel 0x%04X\r\n", panel_id);
    }

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

uint8_t Touch_Scan(void)
{
    if (s_touch_state.ready == 0u)
    {
        return 0u;
    }

    s_touch_clear_points(&s_touch_state);

    switch ((Touch_Controller_t)s_touch_state.controller)
    {
        case TOUCH_CTRL_GT9XXX:
            return Touch_GT9XXX_Scan(&s_touch_state);

        case TOUCH_CTRL_FT5206:
            return Touch_FT5206_Scan(&s_touch_state);

        case TOUCH_CTRL_NONE:
        default:
            return 0u;
    }
}

const Touch_State_t *Touch_GetState(void)
{
    return &s_touch_state;
}

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
