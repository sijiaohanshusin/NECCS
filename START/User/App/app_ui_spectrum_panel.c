/**
 * @file    app_ui_spectrum_panel.c
 * @brief   LVGL 频谱面板组件 —— canvas 柱状图 + 频段 slider
 * @details 实现了一个垂直布局的频谱显示面板，包含：
 *   1. lv_canvas 柱状频谱图（EMA 平滑，自适应参考幅度）
 *   2. 低豱频控制 slider（bin 索引）
 *   3. 高豱频控制 slider（bin 索引）
 *   4. 频段标签（Hz 展示）和峰値标签
 *   5. 预设按钮行（Full/Voice/Ultra/Low）
 *
 *   具体展示逻辑：第 0 bin 在底部（低频），高频在顶部。
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
#define SPEC_CANVAS_BUF_MAX (148u * 260u) /**< 面板 canvas 最大像素数（148×260），静态分配 \n                                          *   uint16_t 内存占用: 148×260×2 = 77 KB */
#define SPEC_MARGIN         2u            /**< canvas 四周边距（像素），给柱状界线留白 */
#define SPEC_DB_FLOOR_VAL   (-40.0f)      /**< 分贝显示地板：-40 dB 以下的柱展示为 0 */
#define SPEC_MIN_MAG        1.0e-8f       /**< 幅度最小值，保持 log10 周遵不为 -inf（约 -160dB）*/
#define SPEC_EMA_ATTACK     0.4f          /**< EMA 上升速率：高于当前均値时快速跟随（=0.4）*/
#define SPEC_EMA_DECAY      0.08f         /**< EMA 下降衰减速率：低于当前均値时慢慢衰减（=0.08）*/

/* ============================================================================
 * 静态变量
 * ============================================================================ */
static lv_obj_t  *s_panel_root    = NULL; /**< 面板根容器（flex column）*/
static lv_obj_t  *s_canvas        = NULL; /**< canvas 绘图区域 */
static lv_obj_t  *s_slider_lo     = NULL; /**< 低豱频界限 slider（bin 索引）*/
static lv_obj_t  *s_slider_hi     = NULL; /**< 高豱频界限 slider（bin 索引）*/
static lv_obj_t  *s_lbl_band      = NULL; /**< 频段文字标签（显示 Hz）*/
static lv_obj_t  *s_lbl_peak      = NULL; /**< 峰値频率标签 */

static lv_color_t s_canvas_buf[SPEC_CANVAS_BUF_MAX]; /**< canvas 像素缓冲（静态分配于 AXI SRAM）*/
static uint16_t s_cvs_w = 140u;  /**< 实际 canvas 宽度（天面和上限 148）*/
static uint16_t s_cvs_h = 200u;  /**< 实际 canvas 高度（面板高度减控件区高）*/

static float  s_ema[APP_SPECTRUM_BIN_COUNT]; /**< 每个 bin 的 EMA 平滑幅度值 */
static uint8_t s_ema_valid = 0u; /**< 0=EMA 未初始化，1=已有效（首帧直接赋値）*/
static float  s_ref_mag = SPEC_MIN_MAG; /**< 自适应参考幅度（跟随峰値缓慢变化）*/

/* 频段预设表：每项定义 [start_bin, end_bin] */
static const App_FreqBand_t s_presets[SPEC_PRESET_COUNT] = {
    { 3u,  42u },   /* FULL：  bin 3–42 ≈ 562.5–7875 Hz */
    { 2u,  18u },   /* VOICE： bin 2–18 ≈ 375–3375 Hz，人声基频范围 */
    { 54u, 128u },  /* ULTRA： bin 54–128 ≈ 10125–24000 Hz */
    { 1u,  5u  }    /* LOW：   bin 1–5 ≈ 187.5–937.5 Hz，低频振动 */
};
static const char *s_preset_names[SPEC_PRESET_COUNT] = {
    "Full", "Voice", "Ultra", "Low" /* 预设名称，频段标签更新时使用 */
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

/** @brief 低豱频 slider 值变化回调
 * @details 发生时：用户滑动低层 slider。
 *          逻辑：若 lo > hi，将 lo 强制撰努至 hi（不允许频段反转）。
 *          将新 [lo, hi] 应用到 App_RuntimeConfig 并更新频段标签。
 */
static void s_slider_lo_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(lv_event_get_target(e)); /* 低层 slider 当前展示 */
    int32_t hi = lv_slider_get_value(s_slider_hi);            /* 高层 slider 当前展示 */
    char buf[32];

    if (lo > hi)
    {
        lo = hi; /* 不允许低层大于高层，捕变反手拉反 */
        lv_slider_set_value(lv_event_get_target(e), lo, LV_ANIM_OFF); /* 更新 UI 低层位置 */
    }
    App_RuntimeConfig_SetFreqBand((uint16_t)lo, (uint16_t)hi); /* 将 bin 写入全局配置 */

    if (s_lbl_band != NULL)
    {
        /* 将 bin 索引转换为 Hz 并更新标签（App_Spectrum_BinToHz = bin * 48000 / 256 / 2）*/
        (void)snprintf(buf, sizeof(buf), "%d-%d Hz",
                       (int)App_Spectrum_BinToHz((uint16_t)lo),
                       (int)App_Spectrum_BinToHz((uint16_t)hi));
        lv_label_set_text(s_lbl_band, buf);
    }
    (void)e; /* 防止编译器警告：e 已通过 lv_event_get_target(e) 间接使用 */
}

/** @brief 高豱频 slider 值变化回调
 * @details 逻辑与 lo cb 对称：若 hi < lo，将 hi 强制扒至 lo。
 *          [注意] 频段变化会单向流到 App_RuntimeConfig，
 *                   音频任务在下一帧 SRP-PHAT 时读取新配置。
 */
static void s_slider_hi_cb(lv_event_t *e)
{
    int32_t lo = lv_slider_get_value(s_slider_lo); /* 低层当前展示（不变）*/
    int32_t hi = lv_slider_get_value(lv_event_get_target(e)); /* 高层新展示 */
    char buf[32];

    if (hi < lo)
    {
        hi = lo; /* 不允许高层小于低层 */
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

/** @brief 预设按钮点击回调
 * @details 利用 user_data 传递按钮索引：四个按钮 Id = 0–3。
 *          (uintptr_t)山过指针大小安全地传递整数。
 */
static void s_preset_btn_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e); /* 获取长按钮索引 */

    if (idx < SPEC_PRESET_COUNT)
    {
        App_UiSpecPanel_ApplyPreset((App_SpecPreset_t)idx); /* 应用预设频段 */
    }
}

/* ============================================================================
 * 公开 API
 * ============================================================================ */

/** @brief 创建频谱面板组件并安装到父容器
 * @param  parent  将此面板挂载到的 LVGL 耸层对象
 * @param  width   面板总宽（像素）
 * @param  height  面板总高（像素）
 * @return 根容器指针（s_panel_root）
 * @details 布局顺序（上到下 flex column）：
 *   canvas–频段标签–低/高 slider–峰值标签–预设按钮行
 *
 *   canvas 尺度逻辑：
 *     controls_h = 最大 82 px (各控件高度和)
 *     s_cvs_w = min(width-8, 148)上限隆SPEC_CANVAS_BUF_MAX
 *     s_cvs_h = min(max(height-controls_h-8, 40), 260)——最终不超 SPEC_CANVAS_BUF_MAX
 */
lv_obj_t *App_UiSpecPanel_Create(lv_obj_t *parent,
                                  lv_coord_t width,
                                  lv_coord_t height)
{
    uint16_t bin_start, bin_end; /* 当前频段起止 bin */
    uint32_t i;                  /* 预设按钮循环索引 */
    char buf[32];                /* 标签文本缓冲 */
    lv_coord_t controls_h;      /* 面板中非 canvas 区域的总高度 */

    App_RuntimeConfig_GetFreqBand(&bin_start, &bin_end); /* 读取全局配置的频段 */
    (void)memset(s_ema, 0, sizeof(s_ema)); /* 清除 EMA 干扰，防止首帧显示历史数据 */
    s_ema_valid = 0u;            /* 首帧直接赋值，不做 EMA 平滑 */
    s_ref_mag = SPEC_MIN_MAG;    /* 参考幅度从最小开始，自动跟随峰值上升 */

    /* 计算 canvas 尺寸：面板高度减去控件高度 */
    controls_h = 14 + 2 + 12 + 2 + 12 + 2 + 14 + 2 + 18 + 4; /* 频段标签14+间截2+loSlider12+间截2+hiSlider12+间截2+峰值标签14+间截2+预设行18+额外填充44 */
    s_cvs_w = (uint16_t)(width - 8u); /* 面板左右各 4 px pad */
    if (s_cvs_w > 148u) { s_cvs_w = 148u; } /* 防止超公司 BUF_MAX */
    s_cvs_h = (uint16_t)(height - controls_h - 8u); /* 减控件高度 + 上下 4px pad 各 */
    if (s_cvs_h > 260u) { s_cvs_h = 260u; }  /* canvas 高上限 */
    if (s_cvs_h < 40u)  { s_cvs_h = 40u;  }  /* canvas 高下限，至少绘 40 行 */
    if ((uint32_t)s_cvs_w * s_cvs_h > SPEC_CANVAS_BUF_MAX)
    {
        s_cvs_h = (uint16_t)(SPEC_CANVAS_BUF_MAX / s_cvs_w); /* 数据超出缓冲时广调高度 */
    }

    /* 根容器：flex column，所有子元素匹垂居中 */
    s_panel_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel_root);          /* 移除默认样式，完全按项目定义 */
    lv_obj_set_size(s_panel_root, width, height);
    lv_obj_set_style_bg_color(s_panel_root, UI_COLOR_BG_PANEL, 0); /* 深灰面板背景 */
    lv_obj_set_style_bg_opa(s_panel_root, LV_OPA_COVER, 0);        /* 不透明 */
    lv_obj_set_style_radius(s_panel_root, UI_RADIUS_DEFAULT, 0);    /* 圆角 */
    lv_obj_set_style_pad_all(s_panel_root, 2, 0);  /* 四周 2px 内边距 */
    lv_obj_set_style_pad_row(s_panel_root, 2, 0);  /* 子元素行间距 2px */
    lv_obj_clear_flag(s_panel_root, LV_OBJ_FLAG_SCROLLABLE); /* 面板不滚动 */
    lv_obj_set_flex_flow(s_panel_root, LV_FLEX_FLOW_COLUMN); /* 垂直排列子元素 */
    lv_obj_set_flex_align(s_panel_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); /* 水平居中 */

    /* Canvas 频谱图（RGB565，静态缓冲）*/
    s_canvas = lv_canvas_create(s_panel_root);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf,
                         s_cvs_w, s_cvs_h,
                         LV_IMG_CF_TRUE_COLOR); /* TRUE_COLOR = RGB565 */
    lv_obj_set_size(s_canvas, (lv_coord_t)s_cvs_w, (lv_coord_t)s_cvs_h); /* 与 buffer 尺度一致 */
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER); /* 初始化为黑色 */

    /* 频段标签：显示当前频段 Hz 范围 */
    s_lbl_band = lv_label_create(s_panel_root);
    (void)snprintf(buf, sizeof(buf), "%d-%d Hz",
                   (int)App_Spectrum_BinToHz(bin_start),
                   (int)App_Spectrum_BinToHz(bin_end));
    lv_label_set_text(s_lbl_band, buf);
    lv_obj_add_style(s_lbl_band, &g_ui_styles.label_unit, 0); /* 小号单位字体 */
    lv_obj_set_style_text_color(s_lbl_band, UI_COLOR_ACCENT, 0); /* 青色标题 */

    /* 低豱频 slider：控制频段下限 bin */
    s_slider_lo = lv_slider_create(s_panel_root);
    lv_obj_set_width(s_slider_lo, (lv_coord_t)(s_cvs_w)); /* 与 canvas 同宽 */
    lv_obj_set_height(s_slider_lo, 10);                    /* 10px 高 */
    lv_slider_set_range(s_slider_lo, 1, (int32_t)SRP_FREQ_BIN_END); /* bin 范围 [1, max] */
    lv_slider_set_value(s_slider_lo, (int32_t)bin_start, LV_ANIM_OFF); /* 展示当前频段起始 */
    lv_obj_set_style_bg_color(s_slider_lo, UI_COLOR_ACCENT, LV_PART_INDICATOR); /* 进度条青色 */
    lv_obj_set_style_bg_color(s_slider_lo, UI_COLOR_BG_MAIN, LV_PART_MAIN);     /* 背景深色 */
    lv_obj_add_event_cb(s_slider_lo, s_slider_lo_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 高豱频 slider：控制频段上限 bin */
    s_slider_hi = lv_slider_create(s_panel_root);
    lv_obj_set_width(s_slider_hi, (lv_coord_t)(s_cvs_w));
    lv_obj_set_height(s_slider_hi, 10);
    lv_slider_set_range(s_slider_hi, 1, (int32_t)SRP_FREQ_BIN_END);
    lv_slider_set_value(s_slider_hi, (int32_t)bin_end, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_hi, UI_COLOR_WARNING, LV_PART_INDICATOR); /* 高层黄色 */
    lv_obj_set_style_bg_color(s_slider_hi, UI_COLOR_BG_MAIN, LV_PART_MAIN);
    lv_obj_add_event_cb(s_slider_hi, s_slider_hi_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 峰値标签：显示当前帧最大能量 bin 对应的 Hz */
    s_lbl_peak = lv_label_create(s_panel_root);
    lv_label_set_text(s_lbl_peak, "Pk: --"); /* 初始掠置文本 */
    lv_obj_add_style(s_lbl_peak, &g_ui_styles.label_unit, 0);

    /* 预设按钮行：F/V/U/L 四个按钮均分排列 */
    {
        lv_obj_t *row = lv_obj_create(s_panel_root);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, (lv_coord_t)(s_cvs_w), LV_SIZE_CONTENT); /* 高度自适应 */
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);             /* 水平排列按钮 */
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); /* 等间距横向排列 */
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_column(row, 2, 0); /* 按钮之间 2px 间距 */

        for (i = 0u; i < SPEC_PRESET_COUNT; i++)
        {
            lv_obj_t *btn = lv_btn_create(row);
            lv_obj_add_style(btn, &g_ui_styles.btn, 0);
            lv_obj_add_style(btn, &g_ui_styles.btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 32, 18); /* 32×18 px，小型按钮 */
            {
                lv_obj_t *lbl = lv_label_create(btn);
                /* 缩寫标签：F=Full，V=Voice，U=Ultra，L=Low */
                lv_label_set_text_static(lbl,
                    (i == 0u) ? "F" : (i == 1u) ? "V" : (i == 2u) ? "U" : "L");
                lv_obj_center(lbl);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            }
            /* 将按钮索引 i 通过 user_data 传递给回调 */
            lv_obj_add_event_cb(btn, s_preset_btn_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        }
    }

    return s_panel_root;
}

/** @brief 更新频谱面板显示（每帧调用一次）
 * @param  frame  频谱帧指针，含 magnitude[bin_count] 和 active_band
 * @details 绘制流程：
 *   1. canvas 全黑清养
 *   2. 对每个 bin 的 magnitude 做 EMA 平滑：
 *      上升快（attack=0.4），下降慢（decay=0.08）——目的是拖尾效果
 *   3. 查找平滑后的峰值 bin
 *   4. 参考幅度自适应：s_ref_mag 跟随峰值快升慢降
 *   5. 展示活动频段背景高亮区（深蓝色）
 *   6. 逐 bin 绘制水平柱：
 *      - 活动频段内：黄色（#FFDD00）
 *      - 活动频段外：青色（#00BBCC）
 *   7. 峰值标记线（红色，宽 2px，12px 长）
 *   8. 更新峰值标签（kHz 或 Hz）
 */
void App_UiSpecPanel_Update(const App_SpectrumFrame_t *frame)
{
    uint16_t bin_count;                    /* 可用频谱 bin 数 */
    uint16_t min_bin;                      /* 跳过 bin 0（直流山谑） */
    uint16_t peak_bin = 1u;                /* 当帧峰值 bin 索引 */
    float peak_mag = SPEC_MIN_MAG;         /* 当帧 EMA 峰值幅度 */
    float ref_mag;                         /* 本帧使用的参考幅度（自适应）*/
    uint16_t i;                            /* 循环索引 */
    lv_draw_rect_dsc_t rect_dsc;           /* LVGL 矩形绘制描述器 */
    char buf[24];                          /* 标签文本缓冲 */
    App_FreqBand_t active_band;            /* 当帧活动频段（用于高亮和柱颜色）*/
    uint16_t band_y0, band_y1, y0tmp, y1tmp; /* 频段背景高亮 Y 坐标 */

    if (s_canvas == NULL)
    {
        return; /* canvas 未创建，跳过 */
    }

    /* 清空 canvas：将全劣屏买为黑色 */
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);

    if (frame == NULL || frame->bin_count == 0u)
    {
        return; /* 无频谱数据，显示全黑 canvas */
    }

    bin_count = frame->bin_count; /* 当帧实际 bin 数 */
    if (bin_count > APP_SPECTRUM_BIN_COUNT)
    {
        bin_count = APP_SPECTRUM_BIN_COUNT; /* 防越界 */
    }
    min_bin = 1u;            /* 跳过 bin 0（DC 分量，对声音无意义）*/
    active_band = frame->active_band; /* 当帧手设应用的频段 */

    /* === EMA 平滑（指数移动平均）=== */
    for (i = 0u; i < bin_count; i++)
    {
        float mag = frame->magnitude[i]; /* 当帧该 bin 的幅度 */
        float alpha;                     /* EMA 平滑系数（上升/下降分开）*/

        if ((!isfinite(mag)) || (mag < 0.0f))
        {
            mag = 0.0f; /* 清周非数/负数（FFT 弗弦异常时小概率出现）*/
        }
        if (s_ema_valid == 0u)
        {
            s_ema[i] = mag; /* 首帧直接赋值，无需平滑 */
        }
        else
        {
            /* EMA：上升用 ATTACK（急速跟随声音），下降用 DECAY（修饰跨喉拖尾）*/ 
            alpha = (mag >= s_ema[i]) ? SPEC_EMA_ATTACK : SPEC_EMA_DECAY;
            s_ema[i] += alpha * (mag - s_ema[i]); /* EMA: s = s + alpha*(x-s) */
        }
    }
    s_ema_valid = 1u; /* EMA 已有效，下帧开始平滑 */

    /* === 查找峰值 bin === */
    for (i = min_bin; i < bin_count; i++)
    {
        if (s_ema[i] > peak_mag)
        {
            peak_mag = s_ema[i]; /* 更新峰值 */
            peak_bin = i;        /* 记录峰值 bin */
        }
    }

    /* === 参考幅度自适应（动态范围调整）===
     * 峰值上升时快速跟随（系数 0.3），下降时缓慢（系数 0.02），
     * 避免在安静片段时放大噪音导致动态范围失真 */
    if (peak_mag > s_ref_mag)
    {
        s_ref_mag += 0.3f * (peak_mag - s_ref_mag); /* 快速上升以跟踪突发声音 */
    }
    else
    {
        s_ref_mag += 0.02f * (peak_mag - s_ref_mag); /* 缓慢下降，避免全幅闪烁 */
    }
    if (s_ref_mag < SPEC_MIN_MAG)
    {
        s_ref_mag = SPEC_MIN_MAG; /* 防止 ref_mag 趋近零导致 log 为 -inf */
    }
    ref_mag = s_ref_mag; /* 本帧使用的参考幅度（快照，避免循环中被修改）*/

    /* === 绘制活动频段背景高亮 ===
     * 注意：Y 轴上低 bin 在下，高 bin 在上，y0tmp 可能大于 y1tmp，需排序 */
    y0tmp = s_bin_to_y(active_band.start_bin, bin_count); /* start_bin 对应的 Y */
    y1tmp = s_bin_to_y(active_band.end_bin,   bin_count); /* end_bin 对应的 Y */
    band_y0 = (y0tmp < y1tmp) ? y0tmp : y1tmp; /* 取较小 Y（canvas 上方）*/
    band_y1 = (y0tmp > y1tmp) ? y0tmp : y1tmp; /* 取较大 Y（canvas 下方）*/

    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x001030); /* 深蓝色背景高亮 */
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = 0;
    rect_dsc.border_width = 0;
    lv_canvas_draw_rect(s_canvas, SPEC_MARGIN, band_y0,
                        s_plot_w(), (lv_coord_t)(band_y1 - band_y0 + 1u), /* +1 防高度为0 */
                        &rect_dsc);

    /* === 绘制频谱柱（每 bin 一根水平条）=== */
    for (i = min_bin; i < bin_count; i++)
    {
        float rel_db; /* 相对参考幅度的 dB 值 */
        uint16_t bar_len; /* 柱的像素宽度 */
        uint16_t y_center = s_bin_to_y(i, bin_count);  /* 当前 bin 对应的 Y 中心 */
        /* 下一个 bin 的 Y，用于计算柱高（相邻 bin 间等分）*/
        uint16_t y_next = (i + 1u < bin_count) ? s_bin_to_y((uint16_t)(i + 1u), bin_count) : (uint16_t)SPEC_MARGIN;
        uint16_t bar_y0_px, bar_y1_px; /* 柱的 Y 起止像素 */
        uint8_t in_band;               /* 1=该 bin 在活动频段内 */

        /* 20·log10(mag / ref) 得到相对 dB；fmaxf 防 mag=0 导致 log 为 -inf */
        rel_db = 20.0f * log10f(fmaxf(s_ema[i], SPEC_MIN_MAG) / ref_mag);
        bar_len = s_db_to_barlen(rel_db); /* dB 映射到像素长度 */
        if (bar_len == 0u)
        {
            continue; /* 幅度极低，跳过绘制（避免 0 宽矩形）*/
        }

        /* Y 坐标：取当前 bin 与下一 bin Y 的算术中点到当前 Y */
        bar_y0_px = (y_center + y_next) / 2u;
        bar_y1_px = y_center;
        if (bar_y0_px > bar_y1_px) /* 确保 y0 < y1 */
        {
            uint16_t t = bar_y0_px;
            bar_y0_px = bar_y1_px;
            bar_y1_px = t;
        }
        if (bar_y1_px <= bar_y0_px)
        {
            bar_y1_px = (uint16_t)(bar_y0_px + 1u); /* 最少 1px 高度 */
        }

        /* 频段内黄色，频段外青色 */
        in_band = (i >= active_band.start_bin && i <= active_band.end_bin) ? 1u : 0u;

        rect_dsc.bg_color = in_band ? lv_color_hex(0xFFDD00) : lv_color_hex(0x00BBCC);
        rect_dsc.bg_opa = LV_OPA_COVER;
        lv_canvas_draw_rect(s_canvas,
                            (lv_coord_t)SPEC_MARGIN,   /* 柱从左侧边距开始 */
                            (lv_coord_t)bar_y0_px,
                            (lv_coord_t)bar_len,
                            (lv_coord_t)(bar_y1_px - bar_y0_px),
                            &rect_dsc);
    }

    /* === 峰值标记线（红色短横线，宽 2px，12px 长）=== */
    {
        uint16_t pk_y = s_bin_to_y(peak_bin, bin_count); /* 峰值 bin 的 Y */
        lv_point_t pts[2];

        pts[0].x = (lv_coord_t)SPEC_MARGIN;           /* 从左边距开始 */
        pts[0].y = (lv_coord_t)pk_y;
        pts[1].x = (lv_coord_t)(SPEC_MARGIN + 12u);   /* 向右 12px */
        pts[1].y = (lv_coord_t)pk_y;

        {
            lv_draw_line_dsc_t line_dsc;
            lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = lv_color_hex(0xFF3333); /* 红色峰值标记 */
            line_dsc.width = 2;                      /* 2px 粗 */
            lv_canvas_draw_line(s_canvas, pts, 2, &line_dsc);
        }
    }

    lv_obj_invalidate(s_canvas); /* 通知 LVGL 重绘 canvas 区域 */

    /* === 更新峰值标签 === */
    if (s_lbl_peak != NULL)
    {
        float pk_hz = App_Spectrum_BinToHz(peak_bin); /* bin 索引转 Hz */
        if (pk_hz >= 1000.0f)
        {
            /* 大于等于 1 kHz 时以 kHz 显示，保留一位小数 */
            (void)snprintf(buf, sizeof(buf), "Pk:%.1fk", (double)(pk_hz / 1000.0f));
        }
        else
        {
            /* 小于 1 kHz 时以整 Hz 显示 */
            (void)snprintf(buf, sizeof(buf), "Pk:%dHz", (int)pk_hz);
        }
        lv_label_set_text(s_lbl_peak, buf); /* 更新标签文本 */
    }
}

/**
 * @brief  应用频段预设，同步更新 RuntimeConfig、滑块和频段标签
 * @param  preset  预设索引（App_SpecPreset_t 枚举值）
 * @note   调用后 s_slider_lo/hi 的值同步为预设的 start_bin/end_bin，
 *         会触发 s_slider_lo_cb/s_slider_hi_cb 回调，但此处直接用 LV_ANIM_OFF
 *         不会产生视觉动画抖动。
 * @note   [改进] 此函数不会重置 EMA 和 s_ema_valid，切换预设后 EMA 历史值
 *         仍沿用上一个频段，可能导致前几帧显示异常，建议切换时清零 s_ema_valid。
 */
void App_UiSpecPanel_ApplyPreset(App_SpecPreset_t preset)
{
    char buf[32]; /* 临时字符串缓冲，用于格式化频段标签 */

    if (preset >= SPEC_PRESET_COUNT) /* 越界保护，枚举值通常不会越界但防御性检查 */
    {
        return;
    }

    /* 将预设的 start/end bin 写入 RuntimeConfig，供 Audio 处理任务读取 */
    App_RuntimeConfig_SetFreqBand(s_presets[preset].start_bin,
                                  s_presets[preset].end_bin);

    /* 同步低频滑块到预设的 start_bin（不触发动画，避免视觉混乱）*/
    if (s_slider_lo != NULL)
    {
        lv_slider_set_value(s_slider_lo,
                            (int32_t)s_presets[preset].start_bin, LV_ANIM_OFF);
    }
    /* 同步高频滑块到预设的 end_bin */
    if (s_slider_hi != NULL)
    {
        lv_slider_set_value(s_slider_hi,
                            (int32_t)s_presets[preset].end_bin, LV_ANIM_OFF);
    }
    /* 更新频段名称标签，格式："{预设名} {起始Hz}-{终止Hz}Hz"，例如 "语音 300-3400Hz" */
    if (s_lbl_band != NULL)
    {
        (void)snprintf(buf, sizeof(buf), "%s %d-%dHz",
                       s_preset_names[preset],                              /* 预设名称字符串 */
                       (int)App_Spectrum_BinToHz(s_presets[preset].start_bin), /* 起始频率 Hz */
                       (int)App_Spectrum_BinToHz(s_presets[preset].end_bin));   /* 终止频率 Hz */
        lv_label_set_text(s_lbl_band, buf);
    }
    /* [注意] 此处不刷新 canvas，下一帧 App_UiSpecPanel_Update() 调用时会自动重绘 */
}

#endif /* APP_LVGL_ENABLE */
