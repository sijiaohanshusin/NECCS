/**
 * @file    app_display.c
 * @brief   声学成像显示模块实现
 * @details
 * 本文件负责把算法层给出的 SRP-PHAT 可视化结果，转换�?LCD 上真正可见的
 * 一帧图像。最终画面由四部分组成：
 * 1. 热力图：展示空间能量分布�? * 2. 十字准星：展示最终定位输出角度�? * 3. 峰值框：展示当前功率场中的最强点位置�? * 4. 文本侧栏：展示坐标、能量、模式、DMA/交换等诊断信息�? *
 * 整个渲染链路可以按如下顺序理解：
 * `SRP_VisFrame_t`
 * -> 将稀疏搜索网格重采样为稠密显示场
 * -> 可选地把细搜索结果重新融合到显示场
 * -> 对场做平滑和动态归一�? * -> 分块写入 LTDC 后台缓冲
 * -> 叠加边框、准星、峰值框和文�? * -> 刷新绘制队列并申请前后台缓冲交换
 *
 * 阅读建议�? * - `App_Display_Init` 关注初始化、布局计算和缓冲绑定�? * - `s_prepare_field` 关注算法结果如何变成可绘制场�? * - `s_update_norm_field` 关注动态范围压缩与亮度稳定机制�? * - `s_render_field_rows` 关注如何�?8bit 热力图送上屏幕�? * - `App_Display_Render` 关注每帧完整时序与实时性取舍�? */
#include "app_display.h"
#include "app_main_task.h"
#include "app_perf.h"

#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "LCD/dma2d_accel.h"
#include "ai_config.h"
#include "mpu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>


/* 显示中间场相关宏�? * 这里的“field”不�?LCD 实际像素，而是绘制前的中间网格�? * 这样设计的好处是�? * - 分辨率比�?细搜索网格高，足以获得较平滑的热力图�? * - 分辨率又远小于整屏逐像素处理，节省 RAM 和计算量�? * - 后续无论采用最近邻还是双线性放大，都有统一的数据来源�?*/
#define APP_DISPLAY_FIELD_PIXELS      (APP_DISPLAY_FIELD_W * APP_DISPLAY_FIELD_H)
#define APP_DISPLAY_BLUR_KERNEL_LEN   (2u * APP_DISPLAY_SMOOTH_RADIUS + 1u)
#define APP_DISPLAY_FINE_KERNEL_LEN   (2u * APP_DISPLAY_FINE_KERNEL_RADIUS + 1u)
#define APP_DISPLAY_CAMERA_CACHE_ADDR 0xC020D000u
#define APP_DISPLAY_CAMERA_CACHE_BYTES (APP_DISPLAY_CAMERA_VIEW_W * APP_DISPLAY_CAMERA_VIEW_H * 2u)
#define APP_DISPLAY_CAMERA_CACHE_LIMIT 0xC0400000u
#if ((APP_CAMERA_ENABLE != 0u) && ((APP_DISPLAY_CAMERA_CACHE_ADDR + APP_DISPLAY_CAMERA_CACHE_BYTES) > APP_DISPLAY_CAMERA_CACHE_LIMIT))
#error "Camera display cache must stay inside the non-cacheable SDRAM window"
#endif

/* ---------------------------------------------------------------------------
 * 模块状�? * ---------------------------------------------------------------------------
 * 下列静态变量都属于“显示模块私有状态”，不是算法状态本身�? * 可按用途分为几组：
 * - 生命周期状态：初始化是否完成、在哪一步失�? * - 布局状态：热力图区域和文本区域�?LCD 上的矩形范围
 * - 动态状态：EMA 峰值、噪声底、文本刷新节流信�? * - 缓存/LUT 状态：为减少逐帧重复计算而保留的辅助数据
 */
static uint8_t s_ready = 0u;
volatile uint32_t g_display_init_stage = 0u;
volatile uint32_t g_display_init_error = 0u;

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
static uint32_t s_last_text_refresh_frame[2] = {0u, 0u};
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
static uint8_t s_camera_cache_full_dirty = 0u;

/* 工作缓冲区说明：
 * - `s_field_a` / `s_field_b`
 *   保存浮点显示场。之所以使用双缓冲，是为了卷积/平滑时避免“边读边写�? *   导致结果污染�? * - `s_field_norm_u8`
 *   保存归一化后�?8bit 强度图，是最终着色和上屏的直接输入�? * - `s_blit_buf` / `s_blit_l8_buf`
 *   保存分块渲染时的临时行块数据，兼�?DMA2D/LTDC 加速路径和软件回退路径�?*/
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

/* 预计算表�? * - `s_blur_kernel`：一维平滑核，供可分离模糊使�? * - `s_fine_kernel`：二维细搜索扩散核，把细网格点能量“撒”回显示�? * - `s_heat_lut`：把 0..255 强度映射�?RGB565 热力图颜�?*/
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
    APP_DISPLAY_SMOOTH_PASSES,
    APP_DISPLAY_FINE_FUSION_ENABLE,
    APP_DISPLAY_DRAW_COARSE_GRID,
    (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_BALANCED_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_TEXT_REFRESH_DIV,
    APP_DISPLAY_BLIT_ROWS_MAX
#endif
};

/* 几何缓存刷新函数在文件后半段定义�? * 它负责把 LCD 坐标到显示场坐标的映射预先算好�?*/
void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h);
static void s_refresh_camera_scale_cache(uint16_t map_w, uint16_t map_h, uint16_t src_w, uint16_t src_h);
static uint32_t s_display_frame_budget_ms(void);
static uint8_t s_flush_temp_draw(void);
static void s_submit_rgb565_block(uint16_t sx,
                                  uint16_t sy,
                                  uint16_t ex,
                                  uint16_t ey,
                                  uint16_t *pixels);
static void s_update_camera_cache_from_frame(const App_CameraFrame_t *camera_frame);
static uint8_t s_blit_camera_cache_region(uint16_t dst_x0,
                                          uint16_t dst_y0,
                                          uint16_t width,
                                          uint16_t height,
                                          uint16_t src_x0,
                                          uint16_t src_y0);

/* 通用钳位工具�? * 显示链路中存在大量“用户可调参数”和“浮点转整数”的过程�? * 钳位函数用于保证数据不会越过合法边界�?*/
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

/* �?8 位无符号值限制在 `[lo, hi]` 范围内�? * 主要用于运行时配置的枚举/小范围参数修正�?*/
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

/* 将有符号中间值限制到 16 位无符号合法范围�? * 常用于像素坐标计算完成后的安全落地�?*/
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

/* �?8bit �?R/G/B 三通道压缩�?RGB565�? * 这是软件回退着色路径使用的基础颜色打包函数�?*/
static uint16_t s_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

/* 输入 `0.0f ~ 1.0f` 的热度比例，输出对应�?RGB565 热力图颜色�? * 函数内部通过 5 个色标控制点做分段线性插值�?*/
static uint16_t s_heat_color(float t)
{
    /* 颜色渐变采用少量控制点线性插值，而不是运行时计算复杂色图�?     * 这样既容易调色，也便于后续预生成 256 �?LUT�?*/
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

/* 构建 256 级热力图颜色查找表�? * 后续只要拿强度值作为索引即可直接得到显示颜色�?*/
static void s_build_heat_lut(void)
{
    /* 预先�?0..255 的热度等级转换成 RGB565，避免逐像素重复算颜色�?*/
    uint32_t i;

    for (i = 0u; i < APP_DISPLAY_HEAT_LUT_SIZE; i++)
    {
        s_heat_lut[i] = s_heat_color((float)i / 255.0f);
    }
}

/* 构建显示模块所需的全部核函数�? * 包括�? * - 一维模糊核：供横向/纵向可分离平�? * - 二维细融合核：供细搜索能量扩�? *
 * 该函数具备“只初始化一次”的保护�?*/
static void s_build_kernels(void)
{
    /* 本函数只需执行一次：
     * - 构建一维平滑核，供横向/纵向两次卷积复用
     * - 构建二维细融合核，把离散细峰值扩展为更易观察的能量团 */
    uint32_t i;
    uint32_t j;
    float sum = 0.0f;

    if (s_kernel_ready != 0u)
    {
        return;
    }

    for (i = 0u; i < APP_DISPLAY_BLUR_KERNEL_LEN; i++)
    {
        int32_t dx = (int32_t)i - (int32_t)APP_DISPLAY_SMOOTH_RADIUS;
        float w = expf(-((float)(dx * dx)) / (2.0f * APP_DISPLAY_SMOOTH_SIGMA * APP_DISPLAY_SMOOTH_SIGMA));
        s_blur_kernel[i] = w;
        sum += w;
    }
    if (sum > 0.0f)
    {
        for (i = 0u; i < APP_DISPLAY_BLUR_KERNEL_LEN; i++)
        {
            s_blur_kernel[i] /= sum;
        }
    }

    sum = 0.0f;
    for (i = 0u; i < APP_DISPLAY_FINE_KERNEL_LEN; i++)
    {
        for (j = 0u; j < APP_DISPLAY_FINE_KERNEL_LEN; j++)
        {
            int32_t dx = (int32_t)i - (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS;
            int32_t dy = (int32_t)j - (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS;
            float d2 = (float)(dx * dx + dy * dy);
            float w = expf(-d2 / (2.0f * APP_DISPLAY_FINE_KERNEL_SIGMA * APP_DISPLAY_FINE_KERNEL_SIGMA));
            s_fine_kernel[i * APP_DISPLAY_FINE_KERNEL_LEN + j] = w;
            sum += w;
        }
    }
    if (sum > 0.0f)
    {
        for (i = 0u; i < APP_DISPLAY_FINE_KERNEL_LEN * APP_DISPLAY_FINE_KERNEL_LEN; i++)
        {
            s_fine_kernel[i] /= sum;
        }
    }

    s_kernel_ready = 1u;
}

/* 把模式枚举转换为短标签字符串，主要给侧边栏显示使用�?*/
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

/* 把插值模式转换为短标签字符串�?*/
const char *App_Display_InterpName(App_Display_Interp_t interp)
{
    if (interp == APP_DISPLAY_INTERP_BILINEAR)
    {
        return "BIL";
    }
    return "NEAR";
}

/* 把归一化模式转换为短标签字符串�?*/
const char *App_Display_NormName(App_Display_Norm_t norm)
{
    if (norm == APP_DISPLAY_NORM_FULL)
    {
        return "FULL";
    }
    return "FAST";
}

/* 根据预设模式装载一组推荐参数�? * 注意这里只是填充 `cfg`，真正生效仍需调用 `App_Display_SetConfig`�?*/
static void s_load_mode_defaults(App_Display_Mode_t mode, App_Display_RuntimeCfg_t *cfg)
{
    /* 预设模式不是简单的“单参数开关”，而是整组参数协同调整�?     * 这样可以保证用户切换模式后，显示观感是成体系变化的�?*/
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

/* 写入运行时配置�? * 关键点：
 * - 对所有数值做合法区间钳位
 * - 对布尔开关做 0/1 归一�? * - 配置变化后使归一�?LUT 失效，确保后续按新参数重�?*/
void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg)
{
    /* 所有运行时配置在这里统一做边界修正，避免非法配置把后续渲染路�?     * 推入未定义状态，例如负的 gamma、过大的 blit 行数等�?*/
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
    s_cfg.smooth_passes = s_clamp_u8(cfg->smooth_passes, 0u, 3u);
    s_cfg.fine_fusion_enable = (cfg->fine_fusion_enable != 0u) ? 1u : 0u;
    s_cfg.draw_coarse_grid = (cfg->draw_coarse_grid != 0u) ? 1u : 0u;
    s_cfg.interp_mode = (cfg->interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
    s_cfg.norm_mode = (cfg->norm_mode == APP_DISPLAY_NORM_FULL) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
    s_cfg.text_refresh_div = s_clamp_u8(cfg->text_refresh_div, 1u, 20u);
    s_cfg.blit_rows = s_clamp_u8(cfg->blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    s_norm_lut_valid = 0u;
}

/* 读取当前生效配置到调用者提供的结构体中�?*/
void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg)
{
    if (cfg != NULL)
    {
        *cfg = s_cfg;
    }
}

/* 切换显示模式�? * 若输入非法模式，则回退�?`APP_DISPLAY_MODE_BALANCED`�?*/
void App_Display_SetMode(App_Display_Mode_t mode)
{
    App_Display_RuntimeCfg_t mode_cfg;

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

/* 返回当前显示模式�?*/
App_Display_Mode_t App_Display_GetMode(void)
{
    return s_mode;
}

/* 返回显示模块是否已完成初始化�?*/
uint8_t App_Display_IsReady(void)
{
    return s_ready;
}

/* 异步填充矩形区域�? * 优先�?LTDC/DMA2D 异步路径；失败时回退�?`lcd_fill`�?*/
static void s_fill_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    /* 优先尝试异步加速填充；若底层不支持或提交失败，则回退到同步软件路径�?     * 这样上层无需关心当前平台是否具备 DMA2D/LTDC 加速能力�?*/
    if ((x0 > x1) || (y0 > y1))
    {
        return;
    }

    if (ltdc_fill_async(x0, y0, x1, y1, color) == 0u)
    {
        lcd_fill(x0, y0, x1, y1, color);
    }
}

/* 绘制水平线，本质上是高度�?1 的矩形填充�?*/
static void s_draw_hline_async(uint16_t x0, uint16_t y, uint16_t x1, uint32_t color)
{
    s_fill_rect_async(x0, y, x1, y, color);
}

/* 绘制垂直线，本质上是宽度�?1 的矩形填充�?*/
static void s_draw_vline_async(uint16_t x, uint16_t y0, uint16_t y1, uint32_t color)
{
    s_fill_rect_async(x, y0, x, y1, color);
}

/* 绘制空心矩形边框�? * 通过四条边组合而成，不单独填充内部区域�?*/
static void s_draw_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    s_draw_hline_async(x0, y0, x1, color);
    s_draw_hline_async(x0, y1, x1, color);
    s_draw_vline_async(x0, y0, y1, color);
    s_draw_vline_async(x1, y0, y1, color);
}

/* 识别当前后台缓冲属于哪一个槽位�? * 返回值约定：
 * - `0`：后台缓冲等�?A
 * - `1`：后台缓冲等�?B
 * - `0xFF`：无法识�?*/
static uint8_t s_backbuf_slot(void)
{
    /* 根据后台缓冲地址判断当前正在绘制的是 A 还是 B�?     * 文本面板刷新节流逻辑依赖这个槽位信息，避免双缓冲切换后节流状态错乱�?*/
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

/* 提交本帧绘制结果�? * 包括冲刷底层绘制队列以及申请 front/back swap�?*/
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
static void s_commit_frame(void)
{
    /* 提交阶段分两步：
     * 1. 等待/刷新底层绘制队列
     * 2. 若当前没有未完成交换，再申请一�?front/back swap
     *
     * �?DMA2D 路径异常超时，会主动复位加速器，防止后续帧长期卡死�?*/
    if (ltdc_draw_flush(APP_DISPLAY_DMA2D_TIMEOUT) != 0u)
    {
        DMA2D_Accel_Reset();
        return;
    }

    if (ltdc_is_swap_pending() == 0u)
    {
        ltdc_request_swap();
    }
}

/* 初始化显示模块�? * 这是整个模块的上电入口，负责�? * - 建表
 * - 初始�?LCD
 * - 计算布局
 * - 获取帧缓冲地址
 * - 绘制首帧边框和背�?*/
void App_Display_Init(void)
{
    uint16_t draw_w;
    uint16_t draw_h;
    uint16_t camera_w;
    uint16_t camera_h;
    uint16_t heat_w;
    uint16_t heat_h;
    g_display_init_stage = 1u;
    g_display_init_error = 0u;
    s_ready = 0u;
    s_build_kernels();
    s_build_heat_lut();
    s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
    s_last_noise_floor = 0.0f;
    s_last_text_refresh_frame[0] = 0u;
    s_last_text_refresh_frame[1] = 0u;
    s_fb_addr_a = 0u;
    s_fb_addr_b = 0u;
    s_cache_map_w = 0u;
    s_cache_map_h = 0u;
    s_camera_cache_map_w = 0u;
    s_camera_cache_map_h = 0u;
    s_camera_cache_src_w = 0u;
    s_camera_cache_src_h = 0u;
    s_camera_cache_seq = 0u;
    s_camera_cache_valid = 0u;
    s_camera_cache_full_dirty = 1u;
    s_norm_lut_valid = 0u;
    App_Display_SetMode((App_Display_Mode_t)APP_DISPLAY_DEFAULT_MODE);
    g_display_init_stage = 2u;
    lcd_init();
    g_display_init_stage = 3u;
    draw_w = lcddev.width;
    draw_h = lcddev.height;
    if ((draw_w == 0u) || (draw_h == 0u))
    {
        g_display_init_error = 1u;
        g_display_init_stage = 0xE001u;
        return;
    }
    if ((draw_w <= APP_DISPLAY_UI_PANEL_W) || (draw_h < APP_DISPLAY_HEAT_VIEW_H))
    {
        g_display_init_error = 2u;
        g_display_init_stage = 0xE002u;
        return;
    }
    camera_w = (uint16_t)(draw_w - APP_DISPLAY_UI_PANEL_W);
    camera_h = draw_h;
    heat_w = (camera_w < APP_DISPLAY_HEAT_VIEW_W) ? camera_w : (uint16_t)APP_DISPLAY_HEAT_VIEW_W;
    heat_h = (camera_h < APP_DISPLAY_HEAT_VIEW_H) ? camera_h : (uint16_t)APP_DISPLAY_HEAT_VIEW_H;
    if ((heat_w < 32u) || (heat_h < 32u))
    {
        g_display_init_error = 3u;
        g_display_init_stage = 0xE003u;
        return;
    }
    s_camera_x0 = 0u;
    s_camera_y0 = 0u;
    s_camera_x1 = (uint16_t)(camera_w - 1u);
    s_camera_y1 = (uint16_t)(camera_h - 1u);
    s_map_x0 = (uint16_t)((camera_w - heat_w) / 2u);
    s_map_y0 = (uint16_t)((camera_h - heat_h) / 2u);
    s_map_x1 = (uint16_t)(s_map_x0 + heat_w - 1u);
    s_map_y1 = (uint16_t)(s_map_y0 + heat_h - 1u);
    s_text_x = camera_w;
    s_ui_x1 = (uint16_t)(draw_w - 1u);
    s_refresh_render_map_cache((uint16_t)(s_map_x1 - s_map_x0 + 1u),
                               (uint16_t)(s_map_y1 - s_map_y0 + 1u));
    g_display_init_stage = 4u;
    s_fb_addr_a = ltdc_get_frontbuf_addr();
    s_fb_addr_b = ltdc_get_backbuf_addr();
    DMA2D_Accel_LoadClutFromRgb565(s_heat_lut, APP_DISPLAY_HEAT_LUT_SIZE);
    s_fill_rect_async(0u, 0u, (uint16_t)(draw_w - 1u), (uint16_t)(draw_h - 1u), BLACK);
    s_draw_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, WHITE);
    if (s_text_x > 0u)
    {
        s_draw_vline_async((uint16_t)(s_text_x - 1u), 0u, (uint16_t)(draw_h - 1u), WHITE);
    }
    s_commit_frame();
    s_ready = 1u;
    g_display_init_stage = 0x8000u;
}

/* 对算法功率值做有效性过滤�? * 只保留“有限且大于 0”的值，其余全部视为 0�?*/
static float s_power_mag(float v)
{
    /* 算法输出中若出现 NaN、Inf 或负值，这里统一视为无效能量�?     * 显示模块只接受“有限且非负”的功率值�?*/
    if (!isfinite(v) || (v <= 0.0f))
    {
        return 0.0f;
    }
    return v;
}

/* 按编译期开关把算法角度坐标映射到显示坐标系�?*/
static void s_apply_output_remap(float *x_angle, float *y_angle)
{
    /* 显示坐标系与算法坐标系可能不完全一致�?     * 这里按编译期开关执行交换轴/翻转轴，保证画面方向符合屏幕安装方式�?*/
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

/* 执行�?`s_apply_output_remap` 相反的坐标映射�?*/
static void s_inverse_output_remap(float *x_angle, float *y_angle)
{
    /* �?`s_apply_output_remap` 相反，用于从显示坐标回到算法原始坐标系�?     * 典型用途是：当我们按“屏幕上的某个采样点”反查粗网格值时，需要先做逆映射�?*/
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

/* 把粗搜索网格重采样到显示中间�?`s_field_a`�? * 每个场点都通过角度反查回粗网格，并做双线性插值�?*/
static void s_resample_coarse_to_field(const SRP_VisFrame_t *vis_frame)
{
    /* 粗搜索网格点数较少，不适合直接上屏，否则会呈现明显棋盘�?块状边界�?     * 这里的做法是�?     * 1. 遍历显示中间场的每个采样�?     * 2. 把该点对应的显示角度反推回算法角度坐�?     * 3. 在粗网格上做双线性插�?     * 4. 得到一个稠密、连续的浮点�?`s_field_a`
     *
     * 这一步是“算法网格”到“显示网格”的第一座桥�?*/
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
        /* 当前行对应的显示垂直角度�?*/
        float phi_disp = (float)COARSE_ANGLE_MAX_DEG
                       - ((float)y * span / (float)(APP_DISPLAY_FIELD_H - 1u));
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            /* 当前列对应的显示水平角度�?*/
            float theta_disp = (float)COARSE_ANGLE_MIN_DEG
                             + ((float)x * span / (float)(APP_DISPLAY_FIELD_W - 1u));
            /* 后续会把显示角度逆变换回算法角度坐标系�?*/
            float theta_raw = theta_disp;
            float phi_raw = phi_disp;
            /* `tx/py` 是粗网格中的浮点坐标�?*/
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

            /* 连续角度 -> 粗网格浮点索引�?*/
            tx = (theta_raw - (float)COARSE_ANGLE_MIN_DEG) * inv_span * (float)(COARSE_GRID_SIZE - 1u);
            py = (phi_raw - (float)COARSE_ANGLE_MIN_DEG) * inv_span * (float)(COARSE_GRID_SIZE - 1u);
            tx = s_clamp_f32(tx, 0.0f, (float)(COARSE_GRID_SIZE - 1u));
            py = s_clamp_f32(py, 0.0f, (float)(COARSE_GRID_SIZE - 1u));

            /* 找到双线性插值的四个邻点及其权重�?*/
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
            /* 先横向插值，再纵向插值�?*/
            vt0 = v00 * (1.0f - wt) + v01 * wt;
            vt1 = v10 * (1.0f - wt) + v11 * wt;
            s_field_a[y * APP_DISPLAY_FIELD_W + x] = vt0 * (1.0f - wp) + vt1 * wp;
        }
    }
}

/* 将细搜索结果融合回显示中间场�? * 只融合强度足够高的细点，并按照二维核向周围扩散�?*/
static void s_apply_fine_fusion(const SRP_VisFrame_t *vis_frame)
{
#if (APP_DISPLAY_FINE_FUSION_ENABLE != 0u)
    /* 细搜索融合的目的不是重建整张图，而是对局部强峰附近补充细节�?     * 逻辑上只处理足够强的细网格点，并用一个二维核把它们扩散到显示场，
     * 这样既能强化峰值附近结构，又不至于把弱噪声全面抬高�?*/
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
            /* 过滤掉过弱的细搜索点�?*/
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

        /* 以细搜索点为中心，把能量扩散到周围显示场像素�?*/
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
                /* 当前核权重对应的能量增量�?*/
                s_field_a[fidx] += s_cfg.fine_gain * mag * w;
            }
        }
    }
#else
    (void)vis_frame;
#endif
}

/* 对显示中间场执行一次可分离平滑�? * 横向结果先写�?`s_field_b`，再纵向写回 `s_field_a`�?*/
static void s_apply_blur_once(void)
{
#if (APP_DISPLAY_SMOOTH_ENABLE != 0u)
    /* 一次完整平滑由两步组成�?     * - 先横向卷积，把结果写�?`s_field_b`
     * - 再纵向卷积，把结果写�?`s_field_a`
     *
     * 这是典型的可分离卷积写法，相比直接二维卷积，运算量更低�?*/
    uint32_t y;
    uint32_t x;
    int32_t k;

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            float acc = 0.0f;
            /* 横向卷积，边界位置采用夹取策略�?*/
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
            /* 纵向卷积，边界同样采用夹取策略�?*/
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

/* 基于当前可视化帧，准备一张完整的浮点显示场�? * 返回值为场中的最大峰值，用于后续动态归一化�?*/
static float s_prepare_field(const SRP_VisFrame_t *vis_frame)
{
    /* 基于最新的 SRP 快照，构建一张“可直接进入显示链路”的浮点场�?     * 这是算法输出到显示逻辑之间的核心桥接步骤，通常包含�?     * - 粗网格重采样
     * - 可选的细网格融�?     * - 若干次平�?     * - 最终峰值提�?     *
     * 返回�?`peak` 会作为后续动态归一化的参考输入�?*/
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

/* 完整精度归一化函数�? * 输入是相对参考峰值的比例，输出是 0..255 灰度�?*/
static uint8_t s_compute_norm_full(float ratio)
{
    /* 完整归一化路径：
     * - 先把线性能量比转换�?dB 空间
     * - 再按 `db_floor` 截断
     * - 再映射回 0..1
     * - 最后套 gamma 调整观感，并量化�?0..255 */
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

/* 刷新快速归一�?LUT�? * 只有�?`gamma` �?`db_floor` 变化时才重新构建�?*/
static void s_refresh_norm_lut(void)
{
    /* 快速归一化模式下，`ratio -> 灰度值` 的关系只取决�?gamma �?db_floor�?     * 只要这两个参数没变，LUT 就可以持续复用�?*/
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

/* 快速归一化查表函数�?*/
static uint8_t s_norm_fast_lookup(float ratio)
{
    /* 快速模式下不再逐像素计�?log10f/powf，而是直接查表�?*/
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

/* 刷新 LCD 像素到显示场坐标的映射缓存�? * 这样渲染阶段可以避免每帧重复进行坐标换算�?*/
void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h)
{
    /* 预计算“LCD 像素坐标 -> 显示场坐标”的映射关系�?     * 最近邻和双线性两种路径都会用到这些缓存�?     *
     * 缓存后，逐帧内层循环就不需要反复做�?     * - 浮点除法
     * - 边界钳位
     * - 双线性权重换�?     *
     * 这对整屏热力图逐帧绘制的性能帮助很直接�?*/
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

        /* `fx` 表示 LCD 当前列对应到显示场中的浮点列坐标�?*/
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

        /* 同时缓存最近邻索引与双线性插值所需的左右邻点、权重�?*/
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

        /* `fy` 表示 LCD 当前行对应到显示场中的浮点行坐标�?*/
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

        /* 同时缓存最近邻索引与双线性插值所需的上下邻点、权重�?*/
        s_row_y0_cache[y] = y0;
        s_row_y1_cache[y] = y1;
        s_row_wy256_cache[y] = wy;
        s_row_near_cache[y] = (wy >= 128u) ? y1 : y0;
    }

    s_cache_map_w = map_w;
    s_cache_map_h = map_h;
}

/* 计算归一化后�?8bit 显示场�? * 内部会估计噪声底、扣除背景，并根据当前模式选择快�?完整路径�?*/
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

static void s_update_camera_cache_from_frame(const App_CameraFrame_t *camera_frame)
{
    uint16_t camera_w = (uint16_t)(s_camera_x1 - s_camera_x0 + 1u);
    uint16_t camera_h = (uint16_t)(s_camera_y1 - s_camera_y0 + 1u);
    uint16_t src_stride;
    uint16_t y;
    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (camera_w == 0u) ||
        (camera_h == 0u) ||
        (camera_w > APP_DISPLAY_CAMERA_VIEW_W) ||
        (camera_h > APP_DISPLAY_CAMERA_VIEW_H) ||
        (camera_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (camera_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }
    if ((s_camera_cache_valid != 0u) &&
        (s_camera_cache_seq == camera_frame->seq) &&
        (s_camera_cache_map_w == camera_w) &&
        (s_camera_cache_map_h == camera_h) &&
        (s_camera_cache_src_w == camera_frame->width) &&
        (s_camera_cache_src_h == camera_frame->height))
    {
        return;
    }
    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    s_refresh_camera_scale_cache(camera_w, camera_h, camera_frame->width, camera_frame->height);
    for (y = 0u; y < camera_h; y++)
    {
        const uint16_t *src_cam = &camera_frame->pixels[(uint32_t)s_camera_row_near_cache[y] * (uint32_t)src_stride];
        uint16_t *dst = &s_camera_cache_pixels[(uint32_t)y * (uint32_t)camera_w];
        uint16_t x;
        for (x = 0u; x < camera_w; x++)
        {
            dst[x] = src_cam[s_camera_col_near_cache[x]];
        }
    }
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;
}
static uint8_t s_blit_camera_cache_region(uint16_t dst_x0,
                                          uint16_t dst_y0,
                                          uint16_t width,
                                          uint16_t height,
                                          uint16_t src_x0,
                                          uint16_t src_y0)
{
    uint16_t camera_w = (uint16_t)(s_camera_x1 - s_camera_x0 + 1u);
    uint16_t camera_h = (uint16_t)(s_camera_y1 - s_camera_y0 + 1u);
    uint16_t dst_x1;
    uint16_t dst_y1;
    const uint16_t *src;
    uint16_t row;
    if ((s_camera_cache_valid == 0u) ||
        (width == 0u) ||
        (height == 0u) ||
        (camera_w == 0u) ||
        (camera_h == 0u))
    {
        return 0u;
    }
    if (((uint32_t)src_x0 + (uint32_t)width > (uint32_t)camera_w) ||
        ((uint32_t)src_y0 + (uint32_t)height > (uint32_t)camera_h))
    {
        return 0u;
    }
    dst_x1 = (uint16_t)(dst_x0 + width - 1u);
    dst_y1 = (uint16_t)(dst_y0 + height - 1u);
    src = &s_camera_cache_pixels[(uint32_t)src_y0 * (uint32_t)camera_w + (uint32_t)src_x0];
    if (ltdc_copy_async(dst_x0, dst_y0, dst_x1, dst_y1, src, camera_w) == 0u)
    {
        return 1u;
    }
    if (s_flush_temp_draw() != 0u)
    {
        return 1u;
    }
    DMA2D_Accel_Reset();
    for (row = 0u; row < height; row++)
    {
        lcd_color_fill(dst_x0,
                       (uint16_t)(dst_y0 + row),
                       dst_x1,
                       (uint16_t)(dst_y0 + row),
                       (uint16_t *)&src[(uint32_t)row * (uint32_t)camera_w]);
    }
    return 1u;
}
static void s_render_camera_rows(const App_CameraFrame_t *camera_frame)
{
    uint16_t camera_w = (uint16_t)(s_camera_x1 - s_camera_x0 + 1u);
    uint16_t camera_h = (uint16_t)(s_camera_y1 - s_camera_y0 + 1u);
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint8_t full_refresh;
    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (camera_w == 0u) ||
        (camera_h == 0u) ||
        (map_w == 0u) ||
        (map_h == 0u) ||
        (camera_w > APP_DISPLAY_CAMERA_VIEW_W) ||
        (camera_h > APP_DISPLAY_CAMERA_VIEW_H))
    {
        return;
    }
    full_refresh = (uint8_t)((s_camera_cache_valid == 0u) ||
                             (s_camera_cache_full_dirty != 0u) ||
                             (s_camera_cache_seq != camera_frame->seq));
    s_update_camera_cache_from_frame(camera_frame);
    if (s_camera_cache_valid == 0u)
    {
        return;
    }
    if (full_refresh != 0u)
    {
        (void)s_blit_camera_cache_region(s_camera_x0, s_camera_y0, camera_w, camera_h, 0u, 0u);
        s_camera_cache_full_dirty = 0u;
        return;
    }
    (void)s_blit_camera_cache_region(s_map_x0,
                                     s_map_y0,
                                     map_w,
                                     map_h,
                                     (uint16_t)(s_map_x0 - s_camera_x0),
                                     (uint16_t)(s_map_y0 - s_camera_y0));
}

static void s_render_field_alpha_rows(uint16_t color565)
{
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;

    if ((map_w == 0u) ||
        (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    s_refresh_render_map_cache(map_w, map_h);

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
                    dst[x] = (uint8_t)((q * (uint32_t)APP_CAMERA_OVERLAY_ALPHA_MAX + 127u) / 255u);
                }
            }
            else
            {
                uint16_t y_idx = s_row_near_cache[y];
                const uint8_t *src = &s_field_norm_u8[(uint32_t)y_idx * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = (uint8_t)(((uint32_t)src[s_col_near_cache[x]] *
                                        (uint32_t)APP_CAMERA_OVERLAY_ALPHA_MAX + 127u) / 255u);
                }
            }
        }

        if (ltdc_a8_blend_async(s_map_x0,
                                (uint16_t)(s_map_y0 + y_blk),
                                s_map_x1,
                                (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                s_blit_l8_buf,
                                map_w,
                                color565) == 0u)
        {
            if (ltdc_l8_fill_async(s_map_x0,
                                   (uint16_t)(s_map_y0 + y_blk),
                                   s_map_x1,
                                   (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                   s_blit_l8_buf,
                                   map_w) == 0u)
            {
                for (row = 0u; row < rows; row++)
                {
                    uint8_t *src = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];
                    uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)map_w];
                    uint16_t x;

                    for (x = 0u; x < map_w; x++)
                    {
                        dst[x] = s_heat_lut[src[x]];
                    }
                }
                s_submit_rgb565_block(s_map_x0,
                                      (uint16_t)(s_map_y0 + y_blk),
                                      s_map_x1,
                                      (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                      s_blit_buf);
            }
        }
    }
}

static void s_update_norm_field(float field_peak, uint32_t frame_seq)
{
    /* 把浮点能量场转换�?8bit 强度场�?     * 这是影响观感最关键的步骤之一，因�?SRP 功率动态范围很大，而且随帧波动�?     *
     * 当前策略分为四层�?     * 1. �?`s_peak_ema` 作为平滑参考峰值，减少亮度剧烈抖动
     * 2. 根据固定比例和背景平均值共同估算噪声底
     * 3. 把超过噪声底的有效能量映射到 0..255
     * 4. 根据模式选择“查表快速路径”或“全精度公式路径�?     *
     * 如果当前帧没有有效峰值，还会根据配置选择输出测试图案或纯黑图�?*/
    uint32_t i;
    float ref;
    float floor_linear;
    float bg_sum = 0.0f;
    uint32_t bg_cnt = 0u;

    if ((field_peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK) && (APP_DISPLAY_IDLE_TEST_PATTERN != 0u))
    {
        for (i = 0u; i < APP_DISPLAY_FIELD_H; i++)
        {
            uint32_t x;
            for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
            {
                uint32_t phase = (frame_seq >> 2) & 1u;
                s_field_norm_u8[i * APP_DISPLAY_FIELD_W + x] = ((((x >> 3) + (i >> 3) + phase) & 1u) != 0u) ? 48u : 12u;
            }
        }
        s_last_noise_floor = 0.0f;
        return;
    }

    if (field_peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK)
    {
        memset(s_field_norm_u8, 0, sizeof(s_field_norm_u8));
        s_last_noise_floor = 0.0f;
        return;
    }

    ref = (s_peak_ema < APP_DISPLAY_DYNAMIC_MIN_PEAK) ? APP_DISPLAY_DYNAMIC_MIN_PEAK : s_peak_ema;
    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        /* 峰值一半以下的区域被作为背景候选，用来估计噪声底�?*/
        if (s_field_a[i] < (field_peak * 0.5f))
        {
            bg_sum += s_field_a[i];
            bg_cnt++;
        }
    }
    /* 先按固定比例给出基础噪声底�?*/
    floor_linear = field_peak * s_cfg.noise_gate_ratio;
    if (bg_cnt > 0u)
    {
        /* 再用背景平均值估计一个自适应噪声底，取两者更大的那个�?*/
        float bg_floor = (bg_sum / (float)bg_cnt) * s_cfg.noise_adapt_gain;
        if (bg_floor > floor_linear)
        {
            floor_linear = bg_floor;
        }
    }
    floor_linear = s_clamp_f32(floor_linear, 0.0f, field_peak);
    s_last_noise_floor = floor_linear;

    if (s_cfg.norm_mode == APP_DISPLAY_NORM_FAST)
    {
        s_refresh_norm_lut();
        for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
        {
            float v = s_field_a[i] - floor_linear;
            if (v <= 0.0f)
            {
                /* 低于噪声底的像素直接压成 0�?*/
                s_field_norm_u8[i] = 0u;
                continue;
            }
            /* `v / ref` 是相对参考峰值的能量比例�?*/
            s_field_norm_u8[i] = s_norm_fast_lookup(v / ref);
        }
        return;
    }

    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        float v = s_field_a[i] - floor_linear;
        if (v <= 0.0f)
        {
            s_field_norm_u8[i] = 0u;
            continue;
        }
        /* 完整模式直接逐像素走公式路径�?*/
        s_field_norm_u8[i] = s_compute_norm_full(v / ref);
    }
}

/* 把水平角度转换为热力图区域内�?X 坐标�?*/
static uint16_t s_angle_to_x(float angle)
{
    /* 把水平角度线性映射到热力图矩形内�?X 像素坐标�?*/
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

/* 把垂直角度转换为热力图区域内�?Y 坐标�?*/
static uint16_t s_angle_to_y(float angle)
{
    /* 把垂直角度映射到热力图矩形内�?Y 像素坐标�?     * 由于屏幕 Y 轴向下增大，因此这里使用“最大角在上方”的映射方式�?*/
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

/* �?8bit 热力图按行块方式绘制到后台缓冲�?*/
static void s_render_field_rows(void)
{
    /* 将归一化后的热力图分块写入后台缓冲�?     * 采用“按若干行一块”的写法，原因有三：
     * - 控制临时缓冲大小，不必为整张图准备全尺寸中间�?     * - 更适合 DMA2D/LTDC 这类块传输接�?     * - 软件回退时也能复用同一套流�?     *
     * 块内处理顺序为：
     * 1. 根据插值模式，�?`s_field_norm_u8` 放大到当�?LCD 行块
     * 2. 若支�?L8 + CLUT 加速，则直接提�?8bit 数据
     * 3. 否则手动�?`s_heat_lut` 转成 RGB565 再写�?*/
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;

    if ((map_w == 0u) ||
        (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }
    s_refresh_render_map_cache(map_w, map_h);

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
                /* 双线性路径：使用预缓存坐标和权重进行 2x2 插值�?*/
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
                    /* 结果理论上落�?0..255，仍保守做一次裁剪�?*/
                    dst[x] = (q > 255u) ? 255u : (uint8_t)q;
                }
            }
            else
            {
                /* 最近邻路径：直接按缓存索引取值�?*/
                uint16_t y_idx = s_row_near_cache[y];
                const uint8_t *src = &s_field_norm_u8[(uint32_t)y_idx * APP_DISPLAY_FIELD_W];
                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = src[s_col_near_cache[x]];
                }
            }
        }

        if (ltdc_l8_fill_async(s_map_x0,
                               (uint16_t)(s_map_y0 + y_blk),
                               s_map_x1,
                               (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                               s_blit_l8_buf,
                               map_w) == 0u)
        {
            /* 若不能直接按 8bit 调色板路径提交，则手动转 RGB565�?*/
            for (row = 0u; row < rows; row++)
            {
                uint8_t *src = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];
                uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)map_w];
                uint16_t x;

                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = s_heat_lut[src[x]];
                }
            }

            s_submit_rgb565_block(s_map_x0,
                                  (uint16_t)(s_map_y0 + y_blk),
                                  s_map_x1,
                                  (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                  s_blit_buf);
            }
        }
    }

/* 绘制侧边文本诊断区域�?*/
static void s_draw_overlay(const Sound_Pos_t *pos,
                           uint32_t frame_seq,
                           uint32_t peak_idx,
                           float peak_theta,
                           float peak_phi,
                           float field_peak,
                           uint8_t sai_dma_active)
{
    char line[80];
    static char title0[] = "Acoustic";
    static char title1[] = "Imaging";
    uint16_t panel_x0 = s_text_x;
    uint16_t panel_x1 = s_ui_x1;
    uint16_t panel_w;
    if ((panel_x0 >= lcddev.width) || (panel_x1 < panel_x0))
    {
        return;
    }
    panel_w = (uint16_t)(panel_x1 - panel_x0 + 1u);
    lcd_fill(panel_x0, 0u, panel_x1, (uint16_t)(lcddev.height - 1u), BLACK);
    if (panel_x0 > 0u)
    {
        lcd_fill((uint16_t)(panel_x0 - 1u), 0u, (uint16_t)(panel_x0 - 1u), (uint16_t)(lcddev.height - 1u), WHITE);
    }
    lcd_show_string((uint16_t)(panel_x0 + 8u), 8u, (uint16_t)(panel_w - 16u), 16u, 16u, title0, CYAN);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 26u, (uint16_t)(panel_w - 16u), 16u, 16u, title1, CYAN);
    (void)snprintf(line, sizeof(line), "X:%5.1f", (double)pos->x_angle);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 58u, (uint16_t)(panel_w - 16u), 16u, 16u, line, YELLOW);
    (void)snprintf(line, sizeof(line), "Y:%5.1f", (double)pos->y_angle);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 76u, (uint16_t)(panel_w - 16u), 16u, 16u, line, YELLOW);
    (void)snprintf(line, sizeof(line), "E:%0.3f", isfinite(pos->energy) ? (double)pos->energy : 0.0);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 94u, (uint16_t)(panel_w - 16u), 16u, 16u, line, GREEN);
    (void)snprintf(line, sizeof(line), "F:%lu P:%c", (unsigned long)frame_seq, (peak_idx < COARSE_TOTAL) ? 'C' : 'F');
    lcd_show_string((uint16_t)(panel_x0 + 8u), 112u, (uint16_t)(panel_w - 16u), 16u, 16u, line, CYAN);
    (void)snprintf(line, sizeof(line), "Tx:%0.1f", (double)peak_theta);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 130u, (uint16_t)(panel_w - 16u), 16u, 16u, line, WHITE);
    (void)snprintf(line, sizeof(line), "Ty:%0.1f", (double)peak_phi);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 148u, (uint16_t)(panel_w - 16u), 16u, 16u, line, WHITE);
    (void)snprintf(line,
                   sizeof(line),
                   "M:%s/%s/%s",
                   App_Display_ModeName(s_mode),
                   App_Display_InterpName((App_Display_Interp_t)s_cfg.interp_mode),
                   App_Display_NormName((App_Display_Norm_t)s_cfg.norm_mode));
    lcd_show_string((uint16_t)(panel_x0 + 8u), 178u, (uint16_t)(panel_w - 16u), 16u, 16u, line, CYAN);
    (void)snprintf(line, sizeof(line), "N:%0.1e", (double)s_last_noise_floor);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 196u, (uint16_t)(panel_w - 16u), 16u, 16u, line, YELLOW);
    (void)snprintf(line, sizeof(line), "Pk:%0.1e", (double)field_peak);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 214u, (uint16_t)(panel_w - 16u), 16u, 16u, line, LIGHTGREEN);
#if (APP_DISPLAY_DIAG_OVERLAY != 0u)
    (void)snprintf(line, sizeof(line), "D2D %lu/%lu", (unsigned long)g_ltdc_dma2d_timeout_count, (unsigned long)g_ltdc_dma2d_sw_fallback_count);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 244u, (uint16_t)(panel_w - 16u), 16u, 16u, line, GREEN);
    (void)snprintf(line, sizeof(line), "Sw %lu/%lu", (unsigned long)g_ltdc_swap_count, (unsigned long)g_ltdc_swap_error_count);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 262u, (uint16_t)(panel_w - 16u), 16u, 16u, line, LIGHTBLUE);
    (void)snprintf(line, sizeof(line), "SAI:%s", (sai_dma_active != 0u) ? "ON" : "OFF");
    lcd_show_string((uint16_t)(panel_x0 + 8u), 280u, (uint16_t)(panel_w - 16u), 16u, 16u, line, LIGHTBLUE);
    (void)snprintf(line, sizeof(line), "UART %lu/%lu", (unsigned long)g_ui_cli_rx_ok_count, (unsigned long)g_ui_cli_rx_err_count);
    lcd_show_string((uint16_t)(panel_x0 + 8u), 298u, (uint16_t)(panel_w - 16u), 16u, 16u, line, RED);
#endif
}

/* 渲染一整帧声学成像画面�? * 这是模块对外最核心的逐帧入口�?*/
void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        const App_CameraFrame_t *camera_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active)
{
    float field_peak;
    uint32_t t_perf;
    uint32_t peak_idx = 0u;
    float peak_theta = 0.0f;
    float peak_phi = 0.0f;
    uint8_t refresh_text;
    uint8_t back_slot;
    uint8_t camera_valid;
    if ((s_ready == 0u) || (pos == NULL) || (vis_frame == NULL))
    {
        return;
    }
    if (ltdc_wait_for_swap_complete(s_display_frame_budget_ms()) != 0u)
    {
        return;
    }
    t_perf = App_Perf_BeginCycles();
    field_peak = s_prepare_field(vis_frame);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_PREPARE, t_perf);
    if (field_peak > s_peak_ema)
    {
        s_peak_ema += s_cfg.ema_attack * (field_peak - s_peak_ema);
    }
    else
    {
        s_peak_ema += s_cfg.ema_decay * (field_peak - s_peak_ema);
    }
    if (s_peak_ema < APP_DISPLAY_EMA_MIN_PEAK)
    {
        s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
    }
    t_perf = App_Perf_BeginCycles();
    s_update_norm_field(field_peak, frame_seq);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_NORM, t_perf);
    camera_valid = (uint8_t)((camera_frame != NULL) &&
                             (camera_frame->valid != 0u) &&
                             (camera_frame->pixels != NULL) &&
                             (camera_frame->width != 0u) &&
                             (camera_frame->height != 0u));
    t_perf = App_Perf_BeginCycles();
    if (camera_valid != 0u)
    {
        s_render_camera_rows(camera_frame);
        if (s_camera_cache_valid != 0u)
        {
            s_render_field_alpha_rows(APP_CAMERA_OVERLAY_COLOR_565);
        }
        else
        {
            camera_valid = 0u;
        }
    }
    if (camera_valid == 0u)
    {
        s_camera_cache_valid = 0u;
        s_camera_cache_full_dirty = 1u;
        s_fill_rect_async(s_camera_x0, s_camera_y0, s_camera_x1, s_camera_y1, BLACK);
        s_render_field_rows();
    }
    s_draw_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, WHITE);
    if (s_text_x > 0u)
    {
        s_draw_vline_async((uint16_t)(s_text_x - 1u), 0u, (uint16_t)(lcddev.height - 1u), WHITE);
    }
    if (vis_frame->peak_idx < SRP_GRID_TOTAL)
    {
        peak_idx = vis_frame->peak_idx;
        peak_theta = vis_frame->theta_deg[peak_idx];
        peak_phi = vis_frame->phi_deg[peak_idx];
    }
    s_apply_output_remap(&peak_theta, &peak_phi);
    {
        float ax = s_clamp_f32(pos->x_angle, (float)COARSE_ANGLE_MIN_DEG, (float)COARSE_ANGLE_MAX_DEG);
        float ay = s_clamp_f32(pos->y_angle, (float)COARSE_ANGLE_MIN_DEG, (float)COARSE_ANGLE_MAX_DEG);
        uint16_t cx = s_angle_to_x(ax);
        uint16_t cy = s_angle_to_y(ay);
        uint16_t half = APP_DISPLAY_CROSSHAIR_HALF_PX;
        uint16_t xl = (cx > (uint16_t)(s_map_x0 + half)) ? (uint16_t)(cx - half) : s_map_x0;
        uint16_t xr = ((uint32_t)cx + half < s_map_x1) ? (uint16_t)(cx + half) : s_map_x1;
        uint16_t yt = (cy > (uint16_t)(s_map_y0 + half)) ? (uint16_t)(cy - half) : s_map_y0;
        uint16_t yb = ((uint32_t)cy + half < s_map_y1) ? (uint16_t)(cy + half) : s_map_y1;
        s_draw_hline_async(xl, cy, xr, WHITE);
        s_draw_vline_async(cx, yt, yb, WHITE);
    }
    {
        uint16_t px = s_angle_to_x(peak_theta);
        uint16_t py = s_angle_to_y(peak_phi);
        uint16_t r = APP_DISPLAY_PEAK_MARKER_RADIUS_PX;
        uint16_t x0 = (px > (uint16_t)(s_map_x0 + r)) ? (uint16_t)(px - r) : s_map_x0;
        uint16_t y0 = (py > (uint16_t)(s_map_y0 + r)) ? (uint16_t)(py - r) : s_map_y0;
        uint16_t x1 = ((uint32_t)px + r < s_map_x1) ? (uint16_t)(px + r) : s_map_x1;
        uint16_t y1 = ((uint32_t)py + r < s_map_y1) ? (uint16_t)(py + r) : s_map_y1;
        s_draw_rect_async(x0, y0, x1, y1, WHITE);
    }
    App_Perf_EndCycles(APP_PERF_SEC_DISP_RENDER, t_perf);
    back_slot = s_backbuf_slot();
    refresh_text = 0u;
    if (frame_seq <= 1u)
    {
        refresh_text = 1u;
    }
    else if (back_slot > 1u)
    {
        refresh_text = 1u;
    }
    else if ((uint32_t)(frame_seq - s_last_text_refresh_frame[back_slot]) >= (uint32_t)s_cfg.text_refresh_div)
    {
        refresh_text = 1u;
    }
    if (refresh_text != 0u)
    {
        if (back_slot <= 1u)
        {
            s_last_text_refresh_frame[back_slot] = frame_seq;
        }
        t_perf = App_Perf_BeginCycles();
        s_draw_overlay(pos, frame_seq, peak_idx, peak_theta, peak_phi, field_peak, sai_dma_active);
        App_Perf_EndCycles(APP_PERF_SEC_DISP_OVERLAY, t_perf);
    }
    t_perf = App_Perf_BeginCycles();
    s_commit_frame();
    App_Perf_EndCycles(APP_PERF_SEC_DISP_COMMIT, t_perf);
}
