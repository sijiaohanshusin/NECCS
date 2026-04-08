/**
 * @file lv_port_indev_templ.c
 *
 */

#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev_template.h"
#include "lv_port_disp_template.h"
#include "../../lvgl.h"
#include "app_touch.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t *indev_touchpad;
lv_indev_t *indev_button;
static uint8_t s_touchpad_registered = 0u;
#define LV_PORT_TOUCH_MOVE_THRESHOLD       4

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    touchpad_init();

    if (s_touchpad_registered != 0u)
    {
        return;
    }

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv);
    indev_button = NULL;
    s_touchpad_registered = 1u;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void touchpad_init(void)
{
    if (App_Touch_IsReady() == 0u)
    {
        App_Touch_Init();
    }
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static uint8_t press_latched = 0u;
    const Touch_State_t *state;
    uint16_t panel_x0;
    uint16_t panel_y0;
    uint16_t raw_x;
    uint16_t raw_y;

    LV_UNUSED(indev_drv);

    if ((data == NULL) || (App_Touch_IsReady() == 0u))
    {
        if (data != NULL)
        {
            data->state = LV_INDEV_STATE_REL;
            data->point.x = last_x;
            data->point.y = last_y;
        }
        return;
    }

    state = App_Touch_GetState();
    if ((state == NULL) || (state->ready == 0u) || (state->pressed == 0u) || (state->count == 0u))
    {
        press_latched = 0u;
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    panel_x0 = lv_port_disp_get_origin_x();
    panel_y0 = lv_port_disp_get_origin_y();
    raw_x = state->x[0];
    raw_y = state->y[0];

    if ((raw_x < panel_x0) || (raw_x >= (uint16_t)(panel_x0 + LV_PORT_OVERLAY_W)) ||
        (raw_y < panel_y0) || (raw_y >= (uint16_t)(panel_y0 + LV_PORT_OVERLAY_H)))
    {
        press_latched = 0u;
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }

    raw_x = (uint16_t)(raw_x - panel_x0);
    raw_y = (uint16_t)(raw_y - panel_y0);

    if (press_latched == 0u)
    {
        last_x = (lv_coord_t)raw_x;
        last_y = (lv_coord_t)raw_y;
        press_latched = 1u;
    }
    else
    {
        int32_t dx = (int32_t)raw_x - (int32_t)last_x;
        int32_t dy = (int32_t)raw_y - (int32_t)last_y;

        if ((dx < -LV_PORT_TOUCH_MOVE_THRESHOLD) || (dx > LV_PORT_TOUCH_MOVE_THRESHOLD) ||
            (dy < -LV_PORT_TOUCH_MOVE_THRESHOLD) || (dy > LV_PORT_TOUCH_MOVE_THRESHOLD))
        {
            last_x = (lv_coord_t)raw_x;
            last_y = (lv_coord_t)raw_y;
        }
    }

    data->state = LV_INDEV_STATE_PR;
    data->point.x = last_x;
    data->point.y = last_y;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
