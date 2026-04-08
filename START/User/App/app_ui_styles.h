/**
 * @file    app_ui_styles.h
 * @brief   LVGL 工业深色主题 —— 样式定义与色彩方案
 * @details 统一定义 NECCS 声学相机的 UI 视觉语言：
 *          - 深蓝底色 + 青色高亮 + 橙色告警 + 绿色正常
 *          - 圆角 4px、1px 边框、半透明面板
 *          - 所有 LVGL 控件复用同一套样式表，保持一致性
 */
#ifndef __APP_UI_STYLES_H
#define __APP_UI_STYLES_H

#include "app_user_config.h"

#if (APP_LVGL_ENABLE != 0u)

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 色彩方案 (Color Palette)
 * ============================================================================ */

/** @brief 主背景色 —— 深蓝黑 */
#define UI_COLOR_BG_MAIN       lv_color_hex(0x1A1A2E)
/** @brief 面板/卡片背景色 —— 深灰蓝（半透明使用） */
#define UI_COLOR_BG_PANEL      lv_color_hex(0x16213E)
/** @brief 状态栏/工具栏背景色 */
#define UI_COLOR_BG_BAR        lv_color_hex(0x0F3460)
/** @brief 边框色 */
#define UI_COLOR_BORDER        lv_color_hex(0x2A3A5E)

/** @brief 主高亮色 —— 青色 */
#define UI_COLOR_ACCENT        lv_color_hex(0x00D4FF)
/** @brief 告警色 —— 橙色 */
#define UI_COLOR_WARNING       lv_color_hex(0xFF6600)
/** @brief 正常/确认色 —— 青绿 */
#define UI_COLOR_OK            lv_color_hex(0x00FF88)
/** @brief 错误色 —— 红色 */
#define UI_COLOR_ERROR         lv_color_hex(0xFF3333)
/** @brief 不活跃/禁用色 —— 暗灰 */
#define UI_COLOR_INACTIVE      lv_color_hex(0x555577)

/** @brief 主文字色 —— 亮白 */
#define UI_COLOR_TEXT_PRIMARY  lv_color_hex(0xE0E0F0)
/** @brief 次级文字色 —— 灰白 */
#define UI_COLOR_TEXT_SECONDARY lv_color_hex(0x8888AA)
/** @brief 高亮文字色 */
#define UI_COLOR_TEXT_ACCENT   lv_color_hex(0x00D4FF)

/** @brief chroma key 色 —— 洋红（与 lv_port_disp 一致） */
#define UI_COLOR_CHROMA_KEY    lv_color_hex(0xF81F)

/* ============================================================================
 * 布局常量 (Layout Constants)
 * ============================================================================ */

#define UI_RADIUS_DEFAULT      4u    /**< 默认圆角半径 (px) */
#define UI_BORDER_WIDTH        1u    /**< 默认边框宽度 (px) */
#define UI_PAD_SMALL           4u    /**< 小间距 (px) */
#define UI_PAD_NORMAL          8u    /**< 普通间距 (px) */
#define UI_PAD_LARGE           12u   /**< 大间距 (px) */
#define UI_STATUSBAR_H         28u   /**< 状态栏高度 (px) */
#define UI_TOOLBAR_H           32u   /**< 工具栏高度 (px) */

/* ============================================================================
 * 全局样式表 (Global Style Table)
 * ============================================================================ */

/** @brief 全局样式表结构体 */
typedef struct {
    lv_style_t scr_bg;       /**< 屏幕背景样式（深蓝黑底） */
    lv_style_t panel;        /**< 面板/卡片样式 */
    lv_style_t statusbar;    /**< 状态栏样式 */
    lv_style_t toolbar;      /**< 工具栏样式 */
    lv_style_t btn;          /**< 按钮默认样式 */
    lv_style_t btn_pressed;  /**< 按钮按下样式 */
    lv_style_t label_title;  /**< 标题文字样式 */
    lv_style_t label_value;  /**< 数值读数样式 */
    lv_style_t label_unit;   /**< 单位/辅助文字样式 */
    lv_style_t slider;       /**< 滑块样式 */
    lv_style_t chart;        /**< 图表样式 */
} App_UiStyleTable_t;

/** @brief 全局样式表单例（只读使用） */
extern App_UiStyleTable_t g_ui_styles;

/** @brief 初始化全局样式表，须在 lv_init() 之后调用 */
void App_UiStyles_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_ENABLE */

#endif /* __APP_UI_STYLES_H */
