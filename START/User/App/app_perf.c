/**
 * @file    app_perf.c
 * @brief   DWT 周期计数器性能分析器 (Performance Profiler)
 * @details 基于 ARM Cortex-M7 DWT CYCCNT 寄存器实现微秒级精度的代码段耗时统计。
 *          支持多区间独立计时、环形缓冲 p95 百分位计算、实时吞吐率打印。
 *          可通过 CLI 命令动态开关: 'cfg perf on/off/dump/reset'。
 */
#include "app_perf.h"

#include "app_main_task.h"
#include "app_runtime.h"
#include "app_task_cfg.h"
#include "LCD/ltdc.h"
#include "stm32h7xx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

uint8_t App_Perf_GetSectionSummary(App_Perf_Section_t section,
                                   App_Perf_SectionSummary_t *out)
{
    App_Perf_SectionStat_t *st;          /* 目标区间统计数据指针 */
    uint32_t core_hz = SystemCoreClock;  /* 读取系统主频，用于周期 → 微秒换算 */

    if ((section >= APP_PERF_SEC_COUNT) || (out == NULL))  /* 参数合法性检查 */
    {
        return 0u;  /* 区间越界或输出指针为空，返回失败 */
    }

    if (core_hz == 0u)              /* SystemCoreClock 未初始化的安全回退 */
    {
        core_hz = 480000000u;       /* 假设 480 MHz（STM32H7 典型主频） */
    }

    st = &s_perf_stats[section];          /* 取目标区间统计结构体指针 */
    out->sample_count = st->sample_count; /* 输出已采集样本总数 */

    if (st->sample_count == 0u)           /* 无样本：输出全零并返回"无数据" */
    {
        out->avg_us = 0.0f;  /* 均值微秒置零 */
        out->max_us = 0.0f;  /* 最大值微秒置零 */
        return 0u;           /* 返回 0 表示数据无效 */
    }

    /* 均值微秒 = (总周期 / 样本数) × (1e6 / 主频)
     * 先除样本数得均值周期，再乘 1e6 / core_hz 转换为微秒 */
    out->avg_us = (float)(((double)st->total_cycles / (double)st->sample_count)
                          * 1e6 / (double)core_hz);

    /* 最大值微秒 = 历史最大周期数 × (1e6 / 主频) */
    out->max_us = (float)((double)st->max_cycles * 1e6 / (double)core_hz);

    return 1u;  /* 返回 1 表示输出数据有效 */
}
