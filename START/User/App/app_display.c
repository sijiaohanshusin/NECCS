/**
 * @file    app_display.c
 * @brief   热力图渲染与显示后端 (Heatmap Rendering Engine)
 * @details
 * 本模块负责将 SRP-PHAT 声源定位功率谱渲染为 LCD 上的实时热力图。
 *
 * 渲染流水线:
 *   SRP_VisFrame_t -> 双线性重采样 -> 精细融合 -> 高斯模糊
 *   -> EMA峰值跟踪+噪声门+归一化 -> Colormap LUT -> RGB565
 *   -> DMA2D/LTDC 输出 -> 摄像头叠加(可选) -> UI覆盖层
 *
 * 关键函数:
 * - App_Display_Init: 初始化LCD、构建colormap LUT和卷积核
 * - s_prepare_field: 重采样 + 精细融合 + 高斯平滑
 * - s_update_norm_field: EMA峰值 + 噪声门 + 归一化到8bit
 * - s_render_field_rows: 8bit映射为RGB565并输出到帧缓冲
 * - App_Display_Render: 主渲染入口, UI任务每帧调用
 */
#include "app_display.h"
#include "app_lvgl_ui.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_spectrum.h"
#include "app_touch_test.h"
#include "app_ui_screens.h"

#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "LCD/dma2d_accel.h"
#include "ai_config.h"
#include "mpu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define APP_DISPLAY_FIELD_PIXELS      (APP_DISPLAY_FIELD_W * APP_DISPLAY_FIELD_H)
#define APP_DISPLAY_BLUR_KERNEL_LEN   (2u * APP_DISPLAY_SMOOTH_RADIUS + 1u)
#define APP_DISPLAY_FINE_KERNEL_LEN   (2u * APP_DISPLAY_FINE_KERNEL_RADIUS + 1u)
#define APP_DISPLAY_CAMERA_CACHE_ADDR 0xC0600000u
#define APP_DISPLAY_CAMERA_CACHE_BYTES (APP_DISPLAY_CAMERA_VIEW_W * APP_DISPLAY_CAMERA_VIEW_H * 2u)
#define APP_DISPLAY_CAMERA_CACHE_LIMIT 0xC2000000u
#define APP_DISPLAY_SPECTRUM_AXIS_LABEL_W 36u
#define APP_DISPLAY_SPECTRUM_MARGIN_L    4u
#define APP_DISPLAY_SPECTRUM_MARGIN_R    4u
#define APP_DISPLAY_SPECTRUM_MARGIN_T    4u
#define APP_DISPLAY_SPECTRUM_MARGIN_B    18u
#define APP_DISPLAY_SPECTRUM_GUIDE_DIVS  4u
#define APP_DISPLAY_SPECTRUM_MIN_MAG     1.0e-12f
#if ((APP_CAMERA_ENABLE != 0u) && ((APP_DISPLAY_CAMERA_CACHE_ADDR + APP_DISPLAY_CAMERA_CACHE_BYTES) > APP_DISPLAY_CAMERA_CACHE_LIMIT))
#error "Camera display cache must stay inside SDRAM"
#endif

static uint8_t s_ready = 0u;
volatile uint32_t g_display_init_stage = 0u;
volatile uint32_t g_display_init_error = 0u;
volatile uint8_t g_display_swap_inhibit = 0u;

static uint16_t s_map_x0 = 0u;
static uint16_t s_map_y0 = 0u;
static uint16_t s_map_x1 = 0u;
static uint16_t s_map_y1 = 0u;
static uint16_t s_text_x = 0u;
static uint16_t s_camera_x0 = 0u;
static uint16_t s_camera_y0 = 0u;
static uint16_t s_camera_x1 = 0u;
static uint16_t s_camera_y1 = 0u;
static uint16_t s_ui_x1 = 0u;

static float s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
static float s_last_noise_floor = 0.0f;
static uint8_t s_kernel_ready = 0u;
static uint8_t s_norm_lut_valid = 0u;
static float s_norm_lut_db_floor = APP_DISPLAY_DYNAMIC_DB_FLOOR;
static float s_norm_lut_gamma = APP_DISPLAY_DYNAMIC_GAMMA;
static uint32_t s_fb_addr_a = 0u;
static uint32_t s_fb_addr_b = 0u;
static uint16_t s_cache_map_w = 0u;
static uint16_t s_cache_map_h = 0u;
static uint16_t s_camera_cache_map_w = 0u;
static uint16_t s_camera_cache_map_h = 0u;
static uint16_t s_camera_cache_src_w = 0u;
static uint16_t s_camera_cache_src_h = 0u;
static uint16_t *const s_camera_cache_pixels = (uint16_t *)APP_DISPLAY_CAMERA_CACHE_ADDR;
static uint32_t s_camera_cache_seq = 0u;
static uint8_t s_camera_cache_valid = 0u;
static uint16_t s_camera_freeze_w = 0u;
static uint16_t s_camera_freeze_h = 0u;
static uint16_t s_camera_freeze_stride = 0u;
static uint8_t s_camera_freeze_valid = 0u;
static uint32_t s_dbg_camera_path_count = 0u;
static uint32_t s_dbg_camera_overlay_count = 0u;
static uint32_t s_dbg_camera_input_seq = 0u;
static App_Display_CameraView_t s_camera_view_mode = APP_DISPLAY_CAMERA_VIEW_OVERLAY;
static float s_spectrum_ema[APP_SPECTRUM_BIN_COUNT];
static App_SpectrumFrame_t s_last_spectrum_frame;
static float s_spectrum_ref_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
static uint8_t s_spectrum_ema_valid = 0u;
static uint8_t s_spectrum_frame_valid = 0u;

/* ---- 声源轨迹追踪 ---- */
#define TRAJECTORY_MAX   32u
#define TRAJECTORY_MIN_E 0.01f   /**< 最低能量阈值 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  age;       /**< 0=最新，递增→淡出 */
    uint8_t  valid;
} TrajectoryPoint_t;
static TrajectoryPoint_t s_traj[TRAJECTORY_MAX];
static uint8_t s_traj_head = 0u;
static uint8_t s_traj_count = 0u;

/* * - `s_field_a` / `s_field_b`
 */
__SECTION_AXI_SRAM static float s_field_a[APP_DISPLAY_FIELD_PIXELS];
__SECTION_AXI_SRAM static float s_field_b[APP_DISPLAY_FIELD_PIXELS];
__SECTION_AXI_SRAM static uint8_t s_field_norm_u8[APP_DISPLAY_FIELD_PIXELS];
__SECTION_D2_SRAM static uint16_t s_blit_buf[APP_DISPLAY_MAX_LINE_PIXELS * APP_DISPLAY_BLIT_ROWS_MAX];
__SECTION_D2_SRAM static uint8_t s_blit_l8_buf[APP_DISPLAY_MAX_LINE_PIXELS * APP_DISPLAY_BLIT_ROWS_MAX];
static uint8_t s_norm_ratio_lut[APP_DISPLAY_NORM_RATIO_LUT_SIZE + 1u];
static uint16_t s_row_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_row_y0_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_row_y1_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_row_wy256_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_x0_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_x1_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_wx256_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_camera_row_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_camera_col_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];

static float s_blur_kernel[APP_DISPLAY_BLUR_KERNEL_LEN];
static float s_fine_kernel[APP_DISPLAY_FINE_KERNEL_LEN * APP_DISPLAY_FINE_KERNEL_LEN];
static uint16_t s_heat_lut[APP_DISPLAY_HEAT_LUT_SIZE];

static App_Display_Mode_t s_mode = (App_Display_Mode_t)APP_DISPLAY_DEFAULT_MODE;
static App_Display_RuntimeCfg_t s_cfg = {
#if (APP_DISPLAY_DEFAULT_MODE == 0u)
    APP_DISPLAY_MODE_FAST_EMA_ATTACK,
    APP_DISPLAY_MODE_FAST_EMA_DECAY,
    APP_DISPLAY_MODE_FAST_DB_FLOOR,
    APP_DISPLAY_MODE_FAST_FINE_GAIN,
    APP_DISPLAY_MODE_FAST_GAMMA,
    APP_DISPLAY_MODE_FAST_NOISE_GATE_RATIO,
    APP_DISPLAY_MODE_FAST_NOISE_ADAPT_GAIN,
    0.85f,  /* heatmap_opacity */
    APP_DISPLAY_MODE_FAST_SMOOTH_PASSES,
    APP_DISPLAY_MODE_FAST_FINE_FUSION_ENABLE,
    APP_DISPLAY_MODE_FAST_DRAW_COARSE_GRID,
    (APP_DISPLAY_MODE_FAST_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_FAST_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_MODE_FAST_TEXT_REFRESH_DIV,
    APP_DISPLAY_MODE_FAST_BLIT_ROWS
#elif (APP_DISPLAY_DEFAULT_MODE == 2u)
    APP_DISPLAY_MODE_CLEAN_EMA_ATTACK,
    APP_DISPLAY_MODE_CLEAN_EMA_DECAY,
    APP_DISPLAY_MODE_CLEAN_DB_FLOOR,
    APP_DISPLAY_MODE_CLEAN_FINE_GAIN,
    APP_DISPLAY_MODE_CLEAN_GAMMA,
    APP_DISPLAY_MODE_CLEAN_NOISE_GATE_RATIO,
    APP_DISPLAY_MODE_CLEAN_NOISE_ADAPT_GAIN,
    0.85f,  /* heatmap_opacity */
    APP_DISPLAY_MODE_CLEAN_SMOOTH_PASSES,
    APP_DISPLAY_MODE_CLEAN_FINE_FUSION_ENABLE,
    APP_DISPLAY_MODE_CLEAN_DRAW_COARSE_GRID,
    (APP_DISPLAY_MODE_CLEAN_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_CLEAN_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_MODE_CLEAN_TEXT_REFRESH_DIV,
    APP_DISPLAY_MODE_CLEAN_BLIT_ROWS
#else
    APP_DISPLAY_EMA_ATTACK,
    APP_DISPLAY_EMA_DECAY,
    APP_DISPLAY_DYNAMIC_DB_FLOOR,
    APP_DISPLAY_FINE_GAIN,
    APP_DISPLAY_DYNAMIC_GAMMA,
    APP_DISPLAY_NOISE_GATE_RATIO,
    APP_DISPLAY_NOISE_ADAPT_GAIN,
    0.85f,  /* heatmap_opacity */
    APP_DISPLAY_SMOOTH_PASSES,
    APP_DISPLAY_FINE_FUSION_ENABLE,
    APP_DISPLAY_DRAW_COARSE_GRID,
    (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_BALANCED_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_TEXT_REFRESH_DIV,
    APP_DISPLAY_BLIT_ROWS_MAX
#endif
};

/**
 * @brief 刷新 LCD 像素坐标 -> 场坐标的插值缓存表
 * @details 预计算每个显示像素对应的场网格坐标和权重,
 *          避免每帧渲染时重复计算, 支持最近邻和双线性两种模式。
 */
void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h);
static void s_refresh_camera_scale_cache(uint16_t map_w, uint16_t map_h, uint16_t src_w, uint16_t src_h);
static uint32_t s_display_frame_budget_ms(void);
static uint8_t s_flush_temp_draw(void);
static void s_submit_rgb565_block(uint16_t sx,
                                  uint16_t sy,
                                  uint16_t ex,
                                  uint16_t ey,
                                  uint16_t *pixels);
static void s_clean_dcache_by_addr(const void *addr, uint32_t size);
static uint16_t s_blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha);
static uint8_t s_overlay_alpha_from_norm(uint8_t norm);
static void s_clear_scene_gutters(void);
static void s_render_camera_frame_rows(const App_CameraFrame_t *camera_frame);

/** @brief 将 float 值限制在 [lo, hi] 范围内 */
static float s_clamp_f32(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/** @brief 将 uint8_t 值限制在 [lo, hi] 范围内 */
static uint8_t s_clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/** @brief 将 int32_t 值限制到 [lo, hi] 并转为 uint16_t */
static uint16_t s_clamp_u16(int32_t v, uint16_t lo, uint16_t hi)
{
    if (v < (int32_t)lo)
    {
        return lo;
    }
    if (v > (int32_t)hi)
    {
        return hi;
    }
    return (uint16_t)v;
}

/** @brief 将 8bit R/G/B 分量打包为 RGB565 像素值 */
static uint16_t s_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

/** @brief 将归一化值 (0.0~1.0) 映射为 5 段热力图 RGB565 颜色 */
static uint16_t s_heat_color(float t)
{
    
    typedef struct
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } rgb8_t;

    static const rgb8_t k_stops[5] = {
        {  7u,  10u,  15u},
        { 19u,  28u,  38u},
        { 33u,  58u,  66u},
        {176u,  92u,  36u},
        {255u,  34u,  20u}
    };

    float x = s_clamp_f32(t, 0.0f, 1.0f) * 4.0f;
    uint32_t seg = (uint32_t)x;
    float k;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (seg >= 4u)
    {
        return s_rgb565(k_stops[4].r, k_stops[4].g, k_stops[4].b);
    }

    k = x - (float)seg;
    r = (uint8_t)((1.0f - k) * (float)k_stops[seg].r + k * (float)k_stops[seg + 1u].r);
    g = (uint8_t)((1.0f - k) * (float)k_stops[seg].g + k * (float)k_stops[seg + 1u].g);
    b = (uint8_t)((1.0f - k) * (float)k_stops[seg].b + k * (float)k_stops[seg + 1u].b);
    return s_rgb565(r, g, b);
}

/** @brief 构建 256 级热力图 colormap LUT (s_heat_lut[]) */
static void s_build_heat_lut(void)
{
    uint32_t i;

    /* 将 LUT 索引 0~255 线性映射到归一化值 0.0~1.0，再通过五色段热力图函数生成 RGB565 颜色 */
    /* LUT[0] = 最冷色（深蓝近黑），LUT[255] = 最热色（亮红） */
    for (i = 0u; i < APP_DISPLAY_HEAT_LUT_SIZE; i++)
    {
        s_heat_lut[i] = s_heat_color((float)i / 255.0f);   /* 将 [0,255] 线性归一化到 [0,1] 再取色 */
    }
}

/** @brief 构建高斯模糊核 (s_blur_kernel) 和精细融合核 (s_fine_kernel) */
static void s_build_kernels(void)
{
    uint32_t i;
    uint32_t j;
    float sum = 0.0f;   /* 归一化用累加和 */

    /* 若已构建过则直接返回（OTP 式初始化，只需一次） */
    if (s_kernel_ready != 0u)
    {
        return;
    }

    /* ---- 构建 1D 高斯模糊核（用于分离式水平/垂直高斯平滑）----
     * 公式：w[i] = exp(-dx² / (2σ²))
     * dx = i - radius（使中心权重最大），σ = APP_DISPLAY_SMOOTH_SIGMA
     * 最后除以累加和做归一化，使卷积不改变总能量
     */
    for (i = 0u; i < APP_DISPLAY_BLUR_KERNEL_LEN; i++)
    {
        int32_t dx = (int32_t)i - (int32_t)APP_DISPLAY_SMOOTH_RADIUS;   /* 到核中心的有符号距离 */
        float w = expf(-((float)(dx * dx)) / (2.0f * APP_DISPLAY_SMOOTH_SIGMA * APP_DISPLAY_SMOOTH_SIGMA));
        s_blur_kernel[i] = w;   /* 存储未归一化权重 */
        sum += w;               /* 累加供后续归一化 */
    }
    if (sum > 0.0f)
    {
        for (i = 0u; i < APP_DISPLAY_BLUR_KERNEL_LEN; i++)
        {
            s_blur_kernel[i] /= sum;   /* 归一化：使权重之和为 1.0 */
        }
    }

    /* ---- 构建 2D 高斯精细融合核（用于精细网格的高斯权重叠加）----
     * 公式：w[i,j] = exp(-(dx² + dy²) / (2σ'²))
     * dx = i - radius_x, dy = j - radius_y
     * σ' = APP_DISPLAY_FINE_KERNEL_SIGMA（通常比模糊核更小/更尖锐）
     * 同样做归一化
     */
    sum = 0.0f;
    for (i = 0u; i < APP_DISPLAY_FINE_KERNEL_LEN; i++)
    {
        for (j = 0u; j < APP_DISPLAY_FINE_KERNEL_LEN; j++)
        {
            int32_t dx = (int32_t)i - (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS;
            int32_t dy = (int32_t)j - (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS;
            float d2 = (float)(dx * dx + dy * dy);   /* 到核中心的欧氏距离平方 */
            float w = expf(-d2 / (2.0f * APP_DISPLAY_FINE_KERNEL_SIGMA * APP_DISPLAY_FINE_KERNEL_SIGMA));
            s_fine_kernel[i * APP_DISPLAY_FINE_KERNEL_LEN + j] = w;   /* 行优先存储 */
            sum += w;
        }
    }
    if (sum > 0.0f)
    {
        for (i = 0u; i < APP_DISPLAY_FINE_KERNEL_LEN * APP_DISPLAY_FINE_KERNEL_LEN; i++)
        {
            s_fine_kernel[i] /= sum;   /* 2D 核归一化 */
        }
    }

    s_kernel_ready = 1u;   /* 标记内核已就绪，后续调用直接跳过 */
}

/** @brief 返回显示模式的可读名称 ("FAST"/"BAL"/"CLEAN") */
const char *App_Display_ModeName(App_Display_Mode_t mode)
{
    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:
            return "FAST";
        case APP_DISPLAY_MODE_CLEAN:
            return "CLEAN";
        case APP_DISPLAY_MODE_BALANCED:
        default:
            return "BAL";
    }
}

/** @brief 返回插值模式的可读名称 ("BIL"/"NEAR") */
const char *App_Display_InterpName(App_Display_Interp_t interp)
{
    if (interp == APP_DISPLAY_INTERP_BILINEAR)
    {
        return "BIL";
    }
    return "NEAR";
}

/** @brief 返回归一化模式的可读名称 ("FULL"/"FAST") */
const char *App_Display_NormName(App_Display_Norm_t norm)
{
    if (norm == APP_DISPLAY_NORM_FULL)
    {
        return "FULL";
    }
    return "FAST";
}

const char *App_Display_CameraViewName(App_Display_CameraView_t view_mode)
{
    switch (view_mode)
    {
        case APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY:
            return "CAM";
        case APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE:
            return "FRZ";
        case APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY:
            return "HEAT";
        case APP_DISPLAY_CAMERA_VIEW_OVERLAY:
        default:
            return "OVLY";
    }
}

/** @brief 根据显示模式枚举加载默认配置参数 */
static void s_load_mode_defaults(App_Display_Mode_t mode, App_Display_RuntimeCfg_t *cfg)
{
    
    if (cfg == NULL)
    {
        return;
    }

    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:
            cfg->ema_attack = APP_DISPLAY_MODE_FAST_EMA_ATTACK;
            cfg->ema_decay = APP_DISPLAY_MODE_FAST_EMA_DECAY;
            cfg->db_floor = APP_DISPLAY_MODE_FAST_DB_FLOOR;
            cfg->fine_gain = APP_DISPLAY_MODE_FAST_FINE_GAIN;
            cfg->gamma = APP_DISPLAY_MODE_FAST_GAMMA;
            cfg->noise_gate_ratio = APP_DISPLAY_MODE_FAST_NOISE_GATE_RATIO;
            cfg->noise_adapt_gain = APP_DISPLAY_MODE_FAST_NOISE_ADAPT_GAIN;
            cfg->smooth_passes = APP_DISPLAY_MODE_FAST_SMOOTH_PASSES;
            cfg->fine_fusion_enable = APP_DISPLAY_MODE_FAST_FINE_FUSION_ENABLE;
            cfg->draw_coarse_grid = APP_DISPLAY_MODE_FAST_DRAW_COARSE_GRID;
            cfg->interp_mode = (APP_DISPLAY_MODE_FAST_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
            cfg->norm_mode = (APP_DISPLAY_MODE_FAST_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
            cfg->text_refresh_div = APP_DISPLAY_MODE_FAST_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_MODE_FAST_BLIT_ROWS;
            break;

        case APP_DISPLAY_MODE_CLEAN:
            cfg->ema_attack = APP_DISPLAY_MODE_CLEAN_EMA_ATTACK;
            cfg->ema_decay = APP_DISPLAY_MODE_CLEAN_EMA_DECAY;
            cfg->db_floor = APP_DISPLAY_MODE_CLEAN_DB_FLOOR;
            cfg->fine_gain = APP_DISPLAY_MODE_CLEAN_FINE_GAIN;
            cfg->gamma = APP_DISPLAY_MODE_CLEAN_GAMMA;
            cfg->noise_gate_ratio = APP_DISPLAY_MODE_CLEAN_NOISE_GATE_RATIO;
            cfg->noise_adapt_gain = APP_DISPLAY_MODE_CLEAN_NOISE_ADAPT_GAIN;
            cfg->smooth_passes = APP_DISPLAY_MODE_CLEAN_SMOOTH_PASSES;
            cfg->fine_fusion_enable = APP_DISPLAY_MODE_CLEAN_FINE_FUSION_ENABLE;
            cfg->draw_coarse_grid = APP_DISPLAY_MODE_CLEAN_DRAW_COARSE_GRID;
            cfg->interp_mode = (APP_DISPLAY_MODE_CLEAN_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
            cfg->norm_mode = (APP_DISPLAY_MODE_CLEAN_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
            cfg->text_refresh_div = APP_DISPLAY_MODE_CLEAN_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_MODE_CLEAN_BLIT_ROWS;
            break;

        case APP_DISPLAY_MODE_BALANCED:
        default:
            cfg->ema_attack = APP_DISPLAY_EMA_ATTACK;
            cfg->ema_decay = APP_DISPLAY_EMA_DECAY;
            cfg->db_floor = APP_DISPLAY_DYNAMIC_DB_FLOOR;
            cfg->fine_gain = APP_DISPLAY_FINE_GAIN;
            cfg->gamma = APP_DISPLAY_DYNAMIC_GAMMA;
            cfg->noise_gate_ratio = APP_DISPLAY_NOISE_GATE_RATIO;
            cfg->noise_adapt_gain = APP_DISPLAY_NOISE_ADAPT_GAIN;
            cfg->smooth_passes = APP_DISPLAY_SMOOTH_PASSES;
            cfg->fine_fusion_enable = APP_DISPLAY_FINE_FUSION_ENABLE;
            cfg->draw_coarse_grid = APP_DISPLAY_DRAW_COARSE_GRID;
            cfg->interp_mode = (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
            cfg->norm_mode = (APP_DISPLAY_MODE_BALANCED_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
            cfg->text_refresh_div = APP_DISPLAY_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_BLIT_ROWS_MAX;
            break;
    }
}

/**
 * @brief 应用运行时显示配置
 * @details 对各参数进行范围限制后写入 s_cfg, 同时使 LUT 失效以触发重建。
 */
void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg)
{
    
    if (cfg == NULL)
    {
        return;
    }

    s_cfg.ema_attack = s_clamp_f32(cfg->ema_attack, 0.01f, 1.0f);
    s_cfg.ema_decay = s_clamp_f32(cfg->ema_decay, 0.01f, 1.0f);
    s_cfg.db_floor = s_clamp_f32(cfg->db_floor, -80.0f, -6.0f);
    s_cfg.fine_gain = s_clamp_f32(cfg->fine_gain, 0.0f, 3.0f);
    s_cfg.gamma = s_clamp_f32(cfg->gamma, 0.5f, 2.5f);
    s_cfg.noise_gate_ratio = s_clamp_f32(cfg->noise_gate_ratio, 0.0f, 0.6f);
    s_cfg.noise_adapt_gain = s_clamp_f32(cfg->noise_adapt_gain, 0.0f, 6.0f);
    s_cfg.heatmap_opacity = s_clamp_f32(cfg->heatmap_opacity, 0.0f, 1.0f);
    s_cfg.smooth_passes = s_clamp_u8(cfg->smooth_passes, 0u, 3u);
    s_cfg.fine_fusion_enable = (cfg->fine_fusion_enable != 0u) ? 1u : 0u;
    s_cfg.draw_coarse_grid = (cfg->draw_coarse_grid != 0u) ? 1u : 0u;
    s_cfg.interp_mode = (cfg->interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
    s_cfg.norm_mode = (cfg->norm_mode == APP_DISPLAY_NORM_FULL) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
    s_cfg.text_refresh_div = s_clamp_u8(cfg->text_refresh_div, 1u, 20u);
    s_cfg.blit_rows = s_clamp_u8(cfg->blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    s_norm_lut_valid = 0u;
}

/** @brief 读取当前运行时显示配置的副本 */
void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg)
{
    if (cfg != NULL)
    {
        *cfg = s_cfg;
    }
}

void App_Display_GetDebugStats(App_Display_DebugStats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->camera_view_mode = (uint8_t)s_camera_view_mode;
    stats->camera_path_count = s_dbg_camera_path_count;
    stats->camera_overlay_count = s_dbg_camera_overlay_count;
    stats->camera_input_seq = s_dbg_camera_input_seq;
    stats->camera_cache_seq = s_camera_cache_seq;
    stats->camera_cache_valid = s_camera_cache_valid;
}

/** @brief 设置摄像头显示模式 (叠加/独立/冻结/纯热力图) */
void App_Display_SetCameraView(App_Display_CameraView_t view_mode)
{
    if ((view_mode != APP_DISPLAY_CAMERA_VIEW_OVERLAY) &&
        (view_mode != APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY) &&
        (view_mode != APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY) &&
        (view_mode != APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE))
    {
        view_mode = APP_DISPLAY_CAMERA_VIEW_OVERLAY;
    }

    s_camera_view_mode = view_mode;
    s_camera_freeze_w = 0u;
    s_camera_freeze_h = 0u;
    s_camera_freeze_stride = 0u;
    s_camera_freeze_valid = 0u;
}

App_Display_CameraView_t App_Display_GetCameraView(void)
{
    return s_camera_view_mode;
}

/** @brief 切换显示模式并加载对应默认配置 */
void App_Display_SetMode(App_Display_Mode_t mode)
{
    /* H1 fix: 从当前配置初始化,保留 heatmap_opacity 等用户设置 */
    App_Display_RuntimeCfg_t mode_cfg = s_cfg;

    if ((mode != APP_DISPLAY_MODE_FAST) &&
        (mode != APP_DISPLAY_MODE_BALANCED) &&
        (mode != APP_DISPLAY_MODE_CLEAN))
    {
        mode = APP_DISPLAY_MODE_BALANCED;
    }

    s_load_mode_defaults(mode, &mode_cfg);
    s_mode = mode;
    App_Display_SetConfig(&mode_cfg);
}

/** @brief 获取当前显示模式 */
App_Display_Mode_t App_Display_GetMode(void)
{
    return s_mode;
}

/** @brief 查询显示模块是否已完成初始化 */
uint8_t App_Display_IsReady(void)
{
    return s_ready;
}

/** @brief 异步矩形填充 (优先 DMA2D, 失败回退 CPU) */
static void s_fill_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    
    if ((x0 > x1) || (y0 > y1))
    {
        return;
    }

    if (ltdc_fill_async(x0, y0, x1, y1, color) == 0u)
    {
        lcd_fill(x0, y0, x1, y1, color);
    }
}

/** @brief 异步绘制水平线 */
static void s_draw_hline_async(uint16_t x0, uint16_t y, uint16_t x1, uint32_t color)
{
    s_fill_rect_async(x0, y, x1, y, color);
}

/** @brief 异步绘制垂直线 */
static void s_draw_vline_async(uint16_t x, uint16_t y0, uint16_t y1, uint32_t color)
{
    s_fill_rect_async(x, y0, x, y1, color);
}

/** @brief 异步绘制空心矩形边框 */
static void s_draw_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    s_draw_hline_async(x0, y0, x1, color);
    s_draw_hline_async(x0, y1, x1, color);
    s_draw_vline_async(x0, y0, y1, color);
    s_draw_vline_async(x1, y0, y1, color);
}

/** @brief 查询 LTDC 后缓冲区是 A(0) 还是 B(1), 0xFF=未知 */
static uint8_t s_backbuf_slot(void)
{
    
    uint32_t back_addr = ltdc_get_backbuf_addr();

    if (back_addr == s_fb_addr_a)
    {
        return 0u;
    }
    if (back_addr == s_fb_addr_b)
    {
        return 1u;
    }
    return 0xFFu;
}

/** @brief 计算单帧渲染时间预算 (ms), 基于目标帧率 */
static uint32_t s_display_frame_budget_ms(void)
{
    uint32_t fps = App_RuntimeConfig_GetUiTargetFps();
    if (fps < UI_FPS_MIN)
    {
        fps = UI_FPS_MIN;
    }
    if (fps > UI_FPS_MAX)
    {
        fps = UI_FPS_MAX;
    }
    return (1000u + fps - 1u) / fps;
}
static uint8_t s_flush_temp_draw(void)
{
    if (ltdc_draw_flush(APP_DISPLAY_DMA2D_TIMEOUT) != 0u)
    {
        DMA2D_Accel_Reset();
        return 0u;
    }
    return 1u;
}
static void s_submit_rgb565_block(uint16_t sx,
                                  uint16_t sy,
                                  uint16_t ex,
                                  uint16_t ey,
                                  uint16_t *pixels)
{
    if ((pixels == NULL) || (sx > ex) || (sy > ey))
    {
        return;
    }
    if (ltdc_color_fill_async(sx, sy, ex, ey, pixels) == 0u)
    {
        return;
    }
    if (s_flush_temp_draw() != 0u)
    {
        return;
    }
    DMA2D_Accel_Reset();
    lcd_color_fill(sx, sy, ex, ey, pixels);
}

static void s_clean_dcache_by_addr(const void *addr, uint32_t size)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start_addr;
    uintptr_t end_addr;
    uintptr_t aligned_addr;
    uint32_t aligned_size;

    if ((addr == NULL) || (size == 0u))
    {
        return;
    }

    start_addr = (uintptr_t)addr;
    end_addr = start_addr + (uintptr_t)size;
    aligned_addr = start_addr & ~(uintptr_t)31u;
    aligned_size = (uint32_t)(((end_addr + 31u) & ~(uintptr_t)31u) - aligned_addr);
    SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
#else
    (void)addr;
    (void)size;
#endif
}

static uint16_t s_blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    uint32_t a = (uint32_t)alpha;
    uint32_t inv = 255u - a;
    uint32_t bg_r = (bg >> 11) & 0x1Fu;
    uint32_t bg_g = (bg >> 5) & 0x3Fu;
    uint32_t bg_b = bg & 0x1Fu;
    uint32_t fg_r = (fg >> 11) & 0x1Fu;
    uint32_t fg_g = (fg >> 5) & 0x3Fu;
    uint32_t fg_b = fg & 0x1Fu;
    uint32_t out_r = (bg_r * inv + fg_r * a + 127u) / 255u;
    uint32_t out_g = (bg_g * inv + fg_g * a + 127u) / 255u;
    uint32_t out_b = (bg_b * inv + fg_b * a + 127u) / 255u;

    return (uint16_t)((out_r << 11) | (out_g << 5) | out_b);
}
static uint8_t s_overlay_alpha_from_norm(uint8_t norm)
{
    uint32_t scaled;
    uint32_t curved;

    if (norm <= 24u)
    {
        return 0u;
    }

    scaled = ((uint32_t)(norm - 24u) * 255u + 115u) / 231u;
    if (scaled > 255u)
    {
        scaled = 255u;
    }
    curved = (scaled * scaled + 127u) / 255u;
    if (curved > 255u)
    {
        curved = 255u;
    }
    return (uint8_t)((curved * ((uint32_t)APP_CAMERA_OVERLAY_ALPHA_MAX / 2u) + 127u) / 255u);
}
static void s_clear_scene_gutters(void)
{
    uint16_t left_w = s_text_x;
    uint16_t screen_h = lcddev.height;

    if ((left_w == 0u) || (screen_h == 0u) ||
        (s_map_x1 < s_map_x0) || (s_map_y1 < s_map_y0))
    {
        return;
    }

    if (s_map_y0 > 0u)
    {
        s_fill_rect_async(0u, 0u, (uint16_t)(left_w - 1u), (uint16_t)(s_map_y0 - 1u), BLACK);
    }
    if (((uint32_t)s_map_y1 + 1u) < (uint32_t)screen_h)
    {
        s_fill_rect_async(0u,
                          (uint16_t)(s_map_y1 + 1u),
                          (uint16_t)(left_w - 1u),
                          (uint16_t)(screen_h - 1u),
                          BLACK);
    }
    if (s_map_x0 > 0u)
    {
        s_fill_rect_async(0u, s_map_y0, (uint16_t)(s_map_x0 - 1u), s_map_y1, BLACK);
    }
    if (((uint32_t)s_map_x1 + 1u) < (uint32_t)left_w)
    {
        s_fill_rect_async((uint16_t)(s_map_x1 + 1u),
                          s_map_y0,
                          (uint16_t)(left_w - 1u),
                          s_map_y1,
                          BLACK);
    }
}
static void s_commit_frame(void)
{
    /*     *
 */
    if (ltdc_draw_flush(APP_DISPLAY_DMA2D_TIMEOUT) != 0u)
    {
        DMA2D_Accel_Reset();
        return;
    }

    /* BMP 截图期间抑制 swap — 防止前缓冲被覆盖导致撕裂 */
    if (g_display_swap_inhibit != 0u)
    {
        return;
    }

    if (ltdc_is_swap_pending() == 0u)
    {
        ltdc_request_swap();
    }
}

/**
 * @brief 初始化显示模块
 * @details 构建卷积核/LUT、初始化 LCD、计算热力图/摄像头/UI 布局、清屏。
 *          通过 g_display_init_stage 报告进度, g_display_init_error 报告错误。
 */
void App_Display_Init(void)
{
    uint16_t draw_w;     /* LCD 物理宽度（像素）*/
    uint16_t draw_h;     /* LCD 物理高度（像素）*/
    uint16_t camera_w;   /* 摄像头区域宽度（总宽 - 右侧 UI 面板宽）*/
    uint16_t camera_h;   /* 摄像头区域高度（= 总高）*/
    uint16_t heat_w;     /* 热力图宽度（≤camera_w 且 ≤HEAT_VIEW_W）*/
    uint16_t heat_h;     /* 热力图高度（≤camera_h 且 ≤HEAT_VIEW_H）*/

    /* ---- 阶段 1: 初始化诊断变量与模块状态 ---- */
    g_display_init_stage = 1u;   /* 用于 CLI 诊断：1=开始初始化 */
    g_display_init_error = 0u;   /* 清零错误代码 */
    s_ready = 0u;                /* 标记为"未就绪"，防止 render 提前运行 */

    /* 预计算渲染所需的 DSP 内核和颜色 LUT */
    s_build_kernels();           /* 高斯模糊核 + 精细融合核（一次性，结果缓存）*/
    s_build_heat_lut();          /* 构建 256 级热力图 colormap LUT */

    /* 初始化 EMA 峰值追踪参数 */
    s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;   /* 防止初始帧出现全亮画面 */
    s_last_noise_floor = 0.0f;

    /* 清零所有帧缓冲地址和尺寸缓存 */
    s_fb_addr_a = 0u;                /* 前缓冲帧地址（待 ltdc_init 后填入）*/
    s_fb_addr_b = 0u;                /* 后缓冲帧地址 */
    s_cache_map_w = 0u;              /* 插值缓存宽度（0 = 未建立，下次 render 时重建）*/
    s_cache_map_h = 0u;
    s_camera_cache_map_w = 0u;       /* 摄像头缩放缓存尺寸 */
    s_camera_cache_map_h = 0u;
    s_camera_cache_src_w = 0u;       /* 摄像头帧源尺寸 */
    s_camera_cache_src_h = 0u;
    s_camera_cache_seq = 0u;         /* 摄像头缓存帧序号 */
    s_camera_cache_valid = 0u;       /* 摄像头缓存是否有效 */

    /* 清零冻结画面相关状态 */
    s_camera_freeze_w = 0u;
    s_camera_freeze_h = 0u;
    s_camera_freeze_stride = 0u;
    s_camera_freeze_valid = 0u;

    /* 清零调试统计计数器 */
    s_dbg_camera_path_count = 0u;
    s_dbg_camera_overlay_count = 0u;
    s_dbg_camera_input_seq = 0u;

    /* 重置为默认摄像头叠加显示模式 */
    s_camera_view_mode = APP_DISPLAY_CAMERA_VIEW_OVERLAY;

    /* 清零频谱显示相关状态 */
    s_norm_lut_valid = 0u;   /* LUT 失效，下次使用前重建 */
    memset(s_spectrum_ema, 0, sizeof(s_spectrum_ema));
    memset(&s_last_spectrum_frame, 0, sizeof(s_last_spectrum_frame));
    s_spectrum_ref_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
    s_spectrum_ema_valid = 0u;
    s_spectrum_frame_valid = 0u;

    /* 应用默认渲染模式（FAST/BAL/CLEAN）并加载对应参数 */
    App_Display_SetMode((App_Display_Mode_t)APP_DISPLAY_DEFAULT_MODE);

    /* ---- 阶段 2: 初始化 LCD 硬件 ---- */
    g_display_init_stage = 2u;
    lcd_init();   /* 驱动 LTDC + FMC-SDRAM，建立与 LCD 屏的通信 */

    /* ---- 阶段 3: 读取 LCD 分辨率并计算布局 ---- */
    g_display_init_stage = 3u;
    draw_w = lcddev.width;    /* 由 lcd_init 填入，通常 1024 像素 */
    draw_h = lcddev.height;   /* 通常 600 像素 */

    /* 分辨率有效性检查：宽高均不能为 0 */
    if ((draw_w == 0u) || (draw_h == 0u))
    {
        g_display_init_error = 1u;
        g_display_init_stage = 0xE001u;   /* 错误码 E001：LCD init 后宽或高为 0 */
        return;
    }

    /* 检查是否满足最小可用布局：宽度必须大于 UI 面板宽度，高度 ≥ 32px */
    if ((draw_w <= APP_DISPLAY_UI_PANEL_W) || (draw_h < 32u))
    {
        g_display_init_error = 2u;
        g_display_init_stage = 0xE002u;   /* 错误码 E002：屏幕太小无法放置 UI 面板 */
        return;
    }

    /* 计算各区域坐标 */
    camera_w = (uint16_t)(draw_w - APP_DISPLAY_UI_PANEL_W);  /* 左侧视频/热力图区域宽 */
    camera_h = draw_h;                                         /* 视频区域高度 = 全屏高 */

    /* 热力图尺寸：取"屏幕配置"和"目标视图"中的较小值（防止热力图超出屏幕）*/
    heat_w = (camera_w < APP_DISPLAY_HEAT_VIEW_W) ? camera_w : (uint16_t)APP_DISPLAY_HEAT_VIEW_W;
    heat_h = (camera_h < APP_DISPLAY_HEAT_VIEW_H) ? camera_h : (uint16_t)APP_DISPLAY_HEAT_VIEW_H;

    /* 确保热力图有最小可用面积（至少 32×32 像素）*/
    if ((heat_w < 32u) || (heat_h < 32u))
    {
        g_display_init_error = 3u;
        g_display_init_stage = 0xE003u;   /* 错误码 E003：热力图区域太小 */
        return;
    }

    /* 将热力图居中在摄像头区域内 */
    s_map_x0 = (uint16_t)((camera_w - heat_w) / 2u);          /* 热力图左边界 */
    s_map_y0 = (uint16_t)((camera_h - heat_h) / 2u);          /* 热力图上边界 */
    s_map_x1 = (uint16_t)(s_map_x0 + heat_w - 1u);            /* 热力图右边界（含端点）*/
    s_map_y1 = (uint16_t)(s_map_y0 + heat_h - 1u);            /* 热力图下边界（含端点）*/

    /* 摄像头叠加区域与热力图区域一致（全叠加模式）*/
    s_camera_x0 = s_map_x0;
    s_camera_y0 = s_map_y0;
    s_camera_x1 = s_map_x1;
    s_camera_y1 = s_map_y1;

    /* 文字/数值显示区域 X 起始 = 摄像头区域右边（= 热力图右边 + 1）*/
    s_text_x  = camera_w;
    /* UI 面板最右侧像素 */
    s_ui_x1   = (uint16_t)(draw_w - 1u);

    /* 预先填充插值坐标缓存（避免第一帧渲染时触发重建）*/
    s_refresh_render_map_cache((uint16_t)(s_map_x1 - s_map_x0 + 1u),
                               (uint16_t)(s_map_y1 - s_map_y0 + 1u));

    /* ---- 阶段 4: 初始化 LTDC 硬件 + 预加载 CLUT ---- */
    g_display_init_stage = 4u;
    s_fb_addr_a = ltdc_get_frontbuf_addr();   /* 获取前缓冲（当前显示帧）地址 */
    s_fb_addr_b = ltdc_get_backbuf_addr();    /* 获取后缓冲（待绘制帧）地址 */

    /* 将热力图 colormap LUT 预装载到 DMA2D CLUT（硬件 L8→RGB565 转换加速）*/
    DMA2D_Accel_LoadClutFromRgb565(s_heat_lut, APP_DISPLAY_HEAT_LUT_SIZE);

    /* 初始清屏：填充全黑背景，绘制热力图区域边框（白色）*/
    s_fill_rect_async(0u, 0u, (uint16_t)(draw_w - 1u), (uint16_t)(draw_h - 1u), BLACK);
    s_draw_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, WHITE);
    if (s_text_x > 0u)
    {
        /* 在摄像头区域和 UI 面板之间绘制分割线 */
        s_draw_vline_async((uint16_t)(s_text_x - 1u), 0u, (uint16_t)(draw_h - 1u), WHITE);
    }
    s_commit_frame();   /* 提交帧：触发 LTDC 换页，让初始界面显示出来 */

    s_ready = 1u;                   /* 标记模块就绪，render 函数可开始调用 */
    g_display_init_stage = 0x8000u;  /* 成功完成：阶段码 0x8000 */
}

/** @brief 安全取功率值: 过滤 NaN/Inf/负值, 返回 0.0 或正值 */
static float s_power_mag(float v)
{
    
    if (!isfinite(v) || (v <= 0.0f))
    {
        return 0.0f;
    }
    return v;
}

/** @brief 根据编译时配置对输出角度进行 XY 交换/反转 */
static void s_apply_output_remap(float *x_angle, float *y_angle)
{
    
#if (SRP_OUTPUT_SWAP_XY != 0u)
    {
        float t = *x_angle;
        *x_angle = *y_angle;
        *y_angle = t;
    }
#endif
#if (SRP_OUTPUT_INVERT_X != 0u)
    *x_angle = -*x_angle;
#endif
#if (SRP_OUTPUT_INVERT_Y != 0u)
    *y_angle = -*y_angle;
#endif
}

/** @brief s_apply_output_remap 的逆变换 (用于从显示坐标回算原始角度) */
static void s_inverse_output_remap(float *x_angle, float *y_angle)
{
    
#if (SRP_OUTPUT_INVERT_Y != 0u)
    *y_angle = -*y_angle;
#endif
#if (SRP_OUTPUT_INVERT_X != 0u)
    *x_angle = -*x_angle;
#endif
#if (SRP_OUTPUT_SWAP_XY != 0u)
    {
        float t = *x_angle;
        *x_angle = *y_angle;
        *y_angle = t;
    }
#endif
}

/**
 * @brief 将粗搜索网格功率值双线性插值重采样到 s_field_a[]
 * @details 遍历显示场每个像素, 计算对应的粗网格坐标, 进行双线性插值。
 *          支持 output remap (XY交换/反转)。
 */
static void s_resample_coarse_to_field(const SRP_VisFrame_t *vis_frame)
{
    /*     *
 */
    uint32_t y;
    uint32_t x;
    float span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    float inv_span;

    if (span <= 1.0e-6f)
    {
        memset(s_field_a, 0, sizeof(s_field_a));
        return;
    }
    inv_span = 1.0f / span;

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        
        float phi_disp = (float)COARSE_ANGLE_MAX_DEG
                       - ((float)y * span / (float)(APP_DISPLAY_FIELD_H - 1u));
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            
            float theta_disp = (float)COARSE_ANGLE_MIN_DEG
                             + ((float)x * span / (float)(APP_DISPLAY_FIELD_W - 1u));
            
            float theta_raw = theta_disp;
            float phi_raw = phi_disp;
            
            float tx;
            float py;
            uint32_t t0;
            uint32_t t1;
            uint32_t p0;
            uint32_t p1;
            float wt;
            float wp;
            uint32_t idx00;
            uint32_t idx01;
            uint32_t idx10;
            uint32_t idx11;
            float v00;
            float v01;
            float v10;
            float v11;
            float vt0;
            float vt1;

            s_inverse_output_remap(&theta_raw, &phi_raw);

            
            tx = (theta_raw - (float)COARSE_ANGLE_MIN_DEG) * inv_span * (float)(COARSE_GRID_SIZE - 1u);
            py = (phi_raw - (float)COARSE_ANGLE_MIN_DEG) * inv_span * (float)(COARSE_GRID_SIZE - 1u);
            tx = s_clamp_f32(tx, 0.0f, (float)(COARSE_GRID_SIZE - 1u));
            py = s_clamp_f32(py, 0.0f, (float)(COARSE_GRID_SIZE - 1u));

            
            t0 = (uint32_t)tx;
            p0 = (uint32_t)py;
            t1 = (t0 + 1u < COARSE_GRID_SIZE) ? (t0 + 1u) : t0;
            p1 = (p0 + 1u < COARSE_GRID_SIZE) ? (p0 + 1u) : p0;
            wt = tx - (float)t0;
            wp = py - (float)p0;

            idx00 = t0 * COARSE_GRID_SIZE + p0;
            idx01 = t1 * COARSE_GRID_SIZE + p0;
            idx10 = t0 * COARSE_GRID_SIZE + p1;
            idx11 = t1 * COARSE_GRID_SIZE + p1;

            v00 = s_power_mag(vis_frame->power[idx00]);
            v01 = s_power_mag(vis_frame->power[idx01]);
            v10 = s_power_mag(vis_frame->power[idx10]);
            v11 = s_power_mag(vis_frame->power[idx11]);
            
            vt0 = v00 * (1.0f - wt) + v01 * wt;
            vt1 = v10 * (1.0f - wt) + v11 * wt;
            s_field_a[y * APP_DISPLAY_FIELD_W + x] = vt0 * (1.0f - wp) + vt1 * wp;
        }
    }
}

/**
 * @brief 将精细搜索点以高斯核加权叠加到 s_field_a[]
 * @details 遍历所有精细网格点, 对功率超过阈值的点以 s_fine_kernel 高斯核
 *          加权叠加到对应场像素位置, 增强热点细节分辨率。
 */
static void s_apply_fine_fusion(const SRP_VisFrame_t *vis_frame)
{
#if (APP_DISPLAY_FINE_FUSION_ENABLE != 0u)
    
    uint32_t i;
    float peak = 0.0f;
    float theta_span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    float min_keep;

    if ((s_cfg.fine_fusion_enable == 0u) || (theta_span <= 1.0e-6f))
    {
        return;
    }

    for (i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        float mag = s_power_mag(vis_frame->power[i]);
        if (mag > peak)
        {
            peak = mag;
        }
    }
    if (peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK)
    {
        return;
    }
    min_keep = peak * APP_DISPLAY_FINE_MIN_RATIO;

    for (i = COARSE_TOTAL; i < SRP_GRID_TOTAL; i++)
    {
        float mag = s_power_mag(vis_frame->power[i]);
        float theta;
        float phi;
        float u;
        float v;
        int32_t cx;
        int32_t cy;
        int32_t ky;
        int32_t kx;

        if (mag < min_keep)
        {
            
            continue;
        }
        theta = vis_frame->theta_deg[i];
        phi = vis_frame->phi_deg[i];
        s_apply_output_remap(&theta, &phi);
        u = (theta - (float)COARSE_ANGLE_MIN_DEG) / theta_span;
        v = ((float)COARSE_ANGLE_MAX_DEG - phi) / theta_span;
        if ((u < 0.0f) || (u > 1.0f) || (v < 0.0f) || (v > 1.0f))
        {
            continue;
        }
        cx = (int32_t)(u * (float)(APP_DISPLAY_FIELD_W - 1u) + 0.5f);
        cy = (int32_t)(v * (float)(APP_DISPLAY_FIELD_H - 1u) + 0.5f);

        
        for (ky = -(int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; ky <= (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; ky++)
        {
            int32_t fy = cy + ky;
            uint32_t ky_idx;
            if ((fy < 0) || (fy >= (int32_t)APP_DISPLAY_FIELD_H))
            {
                continue;
            }
            ky_idx = (uint32_t)(ky + (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS);
            for (kx = -(int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; kx <= (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; kx++)
            {
                int32_t fx = cx + kx;
                uint32_t kx_idx;
                float w;
                uint32_t fidx;
                if ((fx < 0) || (fx >= (int32_t)APP_DISPLAY_FIELD_W))
                {
                    continue;
                }
                kx_idx = (uint32_t)(kx + (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS);
                w = s_fine_kernel[ky_idx * APP_DISPLAY_FINE_KERNEL_LEN + kx_idx];
                fidx = (uint32_t)fy * APP_DISPLAY_FIELD_W + (uint32_t)fx;
                
                s_field_a[fidx] += s_cfg.fine_gain * mag * w;
            }
        }
    }
#else
    (void)vis_frame;
#endif
}

/**
 * @brief 对 s_field_a[] 执行一次可分离高斯模糊
 * @details 水平方向卷积到 s_field_b[], 垂直方向卷积回 s_field_a[]。
 *          使用 s_blur_kernel[] 高斯核, 边界使用 clamp 填充。
 */
static void s_apply_blur_once(void)
{
#if (APP_DISPLAY_SMOOTH_ENABLE != 0u)
    /*     *
 */
    uint32_t y;
    uint32_t x;
    int32_t k;

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            float acc = 0.0f;
            
            for (k = -(int32_t)APP_DISPLAY_SMOOTH_RADIUS; k <= (int32_t)APP_DISPLAY_SMOOTH_RADIUS; k++)
            {
                int32_t xi = (int32_t)x + k;
                uint32_t xi_c = (uint32_t)((xi < 0) ? 0 : ((xi >= (int32_t)APP_DISPLAY_FIELD_W) ? ((int32_t)APP_DISPLAY_FIELD_W - 1) : xi));
                uint32_t wk = (uint32_t)(k + (int32_t)APP_DISPLAY_SMOOTH_RADIUS);
                acc += s_field_a[y * APP_DISPLAY_FIELD_W + xi_c] * s_blur_kernel[wk];
            }
            s_field_b[y * APP_DISPLAY_FIELD_W + x] = acc;
        }
    }

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            float acc = 0.0f;
            
            for (k = -(int32_t)APP_DISPLAY_SMOOTH_RADIUS; k <= (int32_t)APP_DISPLAY_SMOOTH_RADIUS; k++)
            {
                int32_t yi = (int32_t)y + k;
                uint32_t yi_c = (uint32_t)((yi < 0) ? 0 : ((yi >= (int32_t)APP_DISPLAY_FIELD_H) ? ((int32_t)APP_DISPLAY_FIELD_H - 1) : yi));
                uint32_t wk = (uint32_t)(k + (int32_t)APP_DISPLAY_SMOOTH_RADIUS);
                acc += s_field_b[yi_c * APP_DISPLAY_FIELD_W + x] * s_blur_kernel[wk];
            }
            s_field_a[y * APP_DISPLAY_FIELD_W + x] = acc;
        }
    }
#endif
}

/**
 * @brief 完整场处理流水线: 重采样 -> 精细融合 -> 高斯模糊
 * @return 场中的峰值功率, 用于后续归一化
 */
static float s_prepare_field(const SRP_VisFrame_t *vis_frame)
{
    
    uint32_t i;
    float peak = 0.0f;

    s_resample_coarse_to_field(vis_frame);
    s_apply_fine_fusion(vis_frame);
#if (APP_DISPLAY_SMOOTH_ENABLE != 0u)
    {
        uint8_t p;
        for (p = 0u; p < s_cfg.smooth_passes; p++)
        {
            s_apply_blur_once();
        }
    }
#endif
    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        float v = s_field_a[i];
        if (!isfinite(v) || (v < 0.0f))
        {
            v = 0.0f;
            s_field_a[i] = 0.0f;
        }
        if (v > peak)
        {
            peak = v;
        }
    }
    return peak;
}

/**
 * @brief 完整归一化: ratio -> dB -> 截断 db_floor -> gamma 校正 -> 0..255
 * @param ratio  功率与参考峰值的比值 (0.0~1.0)
 * @return 归一化后的 8bit 值
 */
static uint8_t s_compute_norm_full(float ratio)
{
    
    float db;
    float t;
    uint32_t q;

    db = 20.0f * log10f(s_clamp_f32(ratio, 1.0e-9f, 1.0f));
    if (db <= s_cfg.db_floor)
    {
        return 0u;
    }
    if (db >= 0.0f)
    {
        return 255u;
    }

    t = s_clamp_f32((db - s_cfg.db_floor) / (-s_cfg.db_floor), 0.0f, 1.0f);
    if (fabsf(s_cfg.gamma - 1.0f) > 0.02f)
    {
        t = powf(t, s_cfg.gamma);
    }
    q = (uint32_t)(t * 255.0f + 0.5f);
    return (q > 255u) ? 255u : (uint8_t)q;
}

/** @brief 刷新快速归一化 LUT (当 gamma 或 db_floor 变化时重建) */
static void s_refresh_norm_lut(void)
{
    
    uint32_t i;
    float gamma;
    float db_floor;

    if (s_cfg.norm_mode != APP_DISPLAY_NORM_FAST)
    {
        return;
    }

    gamma = s_cfg.gamma;
    db_floor = s_cfg.db_floor;
    if ((s_norm_lut_valid != 0u) &&
        (fabsf(gamma - s_norm_lut_gamma) < 1.0e-4f) &&
        (fabsf(db_floor - s_norm_lut_db_floor) < 1.0e-4f))
    {
        return;
    }

    for (i = 0u; i <= APP_DISPLAY_NORM_RATIO_LUT_SIZE; i++)
    {
        float ratio = (float)i / (float)APP_DISPLAY_NORM_RATIO_LUT_SIZE;
        if (ratio < 1.0e-9f)
        {
            ratio = 1.0e-9f;
        }
        s_norm_ratio_lut[i] = s_compute_norm_full(ratio);
    }

    s_norm_lut_gamma = gamma;
    s_norm_lut_db_floor = db_floor;
    s_norm_lut_valid = 1u;
}

/** @brief 快速归一化: 用预计算 LUT 代替 log10f/powf */
static uint8_t s_norm_fast_lookup(float ratio)
{
    
    uint32_t idx;

    if (ratio <= 0.0f)
    {
        return 0u;
    }

    ratio = s_clamp_f32(ratio, 0.0f, 1.0f);
    idx = (uint32_t)(ratio * (float)APP_DISPLAY_NORM_RATIO_LUT_SIZE + 0.5f);
    if (idx > APP_DISPLAY_NORM_RATIO_LUT_SIZE)
    {
        idx = APP_DISPLAY_NORM_RATIO_LUT_SIZE;
    }
    return s_norm_ratio_lut[idx];
}

/**
 * @brief 刷新 LCD 像素坐标 -> 场坐标的插值缓存表
 * @details 预计算每个显示像素对应的场网格坐标和权重,
 *          避免每帧渲染时重复计算, 支持最近邻和双线性两种模式。
 */
void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h)
{
    /*     * - 鏉堝湱鏅柦鍏呯秴
 */
    uint16_t x;
    uint16_t y;

    if ((map_w == 0u) || (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    if ((s_cache_map_w == map_w) && (s_cache_map_h == map_h))
    {
        return;
    }

    for (x = 0u; x < map_w; x++)
    {
        float fx;
        uint16_t x0;
        uint16_t x1;
        uint16_t wx;

        
        fx = (map_w > 1u)
           ? ((float)x * (float)(APP_DISPLAY_FIELD_W - 1u) / (float)(map_w - 1u))
           : 0.0f;
        x0 = (uint16_t)fx;
        if (x0 >= APP_DISPLAY_FIELD_W)
        {
            x0 = APP_DISPLAY_FIELD_W - 1u;
        }
        x1 = (x0 + 1u < APP_DISPLAY_FIELD_W) ? (uint16_t)(x0 + 1u) : x0;
        wx = (uint16_t)((fx - (float)x0) * 256.0f + 0.5f);
        if (wx > 256u)
        {
            wx = 256u;
        }

        
        s_col_x0_cache[x] = x0;
        s_col_x1_cache[x] = x1;
        s_col_wx256_cache[x] = wx;
        s_col_near_cache[x] = (wx >= 128u) ? x1 : x0;
    }

    for (y = 0u; y < map_h; y++)
    {
        float fy;
        uint16_t y0;
        uint16_t y1;
        uint16_t wy;

        
        fy = (map_h > 1u)
           ? ((float)y * (float)(APP_DISPLAY_FIELD_H - 1u) / (float)(map_h - 1u))
           : 0.0f;
        y0 = (uint16_t)fy;
        if (y0 >= APP_DISPLAY_FIELD_H)
        {
            y0 = APP_DISPLAY_FIELD_H - 1u;
        }
        y1 = (y0 + 1u < APP_DISPLAY_FIELD_H) ? (uint16_t)(y0 + 1u) : y0;
        wy = (uint16_t)((fy - (float)y0) * 256.0f + 0.5f);
        if (wy > 256u)
        {
            wy = 256u;
        }

        
        s_row_y0_cache[y] = y0;
        s_row_y1_cache[y] = y1;
        s_row_wy256_cache[y] = wy;
        s_row_near_cache[y] = (wy >= 128u) ? y1 : y0;
    }

    s_cache_map_w = map_w;
    s_cache_map_h = map_h;
}

/** @brief 刷新摄像头到显示区域的最近邻缩放缓存 */
static void s_refresh_camera_scale_cache(uint16_t map_w, uint16_t map_h, uint16_t src_w, uint16_t src_h)
{
    uint16_t x;
    uint16_t y;

    if ((map_w == 0u) || (map_h == 0u) || (src_w == 0u) || (src_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) || (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    if ((s_camera_cache_map_w == map_w) &&
        (s_camera_cache_map_h == map_h) &&
        (s_camera_cache_src_w == src_w) &&
        (s_camera_cache_src_h == src_h))
    {
        return;
    }

    for (x = 0u; x < map_w; x++)
    {
        uint32_t src_x = (map_w > 1u)
                       ? ((uint32_t)x * (uint32_t)(src_w - 1u) / (uint32_t)(map_w - 1u))
                       : 0u;
        s_camera_col_near_cache[x] = (src_x >= src_w) ? (uint16_t)(src_w - 1u) : (uint16_t)src_x;
    }

    for (y = 0u; y < map_h; y++)
    {
        uint32_t src_y = (map_h > 1u)
                       ? ((uint32_t)y * (uint32_t)(src_h - 1u) / (uint32_t)(map_h - 1u))
                       : 0u;
        s_camera_row_near_cache[y] = (src_y >= src_h) ? (uint16_t)(src_h - 1u) : (uint16_t)src_y;
    }

    s_camera_cache_map_w = map_w;
    s_camera_cache_map_h = map_h;
    s_camera_cache_src_w = src_w;
    s_camera_cache_src_h = src_h;
}

static void s_render_camera_frame_rows(const App_CameraFrame_t *camera_frame)
{
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint16_t fit_w;
    uint16_t fit_h;
    uint16_t fit_x0;
    uint16_t fit_y0;
    uint16_t fit_x1;
    uint16_t src_x0;
    uint16_t src_y0;
    uint16_t src_stride;
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint16_t y_blk;

    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (map_w == 0u) ||
        (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    fit_w = (map_w < camera_frame->width) ? map_w : camera_frame->width;
    fit_h = (map_h < camera_frame->height) ? map_h : camera_frame->height;
    if ((fit_w == 0u) || (fit_h == 0u))
    {
        return;
    }

    src_x0 = (uint16_t)(((uint32_t)camera_frame->width - (uint32_t)fit_w) / 2u);
    src_y0 = (uint16_t)(((uint32_t)camera_frame->height - (uint32_t)fit_h) / 2u);
    fit_x0 = (uint16_t)(s_map_x0 + ((uint32_t)map_w - (uint32_t)fit_w) / 2u);
    fit_y0 = (uint16_t)(s_map_y0 + ((uint32_t)map_h - (uint32_t)fit_h) / 2u);
    fit_x1 = (uint16_t)(fit_x0 + fit_w - 1u);

    s_fill_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, BLACK);
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;

    for (y_blk = 0u; y_blk < fit_h; y_blk = (uint16_t)(y_blk + blit_rows))
    {
        uint16_t rows = fit_h - y_blk;
        uint16_t row;

        if (rows > blit_rows)
        {
            rows = blit_rows;
        }

        for (row = 0u; row < rows; row++)
        {
            uint16_t src_y = (uint16_t)(src_y0 + y_blk + row);
            const uint16_t *src = &camera_frame->pixels[(uint32_t)src_y * (uint32_t)src_stride + (uint32_t)src_x0];
            uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)fit_w];
            memcpy(dst, src, (size_t)fit_w * sizeof(uint16_t));
        }

        s_submit_rgb565_block(fit_x0,
                              (uint16_t)(fit_y0 + y_blk),
                              fit_x1,
                              (uint16_t)(fit_y0 + y_blk + rows - 1u),
                              s_blit_buf);
    }
}

static uint8_t s_capture_frozen_camera_frame(const App_CameraFrame_t *camera_frame)
{
    uint16_t src_stride;
    uint16_t row;

    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (camera_frame->width > APP_DISPLAY_CAMERA_VIEW_W) ||
        (camera_frame->height > APP_DISPLAY_CAMERA_VIEW_H))
    {
        return 0u;
    }

    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    if (src_stride < camera_frame->width)
    {
        return 0u;
    }

    for (row = 0u; row < camera_frame->height; row++)
    {
        memcpy(&s_camera_cache_pixels[(uint32_t)row * (uint32_t)camera_frame->width],
               &camera_frame->pixels[(uint32_t)row * (uint32_t)src_stride],
               (size_t)camera_frame->width * sizeof(uint16_t));
    }

    s_camera_freeze_w = camera_frame->width;
    s_camera_freeze_h = camera_frame->height;
    s_camera_freeze_stride = camera_frame->width;
    s_camera_freeze_valid = 1u;
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;
    s_clean_dcache_by_addr(s_camera_cache_pixels,
                           (uint32_t)s_camera_freeze_w * (uint32_t)s_camera_freeze_h * 2u);
    return 1u;
}

static void s_render_field_alpha_rows(const App_CameraFrame_t *camera_frame, uint16_t color565)
{
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint16_t src_stride;
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;
    /* 每帧预计算 opacity: float → uint8 (0-255), 避免逐像素浮点运算 */
    uint8_t opacity_u8 = (uint8_t)(s_cfg.heatmap_opacity * 255.0f + 0.5f);
    (void)color565;

    if ((map_w == 0u) ||
        (map_h == 0u) ||
        (camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    s_refresh_render_map_cache(map_w, map_h);
    s_refresh_camera_scale_cache(map_w, map_h, camera_frame->width, camera_frame->height);
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;

    for (y_blk = 0u; y_blk < map_h; y_blk = (uint16_t)(y_blk + blit_rows))
    {
        uint16_t rows = map_h - y_blk;
        uint16_t row;

        if (rows > blit_rows)
        {
            rows = blit_rows;
        }

        for (row = 0u; row < rows; row++)
        {
            uint16_t y = (uint16_t)(y_blk + row);
            uint16_t x;
            uint8_t *dst = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];

            if (use_bilinear != 0u)
            {
                uint16_t y0 = s_row_y0_cache[y];
                uint16_t y1 = s_row_y1_cache[y];
                uint16_t wy = s_row_wy256_cache[y];
                uint16_t wy0 = (uint16_t)(256u - wy);
                const uint8_t *src0 = &s_field_norm_u8[(uint32_t)y0 * APP_DISPLAY_FIELD_W];
                const uint8_t *src1 = &s_field_norm_u8[(uint32_t)y1 * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    uint16_t x0 = s_col_x0_cache[x];
                    uint16_t x1 = s_col_x1_cache[x];
                    uint16_t wx = s_col_wx256_cache[x];
                    uint16_t wx0 = (uint16_t)(256u - wx);
                    uint32_t v00 = src0[x0];
                    uint32_t v01 = src0[x1];
                    uint32_t v10 = src1[x0];
                    uint32_t v11 = src1[x1];
                    uint32_t vx0 = v00 * wx0 + v01 * wx;
                    uint32_t vx1 = v10 * wx0 + v11 * wx;
                    uint32_t q = (vx0 * wy0 + vx1 * wy + 32768u) >> 16;
                    if (q > 255u)
                    {
                        q = 255u;
                    }
                    dst[x] = (uint8_t)q;
                }
            }
            else
            {
                uint16_t y_idx = s_row_near_cache[y];
                const uint8_t *src = &s_field_norm_u8[(uint32_t)y_idx * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = src[s_col_near_cache[x]];
                }
            }
        }

        for (row = 0u; row < rows; row++)
        {
            const uint8_t *src = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];
            uint16_t src_y = s_camera_row_near_cache[(uint32_t)y_blk + (uint32_t)row];
            const uint16_t *bg = &camera_frame->pixels[(uint32_t)src_y * (uint32_t)src_stride];
            uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)map_w];
            uint16_t x;

            for (x = 0u; x < map_w; x++)
            {
                uint8_t alpha = s_overlay_alpha_from_norm(src[x]);
                uint16_t bg_px = bg[s_camera_col_near_cache[x]];
                /* 应用用户透明度: alpha = alpha * opacity_u8 / 255 */
                alpha = (uint8_t)(((uint32_t)alpha * (uint32_t)opacity_u8 + 127u) / 255u);
                dst[x] = (alpha == 0u) ? bg_px : s_blend_rgb565(bg_px, s_heat_lut[src[x]], alpha);
            }
        }
        s_submit_rgb565_block(s_map_x0,
                              (uint16_t)(s_map_y0 + y_blk),
                              s_map_x1,
                              (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                              s_blit_buf);
    }
}

/**
 * @brief 自适应归一化: EMA 峰值跟踪 + 噪声门 -> 8bit 映射
 * @details 1. EMA 跟踪 s_peak_ema 作为归一化参考峰值
 *          2. 计算自适应噪底 (背景功率均值 * noise_adapt_gain)
 *          3. 减去噪底后按 ratio 查 LUT 或完整计算归一化到 0..255
 *          4. 特殊处理: 信号过弱时显示棋盘格测试图案或全黑
 */
static void s_update_norm_field(float field_peak, uint32_t frame_seq)
{
    uint32_t i;
    float ref;              /* 峰值参考（EMA 峰值），归一化分母 */
    float floor_linear;     /* 噪声门限（线性域）：低于此值的像素归零 */
    float bg_sum = 0.0f;    /* 背景能量累加（用于自适应噪声估计） */
    uint32_t bg_cnt = 0u;   /* 背景像素计数 */

    /* ---- 特殊情况 1: 空闲测试图案 ----
     * 若峰值低于最小可信阈值且测试图案开关打开，
     * 则渲染一个棋盘格纹理以区分"无声"和"系统未运行"
     */
    if ((field_peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK) && (APP_DISPLAY_IDLE_TEST_PATTERN != 0u))
    {
        for (i = 0u; i < APP_DISPLAY_FIELD_H; i++)
        {
            uint32_t x;
            for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
            {
                /* 用帧序号低 bits 驱动相位，使棋盘格动起来（视觉确认系统在运行） */
                uint32_t phase = (frame_seq >> 2) & 1u;
                /* 若 (x格 + y格 + 相位) 为奇数则亮，否则暗，形成棋盘 */
                s_field_norm_u8[i * APP_DISPLAY_FIELD_W + x] = ((((x >> 3) + (i >> 3) + phase) & 1u) != 0u) ? 48u : 12u;
            }
        }
        s_last_noise_floor = 0.0f;
        return;
    }

    /* ---- 特殊情况 2: 信号过弱 ----
     * 峰值低于最小可信阈值（DYNAMIC_MIN_PEAK）且测试图案关闭，
     * 则清空归一化场（全黑显示）
     */
    if (field_peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK)
    {
        memset(s_field_norm_u8, 0, sizeof(s_field_norm_u8));
        s_last_noise_floor = 0.0f;
        return;
    }

    /* ---- EMA 峰值作为归一化参考 ----
     * s_peak_ema 由外部每帧用 EMA 更新（attack/decay 双速率）
     * 若 EMA 尚未建立（< MIN_PEAK），用 MIN_PEAK 兜底
     */
    ref = (s_peak_ema < APP_DISPLAY_DYNAMIC_MIN_PEAK) ? APP_DISPLAY_DYNAMIC_MIN_PEAK : s_peak_ema;

    /* ---- 自适应噪声本底估计 ----
     * 收集所有能量值低于当前帧峰值 50% 的像素作为"背景"
     * bg_floor = mean(background) × noise_adapt_gain
     * 将 bg_floor 与固定比例门限 (peak × noise_gate_ratio) 取较大者
     * 这样在安静环境下能更智能地抑制低能背景噪声
     */
    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        if (s_field_a[i] < (field_peak * 0.5f))   /* 低于峰值一半 → 视为背景 */
        {
            bg_sum += s_field_a[i];
            bg_cnt++;
        }
    }

    /* 固定比例噪声门限：能量低于 (peak × noise_gate_ratio) 的区域归零 */
    floor_linear = field_peak * s_cfg.noise_gate_ratio;
    if (bg_cnt > 0u)
    {
        /* 自适应门限：背景均值 × 增益 */
        float bg_floor = (bg_sum / (float)bg_cnt) * s_cfg.noise_adapt_gain;
        if (bg_floor > floor_linear)
        {
            floor_linear = bg_floor;   /* 取两者较大值，保留更强的抑制 */
        }
    }
    /* 钳位：门限不能超过当前帧峰值（否则整帧清零） */
    floor_linear = s_clamp_f32(floor_linear, 0.0f, field_peak);
    s_last_noise_floor = floor_linear;   /* 记录供 UI 调试显示 */

    /* ---- 归一化到 uint8 ----
     * FAST 模式：使用预计算的 LUT（快速查表，每次换参才重建）
     * FULL 模式：每像素逐一调用精确浮点 log/pow 计算
     * 公式（两模式相同）：
     *   v ← (field_a[i] - floor_linear) / ref
     *   norm_u8 ← 255 × clamp(v / LUT_RANGE, 0, 1) ^ (1/gamma) × dB_scale
     */
    if (s_cfg.norm_mode == APP_DISPLAY_NORM_FAST)
    {
        s_refresh_norm_lut();   /* 若参数变化则重建 LUT */
        for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
        {
            float v = s_field_a[i] - floor_linear;    /* 减去噪声门限 */
            if (v <= 0.0f)
            {
                s_field_norm_u8[i] = 0u;   /* 低于门限 → 归零（热力图黑色）*/
                continue;
            }
            /* FAST 查表：ratio = v/ref, 在 LUT 中线性插值得到 uint8 */
            s_field_norm_u8[i] = s_norm_fast_lookup(v / ref);
        }
        return;
    }

    /* FULL 模式：每像素完整 log/pow 计算，精度更高但 CPU 开销更大 */
    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        float v = s_field_a[i] - floor_linear;
        if (v <= 0.0f)
        {
            s_field_norm_u8[i] = 0u;
            continue;
        }
        /* s_compute_norm_full 完整执行：10*log10(v/ref) → clamp → gamma → uint8 */
        s_field_norm_u8[i] = s_compute_norm_full(v / ref);
    }
}

/** @brief 将水平角度映射为热力图区域内的 X 像素坐标 */
static uint16_t s_angle_to_x(float angle)
{
    
    float ratio;
    uint16_t width = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    float span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    int32_t x;

    if (span < 1.0e-6f)
    {
        return s_map_x0;
    }
    ratio = s_clamp_f32((angle - (float)COARSE_ANGLE_MIN_DEG) / span, 0.0f, 1.0f);
    x = (int32_t)(s_map_x0 + ratio * (float)(width - 1u));
    return s_clamp_u16(x, s_map_x0, s_map_x1);
}

/** @brief 将垂直角度映射为热力图区域内的 Y 像素坐标 (Y轴翻转) */
static uint16_t s_angle_to_y(float angle)
{
    
    float ratio;
    uint16_t height = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    float span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    int32_t y;

    if (span < 1.0e-6f)
    {
        return s_map_y0;
    }
    ratio = s_clamp_f32(((float)COARSE_ANGLE_MAX_DEG - angle) / span, 0.0f, 1.0f);
    y = (int32_t)(s_map_y0 + ratio * (float)(height - 1u));
    return s_clamp_u16(y, s_map_y0, s_map_y1);
}

/* ============================================================================
 * 声源轨迹追踪
 * ============================================================================ */

/** @brief 推入一个声源位置到轨迹环形缓冲 */
static void s_trajectory_push(const Sound_Pos_t *pos)
{
    uint8_t i;
    TrajectoryPoint_t *p;

    if (pos->energy < TRAJECTORY_MIN_E)
    {
        return;
    }
    /* 老化所有现有点 */
    for (i = 0u; i < s_traj_count; i++)
    {
        s_traj[i].age++;
        if (s_traj[i].age >= TRAJECTORY_MAX)
        {
            s_traj[i].valid = 0u;
        }
    }
    /* 写入新点 */
    p = &s_traj[s_traj_head];
    p->x = s_angle_to_x(pos->x_angle);
    p->y = s_angle_to_y(pos->y_angle);
    p->age = 0u;
    p->valid = 1u;
    s_traj_head = (uint8_t)((s_traj_head + 1u) % TRAJECTORY_MAX);
    if (s_traj_count < TRAJECTORY_MAX)
    {
        s_traj_count++;
    }
}

/** @brief 在热力图上绘制声源轨迹（淡出小十字） */
static void s_trajectory_draw(void)
{
    uint8_t i;
    for (i = 0u; i < s_traj_count; i++)
    {
        uint16_t color;
        uint8_t alpha;
        uint16_t px, py;
        if (s_traj[i].valid == 0u)
        {
            continue;
        }
        /* 透明度随 age 线性衰减 */
        alpha = (uint8_t)(255u - (uint32_t)s_traj[i].age * 255u / TRAJECTORY_MAX);
        /* 使用半透明白色 → 简化为亮度减半的白色 */
        color = (alpha > 128u) ? WHITE : (uint16_t)0x7BEF; /* 亮白 : 暗白 */
        px = s_traj[i].x;
        py = s_traj[i].y;
        /* 绘制 3x3 十字 */
        if (px >= s_map_x0 && px <= s_map_x1 && py >= s_map_y0 && py <= s_map_y1)
        {
            s_draw_hline_async((uint16_t)(px > s_map_x0 ? px - 1u : px), py,
                               (uint16_t)(px < s_map_x1 ? px + 1u : px), color);
            s_draw_vline_async(px, (uint16_t)(py > s_map_y0 ? py - 1u : py),
                               (uint16_t)(py < s_map_y1 ? py + 1u : py), color);
        }
    }
}

/**
 * @brief 将 8bit 归一化场逐行渲染为 RGB565 并提交到帧缓冲
 * @details 支持双线性/最近邻插值。优先使用 L8+CLUT DMA2D 模式,
 *          失败时回退到 CPU 查表 s_heat_lut[] 转 RGB565。
 */
static void s_render_field_rows(void)
{
    /* 计算热力图区域的像素尺寸（从屏幕坐标缓存中派生） */
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    /* blit_rows：每次 DMA2D 传输的行数，值越大传输次数越少但延迟越高 */
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;   /* 当前块的起始行（以热力图本地坐标为准） */

    /* 尺寸合法性检查：零尺寸或超过缓冲区上限则退出 */
    if ((map_w == 0u) ||
        (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }
    /* 刷新插值坐标缓存（仅在尺寸/分辨率变化时重新计算，后续调用直接复用） */
    s_refresh_render_map_cache(map_w, map_h);

    /* ---- 主渲染循环：按 blit_rows 行数分块渲染 ---- */
    for (y_blk = 0u; y_blk < map_h; y_blk = (uint16_t)(y_blk + blit_rows))
    {
        /* 最后一块可能不足 blit_rows 行，取实际剩余行数 */
        uint16_t rows = map_h - y_blk;
        uint16_t row;

        if (rows > blit_rows)
        {
            rows = blit_rows;   /* 超过 blit_rows 则截断 */
        }

        /* ---- 将本块每行像素写入 L8 缓冲区（s_blit_l8_buf）---- */
        for (row = 0u; row < rows; row++)
        {
            uint16_t y = (uint16_t)(y_blk + row);    /* 目标行（热力图坐标，0=顶）*/
            uint16_t x;
            uint8_t *dst = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];  /* 当前行目标指针 */

            if (use_bilinear != 0u)
            {
                /* ---- 双线性插值路径 ----
                 * 预计算的行缓存：
                 *   y0/y1     = 插值源行号（上/下两行）
                 *   wy        = 下权重（定点 Q8，范围 0~255）
                 *   wy0 = 256 - wy = 上权重
                 * 预计算的列缓存同理：x0/x1/wx/wx0
                 *
                 * 公式（全定点，无浮点）：
                 *   q = (v00*wx0 + v01*wx) * wy0 + (v10*wx0 + v11*wx) * wy
                 *   再右移 16 位（256×256 = 65536 = 2^16）
                 *   + 32768 用于四舍五入
                 */
                uint16_t y0  = s_row_y0_cache[y];     /* 上方源行 */
                uint16_t y1  = s_row_y1_cache[y];     /* 下方源行 */
                uint16_t wy  = s_row_wy256_cache[y];  /* 下行权重（Q8，0~255） */
                uint16_t wy0 = (uint16_t)(256u - wy); /* 上行权重 */
                const uint8_t *src0 = &s_field_norm_u8[(uint32_t)y0 * APP_DISPLAY_FIELD_W];
                const uint8_t *src1 = &s_field_norm_u8[(uint32_t)y1 * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    uint16_t x0  = s_col_x0_cache[x];     /* 左侧源列 */
                    uint16_t x1  = s_col_x1_cache[x];     /* 右侧源列 */
                    uint16_t wx  = s_col_wx256_cache[x];  /* 右列权重 */
                    uint16_t wx0 = (uint16_t)(256u - wx); /* 左列权重 */
                    uint32_t v00 = src0[x0];   /* 左上角样本值 */
                    uint32_t v01 = src0[x1];   /* 右上角样本值 */
                    uint32_t v10 = src1[x0];   /* 左下角样本值 */
                    uint32_t v11 = src1[x1];   /* 右下角样本值 */
                    uint32_t vx0 = v00 * wx0 + v01 * wx;   /* 上行水平内插 */
                    uint32_t vx1 = v10 * wx0 + v11 * wx;   /* 下行水平内插 */
                    /* 垂直内插，加 32768 做四舍五入，再右移 16 位还原 uint8 */
                    uint32_t q = (vx0 * wy0 + vx1 * wy + 32768u) >> 16;
                    dst[x] = (q > 255u) ? 255u : (uint8_t)q;   /* 截断到 [0,255] */
                }
            }
            else
            {
                /* ---- 最近邻插值路径（速度最快，无浮点运算）----
                 * s_row_near_cache[y] 和 s_col_near_cache[x] 存储源场坐标
                 * 直接按索引抄值，没有插值计算
                 */
                uint16_t y_idx = s_row_near_cache[y];   /* 源行（场坐标）*/
                const uint8_t *src = &s_field_norm_u8[(uint32_t)y_idx * APP_DISPLAY_FIELD_W];
                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = src[s_col_near_cache[x]];   /* 直接取最近邻 */
                }
            }
        }

        /* ---- 尝试用 LTDC L8 硬件 CLUT 路径传输（最快，GPU 直接转色）----
         * ltdc_l8_fill_async 若返回 0，说明 LTDC 硬件不支持 L8 或正在忙，
         * 则回退到软件 CPU 路径（依次将 L8 转 RGB565，再用 DMA2D blit）
         */
        if (ltdc_l8_fill_async(s_map_x0,
                               (uint16_t)(s_map_y0 + y_blk),
                               s_map_x1,
                               (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                               s_blit_l8_buf,
                               map_w) == 0u)
        {
            /* ---- 软件回退路径：将 L8 索引逐像素查 heat LUT 转为 RGB565 ---- */
            for (row = 0u; row < rows; row++)
            {
                uint8_t  *src = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];
                uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)map_w];   /* RGB565 输出缓冲 */
                uint16_t x;

                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = s_heat_lut[src[x]];   /* L8 → RGB565，通过预建 colormap LUT */
                }
            }

            /* 将 RGB565 块 blit 到帧缓冲（DMA2D 或 CPU memcpy） */
            s_submit_rgb565_block(s_map_x0,
                                  (uint16_t)(s_map_y0 + y_blk),
                                  s_map_x1,
                                  (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                  s_blit_buf);
        }
        else if (s_flush_temp_draw() != 0u)
        {
            continue;
        }
    }

}

/** @brief 将频带范围限制到有效 bin 区间内 */
static App_FreqBand_t s_spectrum_clamp_band(App_FreqBand_t band, uint16_t bin_count)
{
    uint16_t last_bin;

    if (bin_count == 0u)
    {
        return App_Spectrum_DefaultBand();
    }

    last_bin = (uint16_t)(bin_count - 1u);
    if (band.start_bin > last_bin)
    {
        band.start_bin = last_bin;
    }
    if (band.end_bin > last_bin)
    {
        band.end_bin = last_bin;
    }
    if (band.end_bin < band.start_bin)
    {
        band.end_bin = band.start_bin;
    }

    return band;
}

static uint16_t s_spectrum_display_min_bin(uint16_t bin_count)
{
    uint16_t min_bin = App_Spectrum_HzToBin(APP_SPECTRUM_DISPLAY_MIN_HZ);

    if (bin_count == 0u)
    {
        return 0u;
    }
    if (min_bin == 0u)
    {
        min_bin = 1u;
    }
    if (min_bin >= bin_count)
    {
        min_bin = (uint16_t)(bin_count - 1u);
    }

    return min_bin;
}

static float s_spectrum_axis_min_hz(uint16_t bin_count)
{
    return App_Spectrum_BinToHz(s_spectrum_display_min_bin(bin_count));
}

static float s_spectrum_axis_max_hz(uint16_t bin_count)
{
    if (bin_count == 0u)
    {
        return DELTA_F;
    }

    return App_Spectrum_BinToHz((uint16_t)(bin_count - 1u));
}

static float s_spectrum_freq_norm(float hz, float min_hz, float max_hz)
{
    float clamped_hz = s_clamp_f32(hz, min_hz, max_hz);

    if (max_hz <= min_hz)
    {
        return 0.0f;
    }

    #if (APP_SPECTRUM_FREQ_SCALE_MODE != 0u)
    {
        float log_min = logf(min_hz);
        float log_max = logf(max_hz);

        return (logf(clamped_hz) - log_min) / (log_max - log_min);
    }
    #else
    return (clamped_hz - min_hz) / (max_hz - min_hz);
    #endif
}

static uint16_t s_spectrum_freq_to_y(float hz,
                                     uint16_t plot_y0,
                                     uint16_t plot_h,
                                     float min_hz,
                                     float max_hz)
{
    float norm;
    float y_pos;

    if (plot_h <= 1u)
    {
        return plot_y0;
    }

    norm = s_spectrum_freq_norm(hz, min_hz, max_hz);
    if (APP_SPECTRUM_LOW_FREQ_AT_BOTTOM != 0u)
    {
        y_pos = (float)plot_y0 + ((float)(plot_h - 1u) * (1.0f - norm));
    }
    else
    {
        y_pos = (float)plot_y0 + ((float)(plot_h - 1u) * norm);
    }

    return (uint16_t)(y_pos + 0.5f);
}

static uint16_t s_spectrum_bin_to_y(uint16_t bin,
                                    uint16_t plot_y0,
                                    uint16_t plot_h,
                                    float min_hz,
                                    float max_hz)
{
    return s_spectrum_freq_to_y(App_Spectrum_BinToHz(bin), plot_y0, plot_h, min_hz, max_hz);
}

static uint16_t s_spectrum_db_to_bar_x(float rel_db,
                                       uint16_t plot_x0,
                                       uint16_t plot_w)
{
    float norm;

    if (plot_w <= 1u)
    {
        return plot_x0;
    }

    norm = (rel_db - APP_SPECTRUM_DB_FLOOR) / (0.0f - APP_SPECTRUM_DB_FLOOR);
    norm = s_clamp_f32(norm, 0.0f, 1.0f);
    return (uint16_t)(plot_x0 + ((float)(plot_w - 1u) * norm) + 0.5f);
}

static void s_spectrum_format_freq_label(char *buf, size_t buf_size, float hz)
{
    if ((buf == NULL) || (buf_size == 0u))
    {
        return;
    }

    if (hz >= 1000.0f)
    {
        float khz = hz / 1000.0f;

        if ((khz >= 10.0f) || (fabsf(khz - floorf(khz + 0.5f)) < 0.05f))
        {
            (void)snprintf(buf, buf_size, "%0.0fk", (double)khz);
        }
        else
        {
            (void)snprintf(buf, buf_size, "%0.1fk", (double)khz);
        }
    }
    else
    {
        (void)snprintf(buf, buf_size, "%0.0f", (double)hz);
    }
}

static void s_spectrum_draw_freq_tick(uint16_t panel_x0,
                                      uint16_t plot_x0,
                                      uint16_t plot_x1,
                                      uint16_t plot_y0,
                                      uint16_t plot_y1,
                                      uint16_t y,
                                      float hz,
                                      uint32_t color)
{
    char label[16];
    int32_t label_y = (int32_t)y - 8;

    if ((y < plot_y0) || (y > plot_y1))
    {
        return;
    }

    s_spectrum_format_freq_label(label, sizeof(label), hz);
    lcd_draw_line(plot_x0, y, plot_x1, y, color);

    if (label_y < 0)
    {
        label_y = 0;
    }
    if ((uint32_t)label_y + 16u > lcddev.height)
    {
        label_y = (int32_t)lcddev.height - 16;
    }

    lcd_show_string((uint16_t)(panel_x0 + 2u),
                    (uint16_t)label_y,
                    (uint16_t)(APP_DISPLAY_SPECTRUM_AXIS_LABEL_W - 4u),
                    16u,
                    16u,
                    label,
                    WHITE);
}

static void s_spectrum_draw_guides(uint16_t panel_x0,
                                   uint16_t plot_x0,
                                   uint16_t plot_x1,
                                   uint16_t plot_y0,
                                   uint16_t plot_y1,
                                   float min_hz,
                                   float max_hz)
{
    uint16_t plot_w = (uint16_t)(plot_x1 - plot_x0 + 1u);
    uint16_t plot_h = (uint16_t)(plot_y1 - plot_y0 + 1u);
    uint32_t i;
    char db_floor_label[16];
    static const float k_log_ticks[] = {500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
    static const float k_lin_ticks[] = {6000.0f, 12000.0f, 18000.0f};

    for (i = 1u; i < APP_DISPLAY_SPECTRUM_GUIDE_DIVS; i++)
    {
        uint16_t x = (uint16_t)(plot_x0 + (((uint32_t)(plot_w - 1u) * i) / APP_DISPLAY_SPECTRUM_GUIDE_DIVS));

        lcd_draw_line(x, plot_y0, x, plot_y1, GRAYBLUE);
    }

    s_spectrum_draw_freq_tick(panel_x0,
                              plot_x0,
                              plot_x1,
                              plot_y0,
                              plot_y1,
                              s_spectrum_freq_to_y(min_hz, plot_y0, plot_h, min_hz, max_hz),
                              min_hz,
                              WHITE);
    s_spectrum_draw_freq_tick(panel_x0,
                              plot_x0,
                              plot_x1,
                              plot_y0,
                              plot_y1,
                              s_spectrum_freq_to_y(max_hz, plot_y0, plot_h, min_hz, max_hz),
                              max_hz,
                              WHITE);

    if (APP_SPECTRUM_FREQ_SCALE_MODE != 0u)
    {
        for (i = 0u; i < (sizeof(k_log_ticks) / sizeof(k_log_ticks[0])); i++)
        {
            float hz = k_log_ticks[i];

            if ((hz <= min_hz) || (hz >= max_hz))
            {
                continue;
            }
            s_spectrum_draw_freq_tick(panel_x0,
                                      plot_x0,
                                      plot_x1,
                                      plot_y0,
                                      plot_y1,
                                      s_spectrum_freq_to_y(hz, plot_y0, plot_h, min_hz, max_hz),
                                      hz,
                                      GRAYBLUE);
        }
    }
    else
    {
        for (i = 0u; i < (sizeof(k_lin_ticks) / sizeof(k_lin_ticks[0])); i++)
        {
            float hz = k_lin_ticks[i];

            if ((hz <= min_hz) || (hz >= max_hz))
            {
                continue;
            }
            s_spectrum_draw_freq_tick(panel_x0,
                                      plot_x0,
                                      plot_x1,
                                      plot_y0,
                                      plot_y1,
                                      s_spectrum_freq_to_y(hz, plot_y0, plot_h, min_hz, max_hz),
                                      hz,
                                      GRAYBLUE);
        }
    }

    (void)snprintf(db_floor_label, sizeof(db_floor_label), "%0.0fdB", (double)APP_SPECTRUM_DB_FLOOR);
    lcd_show_string(plot_x0,
                    (uint16_t)(plot_y1 + 2u),
                    42u,
                    16u,
                    16u,
                    db_floor_label,
                    WHITE);
    {
        static char peak_label[] = "0dB";

        lcd_show_string((uint16_t)(plot_x1 - 28u),
                        (uint16_t)(plot_y1 + 2u),
                        28u,
                        16u,
                        16u,
                        peak_label,
                        WHITE);
    }
}

static void s_draw_overlay(const Sound_Pos_t *pos,
                           const App_SpectrumFrame_t *spectrum_frame,
                           float field_peak,
                           uint8_t sai_dma_active)
{
#if (APP_LVGL_ENABLE != 0u)
    /* LVGL 全权管理右侧面板区域，跳过遗留频谱渲染 */
    (void)pos;
    (void)spectrum_frame;
    (void)field_peak;
    (void)sai_dma_active;
    return;
#else
    App_FreqBand_t active_band = App_Spectrum_DefaultBand();
    App_FreqBand_t preview_band = active_band;
    uint16_t panel_x0 = s_text_x;
    uint16_t panel_x1 = s_ui_x1;
    uint16_t plot_x0;
    uint16_t plot_x1;
    uint16_t plot_y0;
    uint16_t plot_y1;
    uint16_t plot_w;
    uint16_t plot_h;
    uint16_t bin_count = APP_SPECTRUM_BIN_COUNT;
    uint16_t min_bin = 1u;
    uint16_t peak_bin = 1u;
    float current_peak_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
    float display_ref_mag;
    uint32_t prev_back_color;
#if (APP_SPECTRUM_INFO_ENABLE != 0u)
    float peak_hz = 0.0f;
#endif
    float min_hz;
    float max_hz;

    if ((panel_x0 >= lcddev.width) || (panel_x1 < panel_x0))
    {
        return;
    }

    prev_back_color = g_back_color;
    g_back_color = BLACK;

    lcd_fill(panel_x0, 0u, panel_x1, (uint16_t)(lcddev.height - 1u), BLACK);
    if (panel_x0 > 0u)
    {
        lcd_fill((uint16_t)(panel_x0 - 1u), 0u, (uint16_t)(panel_x0 - 1u), (uint16_t)(lcddev.height - 1u), WHITE);
    }

    plot_x0 = (uint16_t)(panel_x0 + APP_DISPLAY_SPECTRUM_AXIS_LABEL_W);
    plot_x0 = (uint16_t)(plot_x0 + APP_DISPLAY_SPECTRUM_MARGIN_L);
    plot_x1 = (panel_x1 > APP_DISPLAY_SPECTRUM_MARGIN_R)
            ? (uint16_t)(panel_x1 - APP_DISPLAY_SPECTRUM_MARGIN_R)
            : panel_x1;
    plot_y0 = APP_DISPLAY_SPECTRUM_MARGIN_T;
    plot_y1 = (lcddev.height > APP_DISPLAY_SPECTRUM_MARGIN_B)
            ? (uint16_t)(lcddev.height - APP_DISPLAY_SPECTRUM_MARGIN_B - 1u)
            : (uint16_t)(lcddev.height - 1u);
    if ((plot_x1 <= plot_x0) || (plot_y1 <= plot_y0))
    {
        g_back_color = prev_back_color;
        return;
    }

    plot_w = (uint16_t)(plot_x1 - plot_x0 + 1u);
    plot_h = (uint16_t)(plot_y1 - plot_y0 + 1u);

    if ((spectrum_frame != NULL) && (spectrum_frame->bin_count > 0u))
    {
        uint16_t i;

        bin_count = spectrum_frame->bin_count;
        if (bin_count > APP_SPECTRUM_BIN_COUNT)
        {
            bin_count = APP_SPECTRUM_BIN_COUNT;
        }

        min_bin = s_spectrum_display_min_bin(bin_count);
        active_band = s_spectrum_clamp_band(spectrum_frame->active_band, bin_count);
        preview_band = s_spectrum_clamp_band(spectrum_frame->preview_band, bin_count);

        for (i = 0u; i < bin_count; i++)
        {
            float mag = spectrum_frame->magnitude[i];
            float alpha;

            if ((!isfinite(mag)) || (mag < 0.0f))
            {
                mag = 0.0f;
            }

            if (s_spectrum_ema_valid == 0u)
            {
                s_spectrum_ema[i] = mag;
            }
            else
            {
                alpha = (mag >= s_spectrum_ema[i]) ? APP_SPECTRUM_BAR_ATTACK : APP_SPECTRUM_BAR_DECAY;
                s_spectrum_ema[i] += alpha * (mag - s_spectrum_ema[i]);
            }
        }

        s_spectrum_ema_valid = 1u;
    }
    else if (s_spectrum_ema_valid != 0u)
    {
        min_bin = s_spectrum_display_min_bin(bin_count);
    }

    min_hz = s_spectrum_axis_min_hz(bin_count);
    max_hz = s_spectrum_axis_max_hz(bin_count);

    if (s_spectrum_ema_valid != 0u)
    {
        uint16_t i;

        for (i = min_bin; i < bin_count; i++)
        {
            if (s_spectrum_ema[i] > current_peak_mag)
            {
                current_peak_mag = s_spectrum_ema[i];
                peak_bin = i;
            }
        }
    }

    if (current_peak_mag > s_spectrum_ref_mag)
    {
        s_spectrum_ref_mag += APP_SPECTRUM_REF_ATTACK * (current_peak_mag - s_spectrum_ref_mag);
    }
    else
    {
        s_spectrum_ref_mag += APP_SPECTRUM_REF_DECAY * (current_peak_mag - s_spectrum_ref_mag);
    }
    if (s_spectrum_ref_mag < APP_DISPLAY_SPECTRUM_MIN_MAG)
    {
        s_spectrum_ref_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
    }
    display_ref_mag = s_spectrum_ref_mag;

    lcd_draw_rectangle(plot_x0, plot_y0, plot_x1, plot_y1, WHITE);
    lcd_fill((uint16_t)(plot_x0 + 1u), (uint16_t)(plot_y0 + 1u), (uint16_t)(plot_x1 - 1u), (uint16_t)(plot_y1 - 1u), BLACK);

    {
        uint16_t band_start = (active_band.start_bin < min_bin) ? min_bin : active_band.start_bin;
        uint16_t band_end = (active_band.end_bin < min_bin) ? min_bin : active_band.end_bin;

        if ((band_start < bin_count) && (band_end < bin_count))
        {
            uint16_t y0 = s_spectrum_bin_to_y(band_start, plot_y0, plot_h, min_hz, max_hz);
            uint16_t y1 = s_spectrum_bin_to_y(band_end, plot_y0, plot_h, min_hz, max_hz);
            uint16_t band_y0 = (y0 < y1) ? y0 : y1;
            uint16_t band_y1 = (y0 > y1) ? y0 : y1;

            lcd_fill((uint16_t)(plot_x0 + 1u), band_y0, (uint16_t)(plot_x1 - 1u), band_y1, DARKBLUE);
        }

        if ((preview_band.start_bin != active_band.start_bin) || (preview_band.end_bin != active_band.end_bin))
        {
            uint16_t preview_start = (preview_band.start_bin < min_bin) ? min_bin : preview_band.start_bin;
            uint16_t preview_end = (preview_band.end_bin < min_bin) ? min_bin : preview_band.end_bin;

            if ((preview_start < bin_count) && (preview_end < bin_count))
            {
                uint16_t y0 = s_spectrum_bin_to_y(preview_start, plot_y0, plot_h, min_hz, max_hz);
                uint16_t y1 = s_spectrum_bin_to_y(preview_end, plot_y0, plot_h, min_hz, max_hz);
                uint16_t box_y0 = (y0 < y1) ? y0 : y1;
                uint16_t box_y1 = (y0 > y1) ? y0 : y1;

                lcd_draw_rectangle(plot_x0, box_y0, plot_x1, box_y1, LIGHTBLUE);
            }
        }
    }

    s_spectrum_draw_guides(panel_x0, plot_x0, plot_x1, plot_y0, plot_y1, min_hz, max_hz);

    if ((s_spectrum_ema_valid != 0u) && (display_ref_mag > APP_DISPLAY_SPECTRUM_MIN_MAG))
    {
        uint16_t i;

        for (i = min_bin; i < bin_count; i++)
        {
            float rel_db;
            uint16_t y_center = s_spectrum_bin_to_y(i, plot_y0, plot_h, min_hz, max_hz);
            uint16_t y_next = (i + 1u < bin_count)
                            ? s_spectrum_bin_to_y((uint16_t)(i + 1u), plot_y0, plot_h, min_hz, max_hz)
                            : plot_y0;
            uint16_t y_prev = (i > min_bin)
                            ? s_spectrum_bin_to_y((uint16_t)(i - 1u), plot_y0, plot_h, min_hz, max_hz)
                            : plot_y1;
            uint16_t bar_y0;
            uint16_t bar_y1;
            uint16_t bar_x1;
            uint32_t color = ((i >= active_band.start_bin) && (i <= active_band.end_bin)) ? YELLOW : CYAN;

            rel_db = 20.0f * log10f(fmaxf(s_spectrum_ema[i], APP_DISPLAY_SPECTRUM_MIN_MAG) / display_ref_mag);
            if (rel_db < APP_SPECTRUM_DB_FLOOR)
            {
                rel_db = APP_SPECTRUM_DB_FLOOR;
            }

            bar_x1 = s_spectrum_db_to_bar_x(rel_db, (uint16_t)(plot_x0 + 1u), (uint16_t)(plot_w - 2u));

            bar_y0 = (uint16_t)((y_center + y_next) / 2u);
            bar_y1 = (uint16_t)((y_center + y_prev) / 2u);
            if (bar_y1 < bar_y0)
            {
                uint16_t tmp = bar_y0;
                bar_y0 = bar_y1;
                bar_y1 = tmp;
            }
            if (bar_y1 < plot_y0)
            {
                bar_y1 = plot_y0;
            }
            if (bar_y0 > plot_y1)
            {
                bar_y0 = plot_y1;
            }

            lcd_fill((uint16_t)(plot_x0 + 1u), bar_y0, bar_x1, bar_y1, color);
        }

#if (APP_SPECTRUM_INFO_ENABLE != 0u)
        peak_hz = App_Spectrum_BinToHz(peak_bin);
#endif
        {
            uint16_t peak_y = s_spectrum_bin_to_y(peak_bin, plot_y0, plot_h, min_hz, max_hz);
            uint16_t marker_x1 = (uint16_t)(plot_x0 + ((plot_w > 14u) ? 12u : (plot_w - 1u)));

            lcd_draw_line(plot_x0, peak_y, marker_x1, peak_y, RED);
        }
    }
    else
    {
        static char idle_msg[] = "FFT idle";

        lcd_show_string((uint16_t)(plot_x0 + 12u),
                        (uint16_t)(plot_y0 + (plot_h / 2u) - 8u),
                        (uint16_t)(plot_w - 24u),
                        16u,
                        16u,
                        idle_msg,
                        GRAY);
    }

#if (APP_SPECTRUM_INFO_ENABLE != 0u)
    {
        char line[72];
        uint16_t info_x = (uint16_t)(plot_x0 + 4u);
        uint16_t info_y = (uint16_t)(plot_y0 + 4u);
        float band_lo_hz = App_Spectrum_BinToHz(active_band.start_bin);
        float band_hi_hz = App_Spectrum_BinToHz(active_band.end_bin);

        (void)snprintf(line, sizeof(line), "Band %0.1f~%0.0fHz", (double)band_lo_hz, (double)band_hi_hz);
        lcd_show_string(info_x, info_y, (uint16_t)(plot_w - 8u), 16u, 16u, line, LIGHTBLUE);

        (void)snprintf(line, sizeof(line), "Peak %0.1fkHz", (double)(peak_hz / 1000.0f));
        lcd_show_string(info_x, (uint16_t)(info_y + 16u), (uint16_t)(plot_w - 8u), 16u, 16u, line, YELLOW);

        (void)snprintf(line, sizeof(line), "Pos %+4.1f %+4.1f E %0.2f", (double)pos->x_angle, (double)pos->y_angle, isfinite(pos->energy) ? (double)pos->energy : 0.0);
        lcd_show_string(info_x, (uint16_t)(info_y + 32u), (uint16_t)(plot_w - 8u), 16u, 16u, line, CYAN);

        (void)snprintf(line,
                       sizeof(line),
                       "%s S:%s F:%0.1e",
                       App_Display_ModeName(s_mode),
                       (sai_dma_active != 0u) ? "ON" : "OFF",
                       (double)field_peak);
        lcd_show_string(info_x, (uint16_t)(info_y + 48u), (uint16_t)(plot_w - 8u), 16u, 16u, line, WHITE);
    }
#else
    (void)pos;
    (void)field_peak;
    (void)sai_dma_active;
    (void)s_last_noise_floor;
#endif

    g_back_color = prev_back_color;
#endif /* !APP_LVGL_ENABLE */
}

/**
 * @brief 主渲染函数 (每帧由 UI 任务调用)
 * @details 完整渲染流程:
 *          1. 等待 LTDC swap 完成 (帧同步)
 *          2. s_prepare_field: 重采样+融合+模糊
 *          3. EMA 峰值更新
 *          4. s_update_norm_field: 归一化到 8bit
 *          5. 按 camera_view_mode 渲染: 纯热力图/摄像头叠加/冻结帧
 *          6. 绘制准星和峰值标记
 *          7. 频谱图 + UI 文字覆盖层
 *          8. LVGL overlay 合成
 *          9. 提交帧缓冲 (swap)
 */
void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        const App_CameraFrame_t *camera_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active)
{
    float field_peak;                         /* 当前帧 SRP 功率场峰值（线性域）*/
    uint32_t t_perf;                          /* 性能计时起点（DWT 计数）*/
    App_SpectrumFrame_t spectrum_snapshot;    /* 频谱帧局部快照 */
    const App_SpectrumFrame_t *spectrum_frame = NULL; /* 传入 s_draw_overlay 的频谱指针 */
    uint8_t back_slot = s_backbuf_slot();     /* 当前后缓冲槽号（0 或 1） */
    uint32_t peak_idx = 0u;     /* SRP 功率场峰值点在网格中的索引 */
    float peak_theta = 0.0f;    /* 峰值点的水平角度（°，映射后）*/
    float peak_phi = 0.0f;      /* 峰值点的俯仰角度（°，映射后）*/
    uint8_t camera_valid;       /* 摄像头帧是否有效（有数据 + 尺寸合法）*/

    /* 就绪检查：模块未初始化或关键输入为 NULL 时直接返回 */
    if ((s_ready == 0u) || (pos == NULL) || (vis_frame == NULL))
    {
        return;
    }
    (void)back_slot;   /* 当前版本未直接使用 back_slot，但保留供未来 LTDC 双缓冲扩展用 */

    /* ---- 等待上一帧 LTDC 换页完成 ----
     * 确保在写入后缓冲前前一帧已经显示出来（防止撕裂）
     * 超时时间 = 一帧预算（约 33ms @30fps），超时则跳过本帧
     */
    if (ltdc_wait_for_swap_complete(s_display_frame_budget_ms()) != 0u)
    {
        return;
    }

    /* ---- 步骤 1: 准备功率场 (s_prepare_field) ----
     * 将 SRP 功率网格（粗搜+精细）上采样到 APP_DISPLAY_FIELD_W × FIELD_H 的显示分辨率
     * 可选：精细融合（将精细网格叠加到粗搜场）、高斯平滑（space smooth passes）
     * 输出：s_field_a[] 浮点功率场，返回场峰值
     */
    t_perf = App_Perf_BeginCycles();
    field_peak = s_prepare_field(vis_frame);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_PREPARE, t_perf);

    /* ---- 步骤 2: 更新 EMA 峰值跟踪 ----
     * 双速率 EMA：上升边（fast attack）和下降边（slow decay）分开跟踪
     * 保证热力图在声源出现时快速点亮，消失时缓慢淡出（视觉效果更自然）
     * s_peak_ema 作为后续归一化的参考值
     */
    if (field_peak > s_peak_ema)
    {
        /* 上升沿：快速追随（attack 系数通常较大，如 0.3～0.7）*/
        s_peak_ema += s_cfg.ema_attack * (field_peak - s_peak_ema);
    }
    else
    {
        /* 下降沿：缓慢衰减（decay 系数通常较小，如 0.05～0.2）*/
        s_peak_ema += s_cfg.ema_decay * (field_peak - s_peak_ema);
    }
    /* 防止 EMA 跌入数值不稳定区（低于最小可信峰值就钳位）*/
    if (s_peak_ema < APP_DISPLAY_EMA_MIN_PEAK)
    {
        s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
    }

    /* ---- 步骤 3: 归一化场到 uint8 (s_update_norm_field) ----
     * 包含：噪声门限自适应估计、动态范围映射（dBFloor~0dB → 0~255）、伽马校正
     * 输出：s_field_norm_u8[] 供后续 s_render_field_rows 使用
     */
    t_perf = App_Perf_BeginCycles();
    s_update_norm_field(field_peak, frame_seq);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_NORM, t_perf);

    /* 判断摄像头帧是否可用（有效帧必须同时满足：非 NULL + valid + pixels + 非零尺寸）*/
    camera_valid = (uint8_t)((camera_frame != NULL) &&
                             (camera_frame->valid != 0u) &&
                             (camera_frame->pixels != NULL) &&
                             (camera_frame->width != 0u) &&
                             (camera_frame->height != 0u));

    /* ---- 步骤 4: 渲染主视图（摄像头/热力图/叠加）----  */
    t_perf = App_Perf_BeginCycles();
    s_clear_scene_gutters();   /* 清除上一帧可能残留在边缘的旧像素 */

    /* 有摄像头帧（或冻结帧），且当前模式不是纯热力图 */
    if (((camera_valid != 0u) ||
         ((s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE) && (s_camera_freeze_valid != 0u))) &&
        (s_camera_view_mode != APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY))
    {
        s_dbg_camera_path_count++;   /* 统计走到摄像头路径的帧数（用于调试） */
        s_dbg_camera_input_seq = (camera_valid != 0u) ? camera_frame->seq : s_camera_cache_seq;

        if (s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY)
        {
            /* 纯摄像头模式：直接渲染摄像头帧，不叠加热力图 */
            s_render_camera_frame_rows(camera_frame);
        }
        else if (s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE)
        {
            App_CameraFrame_t frozen_frame;

            /* 若冻结帧尚未捕获，先捕获当前摄像头帧存入 SDRAM 缓存 */
            if (s_camera_freeze_valid == 0u)
            {
                (void)s_capture_frozen_camera_frame(camera_frame);
            }

            /* 用缓存中的冻结帧构造临时 App_CameraFrame_t 并渲染 */
            memset(&frozen_frame, 0, sizeof(frozen_frame));
            if (s_camera_freeze_valid != 0u)
            {
                frozen_frame.pixels = s_camera_cache_pixels;   /* 指向 SDRAM 冻结像素 */
                frozen_frame.width  = s_camera_freeze_w;
                frozen_frame.height = s_camera_freeze_h;
                frozen_frame.stride = s_camera_freeze_stride;
                frozen_frame.seq    = s_camera_cache_seq;
                frozen_frame.valid  = 1u;
                s_render_camera_frame_rows(&frozen_frame);
            }
        }
        else
        {
            /* 叠加模式（默认）：以半透明方式将热力图叠加到摄像头帧上
             * APP_CAMERA_OVERLAY_COLOR_565 为热力图在摄像头上的着色键（chroma-key）
             */
            s_render_field_alpha_rows(camera_frame, APP_CAMERA_OVERLAY_COLOR_565);
            s_dbg_camera_overlay_count++;   /* 统计叠加帧数 */
        }
    }

    /* 无摄像头帧，或纯热力图模式：黑底上直接渲染热力图 */
    if ((((camera_valid == 0u) &&
          !((s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE) && (s_camera_freeze_valid != 0u))) ||
         (s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY)))
    {
        s_camera_cache_valid = 0u;                                   /* 缓存失效（无摄像头数据可用）*/
        s_fill_rect_async(s_camera_x0, s_camera_y0, s_camera_x1, s_camera_y1, BLACK);  /* 黑底 */
        s_render_field_rows();   /* 渲染热力图（L8 CLUT 硬件加速 或 CPU 软件回退）*/
    }

    /* ---- 步骤 5: 绘制固定图形元素 ---- */
    /* 热力图区域白色边框 */
    s_draw_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, WHITE);
    /* 摄像头区域与 UI 面板之间的分割竖线 */
    if (s_text_x > 0u)
    {
        s_draw_vline_async((uint16_t)(s_text_x - 1u), 0u, (uint16_t)(lcddev.height - 1u), WHITE);
    }

    /* ---- 步骤 6: 绘制声源定位十字准星 ----
     * 从 pos->x_angle/y_angle 映射到像素坐标，绘制白色十字线（水平+垂直）
     */
    if (vis_frame->peak_idx < SRP_GRID_TOTAL)
    {
        peak_idx   = vis_frame->peak_idx;      /* SRP 功率峰值点索引 */
        peak_theta = vis_frame->theta_deg[peak_idx];  /* 水平角（原始坐标系）*/
        peak_phi   = vis_frame->phi_deg[peak_idx];    /* 俯仰角（原始坐标系）*/
    }
    s_apply_output_remap(&peak_theta, &peak_phi);   /* 应用 XY 交换/反转映射 */

    {
        /* 将方位角/仰角转换为屏幕像素坐标，并钳位到热力图边界内 */
        float ax = s_clamp_f32(pos->x_angle, (float)COARSE_ANGLE_MIN_DEG, (float)COARSE_ANGLE_MAX_DEG);
        float ay = s_clamp_f32(pos->y_angle, (float)COARSE_ANGLE_MIN_DEG, (float)COARSE_ANGLE_MAX_DEG);
        uint16_t cx = s_angle_to_x(ax);    /* 当前帧声源 X 像素坐标 */
        uint16_t cy = s_angle_to_y(ay);    /* 当前帧声源 Y 像素坐标 */
        uint16_t half = APP_DISPLAY_CROSSHAIR_HALF_PX;   /* 十字臂半长（像素）*/
        uint16_t xl = (cx > (uint16_t)(s_map_x0 + half)) ? (uint16_t)(cx - half) : s_map_x0;
        uint16_t xr = ((uint32_t)cx + half < s_map_x1) ? (uint16_t)(cx + half) : s_map_x1;
        uint16_t yt = (cy > (uint16_t)(s_map_y0 + half)) ? (uint16_t)(cy - half) : s_map_y0;
        uint16_t yb = ((uint32_t)cy + half < s_map_y1) ? (uint16_t)(cy + half) : s_map_y1;
        /* 水平臂 + 垂直臂（白色）*/
        s_draw_hline_async(xl, cy, xr, WHITE);
        s_draw_vline_async(cx, yt, yb, WHITE);
    }

    {
        /* 绘制 SRP 峰值点矩形标记（与 EMA 声源标注有别：这是帧峰值位置，不含时间平滑）*/
        uint16_t px = s_angle_to_x(peak_theta);
        uint16_t py = s_angle_to_y(peak_phi);
        uint16_t r  = APP_DISPLAY_PEAK_MARKER_RADIUS_PX;   /* 峰值方框半宽（像素）*/
        uint16_t x0 = (px > (uint16_t)(s_map_x0 + r)) ? (uint16_t)(px - r) : s_map_x0;
        uint16_t y0 = (py > (uint16_t)(s_map_y0 + r)) ? (uint16_t)(py - r) : s_map_y0;
        uint16_t x1 = ((uint32_t)px + r < s_map_x1) ? (uint16_t)(px + r) : s_map_x1;
        uint16_t y1 = ((uint32_t)py + r < s_map_y1) ? (uint16_t)(py + r) : s_map_y1;
        s_draw_rect_async(x0, y0, x1, y1, WHITE);   /* 白色矩形框标记峰值位置 */
    }

    /* ---- 多声源标注：为第 2、3 声源绘制彩色十字准星 ---- */
    {
        static const uint32_t multi_colors[MULTI_SOURCE_MAX] = { WHITE, CYAN, YELLOW };
        uint8_t mi;
        for (mi = 1u; mi < g_multi_source.count; mi++)
        {
            float mxa = s_clamp_f32(g_multi_source.sources[mi].x_angle,
                                     (float)COARSE_ANGLE_MIN_DEG,
                                     (float)COARSE_ANGLE_MAX_DEG);
            float mya = s_clamp_f32(g_multi_source.sources[mi].y_angle,
                                     (float)COARSE_ANGLE_MIN_DEG,
                                     (float)COARSE_ANGLE_MAX_DEG);
            uint16_t mcx = s_angle_to_x(mxa);   /* 第 mi 声源 X 像素坐标 */
            uint16_t mcy = s_angle_to_y(mya);   /* 第 mi 声源 Y 像素坐标 */
            uint16_t mh  = APP_DISPLAY_CROSSHAIR_HALF_PX;
            uint16_t mxl = (mcx > (uint16_t)(s_map_x0 + mh)) ? (uint16_t)(mcx - mh) : s_map_x0;
            uint16_t mxr = ((uint32_t)mcx + mh < s_map_x1)    ? (uint16_t)(mcx + mh) : s_map_x1;
            uint16_t myt = (mcy > (uint16_t)(s_map_y0 + mh)) ? (uint16_t)(mcy - mh) : s_map_y0;
            uint16_t myb = ((uint32_t)mcy + mh < s_map_y1)    ? (uint16_t)(mcy + mh) : s_map_y1;
            /* 使用不同颜色区分多声源（第1声源白色，第2青色，第3黄色）*/
            s_draw_hline_async(mxl, mcy, mxr, multi_colors[mi]);
            s_draw_vline_async(mcx, myt, myb, multi_colors[mi]);
        }
    }

    /* ---- 夜间模式全幅十字准星 (绿色) ---- */
    if (g_crosshair_enable != 0u)
    {
        /* 以准星中心坐标（s_map_x0 + g_crosshair_x）为基准绘制全幅绿色十字 */
        uint16_t gx = (uint16_t)(s_map_x0 + g_crosshair_x);
        uint16_t gy = (uint16_t)(s_map_y0 + g_crosshair_y);
        if (gx >= s_map_x0 && gx <= s_map_x1)
            s_draw_vline_async(gx, s_map_y0, s_map_y1, GREEN);   /* 竖线：全高度 */
        if (gy >= s_map_y0 && gy <= s_map_y1)
            s_draw_hline_async(s_map_x0, gy, s_map_x1, GREEN);  /* 横线：全宽度 */
    }

    /* ---- 定向录音选区框 (青色) ---- */
    if (g_select_enable != 0u)
    {
        /* 将热力图相对坐标（g_select_x1/y1/x2/y2）转为屏幕坐标并绘制选区矩形 */
        uint16_t sx0 = (uint16_t)(s_map_x0 + g_select_x1);
        uint16_t sy0 = (uint16_t)(s_map_y0 + g_select_y1);
        uint16_t sx1 = (uint16_t)(s_map_x0 + g_select_x2);
        uint16_t sy1 = (uint16_t)(s_map_y0 + g_select_y2);
        if (sx1 > s_map_x1) sx1 = s_map_x1;   /* 防止超出热力图范围 */
        if (sy1 > s_map_y1) sy1 = s_map_y1;
        s_draw_rect_async(sx0, sy0, sx1, sy1, CYAN);
    }

    /* ---- 声源轨迹（历史路径）---- */
    s_trajectory_push(pos);     /* 将当前帧声源位置压入环形轨迹缓冲 */
    s_trajectory_draw();        /* 将轨迹点从新到旧逐渐淡出绘制 */
    App_Perf_EndCycles(APP_PERF_SEC_DISP_RENDER, t_perf);

    /* ---- 步骤 7: 获取频谱快照（如有新帧）---- */
    if (App_Spectrum_GetLatestFrame(&spectrum_snapshot) != 0u)
    {
        s_last_spectrum_frame = spectrum_snapshot;  /* 保存最新帧备份 */
        s_spectrum_frame_valid = 1u;                /* 标记帧有效 */
    }
    /* 用上一帧有效频谱（持续显示，即使频谱模块暂时没有新帧）*/
    if (s_spectrum_frame_valid != 0u)
    {
        spectrum_frame = &s_last_spectrum_frame;
    }

    /* ---- 步骤 8: 绘制 UI 文字叠加层（角度/能量/dB/帧率等信息）---- */
    t_perf = App_Perf_BeginCycles();
    s_draw_overlay(pos, spectrum_frame, field_peak, sai_dma_active);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_OVERLAY, t_perf);

    /* ---- 步骤 9: 绘制触摸测试（若 APP_TOUCH_TEST_ENABLE != 0）---- */
    App_TouchTest_Render();

    /* ---- 步骤 10: 将 LVGL 叠加层合成到帧缓冲 (最后执行，保证 LVGL 在最上层) ---- */
    App_LvglUi_BlitToDisplay();

    /* ---- 步骤 11: 提交帧（触发 LTDC 换页，新帧开始显示）---- */
    t_perf = App_Perf_BeginCycles();
    s_commit_frame();
    App_Perf_EndCycles(APP_PERF_SEC_DISP_COMMIT, t_perf);
}
