/**
 ****************************************************************************************************
 * @file        ltdc.c
 * @version     V1.0
 * @brief       LTDC（LCD-TFT 显示控制器）驱动代码
 * @details     本文件实现了 STM32H743 LTDC 外设的完整驱动，主要功能：
 *              1. LCD 面板识别（通过 RGB 高位 ID 引脚读取面板型号）
 *              2. PLL3 像素时钟配置（不同面板需不同时钟：9~60 MHz）
 *              3. 双帧缓冲（front/back buffer）管理与垂直同步 swap
 *              4. 图层（Layer）配置：窗口、像素格式、透明度、混合因子
 *              5. 绘图 API：画点/读点/矩形填充/颜色填充/复制/L8/A8 混合
 *              6. DMA2D 硬件加速绘图（带软件降级回退策略）
 *              7. VSYNC 中断帧交换（无撕裂双缓冲 present）
 *
 * @note        支持面板型号（通过 ID 引脚 M2:M1:M0 组合识别）：
 *              - 0x4342：4.3寸 480×272     (9 MHz)
 *              - 0x7084：7寸  800×480     (33 MHz)
 *              - 0x7016：7寸  1024×600    (50 MHz) ← NECCS 项目实际使用
 *              - 0x5571：5.5寸 720×1280   (55 MHz) SPI+MIPI
 *              - 0x4384：4.3寸 800×480    (33 MHz)
 *              - 0x8081：8寸  800×1280    (60 MHz)
 *              - 0x1018：10.1寸 1280×800  (60 MHz)
 *
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    STM32H743IIT6小系统板
 *
 ****************************************************************************************************
 */

#include "LCD/ltdc.h"      /* LTDC BSP 头文件：宏、结构体、API声明 */
#include "LCD/lcd.h"       /* LCD 公共接口：lcddev 全局设备描述符 */
#include "LCD/dma2d_accel.h" /* DMA2D 硬件加速队列 API */
#include "LCD/tft_spi.h"   /* SPI 接口 TFT 初始化（用于 5571 面板）*/
#include "FreeRTOS.h"      /* FreeRTOS 运行时状态查询（taskSCHEDULER_RUNNING）*/
#include "task.h"          /* xTaskGetSchedulerState / vTaskDelay */


LTDC_HandleTypeDef  g_ltdc_handle;  /* LTDC HAL 句柄，HAL_LTDC_Init/HAL_LTDC_SetAddress 等全部依赖此结构体 */
DMA2D_HandleTypeDef g_dma2d_handle; /* DMA2D HAL 句柄，由 DMA2D_Accel_Init() 内部初始化后此处仅声明 */

static uint8_t s_ltdc_dwt_ready = 0u; /* DWT 计数器是否已初始化（0=未初始化，1=就绪）*/

/**
 * @brief  初始化 DWT 周期计数器（用于微秒级延时）
 * @note   DWT（Data Watchpoint and Trace）是 Cortex-M7 的调试模块，
 *         CYCCNT 寄存器在 480 MHz 时每纳秒计 0.48 次（约 2.08 ns/tick）。
 *         DEMCR_TRCENA 必须先置 1 才能使 DWT->CTRL 生效，
 *         然后 DWT_CTRL_CYCCNTENA 使 CYCCNT 开始自由运行计数。
 * @note   [注意] DWT 依赖调试接口使能（DBGMCU），在某些低功耗模式下可能失效。
 */
static void ltdc_dwt_init(void)
{
    if (s_ltdc_dwt_ready != 0u)
    {
        return; /* 已初始化，防止重复使能 */
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; /* 使能 DWT/ITM 跟踪模块 */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             /* 使能 CYCCNT 自由运行计数 */
    DWT->CYCCNT = 0u;                                 /* 归零计数器（可选，避免初始值影响第一次延时）*/
    s_ltdc_dwt_ready = 1u;                            /* 标记为已就绪 */
}

/**
 * @brief  基于 DWT 的微秒级精确延时
 * @param  us  延时微秒数（0 则立即返回）
 * @note   ticks = SystemCoreClock / 1_000_000 * us
 *         480 MHz 时 1 µs = 480 ticks，uint32 可覆盖约 8.9 秒。
 * @note   [改进] 不考虑中断抢占引起的误差；若在临界区外调用误差约为
 *         ±1 CPU 时钟周期，对 LCD 初始化时序来说完全足够。
 */
static void ltdc_delay_us(uint32_t us)
{
    uint32_t ticks; /* 目标 CPU 周期数 */
    uint32_t start; /* 记录起始 CYCCNT */

    if (us == 0u)
    {
        return; /* 0 microseconds，立即返回 */
    }

    ltdc_dwt_init();                               /* 确保 DWT 已就绪 */
    ticks = (SystemCoreClock / 1000000u) * us;     /* 计算目标周期数（480 MHz → 480 ticks/µs）*/
    start = DWT->CYCCNT;                           /* 快照当前 CYCCNT */
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { } /* 自旋等待（uint32 差值自然处理溢出）*/
}

/**
 * @brief  毫秒级延时（用于初始化时序，不依赖 RTOS tick）
 * @param  ms  延时毫秒数
 * @note   调用 ltdc_delay_us(1000)×ms，避免 RTOS 未启动时 vTaskDelay 失效。
 *         在 FreeRTOS 调度器启动前调用是安全的。
 */
static void ltdc_delay_ms(uint32_t ms)
{
    /* Keep LTDC reset delay independent from RTOS tick to avoid init stalls. */
    while (ms-- > 0u)
    {
        ltdc_delay_us(1000u); /* 每次循环精确延时 1 ms */
    }
}

#define delay_ms ltdc_delay_ms /* 将 delay_ms 宏重定向到 ltdc_delay_ms（与原有代码风格一致）*/
#define delay_us ltdc_delay_us /* 将 delay_us 宏重定向到 ltdc_delay_us */

/* ====================== 全局运行时变量 ====================== */
uint32_t *g_ltdc_framebuf[2];              /* 双帧缓冲地址数组：[0] 和 [1] 指向 SDRAM 中的帧缓冲区 */
_ltdc_dev lcdltdc;                         /* LTDC 设备参数描述符（面板尺寸、时序、像素格式等）*/
volatile uint32_t g_ltdc_init_stage = 0u; /* 初始化阶段码（用于诊断卡死位置，0=未初始化，70=完成）*/

/* === DMA2D 统计计数器（用于性能监控和故障诊断）=== */
volatile uint32_t g_ltdc_dma2d_timeout_count = 0u;       /* DMA2D 等待超时次数 */
volatile uint32_t g_ltdc_dma2d_transfer_count = 0u;      /* DMA2D 传输完成次数 */
volatile uint32_t g_ltdc_dma2d_sw_fallback_count = 0u;   /* DMA2D 回退至 CPU 软件绘制次数 */

/* === LTDC 状态统计 === */
volatile uint16_t g_ltdc_panel_id = 0u;              /* 识别到的面板 ID（非零 = 正常识别）*/
volatile uint32_t g_ltdc_swap_count = 0u;            /* 帧缓冲 swap 成功计数 */
volatile uint32_t g_ltdc_swap_pending_count = 0u;    /* 请求 swap 总次数 */
volatile uint32_t g_ltdc_swap_error_count = 0u;      /* swap 超时/错误次数 */
volatile uint32_t g_ltdc_fifo_underrun_count = 0u;   /* LTDC FIFO 欠载（FIFO Underrun）次数 */
volatile uint32_t g_ltdc_transfer_error_count = 0u;  /* LTDC 传输错误次数 */
volatile uint32_t g_ltdc_last_error_code = 0u;       /* 最近一次 LTDC 错误码 */

/* ====================== 双缓冲内部状态（ISR 与 task 共享，需临界区保护）====================== */
static volatile uint8_t s_front_buf_idx = 0u;       /* 当前被 LTDC 硬件扫描的帧缓冲索引（0 或 1）*/
static volatile uint8_t s_back_buf_idx = 0u;        /* 当前 CPU/DMA2D 正在绘制的帧缓冲索引 */
static volatile uint8_t s_swap_pending = 0u;        /* 1 = 有一次 swap 请求待处理（在下一 VSYNC 执行）*/
static volatile uint8_t s_swap_reload_pending = 0u; /* 1 = 已提交 VBR reload，等待 LTDC 确认切换 */
static volatile uint8_t s_swap_front_target_idx = 0u; /* 下次 swap 完成后的 front 缓冲索引 */
static volatile uint8_t s_swap_back_target_idx = 0u;  /* 下次 swap 完成后的 back 缓冲索引 */

/* ====================== 配置宏 ====================== */
#define LTDC_DMA2D_TIMEOUT_LOOP   0X1FFFFFU  /* DMA2D 软件超时循环计数（约 200 万次轮询）*/
#define LTDC_PRESENT_DIRECT_MODE  0u         /* 0=双缓冲 present；非零=直写模式（无 vsync 同步）*/

/**
 * @brief  返回当前绘制目标帧缓冲的基地址
 * @note   直写模式下返回 front 缓冲（无双缓冲），双缓冲模式下返回 back 缓冲。
 *         所有绘图函数通过此接口统一获取目标地址，切换缓冲只需修改索引。
 */
static uint32_t *ltdc_draw_buf_ptr(void)
{
#if (LTDC_PRESENT_DIRECT_MODE != 0u)
    return g_ltdc_framebuf[s_front_buf_idx]; /* 直写模式：直接操作 front 缓冲 */
#else
    return g_ltdc_framebuf[s_back_buf_idx];  /* 双缓冲模式：操作 back 缓冲，避免撕裂 */
#endif
}

/**
 * @brief  CPU 软件方式矩形填充单色（不经 DMA2D）
 * @param  psx,psy  左上角面板物理像素坐标
 * @param  pex,pey  右下角面板物理像素坐标
 * @param  color    填充颜色（格式取决于 LTDC_PIXFORMAT 宏）
 * @note   此函数为 DMA2D 超时/不可用时的降级实现（fallback）。
 *         RGB565 模式下约 1 byte/cycle，大区域慢于 DMA2D（48 MHz 带宽 vs 480 MHz 总线）。
 * @note   [改进] 使用 memset/wmemset 或 SIMD 可显著提速，但需保证对齐。
 */
static void ltdc_fill_sw_rect(uint32_t psx, uint32_t psy, uint32_t pex, uint32_t pey, uint32_t color)
{
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888
    /* ARGB8888：每像素 4 字节，逐行写入 uint32_t */
    uint32_t *base = (uint32_t *)ltdc_draw_buf_ptr(); /* 帧缓冲起始地址（uint32 步进）*/
    for (uint32_t y = psy; y <= pey; y++)
    {
        uint32_t *row = base + y * lcdltdc.pwidth + psx; /* 该行起始地址 */
        for (uint32_t x = psx; x <= pex; x++)
        {
            *row++ = color; /* 逐像素写入 32 位颜色 */
        }
    }
#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888
    /* RGB888：每像素 3 字节，需拆分写入（注意字节序：BGR 存储）*/
    uint8_t r = (uint8_t)((color >> 16) & 0xFFu); /* 红色分量 */
    uint8_t g = (uint8_t)((color >> 8) & 0xFFu);  /* 绿色分量 */
    uint8_t b = (uint8_t)(color & 0xFFu);          /* 蓝色分量 */
    uint8_t *base = (uint8_t *)ltdc_draw_buf_ptr(); /* 帧缓冲起始地址（uint8 步进）*/
    for (uint32_t y = psy; y <= pey; y++)
    {
        uint8_t *row = base + (y * lcdltdc.pwidth + psx) * 3u; /* 每像素 3 字节偏移 */
        for (uint32_t x = psx; x <= pex; x++)
        {
            row[0] = b;   /* 蓝色字节（低字节先存）*/
            row[1] = g;   /* 绿色字节 */
            row[2] = r;   /* 红色字节（高字节后存）*/
            row += 3;     /* 步进 3 字节到下一个像素 */
        }
    }
#else
    /* RGB565：每像素 2 字节，逐行写入 uint16_t */
    uint16_t c565 = (uint16_t)color; /* 截取低 16 位作为 RGB565 值 */
    uint16_t *base = (uint16_t *)ltdc_draw_buf_ptr(); /* 帧缓冲起始地址（uint16 步进）*/
    for (uint32_t y = psy; y <= pey; y++)
    {
        uint16_t *row = base + y * lcdltdc.pwidth + psx; /* 该行起始物理地址 */
        for (uint32_t x = psx; x <= pex; x++)
        {
            *row++ = c565; /* 逐像素写入 16 位颜色 */
        }
    }
#endif
}

/**
 * @brief  CPU 软件方式将 RGB565 颜色数组填充到矩形区域
 * @param  psx,psy  左上角物理像素坐标
 * @param  pex,pey  右下角物理像素坐标
 * @param  color    源颜色数组（逐行、逐像素排列，宽度 = pex-psx+1）
 * @note   仅支持 RGB565 格式（其他格式编译为空操作），DMA2D 不可用时的 fallback。
 * @note   [改进] 非 RGB565 格式未实现，使用其他格式时需补充。
 */
static void ltdc_color_fill_sw_rect(uint32_t psx, uint32_t psy, uint32_t pex, uint32_t pey, const uint16_t *color)
{
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB565
    uint32_t width = pex - psx + 1u;                   /* 矩形宽度（像素数）*/
    uint16_t *base = (uint16_t *)ltdc_draw_buf_ptr();   /* 帧缓冲 uint16 基址 */

    for (uint32_t y = psy; y <= pey; y++)
    {
        uint16_t *row = base + y * lcdltdc.pwidth + psx; /* 目标行起始地址 */
        for (uint32_t x = 0u; x < width; x++)
        {
            row[x] = *color++; /* 逐像素拷贝源颜色 */
        }
    }
#else
    /* 非 RGB565 格式：参数无用，编译器警告消除 */
    (void)psx;
    (void)psy;
    (void)pex;
    (void)pey;
    (void)color;
#endif
}

/**
 * @brief  CPU 软件方式将 RGB565 图像数据复制到矩形区域（支持非连续源行）
 * @param  psx,psy    目标区域左上角物理坐标
 * @param  pex,pey    目标区域右下角物理坐标
 * @param  src        源图像数据（行优先，每行步长 src_stride 像素）
 * @param  src_stride 源行步长（像素数，≥ 矩形宽度，用于处理有 padding 的图像）
 * @note   src_stride > width 时跳过每行末尾的 padding 像素（LVGL flush 常见场景）。
 */
static void ltdc_copy_sw_rect(uint32_t psx,
                              uint32_t psy,
                              uint32_t pex,
                              uint32_t pey,
                              const uint16_t *src,
                              uint16_t src_stride)
{
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB565
    uint32_t width = pex - psx + 1u;                    /* 矩形宽度 */
    uint16_t *base = (uint16_t *)ltdc_draw_buf_ptr();    /* 目标帧缓冲基址 */

    for (uint32_t y = psy; y <= pey; y++)
    {
        uint16_t *dst_row = base + y * lcdltdc.pwidth + psx;                   /* 目标行地址 */
        const uint16_t *src_row = src + (uint32_t)(y - psy) * (uint32_t)src_stride; /* 源行地址（乘以步长跨越 padding）*/

        for (uint32_t x = 0u; x < width; x++)
        {
            dst_row[x] = src_row[x]; /* 逐像素拷贝 */
        }
    }
#else
    /* 非 RGB565 格式：参数仅消除编译器警告 */
    (void)psx;
    (void)psy;
    (void)pex;
    (void)pey;
    (void)src;
    (void)src_stride;
#endif
}
/**
 * @brief       LTDC 总开关（使能/禁用整个 LTDC 输出）
 * @param       sw         1:使能 LTDC 输出；0:关闭 LTDC（屏幕无显示）
 * @retval      无
 * @note   关闭 LTDC 后屏幕会显示背景色（由 g_ltdc_handle.Init.Backcolor 决定）。
 *         通常不需要频繁关闭，省电模式下可关闭。
 */
void ltdc_switch(uint8_t sw)
{
    if (sw)
    {
        __HAL_LTDC_ENABLE(&g_ltdc_handle);   /* 打开LTDC：设置 LTDC->GCR.LTDCEN 位 */
    }
    else
    {
        __HAL_LTDC_DISABLE(&g_ltdc_handle);  /* 关闭LTDC：清除 LTDC->GCR.LTDCEN 位 */
    }
}

/**
 * @brief       图层开关（使能/禁用指定图层）
 * @param       layerx     图层编号（0 或 1，STM32H7 LTDC 支持最多 2 层）
 * @param       sw         1:使能；0:关闭
 * @retval      无
 * @note   修改图层状态后必须调用 __HAL_LTDC_RELOAD_CONFIG 立即生效，
 *         否则修改在下一个 VSYNC 周期才会被加载。
 */
void ltdc_layer_switch(uint8_t layerx, uint8_t sw)
{
    if (sw) 
    {
        __HAL_LTDC_LAYER_ENABLE(&g_ltdc_handle, layerx);   /* 开启 layerx */
    }
    else
    {
        __HAL_LTDC_LAYER_DISABLE(&g_ltdc_handle, layerx);  /* 关闭 layerx */
    }

    __HAL_LTDC_RELOAD_CONFIG(&g_ltdc_handle); /* 立即重新加载配置（同步模式，不等待 VSYNC）*/
}

/**
 * @brief       选择当前活动图层（影响 lcdltdc.activelayer，供上层绘图使用）
 * @param       layerx     图层编号（0 或 1）
 * @retval      无
 * @note   此函数只修改软件变量，不影响 LTDC 硬件。NECCS 项目目前仅使用图层 0。
 */
void ltdc_select_layer(uint8_t layerx)
{
    lcdltdc.activelayer = layerx; /* 记录当前活动图层（供后续绘图 API 使用）*/
}

/**
 * @brief  获取当前 front 帧缓冲（LTDC 硬件正在扫描输出的缓冲）的物理地址
 * @retval uint32_t  帧缓冲地址
 */
uint32_t ltdc_get_frontbuf_addr(void)
{
    return (uint32_t)g_ltdc_framebuf[s_front_buf_idx]; /* 返回 front 缓冲的 uint32 地址 */
}

/**
 * @brief  获取当前 back 帧缓冲（CPU/DMA2D 可安全写入的缓冲）的物理地址
 * @retval uint32_t  帧缓冲地址
 * @note   直写模式下等同于 front 缓冲（无双缓冲）。
 */
uint32_t ltdc_get_backbuf_addr(void)
{
#if (LTDC_PRESENT_DIRECT_MODE != 0u)
    return (uint32_t)g_ltdc_framebuf[s_front_buf_idx]; /* 直写模式：front = back */
#else
    return (uint32_t)g_ltdc_framebuf[s_back_buf_idx];  /* 双缓冲模式：返回 back 缓冲 */
#endif
}

/**
 * @brief  从 LTDC 硬件寄存器读取当前 front 缓冲索引（中断安全）
 * @retval front 缓冲索引（0 或 1），0xFF = 两个地址都不匹配（出错）
 * @note   LTDC_Layer1->CFBAR（Color Frame Buffer Address Register）存储当前
 *         LTDC 正在扫描的帧缓冲起始地址，比较与 g_ltdc_framebuf[0/1] 确定索引。
 *         此函数应在临界区（禁中断）内调用，防止与 swap 中断竞态。
 */
static uint8_t ltdc_front_index_from_hw_locked(void)
{
    uint32_t front_addr = LTDC_Layer1->CFBAR; /* 读取 Layer1 的帧缓冲地址寄存器 */

    if (front_addr == (uint32_t)g_ltdc_framebuf[0])
    {
        return 0u; /* LTDC 当前扫描 framebuf[0] */
    }
    if (front_addr == (uint32_t)g_ltdc_framebuf[1])
    {
        return 1u; /* LTDC 当前扫描 framebuf[1] */
    }

    return 0xFFu; /* 不匹配：可能 framebuf 尚未初始化或地址被篡改 */
}

/**
 * @brief  从硬件寄存器同步 s_front_buf_idx / s_back_buf_idx（中断安全）
 * @note   在 swap 完成确认、swap 超时恢复等时机调用，确保软件状态与硬件一致。
 *         若读出的 front_idx 无效（0xFF），不修改软件状态（保持上次值）。
 */
static void ltdc_sync_indices_from_hw_locked(void)
{
    uint8_t front_idx = ltdc_front_index_from_hw_locked(); /* 从寄存器读出当前 front 索引 */

    if (front_idx < 2u)
    {
        s_front_buf_idx = front_idx;           /* 更新 front 缓冲索引 */
        s_back_buf_idx = (uint8_t)(front_idx ^ 1u); /* back = 另一个缓冲（0^1=1, 1^1=0）*/
    }
    /* front_idx == 0xFF：硬件地址异常，不修改软件状态 */
}

/**
 * @brief  swap 超时后强制恢复双缓冲状态（中断安全）
 * @note   当 ltdc_wait_for_swap_complete 超时时调用，防止 s_swap_pending 永久置位
 *         导致本帧之后所有 swap 请求都被跳过。
 *         恢复策略：优先从硬件读取真实 front 索引；若硬件地址异常，使用目标索引。
 *         同时递增错误计数器用于诊断。
 */
static void ltdc_recover_swap_timeout_locked(void)
{
    uint8_t front_idx = ltdc_front_index_from_hw_locked(); /* 尝试从硬件恢复 */

    if (front_idx < 2u)
    {
        s_front_buf_idx = front_idx;
        s_back_buf_idx = (uint8_t)(front_idx ^ 1u);
    }
    else
    {
        /* 硬件地址无效：退而使用预期目标索引（不一定准确，但至少不卡死）*/
        s_front_buf_idx = s_swap_front_target_idx;
        s_back_buf_idx = s_swap_back_target_idx;
    }

    s_swap_reload_pending = 0u; /* 清除 reload 等待标志 */
    s_swap_pending = 0u;        /* 清除 swap 请求标志 */
    g_ltdc_swap_error_count++;  /* 递增错误计数（供诊断）*/
}

/**
 * @brief  尝试完成已挂起的 swap（检查 LTDC VBR 标志，中断安全）
 * @note   VBR（Vertical Blanking Reload）位在垂直消隐期 reload 完成后由硬件清零。
 *         此函数在 VSYNC 相关回调和 is_swap_pending 中轮询调用。
 *         若 VBR = 0 说明 reload 已完成，正式完成帧交换并更新索引。
 */
static void ltdc_try_complete_swap_locked(void)
{
    if ((s_swap_reload_pending != 0u) &&
        ((g_ltdc_handle.Instance->SRCR & LTDC_SRCR_VBR) == 0u)) /* VBR=0：reload 已完成 */
    {
        s_front_buf_idx = s_swap_front_target_idx; /* swap 完成：前台缓冲切换为新 front */
        s_back_buf_idx = s_swap_back_target_idx;   /* back 切换为旧 front（现可安全写入）*/
        s_swap_reload_pending = 0u;                /* 清除 reload 等待 */
        s_swap_pending = 0u;                       /* 清除 swap 请求 */
        g_ltdc_swap_count++;                       /* 记录成功 swap 次数 */
    }
}

/**
 * @brief  请求在下一个 VSYNC 执行帧缓冲交换（非阻塞）
 * @note   双缓冲 present 流程：
 *         1. CPU/DMA2D 向 back 缓冲绘制完整一帧
 *         2. 调用 ltdc_request_swap() 标记 swap 请求
 *         3. 下一个行中断（VSYNC 附近）在 HAL_LTDC_LineEventCallback 中
 *            调用 HAL_LTDC_SetAddress_NoReload + HAL_LTDC_Reload(VBR)
 *         4. LTDC 在垂直消隐期完成重载，s_swap_reload_pending 由 ReloadCallback 清除
 * @note   直写模式（LTDC_PRESENT_DIRECT_MODE）：直接剔除 swap 机制，无需等待。
 * @note   [注意] 此函数通过 __disable_irq/__enable_irq 实现原子操作，
 *         不适合在已关中断的 ISR 内再次调用（会重复关中断）。
 */
void ltdc_request_swap(void)
{
#if (LTDC_PRESENT_DIRECT_MODE != 0u)
    /* 直写模式无双缓冲：back 和 front 指向同一缓冲，swap 无意义 */
    s_back_buf_idx = s_front_buf_idx;
    s_swap_reload_pending = 0u;
    s_swap_pending = 0u;
    return;
#else
    uint32_t primask = __get_PRIMASK(); /* 保存当前中断使能状态 */
    __disable_irq();                    /* 进入临界区：防止与 LineEventCallback ISR 竞态 */
    ltdc_sync_indices_from_hw_locked(); /* 先从硬件同步，确保索引一致 */
    if ((s_swap_pending == 0u) && (s_swap_reload_pending == 0u))
    {
        s_swap_pending = 1u;           /* 标记 swap 请求（同一帧内仅允许一次）*/
        g_ltdc_swap_pending_count++;   /* 统计总 swap 请求次数 */
    }
    /* 若已有 swap_pending 或 reload_pending，丢弃本次请求（避免队列溢出）*/
    if (primask == 0u)
    {
        __enable_irq(); /* 恢复中断（仅当进入时中断是开启的才恢复）*/
    }
#endif
}

/**
 * @brief  查询当前是否有帧缓冲 swap 请求挂起（非阻塞）
 * @retval 0 = 无挂起（swap 已完成或未请求）；非零 = 仍有 swap 在等待
 * @note   内部先尝试完成 swap（ltdc_try_complete_swap_locked），
 *         若 swap 已完成则同步索引；仅在确实仍挂起时返回非零。
 *         直写模式永远返回 0。
 */
uint8_t ltdc_is_swap_pending(void)
{
#if (LTDC_PRESENT_DIRECT_MODE != 0u)
    return 0u; /* 直写模式无双缓冲，永远不挂起 */
#else
    uint8_t pending;                           /* 本次查询返回值 */
    uint32_t primask = __get_PRIMASK();        /* 保存中断状态 */

    __disable_irq();                           /* 临界区：防止与 LineEventCallback 竞态 */
    ltdc_try_complete_swap_locked();           /* 轮询：VBR 已清零则完成 swap */
    if (s_swap_pending == 0u)
    {
        ltdc_sync_indices_from_hw_locked();    /* swap 完成后从寄存器同步索引 */
    }
    pending = s_swap_pending;                  /* 快照状态 */
    if (primask == 0u)
    {
        __enable_irq();                        /* 恢复中断 */
    }

    return pending; /* 0=无挂起，1=仍在等待 */
#endif
}

/**
 * @brief  阻塞等待帧缓冲 swap 完成，超时后强制恢复
 * @param  timeout_ms  超时毫秒数；0=仅查询不等待（非阻塞）
 * @retval 0 = 成功完成（swap 完成或直写模式）
 * @note   等待策略：
 *         - FreeRTOS 调度器运行中：每次轮询后 vTaskDelay(1ms)，让出 CPU
 *         - 调度器未运行（初始化阶段）：HAL_Delay(1ms) 忙等
 *         超时后调用 ltdc_recover_swap_timeout_locked() 强制恢复状态，
 *         即使 LTDC 硬件可能处于不一致状态也能继续运行（宁可有一帧撕裂
 *         也不卡死整个系统）。
 * @note   [改进] 可以改用信号量等待而非轮询，进一步降低 CPU 占用率。
 */
uint8_t ltdc_wait_for_swap_complete(uint32_t timeout_ms)
{
#if (LTDC_PRESENT_DIRECT_MODE != 0u)
    (void)timeout_ms; /* 直写模式：参数无用 */
    return 0u;        /* 直写模式永远返回 0（立即完成）*/
#else
    uint32_t start_tick = HAL_GetTick(); /* 记录等待起始毫秒数 */

    for (;;)
    {
        uint8_t pending;
        uint32_t primask = __get_PRIMASK();

        __disable_irq();                           /* 临界区 */
        ltdc_try_complete_swap_locked();           /* 轮询 swap 完成 */
        pending = s_swap_pending;                  /* 快照状态 */
        if (primask == 0u)
        {
            __enable_irq();
        }

        if (pending == 0u)
        {
            return 0u; /* swap 完成，返回成功 */
        }

        if (timeout_ms == 0u)
        {
            break; /* 非阻塞模式：立即退出（仍挂起）*/
        }
        if ((HAL_GetTick() - start_tick) >= timeout_ms)
        {
            break; /* 超时：退出并执行强制恢复 */
        }

        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            vTaskDelay(pdMS_TO_TICKS(1u)); /* 调度器已启动：让出 CPU，1 ms 后重试 */
        }
        else
        {
            HAL_Delay(1u); /* 调度器未启动（初始化阶段）：HAL 阻塞延时 1 ms */
        }
    }

    /* 超时恢复：强制清除 swap_pending，防止后续帧永远等不到 swap 完成 */
    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        ltdc_recover_swap_timeout_locked(); /* 强制同步缓冲索引状态 */
        if (primask == 0u)
        {
            __enable_irq();
        }
    }

    return 0u; /* 超时也返回 0，上层不做区分（宁可继续运行）*/
#endif
}

/**
 * @brief       设置显示方向（横屏/竖屏映射）
 * @param       dir  0:竖屏（portrait）  1:横屏（landscape）
 * @retval      无
 * @note   此函数只修改 lcdltdc.width/height 的逻辑尺寸，
 *         不修改 LTDC 硬件寄存器（物理尺寸 pwidth/pheight 不变）。
 *         上层坐标系（逻辑坐标）通过 ltdc_rect_to_panel 映射到物理坐标。
 *         NECCS 项目使用横屏（dir=1），1024×600。
 */
void ltdc_display_dir(uint8_t dir)
{
    lcdltdc.dir = dir; /* 保存方向标志（0=竖屏，1=横屏）*/

    if (dir == 0)      /* 竖屏：逻辑宽=物理高，逻辑高=物理宽（旋转90°）*/
    {
        lcdltdc.width = lcdltdc.pheight;  /* 竖屏逻辑宽度 = 面板物理高度 */
        lcdltdc.height = lcdltdc.pwidth;  /* 竖屏逻辑高度 = 面板物理宽度 */
    }
    else if (dir == 1) /* 横屏：逻辑宽=物理宽，逻辑高=物理高（不旋转）*/
    {
        lcdltdc.width = lcdltdc.pwidth;   /* 横屏逻辑宽度 = 面板物理宽度 */
        lcdltdc.height = lcdltdc.pheight; /* 横屏逻辑高度 = 面板物理高度 */
    }
}

/**
 * @brief  将逻辑矩形坐标映射到面板物理坐标（内部辅助函数）
 * @param  sx,sy  逻辑左上角坐标（基于当前显示方向）
 * @param  ex,ey  逻辑右下角坐标
 * @param  psx,psy  [out] 物理左上角坐标
 * @param  pex,pey  [out] 物理右下角坐标
 * @retval 1 = 映射成功，0 = 参数无效或越界
 * @note   横屏（dir=1）：逻辑坐标 = 物理坐标，无需变换。
 *         竖屏（dir=0）：坐标需旋转 90°，变换公式为：
 *           psx = sy, psy = pheight - ex - 1, pex = ey, pey = pheight - sx - 1
 *         此变换将逻辑上的"把屏幕顺时针旋转使用"映射到物理位置。
 * @note   [改进] 仅支持 0°横屏和 90°竖屏旋转，不支持 180°/270°。
 */
static uint8_t ltdc_rect_to_panel(uint16_t sx,
                                  uint16_t sy,
                                  uint16_t ex,
                                  uint16_t ey,
                                  uint32_t *psx,
                                  uint32_t *psy,
                                  uint32_t *pex,
                                  uint32_t *pey)
{
    if ((psx == NULL) || (psy == NULL) || (pex == NULL) || (pey == NULL))
    {
        return 0u; /* 输出指针为空，防止空指针写入 */
    }

    if ((lcdltdc.width == 0u) || (lcdltdc.height == 0u))
    {
        return 0u; /* LTDC 尚未初始化，逻辑尺寸无效 */
    }
    if ((sx >= lcdltdc.width) || (sy >= lcdltdc.height))
    {
        return 0u; /* 起始坐标超出显示区域 */
    }
    if (ex >= lcdltdc.width)
    {
        ex = lcdltdc.width - 1u;  /* 裁剪 ex 到逻辑宽度边界 */
    }
    if (ey >= lcdltdc.height)
    {
        ey = lcdltdc.height - 1u; /* 裁剪 ey 到逻辑高度边界 */
    }
    if ((ex < sx) || (ey < sy))
    {
        return 0u; /* 裁剪后矩形为空（ex < sx 或 ey < sy）*/
    }

    if (lcdltdc.dir != 0u) /* 横屏：逻辑坐标 == 物理坐标，直接赋值 */
    {
        *psx = sx;
        *psy = sy;
        *pex = ex;
        *pey = ey;
    }
    else /* 竖屏：旋转 90° 映射（顺时针）*/
    {
        /* 防止旋转后坐标超出物理高度 */
        if (ex >= lcdltdc.pheight)
        {
            ex = lcdltdc.pheight - 1u;
        }
        if (sx >= lcdltdc.pheight)
        {
            sx = lcdltdc.pheight - 1u;
        }
        /* 竖屏旋转坐标变换：
         * 物理 x = 逻辑 y（原来纵放变横放）
         * 物理 y = pheight - 逻辑 x - 1（Y 轴翻转，使逻辑原点在左下角）*/
        *psx = sy;
        *psy = lcdltdc.pheight - ex - 1u;
        *pex = ey;
        *pey = lcdltdc.pheight - sx - 1u;
    }

    return 1u; /* 映射成功 */
}

/**
 * @brief       在 back 缓冲中画一个点（单像素写入）
 * @param       x, y    逻辑坐标（已做方向映射再写入物理帧缓冲）
 * @param       color   颜色值（格式由 LTDC_PIXFORMAT 决定）
 * @retval      无
 * @note   地址计算公式（横屏）：addr = buf_base + pixsize × (pwidth × y + x)
 *         每次调用只写一个像素，频繁调用性能较差；
 *         大面积绘制建议使用 ltdc_fill 或 DMA2D。
 * @note   [改进] 不做越界检查，逻辑坐标 ≥ 面板尺寸时会越界写 SDRAM（缓冲区溢出）。
 */
void ltdc_draw_point(uint16_t x, uint16_t y, uint32_t color)
{ 
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888
    /* ARGB8888：每像素 4 字节，直接写 uint32_t */
    if (lcdltdc.dir) /* 横屏：物理地址 = base + 4×(pwidth×y + x) */
    {
        *(uint32_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) = color;
    }
    else /* 竖屏：旋转 90°，物理地址 = base + 4×(pwidth×(pheight-x-1) + y) */
    {
        *(uint32_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) = color;
    }

#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888
    /* RGB888：每像素 3 字节，低16位写 uint16，高8位写 uint8 */
    if (lcdltdc.dir) /* 横屏 */
    {
        *(uint16_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) = color;       /* 写低 16 位（GB）*/
        *(uint8_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x) + 2) = color >> 16; /* 写高 8 位（R）*/
    }
    else /* 竖屏：旋转 90° 映射 */
    {
        *(uint16_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) = color;
        *(uint8_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y) + 2) = color >> 16;
    }
    
#else
    /* RGB565（默认）：每像素 2 字节，写 uint16_t */
    if (lcdltdc.dir) /* 横屏 */
    {
        *(uint16_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) = color;
    }
    else /* 竖屏：旋转 90° */
    {
        *(uint16_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) = color;
    }

#endif
}

/**
 * @brief       从 back 缓冲读取一个点的颜色值
 * @param       x, y    逻辑坐标
 * @retval      像素颜色值（格式由 LTDC_PIXFORMAT 决定）
 * @note   [注意] 读的是 back 缓冲而非 front 缓冲，若 swap 已提交但未完成，
 *         读出的值为上一帧数据；建议在绘制完成后 swap 前读取。
 * @note   [改进] 不做越界检查，同 ltdc_draw_point。
 */
uint32_t ltdc_read_point(uint16_t x, uint16_t y)
{ 
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888
    /* ARGB8888：返回 uint32_t（完整 32 位颜色）*/
    if (lcdltdc.dir) /* 横屏 */
    {
        return *(uint32_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x));
    }
    else /* 竖屏 */
    {
        return *(uint32_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y));
    }

#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888
    /* RGB888：读 32 位然后掩码取低 24 位（去除最高字节），返回 0x00RRGGBB */
    if (lcdltdc.dir) /* 横屏 */
    {
        return *(uint32_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) & 0XFFFFFF;
    }
    else /* 竖屏 */
    {
        return *(uint32_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) & 0XFFFFFF;
    }
    
#else
    /* RGB565：读 uint16_t，返回 16 位颜色 */
    if (lcdltdc.dir) /* 横屏 */
    {
        return *(uint16_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * y + x));
    }
    else /* 竖屏 */
    {
        return *(uint16_t *)((uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y));
    }

#endif
}

/**
 * @brief  异步矩形填充单色（优先使用 DMA2D，不可用时回退到 CPU）
 * @param  sx,sy  逻辑左上角坐标   ex,ey  逻辑右下角坐标
 * @param  color  填充颜色（32/24/16 位取决于 LTDC_PIXFORMAT）
 * @retval 1=成功（已提交 DMA2D 或 CPU 写入完成），0=失败（坐标越界或 DMA2D 繁忙且回退失败）
 * @note   "async"指 DMA2D 提交后立即返回，实际 DMA 传输在后台进行；
 *         调用方须在 swap 前调用 ltdc_draw_flush 等待 DMA2D 完成。
 *         offline = pwidth - rect_width：DMA2D 行末地址跳过量（行间跨度）。
 * @note   [改进] 回退到 CPU 时返回 0，但上层 ltdc_fill 仍能继续工作。
 */
uint8_t ltdc_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    uint32_t psx; /* 物理左上角 X */
    uint32_t psy; /* 物理左上角 Y */
    uint32_t pex; /* 物理右下角 X */
    uint32_t pey; /* 物理右下角 Y */
    uint16_t offline; /* DMA2D 行末跳过像素数（= pwidth - 矩形宽度）*/
    uint32_t addr;    /* 目标起始地址（帧缓冲物理地址）*/

    if (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) == 0u)
    {
        return 0u; /* 坐标无效（越界或矩形为空）*/
    }

#if (LTDC_ENABLE_DMA2D == 0)
    ltdc_fill_sw_rect(psx, psy, pex, pey, color); /* DMA2D 禁用：直接 CPU 写入 */
    return 1u;
#else
    offline = (uint16_t)(lcdltdc.pwidth - (pex - psx + 1u)); /* 计算 DMA2D 行偏移量 */
    /* pixsize × (pwidth × psy + psx)：矩形起始像素的字节偏移 */
    addr = (uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx);
    if (DMA2D_Accel_EnqueueFill(addr,
                                (uint16_t)(pex - psx + 1u), /* 矩形宽度（像素）*/
                                (uint16_t)(pey - psy + 1u), /* 矩形高度（像素）*/
                                offline,                    /* 行末跳跃（像素）*/
                                color) == 0u)               /* 填充颜色 */
    {
        /* DMA2D 队列满/繁忙：回退到 CPU 软件填充 */
        g_ltdc_dma2d_sw_fallback_count++;
        ltdc_fill_sw_rect(psx, psy, pex, pey, color);
        return 0u; /* 返回 0 表示 DMA2D 未使用（上层可知晓）*/
    }
    return 1u; /* DMA2D 任务已入队，将在后台异步执行 */
#endif
}

/**
 * @brief  异步将 RGB565 颜色数组填充到矩形（每个像素独立颜色，优先 DMA2D）
 * @param  sx,sy  逻辑左上角    ex,ey  逻辑右下角
 * @param  color  颜色数组（逐行排列，总长度 = 矩形面积像素数）
 * @retval 1=成功，0=失败
 * @note   与 ltdc_fill_async 区别：此函数填充"非均匀色"（如渐变、图像数据）。
 *         DMA2D M2M（内存到内存）传输；源行 offline = 0（紧密排列）。
 * @note   [注意] color 数组须在 DMA2D 完成前保持有效（不可为栈上局部变量）。
 */
uint8_t ltdc_color_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint32_t psx, psy, pex, pey;
    uint16_t offline; /* 目标行末跳过量 */
    uint32_t addr;    /* 目标起始地址 */

    if ((color == NULL) || (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) == 0u))
    {
        return 0u; /* 空指针或坐标越界 */
    }

#if (LTDC_ENABLE_DMA2D == 0)
    ltdc_color_fill_sw_rect(psx, psy, pex, pey, color); /* DMA2D 禁用：CPU 逐像素拷贝 */
    return 1u;
#else
    offline = (uint16_t)(lcdltdc.pwidth - (pex - psx + 1u)); /* 目标帧缓冲行末跳过量 */
    addr = (uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx);
    if (DMA2D_Accel_EnqueueCopy((uint32_t)color,        /* 源地址（RGB565 数组）*/
                                addr,                    /* 目标帧缓冲地址 */
                                (uint16_t)(pex - psx + 1u), /* 宽度 */
                                (uint16_t)(pey - psy + 1u), /* 高度 */
                                0u,                      /* 源行 offline=0（紧密排列）*/
                                offline) == 0u)          /* 目标行 offline */
    {
        g_ltdc_dma2d_sw_fallback_count++;
        ltdc_color_fill_sw_rect(psx, psy, pex, pey, color);
        return 0u;
    }
    return 1u;
#endif
}

/**
 * @brief  异步复制 RGB565 图像到矩形（支持非连续源行，优先 DMA2D）
 * @param  sx,sy   逻辑目标左上角    ex,ey  逻辑目标右下角
 * @param  src     源图像数据（行步长 = src_stride 像素）
 * @param  src_stride 源行步长（通常 = LCD 宽度，LVGL flush 场景常见）
 * @retval 1=成功，0=失败
 * @note   src_stride ≥ 矩形宽度，否则行间地址计算错误（直接返回 0）。
 *         源行 offline = src_stride - rect_width（跳过每行末尾的 padding）。
 */
uint8_t ltdc_copy_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint16_t *src, uint16_t src_stride)
{
    uint32_t psx, psy, pex, pey;
    uint16_t width;   /* 矩形物理宽度（像素）*/
    uint16_t height;  /* 矩形物理高度（像素）*/
    uint16_t offline; /* 目标帧缓冲行末跳过量 */
    uint32_t addr;    /* 目标帧缓冲起始地址 */

    if ((src == NULL) || (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) == 0u))
    {
        return 0u; /* 空源指针或坐标无效 */
    }

    width = (uint16_t)(pex - psx + 1u);   /* 物理矩形宽度 */
    height = (uint16_t)(pey - psy + 1u);  /* 物理矩形高度 */
    if (src_stride < width)
    {
        return 0u; /* 源行步长小于矩形宽度：非法，防止行间地址计算溢出 */
    }

#if (LTDC_ENABLE_DMA2D == 0)
    ltdc_copy_sw_rect(psx, psy, pex, pey, src, src_stride); /* DMA2D 禁用：CPU 拷贝 */
    return 1u;
#else
    offline = (uint16_t)(lcdltdc.pwidth - width); /* 目标行末跳过量 */
    addr = (uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx);
    if (DMA2D_Accel_EnqueueCopy((uint32_t)src,          /* 源地址（RGB565）*/
                                addr,                    /* 目标地址（帧缓冲）*/
                                width,                   /* 传输宽度（像素）*/
                                height,                  /* 传输高度（行数）*/
                                (uint16_t)(src_stride - width), /* 源行 offline（跳过 padding）*/
                                offline) == 0u)          /* 目标行 offline */
    {
        g_ltdc_dma2d_sw_fallback_count++;
        ltdc_copy_sw_rect(psx, psy, pex, pey, src, src_stride); /* DMA2D 繁忙：CPU 回退 */
        return 0u;
    }
    return 1u;
#endif
}

/**
 * @brief  异步将 L8（8位灰度索引）图像绘制到矩形（DMA2D L8→RGB565 颜色格式转换）
 * @param  sx,sy   逻辑目标左上角    ex,ey  逻辑目标右下角
 * @param  src_l8  源 L8 数据（每像素 1 字节，灰度值 0-255）
 * @param  src_stride 源行步长（像素，≥ 矩形宽度）
 * @retval 1=成功，0=失败或 DMA2D 禁用
 * @note   L8 格式用于热力图声学相机叠加：8位灰度 → 查色表 → 彩色像素。
 *         DMA2D 硬件使用颜色查找表（CLUT）完成转换，CPU 无需介入。
 * @note   [注意] DMA2D 禁用时无软件回退（不支持 L8 格式 CPU 转换），直接返回 0。
 */
uint8_t ltdc_l8_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint8_t *src_l8, uint16_t src_stride)
{
    uint32_t psx, psy, pex, pey;
    uint16_t offline; /* 目标行末跳过量 */
    uint32_t addr;    /* 目标帧缓冲起始地址 */
    uint16_t width;   /* 矩形物理宽度 */
    uint16_t height;  /* 矩形物理高度 */

    if ((src_l8 == NULL) || (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) == 0u))
    {
        return 0u; /* 空指针或坐标无效 */
    }

    width = (uint16_t)(pex - psx + 1u);   /* 物理矩形宽度 */
    height = (uint16_t)(pey - psy + 1u);  /* 物理矩形高度 */
    if (src_stride < width)
    {
        return 0u; /* 源行步长不足 */
    }

#if (LTDC_ENABLE_DMA2D == 0)
    (void)src_l8;     /* DMA2D 禁用：L8 格式无 CPU 回退，忽略参数 */
    (void)src_stride;
    return 0u; /* 无法处理 L8，直接返回失败 */
#else
    offline = (uint16_t)(lcdltdc.pwidth - width); /* 目标行末跳过量 */
    addr = (uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx);
    if (DMA2D_Accel_EnqueueBlitL8((uint32_t)src_l8, /* L8 源数据地址 */
                                  addr,              /* 目标帧缓冲地址 */
                                  width,             /* 宽度（像素）*/
                                  height,            /* 高度（行数）*/
                                  (uint16_t)(src_stride - width), /* 源行 offline */
                                  offline) == 0u)    /* 目标行 offline */
    {
        g_ltdc_dma2d_sw_fallback_count++; /* DMA2D 繁忙，无回退，仅计数 */
        return 0u;
    }
    return 1u; /* L8 传输任务已入队 */
#endif
}

/**
 * @brief  异步将 A8（8位透明度）掩码以指定颜色混合到矩形（字体渲染）
 * @param  sx,sy    逻辑目标左上角    ex,ey  逻辑目标右下角
 * @param  src_a8   A8 透明度掩码（0=全透明，255=不透明）
 * @param  src_stride 源行步长（像素）
 * @param  color565   混合前景色（RGB565 格式）
 * @retval 1=成功，0=失败
 * @note   用于 LVGL 字体渲染：字体位图为 A8 格式，每像素透明度不同。
 *         DMA2D 将 A8 掩码 + 前景色 + 目标缓冲中的背景色混合输出。
 *         公式：dst = src_a8/255 × color565 + (1 - src_a8/255) × bg
 * @note   [注意] DMA2D 禁用时无 CPU 回退，A8 混合是 DMA2D 硬件专属功能。
 */
uint8_t ltdc_a8_blend_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint8_t *src_a8, uint16_t src_stride, uint16_t color565)
{
    uint32_t psx, psy, pex, pey;
    uint16_t offline; /* 目标行末跳过量 */
    uint32_t addr;    /* 目标帧缓冲起始地址 */
    uint16_t width;   /* 物理矩形宽度 */
    uint16_t height;  /* 物理矩形高度 */

    if ((src_a8 == NULL) || (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) == 0u))
    {
        return 0u; /* 空指针或坐标越界 */
    }

    width = (uint16_t)(pex - psx + 1u);
    height = (uint16_t)(pey - psy + 1u);
    if (src_stride < width)
    {
        return 0u; /* 行步长不足 */
    }

#if (LTDC_ENABLE_DMA2D == 0)
    /* DMA2D 禁用：A8 混合无 CPU 实现，忽略参数 */
    (void)src_a8;
    (void)src_stride;
    (void)color565;
    return 0u;
#else
    offline = (uint16_t)(lcdltdc.pwidth - width);
    addr = (uint32_t)ltdc_draw_buf_ptr() + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx);
    if (DMA2D_Accel_EnqueueBlendA8((uint32_t)src_a8,         /* A8 掩码源地址 */
                                   addr,                     /* 目标帧缓冲地址 */
                                   width,                    /* 宽度 */
                                   height,                   /* 高度 */
                                   (uint16_t)(src_stride - width), /* 源行 offline */
                                   offline,                  /* 目标行 offline */
                                   color565) == 0u)          /* RGB565 前景色 */
    {
        g_ltdc_dma2d_sw_fallback_count++; /* A8 混合无 CPU 回退，仅计数 */
        return 0u;
    }
    return 1u; /* A8 混合任务已入队 */
#endif
}

/**
 * @brief  等待 DMA2D 硬件完成所有挂起操作（同步屏障）
 * @param  timeout_loop  超时循环计数（0 = 不等待直接返回当前状态）
 * @retval 0 = 完成（或 DMA2D 禁用），非零 = 超时仍未完成
 * @note   在 ltdc_fill/ltdc_color_fill 等同步 API 内部调用，
 *         确保 DMA2D 实际写入帧缓冲后才返回给调用方。
 */
uint8_t ltdc_draw_flush(uint32_t timeout_loop)
{
#if (LTDC_ENABLE_DMA2D == 0)
    (void)timeout_loop; /* DMA2D 禁用：所有写入均已通过 CPU 完成，直接返回 0 */
    return 0u;
#else
    return DMA2D_Accel_Flush(timeout_loop); /* 轮询 DMA2D 完成标志，超时返回非零 */
#endif
}

/**
 * @brief  同步矩形填充单色（等待 DMA2D 完成后返回）
 * @param  sx,sy  逻辑左上角   ex,ey  逻辑右下角   color  填充颜色
 * @note   在 ltdc_fill_async 的基础上增加了 ltdc_draw_flush 等待，
 *         确保函数返回时帧缓冲中已写入正确颜色。
 *         若 DMA2D 超时，则回退到 CPU 软件填充保证正确性。
 * @note   [改进] DMA2D 超时时会重新计算坐标映射（二次调用 ltdc_rect_to_panel），
 *         可缓存计算结果避免重复计算。
 */
void ltdc_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    if (ltdc_fill_async(sx, sy, ex, ey, color) == 0u)
    {
        return; /* async 失败（坐标越界）：无需等待，直接返回 */
    }
    if (ltdc_draw_flush(LTDC_DMA2D_TIMEOUT_LOOP) != 0u) /* 等待 DMA2D 完成，超时进入回退 */
    {
        uint32_t psx;
        uint32_t psy;
        uint32_t pex;
        uint32_t pey;
        if (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) != 0u)
        {
            g_ltdc_dma2d_sw_fallback_count++;
            ltdc_fill_sw_rect(psx, psy, pex, pey, color); /* DMA2D 超时：CPU 补充写入 */
        }
    }
}

/**
 * @brief  同步将颜色数组填充到矩形（等待 DMA2D 完成后返回）
 * @param  sx,sy  逻辑左上角   ex,ey  逻辑右下角   color  颜色数组
 * @note   同 ltdc_fill，增加了 DMA2D 超时 CPU 回退保证正确性。
 */
void ltdc_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    if (ltdc_color_fill_async(sx, sy, ex, ey, color) == 0u)
    {
        return; /* async 失败：直接返回 */
    }
    if (ltdc_draw_flush(LTDC_DMA2D_TIMEOUT_LOOP) != 0u) /* 等待 DMA2D，超时执行回退 */
    {
        uint32_t psx;
        uint32_t psy;
        uint32_t pex;
        uint32_t pey;
        if (ltdc_rect_to_panel(sx, sy, ex, ey, &psx, &psy, &pex, &pey) != 0u)
        {
            g_ltdc_dma2d_sw_fallback_count++;
            ltdc_color_fill_sw_rect(psx, psy, pex, pey, color); /* CPU 回退 */
        }
    }
}

/**
 * @brief       清屏（将整个显示区域填充为指定颜色）
 * @param       color  背景颜色（格式由 LTDC_PIXFORMAT 决定）
 * @retval      无
 * @note   等价于 ltdc_fill(0, 0, width-1, height-1, color)，
 *         通过 DMA2D 高效清屏；若 LTDC 未初始化则直接返回。
 */
void ltdc_clear(uint32_t color)
{
    if ((lcdltdc.width == 0u) || (lcdltdc.height == 0u))
    {
        return; /* LTDC 尚未完成初始化，逻辑尺寸无效，跳过 */
    }
    ltdc_fill(0, 0, lcdltdc.width - 1, lcdltdc.height - 1, color); /* 填充整个逻辑显示区 */
}

/**
 * @brief       配置 LTDC 像素时钟（通过 PLL3 分频）
 * @param       pll3n  PLL3 倍频系数 N（VCO 频率 = HSE × N / M）
 * @param       pll3m  PLL3 分频系数 M（输入分频）
 * @param       pll3r  PLL3 R 输出分频系数（LTDC 时钟 = VCO / R）
 * @retval      0:成功 1:失败
 * @note   LTDC 像素时钟公式：f_LTDC = f_HSE × PLL3N / (PLL3M × PLL3R)
 *         以 25 MHz HSE 为例：
 *         - 4.3寸(480×272)：300/25/33 ≈ 9 MHz
 *         - 7寸(800×480) ：300/25/9  ≈ 33 MHz
 *         - 7寸(1024×600)：300/25/6  = 50 MHz  ← NECCS 实际配置
 *         PLL3P 和 PLL3Q 固定为 2（未实际使用，HAL 要求填写）。
 * @note   [注意] PLL3 修改时 LTDC 可能短暂无时钟，应在 LTDC 使能前调用。
 */
uint8_t ltdc_clk_set(uint32_t pll3n, uint32_t pll3m, uint32_t pll3r)
{
    RCC_PeriphCLKInitTypeDef periphclk_initure;

    /* LTDC输出像素时钟，需要根据自己所使用的LCD数据手册来配置！ */
    periphclk_initure.PeriphClockSelection = RCC_PERIPHCLK_LTDC; /* 选择要配置的外设时钟：LTDC */
    periphclk_initure.PLL3.PLL3M = pll3m; /* 输入分频系数 M */
    periphclk_initure.PLL3.PLL3N = pll3n; /* VCO 倍频系数 N */
    periphclk_initure.PLL3.PLL3P = 2;     /* P 输出分频（USB/DSI等，固定2，此处无用）*/
    periphclk_initure.PLL3.PLL3Q = 2;     /* Q 输出分频（SPI/SAI等，固定2，此处无用）*/
    periphclk_initure.PLL3.PLL3R = pll3r; /* R 输出分频 → LTDC 像素时钟 */

    if (HAL_RCCEx_PeriphCLKConfig(&periphclk_initure) == HAL_OK) /* 写入 RCC 寄存器并等待 PLL3 锁定 */
    {
        return 0; /* PLL3 配置成功，LTDC 时钟已切换 */
    }
    else 
    {
        return 1; /* PLL3 配置失败（可能参数超出范围或 PLL 超时）*/
    }
}

/**
 * @brief       配置图层窗口位置和尺寸
 * @param       layerx  图层编号（0 或 1）
 * @param       sx,sy   窗口左上角像素坐标（相对于显示区域起始）
 * @param       width   窗口宽度（像素）
 * @param       height  窗口高度（像素）
 * @retval      无
 * @note   正常情况下窗口覆盖整个面板（sx=0, sy=0, width=pwidth, height=pheight）。
 *         特殊情况（如 0x1018 横置 1280×800 面板）需修改 CFBLR（帧缓冲行长寄存器）。
 *         CFBLR[28:16]=行字节长度+行间距（用于地址步进），CFBLR[12:0]=行字节长度。
 * @note   [注意] 修改窗口后 HAL_LTDC_SetWindowPosition/Size 内部会触发 reload，
 *         与 VBR reload 共存时可能偶发显示异常（已知 HAL 层不一致）。
 */
void ltdc_layer_window_config(uint8_t layerx, uint16_t sx, uint16_t sy, uint16_t width, uint16_t height)
{
    HAL_LTDC_SetWindowPosition(&g_ltdc_handle, sx, sy, layerx); /* 设置图层窗口起始坐标 */
    HAL_LTDC_SetWindowSize(&g_ltdc_handle, width, height, layerx); /* 设置图层窗口尺寸 */
  
    if (lcdltdc.pheight == 1280 && layerx == 0)
    {
        /* 1280行面板特殊处理：手动设置 CFBLR 帧缓冲行长度寄存器
         * 高16位 = 行字节数（含1行间距）= width×4+宽度偏移，低13位 = 行有效字节 */
        LTDC_Layer1->CFBLR = (width * 4 << 16) | (width * 4 + 7); /* 帧缓冲区行长和行间距(字节) */
    }  
}

/**
 * @brief       配置图层参数（像素格式、帧缓冲地址、透明度、混合因子）
 * @param       layerx    图层编号（0 或 1）
 * @param       bufaddr   帧缓冲起始地址（32位物理地址，SDRAM 区域）
 * @param       pixformat 像素格式（LTDC_PIXEL_FORMAT_RGB565 等 HAL 宏）
 * @param       alpha     图层全局 Alpha（0=全透明，255=不透明）
 * @param       alpha0    默认颜色 Alpha（图层以外区域的透明度）
 * @param       bfac1     混合因子 1（控制本图层混合方式）
 * @param       bfac2     混合因子 2（控制与下方图层混合方式）
 * @param       bkcolor   背景色（本图层以外区域颜色，0x00RRGGBB）
 * @retval      无
 * @note   混合因子说明（LTDC_BLENFACTOR 枚举）：
 *         bfac1=6（LTDC_BLENFACTOR_PAXCA）：本层像素乘以 alpha 后与下层混合
 *         bfac2=7（LTDC_BLENFACTOR_PAXCA...）：常用组合，支持 ARGB8888 的 Alpha 通道混合
 *         详见 STM32H7 参考手册 LTDC_LxBFCR 寄存器。
 * @note   [注意] bfac1/bfac2 通过 << 8 和直接赋值写入 BlendingFactor，
 *         与 HAL 定义的枚举取值对应（HAL 内部有 << 8 处理）。做了两次移位，需验证。
 */
void ltdc_layer_parameter_config(uint8_t layerx, uint32_t bufaddr, uint8_t pixformat, uint8_t alpha, uint8_t alpha0, uint8_t bfac1, uint8_t bfac2, uint32_t bkcolor)
{
    LTDC_LayerCfgTypeDef playercfg; /* 图层配置结构体（局部变量，通过 HAL 写入硬件）*/

    playercfg.WindowX0 = 0;                /* 图层窗口左上角 X = 0（从面板左边开始）*/
    playercfg.WindowY0 = 0;                /* 图层窗口左上角 Y = 0（从面板顶部开始）*/
    playercfg.WindowX1 = lcdltdc.pwidth;   /* 图层窗口右下角 X = 面板物理宽度 */
    playercfg.WindowY1 = lcdltdc.pheight;  /* 图层窗口右下角 Y = 面板物理高度 */
    playercfg.PixelFormat = pixformat;     /* 像素格式（RGB565/RGB888/ARGB8888 等）*/
    playercfg.Alpha = alpha;               /* 图层全局 Alpha（0=透明，255=不透明）*/
    playercfg.Alpha0 = alpha0;             /* 默认颜色 Alpha（图层窗口外区域透明度）*/
    playercfg.BlendingFactor1 = (uint32_t)bfac1 << 8; /* 混合因子 1（写入 BF1 字段，需左移 8 位）*/
    playercfg.BlendingFactor2 = (uint32_t)bfac2;       /* 混合因子 2（写入 BF2 字段）*/
    playercfg.FBStartAdress = bufaddr;     /* 帧缓冲起始地址（LTDC 将从此地址读取像素数据）*/
    playercfg.ImageWidth = lcdltdc.pwidth; /* 图像宽度（决定行间步进，单位：像素）*/
    playercfg.ImageHeight = lcdltdc.pheight; /* 图像高度（决定行数）*/
    /* 背景色 RGB 拆分（当像素 Alpha=0 时显示此背景色）*/
    playercfg.Backcolor.Red   = (uint8_t)(bkcolor & 0X00FF0000) >> 16; /* 红色分量 */
    playercfg.Backcolor.Green = (uint8_t)(bkcolor & 0X0000FF00) >> 8;  /* 绿色分量 */
    playercfg.Backcolor.Blue  = (uint8_t)bkcolor & 0X000000FF;         /* 蓝色分量 */
    HAL_LTDC_ConfigLayer(&g_ltdc_handle, &playercfg, layerx); /* 写入 LTDC 图层寄存器并重载 */
}  

/**
 * @brief       通过 RGB 数据线高位引脚（ID 引脚）读取 LCD 面板型号
 * @retval      面板 ID：0x4342/0x7084/0x7016/0x5571/0x4384/0x1018/0（未知）
 * @note   面板 ID 引脚接在 RGB888 接口的高位（R7/G7/B7）上，出厂时根据面板型号
 *         固定接高/低电平，通过读取 M0/M1/M2 三位组合确定面板型号：
 *         M0 = PG6 (R7引脚)
 *         M1 = PI2 (G7引脚)
 *         M2 = PI7 (B7引脚)
 *         组合 idx = M2<<2 | M1<<1 | M0
 * @note   读取前先上拉这三个引脚为输入模式，延时 10µs 后读取稳定电平。
 * @note   [注意] 读完后这些 GPIO 需被 LTDC MSP Init 重新配置为 AF14（LTDC 复用）。
 */
uint16_t ltdc_panelid_read(void)
{
    uint8_t idx = 0; /* 面板 ID 索引（0~7）*/

    GPIO_InitTypeDef gpio_init_struct;

    __HAL_RCC_GPIOG_CLK_ENABLE(); /* 使能 GPIOG 时钟（M0 引脚 PG6 在此端口）*/
    __HAL_RCC_GPIOI_CLK_ENABLE(); /* 使能 GPIOI 时钟（M1/M2 引脚 PI2/PI7 在此端口）*/
    
    gpio_init_struct.Pin = GPIO_PIN_6;          /* 配置 PG6 为 M0（R7 信号线）*/
    gpio_init_struct.Mode = GPIO_MODE_INPUT;     /* 输入模式（读取电平）*/
    gpio_init_struct.Pull = GPIO_PULLUP;         /* 上拉：默认拉高，若面板 ID 为 0 也能读出正确值 */
    gpio_init_struct.Speed = GPIO_SPEED_HIGH;    /* 高速（对输入模式影响不大，保持一致）*/
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);     /* 初始化 PG6 */
    
    gpio_init_struct.Pin = GPIO_PIN_2 | GPIO_PIN_7; /* 配置 PI2(M1=G7) 和 PI7(M2=B7) */
    HAL_GPIO_Init(GPIOI, &gpio_init_struct);     /* 初始化 PI2 和 PI7 */

    delay_us(10); /* 等待 GPIO 电平稳定（RC 时间常数典型值 < 1µs）*/
    idx  = (uint8_t)HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6);       /* 读取 M0（PG6）*/
    idx |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2) << 1;  /* 读取 M1（PI2），移位到 bit1 */
    idx |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_7) << 2;  /* 读取 M2（PI7），移位到 bit2 */

    /* 根据 3 位 idx 返回对应面板型号 ID */
    switch (idx)
    {
        case 0 :
            return 0X4342; /* 4.3寸 480×272，像素时钟约 9 MHz */
        
        case 1 :
            return 0X7084; /* 7寸 800×480，像素时钟约 33 MHz */
        
        case 2 :
            return 0X7016; /* 7寸 1024×600，像素时钟约 50 MHz（NECCS 实际使用）*/
        
        case 3 :
            return 0X5571; /* 5.5寸 720×1280，像素时钟约 55 MHz，SPI 初始化 */
        
        case 4 :
            return 0X4384; /* 4.3寸 800×480，像素时钟约 33 MHz */
        
        case 5 :
            return 0X1018; /* 10.1寸 1280×800，像素时钟 60 MHz */
        
        default :
            return 0; /* 未识别：可能硬件未连接或 ID 引脚状态异常 */
    }
}

/**
 * @brief  LTDC 完整初始化（面板识别→时序配置→帧缓冲分配→图层配置→复位序列）
 * @retval 无（失败时设置 g_ltdc_init_stage 错误码并清零 lcddev）
 * @details 初始化流程：
 *   Stage 1: 清零几何参数
 *   Stage 2: 读取面板 ID（ID 引脚 + 编译时覆盖宏）
 *   Stage 3: 根据面板 ID 设置时序参数（pwidth/pheight/hsw/hbp/hfp/vsw/vbp/vfp）
 *            - 0xE301: 面板 ID = 0（未识别）→ 返回
 *            - 0xE302: 面板分辨率与 LTDC_TARGET_WIDTH/HEIGHT 不符 → 返回
 *            - 0xE303: 双帧缓冲超出 SDRAM 窗口范围 → 返回
 *   Stage 4: 配置 LTDC 同步时序参数（HSW/VSW/HBP/VBP/有效宽高/总宽高）
 *   Stage 5: HAL_LTDC_Init + DMA2D_Accel_Init
 *   Stage 6: 配置图层 0 参数和窗口
 *   Stage 61~69: 背光控制 + 硬复位序列 + 白色清屏
 * @note   [注意] 若面板 ID 不匹配且启用了 ID_FALLBACK，
 *         会跳回 panel_profile_select 标签重试一次（使用 goto）。
 *         此 goto 向后跳转，ARM Compiler 5 支持，但不利于结构化分析。
 */
void ltdc_init(void)
{
    uint16_t lcdid = 0; /* 识别到的面板 ID */
    g_ltdc_init_stage = 1u; /* 阶段1：开始初始化 */

    /* 清零几何参数，防止上次初始化的残留值影响本次判断 */
    lcdltdc.pwidth = 0U;
    lcdltdc.pheight = 0U;
    lcdltdc.width = 0U;
    lcdltdc.height = 0U;

    lcdid = ltdc_panelid_read(); /* 阶段2：通过 ID 引脚读取面板型号 */
    g_ltdc_init_stage = 2u;

#if RGB_80_8001280
    lcdid = 0X8081; /* 编译时强制指定为 8寸 RGB800×1280 面板 */
#endif

#if (LTDC_FORCE_PANEL_ID != 0U)
    lcdid = (uint16_t)LTDC_FORCE_PANEL_ID; /* 编译时强制覆盖面板 ID（调试用）*/
#endif

#if LTDC_ENABLE_ID_FALLBACK
    if (lcdid == 0U)
    {
        lcdid = LTDC_PANEL_FALLBACK_ID; /* ID 引脚未识别时回退到默认面板 ID */
    }
#endif
    g_ltdc_panel_id = lcdid; /* 保存全局面板 ID，供外部诊断代码读取 */

panel_profile_select: /* goto 目标：ID fallback 重试时跳回这里 */
    if (lcdid == 0X5571)
    {
        tft_spi_init(); /* 5571 面板需要通过 SPI 发送初始化命令（MIPI DSI 桥接芯片）*/
    }
  
    /* === 各面板时序参数配置（hsw/hbp/hfp/vsw/vbp/vfp 详解：
     * 像素时钟总周期 = hsw + hbp + pwidth + hfp
     * hsw: 水平同步信号宽度（像素时钟数）
     * hbp: 水平后廊（HSYNC 结束到有效像素开始的延迟）
     * hfp: 水平前廊（有效像素结束到下一行 HSYNC 开始的空白）
     * vsw: 垂直同步信号宽度（行数）
     * vbp: 垂直后廊（VSYNC 后到第一行有效数据的空白行数）
     * vfp: 垂直前廊（最后一行有效数据到 VSYNC 的空白行数）
     * 这些参数必须与屏幕数据手册 timing 参数严格匹配）=== */
    if (lcdid == 0X4342) /* 4.3寸 480×272 */
    {
        lcdltdc.pwidth  = 480;       /* 面板物理宽度（像素）*/
        lcdltdc.pheight = 272;       /* 面板物理高度（像素）*/
        lcdltdc.hsw = 1;             /* 水平同步脉冲宽度 */
        lcdltdc.hbp = 40;            /* 水平后廊（Back Porch）*/
        lcdltdc.hfp = 5;             /* 水平前廊（Front Porch）*/
        lcdltdc.vsw = 1;             /* 垂直同步脉冲宽度 */
        lcdltdc.vbp = 8;             /* 垂直后廊 */
        lcdltdc.vfp = 8;             /* 垂直前廊 */
        ltdc_clk_set(300, 25, 33);   /* PLL3：VCO=300×25MHz/25=300MHz，LTDC=300/33≈9 MHz */
    }
    else if (lcdid == 0X7084) /* 7寸 800×480 */
    {
        lcdltdc.pwidth  = 800;        /* 面板物理宽度 */
        lcdltdc.pheight = 480;        /* 面板物理高度 */
        lcdltdc.hsw = 1;
        lcdltdc.hbp = 46;
        lcdltdc.hfp = 210;
        lcdltdc.vsw = 1;
        lcdltdc.vbp = 23;
        lcdltdc.vfp = 22;
        ltdc_clk_set(300, 25, 9);     /* LTDC = 300/9 = 33 MHz */
    }
    else if (lcdid == 0X7016) /* 7寸 1024×600（NECCS 项目实际面板，WKS70WSV078-WCT）*/
    {
        /* 像素时钟需求验证：(1024+20+140+160)×(600+3+20+12) × ~58.6Hz ≈ 49.9MHz
         * 总水平像素数：hsw(20)+hbp(140)+1024+hfp(160) = 1344 个 pixel clock
         * 总垂直行数：vsw(3)+vbp(20)+600+vfp(12)       = 635 行
         * 帧率：50MHz / (1344 × 635) ≈ 58.6 Hz        → 满足 60Hz 规格书要求 */
        lcdltdc.pwidth  = 1024;     /* 面板有效宽度：1024 像素 */
        lcdltdc.pheight = 600;      /* 面板有效高度：600 像素 */
        lcdltdc.hsw = 20;           /* HSYNC 脉冲宽度（Horizontal Sync Width），单位：pixel clock */
        lcdltdc.hbp = 140;          /* 水平后廊（Back Porch）：HSYNC 结束到有效像素开始的空白时钟数 */
        lcdltdc.hfp = 160;          /* 水平前廊（Front Porch）：有效像素结束到下一 HSYNC 的空白时钟数 */
        lcdltdc.vsw = 3;            /* VSYNC 脉冲宽度（Vertical Sync Width），单位：行 */
        lcdltdc.vbp = 20;           /* 垂直后廊（Vertical Back Porch）：VSYNC 末到第一有效行的空白行数 */
        lcdltdc.vfp = 12;           /* 垂直前廊（Vertical Front Porch）：最后有效行到下一 VSYNC 的空白行数 */
        ltdc_clk_set(300, 25, 6);   /* PLL3：f = HSE×(N/M)/R = 25MHz×(300/25)/6 = 50 MHz 像素时钟 */
    }
    else if (lcdid == 0X5571)       /* 5寸 720×1280 竖屏（若逻辑横屏需软件旋转坐标）*/
    {
        /* 像素时钟需求：(720+10+36+46)×(1280+5+5+16) × ~54Hz ≈ 55MHz
         * [注意] 该面板物理竖屏（720宽×1280高），若 LTDC_TARGET 为横屏需调换宽高 */
        lcdltdc.pwidth  = 720;      /* 面板有效宽度：720 像素（竖向） */
        lcdltdc.pheight = 1280;     /* 面板有效高度：1280 像素（竖向） */
        lcdltdc.hsw = 10;           /* HSYNC 脉冲宽度 */
        lcdltdc.hbp = 36;           /* 水平后廊 */
        lcdltdc.hfp = 46;           /* 水平前廊 */
        lcdltdc.vsw = 5;            /* VSYNC 脉冲宽度 */
        lcdltdc.vbp = 5;            /* 垂直后廊 */
        lcdltdc.vfp = 16;           /* 垂直前廊 */
        ltdc_clk_set(330, 25, 6);   /* PLL3：f = 25MHz×(330/25)/6 = 55 MHz 像素时钟 */
        /* [注意] 0X5571 面板无硬复位引脚（RST），ltdc_init 后段对其跳过复位序列 */
    }
    else if (lcdid == 0X4384)       /* 4.3寸 800×480（RGB 接口，标准横屏）*/
    {
        /* 像素时钟需求：(800+48+88+40)×(480+3+32+13) × ~60Hz ≈ 33.3MHz */
        lcdltdc.pwidth  = 800;      /* 面板有效宽度：800 像素 */
        lcdltdc.pheight = 480;      /* 面板有效高度：480 像素 */
        lcdltdc.hsw = 48;           /* HSYNC 脉冲宽度 */
        lcdltdc.hbp = 88;           /* 水平后廊 */
        lcdltdc.hfp = 40;           /* 水平前廊 */
        lcdltdc.vsw = 3;            /* VSYNC 脉冲宽度 */
        lcdltdc.vbp = 32;           /* 垂直后廊 */
        lcdltdc.vfp = 13;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 9);   /* PLL3：f = 25MHz×(300/25)/9 = 33.3 MHz 像素时钟 */
    }
    else if (lcdid == 0X8081)       /* 8�?00*1280 RGB�?*/
    {
        lcdltdc.pwidth = 800;       /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 1280;     /* 面板高度,单位:像素 */
        lcdltdc.hsw = 5;            /* 水平同步宽度 */
        lcdltdc.hbp = 20;           /* 水平后廊 */
        lcdltdc.hfp = 40;           /* 水平前廊 */
        lcdltdc.vsw = 3;            /* 垂直同步宽度 */
        lcdltdc.vbp = 20;           /* 垂直后廊 */
        lcdltdc.vfp = 30;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 5);   /* 设置像素时钟 60Mhz */
    }
    else if (lcdid == 0X1018)       /* 10.1�?280*800 RGB�?*/
    {
        lcdltdc.pwidth = 1280;      /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 800;      /* 面板高度,单位:像素 */
        lcdltdc.hsw = 10;           /* 水平同步宽度 */
        lcdltdc.hbp = 140;          /* 水平后廊 */
        lcdltdc.hfp = 10;           /* 水平前廊 */
        lcdltdc.vsw = 3;            /* 垂直同步宽度 */
        lcdltdc.vbp = 10;           /* 垂直后廊 */
        lcdltdc.vfp = 10;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 5);   /* 设置像素时钟 60Mhz */
    } 
    g_ltdc_init_stage = 3u;                  /* 阶段3：面板参数校验 */

    /* --- 校验1：面板 ID 匹配检查 ---
     * 若 lcdid 无匹配（或 LTDC_FORCE_PANEL_ID 为 0），pwidth/pheight 仍为默认值 0 */
    if ((lcdltdc.pwidth == 0U) || (lcdltdc.pheight == 0U))
    {
        /* 面板 ID 无匹配，无法确定时序参数，中止初始化 */
        g_ltdc_init_stage = 0xE301u;         /* 错误码 0xE301：未知面板 ID */
        lcddev.id     = 0U;                  /* 清除设备 ID，表示初始化失败 */
        lcddev.width  = 0U;
        lcddev.height = 0U;
        return;
    }

    /* --- 校验2：面板实际分辨率必须与编译期目标分辨率一致 ---
     * LTDC_TARGET_WIDTH/HEIGHT 由 ltdc.h 宏定义，帧缓冲按此静态大小分配
     * 若面板 ID 读取正确但分辨率不匹配，说明板卡型号与固件不符 */
    if ((lcdltdc.pwidth != LTDC_TARGET_WIDTH) || (lcdltdc.pheight != LTDC_TARGET_HEIGHT))
    {
        /* 分辨率不一致：首先尝试 Fallback 面板 ID（若编译时开启 LTDC_ENABLE_ID_FALLBACK）*/
#if LTDC_ENABLE_ID_FALLBACK
        if ((lcdid != LTDC_PANEL_FALLBACK_ID) && (LTDC_PANEL_FALLBACK_ID != 0U))
        {
            /* 切换到 Fallback ID 并跳回 panel_profile_select 重新查表 */
            lcdid              = LTDC_PANEL_FALLBACK_ID;
            g_ltdc_panel_id    = lcdid;      /* 更新全局面板 ID 变量 */
            lcdltdc.pwidth     = 0U;         /* 清零，由下次循环重新填写 */
            lcdltdc.pheight    = 0U;
            lcdltdc.width      = 0U;
            lcdltdc.height     = 0U;
            goto panel_profile_select;       /* 跳回面板选择标签重新匹配 */
        }
#endif
        /* Fallback 不匹配或未启用：中止初始化 */
        g_ltdc_init_stage = 0xE302u;         /* 错误码 0xE302：分辨率与编译期目标不匹配 */
        lcddev.id     = 0U;
        lcddev.width  = 0U;
        lcddev.height = 0U;
        return;
    }

    /* --- 阶段3 通过：将面板 ID 同步到 lcddev 全局设备描述符 --- */
    lcddev.id = lcdid;                       /* 记录最终使用的面板 ID（供上层查询）*/

    lcddev.width  = lcdltdc.pwidth;      /* 设置 lcddev 逻辑宽度（与面板物理宽度一致）*/
    lcddev.height = lcdltdc.pheight;     /* 设置 lcddev 逻辑高度（与面板物理高度一致）*/
    lcdltdc.pixformat = LTDC_PIXFORMAT;  /* 颜色像素格式，由 ltdc.h 编译期宏决定（NECCS 默认 RGB565）*/

    /* 根据颜色格式设置每像素字节数（bytes per pixel, bpp）*/
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888
    lcdltdc.pixsize = 4;    /* ARGB8888：每像素 4 字节，颜色最丰富，内存消耗最大 */
#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888
    lcdltdc.pixsize = 3;    /* RGB888：每像素 3 字节，无 Alpha 通道 */
#else
    lcdltdc.pixsize = 2;    /* RGB565：每像素 2 字节（NECCS 实际使用），内存效率最高 */
#endif

    /* --- 阶段3.5：计算帧缓冲地址并进行越界校验 ---
     * 双缓冲模式：帧缓冲0（前缓冲）、帧缓冲1（后缓冲）顺序存放在 SDRAM 帧缓冲窗口内
     * NECCS 实际用量：1024 × 600 × 2 = 1,228,800 字节 ≈ 1.17 MB/帧，双帧共 2.34 MB */
    {
        uint32_t frame_bytes = lcdltdc.pwidth * lcdltdc.pheight * lcdltdc.pixsize;
        /* frame_bytes = 每帧字节数；双缓冲需 2x frame_bytes 空间 */
        uint32_t limit_end = LTDC_FRAME_BUF_ADDR + LTDC_FRAME_BUF_WINDOW_BYTES;
        /* limit_end = 帧缓冲 SDRAM 窗口的上边界地址 */
        if ((LTDC_FRAME_BUF_ADDR + frame_bytes * 2u) > limit_end)
        {
            /* 双缓冲总空间超出 SDRAM 分配窗口：中止初始化 */
            g_ltdc_init_stage = 0xE303u;    /* 错误码 0xE303：帧缓冲越界 */
            lcddev.id     = 0U;
            lcddev.width  = 0U;
            lcddev.height = 0U;
            return;                          /* [改进] 可返回 Err_t 错误码而非 void return */
        }
        /* 前缓冲从 SDRAM 帧缓冲起始地址开始，后缓冲紧接其后 */
        g_ltdc_framebuf[0] = (uint32_t *)LTDC_FRAME_BUF_ADDR;
        g_ltdc_framebuf[1] = (uint32_t *)(LTDC_FRAME_BUF_ADDR + frame_bytes);
        /* [注意] SDRAM 区域（AXI 总线）无 D-Cache：DMA2D 写入后 LTDC DMA 立即可见，
         *        无需 SCB_CleanDCache / SCB_InvalidateDCache */
    }

    /* --- 初始化双缓冲索引（与 LTDC 硬件初始状态一致）--- */
    lcdltdc.activelayer       = 0u;          /* 当前激活图层索引（0 = Layer1，NECCS 只用一个图层）*/
    s_front_buf_idx           = 0u;          /* 前缓冲（LTDC 当前显示帧）使用帧缓冲 [0] */
    s_back_buf_idx            = 1u;          /* 后缓冲（CPU/DMA2D 写入目标）使用帧缓冲 [1] */
    s_swap_pending            = 0u;          /* 无待处理换帧请求 */
    s_swap_reload_pending     = 0u;          /* 无待处理 VBR Reload */
    s_swap_front_target_idx   = 0u;          /* VBR Reload 完成后前缓冲目标索引（初始与 front 一致）*/
    s_swap_back_target_idx    = 1u;          /* VBR Reload 完成后后缓冲目标索引 */
    g_ltdc_swap_count         = 0u;          /* 历史成功换帧次数清零 */
    g_ltdc_swap_pending_count = 0u;          /* 换帧请求总次数清零 */
    g_ltdc_swap_error_count   = 0u;          /* 换帧错误次数清零（初始化失败/Reload 失败）*/
    
    /* ===== 阶段4：配置 LTDC 句柄并调用 HAL_LTDC_Init ===== */
    g_ltdc_handle.Instance = LTDC;           /* 指向 LTDC 外设基地址寄存器 */

    /* 极性配置：0X8081（800×1280 竖屏）HSYNC 高电平有效，其余面板低电平有效 */
    if (lcdid == 0X8081)
    {
        g_ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AH;     /* HSYNC Active High（高电平有效）*/
    }
    else
    {
        g_ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AL;     /* HSYNC Active Low（低电平有效，多数 RGB 屏标准）*/
    }

    g_ltdc_handle.Init.VSPolarity = LTDC_VSPOLARITY_AL;         /* VSYNC Active Low（低电平有效，标准 RGB 接口）*/
    g_ltdc_handle.Init.DEPolarity = LTDC_DEPOLARITY_AL;         /* DE Active Low（数据使能低电平有效）*/
    g_ltdc_handle.State = HAL_LTDC_STATE_RESET;                 /* 强制句柄为 RESET 态，确保 HAL_LTDC_Init 执行 MspInit */

    /* 像素时钟极性：10.1寸(0x1018) 和 8寸竖屏(0x8081) 在时钟下降沿采样，需反相（IIPC）*/
    if (lcdid == 0X1018 || lcdid == 0X8081)
    {
        g_ltdc_handle.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;   /* 像素时钟取反输出（Inverted Input Pixel Clock）*/
    }
    else
    {
        g_ltdc_handle.Init.PCPolarity = LTDC_PCPOLARITY_IPC;    /* 像素时钟同相输出（标准）*/
    }

    /* 时序寄存器赋值（RM0433 §33.7.2：LTDC 所有时序值均为"累计像素数 - 1"格式）
     * 公式：SSCR.HSW = hsw-1, BPCR.AHBP = hsw+hbp-1, AWCR.AAW = hsw+hbp+width-1
     *       TWCR.TOTALW = hsw+hbp+width+hfp-1（V 方向同理，行数替代像素数）
     * NECCS 7016 实例：HSync(19), AHBP(159), AAW(1183), TOTALW(1343) */
    g_ltdc_handle.Init.HorizontalSync     = lcdltdc.hsw - 1;
    /* HSYNC 脉冲宽度：SSCR.HSW = hsw - 1（单位：pixel clock - 1）*/
    g_ltdc_handle.Init.VerticalSync       = lcdltdc.vsw - 1;
    /* VSYNC 脉冲宽度：SSCR.VSH = vsw - 1（单位：line - 1）*/
    g_ltdc_handle.Init.AccumulatedHBP     = lcdltdc.hsw + lcdltdc.hbp - 1;
    /* 水平后沿累计（BPCR.AHBP）：有效像素从 AHBP+1 开始 */
    g_ltdc_handle.Init.AccumulatedVBP     = lcdltdc.vsw + lcdltdc.vbp - 1;
    /* 垂直后沿累计（BPCR.AVBP）：有效行从 AVBP+1 开始 */
    g_ltdc_handle.Init.AccumulatedActiveW = lcdltdc.hsw + lcdltdc.hbp + lcdltdc.pwidth - 1;
    /* 有效宽度累计（AWCR.AAW）= HSW + HBP + 像素宽度 - 1 */
    g_ltdc_handle.Init.AccumulatedActiveH = lcdltdc.vsw + lcdltdc.vbp + lcdltdc.pheight - 1;
    /* 有效高度累计（AWCR.AAH）= VSW + VBP + 像素高度 - 1 */
    g_ltdc_handle.Init.TotalWidth         = lcdltdc.hsw + lcdltdc.hbp + lcdltdc.pwidth + lcdltdc.hfp - 1;
    /* 总水平宽度（TWCR.TOTALW）= HSW + HBP + 宽度 + HFP - 1 */
    g_ltdc_handle.Init.TotalHeigh         = lcdltdc.vsw + lcdltdc.vbp + lcdltdc.pheight + lcdltdc.vfp - 1;
    /* 总垂直高度（TWCR.TOTALH）= VSW + VBP + 高度 + VFP - 1 */
    g_ltdc_handle.Init.Backcolor.Red   = 0;   /* 背景层颜色 R 分量（图层外区域显示黑色）*/
    g_ltdc_handle.Init.Backcolor.Green = 0;   /* 背景层颜色 G 分量 */
    g_ltdc_handle.Init.Backcolor.Blue  = 0;   /* 背景层颜色 B 分量 */
    g_ltdc_init_stage = 4u;                    /* 阶段4：调用 HAL LTDC 初始化（内含 MspInit GPIO/CLK/NVIC）*/
    HAL_LTDC_Init(&g_ltdc_handle);             /* 配置 LTDC 外设寄存器，触发 HAL_LTDC_MspInit 回调 */
    DMA2D_Accel_Init();                        /* 初始化 DMA2D 加速器命令队列及硬件参数 */
    g_ltdc_init_stage = 5u;                    /* 阶段5：图层配置 */

    /* --- 配置图层0（Layer1）---
     * 参数说明：图层0, 前缓冲基地址, 像素格式RGB565, Alpha=255（完全不透明）,
     *           defaultColor=0（透明背景黑色）, BlendingFactor1=6（PAxCA），
     *           BlendingFactor2=7（PAxCA），背景混合色=黑色 */
    /* LTDC层参数配置 */
    ltdc_layer_parameter_config(0, (uint32_t)g_ltdc_framebuf[s_front_buf_idx], LTDC_PIXFORMAT, 255, 0, 6, 7, 0X000000);   /* 配置layer0，全屏铺满，完全不透明 */
    ltdc_layer_window_config(0, 0, 0, lcdltdc.pwidth, lcdltdc.pheight);   /* 图层窗口铺满整个面板（x=0,y=0,w=pwidth,h=pheight）*/
    g_ltdc_init_stage = 6u;                    /* 阶段6：双缓冲清零 */
    ltdc_select_layer(0);                      /* 切换到图层0（软件状态 lcdltdc.activelayer=0）*/

    /* 将前缓冲和后缓冲均清零为黑色，避免首次显示时看到 SDRAM 随机噪声 */
    /* Clear both buffers to a known value before first swap. */
    s_back_buf_idx = s_front_buf_idx;          /* 临时令 back == front，以便 sw_rect 写前缓冲 */
    ltdc_fill_sw_rect(0u, 0u, lcdltdc.pwidth - 1u, lcdltdc.pheight - 1u, 0u);   /* 清前缓冲为黑色 */
    s_back_buf_idx = (uint8_t)(s_front_buf_idx ^ 1u);  /* 恢复后缓冲索引（XOR 恢复）*/
    ltdc_fill_sw_rect(0u, 0u, lcdltdc.pwidth - 1u, lcdltdc.pheight - 1u, 0u);   /* 清后缓冲为黑色 */
    /* [注意] 临时改写索引期间若 LTDC LI 中断触发换帧，可能读到错误地址；
     *        此处 NVIC 已使能但行中断未编程，不会提前触发，属安全窗口 */

    /* 使能 LTDC 行中断（LI），每帧行号0处触发，用于检测 swap_pending 并提交 VBR Reload */
    __HAL_LTDC_ENABLE_IT(&g_ltdc_handle, LTDC_IT_LI);   /* 使能行中断掩码位 */
    HAL_LTDC_ProgramLineEvent(&g_ltdc_handle, 0u);       /* 设置触发行号为 0（帧起始，VSYNC 后立即）*/
    /* [注意] 行事件为一次性触发，必须在每次 LineEventCallback 中重新调用 ProgramLineEvent */

    /* 提前打开背光，即使后续复位序列阻塞（最坏约 260ms），用户也能立即看到背光点亮 */
    /* Turn on backlight early for visibility, even if panel reset sequence stalls. */
    LTDC_BL(1);                                          /* 背光控制 GPIO 置高 */
    g_ltdc_init_stage = 61u;
    if (lcdid != 0X5571)                   /* 5571无需硬复位：该面板无 RST 引脚，跳过复位序列 */
    {
        /* === 执行 LCD 硬复位序列（RST 高→低→高，满足面板规格书时序要求）=== */
        LTDC_RST(1);               /* RST 引脚置高（预置或复位结束状态）*/
        g_ltdc_init_stage = 62u;
        delay_ms(10);              /* 稳定时间 10ms */
        g_ltdc_init_stage = 63u;
        LTDC_RST(0);               /* RST 引脚拉低（开始复位脉冲，最小宽度 ≥ 10ms）*/
        g_ltdc_init_stage = 64u;
        delay_ms(50);              /* 复位脉冲宽度 50ms（远超规格书最小值）*/
        g_ltdc_init_stage = 65u;
        LTDC_RST(1);               /* RST 引脚置高（释放复位）*/
        g_ltdc_init_stage = 66u;
        delay_ms(200);             /* 等待面板内部初始化完成（规格书约 120~200ms）*/
        g_ltdc_init_stage = 67u;
        /* [改进] 复位序列共阻塞约 260ms；RTOS 环境中应改用 vTaskDelay，
         *        避免 FreeRTOS 看门狗超时或延误高优先级任务 */
    }

    g_ltdc_init_stage = 68u;
    LTDC_BL(1);                            /* 确保背光已打开（防止因分支跳过导致背光关闭）*/
    g_ltdc_init_stage = 69u;
    ltdc_clear(0XFFFFFFFF);                /* 白色清屏：前缓冲填充 0xFFFF（RGB565 白色）*/
    /* [注意] 0xFFFFFFFF 在 RGB565 下截断为 0xFFFF = 白色；本调用走 ltdc_fill_sw_rect CPU 路径 */
    g_ltdc_init_stage = 70u;               /* 阶段70：初始化完成 */
}

/**
 * @brief  LTDC 行事件中断回调（ISR 上下文）
 * @param  hltdc  LTDC 句柄（未使用，本函数直接操作全局变量）
 * @details
 *   每帧由 HAL_LTDC_ProgramLineEvent(&g_ltdc_handle, 0u) 在行号0处触发（即 VSYNC 后第一行）。
 *   双缓冲换帧流程（LTDC_PRESENT_DIRECT_MODE == 0）：
 *     1. 若 s_swap_pending==1 且 s_swap_reload_pending==0（未提交 VBR Reload）：
 *        a. 调用 HAL_LTDC_SetAddress_NoReload() 预置图层0 CFBAR 为新帧地址（不立即生效）；
 *        b. 调用 HAL_LTDC_Reload(VBR)：LTDC 将在下一个 VSYNC 时原子更新 CFBAR，
 *           更新完成后触发 HAL_LTDC_ReloadEventCallback；
 *        c. 设置 s_swap_reload_pending=1，防止重复提交。
 *     2. 末尾重新编程行事件（ProgramLineEvent），使每帧持续触发。
 *
 *   [注意] ISR 上下文（LTDC 中断优先级=6）：不可调用 FreeRTOS 阻塞 API。
 *   [注意] SetAddress_NoReload + Reload(VBR) 两步提交是无撕裂换帧的关键：
 *          旧帧继续显示直到 VSYNC，新地址在 VSYNC 期间原子生效。
 */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;                    /* 未使用句柄参数，避免编译器警告 */

#if (LTDC_PRESENT_DIRECT_MODE == 0u)
    /* 双缓冲模式：检测 swap 请求并提交 VBR Reload */
    if ((s_swap_pending != 0u) && (s_swap_reload_pending == 0u))
    {
        /* s_swap_pending：ltdc_request_swap() 已确认后缓冲渲染完毕，等待换帧 */
        uint8_t next_front = s_back_buf_idx;    /* 下一帧前缓冲 = 当前后缓冲（新渲染帧）*/
        uint8_t next_back  = s_front_buf_idx;   /* 下一帧后缓冲 = 当前前缓冲（回收为写入目标）*/

        /* 步骤 a：预置图层0 CFBAR 为新帧地址（NoReload = 不立即生效）*/
        if (HAL_LTDC_SetAddress_NoReload(&g_ltdc_handle, (uint32_t)g_ltdc_framebuf[next_front], 0u) == HAL_OK)
        {
            /* 步骤 b：请求 VBR（垂直消隐期）时原子重载 CFBAR */
            if (HAL_LTDC_Reload(&g_ltdc_handle, LTDC_RELOAD_VERTICAL_BLANKING) == HAL_OK)
            {
                /* 记录目标索引，由 ReloadEventCallback 确认后更新全局索引变量 */
                s_swap_front_target_idx = next_front;   /* VBR 完成后前缓冲应指向此索引 */
                s_swap_back_target_idx  = next_back;    /* VBR 完成后后缓冲应指向此索引 */
                s_swap_reload_pending   = 1u;            /* 标记：VBR Reload 已提交，等待完成 */
            }
            else
            {
                /* Reload 失败（HAL busy 或状态异常）：取消换帧，记录错误 */
                s_swap_pending = 0u;
                g_ltdc_swap_error_count++;
                ltdc_sync_indices_from_hw_locked();     /* 从硬件 CFBAR 重新同步索引，防止紊乱 */
            }
        }
        else
        {
            /* SetAddress_NoReload 失败：取消换帧，记录错误 */
            s_swap_pending = 0u;
            g_ltdc_swap_error_count++;
            ltdc_sync_indices_from_hw_locked();
        }
    }
#endif

    /* 重新编程行中断触发行号：LI 为一次性触发，必须每帧重新编程才能持续工作 */
    HAL_LTDC_ProgramLineEvent(&g_ltdc_handle, 0u);
}

/**
 * @brief  LTDC VBR（垂直消隐期 Reload）完成回调（ISR 上下文）
 * @param  hltdc  LTDC 句柄（未使用）
 * @details
 *   当 HAL_LTDC_Reload(VBR) 请求在 VSYNC 期间完成时，HAL 触发此回调。
 *   此时 LTDC 硬件已切换至新的 CFBAR 地址，开始显示新帧。
 *   软件状态更新：
 *     - s_front_buf_idx ← s_swap_front_target_idx（硬件正在显示的帧）
 *     - s_back_buf_idx  ← s_swap_back_target_idx（可安全写入的帧）
 *     - s_swap_pending = s_swap_reload_pending = 0（清除 pending 标志）
 *     - g_ltdc_swap_count++（统计成功换帧次数）
 *
 *   [注意] ISR 上下文，不可调用 FreeRTOS 阻塞 API。
 *   [注意] 直写模式（LTDC_PRESENT_DIRECT_MODE != 0）时无双缓冲，直接返回。
 */
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;                        /* 未使用句柄参数 */

#if (LTDC_PRESENT_DIRECT_MODE != 0u)
    return;                             /* 直写模式：无双缓冲换帧逻辑，直接忽略 */
#else
    if (s_swap_reload_pending != 0u)    /* 确认是本模块发起的 VBR Reload */
    {
        /* 原子性更新双缓冲索引：
         * 从此刻起 LTDC 正在显示 framebuf[s_front_buf_idx]，
         * CPU/DMA2D 可安全写入 framebuf[s_back_buf_idx] */
        s_front_buf_idx       = s_swap_front_target_idx;   /* 更新前缓冲索引（硬件正显示帧）*/
        s_back_buf_idx        = s_swap_back_target_idx;    /* 更新后缓冲索引（软件可写入帧）*/
        s_swap_reload_pending = 0u;     /* VBR Reload 已完成，清除 pending 标志 */
        s_swap_pending        = 0u;     /* 本次换帧请求已完成 */
        g_ltdc_swap_count++;            /* 成功换帧计数（可用于帧率统计与调试）*/
    }
#endif
}

/**
 * @brief  LTDC 错误回调（ISR 上下文，通过 LTDC_ER 中断线触发）
 * @param  hltdc  LTDC 句柄
 * @details
 *   LTDC 硬件检测到以下错误时触发：
 *     - FU（FIFO Underrun/欠载）：像素填充速度 < LTDC 读取速率，总线带宽不足。
 *       表现为屏幕出现黑线或撕裂，可能由像素时钟过高或 SDRAM 竞争引起。
 *     - TE（Transfer Error / AXI 传输错误）：LTDC DMA 访问帧缓冲失败，
 *       可能由 SDRAM 时序错误或 MPU 配置不当引起。
 *   回调中仅计数，硬件在下一帧自动恢复。
 *
 *   [改进] 应增加连续错误阈值检测：若 FU 频繁（>N次/秒），
 *          可降低像素时钟或减少 DMA2D 并发度以缓解总线压力。
 */
void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *hltdc)
{
    uint32_t error_code;

    if ((hltdc == NULL) || (hltdc != &g_ltdc_handle))
    {
        /* 防护：句柄为空 OR 非本模块句柄，直接返回，防止空指针访问 */
        return;
    }

    error_code = hltdc->ErrorCode;          /* 读取 HAL 记录的错误位掩码 */
    g_ltdc_last_error_code = error_code;    /* 保存供调试查询（ltdc.h 导出变量）*/

    if ((error_code & HAL_LTDC_ERROR_FU) != 0u)
    {
        g_ltdc_fifo_underrun_count++;       /* FIFO 欠载计数，反映总线带宽瓶颈 */
    }
    if ((error_code & HAL_LTDC_ERROR_TE) != 0u)
    {
        g_ltdc_transfer_error_count++;      /* AXI 传输错误计数，反映内存访问问题 */
    }

    hltdc->ErrorCode = HAL_LTDC_ERROR_NONE; /* 清除 HAL 错误码，允许后续 HAL_LTDC_GetError 正常工作 */
}
/**
 * @brief  LTDC/DMA2D MSP（MCU Support Package）底层硬件初始化回调
 * @param  hltdc  LTDC 句柄（此处忽略，仅一个 LTDC 外设）
 * @retval 无
 * @details
 *   由 HAL_LTDC_Init() 内部调用（弱符号 weak symbol，此处覆盖 HAL 默认空实现）。
 *   初始化内容：
 *     1. 使能 LTDC 和 DMA2D 外设时钟；
 *     2. 配置 LTDC/LTDC_ER/DMA2D 中断优先级（均为 preempt=6, sub=0）并使能；
 *     3. 初始化背光（BL）和复位（RST）控制 GPIO（推挽输出，上拉，高速）；
 *     4. 初始化 LTDC 信号线 GPIO：DE、VSYNC、HSYNC、CLK（AF14，复用推挽，无拉）；
 *     5. 初始化所有 RGB 数据线 GPIO（AF13 或 AF14，参见各引脚注释）。
 *
 *   [注意] NVIC 优先级6 与 FreeRTOS configMAX_SYSCALL_INTERRUPT_PRIORITY 兼容
 *          （FreeRTOS 通常配置为 5，数字越大优先级越低），回调内可安全调用 FromISR API。
 *   [注意] DMA2D 时钟在此一并使能，DMA2D_Accel_Init() 仅配置参数，不单独 enable 时钟。
 */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    GPIO_InitTypeDef gpio_init_struct;
    (void)hltdc;                             /* 未使用句柄参数（仅一个 LTDC 外设）*/

    __HAL_RCC_LTDC_CLK_ENABLE();             /* 使能 LTDC 外设时钟（RCC_APB3ENR.LTDCEN）*/
    __HAL_RCC_DMA2D_CLK_ENABLE();            /* 使能 DMA2D 外设时钟（RCC_AHB3ENR.DMA2DEN）*/

    /* 配置 LTDC 帧/行中断优先级（preempt=6, sub=0）并使能 */
    HAL_NVIC_SetPriority(LTDC_IRQn, 6, 0);  /* LTDC 行/帧事件中断（LI、Reload 事件）*/
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_NVIC_SetPriority(LTDC_ER_IRQn, 6, 0); /* LTDC 错误中断（FU/TE 事件）*/
    HAL_NVIC_EnableIRQ(LTDC_ER_IRQn);
    HAL_NVIC_SetPriority(DMA2D_IRQn, 6, 0); /* DMA2D 传输完成 / 错误中断 */
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    /* [注意] 三个中断均设为优先级6，同优先级时按 NVIC 向量号大小仲裁 */

    /* ===== LTDC RGB数据线映射说明 =====
     * （具体以 ltdc.h 宏定义和板级原理图为准）
     * R[0]=PA2(AF14)  R[2~7]=PH8~13  G[0]=PE5(条件,AF14)  G[1]=PE6(AF14)
     * G[3~7]=PH13~PH15,PI0  B[0~2]=PG14,PG12,PD6  B[3]=PA8(AF13!)
     * B[4~7]=PI4~PI7  HSYNC/VSYNC/DE/CLK=宏定义引脚(AF14)
     * [注意] PA8 使用 AF13（非 AF14），是 STM32H743 LTDC 矩阵的特殊映射 */
    /* LTDC_R7(PG6)...LTDC_B0(PG14) */

    /* 使能控制引脚所在 GPIO 端口时钟 */
    LTDC_BL_GPIO_CLK_ENABLE();               /* 背光控制引脚（BL）端口时钟 */
    LTDC_RST_GPIO_CLK_ENABLE();              /* LCD 复位引脚（RST）端口时钟 */
    LTDC_DE_GPIO_CLK_ENABLE();               /* 数据使能引脚（DE）端口时钟 */
    LTDC_VSYNC_GPIO_CLK_ENABLE();            /* VSYNC 引脚端口时钟 */
    LTDC_HSYNC_GPIO_CLK_ENABLE();            /* HSYNC 引脚端口时钟 */
    LTDC_CLK_GPIO_CLK_ENABLE();              /* 像素时钟引脚（DOTCLK）端口时钟 */

    /* 背光控制引脚：推挽输出，上拉（防浮空时背光意外关闭），高速 */
    gpio_init_struct.Pin   = LTDC_BL_GPIO_PIN;
    gpio_init_struct.Mode  = GPIO_MODE_OUTPUT_PP;   /* 推挽输出（OUTPUT_PP）*/
    gpio_init_struct.Pull  = GPIO_PULLUP;            /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_HIGH;        /* 高速 */
    HAL_GPIO_Init(LTDC_BL_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LTDC_RST_GPIO_PIN;        /* 复位控制引脚（复用同参数，仅换 Pin）*/
    HAL_GPIO_Init(LTDC_RST_GPIO_PORT, &gpio_init_struct);  /* 初始化复位引脚 */

    /* LTDC 信号线（DE/VSYNC/HSYNC/CLK）：复用推挽，无上下拉，高速，AF14 */
    gpio_init_struct.Pin       = LTDC_DE_GPIO_PIN;
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;    /* 复用推挽（Alternate Function PP）*/
    gpio_init_struct.Pull      = GPIO_NOPULL;        /* 无上下拉（信号由 LTDC 驱动，无需外部拉）*/
    gpio_init_struct.Speed     = GPIO_SPEED_HIGH;    /* 高速（匹配最高 60MHz 像素时钟）*/
    gpio_init_struct.Alternate = GPIO_AF14_LTDC;     /* AF14 = LTDC 复用功能 */
    HAL_GPIO_Init(LTDC_DE_GPIO_PORT, &gpio_init_struct);   /* DE 引脚 */

    gpio_init_struct.Pin = LTDC_VSYNC_GPIO_PIN;      /* VSYNC 引脚 */
    HAL_GPIO_Init(LTDC_VSYNC_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LTDC_HSYNC_GPIO_PIN;      /* HSYNC 引脚 */
    HAL_GPIO_Init(LTDC_HSYNC_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LTDC_CLK_GPIO_PIN;        /* 像素时钟引脚（LTDC_CLK/DOTCLK）*/
    HAL_GPIO_Init(LTDC_CLK_GPIO_PORT, &gpio_init_struct);

    /* ===== RGB 数据线 GPIO 初始化 ===== */
    /* 使能数据线所在 GPIO 端口时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();            /* PA2（数据线 AF14）/ PA8（B3，AF13！）*/
    __HAL_RCC_GPIOD_CLK_ENABLE();            /* PD6（B2，AF14）*/
    __HAL_RCC_GPIOE_CLK_ENABLE();            /* PE5（G0，条件编译）/ PE6（G1，AF14）*/
    __HAL_RCC_GPIOG_CLK_ENABLE();            /* PG6/12/13/14（R7/B1 等，AF14）*/
    __HAL_RCC_GPIOH_CLK_ENABLE();            /* PH8~PH15（R2~R7, G4~G6，AF14）*/
    __HAL_RCC_GPIOI_CLK_ENABLE();            /* PI0~PI7（G7, B4~B7，AF14）*/

    /* PA8：LTDC_B3，使用 AF13（STM32H743 特殊映射，其他数据线用 AF14）*/
    gpio_init_struct.Pin       = GPIO_PIN_8;
    gpio_init_struct.Alternate = GPIO_AF13_LTDC;     /* AF13！PA8 不用 AF14 */
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);
    /* [注意] PA8 用 AF13 而非 AF14，若误配则 B3 通道无输出，导致蓝色分量缺失 */

    /* PA2：另一条数据线，恢复 AF14 */
    gpio_init_struct.Pin       = GPIO_PIN_2;
    gpio_init_struct.Alternate = GPIO_AF14_LTDC;     /* 恢复 AF14 */
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);

    /* PD6：LTDC 数据线（AF14 已在上方设置）*/
    gpio_init_struct.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOD, &gpio_init_struct);

    /* PE5（G0，条件编译）和 PE6（G1）：
     * LTDC_USE_PE5_G0==1 时 PE5 用作 LTDC_G0；
     * ==0 时 PE5 交给 SAI1_SCK_A（音频时钟），G0 悬空（绿色最低位丢失，影响极小）*/
    /* PE5 can be switched between LTDC_G0 and SAI1_SCK_A by LTDC_USE_PE5_G0. */
#if LTDC_USE_PE5_G0
    gpio_init_struct.Pin = GPIO_PIN_5 | GPIO_PIN_6;  /* G0（PE5）+ G1（PE6）均配置为 LTDC */
#else
    gpio_init_struct.Pin = GPIO_PIN_6;               /* 仅 G1（PE6），G0 不接 LTDC */
#endif
    HAL_GPIO_Init(GPIOE, &gpio_init_struct);

    /* PG6/PG12/PG13/PG14：LTDC 数据线（R7/B1/等，AF14）*/
    gpio_init_struct.Pin = GPIO_PIN_6 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);

    /* PH8~PH15：LTDC R2~R7 + G4~G6 数据线（8 条引脚），AF14 */
    gpio_init_struct.Pin = GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10 | GPIO_PIN_11 | \
                           GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio_init_struct);

    /* PI0~PI7（跳过 PI3）：LTDC G7 + B4~B7 数据线（7 条引脚），AF14 */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | \
                           GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOI, &gpio_init_struct);
    /* [注意] PI3 未初始化：确认原理图上 PI3 确实未连接 LTDC 信号 */
}


















