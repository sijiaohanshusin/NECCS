/**
 * @file    app_display.h
 * @brief   声学成像显示模块对外接口
 * @details
 * 本模块位于算法结果与 LCD 图像输出之间，负责把定位结果组织成可视化画面。
 * 典型渲染链路包括：
 * 1. 接收 `Sound_Pos_t` 与 `SRP_VisFrame_t`
 * 2. 将 SRP 功率场重采样、平滑并归一化
 * 3. 映射为热力图并放大到 LCD 区域
 * 4. 叠加十字准星、峰值框和诊断文本
 * 5. 通过 LTDC 双缓冲提交到屏幕
 */

#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

#include "app_camera.h"
#include "app_display_cfg.h"
#include "app_types.h"
#include "ai_beamforming.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   显示模式枚举
 * @details 控制热力图渲染的质量与速度权衡
 */
typedef enum
{
    /* 速度优先：更少的平滑/融合，适合实时性要求高的场景。 */
    APP_DISPLAY_MODE_FAST = 0u,

    /* 均衡模式：默认配置，在实时性、稳定性、观感之间折中。 */
    APP_DISPLAY_MODE_BALANCED = 1u,

    /* 观感优先：更平滑、更强调细网格信息，代价是更高计算量。 */
    APP_DISPLAY_MODE_CLEAN = 2u
} App_Display_Mode_t;

/**
 * @brief   热力图插值方式枚举
 * @details 控制低分辨率标量场放大到屏幕分辨率时的插值算法
 */
typedef enum
{
    /* 最近邻插值：速度快，但块状感更明显。 */
    APP_DISPLAY_INTERP_NEAREST = 0u,

    /* 双线性插值：视觉更平滑，适合热力图放大显示。 */
    APP_DISPLAY_INTERP_BILINEAR = 1u
} App_Display_Interp_t;

/**
 * @brief   热力图功率归一化方式枚举
 * @details 控制 SRP 功率值映射到显示强度的算法
 */
typedef enum
{
    /* 快速归一化：依赖查找表减少逐像素对数/幂函数计算。 */
    APP_DISPLAY_NORM_FAST = 0u,

    /* 完整归一化：直接按 dB 与 gamma 公式计算，开销更高。 */
    APP_DISPLAY_NORM_FULL = 1u
} App_Display_Norm_t;

typedef enum
{
    APP_DISPLAY_CAMERA_VIEW_OVERLAY = 0u,
    APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY = 1u,
    APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY = 2u,
    APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE = 3u
} App_Display_CameraView_t;

/**
 * @brief   显示运行时可调配置
 * @details 可通过 CLI 在运行时修改，无需重新编译
 */
typedef struct
{
    /* 峰值上升时 EMA 跟随速度。 */
    float ema_attack;

    /* 峰值下降时 EMA 跟随速度。 */
    float ema_decay;

    /* 归一化下限，单位 dB。 */
    float db_floor;

    /* 细搜索结果融合增益。 */
    float fine_gain;

    /* 亮度曲线 gamma。 */
    float gamma;

    /* 噪声门限比例。 */
    float noise_gate_ratio;

    /* 背景噪声自适应增益。 */
    float noise_adapt_gain;

    /* 高斯平滑迭代次数。 */
    uint8_t smooth_passes;

    /* 是否启用细搜索融合。 */
    uint8_t fine_fusion_enable;

    /* 是否绘制粗网格辅助信息。 */
    uint8_t draw_coarse_grid;

    /* 插值模式，取值见 `App_Display_Interp_t`。 */
    uint8_t interp_mode;

    /* 归一化模式，取值见 `App_Display_Norm_t`。 */
    uint8_t norm_mode;

    /* 文本面板刷新分频。 */
    uint8_t text_refresh_div;

    /* 每次块渲染的行数。 */
    uint8_t blit_rows;
} App_Display_RuntimeCfg_t;

typedef struct
{
    uint8_t camera_view_mode;
    uint32_t camera_path_count;
    uint32_t camera_overlay_count;
    uint32_t camera_input_seq;
    uint32_t camera_cache_seq;
    uint8_t camera_cache_valid;
} App_Display_DebugStats_t;

/**
 * @brief 初始化显示模块
 * @details
 * 通常在系统显示链路建立阶段调用一次。它会：
 * - 建立核函数和颜色查找表
 * - 初始化 LCD/LTDC
 * - 计算热力图区和文字区布局
 * - 记录前后帧缓冲地址
 * - 清屏并提交首帧
 */
void App_Display_Init(void);

/**
 * @brief 渲染一帧声学成像画面
 * @param pos             当前定位结果，供十字准星和文本信息使用
 * @param vis_frame       SRP 可视化快照，包含功率网格与角度信息
 * @param frame_seq       UI/显示帧序号，用于节流和测试图案相位
 * @param sai_dma_active  音频采集 DMA 是否处于活跃状态，供诊断显示使用
 */
void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        const App_CameraFrame_t *camera_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active);

/* 返回显示模块是否已经完成初始化。 */
uint8_t App_Display_IsReady(void);

/* 设置运行时配置。函数内部会做边界钳位，防止配置越界。 */
void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg);

/* 读取当前生效的运行时配置。 */
void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg);
void App_Display_GetDebugStats(App_Display_DebugStats_t *stats);
void App_Display_SetCameraView(App_Display_CameraView_t view_mode);
App_Display_CameraView_t App_Display_GetCameraView(void);

/* 按预设模式覆盖一组推荐配置。 */
void App_Display_SetMode(App_Display_Mode_t mode);

/* 获取当前模式枚举值。 */
App_Display_Mode_t App_Display_GetMode(void);

/* 将枚举转换为短字符串，供侧边文本面板显示。 */
const char *App_Display_ModeName(App_Display_Mode_t mode);
const char *App_Display_InterpName(App_Display_Interp_t interp);
const char *App_Display_NormName(App_Display_Norm_t norm);
const char *App_Display_CameraViewName(App_Display_CameraView_t view_mode);

/* 初始化阶段标志：
 * - `g_display_init_stage` 表示初始化执行到哪个阶段
 * - `g_display_init_error` 表示失败原因码
 */
extern volatile uint32_t g_display_init_stage;
extern volatile uint32_t g_display_init_error;

/**
 * @brief BMP 截图期间帧缓冲 swap 抑制标志
 * @details Storage_Task 在读取前缓冲时置 1, 读取完成后清 0。
 *          s_commit_frame() 检查此标志, 为 1 时跳过 swap 防止撕裂。
 */
extern volatile uint8_t g_display_swap_inhibit;

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
