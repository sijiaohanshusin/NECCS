#ifndef APP_LVGL_UI_H
#define APP_LVGL_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep user LVGL object creation in one place.
 * Later you can replace this file's implementation with Guider-generated UI.
 */
void App_LvglUi_Init(void);
void App_LvglUi_Process(void);
void App_LvglUi_SetOverlayEnabled(uint8_t enabled);
void App_LvglUi_BlitToDisplay(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_UI_H */
