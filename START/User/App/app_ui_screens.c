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

/** @brief 简体中文字体声明 (lv_font_conv 生成, SimHei) */
LV_FONT_DECLARE(lv_font_sc_14);
LV_FONT_DECLARE(lv_font_sc_12);

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

/* 多模式侧边栏创建 + 切换 */
static void      s_sidebar_create_main(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
static void      s_sidebar_create_night(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
static void      s_sidebar_create_dirrec(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
static void      s_sidebar_switch_mode(App_OperatingMode_t mode);
static void      s_mode_btn_cb(lv_event_t *e);

/* 夜间模式回调 */
static void      s_night_slider_cx_cb(lv_event_t *e);
static void      s_night_slider_cy_cb(lv_event_t *e);
static void      s_night_btn_center_cb(lv_event_t *e);
static void      s_night_btn_track_cb(lv_event_t *e);

/* 定向录音模式回调 */
static void      s_dirrec_btn_record_cb(lv_event_t *e);
static void      s_dirrec_btn_reset_sel_cb(lv_event_t *e);
static void      s_dirrec_sel_labels_refresh(void);
static void      s_dirrec_slider_sel_cb(lv_event_t *e);

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

/* ---- 多模式侧边栏面板 (预创建, hide/show 切换) ---- */
static lv_obj_t *s_sidebar_main    = NULL;  /**< 主模式面板容器 */
static lv_obj_t *s_sidebar_night   = NULL;  /**< 夜间模式面板容器 */
static lv_obj_t *s_sidebar_dirrec  = NULL;  /**< 定向录音面板容器 */

/* ---- 夜间模式特有控件 ---- */
static lv_obj_t *s_night_lbl_angle    = NULL;
static lv_obj_t *s_night_lbl_db       = NULL;
static lv_obj_t *s_night_lbl_energy   = NULL;
static lv_obj_t *s_night_slider_cx    = NULL;  /**< 十字准星 X 滑块 */
static lv_obj_t *s_night_slider_cy    = NULL;  /**< 十字准星 Y 滑块 */
static lv_obj_t *s_night_lbl_cx       = NULL;  /**< 准星 X 坐标标签 */
static lv_obj_t *s_night_lbl_cy       = NULL;  /**< 准星 Y 坐标标签 */

/* ---- 定向录音模式特有控件 ---- */
static lv_obj_t *s_dirrec_lbl_angle   = NULL;
static lv_obj_t *s_dirrec_lbl_db      = NULL;
static lv_obj_t *s_dirrec_lbl_energy  = NULL;
static lv_obj_t *s_dirrec_btn_record  = NULL;  /**< 录音按钮 */
static lv_obj_t *s_dirrec_lbl_rec_btn = NULL;  /**< 录音按钮文字 */
static lv_obj_t *s_dirrec_dd_rec_mode = NULL;  /**< 录音模式下拉 */
static lv_obj_t *s_dirrec_lbl_sd_status = NULL; /**< SD卡状态标签 */
static lv_obj_t *s_dirrec_lbl_dur     = NULL;  /**< 录音时长 */
static lv_obj_t *s_dirrec_lbl_data    = NULL;  /**< 录音数据量 */
static lv_obj_t *s_dirrec_lbl_sel_xy  = NULL;  /**< 选区坐标显示 */
static lv_obj_t *s_dirrec_lbl_sel_sz  = NULL;  /**< 选区大小显示 */
static lv_obj_t *s_dirrec_slider_x1   = NULL;  /**< 选区 X1 滑块 */
static lv_obj_t *s_dirrec_slider_y1   = NULL;  /**< 选区 Y1 滑块 */
static lv_obj_t *s_dirrec_slider_x2   = NULL;  /**< 选区 X2 滑块 */
static lv_obj_t *s_dirrec_slider_y2   = NULL;  /**< 选区 Y2 滑块 */

/* ---- 工具栏模式按钮 ---- */
static lv_obj_t *s_mode_btn           = NULL;
static lv_obj_t *s_mode_btn_lbl       = NULL;

#define UI_SCREEN_W     ((lv_coord_t)APP_DISPLAY_TARGET_SCREEN_W)
#define UI_SCREEN_H     ((lv_coord_t)APP_DISPLAY_TARGET_SCREEN_H)
#define UI_MAIN_LEFT_W  ((lv_coord_t)APP_DISPLAY_CAMERA_VIEW_W)
#define UI_MAIN_LEFT_H  ((lv_coord_t)APP_DISPLAY_CAMERA_VIEW_H)

/* ---- 十字准星全局坐标 (共享给 app_display.c) ---- */
/* 十字准星全局变量 (仅 UI_Task 读写，跨模块 extern) */
uint16_t g_crosshair_x = (APP_DISPLAY_CAMERA_VIEW_W / 2u);
uint16_t g_crosshair_y = (APP_DISPLAY_CAMERA_VIEW_H / 2u);
uint8_t  g_crosshair_enable = 0u; /**< 十字准星使能 */

/* ---- 选区全局坐标 (共享给 app_display.c) ---- */
#define SEL_MAX_X       (APP_DISPLAY_CAMERA_VIEW_W - 1u) /**< 选区 X 最大值 */
#define SEL_MAX_Y       (APP_DISPLAY_CAMERA_VIEW_H - 1u) /**< 选区 Y 最大值 */
#define SEL_MIN_SIZE     20u    /**< 选区最小宽/高 (pixels) */
#define SEL_DEFAULT_X1  (APP_DISPLAY_CAMERA_VIEW_W / 4u)
#define SEL_DEFAULT_Y1  (APP_DISPLAY_CAMERA_VIEW_H / 4u)
#define SEL_DEFAULT_X2  ((APP_DISPLAY_CAMERA_VIEW_W * 3u) / 4u)
#define SEL_DEFAULT_Y2  ((APP_DISPLAY_CAMERA_VIEW_H * 3u) / 4u)

uint16_t g_select_x1 = SEL_DEFAULT_X1;
uint16_t g_select_y1 = SEL_DEFAULT_Y1;
uint16_t g_select_x2 = SEL_DEFAULT_X2;
uint16_t g_select_y2 = SEL_DEFAULT_Y2;
uint8_t  g_select_enable = 0u;

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
static lv_obj_t *s_settings_slider_opacity = NULL;
static lv_obj_t *s_settings_lbl_opacity   = NULL;

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
static void s_settings_slider_opacity_cb(lv_event_t *e);
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

    /* 防止重复初始化：若已初始化则直接返回 */
    if (s_inited != 0u)
    {
        return;
    }

    /* 清零所有屏幕对象指针（懒加载模式：NULL = 尚未创建）*/
    (void)memset(s_screen_obj, 0, sizeof(s_screen_obj));

    /* 初始化工业深色主题样式表（全局共享 g_ui_styles）*/
    App_UiStyles_Init();

    /* 懒加载创建 Home 屏幕并立即显示（作为启动画面）*/
    (void)s_ensure_created(APP_SCR_HOME);
    if (s_screen_obj[APP_SCR_HOME] != NULL)
    {
        lv_scr_load(s_screen_obj[APP_SCR_HOME]);   /* 切换 LVGL 当前屏幕到 Home */
    }

    s_current_screen = APP_SCR_HOME;   /* 记录当前活跃屏幕 ID */
    s_inited = 1u;                     /* 标记初始化完成：避免重复调用 */
    (void)i;   /* 消除未使用变量警告 */
}

void App_UiScreens_Switch(App_ScreenId_t id)
{
    lv_obj_t *scr;

    /* 屏幕 ID 范围检查：超出 APP_SCR_COUNT 的值无对应注册表项 */
    if (id >= APP_SCR_COUNT)
    {
        return;
    }
    /* 已在目标屏幕则不触发重复切换（避免 LVGL 执行不必要的 scr_load）*/
    if (id == s_current_screen)
    {
        return;
    }

    /* 懒加载：若目标屏幕尚未创建过 LVGL 对象则立即创建 */
    scr = s_ensure_created(id);
    if (scr == NULL)
    {
        return;   /* 创建失败（内存不足或布局错误）则放弃切换 */
    }

    lv_scr_load(scr);       /* 切换 LVGL 当前屏幕（带默认过渡动画）*/
    s_current_screen = id;  /* 更新当前屏幕 ID 缓存 */
}

App_ScreenId_t App_UiScreens_GetCurrent(void)
{
    return s_current_screen;
}

void App_UiScreens_Update(void)
{
    /* 若未初始化则直接返回（防止在 LVGL 启动前被调用）*/
    if (s_inited == 0u)
    {
        return;
    }
    /* 调用当前活跃屏幕的 update 回调（刷新实时数据标签/进度条等）
     * 注意：s_screen_ops 是 const 虚表，每个屏幕独立实现 update
     * [注意] 仅当 live_data 标记为 dirty 时回调内部才执行刷新（避免每帧重绘）
     */
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
        s_live_data = *data;          /* 将外部实时数据拷贝到模块私有缓存 */
        s_live_data_dirty = 1u;       /* 置脏标志，通知下一次 Update() 刷新控件 */
    }
}

/* ============================================================================
 * Home 屏幕回调 (GUI Guider wrapper)
 * ============================================================================ */

static lv_obj_t *s_home_create(void)
{
    /* === 第1步: 使用 GUI Guider 生成的布局函数创建 Home 屏幕 ===
     * setup_scr_home() 由 NXP GUI Guider 工具自动生成，负责创建所有 LVGL 控件，
     * 并将指针存入 guider_ui 结构体。先清零以防止残留脏指针引发错误。 */
    (void)memset(&guider_ui, 0, sizeof(guider_ui));
    setup_scr_home(&guider_ui);

    /* === 第2步: 替换 GUI Guider 旧事件回调 ===
     * setup_scr_home() 内部调用了 events_init_home()，注册了旧事件回调。
     * 旧回调试图加载 guider_ui.using（nullptr）会触发 HardFault，
     * 因此先批量移除（NULL 代表移除所有同类回调），再绑定自定义回调。 */
    if (guider_ui.home_btn_1 != NULL)
    {
        lv_obj_remove_event_cb(guider_ui.home_btn_1, NULL);   /* 移除所有旧点击回调 */
        lv_obj_add_event_cb(guider_ui.home_btn_1, s_home_btn_start_cb,
                            LV_EVENT_CLICKED, NULL);           /* 绑定"开始"导航回调 */
    }
    if (guider_ui.home_btn_2 != NULL)
    {
        lv_obj_remove_event_cb(guider_ui.home_btn_2, NULL);   /* 移除所有旧点击回调 */
        lv_obj_add_event_cb(guider_ui.home_btn_2, s_home_btn_settings_cb,
                            LV_EVENT_CLICKED, NULL);           /* 绑定"设置展开"回调 */
    }
    if (guider_ui.home_cont_1 != NULL)
    {
        lv_obj_remove_event_cb(guider_ui.home_cont_1, NULL);  /* 移除所有旧点击回调 */
        lv_obj_add_event_cb(guider_ui.home_cont_1, s_home_cont_click_cb,
                            LV_EVENT_CLICKED, NULL);           /* 绑定"点击关闭"回调 */
    }

    return guider_ui.home;  /* 返回屏幕根对象，由 App_UiScreens_Switch() 加载显示 */
}

static void s_home_update(void)
{
    /* Home 屏幕是纯静态欢迎界面，不包含周期性刷新元素，无需任何操作 */
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
        lv_obj_clear_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN); /* 展开下拉设置容器 */
    }
    else
    {
        lv_obj_add_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);   /* 收起下拉设置容器 */
    }
}

/** @brief Home 下拉容器点击回调 → 仅当点击容器自身（非子控件）时关闭 */
static void s_home_cont_click_cb(lv_event_t *e)
{
    lv_obj_t *target  = lv_event_get_target(e);         /* 实际接收事件的对象（可能是子控件）*/
    lv_obj_t *current = lv_event_get_current_target(e); /* 注册回调的对象（容器本身）*/
    if (target == current)
    {
        /* 只有点击容器空白区域才关闭；点击子控件时 target != current，不关闭 */
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
    lv_coord_t content_h = UI_MAIN_LEFT_H;                           /* 主内容区高度 = 屏幕高 - 状态栏 - 工具栏 */
    lv_coord_t panel_w   = (lv_coord_t)APP_DISPLAY_UI_PANEL_W;      /* 右侧面板宽度（160px 由配置定义）*/

    /* ---- 屏幕根对象 ---- */
    scr = lv_obj_create(NULL);                                        /* NULL 表示创建顶级屏幕，非某子容器 */
    lv_obj_add_style(scr, &g_ui_styles.scr_bg, 0);                  /* 应用背景色样式（深色 #111111）*/
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);                 /* 禁止屏幕整体滚动 */

    /* ================================================================
     *  顶部状态栏 (800 × UI_STATUSBAR_H) —— 增强版
     * ================================================================ */
    s_main_statusbar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_statusbar);                       /* 清除默认内边距/边框 */
    lv_obj_add_style(s_main_statusbar, &g_ui_styles.statusbar, 0);  /* 应用状态栏样式（深色背景）*/
    lv_obj_set_size(s_main_statusbar, UI_SCREEN_W, UI_STATUSBAR_H); /* 800×28 px */
    lv_obj_set_pos(s_main_statusbar, 0, 0);                         /* 固定到屏幕顶部 */
    lv_obj_clear_flag(s_main_statusbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_main_statusbar, LV_FLEX_FLOW_ROW);        /* 水平排列子控件 */
    lv_obj_set_flex_align(s_main_statusbar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);/* 均匀分布，垂直居中 */

    /* 品牌名 (点击返回 Home) */
    s_main_lbl_mode = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_mode, "NECCS");
    lv_obj_add_style(s_main_lbl_mode, &g_ui_styles.label_title, 0); /* 14pt 粗体 */
    lv_obj_set_style_text_color(s_main_lbl_mode, UI_COLOR_ACCENT, 0);/* 主题强调色（青绿）*/
    lv_obj_add_flag(s_main_lbl_mode, LV_OBJ_FLAG_CLICKABLE);        /* 标签设为可点击 */
    lv_obj_add_event_cb(s_main_lbl_mode, s_main_btn_home_cb, LV_EVENT_CLICKED, NULL); /* 点击回到 Home */

    /* 触发状态 */
    s_main_lbl_trig = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_trig, "TRIG:IDLE");                /* 初始为空闲状态 */
    lv_obj_set_style_text_font(s_main_lbl_trig, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_INACTIVE, 0); /* 灰色=未激活 */

    /* SAI 状态 */
    s_main_lbl_sai = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_sai, "SAI:---");                   /* "---"代表初始化中 */
    lv_obj_set_style_text_font(s_main_lbl_sai, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_sai, UI_COLOR_INACTIVE, 0);

    /* 激光/夜间模式指示 */
    s_main_lbl_laser = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_laser, "");                        /* 默认空字符串，激活时填文字 */
    lv_obj_set_style_text_font(s_main_lbl_laser, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_laser, UI_COLOR_INACTIVE, 0);

    /* FPS */
    s_main_lbl_fps = lv_label_create(s_main_statusbar);
    lv_label_set_text(s_main_lbl_fps, "-- FPS");                    /* 未知 FPS 显示为 "--" */
    lv_obj_set_style_text_font(s_main_lbl_fps, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_main_lbl_fps, UI_COLOR_TEXT_SECONDARY, 0);

    /* ================================================================
     *  左侧 chroma key 透明区域 (640 × content_h)
     *  该区域填充 UI_COLOR_CHROMA_KEY 颜色，App_Display 渲染时会对
     *  该颜色做色键处理，使摄像头热力图透过显示。
     * ================================================================ */
    area_left = lv_obj_create(scr);
    lv_obj_remove_style_all(area_left);
    lv_obj_set_size(area_left, UI_MAIN_LEFT_W, content_h);          /* 640 × 内容高 */
    lv_obj_set_pos(area_left, 0, (lv_coord_t)UI_STATUSBAR_H);       /* 紧接状态栏下方 */
    lv_obj_set_style_bg_color(area_left, UI_COLOR_CHROMA_KEY, 0);   /* 色键颜色（纯绿 0x07E0）*/
    lv_obj_set_style_bg_opa(area_left, LV_OPA_COVER, 0);            /* 不透明，确保 chroma-key 生效 */
    lv_obj_clear_flag(area_left, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE); /* 不可滚动/点击 */

    /* ================================================================
     *  右侧面板 (160 × content_h) —— 多模式侧边栏容器
     *  三个侧边栏子控件（主/夜间/定向录音）都创建在此容器内，
     *  通过 HIDDEN flag 切换显示。
     * ================================================================ */
    s_main_right_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_right_panel);
    lv_obj_set_size(s_main_right_panel, panel_w, content_h);        /* 160 × 内容高 */
    lv_obj_set_pos(s_main_right_panel, UI_MAIN_LEFT_W, (lv_coord_t)UI_STATUSBAR_H); /* 紧贴左区右侧 */
    lv_obj_set_style_bg_color(s_main_right_panel, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(s_main_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_main_right_panel, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_main_right_panel, 1, 0);
    lv_obj_set_style_border_side(s_main_right_panel, LV_BORDER_SIDE_LEFT, 0); /* 仅左侧边框分隔线 */
    lv_obj_set_style_pad_all(s_main_right_panel, 0, 0);             /* 无内边距，子控件自己管理间距 */
    lv_obj_clear_flag(s_main_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 预创建三个模式面板, 通过 HIDDEN flag 切换 */
    s_sidebar_create_main(s_main_right_panel, panel_w, content_h);   /* 主模式：读数+频谱+按钮 */
    s_sidebar_create_night(s_main_right_panel, panel_w, content_h);  /* 夜间模式：读数+准星控制 */
    s_sidebar_create_dirrec(s_main_right_panel, panel_w, content_h); /* 定向录音：录音控制+选区 */

    /* 初始显示主模式 */
    s_sidebar_switch_mode(APP_MODE_MAIN);                            /* 隐藏另外两个面板 */

    /* ================================================================
     *  底部工具栏 (800 × UI_TOOLBAR_H) —— 模式按钮 + 管线信息
     * ================================================================ */
    s_main_toolbar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_main_toolbar);
    lv_obj_add_style(s_main_toolbar, &g_ui_styles.toolbar, 0);
    lv_obj_set_size(s_main_toolbar, UI_SCREEN_W, UI_TOOLBAR_H);     /* 800×28 px */
    lv_obj_set_pos(s_main_toolbar, 0, (lv_coord_t)(APP_DISPLAY_TARGET_SCREEN_H - UI_TOOLBAR_H)); /* 贴底 */
    lv_obj_clear_flag(s_main_toolbar, LV_OBJ_FLAG_SCROLLABLE);

    {
        /* 模式切换按钮 (M/N/R) — 循环切换主/夜间/定向录音模式 */
        s_mode_btn = lv_btn_create(s_main_toolbar);
        lv_obj_add_style(s_mode_btn, &g_ui_styles.btn, 0);
        lv_obj_add_style(s_mode_btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(s_mode_btn, 26, 22);
        lv_obj_align(s_mode_btn, LV_ALIGN_LEFT_MID, 4, 0);
        s_mode_btn_lbl = lv_label_create(s_mode_btn);
        lv_label_set_text(s_mode_btn_lbl, "M");                     /* M=Main / N=Night / R=Record */
        lv_obj_set_style_text_font(s_mode_btn_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_mode_btn_lbl, UI_COLOR_ACCENT, 0);
        lv_obj_center(s_mode_btn_lbl);
        lv_obj_add_event_cb(s_mode_btn, s_mode_btn_cb, LV_EVENT_CLICKED, NULL);

        /* 管线信息标签：显示算法模式 + 采样率 + 通道数 + 频段 */
        s_main_lbl_toolbar_info = lv_label_create(s_main_toolbar);
        lv_label_set_text(s_main_lbl_toolbar_info,
                          "FAST " LV_SYMBOL_RIGHT " 48kHz 16ch SRP-PHAT"); /* 初始默认值 */
        lv_obj_add_style(s_main_lbl_toolbar_info, &g_ui_styles.label_unit, 0);
        lv_obj_align(s_main_lbl_toolbar_info, LV_ALIGN_LEFT_MID, 36, 0); /* 按钮右侧 36px 偏移 */
    }

    return scr;  /* 返回屏幕对象，由 App_UiScreens_Switch() 传递给 lv_scr_load() */
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

/** @brief Capture 按钮 → 直接触发截图 (不再导航到 Capture 屏幕) */
static void s_main_btn_capture_cb(lv_event_t *e)
{
    (void)e;
    App_Capture_Trigger();
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
        return; /* 数据未更新，跳过本帧刷新，避免不必要的 LVGL 重绘 */
    }
    s_live_data_dirty = 0u; /* 清除脏标志，允许下次写入时再触发刷新 */

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
        /* sai_active 由 Audio_Pipeline_Task 以 RTOS 通知方式同步，
         * OK = 绿色强调色，断线 = 灰色不活跃色 */
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
            lv_label_set_text(s_main_lbl_trig, "TRIG:ARM"); /* 已武装：黄色警告 */
            lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_WARNING, 0);
            break;
        case APP_TRIGGER_TRIGGERED:
            lv_label_set_text(s_main_lbl_trig, "TRIG:HIT"); /* 已触发：红色报警 */
            lv_obj_set_style_text_color(s_main_lbl_trig, UI_COLOR_ERROR, 0);
            break;
        default:
            lv_label_set_text(s_main_lbl_trig, "TRIG:IDLE"); /* 空闲：灰色不活跃 */
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
            lv_obj_set_style_bg_color(s_main_btn_trigger, UI_COLOR_WARNING, 0); /* 黄色背景 */
            lv_obj_set_style_bg_opa(s_main_btn_trigger, LV_OPA_COVER, 0);
            break;
        case APP_TRIGGER_TRIGGERED:
            lv_obj_set_style_bg_color(s_main_btn_trigger, UI_COLOR_ERROR, 0);   /* 红色背景 */
            lv_obj_set_style_bg_opa(s_main_btn_trigger, LV_OPA_COVER, 0);
            break;
        default:
            lv_obj_set_style_bg_color(s_main_btn_trigger, UI_COLOR_BG_PANEL, 0);/* 深灰背景 */
            lv_obj_set_style_bg_opa(s_main_btn_trigger, LV_OPA_COVER, 0);
            break;
        }
    }

    /* ---- 状态栏 激光/夜间 ---- */
    if (s_main_lbl_laser != NULL)
    {
        if (s_live_data.night_mode != 0u)
        {
            lv_label_set_text(s_main_lbl_laser, "NIGHT");            /* 夜间模式开启 */
            lv_obj_set_style_text_color(s_main_lbl_laser, UI_COLOR_WARNING, 0);
        }
        else if (s_live_data.laser_on != 0u)
        {
            lv_label_set_text(s_main_lbl_laser, "LASER");            /* 激光准星开启 */
            lv_obj_set_style_text_color(s_main_lbl_laser, UI_COLOR_OK, 0);
        }
        else
        {
            lv_label_set_text(s_main_lbl_laser, "");                 /* 两者均关闭，不显示文字 */
        }
    }

    /* ---- 声源角度 (14px title) ---- */
    if (s_main_lbl_angle != NULL)
    {
        /* x_angle/y_angle 来自 SRP-PHAT 输出，单位：度，范围 ±90° */
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
            /* energy 为 SRP 归一化后的线性幅度 [0,1]，转 dB = 20·log10(energy)
             * 注意：energy=1 时为 0 dB 满量程，energy<约 6.3e-4 时低于 -64 dB */
            db_val = 20.0f * log10f(s_live_data.energy);
            (void)snprintf(buf, sizeof(buf), "%.1f dB", (double)db_val);
        }
        else
        {
            (void)snprintf(buf, sizeof(buf), "-inf dB"); /* 极小值直接显示 -inf */
        }
        lv_label_set_text(s_main_lbl_db, buf);

        /* dB 值动态三色着色阈值:
         *   > -6 dB  → 红色（高强度，可能过载）
         *  -20~-6 dB → 黄色（中等强度）
         *   < -20 dB → 绿色（低强度正常）*/
        if (s_live_data.energy > 1.0e-6f)
        {
            if (db_val > -6.0f)
            {
                lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_METER_HIGH, 0); /* 红 */
            }
            else if (db_val > -20.0f)
            {
                lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_METER_MID, 0);  /* 黄 */
            }
            else
            {
                lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_METER_LOW, 0);  /* 绿 */
            }
        }
        else
        {
            lv_obj_set_style_text_color(s_main_lbl_db, UI_COLOR_INACTIVE, 0); /* 灰（低于噪底）*/
        }
    }

    /* ---- 能量 (12px) ---- */
    if (s_main_lbl_energy != NULL)
    {
        /* 直接显示线性能量，3位小数，供调试时参考归一化比值 */
        (void)snprintf(buf, sizeof(buf), "E: %.3f",
                       (double)s_live_data.energy);
        lv_label_set_text(s_main_lbl_energy, buf);
    }

    /* ---- 频谱面板实时更新 ---- */
    if (App_Spectrum_GetLatestFrame(&spec_frame) != 0u)
    {
        /* 获取最新频谱快照并推送给频谱面板控件更新显示 */
        App_UiSpecPanel_Update(&spec_frame);
    }

    /* ---- 工具栏：模式 + 频段 ---- */
    if (s_main_lbl_toolbar_info != NULL)
    {
        switch (App_RuntimeConfig_GetDisplayMode())
        {
        case APP_RUNTIME_DISP_MODE_FAST:     mode_str = "FAST"; break; /* 快速模式：无平滑 */
        case APP_RUNTIME_DISP_MODE_BALANCED: mode_str = "BAL";  break; /* 平衡模式：适度平滑 */
        case APP_RUNTIME_DISP_MODE_CLEAN:    mode_str = "CLN";  break; /* 清洁模式：强平滑 */
        default:                             mode_str = "???";  break;
        }
        App_RuntimeConfig_GetFreqBand(&freq_lo, &freq_hi); /* 获取当前 SRP-PHAT 频段 FFT bin 索引 */
        (void)snprintf(buf, sizeof(buf), "%s " LV_SYMBOL_RIGHT " 48k 16ch  %d-%dHz",
                       mode_str,
                       (int)App_Spectrum_BinToHz(freq_lo), /* bin→Hz 换算：freq(Hz)=bin×Fs/NFFT */
                       (int)App_Spectrum_BinToHz(freq_hi));
        lv_label_set_text(s_main_lbl_toolbar_info, buf);
    }

    /* ---- 夜间模式读数镜像更新 ---- */
    /* [注意] 夜间模式侧边栏与主模式侧边栏显示同一套实时数据，故逐字段同步 */
    if (s_night_lbl_angle != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "Dir:%+.0f,%+.0f",
                       (double)s_live_data.x_angle,
                       (double)s_live_data.y_angle);
        lv_label_set_text(s_night_lbl_angle, buf);
    }
    if (s_night_lbl_db != NULL)
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
        lv_label_set_text(s_night_lbl_db, buf);
    }
    if (s_night_lbl_energy != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "E: %.3f",
                       (double)s_live_data.energy);
        lv_label_set_text(s_night_lbl_energy, buf);
    }

    /* ---- 定向录音模式读数镜像更新 ---- */
    if (s_dirrec_lbl_angle != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "Dir:%+.0f,%+.0f",
                       (double)s_live_data.x_angle,
                       (double)s_live_data.y_angle);
        lv_label_set_text(s_dirrec_lbl_angle, buf);
    }
    if (s_dirrec_lbl_db != NULL)
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
        lv_label_set_text(s_dirrec_lbl_db, buf);
    }
    if (s_dirrec_lbl_energy != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "E: %.3f",
                       (double)s_live_data.energy);
        lv_label_set_text(s_dirrec_lbl_energy, buf);
    }

    /* ---- 定向录音: SD 状态指示 ---- */
    if (s_dirrec_lbl_sd_status != NULL)
    {
        App_SD_MountState_t sd_mount = App_SD_GetState();   /* SD 卡挂载状态 */
        App_StorageState_e  sd_st    = App_Storage_GetState(); /* 存储任务当前状态 */

        if (sd_mount != APP_SD_MOUNTED)
        {
            /* SD 卡未挂载 — 最高优先级指示 */
            lv_label_set_text(s_dirrec_lbl_sd_status,
                "SD \xE6\x9C\xAA\xE5\xB0\xB1\xE7\xBB\xAA");  /* SD 未就绪 (UTF-8 inline) */
            lv_obj_set_style_text_color(s_dirrec_lbl_sd_status, UI_COLOR_ERROR, 0);
        }
        else if (sd_st == STORAGE_STATE_ERROR)
        {
            lv_label_set_text(s_dirrec_lbl_sd_status,
                "SD \xE9\x94\x99\xE8\xAF\xAF");  /* SD 错误 */
            lv_obj_set_style_text_color(s_dirrec_lbl_sd_status, UI_COLOR_ERROR, 0);
        }
        else if (sd_st == STORAGE_STATE_RECORDING)
        {
            lv_label_set_text(s_dirrec_lbl_sd_status,
                "SD \xE5\xBD\x95\xE5\x88\xB6\xE4\xB8\xAD");  /* SD 录制中 */
            lv_obj_set_style_text_color(s_dirrec_lbl_sd_status, UI_COLOR_WARNING, 0);
        }
        else if (sd_st == STORAGE_STATE_CAPTURING)
        {
            lv_label_set_text(s_dirrec_lbl_sd_status,
                "SD \xE6\x88\xAA\xE5\x9B\xBE\xE4\xB8\xAD");  /* SD 截图中 */
            lv_obj_set_style_text_color(s_dirrec_lbl_sd_status, UI_COLOR_WARNING, 0);
        }
        else
        {
            lv_label_set_text(s_dirrec_lbl_sd_status,
                "SD \xE5\xB0\xB1\xE7\xBB\xAA");  /* SD 就绪 */
            lv_obj_set_style_text_color(s_dirrec_lbl_sd_status, UI_COLOR_OK, 0);
        }
    }

    /* ---- 定向录音: 按钮/状态 同步 ---- */
    {
        static App_RecorderState_t s_prev_rec_state = RECORDER_IDLE; /* 避免每帧都重绘按钮 */
        App_RecorderState_t cur = App_Recorder_GetState();
        if (cur != s_prev_rec_state)
        {
            s_prev_rec_state = cur;
            if (s_dirrec_lbl_rec_btn != NULL && s_dirrec_btn_record != NULL)
            {
                if (cur == RECORDER_RECORDING)
                {
                    /* 录音进行中：按钮变红色 STOP */
                    lv_label_set_text(s_dirrec_lbl_rec_btn,
                        LV_SYMBOL_STOP " \xE5\x81\x9C\xE6\xAD\xA2\xE5\xBD\x95\xE9\x9F\xB3"); /* 停止录音 */
                    lv_obj_set_style_bg_color(s_dirrec_btn_record, UI_COLOR_ERROR, 0);
                }
                else
                {
                    /* 录音停止：按钮变绿色 PLAY */
                    lv_label_set_text(s_dirrec_lbl_rec_btn,
                        LV_SYMBOL_PLAY " \xE5\xBC\x80\xE5\xA7\x8B\xE5\xBD\x95\xE9\x9F\xB3"); /* 开始录音 */
                    lv_obj_set_style_bg_color(s_dirrec_btn_record, UI_COLOR_OK, 0);
                }
            }
        }

        /* Duration + data label update during recording */
        if (cur == RECORDER_RECORDING && s_dirrec_lbl_dur != NULL)
        {
            App_RecorderStats_t st;
            uint32_t sec, min, hr;
            App_Recorder_GetStats(&st);
            sec = st.duration_ms / 1000u;   /* 毫秒→秒 */
            min = sec / 60u;                /* 秒→分钟 */
            hr  = min / 60u;               /* 分钟→小时 */
            (void)snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                           (unsigned long)hr,
                           (unsigned long)(min % 60u),   /* 剩余分钟（去掉小时部分）*/
                           (unsigned long)(sec % 60u));  /* 剩余秒数（去掉分钟部分）*/
            lv_label_set_text(s_dirrec_lbl_dur, buf);
            if (s_dirrec_lbl_data != NULL)
            {
                /* 字节数 >= 1 MB 时以 MB 显示，否则以 KB 显示 */
                if (st.bytes_written >= 1048576u)
                    (void)snprintf(buf, sizeof(buf), "%.1f MB | %lu frm",
                                   (double)st.bytes_written / 1048576.0,
                                   (unsigned long)st.frames_captured);
                else
                    (void)snprintf(buf, sizeof(buf), "%lu KB | %lu frm",
                                   (unsigned long)(st.bytes_written / 1024u),
                                   (unsigned long)st.frames_captured);
                lv_label_set_text(s_dirrec_lbl_data, buf);
            }
        }
    }
}

/* ============================================================================
 * Settings 屏幕回调（占位 —— Phase 1.5 实现）
 * ============================================================================ */

/* ============================================================================
 * 多模式侧边栏创建 + 切换
 * ============================================================================ */

/** @brief 创建一组读数标签 (角度+dB+能量) 并返回尾部用于追加 */
static void s_make_readings_box(lv_obj_t *parent, lv_coord_t w,
                                lv_obj_t **out_angle, lv_obj_t **out_db,
                                lv_obj_t **out_energy)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, &g_ui_styles.panel, 0);
    lv_obj_set_size(box, w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_pad_row(box, 1, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

    *out_angle = lv_label_create(box);
    lv_label_set_text(*out_angle, "Dir: -- , --");
    lv_obj_add_style(*out_angle, &g_ui_styles.label_title, 0);

    *out_db = lv_label_create(box);
    lv_label_set_text(*out_db, "-- dB");
    lv_obj_add_style(*out_db, &g_ui_styles.label_value_lg, 0);

    *out_energy = lv_label_create(box);
    lv_label_set_text(*out_energy, "E: ---");
    lv_obj_add_style(*out_energy, &g_ui_styles.label_unit, 0);
}

/** @brief 创建快捷按钮行 (截图/触发/激光/设置/诊断) */
static void s_make_btn_row(lv_obj_t *parent, lv_coord_t w)
{
    lv_obj_t *btn_row = lv_obj_create(parent);
    lv_obj_t *btn, *lbl;
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(btn_row, 3, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 📸 截图 */
    btn = lv_btn_create(btn_row);
    lv_obj_add_style(btn, &g_ui_styles.btn, 0);
    lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 26, 26);
    lbl = lv_label_create(btn); lv_label_set_text(lbl, LV_SYMBOL_IMAGE); lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, s_main_btn_capture_cb, LV_EVENT_CLICKED, NULL);

    /* ⏱ 触发 */
    btn = lv_btn_create(btn_row);
    lv_obj_add_style(btn, &g_ui_styles.btn, 0);
    lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 26, 26);
    lbl = lv_label_create(btn); lv_label_set_text(lbl, LV_SYMBOL_PLAY); lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, s_main_btn_trigger_cb, LV_EVENT_CLICKED, NULL);
    s_main_btn_trigger = btn;

    /* 🔦 激光 */
    btn = lv_btn_create(btn_row);
    lv_obj_add_style(btn, &g_ui_styles.btn, 0);
    lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 26, 26);
    lbl = lv_label_create(btn); lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN); lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, s_main_btn_laser_cb, LV_EVENT_CLICKED, NULL);

    /* ⚙ 设置 */
    btn = lv_btn_create(btn_row);
    lv_obj_add_style(btn, &g_ui_styles.btn, 0);
    lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 26, 26);
    lbl = lv_label_create(btn); lv_label_set_text(lbl, LV_SYMBOL_SETTINGS); lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, s_main_btn_settings_cb, LV_EVENT_CLICKED, NULL);

    /* ℹ 诊断 */
    btn = lv_btn_create(btn_row);
    lv_obj_add_style(btn, &g_ui_styles.btn, 0);
    lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btn, 26, 26);
    lbl = lv_label_create(btn); lv_label_set_text(lbl, LV_SYMBOL_LIST); lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, s_main_btn_diag_cb, LV_EVENT_CLICKED, NULL);
}

/** @brief 主模式侧边栏: 读数 + 频谱 + 快捷按钮 */
static void s_sidebar_create_main(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *spacer;
    lv_coord_t btn_w = (lv_coord_t)(w - 8u);

    s_sidebar_main = lv_obj_create(parent);
    lv_obj_remove_style_all(s_sidebar_main);
    lv_obj_set_size(s_sidebar_main, w, h);
    lv_obj_set_pos(s_sidebar_main, 0, 0);
    lv_obj_set_style_pad_all(s_sidebar_main, UI_PAD_SMALL, 0);
    lv_obj_set_style_pad_row(s_sidebar_main, 2, 0);
    lv_obj_clear_flag(s_sidebar_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_sidebar_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sidebar_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_make_readings_box(s_sidebar_main, btn_w, &s_main_lbl_angle, &s_main_lbl_db, &s_main_lbl_energy);

    /* 频谱面板 */
    {
        lv_coord_t spec_h = (lv_coord_t)(h - 80 - 34 - 16);
        (void)App_UiSpecPanel_Create(s_sidebar_main, btn_w, spec_h);
    }

    spacer = lv_obj_create(s_sidebar_main);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);

    s_make_btn_row(s_sidebar_main, btn_w);
}

/** @brief 夜间模式侧边栏: 读数 + 激光状态 + 十字准星控制 + 快捷按钮 */
static void s_sidebar_create_night(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *box, *lbl, *spacer, *row_btns;
    lv_coord_t btn_w = (lv_coord_t)(w - 8u);
    char buf[32];

    s_sidebar_night = lv_obj_create(parent);
    lv_obj_remove_style_all(s_sidebar_night);
    lv_obj_set_size(s_sidebar_night, w, h);
    lv_obj_set_pos(s_sidebar_night, 0, 0);
    lv_obj_set_style_pad_all(s_sidebar_night, UI_PAD_SMALL, 0);
    lv_obj_set_style_pad_row(s_sidebar_night, 2, 0);
    lv_obj_clear_flag(s_sidebar_night, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_sidebar_night, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sidebar_night, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 模式标题 */
    lbl = lv_label_create(s_sidebar_night);
    lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN " \xE5\xA4\x9C\xE9\x97\xB4\xE6\xA8\xA1\xE5\xBC\x8F");  /* 夜间模式 UTF-8 */
    lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_WARNING, 0);

    /* 读数面板 */
    s_make_readings_box(s_sidebar_night, btn_w, &s_night_lbl_angle, &s_night_lbl_db, &s_night_lbl_energy);

    /* 激光状态 */
    box = lv_obj_create(s_sidebar_night);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, &g_ui_styles.panel, 0);
    lv_obj_set_size(box, btn_w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_pad_row(box, 1, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    {
        lbl = lv_label_create(box);
        lv_label_set_text(lbl, "\xE6\xBF\x80\xE5\x85\x89: ON");  /* 激光: ON */
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_OK, 0);
    }

    /* 十字准星控制 */
    box = lv_obj_create(s_sidebar_night);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, &g_ui_styles.panel, 0);
    lv_obj_set_size(box, btn_w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    {
        lbl = lv_label_create(box);
        lv_label_set_text(lbl, "\xE5\x87\x86\xE6\x98\x9F\xE4\xBD\x8D\xE7\xBD\xAE");  /* 准星位置 */
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);

        /* X 准星 */
        lbl = lv_label_create(box);
        lv_label_set_text(lbl, "X:");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
        s_night_slider_cx = lv_slider_create(box);
        lv_obj_set_width(s_night_slider_cx, (lv_coord_t)(btn_w - 12));
        lv_slider_set_range(s_night_slider_cx, 0, (int32_t)SEL_MAX_X);
        lv_slider_set_value(s_night_slider_cx, (int32_t)g_crosshair_x, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_night_slider_cx, s_night_slider_cx_cb, LV_EVENT_VALUE_CHANGED, NULL);
        s_night_lbl_cx = lv_label_create(box);
        (void)snprintf(buf, sizeof(buf), "X: %u", (unsigned)g_crosshair_x);
        lv_label_set_text(s_night_lbl_cx, buf);
        lv_obj_add_style(s_night_lbl_cx, &g_ui_styles.label_unit, 0);

        /* Y 准星 */
        lbl = lv_label_create(box);
        lv_label_set_text(lbl, "Y:");
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
        s_night_slider_cy = lv_slider_create(box);
        lv_obj_set_width(s_night_slider_cy, (lv_coord_t)(btn_w - 12));
        lv_slider_set_range(s_night_slider_cy, 0, (int32_t)SEL_MAX_Y);
        lv_slider_set_value(s_night_slider_cy, (int32_t)g_crosshair_y, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_night_slider_cy, s_night_slider_cy_cb, LV_EVENT_VALUE_CHANGED, NULL);
        s_night_lbl_cy = lv_label_create(box);
        (void)snprintf(buf, sizeof(buf), "Y: %u", (unsigned)g_crosshair_y);
        lv_label_set_text(s_night_lbl_cy, buf);
        lv_obj_add_style(s_night_lbl_cy, &g_ui_styles.label_unit, 0);

        /* 居中 / 跟踪 按钮行 */
        row_btns = lv_obj_create(box);
        lv_obj_remove_style_all(row_btns);
        lv_obj_set_size(row_btns, btn_w - 12, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row_btns, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row_btns, 4, 0);
        lv_obj_clear_flag(row_btns, LV_OBJ_FLAG_SCROLLABLE);
        {
            lv_obj_t *btn;
            btn = lv_btn_create(row_btns);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_set_size(btn, 60, 24);
            lbl = lv_label_create(btn);
            lv_label_set_text(lbl, "\xE5\xB1\x85\xE4\xB8\xAD");  /* 居中 */
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, s_night_btn_center_cb, LV_EVENT_CLICKED, NULL);

            btn = lv_btn_create(row_btns);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_set_size(btn, 60, 24);
            lbl = lv_label_create(btn);
            lv_label_set_text(lbl, "\xE8\xB7\x9F\xE8\xB8\xAA");  /* 跟踪 */
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, s_night_btn_track_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    spacer = lv_obj_create(s_sidebar_night);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);

    s_make_btn_row(s_sidebar_night, btn_w);
}

/** @brief 定向录音模式侧边栏: 读数 + 录音控制 + 选区信息 + 快捷按钮 */
static void s_sidebar_create_dirrec(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *box, *lbl, *spacer;
    lv_coord_t btn_w = (lv_coord_t)(w - 8u);

    s_sidebar_dirrec = lv_obj_create(parent);
    lv_obj_remove_style_all(s_sidebar_dirrec);
    lv_obj_set_size(s_sidebar_dirrec, w, h);
    lv_obj_set_pos(s_sidebar_dirrec, 0, 0);
    lv_obj_set_style_pad_all(s_sidebar_dirrec, UI_PAD_SMALL, 0);
    lv_obj_set_style_pad_row(s_sidebar_dirrec, 2, 0);
    lv_obj_clear_flag(s_sidebar_dirrec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_sidebar_dirrec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sidebar_dirrec, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 模式标题 */
    lbl = lv_label_create(s_sidebar_dirrec);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " \xE5\xAE\x9A\xE5\x90\x91\xE5\xBD\x95\xE9\x9F\xB3");  /* 定向录音 */
    lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_ERROR, 0);

    /* 读数面板 */
    s_make_readings_box(s_sidebar_dirrec, btn_w, &s_dirrec_lbl_angle, &s_dirrec_lbl_db, &s_dirrec_lbl_energy);

    /* 录音控制 */
    box = lv_obj_create(s_sidebar_dirrec);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, &g_ui_styles.panel, 0);
    lv_obj_set_size(box, btn_w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    {
        lbl = lv_label_create(box);
        lv_label_set_text(lbl, "\xE5\xBD\x95\xE9\x9F\xB3\xE6\x8E\xA7\xE5\x88\xB6");  /* 录音控制 */
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);

        /* 模式选择 */
        s_dirrec_dd_rec_mode = lv_dropdown_create(box);
        lv_dropdown_set_options(s_dirrec_dd_rec_mode, "MONO (\xE6\xB3\xA2\xE6\x9D\x9F)\nRAW16 (16ch)");
        lv_obj_set_width(s_dirrec_dd_rec_mode, (lv_coord_t)(btn_w - 12));
        lv_obj_set_style_text_font(s_dirrec_dd_rec_mode, &lv_font_sc_12, 0);

        /* SD 卡状态指示 */
        s_dirrec_lbl_sd_status = lv_label_create(box);
        lv_label_set_text(s_dirrec_lbl_sd_status, "SD ...");
        lv_obj_add_style(s_dirrec_lbl_sd_status, &g_ui_styles.label_unit, 0);

        /* 大录音按钮 */
        s_dirrec_btn_record = lv_btn_create(box);
        lv_obj_add_style(s_dirrec_btn_record, &g_ui_styles.btn, 0);
        lv_obj_set_size(s_dirrec_btn_record, (lv_coord_t)(btn_w - 12), 32);
        lv_obj_set_style_bg_color(s_dirrec_btn_record, UI_COLOR_OK, 0);
        s_dirrec_lbl_rec_btn = lv_label_create(s_dirrec_btn_record);
        lv_label_set_text(s_dirrec_lbl_rec_btn, LV_SYMBOL_PLAY " \xE5\xBC\x80\xE5\xA7\x8B\xE5\xBD\x95\xE9\x9F\xB3");  /* 开始录音 */
        lv_obj_center(s_dirrec_lbl_rec_btn);
        lv_obj_add_event_cb(s_dirrec_btn_record, s_dirrec_btn_record_cb, LV_EVENT_CLICKED, NULL);

        /* 时长 + 数据量显示 */
        s_dirrec_lbl_dur = lv_label_create(box);
        lv_label_set_text(s_dirrec_lbl_dur, "00:00:00");
        lv_obj_add_style(s_dirrec_lbl_dur, &g_ui_styles.label_value, 0);

        s_dirrec_lbl_data = lv_label_create(box);
        lv_label_set_text(s_dirrec_lbl_data, "0 B | 0 frm");
        lv_obj_add_style(s_dirrec_lbl_data, &g_ui_styles.label_unit, 0);
    }

    /* 选区信息 */
    box = lv_obj_create(s_sidebar_dirrec);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, &g_ui_styles.panel, 0);
    lv_obj_set_size(box, btn_w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_style_pad_row(box, 1, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    {
        lv_obj_t *btn;
        lbl = lv_label_create(box);
        lv_label_set_text(lbl, "\xE9\x80\x89\xE5\x8C\xBA\xE8\x8C\x83\xE5\x9B\xB4");  /* 选区范围 */
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);

        s_dirrec_lbl_sel_xy = lv_label_create(box);
        lv_label_set_text(s_dirrec_lbl_sel_xy, "(192,136)-(576,408)");
        lv_obj_add_style(s_dirrec_lbl_sel_xy, &g_ui_styles.label_unit, 0);

        s_dirrec_lbl_sel_sz = lv_label_create(box);
        lv_label_set_text(s_dirrec_lbl_sel_sz, "384x272 px");
        lv_obj_add_style(s_dirrec_lbl_sel_sz, &g_ui_styles.label_unit, 0);

        /* ---- 选区调节滑块 ---- */
        {
            lv_coord_t sl_w = (lv_coord_t)(btn_w - 40);

            lbl = lv_label_create(box);
            lv_label_set_text(lbl, "X1");
            lv_obj_add_style(lbl, &g_ui_styles.label_small, 0);
            s_dirrec_slider_x1 = lv_slider_create(box);
            lv_slider_set_range(s_dirrec_slider_x1, 0, (int32_t)(SEL_MAX_X - SEL_MIN_SIZE));
            lv_slider_set_value(s_dirrec_slider_x1, (int32_t)g_select_x1, LV_ANIM_OFF);
            lv_obj_set_size(s_dirrec_slider_x1, sl_w, 10);
            lv_obj_add_event_cb(s_dirrec_slider_x1, s_dirrec_slider_sel_cb,
                                LV_EVENT_VALUE_CHANGED, NULL);

            lbl = lv_label_create(box);
            lv_label_set_text(lbl, "Y1");
            lv_obj_add_style(lbl, &g_ui_styles.label_small, 0);
            s_dirrec_slider_y1 = lv_slider_create(box);
            lv_slider_set_range(s_dirrec_slider_y1, 0, (int32_t)(SEL_MAX_Y - SEL_MIN_SIZE));
            lv_slider_set_value(s_dirrec_slider_y1, (int32_t)g_select_y1, LV_ANIM_OFF);
            lv_obj_set_size(s_dirrec_slider_y1, sl_w, 10);
            lv_obj_add_event_cb(s_dirrec_slider_y1, s_dirrec_slider_sel_cb,
                                LV_EVENT_VALUE_CHANGED, NULL);

            lbl = lv_label_create(box);
            lv_label_set_text(lbl, "X2");
            lv_obj_add_style(lbl, &g_ui_styles.label_small, 0);
            s_dirrec_slider_x2 = lv_slider_create(box);
            lv_slider_set_range(s_dirrec_slider_x2, (int32_t)SEL_MIN_SIZE, (int32_t)SEL_MAX_X);
            lv_slider_set_value(s_dirrec_slider_x2, (int32_t)g_select_x2, LV_ANIM_OFF);
            lv_obj_set_size(s_dirrec_slider_x2, sl_w, 10);
            lv_obj_add_event_cb(s_dirrec_slider_x2, s_dirrec_slider_sel_cb,
                                LV_EVENT_VALUE_CHANGED, NULL);

            lbl = lv_label_create(box);
            lv_label_set_text(lbl, "Y2");
            lv_obj_add_style(lbl, &g_ui_styles.label_small, 0);
            s_dirrec_slider_y2 = lv_slider_create(box);
            lv_slider_set_range(s_dirrec_slider_y2, (int32_t)SEL_MIN_SIZE, (int32_t)SEL_MAX_Y);
            lv_slider_set_value(s_dirrec_slider_y2, (int32_t)g_select_y2, LV_ANIM_OFF);
            lv_obj_set_size(s_dirrec_slider_y2, sl_w, 10);
            lv_obj_add_event_cb(s_dirrec_slider_y2, s_dirrec_slider_sel_cb,
                                LV_EVENT_VALUE_CHANGED, NULL);
        }

        btn = lv_btn_create(box);
        lv_obj_add_style(btn, &g_ui_styles.btn, 0);
        lv_obj_set_size(btn, (lv_coord_t)(btn_w - 12), 24);
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "\xE9\x87\x8D\xE7\xBD\xAE\xE9\x80\x89\xE5\x8C\xBA");  /* 重置选区 */
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_dirrec_btn_reset_sel_cb, LV_EVENT_CLICKED, NULL);
    }

    spacer = lv_obj_create(s_sidebar_dirrec);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);

    s_make_btn_row(s_sidebar_dirrec, btn_w);
}

/** @brief 切换侧边栏显示 (hide all, show target) */
static void s_sidebar_switch_mode(App_OperatingMode_t mode)
{
    /* 先全部隐藏，再按 mode 显示目标面板，用 HIDDEN flag 代替 del/create 避免内存碎片 */
    if (s_sidebar_main != NULL)   lv_obj_add_flag(s_sidebar_main,   LV_OBJ_FLAG_HIDDEN);
    if (s_sidebar_night != NULL)  lv_obj_add_flag(s_sidebar_night,  LV_OBJ_FLAG_HIDDEN);
    if (s_sidebar_dirrec != NULL) lv_obj_add_flag(s_sidebar_dirrec, LV_OBJ_FLAG_HIDDEN);

    /* 显示目标面板，并同步全局渲染叠加控制变量 */
    switch (mode) {
    case APP_MODE_NIGHT:
        if (s_sidebar_night != NULL) lv_obj_clear_flag(s_sidebar_night, LV_OBJ_FLAG_HIDDEN);
        g_crosshair_enable = 1u;   /* 夜间模式：开启准星绘制 */
        g_select_enable    = 0u;   /* 夜间模式：关闭选区绘制 */
        break;
    case APP_MODE_DIRECTIONAL_REC:
        if (s_sidebar_dirrec != NULL) lv_obj_clear_flag(s_sidebar_dirrec, LV_OBJ_FLAG_HIDDEN);
        g_crosshair_enable = 0u;   /* 录音模式：关闭准星绘制 */
        g_select_enable    = 1u;   /* 录音模式：开启选区绘制 */
        break;
    default: /* APP_MODE_MAIN */
        if (s_sidebar_main != NULL) lv_obj_clear_flag(s_sidebar_main, LV_OBJ_FLAG_HIDDEN);
        g_crosshair_enable = 0u;   /* 主模式：关闭准星 */
        g_select_enable    = 0u;   /* 主模式：关闭选区 */
        break;
    }

    /* 更新模式按钮标签及颜色以反映当前模式 */
    if (s_mode_btn_lbl != NULL) {
        static const char * const mode_labels[] = {"M", "N", "R"}; /* 索引对应 APP_MODE_MAIN/NIGHT/DIRREC */
        lv_color_t clr;
        uint32_t idx = (uint32_t)mode;
        if (idx >= APP_MODE_COUNT) idx = 0u;         /* 越界保护 */
        lv_label_set_text(s_mode_btn_lbl, mode_labels[idx]);
        switch (idx) {
        case 1u:  clr = lv_color_hex(0xFF6600); break; /* NIGHT: 橙色 */
        case 2u:  clr = lv_color_hex(0xFF3333); break; /* REC:   红色 */
        default:  clr = lv_color_hex(0x00D4FF); break; /* MAIN:  青色 */
        }
        lv_obj_set_style_text_color(s_mode_btn_lbl, clr, 0);
    }
}

/** @brief 模式切换按钮回调: 循环 MAIN → NIGHT → REC → MAIN */
static void s_mode_btn_cb(lv_event_t *e)
{
    App_OperatingMode_t cur;
    (void)e;
    cur = App_RuntimeConfig_GetOperatingMode();                  /* 获取当前模式 */
    cur = (App_OperatingMode_t)(((uint32_t)cur + 1u) % (uint32_t)APP_MODE_COUNT); /* 循环到下一个模式 */
    App_RuntimeConfig_SetOperatingMode(cur);                     /* 写入运行时配置 */
    s_sidebar_switch_mode(cur);                                  /* 切换侧边栏和叠加标志 */
}

/* ---- 夜间模式回调 ---- */

/** @brief 夜间模式 X 准星滑块回调：实时更新全局准星 X 坐标 */
static void s_night_slider_cx_cb(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(lv_event_get_target(e)); /* 获取滑块当前值 */
    char buf[16];
    g_crosshair_x = (uint16_t)val;                            /* 同步到全局渲染坐标 */
    (void)snprintf(buf, sizeof(buf), "X: %d", (int)val);
    if (s_night_lbl_cx != NULL) lv_label_set_text(s_night_lbl_cx, buf);
}

/** @brief 夜间模式 Y 准星滑块回调：实时更新全局准星 Y 坐标 */
static void s_night_slider_cy_cb(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];
    g_crosshair_y = (uint16_t)val;
    (void)snprintf(buf, sizeof(buf), "Y: %d", (int)val);
    if (s_night_lbl_cy != NULL) lv_label_set_text(s_night_lbl_cy, buf);
}

/** @brief 夜间模式 "居中" 按钮回调：将准星重置到画面中心 */
static void s_night_btn_center_cb(lv_event_t *e)
{
    (void)e;
    g_crosshair_x = (APP_DISPLAY_CAMERA_VIEW_W / 2u);  /* 水平中点 */
    g_crosshair_y = (APP_DISPLAY_CAMERA_VIEW_H / 2u);  /* 垂直中点 */
    /* 同步更新滑块位置和标签 */
    if (s_night_slider_cx != NULL) lv_slider_set_value(s_night_slider_cx, (int32_t)g_crosshair_x, LV_ANIM_OFF);
    if (s_night_slider_cy != NULL) lv_slider_set_value(s_night_slider_cy, (int32_t)g_crosshair_y, LV_ANIM_OFF);
    if (s_night_lbl_cx != NULL) { char buf[16]; (void)snprintf(buf, sizeof(buf), "X: %u", (unsigned)g_crosshair_x); lv_label_set_text(s_night_lbl_cx, buf); }
    if (s_night_lbl_cy != NULL) { char buf[16]; (void)snprintf(buf, sizeof(buf), "Y: %u", (unsigned)g_crosshair_y); lv_label_set_text(s_night_lbl_cy, buf); }
}

/** @brief 夜间模式 "跟踪" 按钮回调：将准星跳转到当前声源峰值位置 */
static void s_night_btn_track_cb(lv_event_t *e)
{
    (void)e;
    /* 仅当能量足够大（>0.01 线性）时才执行跟踪，避免噪声抖动 */
    if (s_live_data.energy > 0.01f) {
        /* 角度范围 [-60°, +60°] 线性映射到像素坐标 [0, SEL_MAX_X/Y]
         * nx = (angle + 60) / 120  → [0,1]，再乘以像素宽度 */
        float nx = (s_live_data.x_angle + 60.0f) / 120.0f;
        float ny = (s_live_data.y_angle + 60.0f) / 120.0f;
        uint16_t px, py;
        /* M1 fix: clamp before cast to avoid UB */
        if (nx < 0.0f) nx = 0.0f;
        if (nx > 1.0f) nx = 1.0f;
        if (ny < 0.0f) ny = 0.0f;
        if (ny > 1.0f) ny = 1.0f;
        px = (uint16_t)(nx * (float)SEL_MAX_X);
        py = (uint16_t)((1.0f - ny) * (float)SEL_MAX_Y);
        if (px > SEL_MAX_X) px = SEL_MAX_X;
        if (py > SEL_MAX_Y) py = SEL_MAX_Y;
        g_crosshair_x = px;
        g_crosshair_y = py;
        if (s_night_slider_cx != NULL) lv_slider_set_value(s_night_slider_cx, (int32_t)px, LV_ANIM_OFF);
        if (s_night_slider_cy != NULL) lv_slider_set_value(s_night_slider_cy, (int32_t)py, LV_ANIM_OFF);
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "X: %u", (unsigned)px);
            if (s_night_lbl_cx != NULL) lv_label_set_text(s_night_lbl_cx, buf);
            (void)snprintf(buf, sizeof(buf), "Y: %u", (unsigned)py);
            if (s_night_lbl_cy != NULL) lv_label_set_text(s_night_lbl_cy, buf);
        }
    }
}

/* ---- 定向录音模式回调 ---- */

static void s_dirrec_btn_record_cb(lv_event_t *e)
{
    App_RecorderState_t rec_state;
    uint16_t mode_idx = 0u;
    (void)e;

    /* Guard: refuse to start recording if SD card is not mounted */
    if (App_SD_GetState() != APP_SD_MOUNTED)
    {
        if (s_dirrec_lbl_sd_status != NULL)
        {
            lv_label_set_text(s_dirrec_lbl_sd_status,
                "SD \xE6\x9C\xAA\xE5\xB0\xB1\xE7\xBB\xAA");  /* SD 未就绪 */
            lv_obj_set_style_text_color(s_dirrec_lbl_sd_status, UI_COLOR_ERROR, 0);
        }
        return;
    }

    rec_state = App_Recorder_GetState();
    if (rec_state == RECORDER_RECORDING)
    {
        /* Pessimistic: only send stop command; button updated by s_main_update() */
        (void)App_Storage_SendCmd(STORAGE_CMD_REC_STOP, 0u);
    }
    else if (rec_state == RECORDER_IDLE)
    {
        if (s_dirrec_dd_rec_mode != NULL)
            mode_idx = lv_dropdown_get_selected(s_dirrec_dd_rec_mode);
        (void)App_Storage_SendCmd(STORAGE_CMD_REC_START, (uint32_t)mode_idx);
    }
}

static void s_dirrec_btn_reset_sel_cb(lv_event_t *e)
{
    (void)e;
    g_select_x1 = SEL_DEFAULT_X1;
    g_select_y1 = SEL_DEFAULT_Y1;
    g_select_x2 = SEL_DEFAULT_X2;
    g_select_y2 = SEL_DEFAULT_Y2;
    s_dirrec_sel_labels_refresh();
    /* Sync sliders to defaults */
    if (s_dirrec_slider_x1 != NULL) lv_slider_set_value(s_dirrec_slider_x1, (int32_t)SEL_DEFAULT_X1, LV_ANIM_OFF);
    if (s_dirrec_slider_y1 != NULL) lv_slider_set_value(s_dirrec_slider_y1, (int32_t)SEL_DEFAULT_Y1, LV_ANIM_OFF);
    if (s_dirrec_slider_x2 != NULL) lv_slider_set_value(s_dirrec_slider_x2, (int32_t)SEL_DEFAULT_X2, LV_ANIM_OFF);
    if (s_dirrec_slider_y2 != NULL) lv_slider_set_value(s_dirrec_slider_y2, (int32_t)SEL_DEFAULT_Y2, LV_ANIM_OFF);
}

/** @brief 更新选区标签 */
static void s_dirrec_sel_labels_refresh(void)
{
    char buf[40];
    if (s_dirrec_lbl_sel_xy != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "(%u,%u)-(%u,%u)",
                       (unsigned)g_select_x1, (unsigned)g_select_y1,
                       (unsigned)g_select_x2, (unsigned)g_select_y2);
        lv_label_set_text(s_dirrec_lbl_sel_xy, buf);
    }
    if (s_dirrec_lbl_sel_sz != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%ux%u px",
                       (unsigned)(g_select_x2 - g_select_x1),
                       (unsigned)(g_select_y2 - g_select_y1));
        lv_label_set_text(s_dirrec_lbl_sel_sz, buf);
    }
}

/** @brief Selection slider shared callback — enforces minimum selection size */
static void s_dirrec_slider_sel_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t  val = lv_slider_get_value(sl);

    if (sl == s_dirrec_slider_x1)
    {
        if (val > (int32_t)g_select_x2 - (int32_t)SEL_MIN_SIZE)
            val = (int32_t)g_select_x2 - (int32_t)SEL_MIN_SIZE;
        if (val < 0) val = 0;
        g_select_x1 = (uint16_t)val;
        lv_slider_set_value(sl, val, LV_ANIM_OFF);
    }
    else if (sl == s_dirrec_slider_y1)
    {
        if (val > (int32_t)g_select_y2 - (int32_t)SEL_MIN_SIZE)
            val = (int32_t)g_select_y2 - (int32_t)SEL_MIN_SIZE;
        if (val < 0) val = 0;
        g_select_y1 = (uint16_t)val;
        lv_slider_set_value(sl, val, LV_ANIM_OFF);
    }
    else if (sl == s_dirrec_slider_x2)
    {
        if (val < (int32_t)g_select_x1 + (int32_t)SEL_MIN_SIZE)
            val = (int32_t)g_select_x1 + (int32_t)SEL_MIN_SIZE;
        if (val > (int32_t)SEL_MAX_X) val = (int32_t)SEL_MAX_X;
        g_select_x2 = (uint16_t)val;
        lv_slider_set_value(sl, val, LV_ANIM_OFF);
    }
    else if (sl == s_dirrec_slider_y2)
    {
        if (val < (int32_t)g_select_y1 + (int32_t)SEL_MIN_SIZE)
            val = (int32_t)g_select_y1 + (int32_t)SEL_MIN_SIZE;
        if (val > (int32_t)SEL_MAX_Y) val = (int32_t)SEL_MAX_Y;
        g_select_y2 = (uint16_t)val;
        lv_slider_set_value(sl, val, LV_ANIM_OFF);
    }
    s_dirrec_sel_labels_refresh();
}

/* ============================================================================
 * Settings 屏幕 —— 三标签 TabView (Display / Algorithm / System)
 * ============================================================================ */

/** @brief 创建一个设置行：Label + 描述 + 控件 */
static lv_obj_t *s_make_setting_row(lv_obj_t *parent, const char *label_text,
                                     const char *desc_text)
{
    lv_obj_t *left, *lbl;
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* 左侧: 标签名 + 描述 */
    left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 1, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lbl = lv_label_create(left);
    lv_label_set_text(lbl, label_text);
    lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);

    if (desc_text != NULL && desc_text[0] != '\0')
    {
        lbl = lv_label_create(left);
        lv_label_set_text(lbl, desc_text);
        lv_obj_set_style_text_color(lbl, UI_COLOR_INACTIVE, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_sc_12, 0);
    }
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
        lv_label_set_text(lbl, LV_SYMBOL_SETTINGS " \xE8\xAE\xBE\xE7\xBD\xAE");  /* 设置 */
        lv_obj_add_style(lbl, &g_ui_styles.label_title, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 4);
    }

    /* ---- TabView (3 tabs) ---- */
    tv = lv_tabview_create(scr, LV_DIR_TOP, 30);
    lv_obj_set_size(tv, 1000, 500);
    lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(tv, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);

    tab_disp = lv_tabview_add_tab(tv, "\xE6\x98\xBE\xE7\xA4\xBA");    /* 显示 */
    tab_algo = lv_tabview_add_tab(tv, "\xE7\xAE\x97\xE6\xB3\x95");    /* 算法 */
    tab_sys  = lv_tabview_add_tab(tv, "\xE7\xB3\xBB\xE7\xBB\x9F");    /* 系统 */

    /* CJK font for tab buttons */
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(tv), &lv_font_sc_14, 0);

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

    /* 显示模式 */
    row = s_make_setting_row(tab_disp,
            "\xE6\x98\xBE\xE7\xA4\xBA\xE6\xA8\xA1\xE5\xBC\x8F:",   /* 显示模式 */
            "Fast=\xE4\xBD\x8E\xE5\xBB\xB6\xE8\xBF\x9F Bal=\xE5\x9D\x87\xE8\xA1\xA1 Clean=\xE9\xAB\x98\xE7\x94\xBB\xE8\xB4\xA8");
    s_settings_dd_mode = lv_dropdown_create(row);
    lv_dropdown_set_options(s_settings_dd_mode,
        "Fast (\xE4\xBD\x8E\xE5\xBB\xB6\xE8\xBF\x9F)\n"
        "Balanced (\xE5\x9D\x87\xE8\xA1\xA1)\n"
        "Clean (\xE9\xAB\x98\xE7\x94\xBB\xE8\xB4\xA8)");  /* 低延迟/均衡/高画质 */
    lv_dropdown_set_selected(s_settings_dd_mode, (uint16_t)dmode);
    lv_obj_set_width(s_settings_dd_mode, 150);
    lv_obj_set_style_text_font(s_settings_dd_mode, &lv_font_sc_12, 0);
    lv_obj_add_event_cb(s_settings_dd_mode, s_settings_dd_mode_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Gamma */
    row = s_make_setting_row(tab_disp,
            "Gamma (\xE4\xBC\xBD\xE9\xA9\xAC):",   /* Gamma (伽马) */
            "\xE7\x83\xAD\xE5\x8A\x9B\xE5\x9B\xBE\xE5\xAF\xB9\xE6\xAF\x94\xE5\xBA\xA6 0.5-3.0");
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

    /* 噪声门限 */
    row = s_make_setting_row(tab_disp,
            "\xE5\x99\xAA\xE5\xA3\xB0\xE9\x97\xA8\xE9\x99\x90:",   /* 噪声门限 */
            "\xE4\xBD\x8E\xE4\xBA\x8E\xE9\x97\xA8\xE9\x99\x90\xE7\x9A\x84\xE4\xBF\xA1\xE5\x8F\xB7\xE4\xB8\x8D\xE6\x98\xBE\xE7\xA4\xBA 0-100%");
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

    /* 热力图透明度 (T8 新增) */
    row = s_make_setting_row(tab_disp,
            "\xE7\x83\xAD\xE5\x8A\x9B\xE5\x9B\xBE\xE9\x80\x8F\xE6\x98\x8E\xE5\xBA\xA6:",   /* 热力图透明度 */
            "\xE8\xB0\x83\xE6\x95\xB4\xE7\x83\xAD\xE5\x8A\x9B\xE5\x9B\xBE\xE5\x8F\xA0\xE5\x8A\xA0\xE9\x80\x8F\xE6\x98\x8E\xE5\xBA\xA6 0-100%");
    s_settings_slider_opacity = lv_slider_create(row);
    lv_obj_set_width(s_settings_slider_opacity, 200);
    lv_slider_set_range(s_settings_slider_opacity, 0, 100);
    lv_slider_set_value(s_settings_slider_opacity,
                        (int32_t)(dcfg.heatmap_opacity * 100.0f), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_slider_opacity, s_settings_slider_opacity_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    s_settings_lbl_opacity = lv_label_create(row);
    (void)snprintf(buf, sizeof(buf), "%d%%", (int)(dcfg.heatmap_opacity * 100.0f));
    lv_label_set_text(s_settings_lbl_opacity, buf);
    lv_obj_add_style(s_settings_lbl_opacity, &g_ui_styles.label_value, 0);

    /* 双线性插值 */
    row = s_make_setting_row(tab_disp,
            "\xE5\x8F\x8C\xE7\xBA\xBF\xE6\x80\xA7\xE6\x8F\x92\xE5\x80\xBC:",   /* 双线性插值 */
            "\xE5\xBC\x80\xE5\x90\xAF\xE5\x90\x8E\xE7\x83\xAD\xE5\x8A\x9B\xE5\x9B\xBE\xE6\x9B\xB4\xE5\xB9\xB3\xE6\xBB\x91");
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

    /* 频率下限 */
    row = s_make_setting_row(tab_algo,
            "\xE9\xA2\x91\xE7\x8E\x87\xE4\xB8\x8B\xE9\x99\x90:",   /* 频率下限 */
            "SRP-PHAT \xE5\x88\x86\xE6\x9E\x90\xE9\xA2\x91\xE6\xAE\xB5\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA2\x91\xE7\x8E\x87");
    s_settings_slider_freq_lo = lv_slider_create(row);
    lv_obj_set_width(s_settings_slider_freq_lo, 200);
    lv_slider_set_range(s_settings_slider_freq_lo, 1, (int32_t)APP_SPECTRUM_BIN_COUNT);
    lv_slider_set_value(s_settings_slider_freq_lo, (int32_t)freq_lo, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_settings_slider_freq_lo, s_settings_slider_freq_lo_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 频率上限 */
    row = s_make_setting_row(tab_algo,
            "\xE9\xA2\x91\xE7\x8E\x87\xE4\xB8\x8A\xE9\x99\x90:",   /* 频率上限 */
            "SRP-PHAT \xE5\x88\x86\xE6\x9E\x90\xE9\xA2\x91\xE6\xAE\xB5\xE7\xBB\x93\xE6\x9D\x9F\xE9\xA2\x91\xE7\x8E\x87");
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
            const char *names[] = {
                "\xE5\x85\xA8\xE9\xA2\x91",  /* 全频 */
                "\xE4\xBA\xBA\xE5\xA3\xB0",  /* 人声 */
                "\xE8\xB6\x85\xE5\xA3\xB0",  /* 超声 */
                "\xE4\xBD\x8E\xE9\xA2\x91"   /* 低频 */
            };
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

    /* 精细扫描融合 */
    row = s_make_setting_row(tab_algo,
            "\xE7\xB2\xBE\xE7\xBB\x86\xE6\x89\xAB\xE6\x8F\x8F\xE8\x9E\x8D\xE5\x90\x88:",   /* 精细扫描融合 */
            "\xE5\xBC\x80\xE5\x90\xAF\xE5\x90\x8E\xE5\xAF\xB9\xE5\xB3\xB0\xE5\x80\xBC\xE5\x81\x9A\xE4\xBA\x8C\xE6\xAC\xA1\xE7\xBB\x86\xE5\x8C\x96\xE6\x89\xAB\xE6\x8F\x8F");
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
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH " \xE5\x99\xAA\xE5\xBA\x95\xE6\xA0\xA1\xE5\x87\x86");  /* 噪底校准 */
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_settings_btn_noise_cal_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    /* ================================================================
     *  System Tab
     * ================================================================ */

    /* 激光器 */
    row = s_make_setting_row(tab_sys,
            "\xE6\xBF\x80\xE5\x85\x89\xE5\x99\xA8:",   /* 激光器 */
            "\xE6\x8E\xA7\xE5\x88\xB6\xE6\xBF\x80\xE5\x85\x89\xE7\x82\xB9\xE4\xBA\xAE\xE7\x81\xAD");
    s_settings_sw_laser = lv_switch_create(row);
    if (App_Laser_GetState() == APP_LASER_ON)
    {
        lv_obj_add_state(s_settings_sw_laser, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_settings_sw_laser, s_settings_sw_laser_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 夜间模式 */
    row = s_make_setting_row(tab_sys,
            "\xE5\xA4\x9C\xE9\x97\xB4\xE6\xA8\xA1\xE5\xBC\x8F:",   /* 夜间模式 */
            "\xE5\x85\xB3\xE9\x97\xAD\xE6\x91\x84\xE5\x83\x8F\xE5\xA4\xB4\xE5\xB9\xB6\xE5\xBC\x80\xE5\x90\xAF\xE6\xBF\x80\xE5\x85\x89\xE5\x99\xA8");
    s_settings_sw_night = lv_switch_create(row);
    if (App_NightMode_GetState() == APP_NIGHTMODE_ON)
    {
        lv_obj_add_state(s_settings_sw_night, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_settings_sw_night, s_settings_sw_night_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 系统信息 (只读) */
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        lv_label_set_text(lbl, "\xE4\xB8\xBB\xE6\x8E\xA7: STM32H743 @ 480MHz");  /* 主控 */
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        lv_label_set_text(lbl, "\xE7\xB3\xBB\xE7\xBB\x9F: FreeRTOS v10.3.1");  /* 系统 */
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        lv_label_set_text(lbl, "\xE4\xB8\xB2\xE5\x8F\xA3: UART 921600 8N1");  /* 串口 */
        lv_obj_add_style(lbl, &g_ui_styles.label_unit, 0);
    }

    /* 堆使用情况 (只读) */
    {
        lv_obj_t *lbl = lv_label_create(tab_sys);
        (void)snprintf(buf, sizeof(buf), "\xE5\x89\xA9\xE4\xBD\x99\xE5\xA0\x86: %lu B",  /* 剩余堆 */
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
        lv_label_set_text(lbl, LV_SYMBOL_FILE " \xE4\xBD\xBF\xE7\x94\xA8\xE6\x8C\x87\xE5\x8D\x97");  /* 使用指南 */
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
        lv_label_set_text(lbl, LV_SYMBOL_POWER " \xE9\x87\x8D\xE5\x90\xAF");  /* 重启 */
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
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " \xE8\xBF\x94\xE5\x9B\x9E");  /* 返回 */
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, s_settings_btn_back_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

/** @brief 设置屏幕"返回"按钮回调 → 切换回 Main 屏幕 */
static void s_settings_btn_back_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_MAIN);
}

/** @brief 设置屏幕显示模式下拉框回调：下拉框选项索引与 App_Runtime_DisplayMode_t 枚举一一对应 */
static void s_settings_dd_mode_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd); /* 0=FAST / 1=BALANCED / 2=CLEAN */
    App_RuntimeConfig_SetDisplayMode((App_Runtime_DisplayMode_t)sel);
}

/** @brief 设置屏幕 Gamma 滑块回调：滑块整数值 ÷10 → gamma 浮点值 (范围约 0.2~3.0) */
static void s_settings_slider_gamma_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);           /* 滑块整数值（如 10 代表 gamma=1.0）*/
    App_Runtime_DisplayCfg_t cfg;
    char buf[16];
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.gamma = (float)val / 10.0f;                  /* 还原为浮点：10→1.0，25→2.5 */
    App_RuntimeConfig_SetDisplayCfg(&cfg);
    if (s_settings_lbl_gamma != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%.1f", (double)cfg.gamma); /* 刷新旁边的数值标签 */
        lv_label_set_text(s_settings_lbl_gamma, buf);
    }
}

/** @brief 设置屏幕噪声门限滑块回调：滑块值 ÷100 → noise_gate_ratio（0~1.0） */
static void s_settings_slider_noise_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);           /* 滑块值，单位为百分比（0~100）*/
    App_Runtime_DisplayCfg_t cfg;
    char buf[16];
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.noise_gate_ratio = (float)val / 100.0f;      /* 百分比→比率 */
    App_RuntimeConfig_SetDisplayCfg(&cfg);
    if (s_settings_lbl_noise != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%d%%", (int)val); /* 刷新百分比标签 */
        lv_label_set_text(s_settings_lbl_noise, buf);
    }
}

/** @brief 设置屏幕热力图不透明度滑块回调：滑块值 ÷100 → heatmap_opacity（0~1.0） */
static void s_settings_slider_opacity_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);
    App_Runtime_DisplayCfg_t cfg;
    char buf[16];
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.heatmap_opacity = (float)val / 100.0f;       /* 百分比→比率 */
    App_RuntimeConfig_SetDisplayCfg(&cfg);
    if (s_settings_lbl_opacity != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%d%%", (int)val);
        lv_label_set_text(s_settings_lbl_opacity, buf);
    }
}

/** @brief 设置屏幕双线性插值开关回调：选中=双线性，未选中=最近邻 */
static void s_settings_sw_interp_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    App_Runtime_DisplayCfg_t cfg;
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    /* CHECKED 状态 = 开关打开 = 双线性插值；反之为最近邻 */
    cfg.interp_mode = lv_obj_has_state(sw, LV_STATE_CHECKED)
                      ? (uint8_t)APP_RUNTIME_DISP_INTERP_BILINEAR
                      : (uint8_t)APP_RUNTIME_DISP_INTERP_NEAREST;
    App_RuntimeConfig_SetDisplayCfg(&cfg);
}

/** @brief 设置屏幕精细融合开关回调：开 = 使用 2D 高斯核精细热图合成 */
static void s_settings_sw_fine_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    App_Runtime_DisplayCfg_t cfg;
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.fine_fusion_enable = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1u : 0u;
    App_RuntimeConfig_SetDisplayCfg(&cfg);
}

/** @brief 设置屏幕"使用指南"按钮回调 → 跳转到引导屏幕 */
static void s_settings_btn_guide_cb(lv_event_t *e)
{
    (void)e;
    App_UiScreens_Switch(APP_SCR_GUIDE);
}

/** @brief 设置屏幕频段下限滑块回调：限制 lo <= hi 并同步更新频段 */
static void s_settings_slider_freq_lo_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(lv_event_get_target(e)); /* 当前 lo bin */
    int32_t hi = lv_slider_get_value(s_settings_slider_freq_hi); /* 当前 hi bin */
    char buf[32];
    if (lo > hi) { lo = hi; lv_slider_set_value(lv_event_get_target(e), lo, LV_ANIM_OFF); } /* [改进] 可显示提示而非静默夹紧 */
    App_RuntimeConfig_SetFreqBand((uint16_t)lo, (uint16_t)hi);
    App_UiSpecPanel_ApplyPreset(SPEC_PRESET_FULL); /* 同步频谱面板显示范围 */
    if (s_settings_lbl_freq != NULL) {
        /* bin→Hz：freq(Hz) = bin × Fs(48000Hz) / NFFT(256) */
        (void)snprintf(buf, sizeof(buf), "%d - %d Hz",
                       (int)App_Spectrum_BinToHz((uint16_t)lo),
                       (int)App_Spectrum_BinToHz((uint16_t)hi));
        lv_label_set_text(s_settings_lbl_freq, buf);
    }
}

/** @brief 设置屏幕频段上限滑块回调：限制 hi >= lo 并同步更新频段 */
static void s_settings_slider_freq_hi_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(s_settings_slider_freq_lo); /* 当前 lo bin */
    int32_t hi = lv_slider_get_value(lv_event_get_target(e));    /* 当前 hi bin */
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

/** @brief 设置屏幕频段预设按钮回调：快速设置常用频段（全频/语音/超声/低频）
 *  @note  user_data 为索引 0~3，通过 lv_event_get_user_data() 获取 */
static void s_settings_preset_btn_cb(lv_event_t *e)
{
    /* 四个预设的 [lo, hi] FFT bin 索引（bin→Hz = bin×48000/256）
     * Full:  bin 3-42   ≈ 562 Hz  ~ 7875 Hz （宽频）
     * Voice: bin 2-18   ≈ 375 Hz  ~ 3375 Hz （人声）
     * Ultra: bin 54-128 ≈ 10125 Hz ~ 24000 Hz（超声波）
     * Low:   bin 1-5    ≈ 187 Hz  ~ 937 Hz  （次声/低频）*/
    static const uint16_t presets[][2] = {
        { 3u,  42u  },  /* Full  */
        { 2u,  18u  },  /* Voice */
        { 54u, 128u },  /* Ultra */
        { 1u,  5u   }   /* Low   */
    };
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e); /* 按钮创建时注入的索引 */
    char buf[32];
    if (idx < 4u) {
        uint16_t lo = presets[idx][0];
        uint16_t hi = presets[idx][1];
        App_RuntimeConfig_SetFreqBand(lo, hi);   /* 写入运行时频段 */
        if (s_settings_slider_freq_lo != NULL) { /* 同步 lo 滑块位置 */
            lv_slider_set_value(s_settings_slider_freq_lo, (int32_t)lo, LV_ANIM_OFF);
        }
        if (s_settings_slider_freq_hi != NULL) { /* 同步 hi 滑块位置 */
            lv_slider_set_value(s_settings_slider_freq_hi, (int32_t)hi, LV_ANIM_OFF);
        }
        if (s_settings_lbl_freq != NULL) {       /* 刷新频段文字标签 */
            (void)snprintf(buf, sizeof(buf), "%d - %d Hz",
                           (int)App_Spectrum_BinToHz(lo),
                           (int)App_Spectrum_BinToHz(hi));
            lv_label_set_text(s_settings_lbl_freq, buf);
        }
    }
}

/** @brief 设置屏幕激光开关回调：直接调用激光驱动 SetState */
static void s_settings_sw_laser_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    App_Laser_SetState(lv_obj_has_state(sw, LV_STATE_CHECKED)
                       ? APP_LASER_ON : APP_LASER_OFF);
}

/** @brief 设置屏幕夜间模式开关回调：开 = 全幅黑背景 + 准星显示 */
static void s_settings_sw_night_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
    {
        App_NightMode_Enable();  /* 开启夜间模式：摄像头画面变暗，准星激活 */
    }
    else
    {
        App_NightMode_Disable(); /* 关闭夜间模式：恢复正常摄像头预览 */
    }
}

/** @brief 设置屏幕"重启"按钮回调：调用 CMSIS 软件复位（慎用！） */
static void s_settings_btn_reboot_cb(lv_event_t *e)
{
    (void)e;
    NVIC_SystemReset(); /* [注意] 立即复位 MCU，所有未保存数据将丢失 */
}

/** @brief 噪声底校零按钮回调 —— 将当前频谱作为噪声基线 */
static void s_settings_btn_noise_cal_cb(lv_event_t *e)
{
    App_SpectrumFrame_t frame;
    (void)e;
    if (App_Spectrum_GetLatestFrame(&frame) != 0u)
    {
        /* 将最新频谱帧的幅度谱设为噪声底参考，后续帧会减去该基线 */
        App_NoiseFloor_Calibrate(frame.magnitude, APP_SPECTRUM_BIN_COUNT);
    }
}

/** @brief 设置屏幕周期更新（当前无需刷新元素，留作扩展占位）*/
static void s_settings_update(void)
{
}

/* ============================================================================
 * Capture 屏幕 —— 数据捕获（截图 + 录音 + 波束控向）
 * ============================================================================ */

/** @brief 录音按钮脉冲动画回调：LVGL 动画系统每帧调用，设置控件不透明度 */
static void s_cap_rec_anim_cb(void *var, int32_t val)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)val, 0); /* val 从 COVER 到 40 往返 */
}

/** @brief 启动录音按钮脉冲动画（0.6s 周期闪烁，无限循环）*/
static void s_cap_rec_anim_start(void)
{
    lv_anim_t a;
    if (s_cap_btn_record == NULL || s_cap_rec_anim_active != 0u)
    {
        return; /* 按钮不存在或动画已在运行，防止重复创建 */
    }
    lv_anim_init(&a);                                      /* 用默认值初始化动画描述符 */
    lv_anim_set_var(&a, s_cap_btn_record);                 /* 动画目标：录音按钮对象 */
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);      /* 不透明度：255 → 40 往返 */
    lv_anim_set_time(&a, 600);                             /* 正向动画时长 600ms */
    lv_anim_set_playback_time(&a, 600);                    /* 反向回放时长 600ms，合计 1.2s/次 */
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); /* 无限重复 */
    lv_anim_set_exec_cb(&a, s_cap_rec_anim_cb);            /* 每帧回调函数 */
    lv_anim_start(&a);                                     /* 将动画加入 LVGL 动画队列 */
    s_cap_rec_anim_active = 1u;
}

/** @brief 停止录音按钮脉冲动画并恢复完全不透明 */
static void s_cap_rec_anim_stop(void)
{
    if (s_cap_btn_record != NULL && s_cap_rec_anim_active != 0u)
    {
        lv_anim_del(s_cap_btn_record, s_cap_rec_anim_cb);       /* 从动画队列中删除匹配项 */
        lv_obj_set_style_bg_opa(s_cap_btn_record, LV_OPA_COVER, 0); /* 恢复完全不透明 */
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
    lv_obj_set_size(title_bar, 1024, 36);
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
    lv_obj_set_size(content, 1000, 450);
    lv_obj_set_pos(content, 12, 42);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(content, 20, 0);

    /* ================================================================
     *  左卡片 —— 截图 (Screenshot)
     * ================================================================ */
    card_l = lv_obj_create(content);
    lv_obj_remove_style_all(card_l);
    lv_obj_add_style(card_l, &g_ui_styles.panel, 0);
    lv_obj_set_size(card_l, 490, 360);
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
    lv_obj_set_size(card_r, 490, 360);
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
    lv_obj_set_size(s_cap_dd_rec_mode, 200, 30);
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
    lv_obj_set_size(sd_bar, 1000, 52);
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

    /* SD 未挂载时拒绝操作 */
    if (App_SD_GetState() != APP_SD_MOUNTED)
    {
        return;
    }

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
        if (App_SD_GetState() != APP_SD_MOUNTED)
        {
            lv_label_set_text(s_cap_lbl_shot_state,
                "SD \xE6\x9C\xAA\xE5\xB0\xB1\xE7\xBB\xAA");  /* SD 未就绪 */
            lv_obj_set_style_text_color(s_cap_lbl_shot_state,
                                         UI_COLOR_ERROR, 0);
        }
        else if (sto_state == STORAGE_STATE_CAPTURING)
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
    lv_obj_set_size(s_diag_chart, 940, 200);
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
        lv_obj_set_size(name_row, 940, LV_SIZE_CONTENT);
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
        lv_obj_set_size(panel, 940, 190);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 244);
        lv_obj_set_style_pad_all(panel, UI_PAD_NORMAL, 0);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        s_diag_lbl_info = lv_label_create(panel);
        lv_obj_set_width(s_diag_lbl_info, 916);
        lv_label_set_long_mode(s_diag_lbl_info, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(s_diag_lbl_info, &g_ui_styles.label_unit, 0);
        lv_label_set_text(s_diag_lbl_info,
            "MCU: STM32H743IIT6 @ 480 MHz  |  RTOS: FreeRTOS v10.3.1\n"
            "Audio: 16ch PDM, 48kHz TDM16  |  SRP-PHAT (129 pts)\n"
            "Display: 1024x600 LTDC+DMA2D  |  CLI: 921600 baud\n"
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
            "Display: 1024x600 LTDC+DMA2D  |  CLI: 921600 baud\n"
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
        lv_obj_set_size(panel, 980, 470);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_set_style_pad_all(panel, UI_PAD_NORMAL, 0);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *txt = lv_label_create(panel);
        lv_obj_set_width(txt, 956);
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
            "  Display: 1024x600 TFT-LCD (LTDC+DMA2D)\n"
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
