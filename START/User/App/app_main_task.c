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

/** @brief CLI 通过 UART 成功接收字节的次数（ISR 侧累加，可用于判断串口活跃性） */
volatile uint32_t g_ui_cli_rx_ok_count = 0u;
/** @brief CLI UART 接收发生错误的次数（溢出/帧错误/重启失败等均计入此处） */
volatile uint32_t g_ui_cli_rx_err_count = 0u;
/** @brief CLI 活跃标志：2 秒内收到过有效字节则为 1，否则为 0 */
volatile uint8_t  g_ui_cli_rx_alive = 0u;

/* ============================================================================
 * 调试配置 (Debug Configuration)
 * ============================================================================
 *
 * 使用说明：
 *   取消注释 DEBUG_ENABLE 以开启 VOFA+ 调试数据流输出。
 *   DEBUG_MODE 选择输出内容：
 *     0 = 各通道 RMS 幅值（音量检测调试用）
 *     1 = 指定通道 FFT 频谱（频域分析用）
 *     3 = SRP-PHAT 定位结果（声源方位调试用）
 *   DEBUG_THROTTLE_FRAMES 控制输出频率，避免串口带宽饱和。
 * ============================================================================ */

/* #define DEBUG_ENABLE */              /**< 注释掉此行可关闭所有 VOFA 调试输出，节省串口带宽 */
#define DEBUG_THROTTLE_FRAMES   20u     /**< 调试节流：每处理 20 帧才输出一次，避免 UART 阻塞 */
#define DEBUG_MODE              3       /**< 调试模式选择：0=RMS  1=FFT频谱  3=SRP定位结果 */
#define DEBUG_SPECTRUM_CHANNEL  0u      /**< FFT 频谱输出时选择的麦克风通道索引（0 为第一路） */

/* ============================================================================
 * UI 刷新参数 (UI Refresh Parameters)
 * ============================================================================
 *
 * FPS 设计说明：
 *   目标帧率 [UI_FPS_MIN, UI_FPS_MAX] 由运行时配置动态调整。
 *   vTaskDelayUntil 保证帧周期稳定，不受渲染耗时抖动影响。
 *
 * CLI 缓冲说明：
 *   环形缓冲区 UI_CLI_RX_RING_SIZE = 1024 字节，足以容纳多条完整命令。
 *   每次 UI 循环最多消耗 UI_CLI_RX_DRAIN_MAX = 256 字节，防止 CLI 占用过多 CPU。
 *
 * 算法抽帧说明：
 *   AUDIO_ALGO_DECIM = N 表示每 N 帧只跑一次 SRP-PHAT，其余帧复用上次结果，
 *   用于在精度与 CPU 占用之间做权衡。
 * ============================================================================ */

#define UI_RETRY_INIT_MS         1000u  /**< 显示初始化失败后的重试间隔 (ms)，避免死循环空转 */
#define UI_DEBUG_LOG             0u     /**< UI 内部诊断日志开关：0=关闭  1=通过 printf 输出 */
#define UI_CLI_ENABLE            1u     /**< UART CLI 功能总开关：0=编译时裁掉整个 CLI 模块 */
#define UI_CLI_LINE_MAX          96u    /**< 单条 CLI 命令行最大字节数（含终止符），超出部分被截断 */
#define UI_CLI_RX_DRAIN_MAX      256u   /**< 每次 ui_cli_poll() 最多处理的字节数，防止过度占用 CPU */
#define UI_CLI_RX_RING_SIZE      1024u  /**< UART 接收环形缓冲区大小 (字节)，必须为 2 的幂次方效果最佳 */
#define UI_FPS_MIN               5u     /**< UI 帧率下限 (fps)，低于此值会强制 clamp 到 5 fps */
#define UI_FPS_MAX               30u    /**< UI 帧率上限 (fps)，高于此值会强制 clamp 到 30 fps */
#define UI_FPS_DEFAULT           20u    /**< 上电默认帧率 (fps)，20 fps = 50 ms 每帧 */
#define AUDIO_ALGO_DECIM_MIN     1u     /**< 算法抽帧比最小值 1（每帧都跑 SRP-PHAT，精度最高） */
#define AUDIO_ALGO_DECIM_MAX     8u     /**< 算法抽帧比最大值 8（8 帧才跑一次，CPU 占用最低） */
#define AUDIO_ALGO_DECIM_DEFAULT 1u     /**< 上电默认抽帧比，默认每帧都执行 SRP-PHAT 算法 */
#define PERF_RING_SAMPLES        64u    /**< 性能环形缓冲区容量（最近 64 个样本用于计算 p95 延迟） */
#define PERF_RATE_PERIOD_MS      1000u  /**< 性能速率打印周期 (ms)，每秒输出一次吞吐率统计 */

/* ============================================================================
 * 任务优先级 (Task Priorities)
 * ============================================================================
 *
 * 两个任务设置为相同优先级（4），由 FreeRTOS 时间片轮转调度。
 * 音频任务依赖队列阻塞（portMAX_DELAY），有数据时才运行，不会饿死 UI 任务。
 * UI 任务依赖 vTaskDelayUntil 定周期唤醒，空闲时主动出让 CPU。
 * 优先级不宜设置过高，以免抢占系统级任务（如 IDLE、TimerDaemon）。
 * ============================================================================ */

#define APP_AUDIO_TASK_PRIO     4u  /**< 音频处理任务优先级，与 UI 任务同级，由队列阻塞协同调度 */
#define APP_UI_TASK_PRIO        4u  /**< UI 显示任务优先级，与音频任务同级，由定时唤醒协同调度 */

/* ============================================================================
 * UI CLI (Runtime Tuning via UART)
 * ============================================================================
 *
 * 通过 USART1 实现运行时参数热调整，无需重新编译固件。
 * 命令格式：  cfg <key> [value]
 * 典型用例：  cfg mode fast / cfg uifps 25 / cfg perf on
 * ============================================================================ */

/* ============================================================================
 * UI 渲染后端与运行时旋钮 (Renderer Backend & Runtime Knobs)
 * ============================================================================
 *
 * 渲染后端通过函数指针表（虚函数表模式）实现热插拔。
 * 当前仅有 LEGACY 后端（App_Display_Render），预留扩展接口。
 * 运行时配置 s_runtime_cfg 是系统唯一的参数中枢，所有任务通过
 * App_RuntimeConfig_* API 读写，临界区保护保证多任务安全。
 * ============================================================================ */

/**
 * @brief UI 渲染后端操作表（虚函数表）
 *
 * 包含三个函数指针，分别对应：
 *  - is_ready : 查询显示层是否已完成初始化
 *  - init     : 执行显示层初始化（幂等，可重复调用）
 *  - render   : 执行一帧渲染并提交到 LCD
 *
 * 通过函数指针而非直接调用，未来可无缝切换到不同渲染实现（如 GPU 加速后端）。
 */
typedef struct
{
    uint8_t (*is_ready)(void);          /**< 返回 1 表示显示硬件已就绪，可以开始渲染 */
    void    (*init)(void);              /**< 初始化显示层：LCD 时序、LTDC 配置、帧缓冲清零 */
    void    (*render)(const Sound_Pos_t   *pos,        /**< 声源位置（方位角+能量） */
                      const SRP_VisFrame_t *vis_frame, /**< SRP 热力图可视化快照 */
                      uint32_t             frame_seq,  /**< UI 帧序号，用于文字刷新分频 */
                      uint8_t              sai_dma_active); /**< SAI DMA 活跃标志 */
} App_UiRendererOps_t;

/**
 * @brief 运行时配置全局单例（静态存储，临界区保护）
 *
 * 字段初始值均从编译期宏（app_display_cfg.h）读取，保证参数一致性。
 * 所有字段修改必须通过 App_RuntimeConfig_Set* API，禁止直接写入。
 */
static App_Runtime_Config_t s_runtime_cfg = {
    UI_FPS_DEFAULT,             /**< ui_target_fps：目标帧率，默认 20 fps */
    AUDIO_ALGO_DECIM_DEFAULT,   /**< audio_algo_decim：算法抽帧比，默认 1（每帧都跑） */
    0u,                         /**< perf_enabled：性能统计默认关闭 */
    {0u, 0u, 0u},               /**< reserved[3]：对齐填充，保留备用 */
    APP_RUNTIME_DISP_MODE_BALANCED, /**< display_mode：默认均衡模式（兼顾清晰度与 CPU） */
    {
        APP_DISPLAY_EMA_ATTACK,         /**< ema_attack：EMA 攻击系数（能量上升速率） */
        APP_DISPLAY_EMA_DECAY,          /**< ema_decay：EMA 衰减系数（能量下降速率） */
        APP_DISPLAY_DYNAMIC_DB_FLOOR,   /**< db_floor：动态范围底限 (dB)，低于此值视为噪声 */
        APP_DISPLAY_FINE_GAIN,          /**< fine_gain：精细网格叠加增益 */
        APP_DISPLAY_DYNAMIC_GAMMA,      /**< gamma：伽马校正系数，调整热力图视觉对比度 */
        APP_DISPLAY_NOISE_GATE_RATIO,   /**< noise_gate_ratio：噪声门限比例，抑制弱信号假峰 */
        APP_DISPLAY_NOISE_ADAPT_GAIN,   /**< noise_adapt_gain：自适应噪声估计增益 */
        APP_DISPLAY_SMOOTH_PASSES,      /**< smooth_passes：空间平滑迭代次数（0=不平滑） */
        APP_DISPLAY_FINE_FUSION_ENABLE, /**< fine_fusion_enable：是否叠加精细 SRP 网格 */
        APP_DISPLAY_DRAW_COARSE_GRID,   /**< draw_coarse_grid：是否绘制粗网格参考线 */
        /* 插值模式：从编译期 bilinear 宏映射到运行时枚举 */
        (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_RUNTIME_DISP_INTERP_BILINEAR
                                               : APP_RUNTIME_DISP_INTERP_NEAREST,
        APP_RUNTIME_DISP_NORM_FAST,     /**< norm_mode：归一化策略，默认快速模式（仅用峰值） */
        APP_DISPLAY_TEXT_REFRESH_DIV,   /**< text_refresh_div：文字刷新分频（每 N 帧刷新一次） */
        APP_DISPLAY_BLIT_ROWS_MAX       /**< blit_rows：每次 blit 传输的最大行数（DMA2D 分块） */
    }
};

/* -------------------------------------------------------------------------- */
/* 前向声明：Legacy 渲染后端的三个实现函数（定义在下方）                         */
/* -------------------------------------------------------------------------- */
static uint8_t s_ui_legacy_is_ready(void);   /**< 查询 App_Display 是否就绪 */
static void    s_ui_legacy_init(void);        /**< 调用 App_Display_Init() */
static void    s_ui_legacy_render(const Sound_Pos_t *pos,
                                  const SRP_VisFrame_t *vis_frame,
                                  uint32_t frame_seq,
                                  uint8_t sai_dma_active); /**< 调用 App_Display_Render() */

/** @brief Legacy 渲染后端操作表（const，编译期确定，存入 Flash） */
static const App_UiRendererOps_t s_ui_renderer_legacy_ops = {
    s_ui_legacy_is_ready,   /**< is_ready 函数指针 */
    s_ui_legacy_init,        /**< init 函数指针 */
    s_ui_legacy_render       /**< render 函数指针 */
};

/** @brief 当前激活的渲染后端指针，默认指向 Legacy 后端 */
static const App_UiRendererOps_t *s_ui_renderer = &s_ui_renderer_legacy_ops;

/** @brief 当前后端枚举值，用于外部查询（App_UiRenderer_GetBackend） */
static volatile App_UiRenderBackend_t s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;

/* ============================================================================
 * 性能分析器 (Performance Profiler)
 * ============================================================================
 *
 * 原理：使用 ARM Cortex-M7 的 DWT（数据观察点与追踪）单元中的
 *       CYCCNT 寄存器进行周期级精度计时，无需额外硬件定时器资源。
 *
 * 使用流程：
 *   1. App_Perf_Init()  ——  初始化 DWT 寄存器，清零所有统计量
 *   2. App_Perf_SetEnabled(1) ——  开始记录
 *   3. uint32_t t = App_Perf_BeginCycles()  ——  记录起始周期
 *   4. ... 执行被测代码 ...
 *   5. App_Perf_EndCycles(section, t)  ——  记录并累加耗时
 *   6. App_Perf_Dump()  ——  通过 UART 打印各段平均/p95/最大耗时（毫秒）
 *
 * 统计精度：
 *   环形缓冲保存最近 PERF_RING_SAMPLES(64) 个样本，用于计算 p95 延迟，
 *   避免少量毛刺拉高平均值，更真实地反映实时性能。
 * ============================================================================ */

/**
 * @brief 单个剖析区间的统计数据结构
 *
 * 每个 App_Perf_Section_t 对应一个此结构的实例。
 * ring[] 是循环队列，存储最近 PERF_RING_SAMPLES 次的周期数，用于 p95 计算。
 */
typedef struct
{
    uint64_t total_cycles;              /**< 该区间所有样本的周期总和，用于计算平均值 */
    uint32_t sample_count;              /**< 已采集的样本总数（含超出 ring 容量后的旧样本） */
    uint32_t max_cycles;                /**< 历史最大单次耗时（周期数），用于 worst-case 分析 */
    uint32_t ring[PERF_RING_SAMPLES];   /**< 最近 64 次耗时的环形缓冲区 */
    uint32_t ring_count;                /**< ring 中有效数据个数（上限为 PERF_RING_SAMPLES） */
    uint32_t ring_head;                 /**< ring 写指针（下一次写入的位置） */
} App_Perf_SectionStat_t;

/** @brief 所有剖析区间的统计实例数组，按 App_Perf_Section_t 枚举索引 */
static App_Perf_SectionStat_t s_perf_stats[APP_PERF_SEC_COUNT];

/** @brief 各剖析区间的可读名称，与 App_Perf_Section_t 枚举一一对应，用于 Dump 输出 */
static const char *s_perf_section_names[APP_PERF_SEC_COUNT] = {
    "audio_total",  /**< APP_PERF_SEC_AUDIO_TOTAL : 整个音频算法管线总耗时 */
    "audio_deint",  /**< APP_PERF_SEC_AUDIO_DEINT : 解交织（DMA 数据重排）耗时 */
    "audio_fft",    /**< APP_PERF_SEC_AUDIO_FFT   : FFT 频域变换耗时 */
    "audio_srp",    /**< APP_PERF_SEC_AUDIO_SRP   : SRP-PHAT 声源定位算法耗时 */
    "ui_loop",      /**< APP_PERF_SEC_UI_LOOP      : UI 任务单次循环总耗时 */
    "ui_snapshot",  /**< APP_PERF_SEC_UI_SNAPSHOT  : 临界区内复制 SRP 可视化快照耗时 */
    "ui_render",    /**< APP_PERF_SEC_UI_RENDER    : 渲染后端 render() 调用耗时 */
    "disp_prepare", /**< APP_PERF_SEC_DISP_PREPARE : 显示准备（归一化前处理）耗时 */
    "disp_norm",    /**< APP_PERF_SEC_DISP_NORM    : 热力图归一化耗时 */
    "disp_render",  /**< APP_PERF_SEC_DISP_RENDER  : 像素渲染（colormap 映射）耗时 */
    "disp_overlay", /**< APP_PERF_SEC_DISP_OVERLAY : 叠加层（坐标轴/文字）耗时 */
    "disp_commit"   /**< APP_PERF_SEC_DISP_COMMIT  : 提交帧缓冲到 LTDC/DMA2D 耗时 */
};

/** @brief 性能统计全局开关：0=关闭（BeginCycles 直接返回 0），1=开启 */
static volatile uint8_t  s_perf_enabled = 0u;
/** @brief DWT 初始化成功标志：0=CYCCNT 不可用，1=已就绪可正常计时 */
static volatile uint8_t  s_perf_dwt_ready = 0u;
/** @brief 音频算法处理帧计数（每次执行 SRP-PHAT 后递增，用于 proc 速率计算） */
static volatile uint32_t s_perf_audio_proc_count = 0u;
/** @brief UI 主循环执行次数（每次渲染循环入口递增，用于 UI 速率计算） */
static volatile uint32_t s_perf_ui_loop_count = 0u;

/* 以下变量记录上次速率打印时刻的各计数基准值，用差值除时间间隔得到速率 */
static uint32_t s_perf_last_tick = 0u;          /**< 上次打印时的 FreeRTOS tick 值 */
static uint32_t s_perf_last_audio_isr = 0u;     /**< 上次打印时 ISR 帧序号基准 */
static uint32_t s_perf_last_audio_proc = 0u;    /**< 上次打印时音频处理帧计数基准 */
static uint32_t s_perf_last_ui_loop = 0u;       /**< 上次打印时 UI 循环次数基准 */
static uint32_t s_perf_last_commit = 0u;        /**< 上次打印时 LTDC 提交请求数基准 */
static uint32_t s_perf_last_swap = 0u;          /**< 上次打印时 LTDC 实际换页完成数基准 */

/**
 * @brief   无符号 32 位整数范围钳位
 * @details 将 v 限制在 [lo, hi] 闭区间内。
 *          用于所有运行时参数的边界检查，防止非法值写入配置。
 *
 * @param   v   待钳位的值
 * @param   lo  下限（含），若 v < lo 则返回 lo
 * @param   hi  上限（含），若 v > hi 则返回 hi
 * @return  钳位后的值
 */
static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)           /* 低于下限：直接返回下限值 */
    {
        return lo;
    }
    if (v > hi)           /* 高于上限：直接返回上限值 */
    {
        return hi;
    }
    return v;             /* 在范围内：原值返回 */
}

/**
 * @brief   App_Display 模式枚举 -> 运行时配置模式枚举（内部转换）
 * @details App_Display 模块使用自己的枚举，运行时配置模块使用独立枚举。
 *          两套枚举在语义上一一对应，此函数负责从 Display 侧转换到 Runtime 侧，
 *          确保两个模块可以独立演进而不产生耦合。
 *
 * @param   mode  App_Display 模块的模式值
 * @return  对应的运行时配置模式值
 */
static App_Runtime_DisplayMode_t s_runtime_mode_from_display(App_Display_Mode_t mode)
{
    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:     /* 快速模式：最低延迟，牺牲画质 */
            return APP_RUNTIME_DISP_MODE_FAST;
        case APP_DISPLAY_MODE_CLEAN:    /* 清晰模式：最佳画质，CPU 占用较高 */
            return APP_RUNTIME_DISP_MODE_CLEAN;
        case APP_DISPLAY_MODE_BALANCED: /* 均衡模式：默认，兼顾性能与画质 */
        default:                        /* 未知值也映射到均衡模式，保证安全 */
            return APP_RUNTIME_DISP_MODE_BALANCED;
    }
}

/**
 * @brief   运行时配置模式枚举 -> App_Display 模式枚举（内部转换）
 * @details 与 s_runtime_mode_from_display 互为反向映射。
 *          在将运行时配置写入 App_Display 模块时调用。
 *
 * @param   mode  运行时配置的模式值
 * @return  对应的 App_Display 模式值
 */
static App_Display_Mode_t s_runtime_mode_to_display(App_Runtime_DisplayMode_t mode)
{
    switch (mode)
    {
        case APP_RUNTIME_DISP_MODE_FAST:     /* 快速模式 -> Display FAST */
            return APP_DISPLAY_MODE_FAST;
        case APP_RUNTIME_DISP_MODE_CLEAN:    /* 清晰模式 -> Display CLEAN */
            return APP_DISPLAY_MODE_CLEAN;
        case APP_RUNTIME_DISP_MODE_BALANCED: /* 均衡模式 -> Display BALANCED */
        default:                             /* 未知值安全回退到均衡模式 */
            return APP_DISPLAY_MODE_BALANCED;
    }
}

/**
 * @brief   App_Display 配置结构体 -> 运行时配置结构体（字段逐一复制 + 枚举转换）
 * @details 负责将 App_Display 内部使用的配置格式转换为运行时配置格式。
 *          两个结构体字段语义完全相同，差异仅在于插值/归一化枚举的不同定义。
 *
 * @param   src  源：App_Display 模块的运行时配置（只读）
 * @param   dst  目标：写入运行时配置结构体
 */
static void s_runtime_displaycfg_from_display(const App_Display_RuntimeCfg_t *src,
                                              App_Runtime_DisplayCfg_t *dst)
{
    if ((src == NULL) || (dst == NULL)) /* 空指针保护，防止野指针访问 */
    {
        return;
    }

    dst->ema_attack        = src->ema_attack;        /* EMA 攻击系数直接复制 */
    dst->ema_decay         = src->ema_decay;         /* EMA 衰减系数直接复制 */
    dst->db_floor          = src->db_floor;          /* 动态范围底限直接复制 */
    dst->fine_gain         = src->fine_gain;         /* 精细增益直接复制 */
    dst->gamma             = src->gamma;             /* 伽马系数直接复制 */
    dst->noise_gate_ratio  = src->noise_gate_ratio;  /* 噪声门限比例直接复制 */
    dst->noise_adapt_gain  = src->noise_adapt_gain;  /* 自适应噪声增益直接复制 */
    dst->smooth_passes     = src->smooth_passes;     /* 平滑迭代次数直接复制 */
    dst->fine_fusion_enable = src->fine_fusion_enable; /* 精细融合开关直接复制 */
    dst->draw_coarse_grid  = src->draw_coarse_grid;  /* 粗网格绘制开关直接复制 */
    /* 插值模式：Display 枚举 -> Runtime 枚举（不同模块定义相同语义的不同枚举值） */
    dst->interp_mode = (src->interp_mode == APP_DISPLAY_INTERP_BILINEAR)
                           ? APP_RUNTIME_DISP_INTERP_BILINEAR
                           : APP_RUNTIME_DISP_INTERP_NEAREST;
    /* 归一化模式：Display 枚举 -> Runtime 枚举 */
    dst->norm_mode = (src->norm_mode == APP_DISPLAY_NORM_FULL)
                         ? APP_RUNTIME_DISP_NORM_FULL
                         : APP_RUNTIME_DISP_NORM_FAST;
    dst->text_refresh_div  = src->text_refresh_div;  /* 文字刷新分频直接复制 */
    dst->blit_rows         = src->blit_rows;         /* DMA2D 分块行数直接复制 */
}

/**
 * @brief   运行时配置结构体 -> App_Display 配置结构体（与上方函数互为逆操作）
 * @details 在将运行时配置下发到 App_Display 模块时调用。
 *          枚举转换方向与 s_runtime_displaycfg_from_display 相反。
 *
 * @param   src  源：运行时配置结构体（只读）
 * @param   dst  目标：写入 App_Display 运行时配置结构体
 */
static void s_runtime_displaycfg_to_display(const App_Runtime_DisplayCfg_t *src,
                                            App_Display_RuntimeCfg_t *dst)
{
    if ((src == NULL) || (dst == NULL)) /* 空指针保护 */
    {
        return;
    }

    dst->ema_attack        = src->ema_attack;        /* EMA 攻击系数 */
    dst->ema_decay         = src->ema_decay;         /* EMA 衰减系数 */
    dst->db_floor          = src->db_floor;          /* 动态范围底限 */
    dst->fine_gain         = src->fine_gain;         /* 精细增益 */
    dst->gamma             = src->gamma;             /* 伽马系数 */
    dst->noise_gate_ratio  = src->noise_gate_ratio;  /* 噪声门限比例 */
    dst->noise_adapt_gain  = src->noise_adapt_gain;  /* 自适应噪声增益 */
    dst->smooth_passes     = src->smooth_passes;     /* 平滑迭代次数 */
    dst->fine_fusion_enable = src->fine_fusion_enable; /* 精细融合开关 */
    dst->draw_coarse_grid  = src->draw_coarse_grid;  /* 粗网格绘制开关 */
    /* 插值模式：Runtime 枚举 -> Display 枚举（反向映射） */
    dst->interp_mode = (src->interp_mode == APP_RUNTIME_DISP_INTERP_BILINEAR)
                           ? APP_DISPLAY_INTERP_BILINEAR
                           : APP_DISPLAY_INTERP_NEAREST;
    /* 归一化模式：Runtime 枚举 -> Display 枚举（反向映射） */
    dst->norm_mode = (src->norm_mode == APP_RUNTIME_DISP_NORM_FULL)
                         ? APP_DISPLAY_NORM_FULL
                         : APP_DISPLAY_NORM_FAST;
    dst->text_refresh_div  = src->text_refresh_div;  /* 文字刷新分频 */
    dst->blit_rows         = src->blit_rows;         /* DMA2D 分块行数 */
}

/**
 * @brief   从 App_Display 模块回读配置并同步到运行时配置单例
 * @details 在 SetDisplayMode / SetDisplayCfg 调用之后执行，
 *          确保 s_runtime_cfg 与 App_Display 内部状态保持一致。
 *          使用临界区保护写操作，防止 UI 任务读到中间状态。
 */
static void s_runtime_sync_from_display(void)
{
    App_Display_RuntimeCfg_t display_cfg;   /* 临时存放从 App_Display 读回的配置 */
    App_Display_Mode_t       display_mode;  /* 临时存放从 App_Display 读回的模式 */

    App_Display_GetConfig(&display_cfg);    /* 读取 App_Display 当前运行时配置 */
    display_mode = App_Display_GetMode();   /* 读取 App_Display 当前渲染模式 */

    taskENTER_CRITICAL();                   /* 进入临界区：禁止任务切换，保证原子写 */
    s_runtime_cfg.display_mode = s_runtime_mode_from_display(display_mode);           /* 同步模式 */
    s_runtime_displaycfg_from_display(&display_cfg, &s_runtime_cfg.display_cfg);     /* 同步配置 */
    taskEXIT_CRITICAL();                    /* 退出临界区：恢复调度 */
}

/**
 * @brief   读取完整运行时配置快照（线程安全）
 * @details 在临界区内完整复制 s_runtime_cfg，保证调用者拿到的是一致的快照，
 *          而非各字段分开读取时可能出现的撕裂状态。
 *
 * @param   cfg  输出参数，写入当前运行时配置副本
 */
void App_RuntimeConfig_Get(App_Runtime_Config_t *cfg)
{
    if (cfg == NULL)            /* 空指针保护 */
    {
        return;
    }

    taskENTER_CRITICAL();       /* 临界区：保证多字节结构体读取的原子性 */
    *cfg = s_runtime_cfg;       /* 整体结构体赋值复制 */
    taskEXIT_CRITICAL();        /* 恢复调度 */
}

/**
 * @brief   设置 UI 目标帧率（线程安全，自动 clamp）
 * @param   fps  期望帧率，将被 clamp 到 [UI_FPS_MIN, UI_FPS_MAX]
 */
void App_RuntimeConfig_SetUiTargetFps(uint32_t fps)
{
    fps = s_clamp_u32(fps, UI_FPS_MIN, UI_FPS_MAX); /* 边界钳位，防止非法值 */
    taskENTER_CRITICAL();
    s_runtime_cfg.ui_target_fps = fps;               /* 写入配置单例 */
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取 UI 目标帧率（线程安全）
 * @return  当前目标帧率 (fps)
 */
uint32_t App_RuntimeConfig_GetUiTargetFps(void)
{
    uint32_t fps;
    taskENTER_CRITICAL();
    fps = s_runtime_cfg.ui_target_fps;  /* 临界区内读取，防止写端同时修改 */
    taskEXIT_CRITICAL();
    return fps;
}

/**
 * @brief   设置音频算法抽帧比（线程安全，自动 clamp）
 * @details decim=1 每帧都跑 SRP-PHAT，decim=N 每 N 帧才跑一次。
 *          增大 decim 可降低 CPU 占用，但会降低声源位置更新频率。
 * @param   decim  抽帧比，clamp 到 [AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX]
 */
void App_RuntimeConfig_SetAudioAlgoDecim(uint32_t decim)
{
    decim = s_clamp_u32(decim, AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX); /* clamp */
    taskENTER_CRITICAL();
    s_runtime_cfg.audio_algo_decim = decim;  /* 写入配置，音频任务下一帧生效 */
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取音频算法抽帧比（线程安全）
 * @return  当前抽帧比
 */
uint32_t App_RuntimeConfig_GetAudioAlgoDecim(void)
{
    uint32_t decim;
    taskENTER_CRITICAL();
    decim = s_runtime_cfg.audio_algo_decim;
    taskEXIT_CRITICAL();
    return decim;
}

/**
 * @brief   设置性能统计开关（线程安全）
 * @details 内部调用 App_Perf_SetEnabled()，若 DWT 不可用则开启失败，
 *          同步将实际状态写回 s_runtime_cfg.perf_enabled。
 * @param   enable  1=开启，0=关闭
 */
void App_RuntimeConfig_SetPerfEnabled(uint8_t enable)
{
    App_Perf_SetEnabled(enable);                           /* 尝试开启 DWT 并设置标志 */
    taskENTER_CRITICAL();
    /* 读取 Perf 模块实际状态同步回配置（若 DWT 不可用，开启会失败，此处反映真实状态） */
    s_runtime_cfg.perf_enabled = (App_Perf_IsEnabled() != 0u) ? 1u : 0u;
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取性能统计是否已启用（线程安全）
 * @return  1=已启用，0=未启用
 */
uint8_t App_RuntimeConfig_GetPerfEnabled(void)
{
    uint8_t enabled;
    taskENTER_CRITICAL();
    enabled = s_runtime_cfg.perf_enabled;
    taskEXIT_CRITICAL();
    return enabled;
}

/**
 * @brief   设置显示渲染模式（线程安全）
 * @details 先将运行时枚举转换为 App_Display 枚举，调用 App_Display_SetMode()，
 *          再将 App_Display 的实际状态回读同步到 s_runtime_cfg，保证一致性。
 * @param   mode  新的显示模式（FAST / BALANCED / CLEAN）
 */
void App_RuntimeConfig_SetDisplayMode(App_Runtime_DisplayMode_t mode)
{
    App_Display_Mode_t display_mode = s_runtime_mode_to_display(mode); /* 枚举转换 */
    App_Display_SetMode(display_mode);   /* 写入 App_Display 模块 */
    s_runtime_sync_from_display();       /* 回读同步，保证 s_runtime_cfg 与实际一致 */
}

/**
 * @brief   读取当前显示渲染模式（线程安全）
 * @return  当前模式枚举值
 */
App_Runtime_DisplayMode_t App_RuntimeConfig_GetDisplayMode(void)
{
    App_Runtime_DisplayMode_t mode;
    taskENTER_CRITICAL();
    mode = s_runtime_cfg.display_mode;
    taskEXIT_CRITICAL();
    return mode;
}

/**
 * @brief   批量设置显示参数（线程安全）
 * @details 将运行时格式的配置转换后写入 App_Display，然后回读同步。
 *          调用后所有显示参数（EMA/伽马/噪声门限等）立即对下一帧生效。
 * @param   cfg  新的显示参数（若为 NULL 则直接返回）
 */
void App_RuntimeConfig_SetDisplayCfg(const App_Runtime_DisplayCfg_t *cfg)
{
    App_Display_RuntimeCfg_t display_cfg;  /* 转换后的中间结构体 */

    if (cfg == NULL)   /* 空指针保护 */
    {
        return;
    }

    s_runtime_displaycfg_to_display(cfg, &display_cfg); /* 格式转换：Runtime -> Display */
    App_Display_SetConfig(&display_cfg);                 /* 写入 App_Display 模块 */
    s_runtime_sync_from_display();                       /* 回读同步 */
}

/**
 * @brief   读取当前显示参数（线程安全）
 * @param   cfg  输出参数，写入当前显示参数副本
 */
void App_RuntimeConfig_GetDisplayCfg(App_Runtime_DisplayCfg_t *cfg)
{
    if (cfg == NULL)   /* 空指针保护 */
    {
        return;
    }

    taskENTER_CRITICAL();
    *cfg = s_runtime_cfg.display_cfg;  /* 整体结构体复制，保证一致性 */
    taskEXIT_CRITICAL();
}

/**
 * @brief   切换 UI 渲染后端（线程安全）
 * @details 在临界区内切换函数指针表，UI 任务下一次调用 render() 时生效。
 *          当前仅支持 LEGACY 后端，预留未来扩展（如 GPU 加速后端）。
 * @param   backend  目标后端枚举值
 */
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend)
{
    taskENTER_CRITICAL();  /* 临界区：保证指针切换原子性，防止 UI 任务读到半切换状态 */
    switch (backend)
    {
        case APP_UI_RENDER_BACKEND_LEGACY:  /* 切换到 Legacy 软件渲染后端 */
        default:                            /* 未知后端也回退到 Legacy（安全保底） */
            s_ui_renderer = &s_ui_renderer_legacy_ops;   /* 更新操作表指针 */
            s_ui_backend  = APP_UI_RENDER_BACKEND_LEGACY; /* 更新枚举值 */
            break;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取当前 UI 渲染后端枚举值（线程安全）
 * @return  当前后端枚举值
 */
App_UiRenderBackend_t App_UiRenderer_GetBackend(void)
{
    App_UiRenderBackend_t backend;
    taskENTER_CRITICAL();
    backend = s_ui_backend;
    taskEXIT_CRITICAL();
    return backend;
}

/* -------------------------------------------------------------------------- */
/* Legacy 渲染后端实现（直接透传到 App_Display 模块）                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief   Legacy 后端 is_ready 实现
 * @return  App_Display_IsReady() 的返回值（1=已初始化完成，0=未就绪）
 */
static uint8_t s_ui_legacy_is_ready(void)
{
    return App_Display_IsReady();  /* 直接查询 App_Display 初始化状态 */
}

/**
 * @brief   Legacy 后端 init 实现
 * @details 调用 App_Display_Init() 初始化 LCD 时序、LTDC、帧缓冲区。
 *          若 LCD 已就绪则为空操作（App_Display_Init 内部幂等处理）。
 */
static void s_ui_legacy_init(void)
{
    App_Display_Init();  /* 初始化显示层，包括 LCD 驱动、LTDC 配置、帧缓冲清零 */
}

/**
 * @brief   Legacy 后端 render 实现（透传参数到 App_Display_Render）
 * @param   pos           声源位置（方位角+能量），用于绘制十字光标
 * @param   vis_frame     SRP 热力图可视化快照，用于渲染背景热力图
 * @param   frame_seq     UI 帧序号，供文字刷新分频逻辑使用
 * @param   sai_dma_active  SAI DMA 是否活跃，用于显示 "无音频信号" 提示
 */
static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active)
{
    /* 直接转发所有参数到 App_Display 渲染模块，无额外处理 */
    App_Display_Render(pos, vis_frame, frame_seq, sai_dma_active);
}

/**
 * @brief   uint32_t 升序比较函数，供 qsort() 调用
 * @details 用于对性能环形缓冲区的样本排序，以便计算 p95 百分位延迟。
 *          按 C 标准 qsort 约定：返回负数表示 a < b，0 表示相等，正数表示 a > b。
 *
 * @param   a  指向第一个 uint32_t 的指针
 * @param   b  指向第二个 uint32_t 的指针
 * @return  比较结果（-1 / 0 / 1）
 */
static int s_u32_cmp(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t *)a;  /* 取第一个值 */
    uint32_t vb = *(const uint32_t *)b;  /* 取第二个值 */
    if (va < vb)    /* a 小于 b */
    {
        return -1;
    }
    if (va > vb)    /* a 大于 b */
    {
        return 1;
    }
    return 0;       /* 相等 */
}

/**
 * @brief   使能 ARM DWT 周期计数器（CYCCNT）
 * @details 步骤：
 *          1. 写 CoreDebug->DEMCR 使能 DWT/ITM/ETM 追踪单元
 *          2. 写 DWT->LAR 解锁寄存器（部分 MCU 需要，否则写操作被忽略）
 *          3. 清零 CYCCNT，然后开启计数
 *          4. 读回验证 CYCCNTENA 位是否真正置位（无 DWT 的芯片会失败）
 *
 * @return  1=DWT 可用并已使能，0=硬件不支持 CYCCNT
 */
static uint8_t s_perf_enable_dwt(void)
{
    if (s_perf_dwt_ready != 0u)          /* 已初始化过，直接返回成功 */
    {
        return 1u;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; /* 使能 DWT/ITM 追踪模块电源 */
    DWT->LAR = 0xC5ACCE55u;                          /* 解锁 DWT 寄存器（Cortex-M7 需要） */
    DWT->CYCCNT = 0u;                                /* 清零计数器，避免读到历史值 */
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;            /* 开启周期计数使能位 */

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) /* 验证：写入后读回，确认硬件支持 */
    {
        return 0u;  /* DWT CYCCNT 不可用（部分精简 Cortex-M 没有此单元） */
    }

    s_perf_dwt_ready = 1u;  /* 标记 DWT 已就绪 */
    return 1u;
}

/**
 * @brief   重置速率计算基准值（当前时刻各计数器快照）
 * @details 每次开始新一轮速率打印周期时调用，
 *          以当前时刻作为下次差分计算的起始基准。
 */
static void s_perf_reset_rate_baseline(void)
{
    s_perf_last_tick        = xTaskGetTickCount();       /* 记录当前 FreeRTOS 滴答值 */
    s_perf_last_audio_isr   = g_audio_frame_seq_isr;    /* 记录 ISR 帧序号基准 */
    s_perf_last_audio_proc  = s_perf_audio_proc_count;  /* 记录音频处理帧计数基准 */
    s_perf_last_ui_loop     = s_perf_ui_loop_count;     /* 记录 UI 循环次数基准 */
    s_perf_last_commit      = g_ltdc_swap_pending_count; /* 记录 LTDC 提交请求数基准 */
    s_perf_last_swap        = g_ltdc_swap_count;         /* 记录 LTDC 换页完成数基准 */
}

/**
 * @brief   初始化性能分析器
 * @details 清零所有统计量，关闭使能标志，尝试初始化 DWT（不强制开启统计）。
 * @note    在 FreeRTOS 启动前由 App_Task_Init() 调用。
 */
void App_Perf_Init(void)
{
    App_Perf_Reset();           /* 清零所有剖析区间统计量和速率基准 */
    s_perf_enabled = 0u;        /* 默认关闭，需通过 CLI 'cfg perf on' 开启 */
    (void)s_perf_enable_dwt();  /* 尝试初始化 DWT，结果不影响此处逻辑 */
}

/**
 * @brief   运行时开关性能统计（可通过 CLI 动态调用）
 * @details 开启时首先尝试初始化 DWT，若失败则回退到关闭状态并打印错误。
 * @param   enable  1=开启统计，0=关闭统计
 */
void App_Perf_SetEnabled(uint8_t enable)
{
    if (enable != 0u)
    {
        if (s_perf_enable_dwt() == 0u)   /* 开启前先确保 DWT 可用 */
        {
            s_perf_enabled = 0u;          /* DWT 不可用，强制保持关闭 */
            printf("perf: DWT unavailable\r\n"); /* 通过串口告知用户 */
            return;
        }
        s_perf_enabled = 1u;             /* DWT 就绪，正式开启统计 */
        s_perf_reset_rate_baseline();    /* 重置速率基准，避免第一次打印出现超大值 */
    }
    else
    {
        s_perf_enabled = 0u;             /* 直接关闭，无需清理统计数据 */
    }
}

/**
 * @brief   查询性能统计是否已开启
 * @return  1=已开启，0=未开启
 */
uint8_t App_Perf_IsEnabled(void)
{
    return s_perf_enabled;  /* 直接返回标志，无需临界区（单字节读取原子） */
}

/**
 * @brief   清零所有性能统计数据（可通过 CLI 'cfg perf reset' 调用）
 * @details 清零后下次 Dump 输出将从零开始重新统计，不影响当前使能状态。
 */
void App_Perf_Reset(void)
{
    memset(s_perf_stats, 0, sizeof(s_perf_stats));  /* 清零所有区间统计量 */
    s_perf_audio_proc_count = 0u;                   /* 清零音频处理帧计数 */
    s_perf_ui_loop_count    = 0u;                   /* 清零 UI 循环计数 */
    s_perf_reset_rate_baseline();                   /* 同步重置速率基准 */
}

/**
 * @brief   记录剖析区间起始时刻（返回当前 CYCCNT）
 * @details 若统计未使能或 DWT 未就绪，返回 0，EndCycles 收到 0 后同样无操作，
 *          保证在关闭状态下不产生任何副作用。
 *
 * @return  当前 DWT CYCCNT 值（周期数），用于传入 App_Perf_EndCycles()
 */
uint32_t App_Perf_BeginCycles(void)
{
    if ((s_perf_enabled == 0u) || (s_perf_dwt_ready == 0u)) /* 快速退出：关闭状态 */
    {
        return 0u;      /* 返回 0，EndCycles 中 delta = now - 0 可能很大，但不统计 */
    }
    return DWT->CYCCNT; /* 读取当前周期计数器值 */
}

/**
 * @brief   记录剖析区间结束时刻并累加统计数据
 * @details 计算 delta = CYCCNT_now - start_cycles，更新对应区间的：
 *          - total_cycles（用于均值）
 *          - sample_count（样本数）
 *          - max_cycles（最大值）
 *          - ring[]（环形缓冲，用于 p95）
 *
 * @param   section       剖析区间枚举，对应 s_perf_stats 中的索引
 * @param   start_cycles  由 App_Perf_BeginCycles() 返回的起始值
 */
void App_Perf_EndCycles(App_Perf_Section_t section, uint32_t start_cycles)
{
    App_Perf_SectionStat_t *st;  /* 目标区间统计指针 */
    uint32_t now;                /* 当前 CYCCNT */
    uint32_t delta;              /* 本次区间耗时（周期数） */

    /* 前置检查：统计未开启、DWT 未就绪、区间索引越界，均直接返回 */
    if ((s_perf_enabled == 0u) ||
        (s_perf_dwt_ready == 0u) ||
        (section >= APP_PERF_SEC_COUNT))
    {
        return;
    }

    now   = DWT->CYCCNT;           /* 读取结束时刻 */
    delta = now - start_cycles;    /* 无符号减法，自动处理 CYCCNT 溢出回绕 */
    st    = &s_perf_stats[section]; /* 取目标区间统计结构体指针 */

    st->total_cycles += (uint64_t)delta;  /* 累加到总周期数（64 位，防止溢出） */
    st->sample_count++;                   /* 样本计数+1 */

    if (delta > st->max_cycles)           /* 更新历史最大值 */
    {
        st->max_cycles = delta;
    }

    st->ring[st->ring_head] = delta;                            /* 写入环形缓冲 */
    st->ring_head = (st->ring_head + 1u) % PERF_RING_SAMPLES;  /* 移动写指针（循环） */
    if (st->ring_count < PERF_RING_SAMPLES)                     /* 若缓冲未满则递增有效数量 */
    {
        st->ring_count++;
    }
    /* 缓冲已满时 ring_count 保持 PERF_RING_SAMPLES，最旧的样本被自动覆盖 */
}

/**
 * @brief   音频处理帧计数递增（每次执行 SRP-PHAT 后调用）
 * @details 与 g_audio_frame_seq_isr 不同，此计数仅在实际跑算法时递增，
 *          可用于计算算法实际执行速率（考虑抽帧后的有效处理率）。
 */
void App_Perf_CountAudioProc(void)
{
    s_perf_audio_proc_count++;  /* 非原子自增，仅在音频任务内调用，无竞争 */
}

/**
 * @brief   UI 循环计数递增（每次 UI 任务完成一帧渲染后调用）
 * @details 用于统计 UI 实际渲染速率，与目标帧率对比可发现帧率不达标的情况。
 */
void App_Perf_CountUiLoop(void)
{
    s_perf_ui_loop_count++;  /* 非原子自增，仅在 UI 任务内调用，无竞争 */
}

/**
 * @brief   按周期打印各模块的实时吞吐率（在 UI 任务循环中调用）
 * @details 每 PERF_RATE_PERIOD_MS (1000ms) 打印一次，通过差分计算速率：
 *            rate = (当前计数 - 上次基准) / 经过时间(秒)
 *          输出格式：
 *            perf rate isr=X.X proc=X.X ui=X.X commit=X.X swap=X.X
 *          各字段含义：
 *            isr    = SAI DMA 中断触发速率 (Hz，等于音频采样帧率)
 *            proc   = 音频算法实际执行速率 (Hz，考虑抽帧)
 *            ui     = UI 渲染循环速率 (Hz)
 *            commit = LTDC 换页请求速率 (Hz)
 *            swap   = LTDC 实际换页完成速率 (Hz，应与 commit 接近)
 *
 * @note    在性能统计关闭时（s_perf_enabled==0）为纯空操作，无任何开销。
 */
void App_Perf_MaybePrintRates(void)
{
    uint32_t now_tick;      /* 当前 FreeRTOS tick 值 */
    uint32_t elapsed_tick;  /* 距上次打印经过的 tick 数 */
    uint32_t elapsed_ms;    /* 换算为毫秒 */
    uint32_t audio_isr;     /* 当前 ISR 帧序号快照 */
    uint32_t audio_proc;    /* 当前音频处理帧计数快照 */
    uint32_t ui_loop;       /* 当前 UI 循环计数快照 */
    uint32_t commit;        /* 当前 LTDC 提交请求计数快照 */
    uint32_t swap;          /* 当前 LTDC 换页完成计数快照 */
    double   scale;         /* 1000 / elapsed_ms，用于将差值换算为每秒速率 */

    if (s_perf_enabled == 0u)   /* 统计未开启，立即退出，不读任何计数器 */
    {
        return;
    }

    now_tick     = xTaskGetTickCount();           /* 读取当前滴答计数 */
    elapsed_tick = now_tick - s_perf_last_tick;   /* 无符号差，自动处理溢出回绕 */

    if (elapsed_tick < pdMS_TO_TICKS(PERF_RATE_PERIOD_MS)) /* 未到打印周期，跳过 */
    {
        return;
    }

    elapsed_ms = elapsed_tick * portTICK_PERIOD_MS; /* tick 转毫秒 */
    if (elapsed_ms == 0u)   /* 防止除零（理论上不可能，但作为保护） */
    {
        elapsed_ms = 1u;
    }

    /* 一次性读取各计数器快照，减少多次读取的时间差误差 */
    audio_isr  = g_audio_frame_seq_isr;
    audio_proc = s_perf_audio_proc_count;
    ui_loop    = s_perf_ui_loop_count;
    commit     = g_ltdc_swap_pending_count;
    swap       = g_ltdc_swap_count;

    scale = 1000.0 / (double)elapsed_ms;  /* 换算因子：差值 * scale = 每秒次数 */

    /* 打印速率统计行，一行输出便于串口工具解析 */
    printf("perf rate isr=%.1f proc=%.1f ui=%.1f commit=%.1f swap=%.1f\r\n",
           (double)(audio_isr  - s_perf_last_audio_isr)  * scale, /* SAI ISR 速率 */
           (double)(audio_proc - s_perf_last_audio_proc) * scale, /* 算法执行速率 */
           (double)(ui_loop    - s_perf_last_ui_loop)    * scale, /* UI 渲染速率 */
           (double)(commit     - s_perf_last_commit)     * scale, /* LTDC 提交速率 */
           (double)(swap       - s_perf_last_swap)       * scale);/* LTDC 换页速率 */

    /* 更新基准值为本次快照，供下次差分使用 */
    s_perf_last_tick       = now_tick;
    s_perf_last_audio_isr  = audio_isr;
    s_perf_last_audio_proc = audio_proc;
    s_perf_last_ui_loop    = ui_loop;
    s_perf_last_commit     = commit;
    s_perf_last_swap       = swap;
}

/**
 * @brief   转储所有剖析区间的统计数据（通过 UART 打印）
 * @details 对每个有样本的区间输出：
 *            perf <name> n=<总样本数> avg=<均值ms> p95=<95百分位ms> max=<最大值ms>
 *
 *          p95 计算方法：
 *            1. 从环形缓冲拷贝最近 N 个样本到临时数组 tmp[]
 *            2. qsort 升序排序
 *            3. 取第 ceil(N*0.95) 个元素作为 p95
 *          p95 比最大值更能反映"稳定最坏情况"，过滤偶发毛刺。
 *
 * @note    可通过 CLI 'cfg perf dump' 触发，会阻塞 printf 若干毫秒，
 *          建议仅在调试阶段使用。
 */
void App_Perf_Dump(void)
{
    uint32_t i;
    uint32_t core_hz = SystemCoreClock;  /* 读取系统主频，用于周期 -> 毫秒换算 */

    if (core_hz == 0u)          /* SystemCoreClock 未初始化的安全回退 */
    {
        core_hz = 480000000u;   /* 假设 480 MHz（STM32H7 典型主频） */
    }

    /* 打印当前性能配置头部信息，方便对比不同配置下的性能数据 */
    printf("perf cfg enabled=%u dwt=%u uifps=%lu decim=%lu core=%lu\r\n",
           (unsigned int)s_perf_enabled,
           (unsigned int)s_perf_dwt_ready,
           (unsigned long)App_RuntimeConfig_GetUiTargetFps(),   /* 目标帧率 */
           (unsigned long)App_RuntimeConfig_GetAudioAlgoDecim(), /* 算法抽帧比 */
           (unsigned long)core_hz);                             /* 主频 */

    /* 遍历所有剖析区间，输出统计结果 */
    for (i = 0u; i < APP_PERF_SEC_COUNT; i++)
    {
        App_Perf_SectionStat_t *st = &s_perf_stats[i];  /* 当前区间统计指针 */
        uint32_t n          = st->sample_count;          /* 总样本数 */
        uint32_t max_cycles = st->max_cycles;            /* 历史最大耗时（周期） */
        uint32_t p95_cycles = 0u;                        /* p95 耗时（周期），下方计算 */
        double   avg_cycles;                             /* 均值（周期） */
        double   avg_ms;                                 /* 均值（毫秒） */
        double   p95_ms;                                 /* p95（毫秒） */
        double   max_ms;                                 /* 最大值（毫秒） */

        if (n == 0u)                /* 此区间无任何样本，跳过输出 */
        {
            continue;
        }

        if (st->ring_count != 0u)   /* 环形缓冲有数据时才计算 p95 */
        {
            uint32_t tmp[PERF_RING_SAMPLES];  /* 临时数组，用于排序 */
            uint32_t j;
            uint32_t p95_rank;                /* p95 样本的排名（1-based） */

            /* 从环形缓冲按时间顺序（最旧到最新）拷贝到线性数组 */
            for (j = 0u; j < st->ring_count; j++)
            {
                /* 计算最旧样本的索引：ring_head 是下一个写入位置，往前退 ring_count 步 */
                uint32_t idx = (st->ring_head + PERF_RING_SAMPLES - st->ring_count + j)
                               % PERF_RING_SAMPLES;
                tmp[j] = st->ring[idx];
            }

            qsort(tmp, st->ring_count, sizeof(uint32_t), s_u32_cmp); /* 升序排序 */

            /* 计算 p95 排名：ceil(count * 95 / 100)，分子+99 实现向上取整 */
            p95_rank = (st->ring_count * 95u + 99u) / 100u;
            if (p95_rank == 0u)                   /* 最小取 1（至少取第一个样本） */
            {
                p95_rank = 1u;
            }
            if (p95_rank > st->ring_count)        /* 不超出有效数据范围 */
            {
                p95_rank = st->ring_count;
            }
            p95_cycles = tmp[p95_rank - 1u];      /* 取排序后第 p95_rank 个元素（0-based） */
        }

        /* 周期数换算为毫秒：ms = cycles * 1000 / core_hz */
        avg_cycles = (double)st->total_cycles / (double)n;          /* 均值周期 */
        avg_ms     = (avg_cycles             * 1000.0) / (double)core_hz; /* 均值毫秒 */
        p95_ms     = ((double)p95_cycles     * 1000.0) / (double)core_hz; /* p95 毫秒 */
        max_ms     = ((double)max_cycles     * 1000.0) / (double)core_hz; /* 最大值毫秒 */

        /* 输出单行统计结果，%-12s 左对齐名称保证列对齐 */
        printf("perf %-12s n=%lu avg=%.3fms p95=%.3fms max=%.3fms\r\n",
               s_perf_section_names[i],   /* 区间名称 */
               (unsigned long)n,           /* 总样本数 */
               avg_ms,                     /* 平均耗时 */
               p95_ms,                     /* p95 耗时 */
               max_ms);                    /* 最大耗时 */
    }
}

/**
 * @brief   计算 UI 任务的渲染周期（FreeRTOS ticks）
 * @details 将目标帧率换算为 vTaskDelayUntil 的周期 ticks：
 *            period_ms = round(1000 / fps)  （四舍五入：加 fps/2 再除以 fps）
 *            ticks = pdMS_TO_TICKS(period_ms)
 *
 *          示例：fps=20 -> period_ms=50ms -> 50 ticks (1ms/tick)
 *                fps=30 -> period_ms=33ms -> 33 ticks
 *                fps=5  -> period_ms=200ms -> 200 ticks
 *
 *          保证 ticks >= 1，防止 vTaskDelayUntil(0) 导致任务不让出 CPU。
 *
 * @return  渲染周期，单位：FreeRTOS ticks
 */
static uint32_t s_ui_period_ticks(void)
{
    /* 读取目标帧率并钳位，防止运行时配置被设置到范围外 */
    uint32_t fps = s_clamp_u32(App_RuntimeConfig_GetUiTargetFps(), UI_FPS_MIN, UI_FPS_MAX);

    /* 四舍五入换算：加 fps/2 再整除，相当于对 1000/fps 做四舍五入 */
    uint32_t period_ms = (1000u + (fps / 2u)) / fps;

    TickType_t ticks = pdMS_TO_TICKS(period_ms);  /* 毫秒转 FreeRTOS ticks */

    if (ticks == 0u)    /* 极端情况保护（portTICK_PERIOD_MS > period_ms 时可能为 0） */
    {
        ticks = 1u;     /* 至少延迟 1 个 tick，保证任务出让 CPU */
    }
    return (uint32_t)ticks;
}

#if (UI_CLI_ENABLE != 0u)
/* ============================================================================
 * CLI 辅助工具函数（仅在 UI_CLI_ENABLE 宏为非零时编译）
 * ============================================================================ */

/**
 * @brief   大小写不敏感字符串比较（替代非标准 strcasecmp）
 * @details 按 C 标准 strcmp 约定返回：负数/0/正数。
 *          使用 tolower() 对每个字符逐一比较，支持 ASCII 字母大小写混合输入。
 *          NULL 指针视为小于任何非 NULL 字符串（返回 -1）。
 *
 * @param   a  第一个字符串
 * @param   b  第二个字符串
 * @return  比较结果（<0 / 0 / >0）
 */
static int ui_cli_stricmp(const char *a, const char *b)
{
    unsigned char ca;  /* a 当前字符转小写后的值 */
    unsigned char cb;  /* b 当前字符转小写后的值 */

    if ((a == NULL) || (b == NULL))  /* NULL 指针保护 */
    {
        return -1;
    }

    /* 逐字符比较，直到任意一方遇到终止符 */
    while ((*a != '\0') && (*b != '\0'))
    {
        ca = (unsigned char)tolower((unsigned char)*a);  /* 转小写 */
        cb = (unsigned char)tolower((unsigned char)*b);  /* 转小写 */
        if (ca != cb)          /* 字符不同，返回差值 */
        {
            return (int)ca - (int)cb;
        }
        a++;  /* 移动到下一个字符 */
        b++;
    }

    /* 循环结束时，至少一方遇到了 '\0'；用终止符的 ASCII 值做最终比较 */
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/**
 * @brief   从字符串解析浮点数（严格解析，拒绝尾随非空字符）
 * @details 解析规则：
 *          1. 跳过前导空白
 *          2. 调用 strtof 解析数值
 *          3. 跳过数值后的空白
 *          4. 若剩余字符不为空则视为解析失败（防止 "1.5abc" 被误接受）
 *
 * @param   s    输入字符串
 * @param   out  解析成功时写入浮点值
 * @return  1=解析成功，0=失败（NULL/空串/格式错误/尾随非空字符）
 */
static uint8_t ui_cli_parse_float(const char *s, float *out)
{
    char  *endptr;  /* strtof 解析结束位置 */
    float  v;       /* 解析得到的浮点值 */

    if ((s == NULL) || (out == NULL))  /* 空指针保护 */
    {
        return 0u;
    }

    while (isspace((unsigned char)*s) != 0)  /* 跳过前导空白（空格/制表符等） */
    {
        s++;
    }
    if (*s == '\0')  /* 纯空白字符串 */
    {
        return 0u;
    }

    v = strtof(s, &endptr);   /* 尝试解析浮点数，endptr 指向解析停止位置 */
    if (s == endptr)           /* strtof 未消耗任何字符，说明格式错误 */
    {
        return 0u;
    }

    while (isspace((unsigned char)*endptr) != 0)  /* 跳过数值后的空白 */
    {
        endptr++;
    }
    if (*endptr != '\0')  /* 仍有非空字符，说明存在尾随垃圾（严格解析） */
    {
        return 0u;
    }

    *out = v;    /* 写出解析结果 */
    return 1u;   /* 解析成功 */
}

/**
 * @brief   从字符串解析无符号 32 位整数（十进制，严格解析）
 * @details 与 ui_cli_parse_float 逻辑相同，使用 strtoul 解析十进制整数。
 *          同样拒绝尾随非空字符，防止 "123abc" 被解析为 123。
 *
 * @param   s    输入字符串
 * @param   out  解析成功时写入 uint32_t 值
 * @return  1=解析成功，0=失败
 */
static uint8_t ui_cli_parse_u32(const char *s, uint32_t *out)
{
    char          *endptr;  /* strtoul 解析结束位置 */
    unsigned long  v;       /* 解析得到的无符号长整数 */

    if ((s == NULL) || (out == NULL))  /* 空指针保护 */
    {
        return 0u;
    }

    while (isspace((unsigned char)*s) != 0)  /* 跳过前导空白 */
    {
        s++;
    }
    if (*s == '\0')  /* 纯空白或空串 */
    {
        return 0u;
    }

    v = strtoul(s, &endptr, 10);  /* 十进制解析，endptr 指向结束位置 */
    if (s == endptr)               /* 无有效数字字符 */
    {
        return 0u;
    }

    while (isspace((unsigned char)*endptr) != 0)  /* 跳过尾随空白 */
    {
        endptr++;
    }
    if (*endptr != '\0')  /* 尾随非空字符，拒绝接受 */
    {
        return 0u;
    }

    *out = (uint32_t)v;  /* 截断到 32 位后写出 */
    return 1u;
}

/* -------------------------------------------------------------------------- */
/* CLI UART 接收环形缓冲区（ISR -> 任务 单生产者单消费者无锁设计）              */
/* -------------------------------------------------------------------------- */

/** @brief 环形缓冲写指针（ISR 侧写，任务侧读，需 volatile）*/
static volatile uint16_t s_ui_cli_rx_wr = 0u;
/** @brief 环形缓冲读指针（任务侧读写，需 volatile 防止编译器优化）*/
static volatile uint16_t s_ui_cli_rx_rd = 0u;
/** @brief UART 中断接收是否已挂载标志（1=已调用 HAL_UART_Receive_IT，0=未挂载）*/
static volatile uint8_t  s_ui_cli_rx_armed = 0u;
/** @brief 重新挂载标志（ISR 出错后由 ErrorCallback 设置，要求下次 poll 时恢复）*/
static volatile uint8_t  s_ui_cli_rx_need_rearm = 1u;
/** @brief 因环形缓冲满而丢弃的字节计数（可通过 cfg status 查看）*/
static volatile uint32_t s_ui_cli_rx_drop_count = 0u;
/** @brief HAL_UART_Receive_IT 的目标缓冲区（每次接收 1 字节）*/
static uint8_t s_ui_cli_rx_byte = 0u;
/** @brief UART 接收环形缓冲区数据区（1024 字节，约可存 10+ 条命令）*/
static uint8_t s_ui_cli_rx_ring[UI_CLI_RX_RING_SIZE];

/**
 * @brief   从 ISR 向环形缓冲压入一个字节（中断上下文调用）
 * @details 单生产者（ISR）写入，写指针 s_ui_cli_rx_wr 只在此函数中修改。
 *          若缓冲区已满（next == rd），丢弃字节并累加 drop 计数。
 *          写操作是原子的（16 位写在 Cortex-M 上是原子的），无需加锁。
 *
 * @param   ch  待压入的字节
 */
static void ui_cli_ring_push_from_isr(uint8_t ch)
{
    uint16_t wr   = s_ui_cli_rx_wr;           /* 读取当前写指针（只有 ISR 写，无竞争） */
    uint16_t next = (uint16_t)(wr + 1u);      /* 计算写入后的新写指针 */

    if (next >= UI_CLI_RX_RING_SIZE)          /* 到达缓冲区末尾，回绕到 0 */
    {
        next = 0u;
    }

    if (next == s_ui_cli_rx_rd)               /* 缓冲区已满（写追上读） */
    {
        s_ui_cli_rx_drop_count++;             /* 累计丢弃计数 */
        g_ui_cli_rx_err_count++;             /* 同步到全局错误计数（供 cfg status 显示） */
        return;                               /* 丢弃当前字节 */
    }

    s_ui_cli_rx_ring[wr] = ch;               /* 写入数据到当前写位置 */
    s_ui_cli_rx_wr       = next;             /* 更新写指针（对任务侧可见） */
    g_ui_cli_rx_ok_count++;                  /* 累计成功接收字节数 */
}

/**
 * @brief   从环形缓冲弹出一个字节（任务上下文调用）
 * @details 单消费者（UI 任务）读取，读指针 s_ui_cli_rx_rd 只在此函数中修改。
 *          使用临界区保护读指针更新（防止 ISR 在读取过程中并发写入造成竞争）。
 *
 * @param   out  输出参数，成功时写入弹出的字节
 * @return  1=成功弹出，0=缓冲区为空
 */
static uint8_t ui_cli_ring_pop(uint8_t *out)
{
    uint16_t rd;  /* 当前读指针 */

    if (out == NULL)  /* 空指针保护 */
    {
        return 0u;
    }

    taskENTER_CRITICAL();              /* 进入临界区：防止 ISR 在读期间修改 wr */
    rd = s_ui_cli_rx_rd;              /* 读取当前读指针 */
    if (rd == s_ui_cli_rx_wr)        /* 读写指针相同，缓冲区为空 */
    {
        taskEXIT_CRITICAL();
        return 0u;
    }

    *out = s_ui_cli_rx_ring[rd];     /* 从读位置取出一个字节 */
    rd   = (uint16_t)(rd + 1u);      /* 移动读指针 */
    if (rd >= UI_CLI_RX_RING_SIZE)   /* 到达缓冲区末尾，回绕 */
    {
        rd = 0u;
    }
    s_ui_cli_rx_rd = rd;             /* 更新读指针（对 ISR 可见，允许更多写入） */
    taskEXIT_CRITICAL();
    return 1u;                        /* 成功弹出 */
}

/**
 * @brief   UART 错误恢复（中止当前接收并清除错误标志）
 * @details 在 UART 发生 ORE（溢出）/NE（噪声）/FE（帧）/PE（奇偶）错误后调用。
 *          流程：先 AbortReceive 停止当前传输，再清除 HAL 错误标志，
 *          最后清除 armed 标志使 kick_rx_it 在下次 poll 时重新挂载接收。
 */
static void ui_cli_uart_recover(void)
{
    if (HAL_UART_GetError(&huart1) == HAL_UART_ERROR_NONE) /* 无错误，无需恢复 */
    {
        s_ui_cli_rx_armed = 0u;  /* 清除 armed，触发重新挂载（防止 HAL 内部状态不一致） */
        return;
    }

    /* 中止正在进行的接收操作（HAL 层面） */
    (void)HAL_UART_AbortReceive(&huart1);

    /* 清除所有 UART 错误标志：ORE/NE/FE/PE */
    __HAL_UART_CLEAR_FLAG(&huart1,
                          UART_CLEAR_OREF |   /* 溢出错误 */
                          UART_CLEAR_NEF  |   /* 噪声错误 */
                          UART_CLEAR_FEF  |   /* 帧错误 */
                          UART_CLEAR_PEF);    /* 奇偶校验错误 */

    huart1.ErrorCode = HAL_UART_ERROR_NONE;  /* 清除 HAL 错误码，允许后续正常调用 */
    s_ui_cli_rx_armed = 0u;                  /* 清除 armed，下次 kick 时重新挂载 */
}

/**
 * @brief   触发（或维持）UART 中断接收（每次 ui_cli_poll 开头调用）
 * @details 实现自动重挂载机制：
 *          - 若 armed==1，说明已有未完成的接收请求，直接返回。
 *          - 若 need_rearm==1，先执行错误恢复再挂载。
 *          - 调用 HAL_UART_Receive_IT 挂载单字节中断接收。
 *          - 返回 HAL_BUSY 也视为成功（表示 HAL 正在处理中）。
 */
static void ui_cli_uart_kick_rx_it(void)
{
    HAL_StatusTypeDef st;  /* HAL 调用返回值 */

    if (s_ui_cli_rx_armed != 0u)  /* 已挂载，无需重复操作 */
    {
        return;
    }

    if (s_ui_cli_rx_need_rearm != 0u)  /* 需要先恢复错误状态再重挂载 */
    {
        ui_cli_uart_recover();         /* 清除 UART 错误并复位 HAL 状态 */
        s_ui_cli_rx_need_rearm = 0u;   /* 清除重挂载请求标志 */
    }

    /* 挂载单字节中断接收：每收到 1 字节触发 HAL_UART_RxCpltCallback */
    st = HAL_UART_Receive_IT(&huart1, &s_ui_cli_rx_byte, 1u);
    if ((st == HAL_OK) || (st == HAL_BUSY))  /* OK 或 BUSY 均视为挂载成功 */
    {
        s_ui_cli_rx_armed = 1u;         /* 标记已挂载，防止重复调用 */
    }
    else                                 /* HAL_ERROR 或 HAL_TIMEOUT，挂载失败 */
    {
        g_ui_cli_rx_err_count++;        /* 累加错误计数，下次 poll 时继续尝试 */
    }
}

/**
 * @brief   打印 CLI 帮助信息（支持的所有命令及参数格式）
 * @details 通过 'cfg help' 或 'help' 触发，方便用户在串口终端查看所有可用命令。
 */
static void ui_cli_print_help(void)
{
    printf("\r\n");                                    /* 空行分隔，视觉清晰 */
    printf("cfg help\r\n");                            /* 显示本帮助 */
    printf("cfg status\r\n");                          /* 打印所有当前参数值 */
    printf("cfg mode fast|balanced|clean\r\n");        /* 切换渲染模式 */
    printf("cfg interp nearest|bilinear\r\n");         /* 切换插值方式 */
    printf("cfg contrast <db_floor>\r\n");             /* 设置动态范围底限（负 dB 值） */
    printf("cfg gamma <0.5..2.5>\r\n");                /* 设置伽马校正系数 */
    printf("cfg noise <0..0.6>\r\n");                  /* 设置噪声门限比例 */
    printf("cfg adapt <0..6>\r\n");                    /* 设置自适应噪声增益 */
    printf("cfg smooth <0..3>\r\n");                   /* 设置空间平滑迭代次数 */
    printf("cfg fine <0..3>\r\n");                     /* 设置精细网格叠加增益 */
    printf("cfg bilinear <0|1>\r\n");                  /* 快捷开关双线性插值（0=最近邻） */
    printf("cfg norm fast|full\r\n");                  /* 切换归一化策略 */
    printf("cfg textdiv <1..20>\r\n");                 /* 设置文字刷新分频系数 */
    printf("cfg blit <1..8>\r\n");                     /* 设置 DMA2D 每次 blit 行数 */
    printf("cfg uifps <5..30>\r\n");                   /* 设置 UI 目标帧率 */
    printf("cfg algodecim <1..8>\r\n");                /* 设置音频算法抽帧比 */
    printf("cfg perf on|off|dump|reset\r\n");          /* 性能统计开关/打印/重置 */
    printf("cfg uart recover\r\n");                    /* 手动触发 UART 错误恢复 */
}

/**
 * @brief   打印当前所有运行时参数状态（通过 'cfg status' 触发）
 * @details 分四行输出，覆盖：
 *          1. 显示模式与视觉参数（db/gamma/noise/adapt）
 *          2. 渲染参数（smooth/fine/interp/norm/textdiv/blit）
 *          3. 任务参数（uifps/algodecim/perf状态）
 *          4. DMA2D 传输统计（用于诊断 GPU 加速效率）
 *          5. LTDC 换页统计（用于诊断显示撕裂/同步问题）
 *          6. CLI 接收统计（用于诊断串口通信质量）
 */
static void ui_cli_print_status(void)
{
    App_Runtime_DisplayCfg_t  cfg;   /* 读取当前显示参数快照 */
    App_Runtime_DisplayMode_t mode;  /* 读取当前显示模式 */

    App_RuntimeConfig_GetDisplayCfg(&cfg);     /* 线程安全读取显示参数 */
    mode = App_RuntimeConfig_GetDisplayMode(); /* 线程安全读取显示模式 */

    /* 第 1 行：显示模式与主要视觉调节参数 */
    printf("cfg mode=%s\r\n",
           App_Display_ModeName(s_runtime_mode_to_display(mode)));  /* 模式名称字符串 */

    /* 第 2 行：动态范围与噪声抑制参数 */
    printf("cfg db=%.1f gamma=%.2f noise=%.3f adapt=%.2f\r\n",
           (double)cfg.db_floor,         /* 动态范围底限 (dB) */
           (double)cfg.gamma,            /* 伽马校正系数 */
           (double)cfg.noise_gate_ratio, /* 噪声门限比例 */
           (double)cfg.noise_adapt_gain);/* 自适应噪声增益 */

    /* 第 3 行：渲染质量与刷新控制参数 */
    printf("cfg smooth=%u fine=%.2f interp=%s norm=%s textdiv=%u blit=%u\r\n",
           (unsigned int)cfg.smooth_passes,  /* 平滑迭代次数 */
           (double)cfg.fine_gain,            /* 精细增益 */
           /* 插值模式名称：将运行时枚举转回 Display 枚举后获取名称字符串 */
           App_Display_InterpName((cfg.interp_mode == APP_RUNTIME_DISP_INTERP_BILINEAR)
                                      ? APP_DISPLAY_INTERP_BILINEAR
                                      : APP_DISPLAY_INTERP_NEAREST),
           /* 归一化模式名称 */
           App_Display_NormName((cfg.norm_mode == APP_RUNTIME_DISP_NORM_FULL)
                                    ? APP_DISPLAY_NORM_FULL
                                    : APP_DISPLAY_NORM_FAST),
           (unsigned int)cfg.text_refresh_div,  /* 文字刷新分频 */
           (unsigned int)cfg.blit_rows);         /* DMA2D blit 行数 */

    /* 第 4 行：任务调度参数 */
    printf("cfg uifps=%lu algodecim=%lu perf=%s\r\n",
           (unsigned long)App_RuntimeConfig_GetUiTargetFps(),    /* UI 目标帧率 */
           (unsigned long)App_RuntimeConfig_GetAudioAlgoDecim(), /* 算法抽帧比 */
           (App_RuntimeConfig_GetPerfEnabled() != 0u) ? "on" : "off"); /* 性能统计开关 */

    /* 第 5 行：DMA2D GPU 加速传输统计（诊断 LCD blit 效率） */
    printf("dma2d tx=%lu timeout=%lu fallback=%lu qpk=%lu qov=%lu qerr=%lu\r\n",
           (unsigned long)g_ltdc_dma2d_transfer_count,    /* 成功传输次数 */
           (unsigned long)g_ltdc_dma2d_timeout_count,     /* 等待 DMA2D 超时次数 */
           (unsigned long)g_ltdc_dma2d_sw_fallback_count, /* 回退到软件 blit 次数 */
           (unsigned long)g_dma2d_queue_depth_peak,       /* 队列深度历史峰值 */
           (unsigned long)g_dma2d_queue_overflow_count,   /* 队列溢出次数 */
           (unsigned long)g_dma2d_queue_error_count);     /* 队列错误次数 */

    /* 第 6 行：LTDC 帧缓冲换页统计（诊断显示同步） */
    printf("swap done=%lu pend_req=%lu err=%lu pending=%u\r\n",
           (unsigned long)g_ltdc_swap_count,         /* 已完成换页次数 */
           (unsigned long)g_ltdc_swap_pending_count, /* 已发出换页请求次数 */
           (unsigned long)g_ltdc_swap_error_count,   /* 换页错误次数 */
           (unsigned int)ltdc_is_swap_pending());    /* 当前是否有待处理换页请求 */

    /* 第 7 行：CLI UART 接收统计（诊断串口通信质量） */
    printf("cli rx_ok=%lu rx_err=%lu rx_drop=%lu alive=%u uart_err=0x%08lX baud=%lu\r\n",
           (unsigned long)g_ui_cli_rx_ok_count,           /* 成功接收字节数 */
           (unsigned long)g_ui_cli_rx_err_count,          /* UART 错误次数 */
           (unsigned long)s_ui_cli_rx_drop_count,         /* 环形缓冲满丢弃字节数 */
           (unsigned int)g_ui_cli_rx_alive,               /* CLI 活跃标志（2s内有数据=1） */
           (unsigned long)HAL_UART_GetError(&huart1),     /* HAL UART 当前错误码 */
           (unsigned long)huart1.Init.BaudRate);          /* 当前波特率 */
}

/**
 * @brief   解析并执行一条完整的 CLI 命令行
 * @details 命令格式：  cfg <key> [value]
 *          解析流程：
 *          1. 去除行首/行尾空白（trim）
 *          2. 快速路径：识别 "help" 和 "cfg status" 整串命令
 *          3. 验证命令前缀为 "cfg "（大小写不敏感）
 *          4. 分割 <key> 和 [value]（以空白为分隔符）
 *          5. 分发到对应的参数处理分支
 *
 * @param   line  以 '\0' 结尾的命令行字符串（会被就地修改：空白被 '\0' 覆盖）
 */
static void ui_cli_apply_line(char *line)
{
    char    *cursor;           /* 当前解析位置指针 */
    char    *arg    = NULL;    /* 指向参数值字符串（key 后的部分），无参数时为 NULL */
    char    *tail;             /* 用于去除行尾空白 */
    App_Runtime_DisplayCfg_t cfg;  /* 修改显示配置时的临时工作副本 */
    float    fv;               /* 浮点参数解析结果 */
    uint32_t uv;               /* 无符号整数参数解析结果 */

    if (line == NULL)  /* 空指针保护 */
    {
        return;
    }

    /* ---- 步骤 1：trim 行首空白 ---- */
    cursor = line;
    while (isspace((unsigned char)*cursor) != 0)  /* 跳过空格/制表符等前导字符 */
    {
        cursor++;
    }

    /* ---- 步骤 2：trim 行尾空白（就地修改字符串，用 '\0' 替换尾部空白） ---- */
    tail = cursor + strlen(cursor);           /* 指向字符串末尾 '\0' */
    while ((tail > cursor) && (isspace((unsigned char)tail[-1]) != 0))
    {
        *--tail = '\0';                       /* 覆盖尾部空白字符 */
    }

    if (*cursor == '\0')  /* trim 后为空串（原始输入全是空白），忽略 */
    {
        return;
    }

    /* ---- 步骤 3：快速路径识别 "help" 和 "cfg help" ---- */
    if ((ui_cli_stricmp(cursor, "help") == 0) ||
        (ui_cli_stricmp(cursor, "cfg help") == 0))
    {
        ui_cli_print_help();
        return;
    }
    if (ui_cli_stricmp(cursor, "cfg status") == 0)  /* 整串匹配 "cfg status" */
    {
        ui_cli_print_status();
        return;
    }

    /* ---- 步骤 4：验证命令前缀为 "cfg " ---- */
    if ((tolower((unsigned char)cursor[0]) != 'c') ||  /* 第 1 字符必须是 'c'/'C' */
        (tolower((unsigned char)cursor[1]) != 'f') ||  /* 第 2 字符必须是 'f'/'F' */
        (tolower((unsigned char)cursor[2]) != 'g') ||  /* 第 3 字符必须是 'g'/'G' */
        (isspace((unsigned char)cursor[3]) == 0))      /* 第 4 字符必须是空白分隔符 */
    {
        printf("CLI: unknown command, type 'cfg help'\r\n");
        return;
    }

    cursor += 3;  /* 跳过 "cfg" 三个字符 */
    while (isspace((unsigned char)*cursor) != 0)  /* 跳过 cfg 与 key 之间的空白 */
    {
        cursor++;
    }
    if (*cursor == '\0')  /* cfg 后面没有任何 key，显示帮助 */
    {
        ui_cli_print_help();
        return;
    }

    /* ---- 步骤 5：分割 <key> 和 [value] ---- */
    arg = cursor;
    while ((*arg != '\0') && (isspace((unsigned char)*arg) == 0))
    {
        arg++;  /* arg 向后扫描直到空白或串尾，cursor 到 arg 之间就是 key */
    }
    if (*arg != '\0')        /* 在 key 后找到了空白，说明可能有参数值 */
    {
        *arg++ = '\0';       /* 将空白替换为 '\0'，分割 key 字符串 */
        while (isspace((unsigned char)*arg) != 0)  /* 跳过 key 与 value 之间的空白 */
        {
            arg++;
        }
        if (*arg == '\0')    /* 空白后仍无内容，value 为空 */
        {
            arg = NULL;
        }
        /* 否则 arg 指向 value 字符串 */
    }
    else
    {
        arg = NULL;  /* 没有找到空白，key 后无参数 */
    }

    /* ---- 步骤 6：命令分发（key 已在 cursor 中，value 在 arg 中或为 NULL） ---- */

    /* -- cfg help / cfg status（key 单独出现时的第二次检查） -- */
    if (ui_cli_stricmp(cursor, "help") == 0)    /* "cfg help" */
    {
        ui_cli_print_help();
        return;
    }
    if (ui_cli_stricmp(cursor, "status") == 0)  /* "cfg status" */
    {
        ui_cli_print_status();
        return;
    }

    /* -- cfg mode fast|balanced|clean：切换渲染模式 -- */
    if (ui_cli_stricmp(cursor, "mode") == 0)
    {
        if (arg == NULL)  /* 缺少参数，打印用法 */
        {
            printf("CLI: cfg mode fast|balanced|clean\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "fast") == 0)                                     /* 快速模式 */
        {
            App_RuntimeConfig_SetDisplayMode(APP_RUNTIME_DISP_MODE_FAST);
        }
        else if ((ui_cli_stricmp(arg, "balanced") == 0) ||
                 (ui_cli_stricmp(arg, "bal") == 0))                               /* 均衡模式（支持缩写 bal） */
        {
            App_RuntimeConfig_SetDisplayMode(APP_RUNTIME_DISP_MODE_BALANCED);
        }
        else if (ui_cli_stricmp(arg, "clean") == 0)                               /* 清晰模式 */
        {
            App_RuntimeConfig_SetDisplayMode(APP_RUNTIME_DISP_MODE_CLEAN);
        }
        else
        {
            printf("CLI: invalid mode\r\n");
            return;
        }
        ui_cli_print_status();  /* 修改成功后打印新状态确认 */
        return;
    }

    /* -- cfg interp nearest|bilinear：切换热力图插值方式 -- */
    if (ui_cli_stricmp(cursor, "interp") == 0)
    {
        App_RuntimeConfig_GetDisplayCfg(&cfg);   /* 先读取当前配置，只修改 interp_mode */
        if (arg == NULL)
        {
            printf("CLI: cfg interp nearest|bilinear\r\n");
            return;
        }
        if ((ui_cli_stricmp(arg, "nearest") == 0) ||
            (ui_cli_stricmp(arg, "near") == 0))                /* 最近邻插值（支持缩写 near） */
        {
            cfg.interp_mode = APP_RUNTIME_DISP_INTERP_NEAREST;
        }
        else if ((ui_cli_stricmp(arg, "bilinear") == 0) ||
                 (ui_cli_stricmp(arg, "bil") == 0))            /* 双线性插值（支持缩写 bil） */
        {
            cfg.interp_mode = APP_RUNTIME_DISP_INTERP_BILINEAR;
        }
        else
        {
            printf("CLI: cfg interp nearest|bilinear\r\n");
            return;
        }
        App_RuntimeConfig_SetDisplayCfg(&cfg);  /* 整体写回（其他字段不变） */
        ui_cli_print_status();
        return;
    }

    /* -- cfg norm fast|full：切换归一化策略 -- */
    if (ui_cli_stricmp(cursor, "norm") == 0)
    {
        App_RuntimeConfig_GetDisplayCfg(&cfg);   /* 先读当前配置 */
        if (arg == NULL)
        {
            printf("CLI: cfg norm fast|full\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "fast") == 0)    /* 快速归一化：仅用峰值，速度快 */
        {
            cfg.norm_mode = APP_RUNTIME_DISP_NORM_FAST;
        }
        else if (ui_cli_stricmp(arg, "full") == 0) /* 完整归一化：全局最大值，效果更准确 */
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

    /* -- cfg uifps <5..30>：修改 UI 目标帧率 -- */
    if (ui_cli_stricmp(cursor, "uifps") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))  /* 参数缺失或格式错误 */
        {
            printf("CLI: cfg uifps <5..30>\r\n");
            return;
        }
        App_RuntimeConfig_SetUiTargetFps(uv);  /* 内部自动 clamp 到 [5,30] */
        ui_cli_print_status();
        return;
    }

    /* -- cfg algodecim <1..8>：修改音频算法抽帧比 -- */
    if (ui_cli_stricmp(cursor, "algodecim") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg algodecim <1..8>\r\n");
            return;
        }
        App_RuntimeConfig_SetAudioAlgoDecim(uv);  /* 内部自动 clamp 到 [1,8] */
        ui_cli_print_status();
        return;
    }

    /* -- cfg perf on|off|dump|reset：性能统计控制 -- */
    if (ui_cli_stricmp(cursor, "perf") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg perf on|off|dump|reset\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "on") == 0)        /* 开启性能统计（尝试使能 DWT） */
        {
            App_RuntimeConfig_SetPerfEnabled(1u);
        }
        else if (ui_cli_stricmp(arg, "off") == 0)  /* 关闭性能统计 */
        {
            App_RuntimeConfig_SetPerfEnabled(0u);
        }
        else if (ui_cli_stricmp(arg, "reset") == 0) /* 清零所有统计数据 */
        {
            App_Perf_Reset();
        }
        else if (ui_cli_stricmp(arg, "dump") == 0)  /* 打印所有区间统计结果 */
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

    /* ---- 以下命令需要读取并修改显示配置，先取一次快照 ---- */
    App_RuntimeConfig_GetDisplayCfg(&cfg);  /* 读取当前配置到本地副本，下方按需修改单个字段 */

    /* -- cfg contrast <db_floor>：设置动态范围底限（应为负数，单位 dB） -- */
    if (ui_cli_stricmp(cursor, "contrast") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))  /* 参数缺失或格式错误 */
        {
            printf("CLI: cfg contrast <-6..-80>\r\n");
            return;
        }
        if (fv > 0.0f)   /* 用户输入了正数，自动取负（容错处理，负号可省略） */
        {
            fv = -fv;
        }
        cfg.db_floor = fv;  /* 修改动态范围底限 */
    }
    /* -- cfg gamma <0.5..2.5>：设置伽马校正系数 -- */
    else if (ui_cli_stricmp(cursor, "gamma") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg gamma <0.5..2.5>\r\n");
            return;
        }
        cfg.gamma = fv;  /* 修改伽马系数（建议范围 0.5~2.5，<1 压缩高亮，>1 提升对比度） */
    }
    /* -- cfg noise <0..0.6>：设置噪声门限比例 -- */
    else if (ui_cli_stricmp(cursor, "noise") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg noise <0..0.6>\r\n");
            return;
        }
        cfg.noise_gate_ratio = fv;  /* 低于此比例的能量视为噪声，不显示热点 */
    }
    /* -- cfg adapt <0..6>：设置自适应噪声估计增益 -- */
    else if (ui_cli_stricmp(cursor, "adapt") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg adapt <0..6>\r\n");
            return;
        }
        cfg.noise_adapt_gain = fv;  /* 增大此值使自适应噪声估计更激进（抑制更多背景噪声） */
    }
    /* -- cfg smooth <0..3>：设置空间平滑迭代次数 -- */
    else if (ui_cli_stricmp(cursor, "smooth") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg smooth <0..3>\r\n");
            return;
        }
        cfg.smooth_passes = (uint8_t)uv;  /* 0=不平滑（最尖锐），3=三次平滑（最柔和） */
    }
    /* -- cfg fine <0..3>：设置精细网格叠加增益 -- */
    else if (ui_cli_stricmp(cursor, "fine") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_float(arg, &fv) == 0u))
        {
            printf("CLI: cfg fine <0..3>\r\n");
            return;
        }
        cfg.fine_gain = fv;  /* 0=不叠加精细网格，>0 叠加（增强角度分辨率细节） */
    }
    /* -- cfg bilinear <0|1>：快捷开关双线性插值 -- */
    else if (ui_cli_stricmp(cursor, "bilinear") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg bilinear <0|1>\r\n");
            return;
        }
        /* 0 = 最近邻插值（速度快，有锯齿），1 = 双线性插值（更平滑，略慢） */
        cfg.interp_mode = (uv != 0u) ? APP_RUNTIME_DISP_INTERP_BILINEAR
                                      : APP_RUNTIME_DISP_INTERP_NEAREST;
    }
    /* -- cfg textdiv <1..20>：设置文字覆盖层刷新分频 -- */
    else if (ui_cli_stricmp(cursor, "textdiv") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg textdiv <1..20>\r\n");
            return;
        }
        cfg.text_refresh_div = (uint8_t)uv;  /* 每 N 帧刷新一次文字，降低文字渲染 CPU 占用 */
    }
    /* -- cfg blit <1..8>：设置 DMA2D 每次 blit 的最大行数 -- */
    else if (ui_cli_stricmp(cursor, "blit") == 0)
    {
        if ((arg == NULL) || (ui_cli_parse_u32(arg, &uv) == 0u))
        {
            printf("CLI: cfg blit <1..8>\r\n");
            return;
        }
        cfg.blit_rows = (uint8_t)uv;  /* 增大可提高吞吐率，减小可降低单次 DMA 延迟 */
    }
    else  /* 未知的 cfg key */
    {
        printf("CLI: unknown cfg key\r\n");
        return;
    }

    /* 将修改后的配置整体写回（线程安全，内部使用临界区保护） */
    App_RuntimeConfig_SetDisplayCfg(&cfg);
    ui_cli_print_status();  /* 打印新配置确认修改生效 */
}

/**
 * @brief   CLI 轮询函数（在 UI 任务每次循环开头调用）
 * @details 执行以下操作：
 *          1. 首次调用时打印欢迎 banner（告知波特率和帮助命令）
 *          2. 触发/维持 UART 中断接收
 *          3. 从环形缓冲消耗最多 UI_CLI_RX_DRAIN_MAX 字节
 *          4. 将字节组装成行缓冲，遇到 CR/LF 时解析并执行命令
 *          5. 更新 CLI 活跃标志（2 秒内有数据 = 活跃）
 *
 * @note    每次调用限制消耗字节数（UI_CLI_RX_DRAIN_MAX=256），
 *          防止大量 CLI 数据堵塞 UI 渲染循环。
 */
static void ui_cli_poll(void)
{
    static char      line_buf[UI_CLI_LINE_MAX]; /**< 行缓冲区（跨调用保留未完成的行） */
    static uint16_t  line_len       = 0u;       /**< 当前行已接收字节数 */
    static uint8_t   banner_printed = 0u;       /**< banner 是否已打印（只打印一次） */
    static TickType_t last_rx_tick  = 0u;       /**< 上次收到有效字节的 tick 值（活跃检测用）*/
    uint32_t i;   /* 循环计数，限制每次最多消耗 UI_CLI_RX_DRAIN_MAX 字节 */
    uint8_t  ch;  /* 从环形缓冲弹出的单个字节 */

    /* 首次调用：打印 CLI 就绪提示（含波特率），方便用户确认串口连接正常 */
    if (banner_printed == 0u)
    {
        banner_printed = 1u;
        printf("UI CLI ready @%lu baud, type 'cfg help'\r\n",
               (unsigned long)huart1.Init.BaudRate);
    }

    ui_cli_uart_kick_rx_it();  /* 确保 UART 中断接收处于挂载状态 */

    /* 每次最多消耗 UI_CLI_RX_DRAIN_MAX 字节，防止 CLI 饿死渲染循环 */
    for (i = 0u; i < UI_CLI_RX_DRAIN_MAX; i++)
    {
        if (ui_cli_ring_pop(&ch) == 0u)  /* 环形缓冲已空，退出消耗循环 */
        {
            break;
        }

        last_rx_tick = xTaskGetTickCount();  /* 记录最近一次收到有效字节的时刻 */

        /* CR 或 LF：行结束符，触发命令解析 */
        if ((ch == '\r') || (ch == '\n'))
        {
            if (line_len != 0u)           /* 忽略空行（连续 CR/LF 序列的后续字符） */
            {
                line_buf[line_len] = '\0';         /* 添加字符串终止符 */
                ui_cli_apply_line(line_buf);       /* 解析并执行命令 */
                line_len = 0u;                     /* 清空行缓冲，准备接收下一行 */
            }
            continue;
        }

        /* BS (0x08) 或 DEL (0x7F)：退格键，删除最后一个字符 */
        if ((ch == 0x08u) || (ch == 0x7Fu))
        {
            if (line_len != 0u)  /* 缓冲非空才退格 */
            {
                line_len--;
            }
            continue;
        }

        /* 可打印 ASCII 字符（0x20~0x7E）：追加到行缓冲 */
        if ((ch >= 32u) && (ch <= 126u))
        {
            if (line_len < (uint16_t)(UI_CLI_LINE_MAX - 1u))  /* 防止行缓冲溢出（留 1 字节给 '\0'） */
            {
                line_buf[line_len++] = (char)ch;  /* 追加字符 */
            }
            /* 超出行长度限制的字符被静默丢弃，不累计错误计数 */
        }
        /* 其他控制字符（非 CR/LF/BS/DEL）静默忽略 */
    }

    /* 更新 CLI 活跃标志：2 秒内收到过有效字节 = 活跃 */
    if ((last_rx_tick != 0u) &&
        ((xTaskGetTickCount() - last_rx_tick) <= pdMS_TO_TICKS(2000u)))
    {
        g_ui_cli_rx_alive = 1u;  /* 活跃：串口另一端有用户在操作 */
    }
    else
    {
        g_ui_cli_rx_alive = 0u;  /* 静默：2 秒无数据，认为终端断开或无用户操作 */
    }
}

/**
 * @brief   HAL UART 接收完成回调（ISR 上下文）
 * @details 每收到 1 字节触发。执行步骤：
 *          1. 过滤非 USART1 的回调（防止多 UART 系统中误处理）
 *          2. 清除 armed 标志（当前传输已完成）
 *          3. 将收到的字节推入环形缓冲
 *          4. 立即重新挂载下一字节接收（保持连续接收链）
 *
 * @param   huart  触发回调的 UART 句柄指针
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef st;  /* 重新挂载的返回状态 */

    if ((huart == NULL) || (huart->Instance != USART1)) /* 过滤：只处理 USART1 */
    {
        return;
    }

    s_ui_cli_rx_armed = 0u;                          /* 清除 armed 标志 */
    ui_cli_ring_push_from_isr(s_ui_cli_rx_byte);     /* 将字节存入环形缓冲（ISR 安全） */

    /* 立即重新挂载：在 ISR 中直接调用，减少字节间延迟，避免漏字符 */
    st = HAL_UART_Receive_IT(&huart1, &s_ui_cli_rx_byte, 1u);
    if ((st == HAL_OK) || (st == HAL_BUSY))  /* 挂载成功 */
    {
        s_ui_cli_rx_armed = 1u;              /* 标记已挂载 */
    }
    else                                     /* 挂载失败（如 UART 出错） */
    {
        s_ui_cli_rx_need_rearm = 1u;         /* 请求在下次 poll 时执行恢复+重挂载 */
        g_ui_cli_rx_err_count++;             /* 计入错误统计 */
    }
}

/**
 * @brief   HAL UART 错误回调（ISR 上下文）
 * @details 在 UART 发生 ORE/NE/FE/PE 等硬件错误时由 HAL 调用。
 *          此处只记录错误并请求重挂载，实际恢复操作延迟到下次 ui_cli_poll()，
 *          避免在 ISR 中执行耗时的 HAL_UART_Abort 操作。
 *
 * @param   huart  触发错误的 UART 句柄指针
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != USART1)) /* 过滤：只处理 USART1 */
    {
        return;
    }

    s_ui_cli_rx_armed      = 0u;  /* 当前接收已中止 */
    s_ui_cli_rx_need_rearm = 1u;  /* 请求在下次 poll 时恢复 UART 状态并重挂载 */
    g_ui_cli_rx_err_count++;      /* 统计 UART 错误次数 */
}

#else  /* UI_CLI_ENABLE == 0：编译时裁掉 CLI 功能，提供空操作桩 */

/**
 * @brief   CLI 功能已关闭时的空操作桩（编译期裁剪）
 * @details 当 UI_CLI_ENABLE 为 0 时，ui_cli_poll 被替换为此空函数，
 *          消除调用点的 #if 判断，保持代码整洁。
 */
static void ui_cli_poll(void)
{
    /* CLI 已通过宏关闭，此函数为空操作 */
}

#endif  /* UI_CLI_ENABLE */

/**
 * @brief   应用层任务与队列初始化入口
 * @details 在 FreeRTOS 调度器启动前（freertos.c 中）调用。
 *          执行顺序：
 *          1. 设置 UI 渲染后端为 Legacy（App_Display）
 *          2. 初始化性能分析器（清零统计、尝试使能 DWT）
 *          3. 同步性能统计状态到运行时配置
 *          4. 从 App_Display 回读初始配置到运行时配置（保证初始状态一致）
 *          5. 创建长度为 1 的音频帧队列和位置队列
 *          6. 创建音频处理任务和 UI 显示任务
 *
 * @note    所有 configASSERT 在 NDEBUG 模式下被优化掉，
 *          Release 版本中队列/任务创建失败将导致未定义行为，
 *          建议在 RAM 紧张时检查堆配置（configTOTAL_HEAP_SIZE）。
 */
void App_Task_Init(void)
{
    BaseType_t task_ok;  /* xTaskCreate 返回值，pdPASS=成功，errCOULD_NOT_ALLOCATE... =失败 */

    /* 步骤 1：设置渲染后端（目前只有 Legacy，未来可切换到 GPU 加速后端） */
    App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LEGACY);

    /* 步骤 2：初始化性能分析器（清零统计数据，尝试使能 DWT CYCCNT） */
    App_Perf_Init();

    /* 步骤 3：将 Perf 模块实际状态同步到运行时配置单例 */
    taskENTER_CRITICAL();
    s_runtime_cfg.perf_enabled = (App_Perf_IsEnabled() != 0u) ? 1u : 0u;  /* 反映 DWT 初始化结果 */
    taskEXIT_CRITICAL();

    /* 步骤 4：从 App_Display 回读初始配置，确保 s_runtime_cfg 与 Display 层一致 */
    s_runtime_sync_from_display();

    /* 步骤 5：创建长度为 1 的覆盖式队列
     *         长度 = 1 是实时策略：旧帧可丢弃，始终处理最新数据，消除累积延迟。
     *         使用 xQueueOverwrite 写入，xQueueReceive 读取。 */
    xAudioFrameQueue = xQueueCreate(1, sizeof(Audio_FrameEvent_t)); /* ISR -> 音频任务 */
    xPositionQueue   = xQueueCreate(1, sizeof(Sound_Pos_t));        /* 音频任务 -> UI 任务 */
    configASSERT(xAudioFrameQueue != NULL);  /* 创建失败通常是堆空间不足，检查 configTOTAL_HEAP_SIZE */
    configASSERT(xPositionQueue   != NULL);

    /* 步骤 6a：创建音频处理流水线任务
     *           栈大小 2304 字（= 2304*4 = 9216 字节），需容纳 FFT/SRP 的局部变量 */
    task_ok = xTaskCreate(Audio_Pipeline_Task,   /* 任务函数 */
                          "Audio_Pipe",           /* 任务名（调试/trace 使用） */
                          2304,                   /* 栈大小（字，非字节）*/
                          NULL,                   /* 任务参数（未使用） */
                          APP_AUDIO_TASK_PRIO,    /* 优先级 4 */
                          &xAudioPipelineTaskHandle); /* 句柄输出 */
    configASSERT(task_ok == pdPASS);

    /* 步骤 6b：创建 UI 显示任务
     *           栈大小 2048 字（= 8192 字节），需容纳渲染函数调用链 */
    task_ok = xTaskCreate(UI_Display_Task,       /* 任务函数 */
                          "UI_Disp",              /* 任务名 */
                          2048,                   /* 栈大小（字）*/
                          NULL,                   /* 任务参数（未使用） */
                          APP_UI_TASK_PRIO,       /* 优先级 4（与音频任务同级） */
                          &xUITaskHandle);         /* 句柄输出 */
    configASSERT(task_ok == pdPASS);
    /* 初始化完成，等待 vTaskStartScheduler() 启动调度器后两个任务自动运行 */
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
    (void)pvParameters;  /* 任务参数未使用，显式转换避免编译器 -Wunused-parameter 警告 */

    /* ---- 任务局部状态变量（persistent across iterations） ---- */

    /** @brief 已执行解交织的总帧数（用于 DEBUG 节流判断） */
    static uint32_t s_frame_cnt = 0u;

    /** @brief 上一个接收到的 ISR 帧序号，用于检测跳变（丢帧检测） */
    uint32_t s_last_seq = 0u;

    /** @brief 抽帧相位计数（0 ~ decim-1 循环），phase==0 时执行算法 */
    uint32_t s_decim_phase = 0u;

    /** @brief 本轮（含抽帧复用）要送往 UI 的声源位置 */
    Sound_Pos_t current_pos   = {0.0f, 0.0f, 0.0f};

    /** @brief 上次 SRP-PHAT 算法计算得到的声源位置（抽帧时复用） */
    Sound_Pos_t last_algo_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 是否已有至少一次有效算法结果（首帧强制执行算法，不抽帧） */
    uint8_t has_last_algo = 0u;

    /* ---- 帧处理工作变量 ---- */

    Audio_FrameEvent_t event;  /* 从队列接收的 DMA 半缓冲事件 */

    /** @brief 指向当前 DMA 半缓冲的起始地址（PING 或 PONG 区） */
    q15_t *p_current_dma_src;

    /** @brief 解交织临时缓冲区，复用 Mic_Freq_Buffer（节省 RAM）
     *         Mic_Freq_Buffer 存放 FFT 频域数据，但在解交织阶段频域计算尚未开始，
     *         因此可以临时借用，FFT 阶段会用 FFT 结果覆盖此区域 */
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    /* ================================================================
     * 任务主循环（永不退出）
     * ================================================================ */
    for (;;)
    {
        /* ---- 阶段 1：等待 DMA 半缓冲完成事件 ---- */
        /* portMAX_DELAY = 永久阻塞直到有数据，不占用 CPU */
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;  /* 理论上不会到达（portMAX_DELAY 下不会超时），保留作为防御代码 */
        }

        /* ---- 阶段 2：丢帧检测（通过序号跳变判断） ---- */
        /* event.seq 由 ISR 侧单调递增写入；若本次 seq > last_seq+1，说明有帧被覆盖 */
        if ((s_last_seq != 0u) && (event.seq > (s_last_seq + 1u)))
        {
            /* 累计跳变量（可能跳多帧，如队列在两次 ISR 之间未被消费） */
            g_audio_both_flags_count += (event.seq - s_last_seq - 1u);
        }
        s_last_seq = event.seq;  /* 更新上次序号基准 */

        /* ---- 阶段 3：根据 half_id 选择正确的 DMA 缓冲区地址 ---- */
        if (event.half_id == AUDIO_DMA_HALF_PING)
        {
            /* PING 区：DMA 缓冲区前半段，偏移 0 */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if (event.half_id == AUDIO_DMA_HALF_PONG)
        {
            /* PONG 区：DMA 缓冲区后半段，偏移 MIC_CHANNELS * FRAME_LEN */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }
        else
        {
            /* half_id 为非法值（理论上不应发生），记录异常并跳过本帧 */
            g_audio_no_flag_count++;
            continue;
        }

        /* ---- 阶段 4：算法抽帧决策 ---- */
        {
            /* 读取当前抽帧比（运行时可通过 CLI 'cfg algodecim N' 修改） */
            uint32_t decim = s_clamp_u32(App_RuntimeConfig_GetAudioAlgoDecim(),
                                         AUDIO_ALGO_DECIM_MIN,
                                         AUDIO_ALGO_DECIM_MAX);

            /* run_algo 决策：
             *   - 若从未执行过算法（首帧），强制执行（避免 UI 显示全零位置）
             *   - 否则，仅在相位为 0 时执行（每 decim 帧执行一次） */
            uint8_t run_algo = (has_last_algo == 0u) ? 1u
                             : ((s_decim_phase == 0u) ? 1u : 0u);

            /* 推进相位计数（0 -> 1 -> ... -> decim-1 -> 0 循环） */
            s_decim_phase++;
            if (s_decim_phase >= decim)
            {
                s_decim_phase = 0u;  /* 回绕到 0，下次将再次执行算法 */
            }

            if (run_algo != 0u)
            {
                /* ============================================================
                 * 执行完整算法流水线：解交织 -> FFT -> SRP-PHAT
                 * ============================================================ */
                uint32_t t_audio = App_Perf_BeginCycles();  /* 开始整体计时 */
                uint32_t t_sec;                              /* 各子段计时起点 */

                found_val++;  /* 调试计数：统计实际执行算法的帧数（可通过调试器观察） */

                /* ---- 子阶段 A：解交织 + 类型转换 ---- */
                /* 将 DMA 交织格式（ch0_s0, ch1_s0, ..., ch0_s1, ch1_s1, ...）
                 * 转换为平面格式（ch0_s0..ch0_sN, ch1_s0..ch1_sN, ...）
                 * 同时完成 q15 -> float 类型转换并存入 Mic_Process_Buffer */
                t_sec = App_Perf_BeginCycles();
                Deinterleave_Using_Matrix(p_current_dma_src,  /* 源：交织 DMA 缓冲 */
                                          p_temp_planar,       /* 临时平面 q15 缓冲 */
                                          Mic_Process_Buffer,  /* 目标：float 平面缓冲 */
                                          FRAME_LEN,           /* 每通道采样点数 */
                                          MIC_CHANNELS);       /* 麦克风通道数 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_DEINT, t_sec);

                s_frame_cnt++;  /* 递增解交织帧计数（用于 DEBUG 节流） */

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 0)
                /* DEBUG 模式 0：每 DEBUG_THROTTLE_FRAMES 帧输出一次 RMS 数据到 VOFA+ */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_Channel_RMS();  /* 发送各通道 RMS 到串口波形工具 */
                }
#endif
#endif

                /* ---- 子阶段 B：FFT 频域变换 ---- */
                /* 对 Mic_Process_Buffer 中的各通道时域信号执行 FFT，
                 * 结果写入 Mic_Freq_Buffer（覆盖了解交织阶段的临时数据） */
                t_sec = App_Perf_BeginCycles();
                AI_FFT_Process();  /* 内部使用 CMSIS-DSP arm_cfft_q15，加窗+变换 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_FFT, t_sec);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 1)
                /* DEBUG 模式 1：输出指定通道的 FFT 频谱幅度 */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
                }
#endif
#endif

                /* ---- 子阶段 C：SRP-PHAT 声源定位 ---- */
                /* 基于频域互功率谱相位变换计算声源方位角，
                 * 结果（x_angle, y_angle, energy）写入 current_pos */
                t_sec = App_Perf_BeginCycles();
                AI_SRP_PHAT_Process(&current_pos);  /* 核心算法，最耗时的部分 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_SRP, t_sec);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 3)
                /* DEBUG 模式 3：输出 SRP 定位结果（角度+能量）到 VOFA+ */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_SRP_Result(&current_pos);
                }
#endif
#endif

                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_TOTAL, t_audio); /* 结束整体计时 */

                last_algo_pos = current_pos;  /* 缓存本次结果，供抽帧时复用 */
                has_last_algo = 1u;           /* 标记已有有效算法结果 */
                App_Perf_CountAudioProc();    /* 递增算法处理帧计数（性能速率统计用） */
            }
            else
            {
                /* 抽帧阶段：跳过算法，直接复用上次结果 */
                /* 这样 UI 任务仍能收到数据（不会饿死），只是位置更新频率降低 */
                current_pos = last_algo_pos;
            }
        }

        /* ---- 阶段 5：将定位结果投递到 UI 任务 ---- */
        /* xQueueOverwrite：若队列已满（UI 任务还未消费），直接覆盖旧数据，
         * 保证 UI 始终拿到最新位置，不因队列满而阻塞音频任务 */
        xQueueOverwrite(xPositionQueue, &current_pos);

        /* 主动出让 CPU，让同优先级的 UI 任务有机会立即运行处理刚投递的数据 */
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
    (void)pvParameters;  /* 任务参数未使用，消除警告 */

    /* ---- 任务局部状态变量 ---- */

    /** @brief 当次从队列取到的声源位置（可能被多次读取覆盖，取最新值） */
    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 最后一次成功从队列读到的声源位置（队列为空时复用此值继续渲染） */
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};

    /** @brief SRP 热力图可视化快照（从音频任务临界区拷贝，避免读写竞争） */
    SRP_VisFrame_t vis_snapshot;

    /** @brief UI 帧序号，每渲染一帧递增，供文字刷新分频逻辑使用 */
    uint32_t ui_frame_seq = 0u;

    /** @brief 上次读取 g_audio_frame_seq_isr 时的值，用于检测 SAI DMA 是否仍在运行 */
    uint32_t last_audio_isr_seq = 0u;

    /** @brief 连续多帧 SAI DMA 序号未变化的帧数（达到阈值后认为无音频输入） */
    uint8_t audio_idle_frames = 0xFFu;  /* 初始化为最大值，触发首帧后立即归零 */

    /** @brief vTaskDelayUntil 的绝对唤醒时刻（保证帧周期稳定，不受渲染耗时影响） */
    TickType_t next_render_wake;

    /** @brief 上次尝试初始化显示层的 tick 值（限制重试频率） */
    TickType_t last_init_try = 0u;

    /** @brief 上次记录的 DMA2D 超时计数，用于变化检测（仅在 UI_DEBUG_LOG 模式打印） */
    uint32_t last_dma2d_timeout = 0u;

    /* ---- 任务启动时首次尝试初始化显示层 ---- */
    /* 若 LCD 尚未就绪（如时序初始化未完成），先尝试初始化 */
    if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
    {
        s_ui_renderer->init();  /* 初始化 LCD 驱动、LTDC 配置、清零帧缓冲 */
    }
    last_init_try    = xTaskGetTickCount();  /* 记录初始化尝试时刻 */
    next_render_wake = last_init_try;        /* 第一帧立即渲染 */

    /* ================================================================
     * 任务主循环（永不退出）
     * ================================================================ */
    for (;;)
    {
        uint32_t t_loop;       /* 本帧循环整体耗时计时起点 */
        uint8_t  sai_dma_active; /* SAI DMA 活跃标志（传给渲染器显示音频状态） */

        /* ---- 步骤 1：处理 CLI 输入（UART 命令行） ---- */
        /* 每帧都轮询 CLI，保证串口命令响应延迟 <= 1 帧周期（≈50ms@20fps） */
        ui_cli_poll();

        /* ---- 步骤 2：显示初始化重试逻辑 ---- */
        /* 若显示层未就绪（如 LCD 初始化失败），每隔 UI_RETRY_INIT_MS(1000ms) 重试一次 */
        if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                /* 调试模式下打印初始化失败时各模块的阶段值，辅助定位硬件问题 */
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,   /* App_Display 初始化阶段 */
                       (unsigned long)g_display_init_error,   /* App_Display 错误码 */
                       (unsigned long)g_lcd_init_stage,       /* LCD 驱动初始化阶段 */
                       (unsigned long)g_ltdc_init_stage);     /* LTDC 控制器初始化阶段 */
#endif
                s_ui_renderer->init();   /* 重试初始化 */
                last_init_try = now;     /* 更新重试时刻 */
            }
            taskYIELD();  /* 出让 CPU，不要空转等待，让音频任务有机会运行 */
            continue;     /* 回到循环顶部重新判断 */
        }

        /* ---- 步骤 3：性能统计递增与速率打印 ---- */
        App_Perf_CountUiLoop();         /* UI 循环计数 +1（用于 perf rate 速率计算） */
        App_Perf_MaybePrintRates();     /* 若达到打印周期（1s），输出速率统计 */
        t_loop = App_Perf_BeginCycles(); /* 记录本帧循环开始时刻 */

        /* ---- 步骤 4：消耗位置队列，取最新声源位置 ---- */
        /* 使用 timeout=0（非阻塞），若无新数据则复用 last_pos 继续渲染（保持帧率稳定） */
        if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
        {
            last_pos = draw_pos;           /* 保存最新位置 */
            g_ui_queue_rx_count++;         /* 统计成功接收次数 */

            /* 若队列中还有更新的位置（极少情况），继续消耗直到清空，取最后一个 */
            while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;       /* 持续覆盖，保证取到最新的 */
                g_ui_queue_rx_count++;
            }
        }
        else
        {
            /* 队列为空（音频任务尚未产生新结果），复用 last_pos 不更新 */
            g_ui_queue_timeout_count++;  /* 统计无新数据帧数（正常现象：当 UI FPS > 音频帧率 / decim 时） */
        }

        ui_frame_seq++;  /* 递增 UI 帧序号（传给渲染器用于分频刷新文字等元素） */

        /* ---- 步骤 5：检测 SAI DMA 活跃性 ---- */
        /* 通过监测 g_audio_frame_seq_isr 是否有变化来判断 SAI DMA 是否在运行 */
        {
            uint32_t audio_seq = g_audio_frame_seq_isr;  /* 读取 ISR 帧序号（volatile） */
            if (audio_seq != last_audio_isr_seq)          /* 序号有变化：DMA 仍在工作 */
            {
                last_audio_isr_seq = audio_seq;  /* 更新基准值 */
                audio_idle_frames  = 0u;          /* 重置空闲计数（表示 DMA 活跃） */
            }
            else if (audio_idle_frames < 0xFFu)   /* 序号未变：DMA 可能停止，累积空闲帧数 */
            {
                audio_idle_frames++;  /* 饱和计数，不溢出 */
            }
            /* 若连续空闲帧数 <= 阈值，认为 SAI DMA 活跃；超过阈值认为无音频输入 */
            sai_dma_active = (audio_idle_frames <= APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES) ? 1u : 0u;
        }

        /* ---- 步骤 6：在临界区内复制 SRP 可视化快照 ---- */
        /* SRP 可视化数据（热力图数据）由音频任务写入，UI 任务读取，存在竞争。
         * 使用临界区保护复制操作，防止读到中间状态（半拷贝的数据）。
         * 复制后快照与音频任务解耦，渲染期间不再需要持有临界区。 */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            taskENTER_CRITICAL();                         /* 禁止任务切换和中断 */
            AI_SRP_CopyVisualizationFrame(&vis_snapshot); /* 快速内存拷贝 */
            taskEXIT_CRITICAL();                          /* 恢复调度 */
            App_Perf_EndCycles(APP_PERF_SEC_UI_SNAPSHOT, t_sec);
        }

        /* ---- 步骤 7：调用渲染后端执行一帧渲染 ---- */
        /* 渲染流程（在 App_Display_Render 内部）：
         *   归一化热力图 -> colormap 映射 -> 双线性/最近邻插值 -> 平滑 ->
         *   绘制十字光标 -> 绘制坐标轴 -> 覆盖文字 -> DMA2D blit 到 LCD */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            s_ui_renderer->render(&last_pos,       /* 声源位置 */
                                  &vis_snapshot,   /* SRP 热力图快照 */
                                  ui_frame_seq,    /* 帧序号（文字分频用） */
                                  sai_dma_active); /* SAI DMA 状态 */
            App_Perf_EndCycles(APP_PERF_SEC_UI_RENDER, t_sec);
        }
        g_ui_render_count++;  /* 渲染帧计数 +1（可通过调试器观察 UI 实际运行帧率） */

        /* ---- 步骤 8：DMA2D 超时变化检测（可选调试日志） ---- */
        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)  /* 超时计数有增加 */
        {
#if UI_DEBUG_LOG
            /* 打印超时信息，帮助诊断 DMA2D 硬件或时序问题 */
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,  /* 最新超时计数 */
                   (unsigned int)g_ltdc_panel_id);              /* LCD 面板 ID */
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;  /* 更新基准值 */
        }

        /* ---- 步骤 9：结束本帧计时并等待下一帧唤醒时刻 ---- */
        App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);  /* 记录本帧总耗时 */

        /* vTaskDelayUntil：绝对时间延迟，自动补偿渲染耗时，保证帧周期恒定。
         * 若渲染时间超过一个帧周期，next_render_wake 会被推进以避免负延迟。
         * 相比 vTaskDelay（相对延迟），可有效防止帧率随负载波动而漂移。 */
        vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
    }
}

