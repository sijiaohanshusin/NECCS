/**
 * @file    app_ui_styles.c
 * @brief   LVGL 工业深色主题 —— 样式初始化
 * @details 初始化 g_ui_styles 全局样式表，供所有 UI 组件复用。
 *          色彩方案参见 app_ui_styles.h。
 */
#include "app_ui_styles.h"

#if (APP_LVGL_ENABLE != 0u)

#include <string.h>

/** @brief 简体中文字体 — SimHei, ASCII + 126 CJK (lv_font_conv 生成) */
LV_FONT_DECLARE(lv_font_sc_14);
LV_FONT_DECLARE(lv_font_sc_12);

/** @brief 全局样式表实例 */
App_UiStyleTable_t g_ui_styles;

/**
 * @brief 初始化全局样式表
 */
void App_UiStyles_Init(void)
{
    (void)memset(&g_ui_styles, 0, sizeof(g_ui_styles));

    /* ----- 屏幕背景 ----- */
    lv_style_init(&g_ui_styles.scr_bg);
    lv_style_set_bg_color(&g_ui_styles.scr_bg, UI_COLOR_BG_MAIN);
    lv_style_set_bg_opa(&g_ui_styles.scr_bg, LV_OPA_COVER);
    lv_style_set_text_color(&g_ui_styles.scr_bg, UI_COLOR_TEXT_PRIMARY);

    /* ----- 面板/卡片 ----- */
    lv_style_init(&g_ui_styles.panel);
    lv_style_set_bg_color(&g_ui_styles.panel, UI_COLOR_BG_PANEL);
    lv_style_set_bg_opa(&g_ui_styles.panel, LV_OPA_90);
    lv_style_set_border_color(&g_ui_styles.panel, UI_COLOR_BORDER);
    lv_style_set_border_width(&g_ui_styles.panel, UI_BORDER_WIDTH);
    lv_style_set_radius(&g_ui_styles.panel, UI_RADIUS_DEFAULT);
    lv_style_set_pad_all(&g_ui_styles.panel, UI_PAD_SMALL);

    /* ----- 状态栏 ----- */
    lv_style_init(&g_ui_styles.statusbar);
    lv_style_set_bg_color(&g_ui_styles.statusbar, UI_COLOR_BG_BAR);
    lv_style_set_bg_opa(&g_ui_styles.statusbar, LV_OPA_COVER);
    lv_style_set_border_color(&g_ui_styles.statusbar, UI_COLOR_BORDER);
    lv_style_set_border_width(&g_ui_styles.statusbar, 0u);
    lv_style_set_border_side(&g_ui_styles.statusbar, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_radius(&g_ui_styles.statusbar, 0u);
    lv_style_set_pad_left(&g_ui_styles.statusbar, UI_PAD_NORMAL);
    lv_style_set_pad_right(&g_ui_styles.statusbar, UI_PAD_NORMAL);
    lv_style_set_pad_top(&g_ui_styles.statusbar, 2u);
    lv_style_set_pad_bottom(&g_ui_styles.statusbar, 2u);

    /* ----- 工具栏 ----- */
    lv_style_init(&g_ui_styles.toolbar);
    lv_style_set_bg_color(&g_ui_styles.toolbar, UI_COLOR_BG_BAR);
    lv_style_set_bg_opa(&g_ui_styles.toolbar, LV_OPA_COVER);
    lv_style_set_border_color(&g_ui_styles.toolbar, UI_COLOR_BORDER);
    lv_style_set_border_width(&g_ui_styles.toolbar, 0u);
    lv_style_set_border_side(&g_ui_styles.toolbar, LV_BORDER_SIDE_TOP);
    lv_style_set_radius(&g_ui_styles.toolbar, 0u);
    lv_style_set_pad_all(&g_ui_styles.toolbar, UI_PAD_SMALL);

    /* ----- 按钮默认 ----- */
    lv_style_init(&g_ui_styles.btn);
    lv_style_set_bg_color(&g_ui_styles.btn, UI_COLOR_BG_PANEL);
    lv_style_set_bg_opa(&g_ui_styles.btn, LV_OPA_COVER);
    lv_style_set_border_color(&g_ui_styles.btn, UI_COLOR_ACCENT);
    lv_style_set_border_width(&g_ui_styles.btn, UI_BORDER_WIDTH);
    lv_style_set_radius(&g_ui_styles.btn, UI_RADIUS_DEFAULT);
    lv_style_set_text_color(&g_ui_styles.btn, UI_COLOR_TEXT_PRIMARY);
    lv_style_set_text_font(&g_ui_styles.btn, &lv_font_sc_12);  /* CJK-capable font for button labels */
    lv_style_set_pad_ver(&g_ui_styles.btn, UI_PAD_SMALL);
    lv_style_set_pad_hor(&g_ui_styles.btn, UI_PAD_NORMAL);

    /* ----- 按钮按下 ----- */
    lv_style_init(&g_ui_styles.btn_pressed);
    lv_style_set_bg_color(&g_ui_styles.btn_pressed, UI_COLOR_ACCENT);
    lv_style_set_bg_opa(&g_ui_styles.btn_pressed, LV_OPA_80);
    lv_style_set_text_color(&g_ui_styles.btn_pressed, UI_COLOR_BG_MAIN);

    /* ----- 标题文字 (14px SimHei 含 CJK) ----- */
    lv_style_init(&g_ui_styles.label_title);
    lv_style_set_text_color(&g_ui_styles.label_title, UI_COLOR_TEXT_PRIMARY);
    lv_style_set_text_font(&g_ui_styles.label_title, &lv_font_sc_14);

    /* ----- 数值读数 (20px cyan) ----- */
    lv_style_init(&g_ui_styles.label_value);
    lv_style_set_text_color(&g_ui_styles.label_value, UI_COLOR_ACCENT);
    lv_style_set_text_font(&g_ui_styles.label_value, &lv_font_montserrat_20);

    /* ----- 大号数值 (28px cyan) —— 核心读数如 dB ----- */
    lv_style_init(&g_ui_styles.label_value_lg);
    lv_style_set_text_color(&g_ui_styles.label_value_lg, UI_COLOR_ACCENT);
    lv_style_set_text_font(&g_ui_styles.label_value_lg, &lv_font_montserrat_28);

    /* ----- 单位/辅助 (12px SimHei 含 CJK) ----- */
    lv_style_init(&g_ui_styles.label_unit);
    lv_style_set_text_color(&g_ui_styles.label_unit, UI_COLOR_TEXT_SECONDARY);
    lv_style_set_text_font(&g_ui_styles.label_unit, &lv_font_sc_12);

    /* ----- 小号注释 (12px SimHei dim) ----- */
    lv_style_init(&g_ui_styles.label_small);
    lv_style_set_text_color(&g_ui_styles.label_small, UI_COLOR_INACTIVE);
    lv_style_set_text_font(&g_ui_styles.label_small, &lv_font_sc_12);

    /* ----- 滑块 ----- */
    lv_style_init(&g_ui_styles.slider);
    lv_style_set_bg_color(&g_ui_styles.slider, UI_COLOR_BG_PANEL);
    lv_style_set_bg_opa(&g_ui_styles.slider, LV_OPA_COVER);
    lv_style_set_border_color(&g_ui_styles.slider, UI_COLOR_BORDER);
    lv_style_set_border_width(&g_ui_styles.slider, UI_BORDER_WIDTH);

    /* ----- 图表 ----- */
    lv_style_init(&g_ui_styles.chart);
    lv_style_set_bg_color(&g_ui_styles.chart, UI_COLOR_BG_MAIN);
    lv_style_set_bg_opa(&g_ui_styles.chart, LV_OPA_COVER);
    lv_style_set_border_color(&g_ui_styles.chart, UI_COLOR_BORDER);
    lv_style_set_border_width(&g_ui_styles.chart, UI_BORDER_WIDTH);
    lv_style_set_radius(&g_ui_styles.chart, UI_RADIUS_DEFAULT);
    lv_style_set_line_color(&g_ui_styles.chart, UI_COLOR_ACCENT);
    lv_style_set_line_width(&g_ui_styles.chart, 1u);

    /* ----- 状态指示器 亮 (绿色圆点) ----- */
    lv_style_init(&g_ui_styles.indicator_on);
    lv_style_set_bg_color(&g_ui_styles.indicator_on, UI_COLOR_OK);
    lv_style_set_bg_opa(&g_ui_styles.indicator_on, LV_OPA_COVER);
    lv_style_set_radius(&g_ui_styles.indicator_on, LV_RADIUS_CIRCLE);

    /* ----- 状态指示器 暗 (灰色圆点) ----- */
    lv_style_init(&g_ui_styles.indicator_off);
    lv_style_set_bg_color(&g_ui_styles.indicator_off, UI_COLOR_INACTIVE);
    lv_style_set_bg_opa(&g_ui_styles.indicator_off, LV_OPA_COVER);
    lv_style_set_radius(&g_ui_styles.indicator_off, LV_RADIUS_CIRCLE);
}

#endif /* APP_LVGL_ENABLE */
