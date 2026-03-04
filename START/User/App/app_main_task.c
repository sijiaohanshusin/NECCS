/**
 * @file    app_main_task.c
 * @brief   FreeRTOS 任务调度实现
 * @details 实现音频处理流水线和 UI 显示任务
 *
 * 任务架构：
 * - Audio_Pipeline_Task: 音频采集 → 预处理 → FFT → SRP-PHAT → 发送结果
 * - UI_Display_Task: 接收结果 → 渲染热力图 → 刷新显示
 *
 * 数据流：
 * - SAI DMA ISR → xAudioFrameQueue → Audio_Pipeline_Task
 * - Audio_Pipeline_Task → xPositionQueue → UI_Display_Task
 *
 * 队列机制：
 * - 队列长度为 1 (仅保留最新数据)
 * - ISR 使用 xQueueOverwrite (覆盖旧数据)
 * - 任务使用 xQueueReceive (阻塞等待)
 */

#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_display.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "usart.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 外部变量 (External Variables)
 * ============================================================================ */

/** @brief 调试计数：音频任务每处理一帧加 1 */
extern int16_t found_val;

/* ============================================================================
 * FreeRTOS 句柄 (FreeRTOS Handles)
 * ============================================================================ */

/** @brief 音频处理流水线任务句柄 */
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

/** @brief 音频任务丢帧计数 (队列覆盖导致) */
volatile uint32_t g_audio_both_flags_count = 0u;

/** @brief 音频任务未收到标志计数 (异常情况) */
volatile uint32_t g_audio_no_flag_count = 0u;

/** @brief UI 任务渲染帧计数 */
volatile uint32_t g_ui_render_count = 0u;

/** @brief UI 任务成功接收队列数据的次数 */
volatile uint32_t g_ui_queue_rx_count = 0u;

/** @brief UI 任务队列接收超时的次数 */
volatile uint32_t g_ui_queue_timeout_count = 0u;

/* ============================================================================
 * 调试配置 (Debug Configuration)
 * ============================================================================ */

/* #define DEBUG_ENABLE */  /**< 启用调试输出 (VOFA+) */
#define DEBUG_THROTTLE_FRAMES   20u  /**< 调试输出节流 (每 20 帧输出一次) */
#define DEBUG_MODE              3    /**< 调试模式：0=RMS, 1=FFT, 3=SRP */
#define DEBUG_SPECTRUM_CHANNEL  0u   /**< FFT 调试通道 */

/* ============================================================================
 * UI 刷新参数 (UI Refresh Parameters)
 * ============================================================================ */

#define UI_RETRY_INIT_MS        1000u  /**< UI 初始化重试间隔 (ms) */
#define UI_RENDER_PERIOD_MS     33u    /**< UI 渲染周期 (ms)，约 30 FPS */
#define UI_DEBUG_LOG            0u     /**< UI 调试日志开关 */
#define UI_CLI_ENABLE           1u
#define UI_CLI_LINE_MAX         96u
#define UI_CLI_RX_DRAIN_MAX     32u

/* ============================================================================
 * 任务优先级 (Task Priorities)
 * ============================================================================ */

#define APP_AUDIO_TASK_PRIO     4u  /**< 音频任务优先级 (高) */
#define APP_UI_TASK_PRIO        4u  /**< UI 任务优先级 (同级) */

/* ============================================================================
 * 初始化函数 (Initialization Functions)
 * ============================================================================ */

/**
 * @brief   应用任务和队列初始化
 * @details 创建 FreeRTOS 任务和队列
 *
 * 初始化流程：
 * 1. 创建音频帧事件队列 (长度 1, 覆盖模式)
 * 2. 创建声源位置队列 (长度 1, 覆盖模式)
 * 3. 创建音频处理任务 (优先级 4, 堆栈 2304 字节)
 * 4. 创建 UI 显示任务 (优先级 4, 堆栈 2048 字节)
 *
 * 队列长度为 1 的原因：
 * - 实时系统，只关心最新数据
 * - 避免队列积压导致延迟
 * - 丢帧策略：丢弃旧帧，处理新帧
 *
 * @note    在 FreeRTOS 启动前调用 (freertos.c 中)
 */
/* ============================================================================
 * UI CLI (Runtime Tuning via UART)
 * ============================================================================ */

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

static void ui_cli_print_help(void)
{
    printf("\r\n");
    printf("cfg help\r\n");
    printf("cfg status\r\n");
    printf("cfg mode fast|balanced|clean\r\n");
    printf("cfg contrast <db_floor>\r\n");
    printf("cfg gamma <0.5..2.5>\r\n");
    printf("cfg noise <0..0.6>\r\n");
    printf("cfg adapt <0..6>\r\n");
    printf("cfg smooth <0..3>\r\n");
    printf("cfg fine <0..3>\r\n");
    printf("cfg bilinear <0|1>\r\n");
    printf("cfg textdiv <1..20>\r\n");
    printf("cfg blit <1..8>\r\n");
}

static void ui_cli_print_status(void)
{
    App_Display_RuntimeCfg_t cfg;
    App_Display_Mode_t mode;

    App_Display_GetConfig(&cfg);
    mode = App_Display_GetMode();

    printf("cfg mode=%s\r\n", App_Display_ModeName(mode));
    printf("cfg db=%.1f gamma=%.2f noise=%.3f adapt=%.2f\r\n",
           (double)cfg.db_floor,
           (double)cfg.gamma,
           (double)cfg.noise_gate_ratio,
           (double)cfg.noise_adapt_gain);
    printf("cfg smooth=%u fine=%.2f bilinear=%u textdiv=%u blit=%u\r\n",
           (unsigned int)cfg.smooth_passes,
           (double)cfg.fine_gain,
           (unsigned int)cfg.bilinear_sampling,
           (unsigned int)cfg.text_refresh_div,
           (unsigned int)cfg.blit_rows);
    printf("dma2d transfer=%lu timeout=%lu fallback=%lu\r\n",
           (unsigned long)g_ltdc_dma2d_transfer_count,
           (unsigned long)g_ltdc_dma2d_timeout_count,
           (unsigned long)g_ltdc_dma2d_sw_fallback_count);
}

static void ui_cli_apply_line(char *line)
{
    char *cursor;
    char *arg = NULL;
    App_Display_RuntimeCfg_t cfg;
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

    if (ui_cli_stricmp(cursor, "mode") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg mode fast|balanced|clean\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "fast") == 0)
        {
            App_Display_SetMode(APP_DISPLAY_MODE_FAST);
        }
        else if ((ui_cli_stricmp(arg, "balanced") == 0) || (ui_cli_stricmp(arg, "bal") == 0))
        {
            App_Display_SetMode(APP_DISPLAY_MODE_BALANCED);
        }
        else if (ui_cli_stricmp(arg, "clean") == 0)
        {
            App_Display_SetMode(APP_DISPLAY_MODE_CLEAN);
        }
        else
        {
            printf("CLI: invalid mode\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }

    App_Display_GetConfig(&cfg);

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
        cfg.bilinear_sampling = (uv != 0u) ? 1u : 0u;
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

    App_Display_SetConfig(&cfg);
    ui_cli_print_status();
}

static void ui_cli_poll(void)
{
    static char line_buf[UI_CLI_LINE_MAX];
    static uint16_t line_len = 0u;
    static uint8_t banner_printed = 0u;
    uint32_t i;
    uint8_t ch;

    if (banner_printed == 0u)
    {
        banner_printed = 1u;
        printf("UI CLI ready, type 'cfg help'\r\n");
    }

    for (i = 0u; i < UI_CLI_RX_DRAIN_MAX; i++)
    {
        if (HAL_UART_Receive(&huart1, &ch, 1u, 0u) != HAL_OK)
        {
            break;
        }

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
}
#else
static void ui_cli_poll(void)
{
}
#endif

void App_Task_Init(void)
{
    BaseType_t task_ok;

    /* 创建队列：长度为 1，保留最新帧/最新定位结果，降低端到端延迟 */
    xAudioFrameQueue = xQueueCreate(1, sizeof(Audio_FrameEvent_t));
    xPositionQueue = xQueueCreate(1, sizeof(Sound_Pos_t));
    configASSERT(xAudioFrameQueue != NULL);
    configASSERT(xPositionQueue != NULL);

    /* 创建音频处理任务 */
    /* 堆栈：2304 字节 (足够容纳局部变量和函数调用栈) */
    task_ok = xTaskCreate(Audio_Pipeline_Task, "Audio_Pipe", 2304, NULL, APP_AUDIO_TASK_PRIO, &xAudioPipelineTaskHandle);
    configASSERT(task_ok == pdPASS);

    /* 创建 UI 显示任务 */
    /* 堆栈：2048 字节 */
    task_ok = xTaskCreate(UI_Display_Task, "UI_Disp", 2048, NULL, APP_UI_TASK_PRIO, &xUITaskHandle);
    configASSERT(task_ok == pdPASS);
}

/**
 * @brief   音频处理流水线任务
 * @details 处理音频数据流水线：DMA → 预处理 → FFT → SRP-PHAT → 输出
 *
 * 任务流程：
 * 1. 等待 DMA 中断事件 (xQueueReceive, 无限等待)
 * 2. 检测丢帧 (通过序号断层判断)
 * 3. 解交织 + 类型转换 (Deinterleave_Using_Matrix)
 * 4. FFT 频域变换 (AI_FFT_Process)
 * 5. SRP-PHAT 声源定位 (AI_SRP_PHAT_Process)
 * 6. 发送结果到 UI 任务 (xQueueOverwrite)
 * 7. 主动让出 CPU (taskYIELD)
 *
 * 内存优化：
 * - 复用 Mic_Freq_Buffer 作为临时 q15 平面缓冲
 * - 避免额外分配 16KB 内存
 * - 该缓冲在 FFT 前使用，FFT 后被覆盖
 *
 * 调试输出：
 * - DEBUG_MODE=0: 输出 RMS (有效值)
 * - DEBUG_MODE=1: 输出 FFT 频谱
 * - DEBUG_MODE=3: 输出 SRP 结果
 *
 * @param   pvParameters  FreeRTOS 任务参数 (未使用)
 *
 * @note    任务优先级：4 (高于 UI 任务)
 * @note    任务堆栈：2304 字节
 * @note    任务周期：5.33ms (48kHz, 256 点)
 */
void Audio_Pipeline_Task(void *pvParameters)
{
    (void)pvParameters;

    /* 局部变量 */
    static uint32_t s_frame_cnt = 0u;  /* 帧计数器 */
    uint32_t s_last_seq = 0u;          /* 上次序号 (用于丢帧检测) */
    Sound_Pos_t current_pos;           /* 当前声源位置 */

    Audio_FrameEvent_t event;          /* DMA 事件 */
    q15_t *p_current_dma_src;          /* 当前 DMA 源指针 */

    /* 复用 Mic_Freq_Buffer 作为临时 q15 平面缓冲，避免额外分配大块内存 */
    /* 该缓冲只在当前任务内使用，不跨任务共享 */
    /* 使用时机：解交织后，FFT 前；FFT 后会被覆盖 */
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    /* 任务主循环 */
    for (;;)
    {
        /* ========== 步骤 1: 等待 DMA 事件 ========== */
        /* 阻塞等待音频帧事件 (无限等待) */
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;  /* 接收失败，重试 */
        }

        /* ========== 步骤 2: 丢帧检测 ========== */
        /* 通过序号断层检测队列覆盖导致的丢帧 */
        if ((s_last_seq != 0u) && (event.seq > (s_last_seq + 1u)))
        {
            /* 计算丢失的帧数 */
            g_audio_both_flags_count += (event.seq - s_last_seq - 1u);
        }
        s_last_seq = event.seq;

        /* ========== 步骤 3: 确定 DMA 源地址 ========== */
        /* 根据 PING/PONG 标识选择 DMA 缓冲区 */
        if (event.half_id == AUDIO_DMA_HALF_PING)
        {
            /* PING 区：前半区 */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if (event.half_id == AUDIO_DMA_HALF_PONG)
        {
            /* PONG 区：后半区 */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }
        else
        {
            /* 异常：无效的半缓冲标识 */
            g_audio_no_flag_count++;
            continue;
        }

        /* 调试计数 */
        found_val++;

        /* ========== 步骤 4: 解交织 + 类型转换 ========== */
        /* 输入：DMA 缓冲区 (int16 交织) */
        /* 输出：Mic_Process_Buffer (float32 平面) */
        /* 耗时：约 0.3ms */
        Deinterleave_Using_Matrix(p_current_dma_src,
                                  p_temp_planar,
                                  Mic_Process_Buffer,
                                  FRAME_LEN,
                                  MIC_CHANNELS);

        /* 帧计数 */
        s_frame_cnt++;

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 0)
        /* 调试模式 0: 输出 RMS (有效值) */
        if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
        {
            VOFA_Send_Channel_RMS();
        }
#endif
#endif

        /* ========== 步骤 5: FFT 频域变换 ========== */
        /* 输入：Mic_Process_Buffer (float32 时域) */
        /* 输出：Mic_Freq_Buffer (float32 频域复数) */
        /* 耗时：约 0.8ms */
        AI_FFT_Process();

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 1)
        /* 调试模式 1: 输出 FFT 频谱 */
        if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
        {
            VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
        }
#endif
#endif

        /* ========== 步骤 6: SRP-PHAT 声源定位 ========== */
        /* 输入：Mic_Freq_Buffer (频域复数) */
        /* 输出：current_pos (声源位置) */
        /* 耗时：约 4ms */
        AI_SRP_PHAT_Process(&current_pos);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 3)
        /* 调试模式 3: 输出 SRP 结果 */
        if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
        {
            VOFA_Send_SRP_Result(&current_pos);
        }
#endif
#endif

        /* ========== 步骤 7: 发送结果到 UI 任务 ========== */
        /* 使用 xQueueOverwrite 覆盖旧数据 (队列长度为 1) */
        xQueueOverwrite(xPositionQueue, &current_pos);

        /* ========== 步骤 8: 主动让出 CPU ========== */
        /* 音频/UI 同优先级时主动让出一次 CPU，降低 UI 被长期饿死的概率 */
        taskYIELD();
    }
}

/**
 * @brief   UI 显示任务
 * @details 接收声源位置数据，渲染热力图和十字光标
 *
 * 任务流程：
 * 1. 检查显示模块是否就绪 (App_Display_IsReady)
 * 2. 如果未就绪，定期重试初始化 (1 秒间隔)
 * 3. 非阻塞接收声源位置数据 (xQueueReceive, 0 超时)
 * 4. 如果有多帧积压，仅保留最后一帧
 * 5. 临界区快照 SRP 功率数据 (避免读到半更新数据)
 * 6. 渲染 UI (App_Display_Render)
 * 7. 周期性延迟 (vTaskDelayUntil, 33ms)
 *
 * 初始化重试机制：
 * - 显示模块初始化可能失败 (LCD 硬件问题)
 * - 每 1 秒重试一次，直到成功
 * - 重试期间让出 CPU，避免阻塞其他任务
 *
 * 数据同步：
 * - SRP_Power 被音频任务写入，UI 任务读取
 * - 使用临界区快照，避免数据竞争
 * - 临界区时间短 (约 0.1ms)，不影响实时性
 *
 * @param   pvParameters  FreeRTOS 任务参数 (未使用)
 *
 * @note    任务优先级：4 (与音频任务同级)
 * @note    任务堆栈：2048 字节
 * @note    任务周期：33ms (30 FPS)
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;

    /* 局部变量 */
    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};  /* 当前接收的位置 */
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};  /* 上次有效位置 */
    SRP_VisFrame_t vis_snapshot;                /* SRP 粗搜+精搜可视化快照 */
    uint32_t ui_frame_seq = 0u;                 /* UI 帧序号 */
    uint32_t last_audio_isr_seq = 0u;           /* 上次观测到的 SAI DMA ISR 序号 */
    uint8_t audio_idle_frames = 0xFFu;          /* SAI DMA 空闲帧计数 */

    TickType_t next_render_wake;                /* 下次唤醒时间 */
    TickType_t last_init_try = 0u;              /* 上次初始化尝试时间 */
    uint32_t last_dma2d_timeout = 0u;           /* 上次 DMA2D 超时计数 */

    /* ========== 初始化显示模块 ========== */
    if (App_Display_IsReady() == 0u)
    {
        App_Display_Init();
    }
    last_init_try = xTaskGetTickCount();
    next_render_wake = last_init_try;

    /* 任务主循环 */
    for (;;)
    {
        /* ========== 步骤 1: 检查显示模块是否就绪 ========== */
        ui_cli_poll();
        if (App_Display_IsReady() == 0u)
        {
            /* 显示模块未就绪，定期重试初始化 */
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                /* 调试日志：输出初始化状态 */
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,
                       (unsigned long)g_display_init_error,
                       (unsigned long)g_lcd_init_stage,
                       (unsigned long)g_ltdc_init_stage);
#endif
                /* 重试初始化 */
                App_Display_Init();
                last_init_try = now;
            }
            /* 让出 CPU，避免阻塞其他任务 */
            taskYIELD();
            continue;
        }

        /* ========== 步骤 2: 非阻塞接收声源位置数据 ========== */
        /* 非阻塞获取最新定位结果；若有多帧积压，仅保留最后一帧 */
        if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
        {
            /* 接收成功 */
            last_pos = draw_pos;
            g_ui_queue_rx_count++;

            /* 清空队列，仅保留最后一帧 */
            while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;
                g_ui_queue_rx_count++;
            }
        }
        else
        {
            /* 接收超时 (队列为空) */
            g_ui_queue_timeout_count++;
        }

        /* UI 帧序号递增 */
        ui_frame_seq++;

        uint8_t sai_dma_active;
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

        /* ========== 步骤 3: 临界区快照 SRP 功率数据 ========== */
        /* SRP_Power 同时被音频任务写入 */
        /* 这里做一次短临界区快照，避免 UI 读到"半更新"数据 */
        taskENTER_CRITICAL();
        AI_SRP_CopyVisualizationFrame(&vis_snapshot);
        taskEXIT_CRITICAL();

        /* ========== 步骤 4: 渲染 UI ========== */
        /* 渲染热力图 + 十字光标 + 诊断信息 */
        /* 耗时：约 10ms (包含 DMA2D 传输) */
        App_Display_Render(&last_pos, &vis_snapshot, ui_frame_seq, sai_dma_active);
        g_ui_render_count++;

        /* ========== 步骤 5: 检查 DMA2D 超时 ========== */
        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)
        {
#if UI_DEBUG_LOG
            /* 调试日志：输出 DMA2D 超时信息 */
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,
                   (unsigned int)g_ltdc_panel_id);
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;
        }

        /* ========== 步骤 6: 周期性延迟 ========== */
        /* 延迟到下次唤醒时间 (33ms 周期，30 FPS) */
        vTaskDelayUntil(&next_render_wake, pdMS_TO_TICKS(UI_RENDER_PERIOD_MS));
    }
}
