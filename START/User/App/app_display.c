#include "app_display.h"

#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "LCD/dma2d_accel.h"
#include "ai_config.h"
#include "mpu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef COARSE_ANGLE_MIN_DEG
#define COARSE_ANGLE_MIN_DEG (-60.0f)
#endif

#ifndef COARSE_ANGLE_MAX_DEG
#define COARSE_ANGLE_MAX_DEG (60.0f)
#endif

#define APP_DISPLAY_FIELD_PIXELS      (APP_DISPLAY_FIELD_W * APP_DISPLAY_FIELD_H)
#define APP_DISPLAY_BLUR_KERNEL_LEN   (2u * APP_DISPLAY_SMOOTH_RADIUS + 1u)
#define APP_DISPLAY_FINE_KERNEL_LEN   (2u * APP_DISPLAY_FINE_KERNEL_RADIUS + 1u)
#define APP_DISPLAY_DYNAMIC_MIN_PEAK  (1.0e-9f)
#define APP_DISPLAY_HEAT_LUT_SIZE     256u
#define APP_DISPLAY_DMA2D_TIMEOUT     0x1FFFFFu

static uint8_t s_ready = 0u;
volatile uint32_t g_display_init_stage = 0u;
volatile uint32_t g_display_init_error = 0u;

static uint16_t s_map_x0 = 0u;
static uint16_t s_map_y0 = 0u;
static uint16_t s_map_x1 = 0u;
static uint16_t s_map_y1 = 0u;
static uint16_t s_text_x = 0u;

static float s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
static float s_last_noise_floor = 0.0f;
static uint8_t s_kernel_ready = 0u;
static uint32_t s_last_text_refresh_frame[2] = {0u, 0u};
static uint32_t s_fb_addr_a = 0u;
static uint32_t s_fb_addr_b = 0u;

__SECTION_AXI_SRAM static float s_field_a[APP_DISPLAY_FIELD_PIXELS];
__SECTION_AXI_SRAM static float s_field_b[APP_DISPLAY_FIELD_PIXELS];
__SECTION_AXI_SRAM static uint8_t s_field_norm_u8[APP_DISPLAY_FIELD_PIXELS];
__SECTION_D2_SRAM static uint16_t s_blit_buf[APP_DISPLAY_MAX_LINE_PIXELS * APP_DISPLAY_BLIT_ROWS_MAX];
__SECTION_D2_SRAM static uint8_t s_blit_l8_buf[APP_DISPLAY_MAX_LINE_PIXELS * APP_DISPLAY_BLIT_ROWS_MAX];

static float s_blur_kernel[APP_DISPLAY_BLUR_KERNEL_LEN];
static float s_fine_kernel[APP_DISPLAY_FINE_KERNEL_LEN * APP_DISPLAY_FINE_KERNEL_LEN];
static uint16_t s_heat_lut[APP_DISPLAY_HEAT_LUT_SIZE];

static App_Display_Mode_t s_mode = APP_DISPLAY_MODE_BALANCED;
static App_Display_RuntimeCfg_t s_cfg = {
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
    APP_DISPLAY_TEXT_REFRESH_DIV,
    APP_DISPLAY_BLIT_ROWS_MAX
};

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

static uint16_t s_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

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

static void s_build_heat_lut(void)
{
    uint32_t i;

    for (i = 0u; i < APP_DISPLAY_HEAT_LUT_SIZE; i++)
    {
        s_heat_lut[i] = s_heat_color((float)i / 255.0f);
    }
}

static void s_build_kernels(void)
{
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

const char *App_Display_InterpName(App_Display_Interp_t interp)
{
    if (interp == APP_DISPLAY_INTERP_BILINEAR)
    {
        return "BIL";
    }
    return "NEAR";
}

static void s_load_mode_defaults(App_Display_Mode_t mode, App_Display_RuntimeCfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:
            cfg->ema_attack = 0.70f;
            cfg->ema_decay = 0.15f;
            cfg->db_floor = -34.0f;
            cfg->fine_gain = 0.25f;
            cfg->gamma = 1.00f;
            cfg->noise_gate_ratio = 0.12f;
            cfg->noise_adapt_gain = 1.50f;
            cfg->smooth_passes = 0u;
            cfg->fine_fusion_enable = 0u;
            cfg->draw_coarse_grid = 0u;
            cfg->interp_mode = APP_DISPLAY_INTERP_NEAREST;
            cfg->text_refresh_div = 4u;
            cfg->blit_rows = APP_DISPLAY_BLIT_ROWS_MAX;
            break;

        case APP_DISPLAY_MODE_CLEAN:
            cfg->ema_attack = 0.58f;
            cfg->ema_decay = 0.06f;
            cfg->db_floor = -52.0f;
            cfg->fine_gain = 0.50f;
            cfg->gamma = 1.35f;
            cfg->noise_gate_ratio = 0.10f;
            cfg->noise_adapt_gain = 2.80f;
            cfg->smooth_passes = 2u;
            cfg->fine_fusion_enable = 1u;
            cfg->draw_coarse_grid = 0u;
            cfg->interp_mode = APP_DISPLAY_INTERP_BILINEAR;
            cfg->text_refresh_div = 2u;
            cfg->blit_rows = 6u;
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
            cfg->text_refresh_div = APP_DISPLAY_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_BLIT_ROWS_MAX;
            break;
    }
}

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
    s_cfg.smooth_passes = s_clamp_u8(cfg->smooth_passes, 0u, 3u);
    s_cfg.fine_fusion_enable = (cfg->fine_fusion_enable != 0u) ? 1u : 0u;
    s_cfg.draw_coarse_grid = (cfg->draw_coarse_grid != 0u) ? 1u : 0u;
    s_cfg.interp_mode = (cfg->interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
    s_cfg.text_refresh_div = s_clamp_u8(cfg->text_refresh_div, 1u, 20u);
    s_cfg.blit_rows = s_clamp_u8(cfg->blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
}

void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg)
{
    if (cfg != NULL)
    {
        *cfg = s_cfg;
    }
}

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

App_Display_Mode_t App_Display_GetMode(void)
{
    return s_mode;
}

uint8_t App_Display_IsReady(void)
{
    return s_ready;
}

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

static void s_draw_hline_async(uint16_t x0, uint16_t y, uint16_t x1, uint32_t color)
{
    s_fill_rect_async(x0, y, x1, y, color);
}

static void s_draw_vline_async(uint16_t x, uint16_t y0, uint16_t y1, uint32_t color)
{
    s_fill_rect_async(x, y0, x, y1, color);
}

static void s_draw_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    s_draw_hline_async(x0, y0, x1, color);
    s_draw_hline_async(x0, y1, x1, color);
    s_draw_vline_async(x0, y0, y1, color);
    s_draw_vline_async(x1, y0, y1, color);
}

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

static void s_commit_frame(void)
{
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

void App_Display_Init(void)
{
    uint16_t draw_w;
    uint16_t draw_h;
    uint16_t text_w;
    uint16_t map_w;
    uint16_t map_h;
    uint16_t map_size;

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
    App_Display_SetMode(APP_DISPLAY_MODE_FAST);

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

    text_w = (draw_w > (APP_DISPLAY_TEXT_WIDTH_PX + APP_DISPLAY_MARGIN_PX * 3u))
             ? APP_DISPLAY_TEXT_WIDTH_PX
             : (draw_w / 3u);
    map_w = (uint16_t)(draw_w - (APP_DISPLAY_MARGIN_PX * 3u + text_w));
    map_h = (uint16_t)(draw_h - (APP_DISPLAY_MARGIN_PX * 2u));
    map_size = (map_w < map_h) ? map_w : map_h;
    if (map_size > APP_DISPLAY_MAX_LINE_PIXELS)
    {
        map_size = APP_DISPLAY_MAX_LINE_PIXELS;
    }
    if (map_size < 32u)
    {
        g_display_init_error = 2u;
        g_display_init_stage = 0xE002u;
        return;
    }

    s_map_x0 = APP_DISPLAY_MARGIN_PX;
    s_map_y0 = (uint16_t)((draw_h - map_size) / 2u);
    s_map_x1 = (uint16_t)(s_map_x0 + map_size - 1u);
    s_map_y1 = (uint16_t)(s_map_y0 + map_size - 1u);
    s_text_x = (uint16_t)(s_map_x1 + APP_DISPLAY_MARGIN_PX);

    g_display_init_stage = 4u;
    s_fb_addr_a = ltdc_get_frontbuf_addr();
    s_fb_addr_b = ltdc_get_backbuf_addr();
    DMA2D_Accel_LoadClutFromRgb565(s_heat_lut, APP_DISPLAY_HEAT_LUT_SIZE);

    s_fill_rect_async(0u, 0u, (uint16_t)(draw_w - 1u), (uint16_t)(draw_h - 1u), BLACK);
    {
        uint16_t bx0 = (s_map_x0 > 0u) ? (uint16_t)(s_map_x0 - 1u) : s_map_x0;
        uint16_t by0 = (s_map_y0 > 0u) ? (uint16_t)(s_map_y0 - 1u) : s_map_y0;
        uint16_t bx1 = ((uint32_t)s_map_x1 + 1u < draw_w) ? (uint16_t)(s_map_x1 + 1u) : s_map_x1;
        uint16_t by1 = ((uint32_t)s_map_y1 + 1u < draw_h) ? (uint16_t)(s_map_y1 + 1u) : s_map_y1;
        s_draw_rect_async(bx0, by0, bx1, by1, WHITE);
    }
    s_commit_frame();

    s_ready = 1u;
    g_display_init_stage = 0x8000u;
}

static float s_power_mag(float v)
{
    if (!isfinite(v) || (v <= 0.0f))
    {
        return 0.0f;
    }
    return v;
}

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

static void s_resample_coarse_to_field(const SRP_VisFrame_t *vis_frame)
{
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

static void s_apply_blur_once(void)
{
#if (APP_DISPLAY_SMOOTH_ENABLE != 0u)
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

static void s_update_norm_field(float field_peak, uint32_t frame_seq)
{
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
        if (s_field_a[i] < (field_peak * 0.5f))
        {
            bg_sum += s_field_a[i];
            bg_cnt++;
        }
    }
    floor_linear = field_peak * s_cfg.noise_gate_ratio;
    if (bg_cnt > 0u)
    {
        float bg_floor = (bg_sum / (float)bg_cnt) * s_cfg.noise_adapt_gain;
        if (bg_floor > floor_linear)
        {
            floor_linear = bg_floor;
        }
    }
    floor_linear = s_clamp_f32(floor_linear, 0.0f, field_peak);
    s_last_noise_floor = floor_linear;

    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        float v = s_field_a[i] - floor_linear;
        float ratio;
        float db;
        float t;
        uint32_t q;
        if (v <= 0.0f)
        {
            s_field_norm_u8[i] = 0u;
            continue;
        }
        ratio = s_clamp_f32(v / ref, 1.0e-9f, 1.0f);
        db = 20.0f * log10f(ratio);
        if (db <= s_cfg.db_floor)
        {
            s_field_norm_u8[i] = 0u;
            continue;
        }
        if (db >= 0.0f)
        {
            s_field_norm_u8[i] = 255u;
            continue;
        }
        t = s_clamp_f32((db - s_cfg.db_floor) / (-s_cfg.db_floor), 0.0f, 1.0f);
        if (fabsf(s_cfg.gamma - 1.0f) > 0.02f)
        {
            t = powf(t, s_cfg.gamma);
        }
        q = (uint32_t)(t * 255.0f + 0.5f);
        s_field_norm_u8[i] = (q > 255u) ? 255u : (uint8_t)q;
    }
}

static uint8_t s_sample_norm_nearest(float fx, float fy)
{
    uint32_t x;
    uint32_t y;
    fx = s_clamp_f32(fx, 0.0f, (float)(APP_DISPLAY_FIELD_W - 1u));
    fy = s_clamp_f32(fy, 0.0f, (float)(APP_DISPLAY_FIELD_H - 1u));
    x = (uint32_t)(fx + 0.5f);
    y = (uint32_t)(fy + 0.5f);
    if (x >= APP_DISPLAY_FIELD_W)
    {
        x = APP_DISPLAY_FIELD_W - 1u;
    }
    if (y >= APP_DISPLAY_FIELD_H)
    {
        y = APP_DISPLAY_FIELD_H - 1u;
    }
    return s_field_norm_u8[y * APP_DISPLAY_FIELD_W + x];
}

static uint8_t s_sample_norm_bilinear(float fx, float fy)
{
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    float wx;
    float wy;
    float v00;
    float v01;
    float v10;
    float v11;
    float vx0;
    float vx1;
    uint32_t q;

    fx = s_clamp_f32(fx, 0.0f, (float)(APP_DISPLAY_FIELD_W - 1u));
    fy = s_clamp_f32(fy, 0.0f, (float)(APP_DISPLAY_FIELD_H - 1u));
    x0 = (uint32_t)fx;
    y0 = (uint32_t)fy;
    x1 = (x0 + 1u < APP_DISPLAY_FIELD_W) ? (x0 + 1u) : x0;
    y1 = (y0 + 1u < APP_DISPLAY_FIELD_H) ? (y0 + 1u) : y0;
    wx = fx - (float)x0;
    wy = fy - (float)y0;

    v00 = (float)s_field_norm_u8[y0 * APP_DISPLAY_FIELD_W + x0];
    v01 = (float)s_field_norm_u8[y0 * APP_DISPLAY_FIELD_W + x1];
    v10 = (float)s_field_norm_u8[y1 * APP_DISPLAY_FIELD_W + x0];
    v11 = (float)s_field_norm_u8[y1 * APP_DISPLAY_FIELD_W + x1];
    vx0 = v00 * (1.0f - wx) + v01 * wx;
    vx1 = v10 * (1.0f - wx) + v11 * wx;
    q = (uint32_t)(vx0 * (1.0f - wy) + vx1 * wy + 0.5f);
    return (q > 255u) ? 255u : (uint8_t)q;
}

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

static void s_render_field_rows(void)
{
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;

    if ((map_w == 0u) || (map_h == 0u) || (map_w > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

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
            float fy = (map_h > 1u) ? ((float)y * (float)(APP_DISPLAY_FIELD_H - 1u) / (float)(map_h - 1u)) : 0.0f;
            uint16_t x;
            uint8_t *dst = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];

            for (x = 0u; x < map_w; x++)
            {
                float fx = (map_w > 1u) ? ((float)x * (float)(APP_DISPLAY_FIELD_W - 1u) / (float)(map_w - 1u)) : 0.0f;
                uint8_t q = (use_bilinear != 0u) ? s_sample_norm_bilinear(fx, fy) : s_sample_norm_nearest(fx, fy);
                dst[x] = q;
            }
        }

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

            if (ltdc_color_fill_async(s_map_x0,
                                      (uint16_t)(s_map_y0 + y_blk),
                                      s_map_x1,
                                      (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                      s_blit_buf) == 0u)
            {
                lcd_color_fill(s_map_x0,
                               (uint16_t)(s_map_y0 + y_blk),
                               s_map_x1,
                               (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                               s_blit_buf);
            }
        }
    }
}

static void s_draw_overlay(const Sound_Pos_t *pos,
                           uint32_t frame_seq,
                           uint32_t peak_idx,
                           float peak_theta,
                           float peak_phi,
                           float field_peak,
                           uint8_t sai_dma_active)
{
    char line[80];
    static char title[] = "Acoustic Imaging";

    if (s_text_x >= (lcddev.width - APP_DISPLAY_MARGIN_PX))
    {
        return;
    }

    lcd_fill(s_text_x,
             (uint16_t)(APP_DISPLAY_MARGIN_PX + 20u),
             (uint16_t)(lcddev.width - APP_DISPLAY_MARGIN_PX),
             s_map_y1,
             BLACK);
    lcd_show_string(s_text_x,
                    APP_DISPLAY_MARGIN_PX,
                    (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX),
                    16,
                    16,
                    title,
                    CYAN);
    (void)snprintf(line, sizeof(line), "X:%6.1f", (double)pos->x_angle);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 24u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, YELLOW);
    (void)snprintf(line, sizeof(line), "Y:%6.1f", (double)pos->y_angle);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 42u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, YELLOW);
    (void)snprintf(line, sizeof(line), "E:%0.3f", isfinite(pos->energy) ? (double)pos->energy : 0.0);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 60u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, GREEN);
    (void)snprintf(line, sizeof(line), "F:%lu Pk:%c", (unsigned long)frame_seq, (peak_idx < COARSE_TOTAL) ? 'C' : 'F');
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 78u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, CYAN);
    (void)snprintf(line, sizeof(line), "Pk:(%0.1f,%0.1f)", (double)peak_theta, (double)peak_phi);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 96u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, WHITE);
    (void)snprintf(line,
                   sizeof(line),
                   "Mode:%s/%s dB:%0.0f",
                   App_Display_ModeName(s_mode),
                   App_Display_InterpName((App_Display_Interp_t)s_cfg.interp_mode),
                   (double)s_cfg.db_floor);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 114u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, CYAN);
    (void)snprintf(line, sizeof(line), "Nf:%0.2e G:%0.2f", (double)s_last_noise_floor, (double)s_cfg.gamma);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 132u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, YELLOW);
#if (APP_DISPLAY_DIAG_OVERLAY != 0u)
    (void)snprintf(line, sizeof(line), "PkE:%0.2e EMA:%0.2e", (double)field_peak, (double)s_peak_ema);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 150u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, LIGHTGREEN);
    (void)snprintf(line,
                   sizeof(line),
                   "D2D TO:%lu SW:%lu Q:%lu/%lu",
                   (unsigned long)g_ltdc_dma2d_timeout_count,
                   (unsigned long)g_ltdc_dma2d_sw_fallback_count,
                   (unsigned long)g_dma2d_queue_overflow_count,
                   (unsigned long)g_dma2d_queue_depth_peak);
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 168u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, GREEN);
    (void)snprintf(line,
                   sizeof(line),
                   "Swap:%lu Err:%lu SAI:%s",
                   (unsigned long)g_ltdc_swap_count,
                   (unsigned long)g_ltdc_swap_error_count,
                   (sai_dma_active != 0u) ? "ON" : "OFF");
    lcd_show_string(s_text_x, (uint16_t)(APP_DISPLAY_MARGIN_PX + 186u), (uint16_t)(lcddev.width - s_text_x - APP_DISPLAY_MARGIN_PX), 16, 16, line, LIGHTBLUE);
#endif
}

void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active)
{
    float field_peak;
    uint32_t peak_idx = 0u;
    float peak_theta = 0.0f;
    float peak_phi = 0.0f;
    uint8_t refresh_text;
    uint8_t back_slot;

    if ((s_ready == 0u) || (pos == NULL) || (vis_frame == NULL))
    {
        return;
    }
    if (ltdc_is_swap_pending() != 0u)
    {
        return;
    }

    field_peak = s_prepare_field(vis_frame);
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

    s_update_norm_field(field_peak, frame_seq);
    s_render_field_rows();
    {
        uint16_t bx0 = (s_map_x0 > 0u) ? (uint16_t)(s_map_x0 - 1u) : s_map_x0;
        uint16_t by0 = (s_map_y0 > 0u) ? (uint16_t)(s_map_y0 - 1u) : s_map_y0;
        uint16_t bx1 = ((uint32_t)s_map_x1 + 1u < lcddev.width) ? (uint16_t)(s_map_x1 + 1u) : s_map_x1;
        uint16_t by1 = ((uint32_t)s_map_y1 + 1u < lcddev.height) ? (uint16_t)(s_map_y1 + 1u) : s_map_y1;
        s_draw_rect_async(bx0, by0, bx1, by1, WHITE);
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
        s_draw_overlay(pos, frame_seq, peak_idx, peak_theta, peak_phi, field_peak, sai_dma_active);
    }

    s_commit_frame();
}
