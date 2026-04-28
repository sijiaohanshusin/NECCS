/**
 * @file    dma2d_accel.c
 * @brief   DMA2D 异步命令队列加速实现
 * @details
 *   通过"环形命令队列 + 中断驱动"将 DMA2D 操作与 UI 渲染主流程解耦。
 *   设计目标：
 *     - 降低 ltdc_fill_async / ltdc_copy_async 等函数的调用延迟（提交即返回）；
 *     - 中断回调内自动启动下一条命令，队列空时 DMA2D 自行停止；
 *     - 帧提交前通过 DMA2D_Accel_Flush() 轮询等待队列清空，保证显示一致性。
 *
 *   命令类型（DMA2D_CmdType_t）：
 *     FILL      - R2M 模式：固定颜色填充（ltdc_fill_async）
 *     COPY      - M2M 模式：帧缓冲区域拷贝（ltdc_copy_async）
 *     BLIT_L8   - M2M_PFC 模式：L8 灰度索引→目标格式转换（ltdc_l8_fill_async）
 *     BLEND_A8  - M2M_BLEND 模式：A8 alpha 掩码混合前景色（ltdc_a8_blend_async，字体渲染）
 *
 *   线程安全性：
 *     - s_enqueue_cmd / DMA2D_Accel_Flush 使用 PRIMASK 临界区，可在任务上下文调用。
 *     - DMA2D_Accel_IRQHandler 在 DMA2D 中断（优先级6）上下文调用，不可调用 vTask*。
 *
 *   [改进] 当前 DMA2D_Accel_Flush 为忙等轮询，长时间等待会占用 CPU；
 *          可以改为 FreeRTOS 信号量通知（PostFromISR），使任务在 DMA2D 完成前换出。
 */
#include "LCD/dma2d_accel.h"
#include "LCD/ltdc.h"

#include "stm32h7xx_hal.h"

/* 命令队列容量（条数）。设为 128 以支持单帧内大量零散绘制操作，
 * 超出时新命令被丢弃并计入 g_dma2d_queue_overflow_count。
 * [改进] 若 overflow 频繁，可增大此值或在入队失败时 CPU 回退绘制 */
#define DMA2D_CMD_QUEUE_CAPACITY  128u
/* DMA2D 中断使能位掩码：TC（传输完成）+ TE（传输错误）+ CE（配置错误）+ CAE（CLUT访问错误）*/
#define DMA2D_IT_MASK             (DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE | DMA2D_CR_CAEIE)

/**
 * @brief DMA2D 命令类型枚举
 * @details 对应 DMA2D 寄存器 CR.MODE 的四种工作模式
 */
typedef enum
{
    DMA2D_CMD_FILL     = 0u,   /* Register to Memory（R2M）：固定颜色填充，无源缓冲 */
    DMA2D_CMD_COPY     = 1u,   /* Memory to Memory（M2M）：同格式帧缓冲拷贝 */
    DMA2D_CMD_BLIT_L8  = 2u,   /* M2M with Pixel Format Conversion（M2M_PFC）：L8→RGB565 */
    DMA2D_CMD_BLEND_A8 = 3u    /* M2M Blending（M2M_BLEND）：A8 alpha 掩码混合前景色 */
} DMA2D_CmdType_t;

/**
 * @brief DMA2D 命令描述符（20 字节，适合 32 位对齐访问）
 */
typedef struct
{
    uint8_t  type;             /* 命令类型，取自 DMA2D_CmdType_t */
    uint8_t  reserved0;        /* 填充对齐，未使用 */
    uint16_t reserved1;        /* 填充对齐，未使用 */
    uint32_t src_addr;         /* 源缓冲起始地址（FILL 时为 0）*/
    uint32_t dst_addr;         /* 目标缓冲起始地址（帧缓冲内某像素坐标）*/
    uint16_t width;            /* 传输宽度（像素数，对应 NLR.PL）*/
    uint16_t height;           /* 传输高度（行数，对应 NLR.NL）*/
    uint16_t src_line_offset;  /* 源行偏移（列数，OOR.LO = 每行末尾跳过的像素数）*/
    uint16_t dst_line_offset;  /* 目标行偏移（同上，用于非全宽矩形操作）*/
    uint32_t color;            /* 填充颜色（FILL / BLEND_A8 时有效）*/
} DMA2D_Cmd_t;

/* 环形命令队列（静态分配，位于 AXI SRAM，读写均由临界区保护）*/
static DMA2D_Cmd_t       s_cmd_queue[DMA2D_CMD_QUEUE_CAPACITY];
static volatile uint16_t s_queue_head  = 0u;   /* 队首索引（出队指针，取命令处）*/
static volatile uint16_t s_queue_tail  = 0u;   /* 队尾索引（入队指针，写命令处）*/
static volatile uint16_t s_queue_count = 0u;   /* 当前队列中命令数量 */
static volatile uint8_t  s_dma2d_busy  = 0u;   /* DMA2D 硬件是否正在执行（1=忙）*/
static volatile uint8_t  s_dma2d_ready = 0u;   /* DMA2D 是否已完成初始化（0=未init）*/

/* 统计计数器（通过 dma2d_accel.h 导出，供调试查询）*/
volatile uint32_t g_dma2d_queue_overflow_count = 0u;  /* 入队失败次数（队列满溢）*/
volatile uint32_t g_dma2d_queue_error_count    = 0u;  /* DMA2D 硬件错误次数（TE/CE/CAE）*/
volatile uint32_t g_dma2d_queue_depth_peak     = 0u;  /* 历史最大队列深度（峰值水位线）*/

/* 从 ltdc.c 导入的共享计数器，用于统计超时与传输次数 */
extern volatile uint32_t g_ltdc_dma2d_timeout_count;    /* DMA2D 等待超时次数 */
extern volatile uint32_t g_ltdc_dma2d_transfer_count;   /* DMA2D 总传输启动次数 */

/**
 * @brief  从队首取出一条命令（内部辅助函数）
 * @param  cmd  输出参数，存放弹出的命令；不可为 NULL
 * @retval 1 = 成功弹出，0 = 队列为空或参数非法
 * @details
 *   调用者须在临界区（禁中断或 PRIMASK）内调用，保证 head/count 操作原子性。
 *   环形队列实现：head = (head + 1) % CAPACITY，避免额外边界检查。
 * [注意] 不检查 cmd 写入后内存对齐；DMA2D 寄存器需 32 位对齐地址。
 */
/* 从队首弹出一条命令。调用者需确保在受保护上下文中使用。 */
static uint8_t s_pop_cmd(DMA2D_Cmd_t *cmd)
{
    if ((cmd == NULL) || (s_queue_count == 0u))
    {
        return 0u;                      /* 参数非法或队列为空：拒绝操作 */
    }

    *cmd = s_cmd_queue[s_queue_head];   /* 读出队头命令（结构体整体拷贝）*/
    s_queue_head = (uint16_t)((s_queue_head + 1u) % DMA2D_CMD_QUEUE_CAPACITY);
    /* 队头指针前进一格（取模回绕，实现环形）*/
    s_queue_count--;                    /* 命令数量减1 */
    return 1u;
}

/**
 * @brief  将命令描述符写入 DMA2D 寄存器并启动传输（内部辅助函数）
 * @param  cmd  指向已弹出的命令描述符；不可为 NULL
 * @details
 *   针对四种命令类型分别配置 DMA2D 寄存器，然后通过 CR.START 启动 DMA2D。
 *   寄存器含义速查（RM0433 §23）：
 *     CR.MODE  - 工作模式：R2M(0x3)/M2M(0x0)/M2M_PFC(0x1)/M2M_BLEND(0x2)
 *     FGPFCCR  - 前景色像素格式（源格式）
 *     BGPFCCR  - 背景色像素格式（目标格式，BLEND 时有效）
 *     OPFCCR   - 输出像素格式（目标格式）
 *     FGMAR    - 前景色内存起始地址
 *     BGMAR    - 背景色内存起始地址（BLEND 时 = dst，读旧值混合）
 *     OMAR     - 输出内存起始地址（= dst）
 *     FGOR     - 前景色行偏移（列数，每行末尾跳过）
 *     BGOR     - 背景色行偏移
 *     OOR      - 输出行偏移
 *     NLR      - 传输尺寸：高 16 位 = PL（像素/行），低 16 位 = NL（行数）
 *     OCOLR    - 输出颜色寄存器（R2M 模式填充色）
 *     FGCOLR   - 前景色颜色（BLEND_A8 前景色，24 位 RGB）
 *
 *   清除中断标志在命令开始时执行（非结束时），使中断回调仅处理本条命令结果。
 */
static void s_start_cmd(const DMA2D_Cmd_t *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    /* 停止可能残留的旧传输，并清除所有中断标志，避免脏状态串扰。 */
    DMA2D->CR &= ~DMA2D_CR_START;       /* 清除 START 位（若有意外残留传输则中止）*/
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    /* 清理上一条命令状态并清中断标志，避免脏状态串扰。 */

    switch ((DMA2D_CmdType_t)cmd->type)
    {
        case DMA2D_CMD_FILL:
            /* Register to Memory（R2M）模式：颜色来自 OCOLR 寄存器，无前景源地址 */
            DMA2D->CR     = DMA2D_R2M | DMA2D_IT_MASK;  /* R2M 模式 + 使能 TC/TE/CE/CAE 中断 */
            DMA2D->OPFCCR = LTDC_PIXFORMAT;              /* 输出格式 = 帧缓冲格式（RGB565）*/
            DMA2D->OOR    = cmd->dst_line_offset;        /* 输出行偏移（非全宽矩形时跳过的列数）*/
            DMA2D->OMAR   = cmd->dst_addr;               /* 输出起始地址（矩形左上角像素地址）*/
            DMA2D->NLR    = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            /* NLR：高16位=PL（像素/行=宽度），低16位=NL（行数=高度）*/
            DMA2D->OCOLR  = cmd->color;                  /* 填充颜色（RGB565 或 ARGB8888）*/
            break;

        case DMA2D_CMD_COPY:
            /* Memory to Memory（M2M）模式：源与目标格式相同（同像素格式拷贝/移动）*/
            DMA2D->CR     = DMA2D_M2M | DMA2D_IT_MASK;  /* M2M 模式 */
            DMA2D->FGPFCCR = LTDC_PIXFORMAT;             /* 前景（源）格式 = RGB565 */
            DMA2D->FGOR    = cmd->src_line_offset;        /* 源行偏移（源缓冲每行跳过列数）*/
            DMA2D->OPFCCR = LTDC_PIXFORMAT;              /* 输出格式 = RGB565 */
            DMA2D->OOR    = cmd->dst_line_offset;        /* 输出行偏移 */
            DMA2D->FGMAR  = cmd->src_addr;               /* 前景源起始地址 */
            DMA2D->OMAR   = cmd->dst_addr;               /* 输出目标地址 */
            DMA2D->NLR    = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            break;

        case DMA2D_CMD_BLIT_L8:
            /* M2M with PFC（M2M_PFC）模式：L8（8位灰度索引）通过 CLUT 查表转换为输出格式
             * 前提：FGCLUT 已由 DMA2D_Accel_LoadClutFromRgb565 填充有效颜色表 */
            DMA2D->CR     = DMA2D_M2M_PFC | DMA2D_IT_MASK; /* M2M_PFC 模式 */
            DMA2D->FGPFCCR = DMA2D_INPUT_L8;               /* 前景（源）格式：L8 索引格式 */
            DMA2D->FGOR    = cmd->src_line_offset;
            DMA2D->OPFCCR = LTDC_PIXFORMAT;                /* 输出格式：RGB565 */
            DMA2D->OOR    = cmd->dst_line_offset;
            DMA2D->FGMAR  = cmd->src_addr;                  /* 源：L8 灰度 buffer */
            DMA2D->OMAR   = cmd->dst_addr;
            DMA2D->NLR    = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            /* [注意] CLUT 大小需通过 FGPFCCR.CS 设置（NECCS 传入 256 条目）；
             *        当前代码假设 CLUT 已预先加载，未在此处触发 CLUT 传输 */
            break;

        case DMA2D_CMD_BLEND_A8:
            /* M2M Blending（M2M_BLEND）模式：A8 alpha 掩码 × 前景色 → 混合到目标背景
             * 公式：output = alpha/255 × fg_color + (1 - alpha/255) × bg_pixel
             * 用途：字体渲染（fg=字体颜色，bg=帧缓冲已有内容），支持抗锯齿 */
            DMA2D->CR      = DMA2D_M2M_BLEND | DMA2D_IT_MASK; /* BLEND 模式 */
            DMA2D->FGPFCCR = DMA2D_INPUT_A8 | (DMA2D_NO_MODIF_ALPHA << DMA2D_FGPFCCR_AM_Pos);
            /* 前景格式：A8（每像素8位 alpha），alpha 不修改（NO_MODIF = 直接使用）*/
            DMA2D->FGCOLR  = cmd->color;    /* 前景色（24位 RGB，与 alpha 混合后输出）*/
            DMA2D->BGPFCCR = LTDC_PIXFORMAT; /* 背景格式：RGB565（帧缓冲现有内容）*/
            DMA2D->OPFCCR  = LTDC_PIXFORMAT; /* 输出格式：RGB565 */
            DMA2D->FGOR    = cmd->src_line_offset;   /* 前景（A8 源）行偏移 */
            DMA2D->BGOR    = cmd->dst_line_offset;   /* 背景（帧缓冲）行偏移 */
            DMA2D->OOR     = cmd->dst_line_offset;   /* 输出行偏移（覆盖写回 dst）*/
            DMA2D->FGMAR   = cmd->src_addr;  /* 前景源：A8 灰度 buffer（字体 alpha mask）*/
            DMA2D->BGMAR   = cmd->dst_addr;  /* 背景源：帧缓冲（读旧像素参与混合）*/
            DMA2D->OMAR    = cmd->dst_addr;  /* 输出目标：写回同一帧缓冲地址（原地混合）*/
            DMA2D->NLR     = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            break;

        default:
            /* 未知命令类型：记录错误，不启动 DMA2D */
            g_dma2d_queue_error_count++;
            return;
    }

    g_ltdc_dma2d_transfer_count++;  /* 统计启动传输次数（供性能评估）*/
    s_dma2d_busy = 1u;              /* 标记 DMA2D 忙（在 TC 中断回调中由硬件清零）*/
    DMA2D->CR |= DMA2D_CR_START;    /* 启动 DMA2D 传输（写1后硬件自动清0）*/
}

/**
 * @brief  尝试从队列启动下一条命令（内部辅助函数）
 * @details
 *   仅在 DMA2D 空闲（s_dma2d_busy==0）且队列非空时调用 s_start_cmd。
 *   在 s_enqueue_cmd 和 DMA2D_Accel_IRQHandler 中均会调用（分别对应
 *   "刚入队且 DMA2D 空闲"和"上一条命令完成后继续"两个时机）。
 *   调用前须已进入临界区或中断上下文。
 */
static void s_try_start_next(void)
{
    DMA2D_Cmd_t cmd;
    if ((s_dma2d_busy != 0u) || (s_queue_count == 0u))
    {
        return;                         /* DMA2D 忙 或 队列空：什么都不做 */
    }
    if (s_pop_cmd(&cmd) != 0u)
    {
        s_start_cmd(&cmd);              /* 弹出一条命令并启动 DMA2D */
    }
}

/**
 * @brief  将命令压入环形队列（内部辅助函数）
 * @param  cmd  命令描述符指针
 * @retval 1 = 入队成功，0 = 参数非法 / 队列满 / DMA2D 未初始化
 * @details
 *   使用 PRIMASK 临界区保护队列共享变量（与 DMA2D 中断竞争）：
 *     1. 保存 PRIMASK → 禁中断；
 *     2. 检查 s_dma2d_ready（未初始化则拒绝）；
 *     3. 检查 s_queue_count < CAPACITY；
 *     4. 写入 s_cmd_queue[tail] → tail++ → count++；
 *     5. 更新 peak 水位线；
 *     6. 调用 s_try_start_next（若 DMA2D 空闲则立即启动）；
 *     7. 恢复 PRIMASK。
 *   临界区尽可能短（仅操作计数器和启动检查），不包含寄存器写入。
 *   [注意] s_start_cmd 本身也在临界区内执行（通过 s_try_start_next 调用），
 *          但寄存器写入速度很快，不会显著延长中断禁用时间。
 */
static uint8_t s_enqueue_cmd(const DMA2D_Cmd_t *cmd)
{
    uint32_t primask;
    uint8_t ok = 0u;

    if ((cmd == NULL) || (cmd->width == 0u) || (cmd->height == 0u))
    {
        return 0u;                      /* 参数非法：宽/高为0的矩形无意义 */
    }

    /* 队列与 busy 状态在中断和任务上下文共享，使用短临界区保护。 */
    primask = __get_PRIMASK();
    __disable_irq();                    /* 禁中断，进入临界区 */
    if (s_dma2d_ready != 0u)           /* DMA2D_Accel_Init() 已调用？ */
    {
        if (s_queue_count < DMA2D_CMD_QUEUE_CAPACITY)
        {
            s_cmd_queue[s_queue_tail] = *cmd;   /* 将命令写入队尾槽 */
            s_queue_tail = (uint16_t)((s_queue_tail + 1u) % DMA2D_CMD_QUEUE_CAPACITY);
            /* 队尾指针前进一格（取模回绕，实现环形）*/
            s_queue_count++;
            if (s_queue_count > g_dma2d_queue_depth_peak)
            {
                g_dma2d_queue_depth_peak = s_queue_count;   /* 更新历史峰值水位线 */
            }
            s_try_start_next();         /* 若 DMA2D 空闲，立即启动本命令 */
            ok = 1u;
        }
        else
        {
            g_dma2d_queue_overflow_count++;  /* 队列满：丢弃命令，计数 */
            /* [注意] 溢出时不 CPU 回退绘制，调用方 ltdc_fill_async 应检查返回值 */
        }
    }
    if (primask == 0u)
    {
        __enable_irq();                 /* 恢复中断（仅当之前未被禁用时）*/
    }
    return ok;
}

/**
 * @brief  初始化 DMA2D 加速器（首次调用时使用，由 ltdc_init 调用）
 * @details
 *   清零队列状态变量并将 DMA2D 硬件置于已知初始态（停止传输，清中断标志）。
 *   设置 s_dma2d_ready=1，使 s_enqueue_cmd 开始接受命令。
 *   [注意] DMA2D 时钟（__HAL_RCC_DMA2D_CLK_ENABLE）在此处也使能一次，
 *          与 HAL_LTDC_MspInit 中的使能幂等，无副作用。
 *   [注意] 不配置 CLUT。若需使用 BLIT_L8 命令，须在此后调用
 *          DMA2D_Accel_LoadClutFromRgb565 预填充颜色查找表。
 */
void DMA2D_Accel_Init(void)
{
    uint32_t primask = __get_PRIMASK();

    __HAL_RCC_DMA2D_CLK_ENABLE();      /* 确保 DMA2D 时钟已使能（幂等）*/

    __disable_irq();                   /* 临界区：防止 DMA2D 中断在初始化过程中触发 */
    s_queue_head  = 0u;               /* 队首指针归零 */
    s_queue_tail  = 0u;               /* 队尾指针归零 */
    s_queue_count = 0u;               /* 命令计数清零 */
    s_dma2d_busy  = 0u;               /* 标记 DMA2D 空闲 */
    s_dma2d_ready = 1u;               /* 标记初始化完成，s_enqueue_cmd 可以接受命令 */
    g_dma2d_queue_overflow_count = 0u; /* 清零统计计数器 */
    g_dma2d_queue_error_count    = 0u;
    g_dma2d_queue_depth_peak     = 0u;
    DMA2D->CR  &= ~DMA2D_CR_START;    /* 停止任何残留的 DMA2D 传输 */
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    /* 清除所有 DMA2D 中断标志，避免初始化完成后立即触发假中断 */
    if (primask == 0u)
    {
        __enable_irq();
    }
}

/**
 * @brief  复位 DMA2D 加速器（丢弃队列中所有待处理命令）
 * @details
 *   紧急情况（如帧超时、显示异常）下调用，强制清空命令队列并停止正在进行的传输。
 *   复位后需重新调用 DMA2D_Accel_Init 才能重新使用（s_dma2d_ready 未被更改）。
 *   [注意] 此函数不重置 s_dma2d_ready，允许下一帧立即入队新命令。
 *   [改进] 当前实现丢弃所有命令；若部分命令已绘制，画面可能出现不完整渲染帧。
 */
void DMA2D_Accel_Reset(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_queue_head  = 0u;               /* 清空队列（丢弃所有待处理命令）*/
    s_queue_tail  = 0u;
    s_queue_count = 0u;
    s_dma2d_busy  = 0u;               /* 标记空闲（硬件传输可能仍在进行，但软件忽略其结果）*/
    DMA2D->CR  &= ~DMA2D_CR_START;    /* 强制停止 DMA2D 传输 */
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    if (primask == 0u)
    {
        __enable_irq();
    }
}

/**
 * @brief  DMA2D 传输完成/错误中断处理函数（ISR 上下文）
 * @details
 *   由 stm32h7xx_it.c 中的 DMA2D_IRQHandler 调用（或 HAL 弱符号覆盖）。
 *   处理逻辑：
 *     1. 读取 ISR（中断状态寄存器）；
 *     2. 若有错误标志（TE/CE/CAE）：计数 + 清标志；
 *     3. 若有传输完成（TC）：清 TC 标志；
 *     4. 清除 s_dma2d_busy，调用 s_try_start_next 继续执行队列下一条命令。
 *
 *   [注意] TE（传输错误）通常由 AXI 总线故障引起，CE（配置错误）由寄存器设置有误引起，
 *          CAE（CLUT 访问错误）由 CLUT 传输错误引起。出现时仍继续执行后续命令，
 *          避免队列永久阻塞。
 *   [注意] ISR 上下文（中断优先级6），不可调用 FreeRTOS 阻塞 API。
 */
void DMA2D_Accel_IRQHandler(void)
{
    uint32_t isr = DMA2D->ISR;          /* 读取中断状态寄存器 */
    uint32_t primask;

    /* 错误优先处理：统计后清标志，并推进队列继续执行后续命令。 */
    if ((isr & (DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE)) != 0u)
    {
        /* DMA2D 硬件错误：统计并清除所有中断标志 */
        g_dma2d_queue_error_count++;    /* 硬件错误计数 */
        g_ltdc_dma2d_timeout_count++;   /* 借用 timeout 计数（实际是传输失败）*/
        DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
        /* [改进] 错误时可将当前命令重新入队重试，而不是直接丢弃 */
    }
    else if ((isr & DMA2D_FLAG_TC) != 0u)
    {
        /* 传输完成（TC）：清除 TC 标志 */
        DMA2D->IFCR = DMA2D_FLAG_TC;
    }

    /* 更新 busy 标志并启动下一条命令 */
    primask = __get_PRIMASK();
    __disable_irq();                    /* 临界区：保护 s_dma2d_busy 和队列状态 */
    s_dma2d_busy = 0u;                  /* 当前命令已结束，标记 DMA2D 空闲 */
    s_try_start_next();                 /* 若队列非空，立即启动下一条命令 */
    if (primask == 0u)
    {
        __enable_irq();
    }
}

/**
 * @brief  异步填充矩形：入队一条 R2M（固定颜色填充）命令
 * @param  dst_addr       目标矩形左上角像素在帧缓冲中的字节地址
 * @param  width          矩形宽度（像素）
 * @param  height         矩形高度（行数）
 * @param  dst_line_offset 每行末尾跳过的像素数（= 帧缓冲宽度 - 矩形宽度）
 * @param  color          填充颜色（格式与 LTDC_PIXFORMAT 一致，RGB565 下取低16位）
 * @retval 1 = 入队成功；0 = 队列满或 DMA2D 未初始化
 */
uint8_t DMA2D_Accel_EnqueueFill(uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t dst_line_offset,
                                uint32_t color)
{
    DMA2D_Cmd_t cmd;
    cmd.type            = (uint8_t)DMA2D_CMD_FILL; /* 固定颜色填充（R2M）*/
    cmd.reserved0       = 0u;
    cmd.reserved1       = 0u;
    cmd.src_addr        = 0u;          /* FILL 无源地址 */
    cmd.dst_addr        = dst_addr;
    cmd.width           = width;
    cmd.height          = height;
    cmd.src_line_offset = 0u;          /* FILL 不需要源行偏移 */
    cmd.dst_line_offset = dst_line_offset; /* 帧缓冲宽度 - 矩形宽度 */
    cmd.color           = color;
    return s_enqueue_cmd(&cmd);
}

/**
 * @brief  异步内存拷贝：入队一条 M2M（同格式区域拷贝）命令
 * @param  src_addr       源矩形左上角像素地址
 * @param  dst_addr       目标矩形左上角像素地址
 * @param  width          矩形宽度（像素）
 * @param  height         矩形高度（行数）
 * @param  src_line_offset 源行偏移（源缓冲宽度 - 矩形宽度）
 * @param  dst_line_offset 目标行偏移
 * @retval 1 = 入队成功；0 = 失败
 */
uint8_t DMA2D_Accel_EnqueueCopy(uint32_t src_addr,
                                uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t src_line_offset,
                                uint16_t dst_line_offset)
{
    DMA2D_Cmd_t cmd;
    cmd.type            = (uint8_t)DMA2D_CMD_COPY; /* 同格式拷贝（M2M）*/
    cmd.reserved0       = 0u;
    cmd.reserved1       = 0u;
    cmd.src_addr        = src_addr;
    cmd.dst_addr        = dst_addr;
    cmd.width           = width;
    cmd.height          = height;
    cmd.src_line_offset = src_line_offset;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color           = 0u;          /* COPY 无颜色参数 */
    return s_enqueue_cmd(&cmd);
}

/**
 * @brief  异步 L8→RGB565 转换：入队一条 M2M_PFC（含格式转换）命令
 * @param  src_addr       L8 灰度索引 buffer 地址（8位/像素）
 * @param  dst_addr       目标帧缓冲地址（RGB565）
 * @param  width          矩形宽度（像素）
 * @param  height         矩形高度（行数）
 * @param  src_line_offset 源 L8 行偏移
 * @param  dst_line_offset 目标帧缓冲行偏移
 * @retval 1 = 入队成功；0 = 失败
 * @note   调用前须确保 FGCLUT 已由 DMA2D_Accel_LoadClutFromRgb565 填充，
 *         否则颜色映射结果不可预期。
 */
uint8_t DMA2D_Accel_EnqueueBlitL8(uint32_t src_addr,
                                  uint32_t dst_addr,
                                  uint16_t width,
                                  uint16_t height,
                                  uint16_t src_line_offset,
                                  uint16_t dst_line_offset)
{
    DMA2D_Cmd_t cmd;
    cmd.type            = (uint8_t)DMA2D_CMD_BLIT_L8; /* L8→RGB565 格式转换（M2M_PFC）*/
    cmd.reserved0       = 0u;
    cmd.reserved1       = 0u;
    cmd.src_addr        = src_addr;    /* L8 源 buffer */
    cmd.dst_addr        = dst_addr;
    cmd.width           = width;
    cmd.height          = height;
    cmd.src_line_offset = src_line_offset;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color           = 0u;
    return s_enqueue_cmd(&cmd);
}

/**
 * @brief  异步 A8 alpha 混合：入队一条 M2M_BLEND（alpha 混合）命令
 * @param  src_addr        A8 alpha 掩码 buffer 地址（8位/像素，0=透明，255=不透明）
 * @param  dst_addr        目标帧缓冲地址（同时作为背景源和输出目标）
 * @param  width           矩形宽度（像素）
 * @param  height          矩形高度（行数）
 * @param  src_line_offset A8 源行偏移
 * @param  dst_line_offset 帧缓冲行偏移
 * @param  color565        前景颜色（RGB565 格式，与 alpha 掩码混合后叠加到背景）
 * @retval 1 = 入队成功；0 = 失败
 * @note   典型用途：LVGL 字体渲染。字体 bitmap 为 A8 灰度，前景色为文字颜色，
 *         背景为帧缓冲已有画面（平滑抗锯齿效果）。
 */
uint8_t DMA2D_Accel_EnqueueBlendA8(uint32_t src_addr,
                                   uint32_t dst_addr,
                                   uint16_t width,
                                   uint16_t height,
                                   uint16_t src_line_offset,
                                   uint16_t dst_line_offset,
                                   uint16_t color565)
{
    DMA2D_Cmd_t cmd;
    cmd.type            = (uint8_t)DMA2D_CMD_BLEND_A8; /* A8 alpha 混合（M2M_BLEND）*/
    cmd.reserved0       = 0u;
    cmd.reserved1       = 0u;
    cmd.src_addr        = src_addr;    /* A8 灰度 buffer（字体 alpha mask）*/
    cmd.dst_addr        = dst_addr;    /* 帧缓冲地址（同时作为背景读取 + 输出写入）*/
    cmd.width           = width;
    cmd.height          = height;
    cmd.src_line_offset = src_line_offset;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color           = (uint32_t)color565;  /* 前景色（RGB565，存为 uint32 低16位）*/
    return s_enqueue_cmd(&cmd);
}

/**
 * @brief  等待 DMA2D 命令队列完全排空（阻塞轮询）
 * @param  timeout_loop   最大轮询次数（每次轮询不含延时，约1~5个CPU周期）
 * @retval 0 = 队列已空（成功）；1 = 超时（DMA2D 仍有命令未完成）
 * @details
 *   在帧提交（ltdc_request_swap）前调用，确保所有异步 DMA2D 绘制操作都已完成，
 *   帧缓冲内容与 CPU 绘制的最终结果一致，再切换前后缓冲。
 *   超时会统计到 g_ltdc_dma2d_timeout_count。
 *   [改进] 当前忙等占用 CPU；建议改为 FreeRTOS 信号量或 TaskNotifyFromISR，
 *          在 DMA2D_Accel_IRQHandler TC 完成时通知等待任务，节省 CPU 时间。
 */
uint8_t DMA2D_Accel_Flush(uint32_t timeout_loop)
{
    /* 轮询等待 busy=0 且队列为空。用于帧提交前的同步点。 */
    while (timeout_loop > 0u)
    {
        uint8_t  busy;
        uint16_t count;
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        busy  = s_dma2d_busy;           /* 读取 DMA2D 忙标志（原子读）*/
        count = s_queue_count;           /* 读取队列计数（原子读）*/
        if (primask == 0u)
        {
            __enable_irq();
        }

        if ((busy == 0u) && (count == 0u))
        {
            return 0u;                  /* 队列空且 DMA2D 空闲：Flush 成功 */
        }
        timeout_loop--;
    }

    g_ltdc_dma2d_timeout_count++;       /* 超时计数：DMA2D 在规定轮询次数内未完成 */
    return 1u;
}

/**
 * @brief  从 RGB565 调色板数组加载 CLUT（颜色查找表）到 DMA2D FGCLUT
 * @param  lut    RGB565 调色板数组指针（每项 2 字节）
 * @param  count  有效入口数量（0~256）
 * @details
 *   FGCLUT 格式要求 ARGB8888（4字节/条目）。本函数将 RGB565 扩展为 ARGB8888：
 *     R5 → R8：r8 = (r5 × 255 + 15) / 31（最近整数近似，比 r5<<3 精度更高）
 *     G6 → G8：g8 = (g6 × 255 + 31) / 63
 *     B5 → B8：b8 = (b5 × 255 + 15) / 31
 *     Alpha：固定为 0xFF（完全不透明）
 *   CLUT 条目0~count-1 由 lut[] 填充，count~255 复制最后一个有效条目
 *   （避免 CLUT 读取未初始化位置导致颜色错误）。
 *   若 count==0，将所有 256 条目清零（透明黑）。
 *   [注意] 此函数直接写 FGCLUT 寄存器（MMIO），需在 DMA2D_Accel_Init 后调用；
 *          写入时不得有正在进行的 DMA2D 传输（否则寄存器访问可能冲突）。
 *   [改进] 可增加等待 DMA2D 空闲后再写 CLUT 的保护逻辑。
 */
void DMA2D_Accel_LoadClutFromRgb565(const uint16_t *lut, uint16_t count)
{
    uint16_t i;

    if (lut == NULL)
    {
        return;                         /* 防护：空指针直接返回 */
    }
    if (count > 256u)
    {
        count = 256u;                   /* CLUT 最多 256 条目（L8 格式限制）*/
    }
    if (count == 0u)
    {
        /* count==0：清零所有 256 条目（透明黑色）*/
        for (i = 0u; i < 256u; i++)
        {
            DMA2D->FGCLUT[i] = 0u;
        }
        return;
    }

    /* 将 RGB565 扩展为 ARGB8888 并写入 FGCLUT */
    for (i = 0u; i < count; i++)
    {
        uint16_t c  = lut[i];           /* 读取 RGB565 原始值 */
        uint8_t  r5 = (uint8_t)((c >> 11) & 0x1Fu);    /* 提取 R5（bit15:11）*/
        uint8_t  g6 = (uint8_t)((c >>  5) & 0x3Fu);    /* 提取 G6（bit10:5）*/
        uint8_t  b5 = (uint8_t)( c        & 0x1Fu);    /* 提取 B5（bit4:0）*/
        uint8_t  r8 = (uint8_t)((r5 * 255u + 15u) / 31u);  /* R5→R8 扩展（最近整数）*/
        uint8_t  g8 = (uint8_t)((g6 * 255u + 31u) / 63u);  /* G6→G8 扩展 */
        uint8_t  b8 = (uint8_t)((b5 * 255u + 15u) / 31u);  /* B5→B8 扩展 */
        DMA2D->FGCLUT[i] = (0xFFu << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
        /* ARGB8888：Alpha=0xFF（完全不透明）| R8 | G8 | B8 */
    }

    /* 剩余 count~255 条目：复制最后一个有效条目，避免未初始化随机值造成颜色错误 */
    for (; i < 256u; i++)
    {
        DMA2D->FGCLUT[i] = DMA2D->FGCLUT[count - 1u];
    }
}
