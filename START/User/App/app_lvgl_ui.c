/**
 * @file   app_lvgl_ui.c
 * @brief  LVGL 图形界面管理模块
 * @details 封装 LVGL UI 的创建、周期处理及叠加层输出。
 *          通过 APP_LVGL_ENABLE 和 APP_LVGL_TEST_UI_ENABLE 宏控制功能编译。
 * @author gxb 2562444672@qq.com
 * @date   2026-03-27
 */
#include "app_lvgl_ui.h"

#include "app_user_config.h"

#if (APP_LVGL_ENABLE != 0u)
#include "app_ui_screens.h"
#include "custom.h"
#include "gui_guider.h"
#include "lv_port_disp_template.h"
#include "lvgl/lvgl.h"

#include <stdint.h>
#include <string.h>

/** @brief UI 是否已创建 */
static uint8_t s_lvgl_ui_created = 0u;
/** @brief 叠加层是否启用 */
static uint8_t s_overlay_enabled = 0u;

/**
 * @brief  初始化 LVGL 用户界面
 * @details 通过 App_UiScreens_Init() 初始化多屏幕导航框架。
 */
void App_LvglUi_Init(void)
{
#if (APP_LVGL_TEST_UI_ENABLE != 0u)
    if (s_lvgl_ui_created != 0u)
    {
        return;
    }

    App_UiScreens_Init();
    s_lvgl_ui_created = 1u;
#endif
}

/**
 * @brief  LVGL UI 周期处理
 * @details 调用当前屏幕的 update 回调刷新实时数据。
 */
void App_LvglUi_Process(void)
{
#if (APP_LVGL_TEST_UI_ENABLE != 0u)
    if (s_lvgl_ui_created == 0u)
    {
        return;
    }
    App_UiScreens_Update();
#endif
}

/**
 * @brief  设置 LVGL 叠加层使能状态
 * @param  enabled 非零 = 启用叠加层，0 = 禁用
 */
void App_LvglUi_SetOverlayEnabled(uint8_t enabled)
{
    s_overlay_enabled = (enabled != 0u) ? 1u : 0u;
}

/**
 * @brief  将 LVGL 叠加层内容刷写到显示屏
 * @details 仅当叠加层已启用且 UI 已创建时执行刷写。
 */
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
