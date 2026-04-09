/**
 * @file    app_ui_screens.c
 * @brief   LVGL 多屏幕导航框架 —— 屏幕注册与切换实现
 * @details 管理所有 LVGL 屏幕的生命周期：懒加载创建、切换、周期更新。
 *          Home 屏幕沿用 GUI Guider 生成代码，Main/Settings/Capture/Diag
 *          屏幕由本项目实现。
 */
#include "app_ui_screens.h"

#if (APP_LVGL_ENABLE != 0u)

#include "app_ui_styles.h"
#include "app_ui_spectrum_panel.h"
#include "app_user_config.h"
#include "app_runtime.h"
#include "app_spectrum.h"
#include "app_perf.h"
#include "app_laser.h"
#include "gui_guider.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"

#include "app_trigger.h"
#include "app_noise_floor.h"

#include "ai_beamsteer.h"
#include "app_capture.h"
#include "app_recorder.h"
#include "app_sd.h"
#include "app_storage_task.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 前向声明 —— 各屏幕的 create / update / destroy 回调
 * ============================================================================ */

/* Home 屏幕由 GUI Guider 创建，这里仅提供 wrapper */
static lv_obj_t *s_home_create(void);
static void      s_home_update(void);
static void      s_home_btn_start_cb(lv_event_t *e);
static void      s_home_btn_settings_cb(lv_event_t *e);
static void      s_home_cont_click_cb(lv_event_t *e);

/* Main View —— Phase 0.4 实现 */
static lv_obj_t *s_main_create(void);
static void      s_main_update(void);
static void      s_main_btn_settings_cb(lv_event_t *e);
static void      s_main_btn_home_cb(lv_event_t *e);
static void      s_main_btn_diag_cb(lv_event_t *e);
static void      s_main_btn_capture_cb(lv_event_t *e);
static void      s_main_btn_trigger_cb(lv_event_t *e);
static void      s_main_btn_laser_cb(lv_event_t *e);

/* Settings —— Phase 1.5 实现（占位） */
static lv_obj_t *s_settings_create(void);
static void      s_settings_update(void);
static void      s_settings_btn_back_cb(lv_event_t *e);

/* Capture —— 数据捕获屏幕 */
static lv_obj_t *s_capture_create(void);
static void      s_capture_update(void);
static void      s_capture_btn_back_cb(lv_event_t *e);
static void      s_capture_btn_screenshot_cb(lv_event_t *e);
static void      s_capture_btn_record_cb(lv_event_t *e);
static void      s_capture_dd_mode_cb(lv_event_t *e);
static void      s_capture_sw_beamsteer_cb(lv_event_t *e);
static void      s_capture_dd_beam_mode_cb(lv_event_t *e);

/* Diagnostics —— Phase 1.6 实现（占位） */
static lv_obj_t *s_diag_create(void);
static void      s_diag_update(void);
static void      s_diag_btn_back_cb(lv_event_t *e);

/* Guide —— 使用指南 */
static lv_obj_t *s_guide_create(void);
static void      s_guide_update(void);
static void      s_guide_btn_back_cb(lv_event_t *e);

/* ============================================================================
 * 屏幕注册表 (Screen Registry)
 * ============================================================================ */

/** @brief 屏幕描述符表 —— const，存于 Flash */
static const App_ScreenOps_t s_screen_ops[APP_SCR_COUNT] = {
    /* APP_SCR_HOME     */ { s_home_create,     s_home_update,     NULL },
    /* APP_SCR_MAIN     */ { s_main_create,      s_main_update,     NULL },
    /* APP_SCR_SETTINGS */ { s_settings_create,  s_settings_update, NULL },
    /* APP_SCR_CAPTURE  */ { s_capture_create,   s_capture_update,  NULL },
    /* APP_SCR_DIAG     */ { s_diag_create,      s_diag_update,     NULL },
    /* APP_SCR_GUIDE    */ { s_guide_create,     s_guide_update,    NULL },
};

/** @brief 各屏幕 LVGL 根对象缓存（NULL = 尚未创建） */
static lv_obj_t *s_screen_obj[APP_SCR_COUNT];

/** @brief 当前活跃屏幕 ID */
static App_ScreenId_t s_current_screen = APP_SCR_HOME;

/** @brief 框架初始化标志 */
static uint8_t s_inited = 0u;

/* ============================================================================
 * Main View 内部控件引用
 * ============================================================================ */

/** @brief Main View 右侧面板容器 */
static lv_obj_t *s_main_right_panel = NULL;
/** @brief Main View 状态栏 */
static lv_obj_t *s_main_statusbar = NULL;
/** @brief Main View 工具栏 */
static lv_obj_t *s_main_toolbar = NULL;
/** @brief Main View 状态栏 FPS 标签 */
static lv_obj_t *s_main_lbl_fps = NULL;
/** @brief Main View 状态栏模式标签 */
static lv_obj_t *s_main_lbl_mode = NULL;
/** @brief Main View 状态栏 SAI 状态标签 */
static lv_obj_t *s_main_lbl_sai = NULL;
/** @brief Main View 读数标签 —— 角度 */
static lv_obj_t *s_main_lbl_angle = NULL;
/** @brief Main View 读数标签 —— 能量 */
static lv_obj_t *s_main_lbl_energy = NULL;
/** @brief Main View 读数标签 —— dB 值 */
static lv_obj_t *s_main_lbl_db = NULL;
/** @brief Main View 状态栏 —— 触发状态标签 */
static lv_obj_t *s_main_lbl_trig = NULL;
/** @brief Main View 状态栏 —— 激光/夜间指示标签 */
static lv_obj_t *s_main_lbl_laser = NULL;
/** @brief Main View 工具栏 —— 模式信息标签 */
static lv_obj_t *s_main_lbl_toolbar_info = NULL;
/** @brief Main View 触发按钮对象 (按钮本身, 用于状态着色) */
static lv_obj_t *s_main_btn_trigger = NULL;

/* ============================================================================
 * Capture 屏幕内部控件引用
 * ============================================================================ */

/* Screenshot card */
static lv_obj_t *s_cap_lbl_shot_count = NULL;  /**< 截图计数标签 */
static lv_obj_t *s_cap_lbl_shot_state = NULL;  /**< 截图状态标签 */
static lv_obj_t *s_cap_btn_screenshot = NULL;   /**< 截图按钮 */

/* Recording card */
static lv_obj_t *s_cap_btn_record     = NULL;   /**< 录音按钮 */
static lv_obj_t *s_cap_lbl_rec_btn    = NULL;   /**< 录音按钮文字 */
static lv_obj_t *s_cap_dd_rec_mode    = NULL;   /**< 录音模式下拉 */
static lv_obj_t *s_cap_lbl_duration   = NULL;   /**< 录音时长标签 */
static lv_obj_t *s_cap_lbl_rec_info   = NULL;   /**< 录音数据量标签 */
static lv_obj_t *s_cap_lbl_rec_state  = NULL;   /**< 录音状态标签 */

/* Beamsteer controls */
static lv_obj_t *s_cap_sw_beamsteer   = NULL;   /**< 波束使能开关 */
static lv_obj_t *s_cap_dd_beam_mode   = NULL;   /**< 波束模式下拉 */
static lv_obj_t *s_cap_lbl_beam_dir   = NULL;   /**< 波束方向显示 */

/* SD status bar */
static lv_obj_t *s_cap_lbl_sd_status  = NULL;   /**< SD 状态标签 */
static lv_obj_t *s_cap_bar_sd_usage   = NULL;   /**< SD 使用量进度条 */
static lv_obj_t *s_cap_lbl_sd_space   = NULL;   /**< SD 空间标签 */

/** @brief Capture 屏幕上次更新时间戳 */
static uint32_t s_cap_last_update_tick = 0u;

/** @brief 录音按钮动画是否活跃 */
static uint8_t s_cap_rec_anim_active = 0u;

/* (时间序列图表已移至频谱面板内置 EMA 可视化) */

/** @brief 实时数据缓存 */
static App_UiLiveData_t s_live_data;
/** @brief 实时数据更新标志 */
static uint8_t s_live_data_dirty = 0u;

/* ---- Settings 屏幕控件引用 ---- */
static lv_obj_t *s_settings_dd_mode       = NULL;
static lv_obj_t *s_settings_slider_gamma  = NULL;
static lv_obj_t *s_settings_lbl_gamma     = NULL;
static lv_obj_t *s_settings_slider_noise  = NULL;
static lv_obj_t *s_settings_lbl_noise     = NULL;
static lv_obj_t *s_settings_sw_interp     = NULL;
static lv_obj_t *s_settings_sw_fine       = NULL;
static lv_obj_t *s_settings_slider_freq_lo = NULL;
static lv_obj_t *s_settings_slider_freq_hi = NULL;
static lv_obj_t *s_settings_lbl_freq      = NULL;
static lv_obj_t *s_settings_sw_laser      = NULL;
static lv_obj_t *s_settings_sw_night      = NULL;

/* Settings 屏幕额外回调 */
static void s_settings_dd_mode_cb(lv_event_t *e);
static void s_settings_slider_gamma_cb(lv_event_t *e);
static void s_settings_slider_noise_cb(lv_event_t *e);
static void s_settings_sw_interp_cb(lv_event_t *e);
static void s_settings_sw_fine_cb(lv_event_t *e);
static void s_settings_btn_guide_cb(lv_event_t *e);
static void s_settings_slider_freq_lo_cb(lv_event_t *e);
static void s_settings_slider_freq_hi_cb(lv_event_t *e);
static void s_settings_preset_btn_cb(lv_event_t *e);
static void s_settings_sw_laser_cb(lv_event_t *e);
static void s_settings_sw_night_cb(lv_event_t *e);
static void s_settings_btn_reboot_cb(lv_event_t *e);
static void s_settings_btn_noise_cal_cb(lv_event_t *e);

/* ---- Diag 屏幕控件引用 ---- */
static lv_obj_t *s_diag_lbl_info       = NULL;
static lv_obj_t *s_diag_chart          = NULL;
static lv_chart_series_t *s_diag_perf_ser = NULL;

/* ============================================================================
 * 工具函数
 * ============================================================================ */

/**
 * @brief 确保屏幕已创建（懒加载）
 * @param id  屏幕 ID
 * @return    屏幕根对象，或 NULL（创建失败）
 */
static lv_obj_t *s_ensure_created(App_ScreenId_t id)
{
    if (id >= APP_SCR_COUNT)
    {
        return NULL;
    }
    if (s_screen_obj[id] == NULL)
    {
        if (s_screen_ops[id].create != NULL)
        {
            s_screen_obj[id] = s_screen_ops[id].create();
        }
    }
    return s_screen_obj[id];
}

/* ============================================================================
 * 公开 API 实现
 * ============================================================================ */

void App_UiScreens_Init(void)
{
    uint8_t i;

    if (s_inited != 0u)
    {
        return;
    }

    (void)memset(s_screen_obj, 0, sizeof(s_screen_obj));

    /* 初始化工业深色主题样式表 */
    App_UiStyles_Init();

    /* 创建 Home 屏幕并显示 */
    (void)s_ensure_created(APP_SCR_HOME);
    if (s_screen_obj[APP_SCR_HOME] != NULL)
    {
        lv_scr_load(s_screen_obj[APP_SCR_HOME]);
    }

    s_current_screen = APP_SCR_HOME;
    s_inited = 1u;
    (void)i;
}

void App_UiScreens_Switch(App_ScreenId_t id)
{
    lv_obj_t *scr;

    if (id >= APP_SCR_COUNT)
    {
        return;
    }
    if (id == s_current_screen)
    {
        return;
    }

    scr = s_ensure_created(id);
    if (scr == NULL)
    {
        return;
    }

    lv_scr_load(scr);
    s_current_screen = id;
}

App_ScreenId_t App_UiScreens_GetCurrent(void)
{
    return s_current_screen;
}

void App_UiScreens_Update(void)
{
    if (s_inited == 0u)
    {
        return;
    }
    if (s_current_screen < APP_SCR_COUNT)
    {
        if (s_screen_ops[s_current_screen].update != NULL)
        {
            s_screen_ops[s_current_screen].update();
        }
    }
}

void App_UiScreens_OnHomeStartPressed(void)
{
    App_UiScreens_Switch(APP_SCR_MAIN);
}

void App_UiScreens_SetLiveData(const App_UiLiveData_t *data)
{
    if (data != NULL)
    {
        s_live_data = *data;
        s_live_data_dirty = 1u;
    }
}

/* ============================================================================
 * Home 屏幕回调 (GUI Guider wrapper)
 * ============================================================================ */

static lv_obj_t *s_home_create(void)
{
    /* GUI Guider 通过 setup_scr_home() 创建 Home 屏幕布局 */
    (void)memset(&guider_ui, 0, sizeof(guider_ui));
    setup_scr_home(&guider_ui);

    /* setup_scr_home() 内部调用了 events_init_home()，注册了旧事件回调。
     * 旧回调试图加载 guider_ui.using（NULL）会导致 HardFault。
     * 移除旧回调后重新绑定自定义导航回调。 */
    if (guider_ui.home_btn_1 != NULL)
    {
        lv_obj_remove_event_cb(guider_ui.home_btn_1, NULL);
        lv_obj_add_event_cb(guider_ui.home_btn_1, s_home_btn_start_cb,
                            LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.home_btn_2 != NULL)
    {
        lv_obj_remove_event_cb(guider_ui.home_btn_2, NULL);
        lv_obj_add_event_cb(guider_ui.home_btn_2, s_home_btn_settings_cb,
                            LV_EVENT_CLICKED, NULL);
    }
    if (guider_ui.home_cont_1 != NULL)
    {
        lv_obj_remove_event_cb(guider_ui.home_cont_1, NULL);
        lv_obj_add_event_cb(guider_ui.home_cont_1, s_home_cont_click_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    return guider_ui.home;
}

static void s_home_update(void)
{
    /* Home 屏幕是静态的，无需周期更新 */
}

/** @brief Home "开始" 按钮回调 → 切换到 Main 屏幕 */
static void s_home_btn_start_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_MAIN);
}

/** @brief Home 设置下拉按钮回调 → 切换 cont_1 可见性 */
static void s_home_btn_settings_cb(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);
    }
}

/** @brief Home 下拉容器点击回调 → 隐藏自身 */
static void s_home_cont_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *current = lv_event_get_current_target(e);
    if (target == current)
    {
        lv_obj_add_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============================================================================
 * Main View 屏幕回调
 * ============================================================================ */

/**
 * @brief 创建 Main View 工作界面 (工业化重设计)
 * @return 屏幕根对象
 *
 * 布局：
 * ┌──────────────────────────────────────────────────────┐
 * │ NECCS  TRIG:IDLE  SAI:●  LASER:OFF  NIGHT:OFF 25FPS│ 状态栏(28px)
 * ├────────────────┬─────────────────────────────────────┤
 * │                │  Dir: +15.0° , +22.5°               │
 * │  CAMERA +      │  -1.4 dB  (28px 大号)               │
 * │  HEATMAP       │  E: 0.850                           │
 * │  640×424       │  ─────────────────────               │
 * │                │  SPECTRUM PANEL (频谱图)             │
 * │                │  ▓▓▓▓▓░░░▓▓░                      │
 * │                │  [Full][Voice][Ultra][Low]           │
 * │                │  ─────────────────────               │
 * │                │  [📸] [⏱] [🔦] [⚙] [ℹ]             │
 * ├────────────────┴─────────────────────────────────────┤
 * │ FAST ▸ 48kHz·16ch·SRP-PHAT  Band: 562-7875 Hz       │ 工具栏(28px)
 * └──────────────────────────────────────────────────────┘
 */
static lv_obj_t *s_main_create(void)
{
    lv_obj_t *scr;
    lv_obj_t *area_left;
    lv_coord_t content_h = (lv_coord_t)(480u - UI_STATUSBAR_H - UI_TOOLBAR_H);
    lv_coord_t panel_w   = (lv_coord_t)APP_DISPLAY_UI_PANEL_W;
    lv_coord_t btn_w     = (lv_coord_t)(APP_DISPLAY_UI_PANEL_W - 16u);

    /* ---- 屏幕根对象 ---- */
    scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ================================================================
     *  顶部状态栏 (800 × UI_STATUSBAR_H) —— 增强版
     * ================================================================ */
    s_main_statusbar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_statusbar);
    lv_obj_add_style(s_main_statusbar, &g_ui_styles.statusbar, 0);
    lv_obj_set_size(s_main_statusbar, 800, UI_STATUSBAR_H);
    lv_obj_set_pos(s_main_statusbar, 0, 0);
    lv_obj_clear_flag(s_main_statusbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_main_statusbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_main_statusbar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 品牌名 (点击返回 Home) */
    s_main_lbl_mode = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_mode, "NECCS");
    lv_obj_add_style(s_main_lbl_mode, &g_ui_styles.label_title, 0);
    lv_obj_set_style_text_color(s_main_lbl_mode, UI_COLOR_ACCENT, 0);
    lv_obj_add_flag(s_main_lbl_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_main_lbl_mode, s_main_btn_home_cb, LV_EVENT_CLICKED, NULL);

    /* 触发状态 */
    s_main_lbl_trig = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_trig, "TRIG:IDLE");
    lv_obj_set_style_text_font(s_main_lbl_trig, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_INACTIVE, 0);

    /* SAI 状态 */
    s_main_lbl_sai = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_sai, "SAI:---");
    lv_obj_set_style_text_font(s_main_lbl_sai, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_sai, UI_COLOR_INACTIVE, 0);

    /* 激光/夜间模式指示 */
    s_main_lbl_laser = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_laser, "");
    lv_obj_set_style_text_font(s_main_lbl_laser, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_laser, UI_COLOR_INACTIVE, 0);

    /* FPS */
    s_main_lbl_fps = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_fps, "-- FPS");
    lv_obj_set_style_text_font(s_main_lbl_fps, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_fps, UI_COLOR_TEXT_SECONDARY, 0);

    /* ================================================================
     *  左侧 chroma key 透明区域 (640 × content_h)
     * ================================================================ */
    area_left = lv_obj_create(scr);
    lv_obj_remove_style_all(area_left);
    lv_obj_set_size(area_left, 640, content_h);
    lv_obj_set_pos(area_left, 0, (lv_coord_t)UI_STATUSBAR_H);
    lv_obj_set_style_bg_color(area_left, UI_COLOR_CHROMA_KEY, 0);
    lv_obj_set_style_bg_opa(area_left, LV_OPA_COVER, 0);
    lv_obj_clear_flag(area_left, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* ================================================================
     *  右侧面板 (160 × content_h) —— 数值面板 + 频谱 + 快捷键
     * ================================================================ */
    s_main_right_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_right_panel);
    lv_obj_set_size(s_main_right_panel, panel_w, content_h);
    lv_obj_set_pos(s_main_right_panel, 640, (lv_coord_t)UI_STATUSBAR_H);
    lv_obj_set_style_bg_color(s_main_right_panel, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(s_main_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_main_right_panel, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_main_right_panel, 1, 0);
    lv_obj_set_style_border_side(s_main_right_panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_pad_all(s_main_right_panel, UI_PAD_SMALL, 0);
    lv_obj_set_style_pad_row(s_main_right_panel, 2, 0);
    lv_obj_clear_flag(s_main_right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_main_right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_main_right_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* -- 读数面板 (角度 + dB 大号 + 能量) -- */
    {
        lv_obj_t *box = lv_obj_create(s_main_right_panel);
        lv_obj_remove_style_all(box);
        lv_obj_add_style(box, &g_ui_styles.panel, 0);
        lv_obj_set_size(box, btn_w, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(box, 4, 0);
        lv_obj_set_style_pad_row(box, 1, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

        /* 角度: 使用 label_title (14px) */
        s_main_lbl_angle = lv_label_create(box);
        lv_label_set_text(s_main_lbl_angle, "Dir: -- , --");
        lv_obj_add_style(s_main_lbl_angle, &g_ui_styles.label_title, 0);

        /* dB读数: 大号 28px */
        s_main_lbl_db = lv_label_create(box);
        lv_label_set_text(s_main_lbl_db, "-- dB");
        lv_obj_add_style(s_main_lbl_db, &g_ui_styles.label_value_lg, 0);

        /* 能量: 小号 12px */
        s_main_lbl_energy = lv_label_create(box);
        lv_label_set_text(s_main_lbl_energy, "E: ---");
        lv_obj_add_style(s_main_lbl_energy, &g_ui_styles.label_unit, 0);
    }

    /* -- 频谱面板 (嵌入式 canvas + sliders + presets) -- */
    {
        lv_coord_t spec_h = (lv_coord_t)(content_h - 80 - 34 - 16);
        (void)App_UiSpecPanel_Create(s_main_right_panel, btn_w, spec_h);
    }

    /* -- 弹性间隔 -- */
    {
        lv_obj_t *spacer = lv_obj_create(s_main_right_panel);
        lv_obj_remove_style_all(spacer);
        lv_obj_set_size(spacer, 1, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_grow(spacer, 1);
    }

    /* ================================================================
     *  快捷按钮行 —— 5 个功能按钮
     *  [📸 Capture] [⏱ Trigger] [🔦 Laser] [⚙ Settings] [ℹ Diag]
     * ================================================================ */
    {
        lv_obj_t *btn_row = lv_obj_create(s_main_right_panel);
        lv_obj_remove_style_all(btn_row);
        lv_obj_set_size(btn_row, btn_w, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(btn_row, 3, 0);
        lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* 📸 Capture */
        {
            lv_obj_t *btn = lv_btn_create(btn_row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 26, 26);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_IMAGE);
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_capture_cb, LV_EVENT_CLICKED, NULL);
        }
        /* ⏱ Trigger */
        {
            lv_obj_t *btn = lv_btn_create(btn_row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 26, 26);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_PLAY);
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_trigger_cb, LV_EVENT_CLICKED, NULL);
            s_main_btn_trigger = btn;
        }
        /* 🔦 Laser */
        {
            lv_obj_t *btn = lv_btn_create(btn_row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 26, 26);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN);
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_laser_cb, LV_EVENT_CLICKED, NULL);
        }
        /* ⚙ Settings */
        {
            lv_obj_t *btn = lv_btn_create(btn_row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 26, 26);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_SETTINGS);
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_settings_cb, LV_EVENT_CLICKED, NULL);
        }
        /* ℹ Diag */
        {
            lv_obj_t *btn = lv_btn_create(btn_row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 26, 26);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_LIST);
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_diag_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    /* ================================================================
     *  底部工具栏 (800 × UI_TOOLBAR_H) —— 模式 + 管线信息
     * ================================================================ */
    s_main_toolbar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_toolbar);
    lv_obj_add_style(s_main_toolbar, &g_ui_styles.toolbar, 0);
    lv_obj_set_size(s_main_toolbar, 800, UI_TOOLBAR_H);
    lv_obj_set_pos(s_main_toolbar, 0, (lv_coord_t)(480u - UI_TOOLBAR_H));
    lv_obj_clear_flag(s_main_toolbar, LV_OBJ_FLAG_SCROLLABLE);

    {
        s_main_lbl_toolbar_info = lv_label_create(s_main_toolbar);
        lv_label_set_text(s_main_lbl_toolbar_info,
                          "FAST " LV_SYMBOL_RIGHT " 48kHz 16ch SRP-PHAT");
        lv_obj_add_style(s_main_lbl_toolbar_info, &g_ui_styles.label_unit, 0);
        lv_obj_align(s_main_lbl_toolbar_info, LV_ALIGN_LEFT_MID, UI_PAD_NORMAL, 0);
    }

    return scr;
}

/* ---- Main View 按钮回调 ---- */

static void s_main_btn_settings_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_SETTINGS);
}

static void s_main_btn_home_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_HOME);
}

static void s_main_btn_diag_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_DIAG);
}

/** @brief Capture 按钮 → Capture 屏幕 */
static void s_main_btn_capture_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_CAPTURE);
}

/** @brief Trigger 按钮 → ARM/DISARM 切换 */
static void s_main_btn_trigger_cb(lv_event_t *e)
{
    (void)e;
    switch (App_Trigger_GetState())
    {
    case APP_TRIGGER_IDLE:
        App_Trigger_Arm();
        break;
    case APP_TRIGGER_ARMED:
        App_Trigger_Disarm();
        break;
    case APP_TRIGGER_TRIGGERED:
        App_Trigger_Rearm();
        break;
    default:
        break;
    }
}

/** @brief Laser 按钮 → 激光开关切换 */
static void s_main_btn_laser_cb(lv_event_t *e)
{
    (void)e;
    App_Laser_Toggle();
}

/**
 * @brief Main View 周期更新 (工业化增强版)
 * @details 更新状态栏指示、读数面板、频谱、工具栏信息。
 */
static void s_main_update(void)
{
    char buf[64];
    float db_val;
    App_SpectrumFrame_t spec_frame;
    const char *mode_str;
    uint16_t freq_lo, freq_hi;

    if (s_live_data_dirty == 0u)
    {
        return;
    }
    s_live_data_dirty = 0u;

    /* ---- 状态栏 FPS ---- */
    if (s_main_lbl_fps != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%lu FPS",
                       (unsigned long)s_live_data.ui_fps);
        lv_label_set_text(s_main_lbl_fps, buf);
    }

    /* ---- 状态栏 SAI ---- */
    if (s_main_lbl_sai != NULL)
    {
        lv_label_set_text(s_main_lbl_sai,
                          s_live_data.sai_active ? "SAI:OK" : "SAI:---");
        lv_obj_set_style_text_color(s_main_lbl_sai,
                                    s_live_data.sai_active ? UI_COLOR_OK : UI_COLOR_INACTIVE,
                                    0);
    }

    /* ---- 状态栏 触发状态 (三态着色) ---- */
    if (s_main_lbl_trig != NULL)
    {
        switch ((App_TriggerState_t)s_live_data.trigger_state)
        {
        case APP_TRIGGER_ARMED:
            lv_label_set_text(s_main_lbl_trig, "TRIG:ARM");
            lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_WARNING, 0);
            break;
        case APP_TRIGGER_TRIGGERED:
            lv_label_set_text(s_main_lbl_trig, "TRIG:HIT");
            lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_ERROR, 0);
            break;
        default:
            lv_label_set_text(s_main_lbl_trig, "TRIG:IDLE");
            lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_INACTIVE, 0);
            break;
        }
    }

    /* ---- 触发按钮动态着色 ---- */
    if (s_main_btn_trigger != NULL)
    {
        switch ((App_TriggerState_t)s_live_data.trigger_state)
        {
        case APP_TRIGGER_ARMED:
            lv_obj_set_style_bg_color(s_main_btn_trigger, UI_COLOR_WARNING, 0);
            lv_obj_set_style_bg_opa(s_main_btn_trigger, LV_OPA_COVER, 0);
            break;
        case APP_TRIGGER_TRIGGERED:
            lv_obj_set_style_bg_color(s_main_btn_trigger, UI_COLOR_ERROR, 0);
            lv_obj_set_style_bg_opa(s_main_btn_trigger, LV_OPA_COVER, 0);
            break;
        default:
            lv_obj_set_style_bg_color(s_main_btn_trigger, UI_COLOR_BG_PANEL, 0);
            lv_obj_set_style_bg_opa(s_main_btn_trigger, LV_OPA_COVER, 0);
            break;
        }
    }

    /* ---- 状态栏 激光/夜间 ---- */
    if (s_main_lbl_laser != NULL)
    {
        if (s_live_data.night_mode != 0u)
        {
            lv_label_set_text(s_main_lbl_laser, "NIGHT");
            lv_obj_set_style_text_color(s_main_lbl_laser, UI_COLOR_WARNING, 0);
        }
        else if (s_live_data.laser_on != 0u)
        {
            lv_label_set_text(s_main_lbl_laser, "LASER");
            lv_obj_set_style_text_color(s_main_lbl_laser, UI_COLOR_OK, 0);
        }
        else
        {
            lv_label_set_text(s_main_lbl_laser, "");
        }
    }

    /* ---- 声源角度 (14px title) ---- */
    if (s_main_lbl_angle != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "Dir:%+.0f,%+.0f",
                       (double)s_live_data.x_angle,
                       (double)s_live_data.y_angle);
        lv_label_set_text(s_main_lbl_angle, buf);
    }

    /* ---- dB 读数 (28px 大号) ---- */
    if (s_main_lbl_db != NULL)
    {
        if (s_live_data.energy > 1.0e-6f)
        {
            db_val = 20.0f * log10f(s_live_data.energy);
            (void)snprintf(buf, sizeof(buf), "%.1f dB", (double)db_val);
        }
        else
        {
            (void)snprintf(buf, sizeof(buf), "-inf dB");
        }
        lv_label_set_text(s_main_lbl_db, buf);

        /* dB 值着色: 绿(<-20), 黄(-20~-6), 红(>-6) */
        if (s_live_data.energy > 1.0e-6f)
        {
            if (db_val > -6.0f)
            {
                lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_METER_HIGH, 0);
            }
            else if (db_val > -20.0f)
            {
                lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_METER_MID, 0);
            }
            else
            {
                lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_METER_LOW, 0);
            }
        }
        else
        {
            lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_INACTIVE, 0);
        }
    }

    /* ---- 能量 (12px) ---- */
    if (s_main_lbl_energy != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "E: %.3f",
                       (double)s_live_data.energy);
        lv_label_set_text(s_main_lbl_energy, buf);
    }

    /* ---- 频谱面板实时更新 ---- */
    if (App_Spectrum_GetLatestFrame(&spec_frame) != 0u)
    {
        App_UiSpecPanel_Update(&spec_frame);
    }

    /* ---- 工具栏：模式 + 频段 ---- */
    if (s_main_lbl_toolbar_info != NULL)
    {
        switch (App_RuntimeConfig_GetDisplayMode())
        {
        case APP_RUNTIME_DISP_MODE_FAST:     mode_str = "FAST"; break;
        case APP_RUNTIME_DISP_MODE_BALANCED: mode_str = "BAL";  break;
        case APP_RUNTIME_DISP_MODE_CLEAN:    mode_str = "CLN";  break;
        default:                             mode_str = "???";  break;
        }
        App_RuntimeConfig_GetFreqBand(&freq_lo, &freq_hi);
        (void)snprintf(buf, sizeof(buf), "%s " LV_SYMBOL_RIGHT " 48k 16ch  %d-%dHz",
                       mode_str,
                       (int)App_Spectrum_BinToHz(freq_lo),
                       (int)App_Spectrum_BinToHz(freq_hi));
        lv_label_set_text(s_main_lbl_toolbar_info, buf);
    }
}

/* ============================================================================
 * Settings 屏幕回调（占位 —— Phase 1.5 实现）
 * ============================================================================ */

/* ============================================================================
 * Settings 屏幕 —— 三标签 TabView (Display / Algorithm / System)
 * ============================================================================ */

/** @brief 创建一个设置行：Label + 控件 */
static lv_obj_t *s_make_setting_row(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *lbl;
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    return row;
}

static lv_obj_t *s_settings_create(void)
{
    App_Runtime_DisplayCfg_t dcfg;
    App_Runtime_DisplayMode_t dmode;
    uint16_t freq_lo, freq_hi;
    char buf[32];
    lv_obj_t *scr, *tv, *tab_disp, *tab_algo, *tab_sys, *row;

    scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    App_RuntimeConfig_GetDisplayCfg(&dcfg);
    dmode = App_RuntimeConfig_GetDisplayMode();
    App_RuntimeConfig_GetFreqBand(&freq_lo, &freq_hi);

    /* ---- 标题 ---- */
    {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, LV_SYMBOL_SETTINGS " Settings");
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 4);
    }

    /* ---- TabView (3 tabs) ---- */
    tv = lv_tabview_create(scr, LV_DIR_TOP, 30);
    lv_obj_set_size(tv, 780, 390);
    lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(tv, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);

    tab_disp = lv_tabview_add_tab(tv, "Display");
    tab_algo = lv_tabview_add_tab(tv, "Algorithm");
    tab_sys  = lv_tabview_add_tab(tv, "System");

    /* 设置 tab 内容样式 */
    lv_obj_set_flex_flow(tab_disp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_disp, 6, 0);
    lv_obj_set_style_pad_all(tab_disp, 8, 0);
    lv_obj_set_flex_flow(tab_algo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_algo, 6, 0);
    lv_obj_set_style_pad_all(tab_algo, 8, 0);
    lv_obj_set_flex_flow(tab_sys, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_sys, 6, 0);
    lv_obj_set_style_pad_all(tab_sys, 8, 0);

    /* ================================================================
     *  Display Tab
     * ================================================================ */

    /* Display Mode */
    row = s_make_setting_row(tab_disp, "Display Mode:");
    s_settings_dd_mode = lv_dropdown_create(row);
    lv_dropdown_set_options(s_settings_dd_mode, "Fast\nBalanced\nClean");
    lv_dropdown_set_selected(s_settings_dd_mode, (uint16_t)dmode);
    lv_obj_set_width(s_settings_dd_mode, 150);
    lv_obj_add_event_cb(s_settings_dd_mode, s_settings_dd_mode_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Gamma */
    row = s_make_setting_row(tab_disp, "Gamma:");
    s_settings_slider_gamma = lv_slider_create(row);
    lv_obj_set_width(s_settings_slider_gamma, 200);
    lv_slider_set_range(s_settings_slider_gamma, 5, 30);
    lv_slider_set_value(s_settings_slider_gamma,
                        (int32_t)(dcfg.gamma * 10.0f), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_slider_gamma, s_settings_slider_gamma_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    s_settings_lbl_gamma = lv_label_create(row);
    (void)snprintf(buf, sizeof(buf), "%.1f", (double)dcfg.gamma);
    lv_label_set_text(s_settings_lbl_gamma, buf);
    lv_obj_add_style(s_settings_lbl_gamma, &g_ui_styles.label_value, 0);

    /* Noise Gate */
    row = s_make_setting_row(tab_disp, "Noise Gate:");
    s_settings_slider_noise = lv_slider_create(row);
    lv_obj_set_width(s_settings_slider_noise, 200);
    lv_slider_set_range(s_settings_slider_noise, 0, 100);
    lv_slider_set_value(s_settings_slider_noise,
                        (int32_t)(dcfg.noise_gate_ratio * 100.0f), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_slider_noise, s_settings_slider_noise_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    s_settings_lbl_noise = lv_label_create(row);
    (void)snprintf(buf, sizeof(buf), "%d%%", (int)(dcfg.noise_gate_ratio * 100.0f));
    lv_label_set_text(s_settings_lbl_noise, buf);
    lv_obj_add_style(s_settings_lbl_noise, &g_ui_styles.label_value, 0);

    /* Bilinear Interpolation */
    row = s_make_setting_row(tab_disp, "Bilinear Interp:");
    s_settings_sw_interp = lv_switch_create(row);
    if (dcfg.interp_mode == (uint8_t)APP_RUNTIME_DISP_INTERP_BILINEAR)
    {
        lv_obj_add_state(s_settings_sw_interp, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_settings_sw_interp, s_settings_sw_interp_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ================================================================
     *  Algorithm Tab
     * ================================================================ */

    /* Frequency Band Low */
    row = s_make_setting_row(tab_algo, "Freq Lo:");
    s_settings_slider_freq_lo = lv_slider_create(row);
    lv_obj_set_width(s_settings_slider_freq_lo, 200);
    lv_slider_set_range(s_settings_slider_freq_lo, 1, (int32_t)APP_SPECTRUM_BIN_COUNT);
    lv_slider_set_value(s_settings_slider_freq_lo, (int32_t)freq_lo, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_slider_freq_lo, s_settings_slider_freq_lo_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Frequency Band High */
    row = s_make_setting_row(tab_algo, "Freq Hi:");
    s_settings_slider_freq_hi = lv_slider_create(row);
    lv_obj_set_width(s_settings_slider_freq_hi, 200);
    lv_slider_set_range(s_settings_slider_freq_hi, 1, (int32_t)APP_SPECTRUM_BIN_COUNT);
    lv_slider_set_value(s_settings_slider_freq_hi, (int32_t)freq_hi, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_slider_freq_hi, s_settings_slider_freq_hi_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Freq band label */
    s_settings_lbl_freq = lv_label_create(tab_algo);
    (void)snprintf(buf, sizeof(buf), "%d - %d Hz",
                   (int)App_Spectrum_BinToHz(freq_lo),
                   (int)App_Spectrum_BinToHz(freq_hi));
    lv_label_set_text(s_settings_lbl_freq, buf);
    lv_obj_add_style(s_settings_lbl_freq, &g_ui_styles.label_value, 0);

    /* Presets */
    {
        lv_obj_t *preset_row = lv_obj_create(tab_algo);
        lv_obj_remove_style_all(preset_row);
        lv_obj_set_size(preset_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(preset_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(preset_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(preset_row, 8, 0);
        lv_obj_clear_flag(preset_row, LV_OBJ_FLAG_SCROLLABLE);

        {
            const char *names[] = {"Full", "Voice", "Ultra", "Low"};
            uint32_t pi;
            for (pi = 0u; pi < 4u; pi++)
            {
                lv_obj_t *btn = lv_btn_create(preset_row);
                lv_obj_t *lbl;
                lv_obj_add_style(btn, &g_ui_styles.btn, 0);
                lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
                lv_obj_set_size(btn, 72, 28);
                lbl = lv_label_create(btn);
                lv_label_set_text(lbl, names[pi]);
                lv_obj_center(lbl);
                lv_obj_add_event_cb(btn, s_settings_preset_btn_cb,
                                    LV_EVENT_CLICKED, (void *)(uintptr_t)pi);
            }
        }
    }

    /* Fine Fusion */
    row = s_make_setting_row(tab_algo, "Fine Fusion:");
    s_settings_sw_fine = lv_switch_create(row);
    if (dcfg.fine_fusion_enable != 0u)
    {
        lv_obj_add_state(s_settings_sw_fine, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_settings_sw_fine, s_settings_sw_fine_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Noise Floor Calibrate button */
    {
        lv_obj_t *btn = lv_btn_create(tab_algo);
        lv_obj_t *lbl;
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 200, 28);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Noise Calibrate");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_settings_btn_noise_cal_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    /* ================================================================
     *  System Tab
     * ================================================================ */

    /* Laser */
    row = s_make_setting_row(tab_sys, "Laser:");
    s_settings_sw_laser = lv_switch_create(row);
    if (App_Laser_GetState() == APP_LASER_ON)
    {
        lv_obj_add_state(s_settings_sw_laser, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_settings_sw_laser, s_settings_sw_laser_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Night Mode */
    row = s_make_setting_row(tab_sys, "Night Mode:");
    s_settings_sw_night = lv_switch_create(row);
    if (App_NightMode_GetState() == APP_NIGHTMODE_ON)
    {
        lv_obj_add_state(s_settings_sw_night, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_settings_sw_night, s_settings_sw_night_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* FW Version (read-only) */
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        lv_label_set_text(lbl, "MCU: STM32H743 @ 480MHz");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        lv_label_set_text(lbl, "RTOS: FreeRTOS v10.3.1");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        lv_label_set_text(lbl, "CLI: UART 921600 8N1");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }

    /* Heap usage (read-only, updated on creation) */
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        (void)snprintf(buf, sizeof(buf), "Heap Free: %lu B",
                       (unsigned long)xPortGetFreeHeapSize());
        lv_label_set_text(lbl, buf);
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }

    /* Guide button */
    {
        lv_obj_t *btn = lv_btn_create(tab_sys);
        lv_obj_t *lbl;
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 200, 32);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_FILE " Usage Guide");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_settings_btn_guide_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Reboot button */
    {
        lv_obj_t *btn = lv_btn_create(tab_sys);
        lv_obj_t *lbl;
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 200, 32);
        lv_obj_set_style_bg_color(btn, UI_COLOR_WARNING, 0);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_POWER " Reboot");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_settings_btn_reboot_cb, LV_EVENT_CLICKED, NULL);
    }

    /* ---- 返回按钮 ---- */
    {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_t *lbl;
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 100, 32);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_settings_btn_back_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

static void s_settings_btn_back_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_MAIN);
}

static void s_settings_dd_mode_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    App_RuntimeConfig_SetDisplayMode((App_Runtime_DisplayMode_t)sel);
}

static void s_settings_slider_gamma_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);
    App_Runtime_DisplayCfg_t cfg;
    char buf[16];
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.gamma = (float)val / 10.0f;
    App_RuntimeConfig_SetDisplayCfg(&cfg);
    if (s_settings_lbl_gamma != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%.1f", (double)cfg.gamma);
        lv_label_set_text(s_settings_lbl_gamma, buf);
    }
}

static void s_settings_slider_noise_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);
    App_Runtime_DisplayCfg_t cfg;
    char buf[16];
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.noise_gate_ratio = (float)val / 100.0f;
    App_RuntimeConfig_SetDisplayCfg(&cfg);
    if (s_settings_lbl_noise != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%d%%", (int)val);
        lv_label_set_text(s_settings_lbl_noise, buf);
    }
}

static void s_settings_sw_interp_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    App_Runtime_DisplayCfg_t cfg;
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.interp_mode = lv_obj_has_state(sw, LV_STATE_CHECKED)
                      ? (uint8_t)APP_RUNTIME_DISP_INTERP_BILINEAR
                      : (uint8_t)APP_RUNTIME_DISP_INTERP_NEAREST;
    App_RuntimeConfig_SetDisplayCfg(&cfg);
}

static void s_settings_sw_fine_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    App_Runtime_DisplayCfg_t cfg;
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.fine_fusion_enable = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1u : 0u;
    App_RuntimeConfig_SetDisplayCfg(&cfg);
}

static void s_settings_btn_guide_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_GUIDE);
}

static void s_settings_slider_freq_lo_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(lv_event_get_target(e));
    int32_t hi = lv_slider_get_value(s_settings_slider_freq_hi);
    char buf[32];
    if (lo > hi) { lo = hi; lv_slider_set_value(lv_event_get_target(e), lo, LV_ANIM_OFF); }
    App_RuntimeConfig_SetFreqBand((uint16_t)lo, (uint16_t)hi);
    App_UiSpecPanel_ApplyPreset(SPEC_PRESET_FULL); /* sync spectrum panel */
    if (s_settings_lbl_freq != NULL) {
        (void)snprintf(buf, sizeof(buf), "%d - %d Hz",
                       (int)App_Spectrum_BinToHz((uint16_t)lo),
                       (int)App_Spectrum_BinToHz((uint16_t)hi));
        lv_label_set_text(s_settings_lbl_freq, buf);
    }
}

static void s_settings_slider_freq_hi_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(s_settings_slider_freq_lo);
    int32_t hi = lv_slider_get_value(lv_event_get_target(e));
    char buf[32];
    if (hi < lo) { hi = lo; lv_slider_set_value(lv_event_get_target(e), hi, LV_ANIM_OFF); }
    App_RuntimeConfig_SetFreqBand((uint16_t)lo, (uint16_t)hi);
    if (s_settings_lbl_freq != NULL) {
        (void)snprintf(buf, sizeof(buf), "%d - %d Hz",
                       (int)App_Spectrum_BinToHz((uint16_t)lo),
                       (int)App_Spectrum_BinToHz((uint16_t)hi));
        lv_label_set_text(s_settings_lbl_freq, buf);
    }
}

static void s_settings_preset_btn_cb(lv_event_t *e)
{
    static const uint16_t presets[][2] = {
        { 3u,  42u  },  /* Full */
        { 2u,  18u  },  /* Voice */
        { 54u, 128u },  /* Ultra */
        { 1u,  5u   }   /* Low */
    };
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    char buf[32];
    if (idx < 4u) {
        uint16_t lo = presets[idx][0];
        uint16_t hi = presets[idx][1];
        App_RuntimeConfig_SetFreqBand(lo, hi);
        if (s_settings_slider_freq_lo != NULL) {
            lv_slider_set_value(s_settings_slider_freq_lo, (int32_t)lo, LV_ANIM_OFF);
        }
        if (s_settings_slider_freq_hi != NULL) {
            lv_slider_set_value(s_settings_slider_freq_hi, (int32_t)hi, LV_ANIM_OFF);
        }
        if (s_settings_lbl_freq != NULL) {
            (void)snprintf(buf, sizeof(buf), "%d - %d Hz",
                           (int)App_Spectrum_BinToHz(lo),
                           (int)App_Spectrum_BinToHz(hi));
            lv_label_set_text(s_settings_lbl_freq, buf);
        }
    }
}

static void s_settings_sw_laser_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    App_Laser_SetState(lv_obj_has_state(sw, LV_STATE_CHECKED)
                       ? APP_LASER_ON : APP_LASER_OFF);
}

static void s_settings_sw_night_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        App_NightMode_Enable();
    }
    else
    {
        App_NightMode_Disable();
    }
}

static void s_settings_btn_reboot_cb(lv_event_t *e)
{
    (void)e;
    NVIC_SystemReset();
}

/** @brief 噪声底校零按钮回调 —— 将当前频谱作为噪声基线 */
static void s_settings_btn_noise_cal_cb(lv_event_t *e)
{
    App_SpectrumFrame_t frame;
    (void)e;
    if (App_Spectrum_GetLatestFrame(&frame) != 0u)
    {
        App_NoiseFloor_Calibrate(frame.magnitude, APP_SPECTRUM_BIN_COUNT);
    }
}

static void s_settings_update(void)
{
}

/* ============================================================================
 * Capture 屏幕 —— 数据捕获（截图 + 录音 + 波束控向）
 * ============================================================================ */

/** @brief 录音按钮脉冲动画回调 */
static void s_cap_rec_anim_cb(void *var, int32_t val)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)val, 0);
}

/** @brief 启动录音按钮脉冲动画 */
static void s_cap_rec_anim_start(void)
{
    lv_anim_t a;
    if (s_cap_btn_record == NULL || s_cap_rec_anim_active != 0u)
    {
        return;
    }
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cap_btn_record);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);
    lv_anim_set_time(&a, 600);
    lv_anim_set_playback_time(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, s_cap_rec_anim_cb);
    lv_anim_start(&a);
    s_cap_rec_anim_active = 1u;
}

/** @brief 停止录音按钮脉冲动画 */
static void s_cap_rec_anim_stop(void)
{
    if (s_cap_btn_record != NULL && s_cap_rec_anim_active != 0u)
    {
        lv_anim_del(s_cap_btn_record, s_cap_rec_anim_cb);
        lv_obj_set_style_bg_opa(s_cap_btn_record, LV_OPA_COVER, 0);
        s_cap_rec_anim_active = 0u;
    }
}

/** @brief Create Capture screen — screenshot + recording + beamsteer controls */
static lv_obj_t *s_capture_create(void)
{
    lv_obj_t *scr, *title_bar, *content, *card_l, *card_r, *sd_bar;
    lv_obj_t *row, *lbl, *btn;

    scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ================================================================
     *  标题栏 (800 × 32)
     * ================================================================ */
    title_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(title_bar);
    lv_obj_add_style(title_bar, &g_ui_styles.statusbar, 0);
    lv_obj_set_size(title_bar, 800, 32);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(title_bar, 8, 0);
    lv_obj_set_style_pad_column(title_bar, 8, 0);

    /* 返回按钮 */
    btn = lv_btn_create(title_bar);
    lv_obj_add_style(btn, &g_ui_styles.btn, 0);
    lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 60, 24);
    {
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn, s_capture_btn_back_cb, LV_EVENT_CLICKED, NULL);

    /* 标题 */
    lbl = lv_label_create(title_bar);
    lv_label_set_text(lbl, LV_SYMBOL_SAVE " CAPTURE & RECORD");
    lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);

    /* ================================================================
     *  主内容区 (800 × 388) — 两卡片布局
     * ================================================================ */
    content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 780, 388);
    lv_obj_set_pos(content, 10, 36);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(content, 20, 0);

    /* ================================================================
     *  左卡片 —— 截图 (Screenshot)
     * ================================================================ */
    card_l = lv_obj_create(content);
    lv_obj_remove_style_all(card_l);
    lv_obj_add_style(card_l, &g_ui_styles.panel, 0);
    lv_obj_set_size(card_l, 370, 320);
    lv_obj_set_style_pad_all(card_l, UI_PAD_NORMAL, 0);
    lv_obj_set_style_pad_row(card_l, 10, 0);
    lv_obj_clear_flag(card_l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card_l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_l, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 标题 */
    lbl = lv_label_create(card_l);
    lv_label_set_text(lbl, LV_SYMBOL_IMAGE " SCREENSHOT");
    lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);

    /* 截图计数 */
    s_cap_lbl_shot_count = lv_label_create(card_l);
    lv_label_set_text(s_cap_lbl_shot_count, "Count: 0");
    lv_obj_add_style(s_cap_lbl_shot_count, &g_ui_styles.label_value, 0);

    /* 截图状态 */
    s_cap_lbl_shot_state = lv_label_create(card_l);
    lv_label_set_text(s_cap_lbl_shot_state, "Ready");
    lv_obj_add_style(s_cap_lbl_shot_state, &g_ui_styles.label_unit, 0);

    /* 截图按钮 */
    s_cap_btn_screenshot = lv_btn_create(card_l);
    lv_obj_add_style(s_cap_btn_screenshot, &g_ui_styles.btn, 0);
    lv_obj_add_style(s_cap_btn_screenshot, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_cap_btn_screenshot, 200, 48);
    lv_obj_set_style_bg_color(s_cap_btn_screenshot, UI_COLOR_ACCENT, 0);
    {
        lbl = lv_label_create(s_cap_btn_screenshot);
        lv_label_set_text(lbl, LV_SYMBOL_IMAGE " Take Screenshot");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(s_cap_btn_screenshot, s_capture_btn_screenshot_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ================================================================
     *  右卡片 —— 录音 (Recording)
     * ================================================================ */
    card_r = lv_obj_create(content);
    lv_obj_remove_style_all(card_r);
    lv_obj_add_style(card_r, &g_ui_styles.panel, 0);
    lv_obj_set_size(card_r, 370, 320);
    lv_obj_set_style_pad_all(card_r, UI_PAD_NORMAL, 0);
    lv_obj_set_style_pad_row(card_r, 6, 0);
    lv_obj_clear_flag(card_r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card_r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_r, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 标题 */
    lbl = lv_label_create(card_r);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " RECORDING");
    lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);

    /* 模式下拉行 */
    row = lv_obj_create(card_r);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    {
        lbl = lv_label_create(row);
        lv_label_set_text(lbl, "Mode:");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }
    s_cap_dd_rec_mode = lv_dropdown_create(row);
    lv_dropdown_set_options(s_cap_dd_rec_mode, "MONO (Beam)\nRAW16 (16ch)");
    lv_obj_set_size(s_cap_dd_rec_mode, 160, 30);
    lv_obj_set_style_text_font(s_cap_dd_rec_mode, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(s_cap_dd_rec_mode, UI_COLOR_BG_PANEL, 0);
    lv_obj_set_style_text_color(s_cap_dd_rec_mode, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(s_cap_dd_rec_mode, UI_COLOR_BORDER, 0);
    lv_obj_add_event_cb(s_cap_dd_rec_mode, s_capture_dd_mode_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 录音按钮 */
    s_cap_btn_record = lv_btn_create(card_r);
    lv_obj_add_style(s_cap_btn_record, &g_ui_styles.btn, 0);
    lv_obj_add_style(s_cap_btn_record, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(s_cap_btn_record, 200, 48);
    lv_obj_set_style_bg_color(s_cap_btn_record, UI_COLOR_OK, 0);
    s_cap_lbl_rec_btn = lv_label_create(s_cap_btn_record);
    lv_label_set_text(s_cap_lbl_rec_btn, LV_SYMBOL_PLAY " START");
    lv_obj_set_style_text_font(s_cap_lbl_rec_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_cap_lbl_rec_btn,
                                lv_color_hex(0x000000), 0);
    lv_obj_center(s_cap_lbl_rec_btn);
    lv_obj_add_event_cb(s_cap_btn_record, s_capture_btn_record_cb,
                        LV_EVENT_CLICKED, NULL);

    /* 录音时长 (大号显示) */
    s_cap_lbl_duration = lv_label_create(card_r);
    lv_label_set_text(s_cap_lbl_duration, "00:00:00");
    lv_obj_add_style(s_cap_lbl_duration, &g_ui_styles.label_value_lg, 0);

    /* 录音数据量 */
    s_cap_lbl_rec_info = lv_label_create(card_r);
    lv_label_set_text(s_cap_lbl_rec_info, "0 KB  |  0 frames");
    lv_obj_add_style(s_cap_lbl_rec_info, &g_ui_styles.label_unit, 0);

    /* 录音状态 */
    s_cap_lbl_rec_state = lv_label_create(card_r);
    lv_label_set_text(s_cap_lbl_rec_state, "IDLE");
    lv_obj_add_style(s_cap_lbl_rec_state, &g_ui_styles.label_unit, 0);

    /* ---- 波束控向区 (嵌入录音卡片底部) ---- */
    {
        lv_obj_t *beam_box = lv_obj_create(card_r);
        lv_obj_remove_style_all(beam_box);
        lv_obj_set_size(beam_box, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(beam_box, 4, 0);
        lv_obj_set_style_pad_row(beam_box, 4, 0);
        lv_obj_set_style_border_width(beam_box, 1, 0);
        lv_obj_set_style_border_color(beam_box, UI_COLOR_BORDER, 0);
        lv_obj_set_style_radius(beam_box, UI_RADIUS_DEFAULT, 0);
        lv_obj_clear_flag(beam_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(beam_box, LV_FLEX_FLOW_COLUMN);

        /* 波束使能行 */
        row = lv_obj_create(beam_box);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        {
            lbl = lv_label_create(row);
            lv_label_set_text(lbl, "Beamforming:");
            lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
        }
        s_cap_sw_beamsteer = lv_switch_create(row);
        lv_obj_set_size(s_cap_sw_beamsteer, 40, 20);
        if (AI_BeamSteer_GetEnabled() != 0u)
        {
            lv_obj_add_state(s_cap_sw_beamsteer, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(s_cap_sw_beamsteer, s_capture_sw_beamsteer_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);

        /* 波束模式 + 方向行 */
        row = lv_obj_create(beam_box);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        s_cap_dd_beam_mode = lv_dropdown_create(row);
        lv_dropdown_set_options(s_cap_dd_beam_mode,
                                "Auto\nManual\nTrigger");
        lv_dropdown_set_selected(s_cap_dd_beam_mode,
                                  (uint16_t)AI_BeamSteer_GetMode());
        lv_obj_set_size(s_cap_dd_beam_mode, 100, 26);
        lv_obj_set_style_text_font(s_cap_dd_beam_mode,
                                    &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(s_cap_dd_beam_mode,
                                   UI_COLOR_BG_PANEL, 0);
        lv_obj_set_style_text_color(s_cap_dd_beam_mode,
                                     UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_border_color(s_cap_dd_beam_mode,
                                       UI_COLOR_BORDER, 0);
        lv_obj_add_event_cb(s_cap_dd_beam_mode, s_capture_dd_beam_mode_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);

        s_cap_lbl_beam_dir = lv_label_create(row);
        lv_label_set_text(s_cap_lbl_beam_dir, "Dir: --");
        lv_obj_add_style(s_cap_lbl_beam_dir, &g_ui_styles.label_unit, 0);
    }

    /* ================================================================
     *  SD 状态栏 (底部)
     * ================================================================ */
    sd_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(sd_bar);
    lv_obj_add_style(sd_bar, &g_ui_styles.toolbar, 0);
    lv_obj_set_size(sd_bar, 780, 48);
    lv_obj_align(sd_bar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_pad_all(sd_bar, 6, 0);
    lv_obj_set_style_pad_column(sd_bar, 12, 0);
    lv_obj_clear_flag(sd_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(sd_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sd_bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* SD 状态文字 */
    s_cap_lbl_sd_status = lv_label_create(sd_bar);
    lv_label_set_text(s_cap_lbl_sd_status, "SD: ---");
    lv_obj_add_style(s_cap_lbl_sd_status, &g_ui_styles.label_unit, 0);

    /* SD 使用量进度条 */
    s_cap_bar_sd_usage = lv_bar_create(sd_bar);
    lv_obj_set_size(s_cap_bar_sd_usage, 300, 14);
    lv_bar_set_range(s_cap_bar_sd_usage, 0, 100);
    lv_bar_set_value(s_cap_bar_sd_usage, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_cap_bar_sd_usage, UI_COLOR_BG_PANEL, 0);
    lv_obj_set_style_bg_color(s_cap_bar_sd_usage, UI_COLOR_ACCENT,
                              LV_PART_INDICATOR);

    /* SD 空间文字 */
    s_cap_lbl_sd_space = lv_label_create(sd_bar);
    lv_label_set_text(s_cap_lbl_sd_space, "-- / -- MB");
    lv_obj_add_style(s_cap_lbl_sd_space, &g_ui_styles.label_unit, 0);

    return scr;
}

static void s_capture_btn_back_cb(lv_event_t *e)
{
    (void)e;
    s_cap_rec_anim_stop();  /* 离开屏幕前停止动画 */
    App_UiScreens_Switch(APP_SCR_MAIN);
}

/** @brief 截图按钮回调 */
static void s_capture_btn_screenshot_cb(lv_event_t *e)
{
    (void)e;
    App_Capture_Trigger();
}

/** @brief 录音按钮回调 (切换 Start / Stop) */
static void s_capture_btn_record_cb(lv_event_t *e)
{
    App_RecorderState_t rec_state;
    uint16_t mode_idx;
    (void)e;

    rec_state = App_Recorder_GetState();
    if (rec_state == RECORDER_RECORDING)
    {
        /* 停止录音 */
        App_Storage_SendCmd(STORAGE_CMD_REC_STOP, 0u);
    }
    else if (rec_state == RECORDER_IDLE)
    {
        /* 开始录音 */
        mode_idx = lv_dropdown_get_selected(s_cap_dd_rec_mode);
        App_Storage_SendCmd(STORAGE_CMD_REC_START, (uint32_t)mode_idx);
    }
    /* STOPPING / ERROR: 忽略点击 */
}

/** @brief 录音模式下拉回调 */
static void s_capture_dd_mode_cb(lv_event_t *e)
{
    (void)e;
    /* 模式在录音启动时读取, 此处无需额外动作 */
}

/** @brief 波束使能开关回调 */
static void s_capture_sw_beamsteer_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    uint8_t en = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1u : 0u;
    AI_BeamSteer_SetEnabled(en);
}

/** @brief 波束模式下拉回调 */
static void s_capture_dd_beam_mode_cb(lv_event_t *e)
{
    uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    AI_BeamSteer_SetMode((AI_BeamSteer_Mode_t)sel);
}

static void s_capture_update(void)
{
    App_RecorderStats_t stats;
    App_SD_SpaceInfo_t  space;
    App_RecorderState_t rec_state;
    App_CaptureState_t  cap_state;
    App_StorageState_e  sto_state;
    float theta, phi;
    uint32_t h, m, sec;
    uint32_t now;
    char buf[64];
    int32_t used_pct;

    /* 500ms 节流 */
    now = lv_tick_get();
    if ((now - s_cap_last_update_tick) < 500u)
    {
        return;
    }
    s_cap_last_update_tick = now;

    /* ---- 截图状态 ---- */
    cap_state = App_Capture_GetState();
    if (s_cap_lbl_shot_count != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "Count: %u",
                       (unsigned)App_Capture_GetCount());
        lv_label_set_text(s_cap_lbl_shot_count, buf);
    }
    if (s_cap_lbl_shot_state != NULL)
    {
        sto_state = App_Storage_GetState();
        if (sto_state == STORAGE_STATE_CAPTURING)
        {
            lv_label_set_text(s_cap_lbl_shot_state, "Saving...");
            lv_obj_set_style_text_color(s_cap_lbl_shot_state,
                                         UI_COLOR_WARNING, 0);
        }
        else if (cap_state == CAPTURE_DONE)
        {
            lv_label_set_text(s_cap_lbl_shot_state, "Saved!");
            lv_obj_set_style_text_color(s_cap_lbl_shot_state,
                                         UI_COLOR_OK, 0);
        }
        else
        {
            lv_label_set_text(s_cap_lbl_shot_state, "Ready");
            lv_obj_set_style_text_color(s_cap_lbl_shot_state,
                                         UI_COLOR_TEXT_SECONDARY, 0);
        }
    }

    /* ---- 录音状态 ---- */
    rec_state = App_Recorder_GetState();
    App_Recorder_GetStats(&stats);

    /* 录音按钮外观 */
    if (s_cap_btn_record != NULL && s_cap_lbl_rec_btn != NULL)
    {
        if (rec_state == RECORDER_RECORDING)
        {
            lv_obj_set_style_bg_color(s_cap_btn_record, UI_COLOR_ERROR, 0);
            lv_label_set_text(s_cap_lbl_rec_btn, LV_SYMBOL_STOP " STOP");
            lv_obj_set_style_text_color(s_cap_lbl_rec_btn,
                                         lv_color_hex(0xFFFFFF), 0);
            s_cap_rec_anim_start();
            /* 录音中禁用模式下拉 */
            if (s_cap_dd_rec_mode != NULL)
            {
                lv_obj_add_state(s_cap_dd_rec_mode, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_set_style_bg_color(s_cap_btn_record, UI_COLOR_OK, 0);
            lv_label_set_text(s_cap_lbl_rec_btn, LV_SYMBOL_PLAY " START");
            lv_obj_set_style_text_color(s_cap_lbl_rec_btn,
                                         lv_color_hex(0x000000), 0);
            s_cap_rec_anim_stop();
            /* 恢复模式下拉 */
            if (s_cap_dd_rec_mode != NULL)
            {
                lv_obj_clear_state(s_cap_dd_rec_mode, LV_STATE_DISABLED);
            }
        }
    }

    /* 时长显示 */
    if (s_cap_lbl_duration != NULL)
    {
        h = stats.duration_ms / 3600000u;
        m = (stats.duration_ms / 60000u) % 60u;
        sec = (stats.duration_ms / 1000u) % 60u;
        (void)snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                       (unsigned)h, (unsigned)m, (unsigned)sec);
        lv_label_set_text(s_cap_lbl_duration, buf);
    }

    /* 数据量 */
    if (s_cap_lbl_rec_info != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%u KB  |  %u fr  |  drop:%u",
                       (unsigned)(stats.bytes_written / 1024u),
                       (unsigned)stats.frames_captured,
                       (unsigned)stats.dropped_frames);
        lv_label_set_text(s_cap_lbl_rec_info, buf);
    }

    /* 录音状态文字 */
    if (s_cap_lbl_rec_state != NULL)
    {
        switch (rec_state)
        {
        case RECORDER_RECORDING:
            lv_label_set_text(s_cap_lbl_rec_state, "RECORDING");
            lv_obj_set_style_text_color(s_cap_lbl_rec_state,
                                         UI_COLOR_ERROR, 0);
            break;
        case RECORDER_STOPPING:
            lv_label_set_text(s_cap_lbl_rec_state, "STOPPING...");
            lv_obj_set_style_text_color(s_cap_lbl_rec_state,
                                         UI_COLOR_WARNING, 0);
            break;
        case RECORDER_ERROR:
            lv_label_set_text(s_cap_lbl_rec_state, "ERROR");
            lv_obj_set_style_text_color(s_cap_lbl_rec_state,
                                         UI_COLOR_ERROR, 0);
            break;
        default:
            lv_label_set_text(s_cap_lbl_rec_state, "IDLE");
            lv_obj_set_style_text_color(s_cap_lbl_rec_state,
                                         UI_COLOR_TEXT_SECONDARY, 0);
            break;
        }
    }

    /* ---- 波束控向方向 ---- */
    if (s_cap_lbl_beam_dir != NULL)
    {
        AI_BeamSteer_GetDirection(&theta, &phi);
        (void)snprintf(buf, sizeof(buf), "Dir:%+.0f,%+.0f",
                       (double)theta, (double)phi);
        lv_label_set_text(s_cap_lbl_beam_dir, buf);
    }

    /* ---- SD 状态栏 ---- */
    if (s_cap_lbl_sd_status != NULL)
    {
        switch (App_SD_GetState())
        {
        case APP_SD_MOUNTED:
            lv_label_set_text(s_cap_lbl_sd_status, "SD: Mounted");
            lv_obj_set_style_text_color(s_cap_lbl_sd_status,
                                         UI_COLOR_OK, 0);
            break;
        case APP_SD_ERROR:
            lv_label_set_text(s_cap_lbl_sd_status, "SD: Error");
            lv_obj_set_style_text_color(s_cap_lbl_sd_status,
                                         UI_COLOR_ERROR, 0);
            break;
        default:
            lv_label_set_text(s_cap_lbl_sd_status, "SD: Not Mounted");
            lv_obj_set_style_text_color(s_cap_lbl_sd_status,
                                         UI_COLOR_INACTIVE, 0);
            break;
        }
    }

    App_SD_GetSpace(&space);
    if (s_cap_lbl_sd_space != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%u / %u MB free",
                       (unsigned)space.free_mb, (unsigned)space.total_mb);
        lv_label_set_text(s_cap_lbl_sd_space, buf);
    }
    if (s_cap_bar_sd_usage != NULL && space.total_mb > 0u)
    {
        used_pct = (int32_t)(100u - (space.free_mb * 100u / space.total_mb));
        lv_bar_set_value(s_cap_bar_sd_usage, used_pct, LV_ANIM_OFF);
        /* 使用量 >90% 时变红 */
        if (used_pct > 90)
        {
            lv_obj_set_style_bg_color(s_cap_bar_sd_usage, UI_COLOR_ERROR,
                                       LV_PART_INDICATOR);
        }
        else if (used_pct > 70)
        {
            lv_obj_set_style_bg_color(s_cap_bar_sd_usage, UI_COLOR_WARNING,
                                       LV_PART_INDICATOR);
        }
        else
        {
            lv_obj_set_style_bg_color(s_cap_bar_sd_usage, UI_COLOR_ACCENT,
                                       LV_PART_INDICATOR);
        }
    }
}

/* ============================================================================
 * Diagnostics 屏幕回调（占位 —— Phase 1.6 实现）
 * ============================================================================ */

/* ============================================================================
 * Diagnostics 屏幕 —— 性能图表 + 系统信息
 * ============================================================================ */

/** @brief 性能段短名表 (对应 APP_PERF_SEC_*) */
static const char * const s_perf_names[APP_PERF_SEC_COUNT] = {
    "AudTot", "Deint", "FFT", "SRP",
    "UILp",   "UISnp", "UIRnd",
    "DPrp",   "DNrm",  "DRnd", "DOvl", "DCmt"
};

static lv_obj_t *s_diag_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, LV_SYMBOL_LIST " Diagnostics");
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_PAD_NORMAL);
    }

    /* 性能条形图 */
    s_diag_chart = lv_chart_create(scr);
    lv_obj_set_size(s_diag_chart, 720, 180);
    lv_obj_align(s_diag_chart, LV_ALIGN_TOP_MID, 0, 36);
    lv_chart_set_type(s_diag_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_diag_chart, (uint16_t)APP_PERF_SEC_COUNT);
    lv_chart_set_range(s_diag_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 5000);
    lv_chart_set_div_line_count(s_diag_chart, 4, 0);
    lv_obj_set_style_bg_color(s_diag_chart, UI_COLOR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_diag_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_diag_chart, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_diag_chart, 1, 0);
    lv_obj_set_style_line_color(s_diag_chart, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_radius(s_diag_chart, UI_RADIUS_DEFAULT, 0);
    lv_obj_set_style_size(s_diag_chart, 0, LV_PART_INDICATOR);
    s_diag_perf_ser = lv_chart_add_series(s_diag_chart,
                                           UI_COLOR_ACCENT,
                                           LV_CHART_AXIS_PRIMARY_Y);

    /* 性能段名称标签 */
    {
        lv_obj_t *name_row = lv_obj_create(scr);
        uint32_t pi;
        lv_obj_remove_style_all(name_row);
        lv_obj_set_size(name_row, 720, LV_SIZE_CONTENT);
        lv_obj_align(name_row, LV_ALIGN_TOP_MID, 0, 220);
        lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(name_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(name_row, LV_OBJ_FLAG_SCROLLABLE);

        for (pi = 0u; pi < (uint32_t)APP_PERF_SEC_COUNT; pi++)
        {
            lv_obj_t *lbl = lv_label_create(name_row);
            lv_label_set_text(lbl, s_perf_names[pi]);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
        }
    }

    /* 系统信息面板 */
    {
        lv_obj_t *panel = lv_obj_create(scr);
        lv_obj_remove_style_all(panel);
        lv_obj_add_style(panel, &g_ui_styles.panel, 0);
        lv_obj_set_size(panel, 720, 150);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 244);
        lv_obj_set_style_pad_all(panel, UI_PAD_NORMAL, 0);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        s_diag_lbl_info = lv_label_create(panel);
        lv_obj_set_width(s_diag_lbl_info, 700);
        lv_label_set_long_mode(s_diag_lbl_info, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(s_diag_lbl_info, &g_ui_styles.label_unit, 0);
        lv_label_set_text(s_diag_lbl_info,
            "MCU: STM32H743IIT6 @ 480 MHz  |  RTOS: FreeRTOS v10.3.1\n"
            "Audio: 16ch PDM, 48kHz TDM16  |  SRP-PHAT (129 pts)\n"
            "Display: 800x480 LTDC+DMA2D  |  CLI: 921600 baud\n"
            "UI FPS: --  |  Audio FPS: --  |  SAI: ---");
    }

    /* 返回按钮 */
    {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_t *lbl;
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 100, 32);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_diag_btn_back_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

static void s_diag_btn_back_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_MAIN);
}

static void s_diag_update(void)
{
    char buf[256];
    App_Perf_SectionSummary_t summary;
    uint32_t i;
    lv_coord_t max_val = 100;

    /* 更新性能条形图 */
    if (s_diag_chart != NULL && s_diag_perf_ser != NULL)
    {
        for (i = 0u; i < (uint32_t)APP_PERF_SEC_COUNT; i++)
        {
            lv_coord_t val = 0;
            if (App_Perf_GetSectionSummary((App_Perf_Section_t)i, &summary) != 0u)
            {
                val = (lv_coord_t)(summary.avg_us);
            }
            s_diag_perf_ser->y_points[i] = val;
            if (val > max_val) { max_val = val; }
        }
        /* 动态调整 Y 轴范围 */
        lv_chart_set_range(s_diag_chart, LV_CHART_AXIS_PRIMARY_Y, 0,
                           (lv_coord_t)(max_val + max_val / 4));
        lv_chart_refresh(s_diag_chart);
    }

    /* 更新系统信息文本 */
    if (s_diag_lbl_info != NULL)
    {
        (void)snprintf(buf, sizeof(buf),
            "MCU: STM32H743IIT6 @ 480 MHz  |  RTOS: FreeRTOS v10.3.1\n"
            "Audio: 16ch PDM, 48kHz TDM16  |  SRP-PHAT (129 pts)\n"
            "Display: 800x480 LTDC+DMA2D  |  CLI: 921600 baud\n"
            "UI FPS: %lu  |  Audio FPS: %lu  |  SAI: %s  |  Mode: %s",
            (unsigned long)s_live_data.ui_fps,
            (unsigned long)s_live_data.audio_fps,
            s_live_data.sai_active ? "OK" : "OFF",
            (App_RuntimeConfig_GetDisplayMode() == APP_RUNTIME_DISP_MODE_FAST)
                ? "Fast"
                : (App_RuntimeConfig_GetDisplayMode() == APP_RUNTIME_DISP_MODE_BALANCED)
                    ? "Balanced" : "Clean");
        lv_label_set_text(s_diag_lbl_info, buf);
    }
}

/* ============================================================================
 * Guide 屏幕 —— 使用指南
 * ============================================================================ */

static lv_obj_t *s_guide_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, LV_SYMBOL_FILE " Usage Guide");
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_PAD_NORMAL);
    }

    /* 指南内容（可滚动） */
    {
        lv_obj_t *panel = lv_obj_create(scr);
        lv_obj_remove_style_all(panel);
        lv_obj_add_style(panel, &g_ui_styles.panel, 0);
        lv_obj_set_size(panel, 760, 380);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_set_style_pad_all(panel, UI_PAD_NORMAL, 0);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *txt = lv_label_create(panel);
        lv_obj_set_width(txt, 740);
        lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(txt, &g_ui_styles.label_unit, 0);
        lv_label_set_text(txt,
            "NECCS Acoustic Camera - 16ch Sound Source Localization\n"
            "=====================================================\n\n"
            "Main Screen:\n"
            "  Left area: Real-time acoustic heatmap overlay\n"
            "  Right panel: Spectrum + angle/energy readouts\n"
            "  Bottom: Status information and mode display\n\n"
            "Touch Controls:\n"
            "  [Settings] - Open display parameter settings\n"
            "  [Home]     - Return to the startup screen\n"
            "  [Diag]     - View system diagnostics\n\n"
            "Settings:\n"
            "  Display Mode: Fast (high FPS) / Balanced / Clean\n"
            "  Gamma: Heatmap contrast adjustment (0.5~3.0)\n"
            "  Noise Gate: Background noise suppression level\n"
            "  Bilinear Interp: Smooth heatmap interpolation\n"
            "  Fine Fusion: Enable fine-scan sub-grid fusion\n\n"
            "CLI Commands (UART 921600 baud, 8N1):\n"
            "  srp scan         - Run coarse SRP scan\n"
            "  srp scan_fine    - Run fine SRP scan\n"
            "  srp alpha <val>  - Set SRP smoothing alpha\n"
            "  cfg perf on/off  - Enable/disable perf profiling\n"
            "  cfg perf dump    - Print detailed timing report\n"
            "  cfg noise on/off - Enable/disable noise floor\n"
            "  disp mode <mode> - Switch display mode\n"
            "  sys reboot       - System reset\n"
            "  sys heap         - Print heap usage\n\n"
            "Hardware:\n"
            "  MCU: STM32H743IIT6 (Cortex-M7, 480 MHz)\n"
            "  Microphones: 16x PCMD3180 PDM array\n"
            "  Display: 800x480 TFT-LCD (LTDC+DMA2D)\n"
            "  Touch: GT9xxx / FT5206 (Software I2C)\n"
            "  Debug: ST-Link V2/V3");
    }

    /* Back 按钮 */
    {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(btn, 100, 36);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -(lv_coord_t)UI_PAD_LARGE);
        {
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
            lv_obj_center(lbl);
        }
        lv_obj_add_event_cb(btn, s_guide_btn_back_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

static void s_guide_btn_back_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_SETTINGS);
}

static void s_guide_update(void)
{
}

#endif /* APP_LVGL_ENABLE */
