/**
 * @file    app_runtime.h
 * @brief   Runtime config and UI renderer interfaces
 */
#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdint.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 显示模式枚举 */
typedef enum {
    APP_RUNTIME_DISP_MODE_FAST = 0u,     /**< 快速模式（优先帧率） */
    APP_RUNTIME_DISP_MODE_BALANCED = 1u, /**< 均衡模式（帧率与画质折中） */
    APP_RUNTIME_DISP_MODE_CLEAN = 2u     /**< 清晰模式（优先画质） */
} App_Runtime_DisplayMode_t;

/** @brief 显示插值方式枚举 */
typedef enum {
    APP_RUNTIME_DISP_INTERP_NEAREST = 0u,  /**< 最近邻插值 */
    APP_RUNTIME_DISP_INTERP_BILINEAR = 1u  /**< 双线性插值 */
} App_Runtime_DisplayInterp_t;

/** @brief 显示归一化方式枚举 */
typedef enum {
    APP_RUNTIME_DISP_NORM_FAST = 0u, /**< 快速归一化 */
    APP_RUNTIME_DISP_NORM_FULL = 1u  /**< 完整归一化 */
} App_Runtime_DisplayNorm_t;

/** @brief 显示配置参数结构体 */
typedef struct
{
    float ema_attack;           /**< EMA 上升（攻击）系数 */
    float ema_decay;            /**< EMA 衰减系数 */
    float db_floor;             /**< 分贝下限阈值 */
    float fine_gain;            /**< 精细增益 */
    float gamma;                /**< Gamma 校正系数 */
    float noise_gate_ratio;     /**< 噪声门限比率 */
    float noise_adapt_gain;     /**< 噪声自适应增益 */
    uint8_t smooth_passes;      /**< 平滑处理次数 */
    uint8_t fine_fusion_enable; /**< 精细融合使能标志 */
    uint8_t draw_coarse_grid;   /**< 粗网格绘制使能标志 */
    uint8_t interp_mode;        /**< 插值模式 @see App_Runtime_DisplayInterp_t */
    uint8_t norm_mode;          /**< 归一化模式 @see App_Runtime_DisplayNorm_t */
    uint8_t text_refresh_div;   /**< 文本刷新分频系数 */
    uint8_t blit_rows;          /**< 每次位块传输的行数 */
} App_Runtime_DisplayCfg_t;

/** @brief 运行时总配置结构体 */
typedef struct
{
    uint32_t ui_target_fps;                /**< UI 目标帧率 (FPS) */
    uint32_t audio_algo_decim;             /**< 音频算法抽取因子 */
    uint8_t perf_enabled;                  /**< 性能统计使能标志 */
    uint8_t reserved[3];                   /**< 预留对齐字节 */
    App_Runtime_DisplayMode_t display_mode; /**< 当前显示模式 */
    App_Runtime_DisplayCfg_t display_cfg;   /**< 显示配置参数 */
} App_Runtime_Config_t;

/** @brief UI 渲染后端枚举 */
typedef enum
{
    APP_UI_RENDER_BACKEND_LEGACY = 0u, /**< 传统直接渲染后端 */
    APP_UI_RENDER_BACKEND_LVGL = 1u    /**< LVGL 图形库渲染后端 */
} App_UiRenderBackend_t;

/**
 * @brief 初始化运行时配置（恢复默认值）
 */
void App_RuntimeConfig_Init(void);

/**
 * @brief 获取当前运行时配置的完整副本
 * @param cfg 输出参数，指向接收配置的结构体
 */
void App_RuntimeConfig_Get(App_Runtime_Config_t *cfg);

/**
 * @brief 设置 UI 目标帧率
 * @param fps 目标帧率（单位：FPS）
 */
void App_RuntimeConfig_SetUiTargetFps(uint32_t fps);

/**
 * @brief 获取当前 UI 目标帧率
 * @return 当前目标帧率（单位：FPS）
 */
uint32_t App_RuntimeConfig_GetUiTargetFps(void);

/**
 * @brief 设置音频算法抽取因子
 * @param decim 抽取因子
 */
void App_RuntimeConfig_SetAudioAlgoDecim(uint32_t decim);

/**
 * @brief 获取当前音频算法抽取因子
 * @return 当前抽取因子
 */
uint32_t App_RuntimeConfig_GetAudioAlgoDecim(void);

/**
 * @brief 设置性能统计使能状态
 * @param enable 非零值使能，0 禁用
 */
void App_RuntimeConfig_SetPerfEnabled(uint8_t enable);

/**
 * @brief 获取性能统计使能状态
 * @return 非零值表示已使能，0 表示已禁用
 */
uint8_t App_RuntimeConfig_GetPerfEnabled(void);

/**
 * @brief 设置显示模式
 * @param mode 目标显示模式
 */
void App_RuntimeConfig_SetDisplayMode(App_Runtime_DisplayMode_t mode);

/**
 * @brief 获取当前显示模式
 * @return 当前显示模式
 */
App_Runtime_DisplayMode_t App_RuntimeConfig_GetDisplayMode(void);

/**
 * @brief 设置显示配置参数
 * @param cfg 指向新配置参数的常量指针
 */
void App_RuntimeConfig_SetDisplayCfg(const App_Runtime_DisplayCfg_t *cfg);

/**
 * @brief 获取当前显示配置参数
 * @param cfg 输出参数，指向接收配置的结构体
 */
void App_RuntimeConfig_GetDisplayCfg(App_Runtime_DisplayCfg_t *cfg);

/**
 * @brief 设置 UI 渲染后端
 * @param backend 目标渲染后端
 */
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend);

/**
 * @brief 获取当前 UI 渲染后端
 * @return 当前渲染后端
 */
App_UiRenderBackend_t App_UiRenderer_GetBackend(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_RUNTIME_H */
