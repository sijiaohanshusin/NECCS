#ifndef TOUCH_H
#define TOUCH_H

#include "main.h"

#define TOUCH_MAX_POINTS 10u

typedef enum
{
    TOUCH_CTRL_NONE = 0u,
    TOUCH_CTRL_GT9XXX = 1u,
    TOUCH_CTRL_FT5206 = 2u
} Touch_Controller_t;

typedef struct
{
    uint8_t ready;
    uint8_t pressed;
    uint8_t max_points;
    uint8_t controller;
    uint16_t count;
    uint16_t active_mask;
    uint16_t x[TOUCH_MAX_POINTS];
    uint16_t y[TOUCH_MAX_POINTS];
} Touch_State_t;

uint8_t Touch_Init(void);
uint8_t Touch_Scan(void);
const Touch_State_t *Touch_GetState(void);
const char *Touch_ControllerName(uint8_t controller);

#endif /* TOUCH_H */
