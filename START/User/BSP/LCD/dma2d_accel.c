/**
 * @file    dma2d_accel.c
 * @brief   DMA2D 异步命令队列加速实现
 * @details 通过环形队列 + 中断驱动将 DMA2D 操作与 UI 渲染主流程解耦。
 *          目标是降低 `ltdc_*` 绘制函数的阻塞时间，稳定帧周期。
 */
#include "LCD/dma2d_accel.h"
#include "LCD/ltdc.h"

#include "stm32h7xx_hal.h"

#define DMA2D_CMD_QUEUE_CAPACITY  128u
#define DMA2D_IT_MASK             (DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE | DMA2D_CR_CAEIE)

typedef enum
{
    DMA2D_CMD_FILL = 0u,
    DMA2D_CMD_COPY = 1u,
    DMA2D_CMD_BLIT_L8 = 2u,
    DMA2D_CMD_BLEND_A8 = 3u
} DMA2D_CmdType_t;

typedef struct
{
    uint8_t type;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t width;
    uint16_t height;
    uint16_t src_line_offset;
    uint16_t dst_line_offset;
    uint32_t color;
} DMA2D_Cmd_t;

static DMA2D_Cmd_t s_cmd_queue[DMA2D_CMD_QUEUE_CAPACITY];
static volatile uint16_t s_queue_head = 0u;
static volatile uint16_t s_queue_tail = 0u;
static volatile uint16_t s_queue_count = 0u;
static volatile uint8_t s_dma2d_busy = 0u;
static volatile uint8_t s_dma2d_ready = 0u;

volatile uint32_t g_dma2d_queue_overflow_count = 0u;
volatile uint32_t g_dma2d_queue_error_count = 0u;
volatile uint32_t g_dma2d_queue_depth_peak = 0u;

extern volatile uint32_t g_ltdc_dma2d_timeout_count;
extern volatile uint32_t g_ltdc_dma2d_transfer_count;

/* 从队首弹出一条命令。调用者需确保在受保护上下文中使用。 */
static uint8_t s_pop_cmd(DMA2D_Cmd_t *cmd)
{
    if ((cmd == NULL) || (s_queue_count == 0u))
    {
        return 0u;
    }

    *cmd = s_cmd_queue[s_queue_head];
    s_queue_head = (uint16_t)((s_queue_head + 1u) % DMA2D_CMD_QUEUE_CAPACITY);
    s_queue_count--;
    return 1u;
}

static void s_start_cmd(const DMA2D_Cmd_t *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    /* 清理上一条命令状态并清中断标志，避免脏状态串扰。 */
    DMA2D->CR &= ~DMA2D_CR_START;
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;

    switch ((DMA2D_CmdType_t)cmd->type)
    {
        case DMA2D_CMD_FILL:
            DMA2D->CR = DMA2D_R2M | DMA2D_IT_MASK;
            DMA2D->OPFCCR = LTDC_PIXFORMAT;
            DMA2D->OOR = cmd->dst_line_offset;
            DMA2D->OMAR = cmd->dst_addr;
            DMA2D->NLR = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            DMA2D->OCOLR = cmd->color;
            break;

        case DMA2D_CMD_COPY:
            DMA2D->CR = DMA2D_M2M | DMA2D_IT_MASK;
            DMA2D->FGPFCCR = LTDC_PIXFORMAT;
            DMA2D->FGOR = cmd->src_line_offset;
            DMA2D->OPFCCR = LTDC_PIXFORMAT;
            DMA2D->OOR = cmd->dst_line_offset;
            DMA2D->FGMAR = cmd->src_addr;
            DMA2D->OMAR = cmd->dst_addr;
            DMA2D->NLR = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            break;

        case DMA2D_CMD_BLIT_L8:
            DMA2D->CR = DMA2D_M2M_PFC | DMA2D_IT_MASK;
            DMA2D->FGPFCCR = DMA2D_INPUT_L8;
            DMA2D->FGOR = cmd->src_line_offset;
            DMA2D->OPFCCR = LTDC_PIXFORMAT;
            DMA2D->OOR = cmd->dst_line_offset;
            DMA2D->FGMAR = cmd->src_addr;
            DMA2D->OMAR = cmd->dst_addr;
            DMA2D->NLR = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            break;

        case DMA2D_CMD_BLEND_A8:
            DMA2D->CR = DMA2D_M2M_BLEND | DMA2D_IT_MASK;
            DMA2D->FGPFCCR = DMA2D_INPUT_A8 | (DMA2D_NO_MODIF_ALPHA << DMA2D_FGPFCCR_AM_Pos);
            DMA2D->FGCOLR = cmd->color;
            DMA2D->BGPFCCR = LTDC_PIXFORMAT;
            DMA2D->OPFCCR = LTDC_PIXFORMAT;
            DMA2D->FGOR = cmd->src_line_offset;
            DMA2D->BGOR = cmd->dst_line_offset;
            DMA2D->OOR = cmd->dst_line_offset;
            DMA2D->FGMAR = cmd->src_addr;
            DMA2D->BGMAR = cmd->dst_addr;
            DMA2D->OMAR = cmd->dst_addr;
            DMA2D->NLR = ((uint32_t)cmd->width << 16) | (uint32_t)cmd->height;
            break;

        default:
            g_dma2d_queue_error_count++;
            return;
    }

    g_ltdc_dma2d_transfer_count++;
    s_dma2d_busy = 1u;
    DMA2D->CR |= DMA2D_CR_START;
}

static void s_try_start_next(void)
{
    DMA2D_Cmd_t cmd;
    if ((s_dma2d_busy != 0u) || (s_queue_count == 0u))
    {
        return;
    }
    if (s_pop_cmd(&cmd) != 0u)
    {
        s_start_cmd(&cmd);
    }
}

static uint8_t s_enqueue_cmd(const DMA2D_Cmd_t *cmd)
{
    uint32_t primask;
    uint8_t ok = 0u;

    if ((cmd == NULL) || (cmd->width == 0u) || (cmd->height == 0u))
    {
        return 0u;
    }

    /* 队列与 busy 状态在中断和任务上下文共享，使用短临界区保护。 */
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_dma2d_ready != 0u)
    {
        if (s_queue_count < DMA2D_CMD_QUEUE_CAPACITY)
        {
            s_cmd_queue[s_queue_tail] = *cmd;
            s_queue_tail = (uint16_t)((s_queue_tail + 1u) % DMA2D_CMD_QUEUE_CAPACITY);
            s_queue_count++;
            if (s_queue_count > g_dma2d_queue_depth_peak)
            {
                g_dma2d_queue_depth_peak = s_queue_count;
            }
            s_try_start_next();
            ok = 1u;
        }
        else
        {
            g_dma2d_queue_overflow_count++;
        }
    }
    if (primask == 0u)
    {
        __enable_irq();
    }
    return ok;
}

void DMA2D_Accel_Init(void)
{
    uint32_t primask = __get_PRIMASK();

    __HAL_RCC_DMA2D_CLK_ENABLE();

    __disable_irq();
    s_queue_head = 0u;
    s_queue_tail = 0u;
    s_queue_count = 0u;
    s_dma2d_busy = 0u;
    s_dma2d_ready = 1u;
    g_dma2d_queue_overflow_count = 0u;
    g_dma2d_queue_error_count = 0u;
    g_dma2d_queue_depth_peak = 0u;
    DMA2D->CR &= ~DMA2D_CR_START;
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    if (primask == 0u)
    {
        __enable_irq();
    }
}

void DMA2D_Accel_Reset(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_queue_head = 0u;
    s_queue_tail = 0u;
    s_queue_count = 0u;
    s_dma2d_busy = 0u;
    DMA2D->CR &= ~DMA2D_CR_START;
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    if (primask == 0u)
    {
        __enable_irq();
    }
}

void DMA2D_Accel_IRQHandler(void)
{
    uint32_t isr = DMA2D->ISR;
    uint32_t primask;

    /* 错误优先处理：统计后清标志，并推进队列继续执行后续命令。 */
    if ((isr & (DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE)) != 0u)
    {
        g_dma2d_queue_error_count++;
        g_ltdc_dma2d_timeout_count++;
        DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    }
    else if ((isr & DMA2D_FLAG_TC) != 0u)
    {
        DMA2D->IFCR = DMA2D_FLAG_TC;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    s_dma2d_busy = 0u;
    s_try_start_next();
    if (primask == 0u)
    {
        __enable_irq();
    }
}

uint8_t DMA2D_Accel_EnqueueFill(uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t dst_line_offset,
                                uint32_t color)
{
    DMA2D_Cmd_t cmd;
    cmd.type = (uint8_t)DMA2D_CMD_FILL;
    cmd.reserved0 = 0u;
    cmd.reserved1 = 0u;
    cmd.src_addr = 0u;
    cmd.dst_addr = dst_addr;
    cmd.width = width;
    cmd.height = height;
    cmd.src_line_offset = 0u;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color = color;
    return s_enqueue_cmd(&cmd);
}

uint8_t DMA2D_Accel_EnqueueCopy(uint32_t src_addr,
                                uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t src_line_offset,
                                uint16_t dst_line_offset)
{
    DMA2D_Cmd_t cmd;
    cmd.type = (uint8_t)DMA2D_CMD_COPY;
    cmd.reserved0 = 0u;
    cmd.reserved1 = 0u;
    cmd.src_addr = src_addr;
    cmd.dst_addr = dst_addr;
    cmd.width = width;
    cmd.height = height;
    cmd.src_line_offset = src_line_offset;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color = 0u;
    return s_enqueue_cmd(&cmd);
}

uint8_t DMA2D_Accel_EnqueueBlitL8(uint32_t src_addr,
                                  uint32_t dst_addr,
                                  uint16_t width,
                                  uint16_t height,
                                  uint16_t src_line_offset,
                                  uint16_t dst_line_offset)
{
    DMA2D_Cmd_t cmd;
    cmd.type = (uint8_t)DMA2D_CMD_BLIT_L8;
    cmd.reserved0 = 0u;
    cmd.reserved1 = 0u;
    cmd.src_addr = src_addr;
    cmd.dst_addr = dst_addr;
    cmd.width = width;
    cmd.height = height;
    cmd.src_line_offset = src_line_offset;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color = 0u;
    return s_enqueue_cmd(&cmd);
}

uint8_t DMA2D_Accel_EnqueueBlendA8(uint32_t src_addr,
                                   uint32_t dst_addr,
                                   uint16_t width,
                                   uint16_t height,
                                   uint16_t src_line_offset,
                                   uint16_t dst_line_offset,
                                   uint16_t color565)
{
    DMA2D_Cmd_t cmd;
    cmd.type = (uint8_t)DMA2D_CMD_BLEND_A8;
    cmd.reserved0 = 0u;
    cmd.reserved1 = 0u;
    cmd.src_addr = src_addr;
    cmd.dst_addr = dst_addr;
    cmd.width = width;
    cmd.height = height;
    cmd.src_line_offset = src_line_offset;
    cmd.dst_line_offset = dst_line_offset;
    cmd.color = (uint32_t)color565;
    return s_enqueue_cmd(&cmd);
}

uint8_t DMA2D_Accel_Flush(uint32_t timeout_loop)
{
    /* 轮询等待 busy=0 且队列为空。用于帧提交前的同步点。 */
    while (timeout_loop > 0u)
    {
        uint8_t busy;
        uint16_t count;
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        busy = s_dma2d_busy;
        count = s_queue_count;
        if (primask == 0u)
        {
            __enable_irq();
        }

        if ((busy == 0u) && (count == 0u))
        {
            return 0u;
        }
        timeout_loop--;
    }

    g_ltdc_dma2d_timeout_count++;
    return 1u;
}

void DMA2D_Accel_LoadClutFromRgb565(const uint16_t *lut, uint16_t count)
{
    uint16_t i;

    if (lut == NULL)
    {
        return;
    }
    if (count > 256u)
    {
        count = 256u;
    }
    if (count == 0u)
    {
        for (i = 0u; i < 256u; i++)
        {
            DMA2D->FGCLUT[i] = 0u;
        }
        return;
    }

    for (i = 0u; i < count; i++)
    {
        uint16_t c = lut[i];
        uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
        uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
        uint8_t b5 = (uint8_t)(c & 0x1Fu);
        uint8_t r8 = (uint8_t)((r5 * 255u + 15u) / 31u);
        uint8_t g8 = (uint8_t)((g6 * 255u + 31u) / 63u);
        uint8_t b8 = (uint8_t)((b5 * 255u + 15u) / 31u);
        DMA2D->FGCLUT[i] = (0xFFu << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
    }

    for (; i < 256u; i++)
    {
        DMA2D->FGCLUT[i] = DMA2D->FGCLUT[count - 1u];
    }
}
