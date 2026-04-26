/**
 * @file    app_ui_screens.h
 * @brief   LVGL 多屏幕导航框架 —— 屏幕注册与切换
 * @details 管理 NECCS 声学相机的 5 个主屏幕：
 *          - Home    : 启动画面（GUI Guider 生成）
 *          - Main    : 实时工作界面（热力图+频谱+状态栏+工具栏）
 *          - Settings: 参数设置（多 Tab）
 *          - Capture : 数据捕获（截图/录音）
 *          - Diag    : 系统诊断（性能/内存/任务）
 *
 *          采用懒加载模式：屏幕仅在首次导航时创建。
 */
#ifndef __APP_UI_SCREENS_H
#define __APP_UI_SCREENS_H

#include "app_user_config.h"

#if (APP_LVGL_ENABLE != 0u)

#include "lvgl/lvgl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 屏幕 ID 枚举 (Screen IDs)
 * ============================================================================ */

/** @brief 屏幕 ID 定义 */
typedef enum {
    APP_SCR_HOME       = 0u,   /**< 启动画面 */
    APP_SCR_MAIN       = 1u,   /**< 主工作界面 */
    APP_SCR_SETTINGS   = 2u,   /**< 设置页面 */
    APP_SCR_CAPTURE    = 3u,   /**< 数据捕获页 */
    APP_SCR_DIAG       = 4u,   /**< 系统诊断页 */
    APP_SCR_GUIDE      = 5u,   /**< 使用指南页 */
    APP_SCR_COUNT      = 6u    /**< 屏幕总数（哨兵值） */
} App_ScreenId_t;

/* ============================================================================
 * 屏幕描述符 (Screen Descriptor)
 * ============================================================================ */

/**
 * @brief 屏幕回调函数集
 *
 * 每个屏幕实现自己的 create / update / destroy 三个回调：
 * - create  : 创建 LVGL 对象树，返回屏幕根对象 (lv_obj_t*)
 * - update  : 周期性数据刷新（由 App_UiScreens_Update 驱动）
 * - destroy : 销毁屏幕资源（可选，NULL 表示不需要）
 */
typedef struct {
    lv_obj_t *(*create)(void);   /**< 创建屏幕，返回 lv_obj_t* 根对象 */
    void      (*update)(void);   /**< 周期刷新数据 */
    void      (*destroy)(void);  /**< 销毁屏幕（可选） */
} App_ScreenOps_t;

/* ============================================================================
 * 公开 API
 * ============================================================================ */

/**
 * @brief 初始化屏幕管理框架
 * @details 初始化样式表，注册所有屏幕描述符，创建并显示 Home 屏幕。
 *          必须在 lv_init() + lv_port_disp_init() 之后调用。
 */
void App_UiScreens_Init(void);

/**
 * @brief 切换到指定屏幕
 * @param id  目标屏幕 ID
 * @details 若目标屏幕尚未创建，自动调用其 create 回调。
 *          使用 lv_scr_load 加载（不销毁旧屏幕）。
 */
void App_UiScreens_Switch(App_ScreenId_t id);

/**
 * @brief 获取当前活跃屏幕 ID
 * @return 当前屏幕 ID 枚举值
 */
App_ScreenId_t App_UiScreens_GetCurrent(void);

/**
 * @brief 周期性更新当前屏幕数据
 * @details 调用当前屏幕的 update 回调，应在 LVGL timer handler 前调用。
 */
void App_UiScreens_Update(void);

/**
 * @brief 通知屏幕管理器：Home 屏幕上的"开始"按钮被按下
 * @details 由 GUI Guider 事件回调中调用，切换到 Main 屏幕。
 */
void App_UiScreens_OnHomeStartPressed(void);

/* ============================================================================
 * Main View 实时数据馈送 (Live Data Feed)
 * ============================================================================ */

/** @brief Main View 实时数据包 */
typedef struct {
    float x_angle;          /**< 声源水平角 (度) */
    float y_angle;          /**< 声源垂直角 (度) */
    float energy;           /**< 归一化能量 [0,1] */
    uint32_t ui_fps;        /**< UI 实际帧率 */
    uint32_t audio_fps;     /**< 音频处理帧率 */
    uint8_t sai_active;     /**< SAI DMA 活跃标志 */
    uint8_t trigger_state;  /**< 触发状态 (App_TriggerState_t) */
    uint8_t laser_on;       /**< 激光开启标志 */
    uint8_t night_mode;     /**< 夜间模式标志 */
} App_UiLiveData_t;

/**
 * @brief 设置 Main View 实时数据（由 UI 任务调用）
 * @param data 实时数据指针
 */
void App_UiScreens_SetLiveData(const App_UiLiveData_t *data);

/* ============================================================================
 * 夜间模式十字准星 / 定向录音选区 (跨模块共享)
 * ============================================================================ */

extern uint16_t g_crosshair_x;       /**< 十字准星 X 坐标 (0..639) */
extern uint16_t g_crosshair_y;       /**< 十字准星 Y 坐标 (0..423) */
extern uint8_t  g_crosshair_enable;  /**< 十字准星使能 */

extern uint16_t g_select_x1;         /**< 选区左上 X */
extern uint16_t g_select_y1;         /**< 选区左上 Y */
extern uint16_t g_select_x2;         /**< 选区右下 X */
extern uint16_t g_select_y2;         /**< 选区右下 Y */
extern uint8_t  g_select_enable;     /**< 选区使能 */

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_ENABLE */

#endif /* __APP_UI_SCREENS_H */
