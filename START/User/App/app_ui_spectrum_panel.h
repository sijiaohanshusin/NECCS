/**
 * @file    app_ui_spectrum_panel.h
 * @brief   LVGL 频谱面板组件 —— 128-bin 频谱柱状图 + 频段选择 slider
 * @details 独立 LVGL 组件，可嵌入任意父容器。核心功能：
 *          - lv_canvas 逐帧绘制水平频谱柱状图（频率Y轴，幅度X轴）
 *          - 双 slider 选择活动频段（映射到 App_Spectrum_SetActiveBand）
 *          - 峰值频率标注、对数/线性刻度切换
 *          - 预设频段快捷按钮（全频/语音/超声/低频）
 */
#ifndef __APP_UI_SPECTRUM_PANEL_H
#define __APP_UI_SPECTRUM_PANEL_H

#include "app_user_config.h"

#if (APP_LVGL_ENABLE != 0u)

#include "app_types.h"
#include "lvgl/lvgl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 频段预设枚举 */
typedef enum {
    SPEC_PRESET_FULL   = 0u,  /**< 全频 (3-42, ~562-7875 Hz) */
    SPEC_PRESET_VOICE  = 1u,  /**< 语音 (2-18, ~375-3375 Hz) */
    SPEC_PRESET_ULTRA  = 2u,  /**< 超声 (54-128, ~10-24 kHz) */
    SPEC_PRESET_LOW    = 3u,  /**< 低频 (1-5, ~187-937 Hz) */
    SPEC_PRESET_COUNT  = 4u
} App_SpecPreset_t;

/**
 * @brief 创建频谱面板并嵌入父容器
 * @param parent   LVGL 父对象
 * @param width    面板宽度 (px)
 * @param height   面板高度 (px)
 * @return 面板根对象 (可用于布局)
 */
lv_obj_t *App_UiSpecPanel_Create(lv_obj_t *parent,
                                  lv_coord_t width,
                                  lv_coord_t height);

/**
 * @brief 每帧更新频谱面板数据
 * @param frame 最新频谱帧（NULL 时保持上一帧）
 */
void App_UiSpecPanel_Update(const App_SpectrumFrame_t *frame);

/**
 * @brief 应用频段预设
 * @param preset 预设枚举
 */
void App_UiSpecPanel_ApplyPreset(App_SpecPreset_t preset);

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_ENABLE */

#endif /* __APP_UI_SPECTRUM_PANEL_H */
