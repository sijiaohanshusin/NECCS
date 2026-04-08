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
#include "app_user_config.h"
#include "gui_guider.h"

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

/* Settings —— Phase 1.5 实现（占位） */
static lv_obj_t *s_settings_create(void);
static void      s_settings_update(void);
static void      s_settings_btn_back_cb(lv_event_t *e);

/* Capture —— Phase 4 实现（占位） */
static lv_obj_t *s_capture_create(void);
static void      s_capture_update(void);
static void      s_capture_btn_back_cb(lv_event_t *e);

/* Diagnostics —— Phase 1.6 实现（占位） */
static lv_obj_t *s_diag_create(void);
static void      s_diag_update(void);
static void      s_diag_btn_back_cb(lv_event_t *e);

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

/* ---- 时间序列声纹图表 ---- */
#define TSERIES_POINTS  60u   /**< 记录点数（约 2~4 秒） */
static lv_obj_t       *s_main_chart = NULL;
static lv_chart_series_t *s_main_chart_ser_energy = NULL;
static lv_coord_t      s_tseries_buf[TSERIES_POINTS];
static uint16_t        s_tseries_head = 0u;

/** @brief 实时数据缓存 */
static App_UiLiveData_t s_live_data;
/** @brief 实时数据更新标志 */
static uint8_t s_live_data_dirty = 0u;

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
 * @brief 创建 Main View 工作界面
 * @return 屏幕根对象
 *
 * 布局：
 * ┌────────────────────┬──────┐
 * │    status bar (800×28)     │
 * ├─────────────┬──────┤      │
 * │             │ right│      │
 * │  chroma key │ panel│      │
 * │  640×420    │160×420│      │
 * │  (透过热力图)│(读数) │      │
 * ├─────────────┴──────┤      │
 * │    toolbar (800×32)        │
 * └────────────────────┴──────┘
 */
static lv_obj_t *s_main_create(void)
{
    lv_obj_t *scr;
    lv_obj_t *area_left;

    /* ---- 屏幕根对象 ---- */
    scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 顶部状态栏 (800 × UI_STATUSBAR_H) ---- */
    s_main_statusbar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_statusbar);
    lv_obj_add_style(s_main_statusbar, &g_ui_styles.statusbar, 0);
    lv_obj_set_size(s_main_statusbar, 800, UI_STATUSBAR_H);
    lv_obj_set_pos(s_main_statusbar, 0, 0);
    lv_obj_clear_flag(s_main_statusbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_main_statusbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_main_statusbar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 状态栏标签 */
    s_main_lbl_mode = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_mode, "NECCS");
    lv_obj_add_style(s_main_lbl_mode, &g_ui_styles.label_title, 0);

    s_main_lbl_sai = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_sai, "SAI: ---");
    lv_obj_add_style(s_main_lbl_sai, &g_ui_styles.label_unit, 0);

    s_main_lbl_fps = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_fps, "-- FPS");
    lv_obj_add_style(s_main_lbl_fps, &g_ui_styles.label_unit, 0);

    /* ---- 左侧 chroma key 透明区域 (640 × content_h) ---- */
    {
        lv_coord_t content_h = (lv_coord_t)(480u - UI_STATUSBAR_H - UI_TOOLBAR_H);

        area_left = lv_obj_create(scr);
        lv_obj_remove_style_all(area_left);
        lv_obj_set_size(area_left, 640, content_h);
        lv_obj_set_pos(area_left, 0, (lv_coord_t)UI_STATUSBAR_H);
        lv_obj_set_style_bg_color(area_left, UI_COLOR_CHROMA_KEY, 0);
        lv_obj_set_style_bg_opa(area_left, LV_OPA_COVER, 0);
        lv_obj_clear_flag(area_left, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    /* ---- 右侧面板 (160 × content_h) ---- */
    {
        lv_coord_t content_h = (lv_coord_t)(480u - UI_STATUSBAR_H - UI_TOOLBAR_H);

        s_main_right_panel = lv_obj_create(scr);
        lv_obj_remove_style_all(s_main_right_panel);
        lv_obj_add_style(s_main_right_panel, &g_ui_styles.panel, 0);
        lv_obj_set_size(s_main_right_panel, APP_DISPLAY_UI_PANEL_W, content_h);
        lv_obj_set_pos(s_main_right_panel, 640, (lv_coord_t)UI_STATUSBAR_H);
        lv_obj_clear_flag(s_main_right_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(s_main_right_panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_main_right_panel, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(s_main_right_panel, UI_PAD_SMALL, 0);

        /* 声源角度读数 */
        {
            lv_obj_t *lbl_hdr = lv_label_create(s_main_right_panel);
            lv_label_set_text(lbl_hdr, "Direction");
            lv_obj_add_style(lbl_hdr, &g_ui_styles.label_title, 0);
        }
        s_main_lbl_angle = lv_label_create(s_main_right_panel);
        lv_label_set_text(s_main_lbl_angle, "-- , --");
        lv_obj_add_style(s_main_lbl_angle, &g_ui_styles.label_value, 0);

        /* 能量读数 */
        {
            lv_obj_t *lbl_hdr = lv_label_create(s_main_right_panel);
            lv_label_set_text(lbl_hdr, "Energy");
            lv_obj_add_style(lbl_hdr, &g_ui_styles.label_title, 0);
        }
        s_main_lbl_energy = lv_label_create(s_main_right_panel);
        lv_label_set_text(s_main_lbl_energy, "---");
        lv_obj_add_style(s_main_lbl_energy, &g_ui_styles.label_value, 0);

        /* dB 值 */
        {
            lv_obj_t *lbl_hdr = lv_label_create(s_main_right_panel);
            lv_label_set_text(lbl_hdr, "Level");
            lv_obj_add_style(lbl_hdr, &g_ui_styles.label_title, 0);
        }
        s_main_lbl_db = lv_label_create(s_main_right_panel);
        lv_label_set_text(s_main_lbl_db, "-- dB");
        lv_obj_add_style(s_main_lbl_db, &g_ui_styles.label_value, 0);

        /* 时间序列能量图表 */
        {
            lv_obj_t *lbl_hdr = lv_label_create(s_main_right_panel);
            lv_label_set_text(lbl_hdr, "Trend");
            lv_obj_add_style(lbl_hdr, &g_ui_styles.label_title, 0);
        }
        s_main_chart = lv_chart_create(s_main_right_panel);
        lv_obj_set_size(s_main_chart, (lv_coord_t)(APP_DISPLAY_UI_PANEL_W - 20u), 80);
        lv_chart_set_type(s_main_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(s_main_chart, TSERIES_POINTS);
        lv_chart_set_range(s_main_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(s_main_chart, 3, 0);
        lv_obj_set_style_bg_color(s_main_chart, UI_COLOR_BG_PANEL, 0);
        lv_obj_set_style_bg_opa(s_main_chart, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_main_chart, UI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(s_main_chart, 1, 0);
        lv_obj_set_style_line_color(s_main_chart, lv_color_hex(0x333355), LV_PART_MAIN);
        lv_obj_set_style_size(s_main_chart, 0, LV_PART_INDICATOR); /* 隐藏点标记 */
        s_main_chart_ser_energy = lv_chart_add_series(s_main_chart,
                                     lv_color_hex(0x00D4FF),
                                     LV_CHART_AXIS_PRIMARY_Y);
        /* 初始化序列缓冲区 */
        (void)memset(s_tseries_buf, 0, sizeof(s_tseries_buf));
        s_tseries_head = 0u;

        /* 分隔线 */
        {
            lv_obj_t *sep = lv_obj_create(s_main_right_panel);
            lv_obj_remove_style_all(sep);
            lv_obj_set_size(sep, (lv_coord_t)(APP_DISPLAY_UI_PANEL_W - 16u), 1);
            lv_obj_set_style_bg_color(sep, UI_COLOR_BORDER, 0);
            lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
        }

        /* 设置按钮 */
        {
            lv_obj_t *btn = lv_btn_create(s_main_right_panel);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, (lv_coord_t)(APP_DISPLAY_UI_PANEL_W - 16u), 32);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_SETTINGS " Setup");
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_settings_cb, LV_EVENT_CLICKED, NULL);
        }

        /* 首页按钮 */
        {
            lv_obj_t *btn = lv_btn_create(s_main_right_panel);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, (lv_coord_t)(APP_DISPLAY_UI_PANEL_W - 16u), 32);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_HOME " Home");
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_home_cb, LV_EVENT_CLICKED, NULL);
        }

        /* 诊断按钮 */
        {
            lv_obj_t *btn = lv_btn_create(s_main_right_panel);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, (lv_coord_t)(APP_DISPLAY_UI_PANEL_W - 16u), 32);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, LV_SYMBOL_LIST " Diag");
                lv_obj_center(lbl);
            }
            lv_obj_add_event_cb(btn, s_main_btn_diag_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    /* ---- 底部工具栏 (800 × UI_TOOLBAR_H) ---- */
    s_main_toolbar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_toolbar);
    lv_obj_add_style(s_main_toolbar, &g_ui_styles.toolbar, 0);
    lv_obj_set_size(s_main_toolbar, 800, UI_TOOLBAR_H);
    lv_obj_set_pos(s_main_toolbar, 0, (lv_coord_t)(480u - UI_TOOLBAR_H));
    lv_obj_clear_flag(s_main_toolbar, LV_OBJ_FLAG_SCROLLABLE);

    /* 工具栏暂时放置一个 dB 色阶提示文本 */
    {
        lv_obj_t *lbl = lv_label_create(s_main_toolbar);
        lv_label_set_text(lbl, "dB:  Low " LV_SYMBOL_RIGHT " High");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, UI_PAD_NORMAL, 0);
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

/**
 * @brief Main View 周期更新
 * @details 更新状态栏的 FPS、SAI 状态和声源定位读数。
 */
static void s_main_update(void)
{
    char buf[32];
    float db_val;

    if (s_live_data_dirty == 0u)
    {
        return;
    }
    s_live_data_dirty = 0u;

    /* 状态栏 FPS */
    if (s_main_lbl_fps != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%lu FPS",
                       (unsigned long)s_live_data.ui_fps);
        lv_label_set_text(s_main_lbl_fps, buf);
    }

    /* 状态栏 SAI */
    if (s_main_lbl_sai != NULL)
    {
        lv_label_set_text(s_main_lbl_sai,
                          s_live_data.sai_active ? "SAI: OK" : "SAI: ---");
        lv_obj_set_style_text_color(s_main_lbl_sai,
                                    s_live_data.sai_active ? UI_COLOR_OK : UI_COLOR_INACTIVE,
                                    0);
    }

    /* 声源角度 */
    if (s_main_lbl_angle != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%+.1f, %+.1f",
                       (double)s_live_data.x_angle,
                       (double)s_live_data.y_angle);
        lv_label_set_text(s_main_lbl_angle, buf);
    }

    /* 能量 */
    if (s_main_lbl_energy != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%.3f",
                       (double)s_live_data.energy);
        lv_label_set_text(s_main_lbl_energy, buf);
    }

    /* dB 值 (20*log10(energy), 避免 log(0)) */
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
    }

    /* 时间序列图表更新 */
    if (s_main_chart != NULL && s_main_chart_ser_energy != NULL)
    {
        lv_coord_t val = (lv_coord_t)(s_live_data.energy * 100.0f);
        if (val > 100) { val = 100; }
        if (val < 0)   { val = 0; }
        lv_chart_set_next_value(s_main_chart, s_main_chart_ser_energy, val);
    }
}

/* ============================================================================
 * Settings 屏幕回调（占位 —— Phase 1.5 实现）
 * ============================================================================ */

static lv_obj_t *s_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, LV_SYMBOL_SETTINGS " Settings");
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_PAD_LARGE);
    }

    /* 返回按钮 */
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
        lv_obj_add_event_cb(btn, s_settings_btn_back_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

static void s_settings_btn_back_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_MAIN);
}

static void s_settings_update(void)
{
}

/* ============================================================================
 * Capture 屏幕回调（占位 —— Phase 4 实现）
 * ============================================================================ */

static lv_obj_t *s_capture_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, LV_SYMBOL_SAVE " Capture");
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_PAD_LARGE);
    }

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
        lv_obj_add_event_cb(btn, s_capture_btn_back_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

static void s_capture_btn_back_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_MAIN);
}

static void s_capture_update(void)
{
}

/* ============================================================================
 * Diagnostics 屏幕回调（占位 —— Phase 1.6 实现）
 * ============================================================================ */

static lv_obj_t *s_diag_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, LV_SYMBOL_LIST " Diagnostics");
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UI_PAD_LARGE);
    }

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
}

#endif /* APP_LVGL_ENABLE */
