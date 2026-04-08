/**
 * @file   app_lvgl_ui.h
 * @brief  LVGL 用户界面管理接口
 * @details 集中管理 LVGL UI 对象的创建、事件处理及叠加层控制。
 *          后续可替换为 GUI Guider 生成的 UI 代码。
 */
#ifndef APP_LVGL_UI_H
#define APP_LVGL_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 LVGL UI，创建所有界面控件和页面布局 */
void App_LvglUi_Init(void);

/** @brief 处理 LVGL UI 事件与动画更新，需在主循环或定时器中周期调用 */
void App_LvglUi_Process(void);

/**
 * @brief 启用或禁用 UI 叠加层（如声源热力图叠加在摄像头画面上）
 * @param enabled 1=启用叠加层，0=禁用
 */
void App_LvglUi_SetOverlayEnabled(uint8_t enabled);

/** @brief 将 LVGL 渲染缓冲区内容搬运到显示器（LCD 刷屏） */
void App_LvglUi_BlitToDisplay(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_UI_H */
