/**
 * @file    app_ui_cli.c
 * @brief   UI UART CLI implementation
 */
#include "main.h"

#include "app_ui_cli.h"

#include "app_camera.h"
#include "app_display.h"
#include "app_laser.h"
#include "app_main_task.h"
#include "app_noise_floor.h"
#include "app_spectrum.h"
#include "app_task_cfg.h"
#include "app_trigger.h"
#include "ai_beamforming.h"
#include "LCD/dma2d_accel.h"
#include "LCD/ltdc.h"
#include "usart.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern volatile uint32_t g_ltdc_fifo_underrun_count;
extern volatile uint32_t g_ltdc_transfer_error_count;
extern volatile uint32_t g_ltdc_last_error_code;

static const char *s_ui_backend_name(App_UiRenderBackend_t backend)
{
    switch (backend)
    {
        case APP_UI_RENDER_BACKEND_LVGL:
            return "lvgl";
        case APP_UI_RENDER_BACKEND_LEGACY:
        default:
            return "legacy";
    }
}

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
    printf("cfg backend legacy|lvgl\r\n");             /* 切换 UI 渲染后端 */
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
    printf("cfg cam retry\r\n");                      /* 手动重试摄像头初始化/启动 */
    printf("cfg camfreeze on|off\r\n");               /* 冻结/恢复 camera 发布，定位撕裂发生层级 */
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
    App_CameraStatus_t        camera_status;
    App_Display_DebugStats_t  display_debug;

    App_RuntimeConfig_GetDisplayCfg(&cfg);     /* 线程安全读取显示参数 */
    mode = App_RuntimeConfig_GetDisplayMode(); /* 线程安全读取显示模式 */
    App_Camera_GetStatus(&camera_status);
    App_Display_GetDebugStats(&display_debug);

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
    printf("cfg backend=%s uifps=%lu algodecim=%lu perf=%s\r\n",
           s_ui_backend_name(App_UiRenderer_GetBackend()),
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
    printf("cam init=%u stream=%u valid=%u stage=%s pending=%u freeze=%u raw=%lu pub=%lu\r\n",
           (unsigned int)camera_status.initialized,
           (unsigned int)camera_status.streaming,
           (unsigned int)camera_status.valid,
           App_Camera_InitStageName(camera_status.init_stage),
           (unsigned int)camera_status.pending_restart,
           (unsigned int)camera_status.freeze_enabled,
           (unsigned long)camera_status.frame_seq,
           (unsigned long)camera_status.published_seq);

    printf("cam err=0x%08lX dma=0x%08lX restart=%lu/%lu drop=%lu publish=%lu irq=%lu/%lu idx=%u/%u\r\n",
           (unsigned long)camera_status.error_code,
           (unsigned long)camera_status.dma_error_code,
           (unsigned long)camera_status.restart_count,
           (unsigned long)camera_status.restart_fail_count,
           (unsigned long)camera_status.publish_drop_count,
           (unsigned long)camera_status.publish_count,
           (unsigned long)camera_status.dma_done_count,
           (unsigned long)camera_status.frame_event_count,
           (unsigned int)camera_status.latest_index,
           (unsigned int)camera_status.published_index);
    printf("cam arm=%lu/%lu state=%lu/%lu\r\n",
           (unsigned long)camera_status.arm_count,
           (unsigned long)camera_status.arm_fail_count,
           (unsigned long)camera_status.dcmi_state,
           (unsigned long)camera_status.dma_state);
    printf("cam sensor mid=0x%04X pid=0x%04X diag=%u wr=%u rd=%u\r\n",
           (unsigned int)camera_status.sensor_mid,
           (unsigned int)camera_status.sensor_pid,
           (unsigned int)camera_status.sensor_diag_stage,
           (unsigned int)camera_status.sensor_last_write_status,
           (unsigned int)camera_status.sensor_last_read_status);
    printf("cam samp raw=%04X/%04X/%04X pub=%04X/%04X/%04X hash=%08lX/%08lX\r\n",
           (unsigned int)camera_status.raw_sample0,
           (unsigned int)camera_status.raw_sample1,
           (unsigned int)camera_status.raw_sample2,
           (unsigned int)camera_status.pub_sample0,
           (unsigned int)camera_status.pub_sample1,
           (unsigned int)camera_status.pub_sample2,
           (unsigned long)camera_status.raw_hash,
           (unsigned long)camera_status.pub_hash);
    printf("disp view=%s cam_path=%lu overlay=%lu in_seq=%lu cache_seq=%lu cache_valid=%u\r\n",
           App_Display_CameraViewName((App_Display_CameraView_t)display_debug.camera_view_mode),
           (unsigned long)display_debug.camera_path_count,
           (unsigned long)display_debug.camera_overlay_count,
           (unsigned long)display_debug.camera_input_seq,
           (unsigned long)display_debug.camera_cache_seq,
           (unsigned int)display_debug.camera_cache_valid);
    printf("ltdc fu=%lu te=%lu last=0x%08lX\r\n",
           (unsigned long)g_ltdc_fifo_underrun_count,
           (unsigned long)g_ltdc_transfer_error_count,
           (unsigned long)g_ltdc_last_error_code);

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

    /* -- cfg backend legacy|lvgl：切换 UI 后端 -- */
    if (ui_cli_stricmp(cursor, "backend") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg backend legacy|lvgl\r\n");
            return;
        }
        if ((ui_cli_stricmp(arg, "legacy") == 0) ||
            (ui_cli_stricmp(arg, "old") == 0))
        {
            App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LEGACY);
        }
        else if (ui_cli_stricmp(arg, "lvgl") == 0)
        {
            App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LVGL);
        }
        else
        {
            printf("CLI: cfg backend legacy|lvgl\r\n");
            return;
        }
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
    if (ui_cli_stricmp(cursor, "cam") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg cam retry\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "retry") == 0)
        {
            (void)App_Camera_Retry();
        }
        else
        {
            printf("CLI: cfg cam retry\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }

    /* ---- 以下命令需要读取并修改显示配置，先取一次快照 ---- */
    App_RuntimeConfig_GetDisplayCfg(&cfg);  /* 读取当前配置到本地副本，下方按需修改单个字段 */

    /* -- cfg contrast <db_floor>：设置动态范围底限（应为负数，单位 dB） -- */
    if (ui_cli_stricmp(cursor, "camview") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg camview overlay|camera|heat|freeze\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "overlay") == 0)
        {
            App_Display_SetCameraView(APP_DISPLAY_CAMERA_VIEW_OVERLAY);
        }
        else if (ui_cli_stricmp(arg, "camera") == 0)
        {
            App_Display_SetCameraView(APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY);
        }
        else if (ui_cli_stricmp(arg, "heat") == 0)
        {
            App_Display_SetCameraView(APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY);
        }
        else if (ui_cli_stricmp(arg, "freeze") == 0)
        {
            App_Display_SetCameraView(APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE);
        }
        else
        {
            printf("CLI: cfg camview overlay|camera|heat|freeze\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }
    if (ui_cli_stricmp(cursor, "camfreeze") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: cfg camfreeze on|off\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "on") == 0)
        {
            App_Camera_SetFreeze(1u);
        }
        else if (ui_cli_stricmp(arg, "off") == 0)
        {
            App_Camera_SetFreeze(0u);
        }
        else
        {
            printf("CLI: cfg camfreeze on|off\r\n");
            return;
        }
        ui_cli_print_status();
        return;
    }

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
    /* -- cfg laser on|off|toggle：激光瞄准器控制 -- */
    else if (ui_cli_stricmp(cursor, "laser") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: laser=%s\r\n",
                   App_Laser_GetState() == APP_LASER_ON ? "ON" : "OFF");
            return;
        }
        if (ui_cli_stricmp(arg, "on") == 0)
        {
            App_Laser_SetState(APP_LASER_ON);
        }
        else if (ui_cli_stricmp(arg, "off") == 0)
        {
            App_Laser_SetState(APP_LASER_OFF);
        }
        else if (ui_cli_stricmp(arg, "toggle") == 0)
        {
            App_Laser_Toggle();
        }
        printf("CLI: laser=%s\r\n",
               App_Laser_GetState() == APP_LASER_ON ? "ON" : "OFF");
        return;
    }
    /* -- cfg night on|off|toggle：夜间模式 -- */
    else if (ui_cli_stricmp(cursor, "night") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: night=%s\r\n",
                   App_NightMode_GetState() == APP_NIGHTMODE_ON ? "ON" : "OFF");
            return;
        }
        if (ui_cli_stricmp(arg, "on") == 0)
        {
            App_NightMode_Enable();
        }
        else if (ui_cli_stricmp(arg, "off") == 0)
        {
            App_NightMode_Disable();
        }
        else if (ui_cli_stricmp(arg, "toggle") == 0)
        {
            App_NightMode_Toggle();
        }
        printf("CLI: night=%s\r\n",
               App_NightMode_GetState() == APP_NIGHTMODE_ON ? "ON" : "OFF");
        return;
    }
    /* -- cfg trigger arm|disarm|rearm|threshold <val>：触发模式 -- */
    else if (ui_cli_stricmp(cursor, "trigger") == 0)
    {
        if (arg == NULL)
        {
            const char *state_str = "IDLE";
            App_TriggerState_t ts = App_Trigger_GetState();
            if (ts == APP_TRIGGER_ARMED) { state_str = "ARMED"; }
            else if (ts == APP_TRIGGER_TRIGGERED) { state_str = "TRIGGERED"; }
            printf("CLI: trigger=%s threshold=%.3f\r\n",
                   state_str, (double)App_Trigger_GetThreshold());
            return;
        }
        if (ui_cli_stricmp(arg, "arm") == 0)
        {
            App_Trigger_Arm();
            printf("CLI: trigger ARMED\r\n");
        }
        else if (ui_cli_stricmp(arg, "disarm") == 0)
        {
            App_Trigger_Disarm();
            printf("CLI: trigger IDLE\r\n");
        }
        else if (ui_cli_stricmp(arg, "rearm") == 0)
        {
            App_Trigger_Rearm();
            printf("CLI: trigger ARMED\r\n");
        }
        else
        {
            float tv;
            if (ui_cli_parse_float(arg, &tv) != 0u)
            {
                App_Trigger_SetThreshold(tv);
                printf("CLI: trigger threshold=%.3f\r\n", (double)tv);
            }
            else
            {
                printf("CLI: cfg trigger arm|disarm|rearm|<threshold>\r\n");
            }
        }
        return;
    }
    /* -- cfg band <start> <end>：设置 SRP 活动频率 bin 范围 -- */
    else if (ui_cli_stricmp(cursor, "band") == 0)
    {
        if (arg == NULL)
        {
            uint16_t bs, be;
            AI_SRP_GetActiveFreqRange(&bs, &be);
            printf("CLI: band=%u..%u (%.0f..%.0f Hz)\r\n",
                   (unsigned)bs, (unsigned)be,
                   (double)App_Spectrum_BinToHz(bs),
                   (double)App_Spectrum_BinToHz(be));
            return;
        }
        {
            uint32_t bstart, bend;
            char *sep = arg;
            while ((*sep != '\0') && (isspace((unsigned char)*sep) == 0))
            {
                sep++;
            }
            if (*sep != '\0')
            {
                *sep++ = '\0';
                while (isspace((unsigned char)*sep) != 0) { sep++; }
            }
            if ((ui_cli_parse_u32(arg, &bstart) == 0u) ||
                (*sep == '\0') || (ui_cli_parse_u32(sep, &bend) == 0u))
            {
                printf("CLI: cfg band <start_bin> <end_bin>\r\n");
                return;
            }
            {
                App_FreqBand_t b;
                b.start_bin = (uint16_t)bstart;
                b.end_bin   = (uint16_t)bend;
                App_Spectrum_SetActiveBand(b);
                printf("CLI: band set %u..%u (%.0f..%.0f Hz)\r\n",
                       (unsigned)b.start_bin, (unsigned)b.end_bin,
                       (double)App_Spectrum_BinToHz(b.start_bin),
                       (double)App_Spectrum_BinToHz(b.end_bin));
            }
        }
        return;
    }
    /* -- cfg band_preset <voice|ultra|low|full>：频段预设 -- */
    else if (ui_cli_stricmp(cursor, "band_preset") == 0)
    {
        App_FreqBand_t b;
        if (arg == NULL)
        {
            printf("CLI: cfg band_preset voice|ultra|low|full\r\n");
            return;
        }
        if (ui_cli_stricmp(arg, "voice") == 0)
        {
            b.start_bin = App_Spectrum_HzToBin(300.0f);
            b.end_bin   = App_Spectrum_HzToBin(4000.0f);
        }
        else if (ui_cli_stricmp(arg, "ultra") == 0)
        {
            b.start_bin = App_Spectrum_HzToBin(8000.0f);
            b.end_bin   = App_Spectrum_HzToBin(20000.0f);
        }
        else if (ui_cli_stricmp(arg, "low") == 0)
        {
            b.start_bin = App_Spectrum_HzToBin(100.0f);
            b.end_bin   = App_Spectrum_HzToBin(1000.0f);
        }
        else if (ui_cli_stricmp(arg, "full") == 0)
        {
            b = App_Spectrum_DefaultBand();
        }
        else
        {
            printf("CLI: unknown preset\r\n");
            return;
        }
        App_Spectrum_SetActiveBand(b);
        printf("CLI: band preset '%s' => %u..%u (%.0f..%.0f Hz)\r\n",
               arg,
               (unsigned)b.start_bin, (unsigned)b.end_bin,
               (double)App_Spectrum_BinToHz(b.start_bin),
               (double)App_Spectrum_BinToHz(b.end_bin));
        return;
    }
    /* -- cfg noise on|off|cal|alpha <val>：噪声底控制 -- */
    else if (ui_cli_stricmp(cursor, "noise") == 0)
    {
        if (arg == NULL)
        {
            printf("CLI: noise %s, alpha=%.4f\r\n",
                   App_NoiseFloor_GetEnabled() ? "ON" : "OFF",
                   (double)App_NoiseFloor_GetAlpha());
            return;
        }
        if (ui_cli_stricmp(arg, "on") == 0)
        {
            App_NoiseFloor_SetEnabled(1u);
            printf("CLI: noise floor enabled\r\n");
        }
        else if (ui_cli_stricmp(arg, "off") == 0)
        {
            App_NoiseFloor_SetEnabled(0u);
            printf("CLI: noise floor disabled\r\n");
        }
        else if (ui_cli_stricmp(arg, "cal") == 0)
        {
            printf("CLI: noise floor calibrate (next frame)\r\n");
        }
        else
        {
            /* try parse as alpha value */
            float v = (float)atof(arg);
            if (v > 0.0f && v < 1.0f)
            {
                App_NoiseFloor_SetAlpha(v);
                printf("CLI: noise alpha = %.4f\r\n", (double)v);
            }
            else
            {
                printf("CLI: cfg noise on|off|cal|<alpha 0~1>\r\n");
            }
        }
        return;
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
        ((xTaskGetTickCount() - last_rx_tick) <= pdMS_TO_TICKS(UI_CLI_ALIVE_WINDOW_MS)))
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

void App_UiCli_Poll(void)
{
    ui_cli_poll();
}
