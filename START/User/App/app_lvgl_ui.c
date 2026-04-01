/*
 * @Author: gxb 2562444672@qq.com
 * @Date: 2026-03-27 21:21:03
 * @LastEditors: gxb 2562444672@qq.com
 * @LastEditTime: 2026-04-01 17:25:35
 * @FilePath: \EmbeddedCompetition2c:\Users\GXB\Documents\New project\EmbeddedCompetition2_START\User\App\app_lvgl_ui.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "app_lvgl_ui.h"

#include "app_user_config.h"

#if (APP_LVGL_ENABLE != 0u)
#include "custom.h"
#include "gui_guider.h"
#include "lv_port_disp_template.h"
#include "lvgl/lvgl.h"

#include <stdint.h>
#include <string.h>

static uint8_t s_lvgl_ui_created = 0u;
static uint8_t s_overlay_enabled = 0u;

void App_LvglUi_Init(void)
{
#if (APP_LVGL_TEST_UI_ENABLE != 0u)
    if (s_lvgl_ui_created != 0u)
    {
        return;
    }

    (void)memset(&guider_ui, 0, sizeof(guider_ui));
    setup_ui(&guider_ui);
    custom_init(&guider_ui);
    s_lvgl_ui_created = 1u;
#endif
}

void App_LvglUi_Process(void)
{
#if (APP_LVGL_TEST_UI_ENABLE != 0u)
    if (s_lvgl_ui_created == 0u)
    {
        return;
    }
#endif
}

void App_LvglUi_SetOverlayEnabled(uint8_t enabled)
{
    s_overlay_enabled = (enabled != 0u) ? 1u : 0u;
}

void App_LvglUi_BlitToDisplay(void)
{
    if ((s_overlay_enabled == 0u) || (s_lvgl_ui_created == 0u))
    {
        return;
    }

    lv_port_disp_blit_to_display();
}

#else

void App_LvglUi_Init(void)
{
}

void App_LvglUi_Process(void)
{
}

void App_LvglUi_SetOverlayEnabled(uint8_t enabled)
{
    (void)enabled;
}

void App_LvglUi_BlitToDisplay(void)
{
}

#endif
