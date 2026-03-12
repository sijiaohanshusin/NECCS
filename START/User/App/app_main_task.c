/**
 * @file    app_main_task.c
 * @brief   FreeRTOS 任务调度实现
 * @details 实现音频处理任务与 UI 显示任务，负责从 DMA 事件到声源显示的整条运行链路。
 *
 * 任务架构：
 * - `Audio_Pipeline_Task`: 解交织 -> FFT -> SRP-PHAT -> 位置结果输出。
 * - `UI_Display_Task`: 拉取位置与可视化快照 -> 渲染 -> 刷新 LCD。
 *
 * 数据流：
 * - SAI DMA ISR -> `xAudioFrameQueue` -> `Audio_Pipeline_Task`
 * - `Audio_Pipeline_Task` -> `xPositionQueue` -> `UI_Display_Task`
 *
 * 队列策略：
 * - 两个队列长度均为 1，仅保留最新帧，避免累积延迟。
 * - ISR 与任务端统一使用覆盖写入策略，优先保证实时性。
 */

#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_display.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "LCD/dma2d_accel.h"
#include "usart.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 外部变量 (External Variables)
 * ============================================================================ */

/** @brief 调试计数：每处理一帧音频递增一次 */
extern int16_t found_val;

/* ============================================================================
 * FreeRTOS 句柄 (FreeRTOS Handles)
 * ============================================================================ */

/** @brief 音频处理任务句柄 */
TaskHandle_t xAudioPipelineTaskHandle = NULL;

/** @brief UI 显示任务句柄 */
TaskHandle_t xUITaskHandle = NULL;

/** @brief 音频帧事件队列句柄 */
QueueHandle_t xAudioFrameQueue = NULL;

/** @brief 声源位置队列句柄 */
QueueHandle_t xPositionQueue = NULL;

/* ============================================================================
 * 运行时诊断计数器 (Runtime Diagnostic Counters)
 * ============================================================================ */

/** @brief 音频帧事件序号跳变计数（用于估算丢帧） */
volatile uint32_t g_audio_both_flags_count = 0u;

/** @brief 音频任务收到非法 half_id 的次数 */
volatile uint32_t g_audio_no_flag_count = 0u;

/** @brief UI 渲染帧计数 */
volatile uint32_t g_ui_render_count = 0u;

/** @brief UI 成功接收位置队列数据次数 */
volatile uint32_t g_ui_queue_rx_count = 0u;

/** @brief UI 轮询位置队列未取到数据次数 */
volatile uint32_t g_ui_queue_timeout_count = 0u;

volatile uint32_t g_ui_cli_rx_ok_count = 0u;
volatile uint32_t g_ui_cli_rx_err_count = 0u;
volatile uint8_t g_ui_cli_rx_alive = 0u;

/* ============================================================================
 * 调试配置 (Debug Configuration)
 * ============================================================================ */

/* #define DEBUG_ENABLE */  /**< 启用 VOFA 调试输出 */
#define DEBUG_THROTTLE_FRAMES   20u  /**< 调试输出节流：每 N 帧输出一次 */
#define DEBUG_MODE              3    /**< 调试模式：0=RMS, 1=FFT, 3=SRP */
#define DEBUG_SPECTRUM_CHANNEL  0u   /**< FFT 调试输出通道 */

/* ============================================================================
 * UI 刷新参数 (UI Refresh Parameters)
 * ============================================================================ */

#define UI_RETRY_INIT_MS         1000u  /**< UI 初始化失败后的重试周期 (ms) */
#define UI_DEBUG_LOG             0u     /**< UI 调试日志开关 */
#define UI_CLI_ENABLE            1u
#define UI_CLI_LINE_MAX          96u
#define UI_CLI_RX_DRAIN_MAX      256u
#define UI_CLI_RX_RING_SIZE      1024u
#define UI_FPS_MIN               5u
#define UI_FPS_MAX               30u
#define UI_FPS_DEFAULT           20u
#define AUDIO_ALGO_DECIM_MIN     1u
#define AUDIO_ALGO_DECIM_MAX     8u
#define AUDIO_ALGO_DECIM_DEFAULT 1u
#define PERF_RING_SAMPLES        64u
#define PERF_RATE_PERIOD_MS      1000u

/* ============================================================================
 * 任务优先级 (Task Priorities)
 * ============================================================================ */

#define APP_AUDIO_TASK_PRIO     4u  /**< 音频任务优先级 */
#define APP_UI_TASK_PRIO        4u  /**< UI 任务优先级 */

/* ============================================================================
 * 初始化函数 (Initialization Functions)
 * ============================================================================ */

/**
 * @brief   应用任务与队列初始化
 * @details 创建音频队列、位置队列、音频任务与 UI 任务，并同步运行时配置。
 *
 * 初始化顺序：
 * 1. 初始化渲染后端与性能统计模块。
 * 2. 创建长度为 1 的音频事件队列和位置队列。
 * 3. 创建 `Audio_Pipeline_Task` 与 `UI_Display_Task`。
 *
 * @note 队列长度为 1 是实时策略设计：旧帧可丢，优先处理最新帧。
 */
/* ============================================================================
 * UI CLI (Runtime Tuning via UART)
 * ============================================================================ */

/* ============================================================================
 * Runtime Knobs
 * ============================================================================ */

typedef struct
{
    uint8_t (*is_ready)(void);
    void (*init)(void);
    void (*render)(const Sound_Pos_t *pos,
                   const SRP_VisFrame_t *vis_frame,
                   uint32_t frame_seq,
                   uint8_t sai_dma_active);
} App_UiRendererOps_t;

static App_Runtime_Config_t s_runtime_cfg = {
    UI_FPS_DEFAULT,
    AUDIO_ALGO_DECIM_DEFAULT,
    0u,
    {0u, 0u, 0u},
    APP_RUNTIME_DISP_MODE_BALANCED,
    {
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
        (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_RUNTIME_DISP_INTERP_BILINEAR : APP_RUNTIME_DISP_INTERP_NEAREST,
        APP_RUNTIME_DISP_NORM_FAST,
        APP_DISPLAY_TEXT_REFRESH_DIV,
        APP_DISPLAY_BLIT_ROWS_MAX
    }
};

static uint8_t s_ui_legacy_is_ready(void);
static void s_ui_legacy_init(void);
static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active);

static const App_UiRendererOps_t s_ui_renderer_legacy_ops = {
    s_ui_legacy_is_ready,
    s_ui_legacy_init,
    s_ui_legacy_render
};

static const App_UiRendererOps_t *s_ui_renderer = &s_ui_renderer_legacy_ops;
static volatile App_UiRenderBackend_t s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;

/* ============================================================================
 * Performance Profiler
 * ============================================================================ */

typedef struct
{
    uint64_t total_cycles;
    uint32_t sample_count;
    uint32_t max_cycles;
    uint32_t ring[PERF_RING_SAMPLES];
    uint32_t ring_count;
    uint32_t ring_head;
} App_Perf_SectionStat_t;

static App_Perf_SectionStat_t s_perf_stats[APP_PERF_SEC_COUNT];
static const char *s_perf_section_names[APP_PERF_SEC_COUNT] = {
    "audio_total",
    "audio_deint",
    "audio_fft",
    "audio_srp",
    "ui_loop",
    "ui_snapshot",
    "ui_render",
    "disp_prepare",
    "disp_norm",
    "disp_render",
    "disp_overlay",
    "disp_commit"
};

static volatile uint8_t s_perf_enabled = 0u;
static volatile uint8_t s_perf_dwt_ready = 0u;
static volatile uint32_t s_perf_audio_proc_count = 0u;
static volatile uint32_t s_perf_ui_loop_count = 0u;

static uint32_t s_perf_last_tick = 0u;
static uint32_t s_perf_last_audio_isr = 0u;
static uint32_t s_perf_last_audio_proc = 0u;
static uint32_t s_perf_last_ui_loop = 0u;
static uint32_t s_perf_last_commit = 0u;
static uint32_t s_perf_last_swap = 0u;

static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
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

static App_Runtime_DisplayMode_t s_runtime_mode_from_display(App_Display_Mode_t mode)
{
    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:
            return APP_RUNTIME_DISP_MODE_FAST;
        case APP_DISPLAY_MODE_CLEAN:
            return APP_RUNTIME_DISP_MODE_CLEAN;
        case APP_DISPLAY_MODE_BALANCED:
        default:
            return APP_RUNTIME_DISP_MODE_BALANCED;
    }
}

static App_Display_Mode_t s_runtime_mode_to_display(App_Runtime_DisplayMode_t mode)
{
    switch (mode)
    {
        case APP_RUNTIME_DISP_MODE_FAST:
            return APP_DISPLAY_MODE_FAST;
        case APP_RUNTIME_DISP_MODE_CLEAN:
            return APP_DISPLAY_MODE_CLEAN;
        case APP_RUNTIME_DISP_MODE_BALANCED:
        default:
            return APP_DISPLAY_MODE_BALANCED;
    }
}

static void s_runtime_displaycfg_from_display(const App_Display_RuntimeCfg_t *src,
                                              App_Runtime_DisplayCfg_t *dst)
{
    if ((src == NULL) || (dst == NULL))
    {
        return;
    }

    dst->ema_attack = src->ema_attack;
    dst->ema_decay = src->ema_decay;
    dst->db_floor = src->db_floor;
    dst->fine_gain = src->fine_gain;
    dst->gamma = src->gamma;
    dst->noise_gate_ratio = src->noise_gate_ratio;
    dst->noise_adapt_gain = src->noise_adapt_gain;
    dst->smooth_passes = src->smooth_passes;
    dst->fine_fusion_enable = src->fine_fusion_enable;
    dst->draw_coarse_grid = src->draw_coarse_grid;
    dst->interp_mode = (src->interp_mode == APP_DISPLAY_INTERP_BILINEAR)
                           ? APP_RUNTIME_DISP_INTERP_BILINEAR
                           : APP_RUNTIME_DISP_INTERP_NEAREST;
    dst->norm_mode = (src->norm_mode == APP_DISPLAY_NORM_FULL)
                         ? APP_RUNTIME_DISP_NORM_FULL
                         : APP_RUNTIME_DISP_NORM_FAST;
    dst->text_refresh_div = src->text_refresh_div;
    dst->blit_rows = src->blit_rows;
}

static void s_runtime_displaycfg_to_display(const App_Runtime_DisplayCfg_t *src,
                                            App_Display_RuntimeCfg_t *dst)
{
    if ((src == NULL) || (dst == NULL))
    {
        return;
    }

    dst->ema_attack = src->ema_attack;
    dst->ema_decay = src->ema_decay;
    dst->db_floor = src->db_floor;
    dst->fine_gain = src->fine_gain;
    dst->gamma = src->gamma;
    dst->noise_gate_ratio = src->noise_gate_ratio;
    dst->noise_adapt_gain = src->noise_adapt_gain;
    dst->smooth_passes = src->smooth_passes;
    dst->fine_fusion_enable = src->fine_fusion_enable;
    dst->draw_coarse_grid = src->draw_coarse_grid;
    dst->interp_mode = (src->interp_mode == APP_RUNTIME_DISP_INTERP_BILINEAR)
                           ? APP_DISPLAY_INTERP_BILINEAR
                           : APP_DISPLAY_INTERP_NEAREST;
    dst->norm_mode = (src->norm_mode == APP_RUNTIME_DISP_NORM_FULL)
                         ? APP_DISPLAY_NORM_FULL
                         : APP_DISPLAY_NORM_FAST;
    dst->text_refresh_div = src->text_refresh_div;
    dst->blit_rows = src->blit_rows;
}

static void s_runtime_sync_from_display(void)
{
    App_Display_RuntimeCfg_t display_cfg;
    App_Display_Mode_t display_mode;

    App_Display_GetConfig(&display_cfg);
    display_mode = App_Display_GetMode();

    taskENTER_CRITICAL();
    s_runtime_cfg.display_mode = s_runtime_mode_from_display(display_mode);
    s_runtime_displaycfg_from_display(&display_cfg, &s_runtime_cfg.display_cfg);
    taskEXIT_CRITICAL();
}

void App_RuntimeConfig_Get(App_Runtime_Config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    *cfg = s_runtime_cfg;
    taskEXIT_CRITICAL();
}

void App_RuntimeConfig_SetUiTargetFps(uint32_t fps)
{
    fps = s_clamp_u32(fps, UI_FPS_MIN, UI_FPS_MAX);
    taskENTER_CRITICAL();
    s_runtime_cfg.ui_target_fps = fps;
    taskEXIT_CRITICAL();
}

uint32_t App_RuntimeConfig_GetUiTargetFps(void)
{
    uint32_t fps;
    taskENTER_CRITICAL();
    fps = s_runtime_cfg.ui_target_fps;
    taskEXIT_CRITICAL();
    return fps;
}

void App_RuntimeConfig_SetAudioAlgoDecim(uint32_t decim)
{
    decim = s_clamp_u32(decim, AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX);
    taskENTER_CRITICAL();
    s_runtime_cfg.audio_algo_decim = decim;
    taskEXIT_CRITICAL();
}

uint32_t App_RuntimeConfig_GetAudioAlgoDecim(void)
{
    uint32_t decim;
    taskENTER_CRITICAL();
    decim = s_runtime_cfg.audio_algo_decim;
    taskEXIT_CRITICAL();
    return decim;
}

void App_RuntimeConfig_SetPerfEnabled(uint8_t enable)
{
    App_Perf_SetEnabled(enable);
    taskENTER_CRITICAL();
    s_runtime_cfg.perf_enabled = (App_Perf_IsEnabled() != 0u) ? 1u : 0u;
    taskEXIT_CRITICAL();
}

uint8_t App_RuntimeConfig_GetPerfEnabled(void)
{
    uint8_t enabled;
    taskENTER_CRITICAL();
    enabled = s_runtime_cfg.perf_enabled;
    taskEXIT_CRITICAL();
    return enabled;
}

void App_RuntimeConfig_SetDisplayMode(App_Runtime_DisplayMode_t mode)
{
    App_Display_Mode_t display_mode = s_runtime_mode_to_display(mode);
    App_Display_SetMode(display_mode);
    s_runtime_sync_from_display();
}

App_Runtime_DisplayMode_t App_RuntimeConfig_GetDisplayMode(void)
{
    App_Runtime_DisplayMode_t mode;
    taskENTER_CRITICAL();
    mode = s_runtime_cfg.display_mode;
    taskEXIT_CRITICAL();
    return mode;
}

void App_RuntimeConfig_SetDisplayCfg(const App_Runtime_DisplayCfg_t *cfg)
{
    App_Display_RuntimeCfg_t display_cfg;

    if (cfg == NULL)
    {
        return;
    }

    s_runtime_displaycfg_to_display(cfg, &display_cfg);
    App_Display_SetConfig(&display_cfg);
    s_runtime_sync_from_display();
}

void App_RuntimeConfig_GetDisplayCfg(App_Runtime_DisplayCfg_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    *cfg = s_runtime_cfg.display_cfg;
    taskEXIT_CRITICAL();
}

void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend)
{
    taskENTER_CRITICAL();
    switch (backend)
    {
        case APP_UI_RENDER_BACKEND_LEGACY:
        default:
            s_ui_renderer = &s_ui_renderer_legacy_ops;
            s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;
            break;
    }
    taskEXIT_CRITICAL();
}

App_UiRenderBackend_t App_UiRenderer_GetBackend(void)
{
    App_UiRenderBackend_t backend;
    taskENTER_CRITICAL();
    backend = s_ui_backend;
    taskEXIT_CRITICAL();
    return backend;
}

static uint8_t s_ui_legacy_is_ready(void)
{
    return App_Display_IsReady();
}

static void s_ui_legacy_init(void)
{
    App_Display_Init();
}

static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active)
{
    App_Display_Render(pos, vis_frame, frame_seq, sai_dma_active);
}

static int s_u32_cmp(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    if (va < vb)
    {
        return -1;
    }
    if (va > vb)
    {
        return 1;
    }
    return 0;
}

static uint8_t s_perf_enable_dwt(void)
{
    if (s_perf_dwt_ready != 0u)
    {
        return 1u;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55u;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u)
    {
        return 0u;
    }

    s_perf_dwt_ready = 1u;
    return 1u;
}

static void s_perf_reset_rate_baseline(void)
{
    s_perf_last_tick = xTaskGetTickCount();
    s_perf_last_audio_isr = g_audio_frame_seq_isr;
    s_perf_last_audio_proc = s_perf_audio_proc_count;
    s_perf_last_ui_loop = s_perf_ui_loop_count;
    s_perf_last_commit = g_ltdc_swap_pending_count;
    s_perf_last_swap = g_ltdc_swap_count;
}

void App_Perf_Init(void)
{
    App_Perf_Reset();
    s_perf_enabled = 0u;
    (void)s_perf_enable_dwt();
}

void App_Perf_SetEnabled(uint8_t enable)
{
    if (enable != 0u)
    {
        if (s_perf_enable_dwt() == 0u)
        {
            s_perf_enabled = 0u;
            printf("perf: DWT unavailable\r\n");
            return;
        }
        s_perf_enabled = 1u;
        s_perf_reset_rate_baseline();
    }
    else
    {
        s_perf_enabled = 0u;
    }
}

uint8_t App_Perf_IsEnabled(void)
{
    return s_perf_enabled;
}

void App_Perf_Reset(void)
{
    memset(s_perf_stats, 0, sizeof(s_perf_stats));
    s_perf_audio_proc_count = 0u;
    s_perf_ui_loop_count = 0u;
    s_perf_reset_rate_baseline();
}

uint32_t App_Perf_BeginCycles(void)
{
    if ((s_perf_enabled == 0u) || (s_perf_dwt_ready == 0u))
    {
        return 0u;
    }
    return DWT->CYCCNT;
}

void App_Perf_EndCycles(App_Perf_Section_t section, uint32_t start_cycles)
{
    App_Perf_SectionStat_t *st;
    uint32_t now;
    uint32_t delta;

    if ((s_perf_enabled == 0u) ||
        (s_perf_dwt_ready == 0u) ||
        (section >= APP_PERF_SEC_COUNT))
    {
        return;
    }

    now = DWT->CYCCNT;
    delta = now - start_cycles;
    st = &s_perf_stats[section];
    st->total_cycles += (uint64_t)delta;
    st->sample_count++;
    if (delta > st->max_cycles)
    {
        st->max_cycles = delta;
    }
    st->ring[st->ring_head] = delta;
    st->ring_head = (st->ring_head + 1u) % PERF_RING_SAMPLES;
    if (st->ring_count < PERF_RING_SAMPLES)
    {
        st->ring_count++;
    }
}

void App_Perf_CountAudioProc(void)
{
    s_perf_audio_proc_count++;
}

void App_Perf_CountUiLoop(void)
{
    s_perf_ui_loop_count++;
}

void App_Perf_MaybePrintRates(void)
{
    uint32_t now_tick;
    uint32_t elapsed_tick;
    uint32_t elapsed_ms;
    uint32_t audio_isr;
    uint32_t audio_proc;
    uint32_t ui_loop;
    uint32_t commit;
    uint32_t swap;
    double scale;

    if (s_perf_enabled == 0u)
    {
        return;
    }

    now_tick = xTaskGetTickCount();
    elapsed_tick = now_tick - s_perf_last_tick;
    if (elapsed_tick < pdMS_TO_TICKS(PERF_RATE_PERIOD_MS))
    {
        return;
    }

    elapsed_ms = elapsed_tick * portTICK_PERIOD_MS;
    if (elapsed_ms == 0u)
    {
        elapsed_ms = 1u;
    }

    audio_isr = g_audio_frame_seq_isr;
    audio_proc = s_perf_audio_proc_count;
    ui_loop = s_perf_ui_loop_count;
    commit = g_ltdc_swap_pending_count;
    swap = g_ltdc_swap_count;

    scale = 1000.0 / (double)elapsed_ms;
    printf("perf rate isr=%.1f proc=%.1f ui=%.1f commit=%.1f swap=%.1f\r\n",
           (double)(audio_isr - s_perf_last_audio_isr) * scale,
           (double)(audio_proc - s_perf_last_audio_proc) * scale,
           (double)(ui_loop - s_perf_last_ui_loop) * scale,
           (double)(commit - s_perf_last_commit) * scale,
           (double)(swap - s_perf_last_swap) * scale);

    s_perf_last_tick = now_tick;
    s_perf_last_audio_isr = audio_isr;
    s_perf_last_audio_proc = audio_proc;
    s_perf_last_ui_loop = ui_loop;
    s_perf_last_commit = commit;
    s_perf_last_swap = swap;
}

void App_Perf_Dump(void)
{
    uint32_t i;
    uint32_t core_hz = SystemCoreClock;

    if (core_hz == 0u)
    {
        core_hz = 480000000u;
    }

    printf("perf cfg enabled=%u dwt=%u uifps=%lu decim=%lu core=%lu\r\n",
           (unsigned int)s_perf_enabled,
           (unsigned int)s_perf_dwt_ready,
           (unsigned long)App_RuntimeConfig_GetUiTargetFps(),
           (unsigned long)App_RuntimeConfig_GetAudioAlgoDecim(),
           (unsigned long)core_hz);

    for (i = 0u; i < APP_PERF_SEC_COUNT; i++)
    {
        App_Perf_SectionStat_t *st = &s_perf_stats[i];
        uint32_t n = st->sample_count;
        uint32_t max_cycles = st->max_cycles;
        uint32_t p95_cycles = 0u;
        double avg_cycles;
        double avg_ms;
        double p95_ms;
        double max_ms;

        if (n == 0u)
        {
            continue;
        }

        if (st->ring_count != 0u)
        {
            uint32_t tmp[PERF_RING_SAMPLES];
            uint32_t j;
            uint32_t p95_rank;

            for (j = 0u; j < st->ring_count; j++)
            {
                uint32_t idx = (st->ring_head + PERF_RING_SAMPLES - st->ring_count + j) % PERF_RING_SAMPLES;
                tmp[j] = st->ring[idx];
            }
            qsort(tmp, st->ring_count, sizeof(uint32_t), s_u32_cmp);
            p95_rank = (st->ring_count * 95u + 99u) / 100u;
            if (p95_rank == 0u)
            {
                p95_rank = 1u;
            }
            if (p95_rank > st->ring_count)
            {
                p95_rank = st->ring_count;
            }
            p95_cycles = tmp[p95_rank - 1u];
        }

        avg_cycles = (double)st->total_cycles / (double)n;
        avg_ms = (avg_cycles * 1000.0) / (double)core_hz;
        p95_ms = ((double)p95_cycles * 1000.0) / (double)core_hz;
        max_ms = ((double)max_cycles * 1000.0) / (double)core_hz;

        printf("perf %-12s n=%lu avg=%.3fms p95=%.3fms max=%.3fms\r\n",
               s_perf_section_names[i],
               (unsigned long)n,
               avg_ms,
               p95_ms,
               max_ms);
    }
}

static uint32_t s_ui_period_ticks(void)
{
    uint32_t fps = s_clamp_u32(App_RuntimeConfig_GetUiTargetFps(), UI_FPS_MIN, UI_FPS_MAX);
    uint32_t period_ms = (1000u + (fps / 2u)) / fps;
    TickType_t ticks = pdMS_TO_TICKS(period_ms);
    if (ticks == 0u)
    {
        ticks = 1u;
    }
    return (uint32_t)ticks;
}

#if (UI_CLI_ENABLE != 0u)
static int ui_cli_stricmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    if ((a == NULL) || (b == NULL))
    {
        return -1;
    }

    while ((*a != '\0') && (*b != '\0'))
    {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb)
        {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint8_t ui_cli_parse_float(const char *s, float *out)
{
    char *endptr;
    float v;

    if ((s == NULL) || (out == NULL))
    {
        return 0u;
    }

    while (isspace((unsigned char)*s) != 0)
    {
        s++;
    }
    if (*s == '\0')
    {
        return 0u;
    }

    v = strtof(s, &endptr);
    if (s == endptr)
    {
        return 0u;
    }

    while (isspace((unsigned char)*endptr) != 0)
    {
        endptr++;
    }
    if (*endptr != '\0')
    {
        return 0u;
    }

    *out = v;
    return 1u;
}

static uint8_t ui_cli_parse_u32(const char *s, uint32_t *out)
{
    char *endptr;
    unsigned long v;

    if ((s == NULL) || (out == NULL))
    {
        return 0u;
    }

    while (isspace((unsigned char)*s) != 0)
    {
        s++;
    }
    if (*s == '\0')
    {
        return 0u;
    }

    v = strtoul(s, &endptr, 10);
    if (s == endptr)
    {
        return 0u;
    }

    while (isspace((unsigned char)*endptr) != 0)
    {
        endptr++;
    }
    if (*endptr != '\0')
    {
        return 0u;
    }

    *out = (uint32_t)v;
    return 1u;
}

static volatile uint16_t s_ui_cli_rx_wr = 0u;
static volatile uint16_t s_ui_cli_rx_rd = 0u;
static volatile uint8_t s_ui_cli_rx_armed = 0u;
static volatile uint8_t s_ui_cli_rx_need_rearm = 1u;
static volatile uint32_t s_ui_cli_rx_drop_count = 0u;
static uint8_t s_ui_cli_rx_byte = 0u;
static uint8_t s_ui_cli_rx_ring[UI_CLI_RX_RING_SIZE];

static void ui_cli_ring_push_from_isr(uint8_t ch)
{
    uint16_t wr = s_ui_cli_rx_wr;
    uint16_t next = (uint16_t)(wr + 1u);

    if (next >= UI_CLI_RX_RING_SIZE)
    {
        next = 0u;
    }

    if (next == s_ui_cli_rx_rd)
    {
        s_ui_cli_rx_drop_count++;
        g_ui_cli_rx_err_count++;
        return;
    }

    s_ui_cli_rx_ring[wr] = ch;
    s_ui_cli_rx_wr = next;
    g_ui_cli_rx_ok_count++;
}

static uint8_t ui_cli_ring_pop(uint8_t *out)
{
    uint16_t rd;

    if (out == NULL)
    {
        return 0u;
    }

    taskENTER_CRITICAL();
    rd = s_ui_cli_rx_rd;
    if (rd == s_ui_cli_rx_wr)
    {
        taskEXIT_CRITICAL();
        return 0u;
    }

    *out = s_ui_cli_rx_ring[rd];
    rd = (uint16_t)(rd + 1u);
    if (rd >= UI_CLI_RX_RING_SIZE)
    {
        rd = 0u;
    }
    s_ui_cli_rx_rd = rd;
    taskEXIT_CRITICAL();
    return 1u;
}

static void ui_cli_uart_recover(void)
{
    if (HAL_UART_GetError(&huart1) == HAL_UART_ERROR_NONE)
    {
        s_ui_cli_rx_armed = 0u;
        return;
    }

    (void)HAL_UART_AbortReceive(&huart1);
    __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF);
    huart1.ErrorCode = HAL_UART_ERROR_NONE;
    s_ui_cli_rx_armed = 0u;
}

static void ui_cli_uart_kick_rx_it(void)
{
    HAL_StatusTypeDef st;

    if (s_ui_cli_rx_armed != 0u)
    {
        return;
    }

    if (s_ui_cli_rx_need_rearm != 0u)
    {
        ui_cli_uart_recover();
        s_ui_cli_rx_need_rearm = 0u;
    }

    st = HAL_UART_Receive_IT(&huart1, &s_ui_cli_rx_byte, 1u);
    if ((st == HAL_OK) || (st == HAL_BUSY))
    {
        s_ui_cli_rx_armed = 1u;
    }
    else
    {
        g_ui_cli_rx_err_count++;
    }
}

static void ui_cli_print_help(void)
{
    printf("\r\n");
    printf("cfg help\r\n");
    printf("cfg status\r\n");
    printf("cfg mode fast|balanced|clean\r\n");
    printf("cfg interp nearest|bilinear\r\n");
    printf("cfg contrast <db_floor>\r\n");
    printf("cfg gamma <0.5..2.5>\r\n");
    printf("cfg noise <0..0.6>\r\n");
    printf("cfg adapt <0..6>\r\n");
    printf("cfg smooth <0..3>\r\n");
    printf("cfg fine <0..3>\r\n");
    printf("cfg bilinear <0|1>\r\n");
    printf("cfg norm fast|full\r\n");
    printf("cfg textdiv <1..20>\r\n");
    printf("cfg blit <1..8>\r\n");
    printf("cfg uifps <5..30>\r\n");
    printf("cfg algodecim <1..8>\r\n");
    printf("cfg perf on|off|dump|reset\r\n");
    printf("cfg uart recover\r\n");
}

static void ui_cli_print_status(void)
{
    App_Runtime_DisplayCfg_t cfg;
    App_Runtime_DisplayMode_t mode;

    App_RuntimeConfig_GetDisplayCfg(&cfg);
    mode = App_RuntimeConfig_GetDisplayMode();

    printf("cfg mode=%s\r\n", App_Display_ModeName(s_runtime_mode_to_display(mode)));
    printf("cfg db=%.1f gamma=%.2f noise=%.3f adapt=%.2f\r\n",
           (double)cfg.db_floor,
           (double)cfg.gamma,
           (double)cfg.noise_gate_ratio,
           (double)cfg.noise_adapt_gain);
    printf("cfg smooth=%u fine=%.2f interp=%s norm=%s textdiv=%u blit=%u\r\n",
           (unsigned int)cfg.smooth_passes,
           (double)cfg.fine_gain,
           App_Display_InterpName((cfg.interp_mode == APP_RUNTIME_DISP_INTERP_BILINEAR)
                                      ? APP_DISPLAY_INTERP_BILINEAR
                                      : APP_DISPLAY_INTERP_NEAREST),
           App_Display_NormName((cfg.norm_mode == APP_RUNTIME_DISP_NORM_FULL)
                                    ? APP_DISPLAY_NORM_FULL
                                    : APP_DISPLAY_NORM_FAST),
           (unsigned int)cfg.text_refresh_div,
           (unsigned int)cfg.blit_rows);
    printf("cfg uifps=%lu algodecim=%lu perf=%s\r\n",
           (unsigned long)App_RuntimeConfig_GetUiTargetFps(),
           (unsigned long)App_RuntimeConfig_GetAudioAlgoDecim(),
           (App_RuntimeConfig_GetPerfEnabled() != 0u) ? "on" : "off");
    printf("dma2d tx=%lu timeout=%lu fallback=%lu qpk=%lu qov=%lu qerr=%lu\r\n",
           (unsigned long)g_ltdc_dma2d_transfer_count,
           (unsigned long)g_ltdc_dma2d_timeout_count,
           (unsigned long)g_ltdc_dma2d_sw_fallback_count,
           (unsigned long)g_dma2d_queue_depth_peak,
           (unsigned long)g_dma2d_queue_overflow_count,
           (unsigned long)g_dma2d_queue_error_count);
    printf("swap done=%lu pend_req=%lu err=%lu pending=%u\r\n",
           (unsigned long)g_ltdc_swap_count,
           (unsigned long)g_ltdc_swap_pending_count,
           (unsigned long)g_ltdc_swap_error_count,
           (unsigned int)ltdc_is_swap_pending());
    printf("cli rx_ok=%lu rx_err=%lu rx_drop=%lu alive=%u uart_err=0x%08lX baud=%lu\r\n",
           (unsigned long)g_ui_cli_rx_ok_count,
           (unsigned long)g_ui_cli_rx_err_count,
           (unsigned long)s_ui_cli_rx_drop_count,
           (unsigned int)g_ui_cli_rx_alive,
           (unsigned long)HAL_UART_GetError(&huart1),
           (unsigned long)huart1.Init.BaudRate);
}

static void ui_cli_apply_line(char *line)
{
    char *cursor;
    char *arg = NULL;
    char *tail;
    App_Runtime_DisplayCfg_t cfg;
    float fv;
    uint32_t uv;

    if (line == NULL)
    {
        return;
    }

    cursor = line;
    while (isspace((unsigned char)*cursor) != 0)
    {
        cursor++;
    }
    tail = cursor + strlen(cursor);
    while ((tail > cursor) && (isspace((unsigned char)tail[-1]) != 0))
    {
        *--tail = '\0';
    }
    if (*cursor == '\0')
    {
        return;
    }

    if ((ui_cli_stricmp(cursor, "help") == 0) ||
        (ui_cli_stricmp(cursor, "cfg help") == 0))
    {
        ui_cli_print_help();
        return;
    }
    if (ui_cli_stricmp(cursor, "cfg status") == 0)
    {
        ui_cli_print_status();
        return;
    }

    if ((tolower((unsigned char)cursor[0]) != 'c') ||
        (tolower((unsigned char)cursor[1]) != 'f') ||
        (tolower((unsigned char)cursor[2]) != 'g') ||
        (isspace((unsigned char)cursor[3]) == 0))
    {
        printf("CLI: unknown command, type 'cfg help'\r\n");
        return;
    }

    cursor += 3;
    while (isspace((unsigned char)*cursor) != 0)
    {
        cursor++;
    }
    if (*cursor == '\0')
    {
        ui_cli_print_help();
        return;
    }

    arg = cursor;
    while ((*arg != '\0') && (isspace((unsigned char)*arg) == 0))
    {
        arg++;
    }
    if (*arg != '\0')
    {
        *arg++ = '\0';
        while (isspace((unsigned char)*arg) != 0)
        {
            arg++;
        }
        if (*arg == '\0')
        {
            arg = NULL;
        }
    }
    else
    {
        arg = NULL;
    }

    if (ui_cli_stricmp(cursor, "help") == 0)
    {
        ui_cli_print_help();
        return;
    }
    if (ui_cli_stricmp(cursor, "status") == 0)
    {
        ui_cli_print_status();
        return;
    }

    if (ui_cli_stricmp(cursor, "mode") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg mode fast|balanced|clean\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "fast") == 0)
        {
            App_RuntimeConfig_SetDisplayMode(APP_RUNTIME_DISP_MODE_FAST);
        }
        else if ((ui_cli_stricmp(arg, "balanced") == 0) || (ui_cli_stricmp(arg, "bal") == 0))
        {
            App_RuntimeConfig_SetDisplayMode(APP_RUNTIME_DISP_MODE_BALANCED);
        }
        else if (ui_cli_stricmp(arg, "clean") == 0)
        {
            App_RuntimeConfig_SetDisplayMode(APP_RUNTIME_DISP_MODE_CLEAN);
        }
        else
        {
            printf("CLI: invalid mode\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "interp") == 0)
    {
        App_RuntimeConfig_GetDisplayCfg(&cfg);
        if (arg == NULL)
        {
            printf("CLI: cfg interp nearest|bilinear\r\n");
            return;
        }
        if ((ui_cli_stricmp(arg, "nearest") == 0) || (ui_cli_stricmp(arg, "near") == 0))
        {
            cfg.interp_mode = APP_RUNTIME_DISP_INTERP_NEAREST;
        }
        else if ((ui_cli_stricmp(arg, "bilinear") == 0) || (ui_cli_stricmp(arg, "bil") == 0))
        {
            cfg.interp_mode = APP_RUNTIME_DISP_INTERP_BILINEAR;
        }
        else
        {
            printf("CLI: cfg interp nearest|bilinear\r\n");
            return;
        }
        App_RuntimeConfig_SetDisplayCfg(&cfg);
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "norm") == 0)
    {
        App_RuntimeConfig_GetDisplayCfg(&cfg);
        if (arg == NULL)
        {
            printf("CLI: cfg norm fast|full\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "fast") == 0)
        {
            cfg.norm_mode = APP_RUNTIME_DISP_NORM_FAST;
        }
        else if (ui_cli_stricmp(arg, "full") == 0)
        {
            cfg.norm_mode = APP_RUNTIME_DISP_NORM_FULL;
        }
        else
        {
            printf("CLI: cfg norm fast|full\r\n");
            return;
        }
        App_RuntimeConfig_SetDisplayCfg(&cfg);
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "uifps") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg uifps <5..30>\r\n");
            return;
        }
        App_RuntimeConfig_SetUiTargetFps(uv);
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "algodecim") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg algodecim <1..8>\r\n");
            return;
        }
        App_RuntimeConfig_SetAudioAlgoDecim(uv);
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "perf") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg perf on|off|dump|reset\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "on") == 0)
        {
            App_RuntimeConfig_SetPerfEnabled(1u);
        }
        else if (ui_cli_stricmp(arg, "off") == 0)
        {
            App_RuntimeConfig_SetPerfEnabled(0u);
        }
        else if (ui_cli_stricmp(arg, "reset") == 0)
        {
            App_Perf_Reset();
        }
        else if (ui_cli_stricmp(arg, "dump") == 0)
        {
            App_Perf_Dump();
        }
        else
        {
            printf("CLI: cfg perf on|off|dump|reset\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "uart") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg uart recover\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "recover") == 0)
        {
            ui_cli_uart_recover();
            printf("CLI: uart recover done\r\n");
        }
        else
        {
            printf("CLI: cfg uart recover\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }

    App_RuntimeConfig_GetDisplayCfg(&cfg);

    if (ui_cli_stricmp(cursor, "contrast") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg contrast <-6..-80>\r\n");
            return;
        }
        if (fv > 0.0f)
        {
            fv = -fv;
        }
        cfg.db_floor = fv;
    }
    else if (ui_cli_stricmp(cursor, "gamma") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg gamma <0.5..2.5>\r\n");
            return;
        }
        cfg.gamma = fv;
    }
    else if (ui_cli_stricmp(cursor, "noise") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg noise <0..0.6>\r\n");
            return;
        }
        cfg.noise_gate_ratio = fv;
    }
    else if (ui_cli_stricmp(cursor, "adapt") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg adapt <0..6>\r\n");
            return;
        }
        cfg.noise_adapt_gain = fv;
    }
    else if (ui_cli_stricmp(cursor, "smooth") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg smooth <0..3>\r\n");
            return;
        }
        cfg.smooth_passes = (uint8_t)uv;
    }
    else if (ui_cli_stricmp(cursor, "fine") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg fine <0..3>\r\n");
            return;
        }
        cfg.fine_gain = fv;
    }
    else if (ui_cli_stricmp(cursor, "bilinear") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg bilinear <0|1>\r\n");
            return;
        }
        cfg.interp_mode = (uv != 0u) ? APP_RUNTIME_DISP_INTERP_BILINEAR : APP_RUNTIME_DISP_INTERP_NEAREST;
    }
    else if (ui_cli_stricmp(cursor, "textdiv") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg textdiv <1..20>\r\n");
            return;
        }
        cfg.text_refresh_div = (uint8_t)uv;
    }
    else if (ui_cli_stricmp(cursor, "blit") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg blit <1..8>\r\n");
            return;
        }
        cfg.blit_rows = (uint8_t)uv;
    }
    else
    {
        printf("CLI: unknown cfg key\r\n");
        return;
    }

    App_RuntimeConfig_SetDisplayCfg(&cfg);
    ui_cli_print_status();
}

static void ui_cli_poll(void)
{
    static char line_buf[UI_CLI_LINE_MAX];
    static uint16_t line_len = 0u;
    static uint8_t banner_printed = 0u;
    static TickType_t last_rx_tick = 0u;
    uint32_t i;
    uint8_t ch;

    if (banner_printed == 0u)
    {
        banner_printed = 1u;
        printf("UI CLI ready @%lu baud, type 'cfg help'\r\n", (unsigned long)huart1.Init.BaudRate);
    }

    ui_cli_uart_kick_rx_it();

    for (i = 0u; i < UI_CLI_RX_DRAIN_MAX; i++)
    {
        if (ui_cli_ring_pop(&ch) == 0u)
        {
            break;
        }

        last_rx_tick = xTaskGetTickCount();

        if ((ch == '\r') || (ch == '\n'))
        {
            if (line_len != 0u)
            {
                line_buf[line_len] = '\0';
                ui_cli_apply_line(line_buf);
                line_len = 0u;
            }
            continue;
        }

        if ((ch == 0x08u) || (ch == 0x7Fu))
        {
            if (line_len != 0u)
            {
                line_len--;
            }
            continue;
        }

        if ((ch >= 32u) && (ch <= 126u))
        {
            if (line_len < (uint16_t)(UI_CLI_LINE_MAX - 1u))
            {
                line_buf[line_len++] = (char)ch;
            }
        }
    }

    if ((last_rx_tick != 0u) &&
        ((xTaskGetTickCount() - last_rx_tick) <= pdMS_TO_TICKS(2000u)))
    {
        g_ui_cli_rx_alive = 1u;
    }
    else
    {
        g_ui_cli_rx_alive = 0u;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef st;

    if ((huart == NULL) || (huart->Instance != USART1))
    {
        return;
    }

    s_ui_cli_rx_armed = 0u;
    ui_cli_ring_push_from_isr(s_ui_cli_rx_byte);

    st = HAL_UART_Receive_IT(&huart1, &s_ui_cli_rx_byte, 1u);
    if ((st == HAL_OK) || (st == HAL_BUSY))
    {
        s_ui_cli_rx_armed = 1u;
    }
    else
    {
        s_ui_cli_rx_need_rearm = 1u;
        g_ui_cli_rx_err_count++;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != USART1))
    {
        return;
    }

    s_ui_cli_rx_armed = 0u;
    s_ui_cli_rx_need_rearm = 1u;
    g_ui_cli_rx_err_count++;
}
#else
static void ui_cli_poll(void)
{
}
#endif

void App_Task_Init(void)
{
    BaseType_t task_ok;
    App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LEGACY);
    App_Perf_Init();
    taskENTER_CRITICAL();
    s_runtime_cfg.perf_enabled = (App_Perf_IsEnabled() != 0u) ? 1u : 0u;
    taskEXIT_CRITICAL();
    s_runtime_sync_from_display();

    /* 创建长度为 1 的覆盖队列，确保端到端低延迟。 */
    xAudioFrameQueue = xQueueCreate(1, sizeof(Audio_FrameEvent_t));
    xPositionQueue = xQueueCreate(1, sizeof(Sound_Pos_t));
    configASSERT(xAudioFrameQueue != NULL);
    configASSERT(xPositionQueue != NULL);

    /* 创建音频处理任务（2304 字节栈）。 */
    task_ok = xTaskCreate(Audio_Pipeline_Task, "Audio_Pipe", 2304, NULL, APP_AUDIO_TASK_PRIO, &xAudioPipelineTaskHandle);
    configASSERT(task_ok == pdPASS);

    /* 创建 UI 显示任务（2048 字节栈）。 */
    task_ok = xTaskCreate(UI_Display_Task, "UI_Disp", 2048, NULL, APP_UI_TASK_PRIO, &xUITaskHandle);
    configASSERT(task_ok == pdPASS);
}

/**
 * @brief   音频处理主任务
 * @details 处理流程：DMA 半缓冲事件 -> 解交织 -> FFT -> SRP-PHAT -> 结果投递。
 *
 * 关键点：
 * - 通过 `event.seq` 检测事件跳变并累计异常计数。
 * - 使用 `audio_algo_decim` 实现算法降采样，非执行帧复用上次定位结果。
 * - 复用 `Mic_Freq_Buffer` 作为临时 q15 平面缓冲，减少额外 RAM 占用。
 *
 * 调试输出：
 * - `DEBUG_MODE=0`: 输出 RMS。
 * - `DEBUG_MODE=1`: 输出 FFT。
 * - `DEBUG_MODE=3`: 输出 SRP 结果。
 *
 * @param   pvParameters  FreeRTOS 任务参数（未使用）
 */
void Audio_Pipeline_Task(void *pvParameters)
{
    (void)pvParameters;

    static uint32_t s_frame_cnt = 0u;
    uint32_t s_last_seq = 0u;
    uint32_t s_decim_phase = 0u;
    Sound_Pos_t current_pos = {0.0f, 0.0f, 0.0f};
    Sound_Pos_t last_algo_pos = {0.0f, 0.0f, 0.0f};
    uint8_t has_last_algo = 0u;

    Audio_FrameEvent_t event;
    q15_t *p_current_dma_src;
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    for (;;)
    {
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if ((s_last_seq != 0u) && (event.seq > (s_last_seq + 1u)))
        {
            g_audio_both_flags_count += (event.seq - s_last_seq - 1u);
        }
        s_last_seq = event.seq;

        if (event.half_id == AUDIO_DMA_HALF_PING)
        {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if (event.half_id == AUDIO_DMA_HALF_PONG)
        {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }
        else
        {
            g_audio_no_flag_count++;
            continue;
        }

        {
            uint32_t decim = s_clamp_u32(App_RuntimeConfig_GetAudioAlgoDecim(),
                                         AUDIO_ALGO_DECIM_MIN,
                                         AUDIO_ALGO_DECIM_MAX);
            uint8_t run_algo = (has_last_algo == 0u) ? 1u : ((s_decim_phase == 0u) ? 1u : 0u);

            s_decim_phase++;
            if (s_decim_phase >= decim)
            {
                s_decim_phase = 0u;
            }

            if (run_algo != 0u)
            {
                uint32_t t_audio = App_Perf_BeginCycles();
                uint32_t t_sec;

                found_val++;
                t_sec = App_Perf_BeginCycles();
                Deinterleave_Using_Matrix(p_current_dma_src,
                                          p_temp_planar,
                                          Mic_Process_Buffer,
                                          FRAME_LEN,
                                          MIC_CHANNELS);
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_DEINT, t_sec);

                s_frame_cnt++;

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 0)
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_Channel_RMS();
                }
#endif
#endif

                t_sec = App_Perf_BeginCycles();
                AI_FFT_Process();
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_FFT, t_sec);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 1)
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
                }
#endif
#endif

                t_sec = App_Perf_BeginCycles();
                AI_SRP_PHAT_Process(&current_pos);
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_SRP, t_sec);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 3)
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_SRP_Result(&current_pos);
                }
#endif
#endif

                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_TOTAL, t_audio);
                last_algo_pos = current_pos;
                has_last_algo = 1u;
                App_Perf_CountAudioProc();
            }
            else
            {
                current_pos = last_algo_pos;
            }
        }

        xQueueOverwrite(xPositionQueue, &current_pos);
        taskYIELD();
    }
}

/**
 * @brief   UI 显示任务
 * @details 轮询位置队列并刷新显示：取最新位置 -> 快照 SRP 可视化数据 -> 渲染输出。
 *
 * 关键点：
 * - 显示未就绪时按 `UI_RETRY_INIT_MS` 周期重试初始化。
 * - 每帧仅使用队列中的最后一条位置数据，避免 UI 堆积。
 * - 在临界区内复制 SRP 可视化快照，避免读写竞争。
 *
 * @param   pvParameters  FreeRTOS 任务参数（未使用）
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;

    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};
    SRP_VisFrame_t vis_snapshot;
    uint32_t ui_frame_seq = 0u;
    uint32_t last_audio_isr_seq = 0u;
    uint8_t audio_idle_frames = 0xFFu;

    TickType_t next_render_wake;
    TickType_t last_init_try = 0u;
    uint32_t last_dma2d_timeout = 0u;

    if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
    {
        s_ui_renderer->init();
    }
    last_init_try = xTaskGetTickCount();
    next_render_wake = last_init_try;

    for (;;)
    {
        uint32_t t_loop;
        uint8_t sai_dma_active;

        ui_cli_poll();
        if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,
                       (unsigned long)g_display_init_error,
                       (unsigned long)g_lcd_init_stage,
                       (unsigned long)g_ltdc_init_stage);
#endif
                s_ui_renderer->init();
                last_init_try = now;
            }
            taskYIELD();
            continue;
        }

        App_Perf_CountUiLoop();
        App_Perf_MaybePrintRates();
        t_loop = App_Perf_BeginCycles();

        if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
        {
            last_pos = draw_pos;
            g_ui_queue_rx_count++;

            while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;
                g_ui_queue_rx_count++;
            }
        }
        else
        {
            g_ui_queue_timeout_count++;
        }

        ui_frame_seq++;

        {
            uint32_t audio_seq = g_audio_frame_seq_isr;
            if (audio_seq != last_audio_isr_seq)
            {
                last_audio_isr_seq = audio_seq;
                audio_idle_frames = 0u;
            }
            else if (audio_idle_frames < 0xFFu)
            {
                audio_idle_frames++;
            }
            sai_dma_active = (audio_idle_frames <= APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES) ? 1u : 0u;
        }

        {
            uint32_t t_sec = App_Perf_BeginCycles();
            taskENTER_CRITICAL();
            AI_SRP_CopyVisualizationFrame(&vis_snapshot);
            taskEXIT_CRITICAL();
            App_Perf_EndCycles(APP_PERF_SEC_UI_SNAPSHOT, t_sec);
        }

        {
            uint32_t t_sec = App_Perf_BeginCycles();
            s_ui_renderer->render(&last_pos, &vis_snapshot, ui_frame_seq, sai_dma_active);
            App_Perf_EndCycles(APP_PERF_SEC_UI_RENDER, t_sec);
        }
        g_ui_render_count++;

        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)
        {
#if UI_DEBUG_LOG
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,
                   (unsigned int)g_ltdc_panel_id);
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;
        }

        App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);
        vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
    }
}

