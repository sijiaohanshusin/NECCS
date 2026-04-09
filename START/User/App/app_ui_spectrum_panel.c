/**
 * @file    app_ui_spectrum_panel.c
 * @brief   LVGL 频谱面板组件 —— canvas 柱状图 + 频段 slider
 */
#include "app_ui_spectrum_panel.h"

#if (APP_LVGL_ENABLE != 0u)

#include "app_runtime.h"
#include "app_spectrum.h"
#include "app_ui_styles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 常量
 * ============================================================================ */
#define SPEC_CANVAS_BUF_MAX (148u * 260u) /**< max buffer for any panel size */
#define SPEC_MARGIN         2u
#define SPEC_DB_FLOOR_VAL   (-40.0f)
#define SPEC_MIN_MAG        1.0e-8f
#define SPEC_EMA_ATTACK     0.4f
#define SPEC_EMA_DECAY      0.08f

/* ============================================================================
 * 静态变量
 * ============================================================================ */
static lv_obj_t  *s_panel_root    = NULL;
static lv_obj_t  *s_canvas        = NULL;
static lv_obj_t  *s_slider_lo     = NULL;
static lv_obj_t  *s_slider_hi     = NULL;
static lv_obj_t  *s_lbl_band      = NULL;
static lv_obj_t  *s_lbl_peak      = NULL;

static lv_color_t s_canvas_buf[SPEC_CANVAS_BUF_MAX];
static uint16_t s_cvs_w = 140u;  /**< actual canvas width */
static uint16_t s_cvs_h = 200u;  /**< actual canvas height */

static float  s_ema[APP_SPECTRUM_BIN_COUNT];
static uint8_t s_ema_valid = 0u;
static float  s_ref_mag = SPEC_MIN_MAG;

/* 频段预设表 */
static const App_FreqBand_t s_presets[SPEC_PRESET_COUNT] = {
    { 3u,  42u },   /* FULL */
    { 2u,  18u },   /* VOICE */
    { 54u, 128u },  /* ULTRA */
    { 1u,  5u  }    /* LOW */
};
static const char *s_preset_names[SPEC_PRESET_COUNT] = {
    "Full", "Voice", "Ultra", "Low"
};

/* ============================================================================
 * 内部辅助
 * ============================================================================ */

/** @brief 将 bin 索引映射到 canvas Y 坐标（低频在底部） */
static uint16_t s_plot_w(void) { return (uint16_t)(s_cvs_w - 2u * SPEC_MARGIN); }
static uint16_t s_plot_h(void) { return (uint16_t)(s_cvs_h - 2u * SPEC_MARGIN); }

static uint16_t s_bin_to_y(uint16_t bin, uint16_t bin_count)
{
    uint16_t ph = s_plot_h();
    if (bin_count <= 1u)
    {
        return (uint16_t)(SPEC_MARGIN + ph / 2u);
    }
    return (uint16_t)(SPEC_MARGIN + ph
                      - (uint32_t)bin * ph / (bin_count - 1u));
}

/** @brief 将 dB 值映射到 canvas X 方向长度 */
static uint16_t s_db_to_barlen(float rel_db)
{
    float ratio;

    if (rel_db < SPEC_DB_FLOOR_VAL)
    {
        rel_db = SPEC_DB_FLOOR_VAL;
    }
    if (rel_db > 0.0f)
    {
        rel_db = 0.0f;
    }

    ratio = 1.0f - (rel_db / SPEC_DB_FLOOR_VAL);
    return (uint16_t)(ratio * (float)s_plot_w());
}

/* ============================================================================
 * 回调
 * ============================================================================ */
static void s_slider_lo_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(lv_event_get_target(e));
    int32_t hi = lv_slider_get_value(s_slider_hi);
    char buf[32];

    if (lo > hi)
    {
        lo = hi;
        lv_slider_set_value(lv_event_get_target(e), lo, LV_ANIM_OFF);
    }
    App_RuntimeConfig_SetFreqBand((uint16_t)lo, (uint16_t)hi);

    if (s_lbl_band != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%d-%d Hz",
                       (int)App_Spectrum_BinToHz((uint16_t)lo),
                       (int)App_Spectrum_BinToHz((uint16_t)hi));
        lv_label_set_text(s_lbl_band, buf);
    }
    (void)e;
}

static void s_slider_hi_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(s_slider_lo);
    int32_t hi = lv_slider_get_value(lv_event_get_target(e));
    char buf[32];

    if (hi < lo)
    {
        hi = lo;
        lv_slider_set_value(lv_event_get_target(e), hi, LV_ANIM_OFF);
    }
    App_RuntimeConfig_SetFreqBand((uint16_t)lo, (uint16_t)hi);

    if (s_lbl_band != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%d-%d Hz",
                       (int)App_Spectrum_BinToHz((uint16_t)lo),
                       (int)App_Spectrum_BinToHz((uint16_t)hi));
        lv_label_set_text(s_lbl_band, buf);
    }
    (void)e;
}

static void s_preset_btn_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if (idx < SPEC_PRESET_COUNT)
    {
        App_UiSpecPanel_ApplyPreset((App_SpecPreset_t)idx);
    }
}

/* ============================================================================
 * 公开 API
 * ============================================================================ */

lv_obj_t *App_UiSpecPanel_Create(lv_obj_t *parent,
                                  lv_coord_t width,
                                  lv_coord_t height)
{
    uint16_t bin_start, bin_end;
    uint32_t i;
    char buf[32];
    lv_coord_t controls_h;

    App_RuntimeConfig_GetFreqBand(&bin_start, &bin_end);
    (void)memset(s_ema, 0, sizeof(s_ema));
    s_ema_valid = 0u;
    s_ref_mag = SPEC_MIN_MAG;

    /* 计算 canvas 尺寸：面板高度减去控件高度 */
    controls_h = 14 + 2 + 12 + 2 + 12 + 2 + 14 + 2 + 18 + 4; /* labels+sliders+presets+pad */
    s_cvs_w = (uint16_t)(width - 8u);
    if (s_cvs_w > 148u) { s_cvs_w = 148u; }
    s_cvs_h = (uint16_t)(height - controls_h - 8u); /* 8px top+bottom panel pad */
    if (s_cvs_h > 260u) { s_cvs_h = 260u; }
    if (s_cvs_h < 40u)  { s_cvs_h = 40u;  }
    if ((uint32_t)s_cvs_w * s_cvs_h > SPEC_CANVAS_BUF_MAX)
    {
        s_cvs_h = (uint16_t)(SPEC_CANVAS_BUF_MAX / s_cvs_w);
    }

    /* 根容器 */
    s_panel_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel_root);
    lv_obj_set_size(s_panel_root, width, height);
    lv_obj_set_style_bg_color(s_panel_root, UI_COLOR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_panel_root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel_root, UI_RADIUS_DEFAULT, 0);
    lv_obj_set_style_pad_all(s_panel_root, 2, 0);
    lv_obj_set_style_pad_row(s_panel_root, 2, 0);
    lv_obj_clear_flag(s_panel_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_panel_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Canvas 频谱图 */
    s_canvas = lv_canvas_create(s_panel_root);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf,
                         s_cvs_w, s_cvs_h,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_canvas, (lv_coord_t)s_cvs_w, (lv_coord_t)s_cvs_h);
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);

    /* 频段标签 */
    s_lbl_band = lv_label_create(s_panel_root);
    (void)snprintf(buf, sizeof(buf), "%d-%d Hz",
                   (int)App_Spectrum_BinToHz(bin_start),
                   (int)App_Spectrum_BinToHz(bin_end));
    lv_label_set_text(s_lbl_band, buf);
    lv_obj_add_style(s_lbl_band, &g_ui_styles.label_unit, 0);
    lv_obj_set_style_text_color(s_lbl_band, UI_COLOR_ACCENT, 0);

    /* 低频 slider */
    s_slider_lo = lv_slider_create(s_panel_root);
    lv_obj_set_width(s_slider_lo, (lv_coord_t)(s_cvs_w));
    lv_obj_set_height(s_slider_lo, 10);
    lv_slider_set_range(s_slider_lo, 1, (int32_t)SRP_FREQ_BIN_END);
    lv_slider_set_value(s_slider_lo, (int32_t)bin_start, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_lo, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_lo, UI_COLOR_BG_MAIN, LV_PART_MAIN);
    lv_obj_add_event_cb(s_slider_lo, s_slider_lo_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 高频 slider */
    s_slider_hi = lv_slider_create(s_panel_root);
    lv_obj_set_width(s_slider_hi, (lv_coord_t)(s_cvs_w));
    lv_obj_set_height(s_slider_hi, 10);
    lv_slider_set_range(s_slider_hi, 1, (int32_t)SRP_FREQ_BIN_END);
    lv_slider_set_value(s_slider_hi, (int32_t)bin_end, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_hi, UI_COLOR_WARNING, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_hi, UI_COLOR_BG_MAIN, LV_PART_MAIN);
    lv_obj_add_event_cb(s_slider_hi, s_slider_hi_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 峰值标签 */
    s_lbl_peak = lv_label_create(s_panel_root);
    lv_label_set_text(s_lbl_peak, "Pk: --");
    lv_obj_add_style(s_lbl_peak, &g_ui_styles.label_unit, 0);

    /* 预设按钮行 */
    {
        lv_obj_t *row = lv_obj_create(s_panel_root);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, (lv_coord_t)(s_cvs_w), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_column(row, 2, 0);

        for (i = 0u; i < SPEC_PRESET_COUNT; i++)
        {
            lv_obj_t *btn = lv_btn_create(row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 32, 18);
            {
                lv_obj_t *lbl = lv_label_create(btn);
                /* 缩写标签 */
                lv_label_set_text_static(lbl,
                    (i == 0u) ? "F" : (i == 1u) ? "V" : (i == 2u) ? "U" : "L");
                lv_obj_center(lbl);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            }
            lv_obj_add_event_cb(btn, s_preset_btn_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        }
    }

    return s_panel_root;
}

void App_UiSpecPanel_Update(const App_SpectrumFrame_t *frame)
{
    uint16_t bin_count;
    uint16_t min_bin;
    uint16_t peak_bin = 1u;
    float peak_mag = SPEC_MIN_MAG;
    float ref_mag;
    uint16_t i;
    lv_draw_rect_dsc_t rect_dsc;
    char buf[24];
    App_FreqBand_t active_band;
    uint16_t band_y0, band_y1, y0tmp, y1tmp;

    if (s_canvas == NULL)
    {
        return;
    }

    /* 清空 canvas */
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);

    if (frame == NULL || frame->bin_count == 0u)
    {
        return;
    }

    bin_count = frame->bin_count;
    if (bin_count > APP_SPECTRUM_BIN_COUNT)
    {
        bin_count = APP_SPECTRUM_BIN_COUNT;
    }
    min_bin = 1u;
    active_band = frame->active_band;

    /* EMA 平滑 */
    for (i = 0u; i < bin_count; i++)
    {
        float mag = frame->magnitude[i];
        float alpha;

        if ((!isfinite(mag)) || (mag < 0.0f))
        {
            mag = 0.0f;
        }
        if (s_ema_valid == 0u)
        {
            s_ema[i] = mag;
        }
        else
        {
            alpha = (mag >= s_ema[i]) ? SPEC_EMA_ATTACK : SPEC_EMA_DECAY;
            s_ema[i] += alpha * (mag - s_ema[i]);
        }
    }
    s_ema_valid = 1u;

    /* 查找峰值 */
    for (i = min_bin; i < bin_count; i++)
    {
        if (s_ema[i] > peak_mag)
        {
            peak_mag = s_ema[i];
            peak_bin = i;
        }
    }

    /* 参考幅度自适应 */
    if (peak_mag > s_ref_mag)
    {
        s_ref_mag += 0.3f * (peak_mag - s_ref_mag);
    }
    else
    {
        s_ref_mag += 0.02f * (peak_mag - s_ref_mag);
    }
    if (s_ref_mag < SPEC_MIN_MAG)
    {
        s_ref_mag = SPEC_MIN_MAG;
    }
    ref_mag = s_ref_mag;

    /* 绘制活动频段背景高亮 */
    y0tmp = s_bin_to_y(active_band.start_bin, bin_count);
    y1tmp = s_bin_to_y(active_band.end_bin, bin_count);
    band_y0 = (y0tmp < y1tmp) ? y0tmp : y1tmp;
    band_y1 = (y0tmp > y1tmp) ? y0tmp : y1tmp;

    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x001030);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = 0;
    rect_dsc.border_width = 0;
    lv_canvas_draw_rect(s_canvas, SPEC_MARGIN, band_y0,
                        s_plot_w(), (lv_coord_t)(band_y1 - band_y0 + 1u),
                        &rect_dsc);

    /* 绘制频谱柱 */
    for (i = min_bin; i < bin_count; i++)
    {
        float rel_db;
        uint16_t bar_len;
        uint16_t y_center = s_bin_to_y(i, bin_count);
        uint16_t y_next = (i + 1u < bin_count) ? s_bin_to_y((uint16_t)(i + 1u), bin_count) : (uint16_t)SPEC_MARGIN;
        uint16_t bar_y0_px, bar_y1_px;
        uint8_t in_band;

        rel_db = 20.0f * log10f(fmaxf(s_ema[i], SPEC_MIN_MAG) / ref_mag);
        bar_len = s_db_to_barlen(rel_db);
        if (bar_len == 0u)
        {
            continue;
        }

        bar_y0_px = (y_center + y_next) / 2u;
        bar_y1_px = y_center;
        if (bar_y0_px > bar_y1_px)
        {
            uint16_t t = bar_y0_px;
            bar_y0_px = bar_y1_px;
            bar_y1_px = t;
        }
        if (bar_y1_px <= bar_y0_px)
        {
            bar_y1_px = (uint16_t)(bar_y0_px + 1u);
        }

        in_band = (i >= active_band.start_bin && i <= active_band.end_bin) ? 1u : 0u;

        rect_dsc.bg_color = in_band ? lv_color_hex(0xFFDD00) : lv_color_hex(0x00BBCC);
        rect_dsc.bg_opa = LV_OPA_COVER;
        lv_canvas_draw_rect(s_canvas,
                            (lv_coord_t)SPEC_MARGIN,
                            (lv_coord_t)bar_y0_px,
                            (lv_coord_t)bar_len,
                            (lv_coord_t)(bar_y1_px - bar_y0_px),
                            &rect_dsc);
    }

    /* 峰值标记线 */
    {
        uint16_t pk_y = s_bin_to_y(peak_bin, bin_count);
        lv_point_t pts[2];

        pts[0].x = (lv_coord_t)SPEC_MARGIN;
        pts[0].y = (lv_coord_t)pk_y;
        pts[1].x = (lv_coord_t)(SPEC_MARGIN + 12u);
        pts[1].y = (lv_coord_t)pk_y;

        {
            lv_draw_line_dsc_t line_dsc;
            lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = lv_color_hex(0xFF3333);
            line_dsc.width = 2;
            lv_canvas_draw_line(s_canvas, pts, 2, &line_dsc);
        }
    }

    lv_obj_invalidate(s_canvas);

    /* 更新峰值标签 */
    if (s_lbl_peak != NULL)
    {
        float pk_hz = App_Spectrum_BinToHz(peak_bin);
        if (pk_hz >= 1000.0f)
        {
            (void)snprintf(buf, sizeof(buf), "Pk:%.1fk", (double)(pk_hz / 1000.0f));
        }
        else
        {
            (void)snprintf(buf, sizeof(buf), "Pk:%dHz", (int)pk_hz);
        }
        lv_label_set_text(s_lbl_peak, buf);
    }
}

void App_UiSpecPanel_ApplyPreset(App_SpecPreset_t preset)
{
    char buf[32];

    if (preset >= SPEC_PRESET_COUNT)
    {
        return;
    }

    App_RuntimeConfig_SetFreqBand(s_presets[preset].start_bin,
                                  s_presets[preset].end_bin);

    if (s_slider_lo != NULL)
    {
        lv_slider_set_value(s_slider_lo,
                            (int32_t)s_presets[preset].start_bin, LV_ANIM_OFF);
    }
    if (s_slider_hi != NULL)
    {
        lv_slider_set_value(s_slider_hi,
                            (int32_t)s_presets[preset].end_bin, LV_ANIM_OFF);
    }
    if (s_lbl_band != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%s %d-%dHz",
                       s_preset_names[preset],
                       (int)App_Spectrum_BinToHz(s_presets[preset].start_bin),
                       (int)App_Spectrum_BinToHz(s_presets[preset].end_bin));
        lv_label_set_text(s_lbl_band, buf);
    }
}

#endif /* APP_LVGL_ENABLE */
