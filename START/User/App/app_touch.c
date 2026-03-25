#include "app_touch.h"

#include "app_user_config.h"
#include "LCD/lcd.h"

#include <stdio.h>

static uint8_t s_app_touch_ready = 0u;
static uint32_t s_app_touch_last_init_tick = 0u;

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

void App_Touch_Init(void)
{
#if (APP_TOUCH_ENABLE != 0u)
    s_app_touch_try_init();
#else
    s_app_touch_ready = 0u;
#endif
}

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

uint8_t App_Touch_IsReady(void)
{
    return s_app_touch_ready;
}

const Touch_State_t *App_Touch_GetState(void)
{
    return Touch_GetState();
}

const char *App_Touch_ControllerName(void)
{
    return Touch_ControllerName(Touch_GetState()->controller);
}
