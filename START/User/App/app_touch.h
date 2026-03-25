#ifndef APP_TOUCH_H
#define APP_TOUCH_H

#include "touch.h"

void App_Touch_Init(void);
void App_Touch_Poll(void);
uint8_t App_Touch_IsReady(void);
const Touch_State_t *App_Touch_GetState(void);
const char *App_Touch_ControllerName(void);

#endif /* APP_TOUCH_H */
