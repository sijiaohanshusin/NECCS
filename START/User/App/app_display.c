/**
 * @file    app_display.c
 * @brief   声学相机 UI 显示实现
 * @details 实时渲染 SRP-PHAT 声源定位结果到 LCD 屏幕
 *
 * 显示内容：
 * - 7×7 热力图 (粗搜索功率分布)
 * - 十字准星 (精细定位结果)
 * - 文本信息 (角度、能量、帧序号)
 * - 诊断信息 (对比度、质量、DMA 状态)
 *
 * 热力图配色：
 * - 蓝色 (低能量) → 绿色 → 黄色 → 红色 (高能量)
 * - 动态范围：-60dB ~ 0dB (相对峰值)
 *
 * 性能优化：
 * - 使用 DMA2D 硬件加速填充 (如果启用)
 * - 仅刷新变化区域 (热力图 + 文本区)
 * - 帧率：30 FPS (33ms 刷新周期)
 */

#include "app_display.h"

#include "LCD/lcd.h"
#include "ai_beamforming.h"
#include "ai_config.h"
#include "ai_srp_lut.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define UI_MARGIN_PX        8u
#define UI_TEXT_WIDTH_PX    140u
#define UI_ANGLE_MIN_DEG   (-60.0f)
#define UI_ANGLE_MAX_DEG   (60.0f)
#define UI_DYNAMIC_MAG_MIN  (1.0e-6f)

/* ==================== 显示调试开关 ==================== */
#define APP_DISPLAY_DIAG_OVERLAY       1u
#define APP_DISPLAY_DEBUG_LOG          0u
#define APP_DISPLAY_IDLE_TEST_PATTERN  1u

static uint8_t s_ready = 0u;
volatile uint32_t g_display_init_stage = 0u;
volatile uint32_t g_display_init_error = 0u;

static uint16_t s_map_x0 = 0u;
static uint16_t s_map_y0 = 0u;
static uint16_t s_map_x1 = 0u;
static uint16_t s_map_y1 = 0u;

static uint16_t s_cell_w = 1u;
static uint16_t s_cell_h = 1u;
static uint16_t s_text_x = 0u;

/**
 * @brief   限制 uint16_t 值到指定范围
 * @param   v   输入值 (int32_t，允许负数)
 * @param   lo  下限
 * @param   hi  上限
 * @return  限制后的值 [lo, hi]
 */
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

/**
 * @brief   RGB888 转 RGB565
 * @param   r   红色分量 (0-255)
 * @param   g   绿色分量 (0-255)
 * @param   b   蓝色分量 (0-255)
 * @return  RGB565 颜色值
 * @note    RGB565 格式：RRRRR GGGGGG BBBBB (5-6-5 位)
 */
static uint16_t s_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

/**
 * @brief   热力图配色函数
 * @param   t   归一化值 [0.0, 1.0]
 * @return  RGB565 颜色值
 * @details 配色方案：
 *          - [0.0, 0.33]: 蓝色 → 青色 (低能量)
 *          - [0.33, 0.67]: 青色 → 黄色 (中等能量)
 *          - [0.67, 1.0]: 黄色 → 红色 (高能量)
 */
static uint32_t s_heat_color(float32_t t)
{
    float32_t x = t;
    uint8_t r = 0u;
    uint8_t g = 0u;
    uint8_t b = 0u;

    if (x < 0.0f)
    {
        x = 0.0f;
    }
    if (x > 1.0f)
    {
        x = 1.0f;
    }

    if (x < 0.33333334f)
    {
        float32_t k = x / 0.33333334f;
        r = 0u;
        g = (uint8_t)(255.0f * k);
        b = (uint8_t)(128.0f + 127.0f * k);
    }
    else if (x < 0.6666667f)
    {
        float32_t k = (x - 0.33333334f) / 0.33333334f;
        r = (uint8_t)(255.0f * k);
        g = 255u;
        b = (uint8_t)(255.0f * (1.0f - k));
    }
    else
    {
        float32_t k = (x - 0.6666667f) / 0.3333333f;
        r = 255u;
        g = (uint8_t)(255.0f * (1.0f - k));
        b = 0u;
    }

    return s_rgb565(r, g, b);
}

/**
 * @brief   检查显示模块是否就绪
 * @return  1=就绪, 0=未就绪
 */
uint8_t App_Display_IsReady(void)
{
    return s_ready;
}

/**
 * @brief   初始化显示模块
 * @details 执行以下步骤：
 *          1. 初始化 LCD 硬件 (LTDC + 面板)
 *          2. 检测屏幕分辨率
 *          3. 计算热力图布局 (7×7 网格)
 *          4. 绘制边框和标题
 *          5. 设置就绪标志
 * @note    如果屏幕未检测到或分辨率过小，会跳过初始化
 */
void App_Display_Init(void)
{
    uint16_t draw_w;
    uint16_t draw_h;
    uint16_t map_size;
    static const char title[] = "Acoustic Imaging";

    g_display_init_stage = 1u;
    g_display_init_error = 0u;
    s_ready = 0u;

    g_display_init_stage = 2u;
    lcd_init();

    g_display_init_stage = 3u;
#if APP_DISPLAY_DEBUG_LOG
    (void)printf("Display probe: ID=0x%04X, lcd=%ux%u, panel=%lux%lu\r\n",
                 lcddev.id,
                 lcddev.width,
                 lcddev.height,
                 (unsigned long)lcdltdc.pwidth,
                 (unsigned long)lcdltdc.pheight);
#endif
    g_display_init_stage = 4u;

    if ((lcddev.width == 0u) || (lcddev.height == 0u))
    {
        g_display_init_error = 1u;
        g_display_init_stage = 0xE001u;
#if APP_DISPLAY_DEBUG_LOG
        (void)printf("Display init skipped: invalid LCD geometry.\r\n");
#endif
        return;
    }

    draw_w = lcddev.width;
    draw_h = lcddev.height;

    if (draw_w <= (UI_MARGIN_PX * 3u + 40u))
    {
        g_display_init_error = 2u;
        g_display_init_stage = 0xE002u;
#if APP_DISPLAY_DEBUG_LOG
        (void)printf("Display init skipped: width too small (%u).\r\n", draw_w);
#endif
        return;
    }

    {
        uint16_t text_w = (draw_w > (UI_TEXT_WIDTH_PX + UI_MARGIN_PX * 3u)) ? UI_TEXT_WIDTH_PX : (draw_w / 3u);
        uint16_t map_w = (uint16_t)(draw_w - (UI_MARGIN_PX * 3u + text_w));
        uint16_t map_h = (uint16_t)(draw_h - (UI_MARGIN_PX * 2u));
        map_size = (map_w < map_h) ? map_w : map_h;
        if (map_size < COARSE_GRID_SIZE)
        {
            map_size = COARSE_GRID_SIZE;
        }
        s_cell_w = (uint16_t)(map_size / COARSE_GRID_SIZE);
        s_cell_h = (uint16_t)(map_size / COARSE_GRID_SIZE);
        if (s_cell_w == 0u)
        {
            s_cell_w = 1u;
        }
        if (s_cell_h == 0u)
        {
            s_cell_h = 1u;
        }

        s_map_x0 = UI_MARGIN_PX;
        s_map_y0 = (uint16_t)((draw_h - s_cell_h * COARSE_GRID_SIZE) / 2u);
        s_map_x1 = (uint16_t)(s_map_x0 + s_cell_w * COARSE_GRID_SIZE - 1u);
        s_map_y1 = (uint16_t)(s_map_y0 + s_cell_h * COARSE_GRID_SIZE - 1u);
        s_text_x = (uint16_t)(s_map_x1 + UI_MARGIN_PX);
    }

    lcd_clear(BLACK);
    lcd_draw_rectangle((uint16_t)(s_map_x0 - 1u), (uint16_t)(s_map_y0 - 1u), (uint16_t)(s_map_x1 + 1u), (uint16_t)(s_map_y1 + 1u), WHITE);
    lcd_show_string(s_text_x, UI_MARGIN_PX, (uint16_t)(draw_w - s_text_x - UI_MARGIN_PX), 24, 16, title, CYAN);

    s_ready = 1u;
    g_display_init_stage = 0x8000u;
}

/**
 * @brief   渲染声学相机 UI
 * @param   pos           SRP 定位结果 (角度 + 能量)
 * @param   coarse_power  粗搜索功率图 (7×7 = 49 点)
 * @param   frame_seq     帧序号 (用于心跳指示)
 * @details 渲染内容：
 *          1. 7×7 热力图 (动态范围 -60dB ~ 0dB)
 *          2. 十字准星 (精细定位结果)
 *          3. 峰值矩形框 (粗搜索最大值位置)
 *          4. 文本信息 (角度、能量、帧号、诊断)
 *          5. 心跳指示 (左上角闪烁块)
 * @note    如果无有效能量，显示棋盘测试图案
 */
void App_Display_Render(const Sound_Pos_t *pos, const float32_t *coarse_power, uint32_t frame_seq)
{
    float32_t peak_mag = 0.0f;
    uint8_t has_dynamic = 0u;
    uint32_t max_idx = 0u;
    uint16_t gx;
    uint16_t gy;
    char line[64];

    if ((s_ready == 0u) || (pos == NULL) || (coarse_power == NULL))
    {
        return;
    }

    for (uint32_t i = 0u; i < COARSE_TOTAL; i++)
    {
        float32_t v = coarse_power[i];
        float32_t mag;

        if (!isfinite(v))
        {
            v = 0.0f;
        }
        mag = fabsf(v);

        if (mag > peak_mag)
        {
            peak_mag = mag;
            max_idx = i;
        }
    }
    has_dynamic = (peak_mag > UI_DYNAMIC_MAG_MIN) ? 1u : 0u;

    for (gy = 0u; gy < COARSE_GRID_SIZE; gy++)
    {
        for (gx = 0u; gx < COARSE_GRID_SIZE; gx++)
        {
            uint32_t idx = (uint32_t)gy * COARSE_GRID_SIZE + gx;
            float32_t v = coarse_power[idx];
            float32_t mag;
            float32_t t;
            uint16_t x0;
            uint16_t y0;
            uint16_t x1;
            uint16_t y1;

            if (!isfinite(v))
            {
                v = 0.0f;
            }
            mag = fabsf(v);

            if (has_dynamic != 0u)
            {
                float32_t ratio = mag / peak_mag;   /* [0,1] */
                if (ratio < 1.0e-6f)
                {
                    ratio = 1.0e-6f;               /* clamp to -60dB */
                }
                t = (log10f(ratio) + 6.0f) / 6.0f; /* -60dB~0dB -> 0~1 */
            }
            else
            {
#if APP_DISPLAY_IDLE_TEST_PATTERN
                /* 无有效能量动态时，显示棋盘图案用于确认屏幕持续刷新。 */
                uint32_t phase = (frame_seq >> 2) & 1u;
                t = (((gx + gy + phase) & 1u) != 0u) ? 0.55f : 0.25f;
#else
                t = 0.0f;
#endif
            }

            x0 = (uint16_t)(s_map_x0 + gx * s_cell_w);
            y0 = (uint16_t)(s_map_y0 + gy * s_cell_h);
            x1 = (uint16_t)(x0 + s_cell_w - 1u);
            y1 = (uint16_t)(y0 + s_cell_h - 1u);

            lcd_fill(x0, y0, x1, y1, s_heat_color(t));
        }
    }
#if APP_DISPLAY_DIAG_OVERLAY
    /* 左上角心跳块: 用于肉眼确认 UI 正在持续刷新。 */
    {
        uint16_t hb_w = (s_cell_w > 12u) ? 12u : s_cell_w;
        uint16_t hb_h = (s_cell_h > 12u) ? 12u : s_cell_h;
        uint16_t hb_x1 = (uint16_t)(s_map_x0 + hb_w - 1u);
        uint16_t hb_y1 = (uint16_t)(s_map_y0 + hb_h - 1u);
        uint32_t hb_color = (((frame_seq >> 1) & 1u) != 0u) ? GREEN : RED;
        lcd_fill(s_map_x0, s_map_y0, hb_x1, hb_y1, hb_color);
    }
#endif

    {
        uint16_t cx;
        uint16_t cy;
        uint16_t c_half;
        uint16_t x_l;
        uint16_t x_r;
        uint16_t y_t;
        uint16_t y_b;
        float32_t ax = pos->x_angle;
        float32_t ay = pos->y_angle;
        float32_t ratio_x;
        float32_t ratio_y;

        if (!isfinite(ax))
        {
            ax = 0.0f;
        }
        if (!isfinite(ay))
        {
            ay = 0.0f;
        }

        if (ax < UI_ANGLE_MIN_DEG)
        {
            ax = UI_ANGLE_MIN_DEG;
        }
        if (ax > UI_ANGLE_MAX_DEG)
        {
            ax = UI_ANGLE_MAX_DEG;
        }
        if (ay < UI_ANGLE_MIN_DEG)
        {
            ay = UI_ANGLE_MIN_DEG;
        }
        if (ay > UI_ANGLE_MAX_DEG)
        {
            ay = UI_ANGLE_MAX_DEG;
        }

        ratio_x = (ax - UI_ANGLE_MIN_DEG) / (UI_ANGLE_MAX_DEG - UI_ANGLE_MIN_DEG);
        ratio_y = (UI_ANGLE_MAX_DEG - ay) / (UI_ANGLE_MAX_DEG - UI_ANGLE_MIN_DEG);

        cx = (uint16_t)(s_map_x0 + ratio_x * (float32_t)(s_cell_w * COARSE_GRID_SIZE - 1u));
        cy = (uint16_t)(s_map_y0 + ratio_y * (float32_t)(s_cell_h * COARSE_GRID_SIZE - 1u));

        cx = s_clamp_u16(cx, s_map_x0, s_map_x1);
        cy = s_clamp_u16(cy, s_map_y0, s_map_y1);

        c_half = (uint16_t)(((s_cell_w < s_cell_h) ? s_cell_w : s_cell_h) / 2u);
        if (c_half < 2u)
        {
            c_half = 2u;
        }

        x_l = (cx > (uint16_t)(s_map_x0 + c_half)) ? (uint16_t)(cx - c_half) : s_map_x0;
        x_r = ((uint32_t)cx + c_half < s_map_x1) ? (uint16_t)(cx + c_half) : s_map_x1;
        y_t = (cy > (uint16_t)(s_map_y0 + c_half)) ? (uint16_t)(cy - c_half) : s_map_y0;
        y_b = ((uint32_t)cy + c_half < s_map_y1) ? (uint16_t)(cy + c_half) : s_map_y1;

        lcd_draw_line(x_l, cy, x_r, cy, WHITE);
        lcd_draw_line(cx, y_t, cx, y_b, WHITE);
    }

    {
        uint16_t peak_col = (uint16_t)(max_idx % COARSE_GRID_SIZE);
        uint16_t peak_row = (uint16_t)(max_idx / COARSE_GRID_SIZE);
        uint16_t x0 = (uint16_t)(s_map_x0 + peak_col * s_cell_w);
        uint16_t y0 = (uint16_t)(s_map_y0 + peak_row * s_cell_h);
        uint16_t x1 = (uint16_t)(x0 + s_cell_w - 1u);
        uint16_t y1 = (uint16_t)(y0 + s_cell_h - 1u);
        lcd_draw_rectangle(x0, y0, x1, y1, WHITE);
    }

    lcd_fill(s_text_x, (uint16_t)(UI_MARGIN_PX + 20u), (uint16_t)(lcddev.width - UI_MARGIN_PX), s_map_y1, BLACK);

    (void)snprintf(line, sizeof(line), "X:%6.1f deg", (double)pos->x_angle);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 24u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, YELLOW);

    (void)snprintf(line, sizeof(line), "Y:%6.1f deg", (double)pos->y_angle);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 42u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, YELLOW);

    (void)snprintf(line, sizeof(line), "E:%0.3f", isfinite(pos->energy) ? (double)pos->energy : 0.0);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 60u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, GREEN);

    (void)snprintf(line, sizeof(line), "F:%lu", (unsigned long)frame_seq);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 78u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, (((frame_seq >> 1) & 1u) != 0u) ? GREEN : CYAN);

    (void)snprintf(line, sizeof(line), "Pk:(%0.1f,%0.1f)", (double)coarse_theta_deg[max_idx / COARSE_GRID_SIZE], (double)coarse_phi_deg[max_idx % COARSE_GRID_SIZE]);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 96u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, WHITE);

#if APP_DISPLAY_DIAG_OVERLAY
    (void)snprintf(line, sizeof(line), "UI:R%lu Q%lu T%lu",
                   (unsigned long)g_ui_render_count,
                   (unsigned long)g_ui_queue_rx_count,
                   (unsigned long)g_ui_queue_timeout_count);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 114u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, CYAN);

    (void)snprintf(line, sizeof(line), "SRP:C%0.2f Q%0.2f",
                   (double)g_srp_last_contrast,
                   (double)g_srp_last_quality);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 132u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, YELLOW);

    (void)snprintf(line, sizeof(line), "Mag:%0.2e DMA:%s TO:%lu",
                   (double)peak_mag,
                   (LTDC_ENABLE_DMA2D != 0) ? "ON" : "SW",
                   (unsigned long)g_ltdc_dma2d_timeout_count);
    lcd_show_string(s_text_x, (uint16_t)(UI_MARGIN_PX + 150u), (uint16_t)(lcddev.width - s_text_x - UI_MARGIN_PX), 16, 16, line, GREEN);
#endif
}
