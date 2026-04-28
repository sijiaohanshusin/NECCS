/**
 * @file   app_camera.c
 * @brief  OV2640 摄像头 DCMI 驱动与帧发布模块
 * @details 管理 OV2640 传感器初始化、DCMI 双缓冲 DMA 采集、行级数据拷贝、
 *          发布缓冲管理（引用计数）以及 FreeRTOS 服务任务调度。
 *          通过 APP_CAMERA_ENABLE 宏控制是否编译摄像头功能。
 */

#include "app_camera.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_user_config.h"
#include "camera_ov2640.h"
#include "main.h"
#include "mpu.h"

#include <stdio.h>
#include <string.h>

#define APP_CAMERA_FB0_ADDR             0xC0400000u
#define APP_CAMERA_FB1_ADDR             (APP_CAMERA_FB0_ADDR + APP_CAMERA_FRAME_BYTES)
#define APP_CAMERA_PUB_COUNT            4u
#define APP_CAMERA_PUB0_ADDR            0xC0500000u
#define APP_CAMERA_PUB1_ADDR            (APP_CAMERA_PUB0_ADDR + APP_CAMERA_FRAME_BYTES)
#define APP_CAMERA_PUB2_ADDR            (APP_CAMERA_PUB1_ADDR + APP_CAMERA_FRAME_BYTES)
#define APP_CAMERA_PUB3_ADDR            (APP_CAMERA_PUB2_ADDR + APP_CAMERA_FRAME_BYTES)
#define APP_CAMERA_FRAME_BYTES          (APP_CAMERA_PREVIEW_W * APP_CAMERA_PREVIEW_H * 2u)
#define APP_CAMERA_FRAME_PIXELS         (APP_CAMERA_PREVIEW_W * APP_CAMERA_PREVIEW_H)
#define APP_CAMERA_DMA_FRAME_WORDS      (APP_CAMERA_FRAME_BYTES / 4u)
#define APP_CAMERA_DMA_TOTAL_WORDS      (APP_CAMERA_DMA_FRAME_WORDS * 2u)
#define APP_CAMERA_DMA_LINE_WORDS       (APP_CAMERA_PREVIEW_W / 2u)
#define APP_CAMERA_LINE_BYTES           (APP_CAMERA_PREVIEW_W * 2u)
#define APP_CAMERA_RAW_WINDOW_LIMIT     0xC0500000u
#define APP_CAMERA_SDRAM_LIMIT          0xC2000000u
#define APP_CAMERA_SERVICE_TASK_STACK_WORDS  640u
#define APP_CAMERA_SERVICE_TASK_PRIO         (APP_AUDIO_TASK_PRIO + 1u)
#define APP_CAMERA_SERVICE_WAKE_MS           50u

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FRAME_BYTES % 4u) != 0u))
#error "Camera frame buffer size must be aligned to 32-bit DMA transfers"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_PREVIEW_W % 2u) != 0u))
#error "Camera preview width must be even for line DMA packing"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FB1_ADDR - APP_CAMERA_FB0_ADDR) != APP_CAMERA_FRAME_BYTES))
#error "Camera framebuffer addresses must match the configured frame size"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FB1_ADDR + APP_CAMERA_FRAME_BYTES) > APP_CAMERA_RAW_WINDOW_LIMIT))
#error "Camera raw buffers must stay inside the dedicated non-cacheable SDRAM window"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_PUB3_ADDR + APP_CAMERA_FRAME_BYTES) > APP_CAMERA_SDRAM_LIMIT))
#error "Camera published buffers must stay inside SDRAM"
#endif

#if (APP_CAMERA_ENABLE != 0u)
/** @brief DCMI 外设句柄 */
static DCMI_HandleTypeDef s_hdcmi;
/** @brief DCMI 关联的 DMA 句柄 */
static DMA_HandleTypeDef s_hdma_dcmi;
/** @brief 摄像头服务任务句柄 */
static TaskHandle_t s_camera_service_task_handle = NULL;

/** @brief 传感器是否已初始化 */
static volatile uint8_t s_camera_initialized = 0u;
/** @brief 是否正在流式采集 */
static volatile uint8_t s_camera_streaming = 0u;
static volatile uint8_t s_camera_raw_valid = 0u;
static volatile uint8_t s_camera_frame_valid = 0u;
static volatile uint8_t s_camera_latest_index = 0u;
static volatile uint8_t s_camera_pending_index = 0u;
static volatile uint8_t s_camera_pending_valid = 0u;
static volatile uint8_t s_camera_dma_done_index = 0u;
static volatile uint8_t s_camera_dma_done_valid = 0u;
static volatile uint8_t s_camera_frame_boundary_valid = 0u;
static volatile uint8_t s_camera_capture_index = 0u;
static volatile uint8_t s_camera_capture_restart_index = 0u;
static volatile uint8_t s_camera_capture_restart_pending = 0u;
static volatile uint8_t s_camera_frame_sync_locked = 0u;
static volatile uint8_t s_camera_frame_end_pending = 0u;
static volatile uint8_t s_camera_line_overflow = 0u;
static volatile uint8_t s_camera_pub_index = 0u;
static volatile uint8_t s_camera_pending_restart = 0u;
static volatile uint8_t s_camera_msp_error = 0u;
static volatile uint8_t s_camera_pub_refcount[APP_CAMERA_PUB_COUNT] = {0u};
static volatile uint8_t s_camera_wait_vsync_start = 0u;
static volatile uint16_t s_camera_line_index = 0u;
static volatile uint32_t s_camera_frame_seq = 0u;
static volatile uint32_t s_camera_pending_seq = 0u;
static volatile uint32_t s_camera_published_seq = 0u;
static volatile uint32_t s_camera_error_code = 0u;
static volatile uint32_t s_camera_dma_error_code = 0u;
static volatile uint32_t s_camera_restart_count = 0u;
static volatile uint32_t s_camera_restart_fail_count = 0u;
static volatile uint32_t s_camera_init_attempt_count = 0u;
static volatile uint32_t s_camera_publish_count = 0u;
static volatile uint32_t s_camera_publish_drop_count = 0u;
static volatile uint32_t s_camera_dma_done_count = 0u;
static volatile uint32_t s_camera_frame_event_count = 0u;
static volatile uint32_t s_camera_arm_count = 0u;
static volatile uint32_t s_camera_arm_fail_count = 0u;
static volatile uint8_t s_camera_init_stage = APP_CAMERA_INIT_STAGE_IDLE;
static volatile uint8_t s_camera_freeze_publish = 0u;

/** @brief 发布缓冲指针数组（四缓冲） */
static uint16_t *const s_camera_buffers[APP_CAMERA_PUB_COUNT] = {
    (uint16_t *)APP_CAMERA_PUB0_ADDR,
    (uint16_t *)APP_CAMERA_PUB1_ADDR,
    (uint16_t *)APP_CAMERA_PUB2_ADDR,
    (uint16_t *)APP_CAMERA_PUB3_ADDR
};

/** @brief 发布缓冲指针数组（与 s_camera_buffers 相同，用于读者端访问） */
static uint16_t *const s_camera_pub_buffers[APP_CAMERA_PUB_COUNT] = {
    (uint16_t *)APP_CAMERA_PUB0_ADDR,
    (uint16_t *)APP_CAMERA_PUB1_ADDR,
    (uint16_t *)APP_CAMERA_PUB2_ADDR,
    (uint16_t *)APP_CAMERA_PUB3_ADDR
};

/** @brief DMA 行双缓冲（放置在 DMA 可访问段） */
__SECTION_DMA_BUFFER static uint32_t s_camera_line_buffers[2][APP_CAMERA_DMA_LINE_WORDS];

/** @brief 对帧像素采样计算 FNV-1a 哈希，用于快速判断帧是否为有效非全零帧
 * @details 从帧的顶部/中部/底部各取若干采样点（15点均匀分布），计算 FNV-1a 32位哈希。
 * @note    [改进] 全零帧 FNV-1a 哈希非零（因为 FNV prime 乘法），无法检测全黑帧；
 *          可改为附加 sum_check 检测全零输入。
 */
static uint32_t s_camera_sample_hash(const uint16_t *pixels)
{
    /* 15 个采样位置偏移（像素索引），均匀覆盖帧的不同区域
     * 以便快速检测帧是否有效（全零帧与正常帧散列值不同）*/
    static const uint32_t k_sample_offsets[] = {
        0u,                                                                              /* 左上角 */
        ((uint32_t)APP_CAMERA_PREVIEW_W / 4u),                                          /* 顶行 1/4 */
        ((uint32_t)APP_CAMERA_PREVIEW_W / 2u),                                          /* 顶行中点 */
        ((uint32_t)APP_CAMERA_PREVIEW_W * 3u) / 4u,                                     /* 顶行 3/4 */
        (uint32_t)APP_CAMERA_PREVIEW_W - 1u,                                            /* 右上角 */
        ((uint32_t)APP_CAMERA_PREVIEW_H / 4u) * (uint32_t)APP_CAMERA_PREVIEW_W,         /* 1/4 行首 */
        ((uint32_t)APP_CAMERA_PREVIEW_H / 4u) * (uint32_t)APP_CAMERA_PREVIEW_W + ((uint32_t)APP_CAMERA_PREVIEW_W / 2u), /* 1/4 行中 */
        ((uint32_t)APP_CAMERA_PREVIEW_H / 2u) * (uint32_t)APP_CAMERA_PREVIEW_W,         /* 中行首 */
        ((uint32_t)APP_CAMERA_PREVIEW_H / 2u) * (uint32_t)APP_CAMERA_PREVIEW_W + ((uint32_t)APP_CAMERA_PREVIEW_W / 2u), /* 帧中心 */
        ((uint32_t)APP_CAMERA_PREVIEW_H / 2u) * (uint32_t)APP_CAMERA_PREVIEW_W + (uint32_t)APP_CAMERA_PREVIEW_W - 1u,  /* 中行尾 */
        (((uint32_t)APP_CAMERA_PREVIEW_H * 3u) / 4u) * (uint32_t)APP_CAMERA_PREVIEW_W,  /* 3/4 行首 */
        (((uint32_t)APP_CAMERA_PREVIEW_H * 3u) / 4u) * (uint32_t)APP_CAMERA_PREVIEW_W + ((uint32_t)APP_CAMERA_PREVIEW_W / 2u), /* 3/4 行中 */
        (uint32_t)APP_CAMERA_FRAME_PIXELS - (uint32_t)APP_CAMERA_PREVIEW_W,             /* 末行首 */
        (uint32_t)APP_CAMERA_FRAME_PIXELS - ((uint32_t)APP_CAMERA_PREVIEW_W / 2u) - 1u, /* 末行中 */
        (uint32_t)APP_CAMERA_FRAME_PIXELS - 1u                                          /* 右下角 */
    };
    uint32_t hash = 2166136261u; /* FNV-1a 32位初始偏移基 (FNV offset basis = 0x811C9DC5) */
    uint32_t i;

    if (pixels == NULL)
    {
        return 0u; /* NULL 输入返回 0，调用方以 0 作为无效哈希判据 */
    }

    for (i = 0u; i < (sizeof(k_sample_offsets) / sizeof(k_sample_offsets[0])); i++)
    {
        hash ^= (uint32_t)pixels[k_sample_offsets[i]]; /* XOR 当前采样的像素值（RGB565 16位）*/
        hash *= 16777619u;                              /* 乘以 FNV-1a prime，混淆位分布 */
    }

    return hash; /* 返回 32位 FNV-1a 哈希，调用方以非零值判断帧非空 */
}

static void s_camera_service_task(void *argument);
static void s_camera_notify_service_from_isr(void);
static void s_camera_on_dma_bank_complete(uint8_t completed_index);
static void s_camera_on_frame_boundary_isr(void);
static void s_camera_try_queue_frame_from_isr(uint8_t completed_index);
static void s_camera_complete_frame_from_isr(void);
static void s_camera_drop_partial_frame_from_isr(void);
static uint8_t s_camera_select_capture_index_from_isr(uint8_t completed_index, uint8_t *next_index);
static void s_camera_dma_m0_complete_callback(DMA_HandleTypeDef *hdma);
static void s_camera_dma_m1_complete_callback(DMA_HandleTypeDef *hdma);
static void s_camera_dma_error_callback(DMA_HandleTypeDef *hdma);

/** @brief 配置 DCMI 句柄参数（与 OV2640 RGB565 输出时序匹配）*/
static void s_camera_setup_handle(void)
{
    memset(&s_hdcmi, 0, sizeof(s_hdcmi));               /* 清零句柄，防止残留脏状态 */
    s_hdcmi.Instance = DCMI;                            /* 指向 DCMI 外设寄存器基地址 */
    s_hdcmi.Init.SynchroMode     = DCMI_SYNCHRO_HARDWARE;  /* 硬件 VSYNC/HSYNC 同步 */
    s_hdcmi.Init.PCKPolarity     = DCMI_PCKPOLARITY_RISING; /* 像素时钟上升沿采样，与 OV2640 一致 */
    s_hdcmi.Init.VSPolarity      = DCMI_VSPOLARITY_LOW;     /* 场同步低有效（OV2640 默认极性）*/
    s_hdcmi.Init.HSPolarity      = DCMI_HSPOLARITY_LOW;     /* 行同步低有效（OV2640 默认极性）*/
    s_hdcmi.Init.CaptureRate     = DCMI_CR_ALL_FRAME;       /* 采集全部帧，不跳帧 */
    s_hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;   /* 8 位宽数据总线（D0-D7）*/
    s_hdcmi.Init.JPEGMode        = DCMI_JPEG_DISABLE;       /* 禁用 JPEG 模式（使用 RGB565）*/
    s_hdcmi.Init.ByteSelectMode  = DCMI_BSM_ALL;            /* 接收所有字节，不做字节抽选 */
    s_hdcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;           /* 字节采集从奇数字节开始 */
    s_hdcmi.Init.LineSelectMode  = DCMI_LSM_ALL;            /* 接收所有行，不做行抽选 */
    s_hdcmi.Init.LineSelectStart = DCMI_OELS_ODD;           /* 行采集从奇数行开始 */
}

/** @brief 禁用非帧中断（LINE / VSYNC / ERR / OVR） */
static void s_camera_disable_nonframe_interrupts(void)
{
    __HAL_DCMI_DISABLE_IT(&s_hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
}

/** @brief 清除 DMA 和 DCMI 所有挂起标志
 * @details 在（重）启动流之前必须调用，防止旧的中断标志立即触发新 ISR。
 */
static void s_camera_clear_pending_flags(void)
{
    uint32_t dma_flags;

    /* 汇总 DMA 的所有中断标志掩码 */
    dma_flags = __HAL_DMA_GET_TC_FLAG_INDEX(&s_hdma_dcmi) |  /* TC: 传输完成 */
                __HAL_DMA_GET_HT_FLAG_INDEX(&s_hdma_dcmi) |  /* HT: 半传输 */
                __HAL_DMA_GET_TE_FLAG_INDEX(&s_hdma_dcmi) |  /* TE: 传输错误 */
                __HAL_DMA_GET_DME_FLAG_INDEX(&s_hdma_dcmi) | /* DME: 直接模式错误 */
                __HAL_DMA_GET_FE_FLAG_INDEX(&s_hdma_dcmi);   /* FE: FIFO 错误 */
    if (dma_flags != 0u)
    {
        __HAL_DMA_CLEAR_FLAG(&s_hdma_dcmi, dma_flags); /* 批量清除所有 DMA 中断标志 */
    }

    /* 清除 DCMI 的所有原始（Raw）中断标志 */
    __HAL_DCMI_CLEAR_FLAG(&s_hdcmi,
                          DCMI_FLAG_FRAMERI |  /* 帧结束 Raw 标志 */
                          DCMI_FLAG_OVRRI   |  /* 数据溢出 Raw 标志 */
                          DCMI_FLAG_ERRRI   |  /* 同步错误 Raw 标志 */
                          DCMI_FLAG_VSYNCRI |  /* 场同步 Raw 标志 */
                          DCMI_FLAG_LINERI);   /* 行结束 Raw 标志 */
}

/** @brief 复位采集状态机变量 */
static void s_camera_reset_capture_state(uint8_t reset_seq)
{
    s_camera_raw_valid = 0u;
    s_camera_latest_index = 0u;
    s_camera_pending_index = 0u;
    s_camera_pending_valid = 0u;
    s_camera_dma_done_index = 0u;
    s_camera_dma_done_valid = 0u;
    s_camera_frame_boundary_valid = 0u;
    s_camera_capture_index = 0u;
    s_camera_capture_restart_index = 0u;
    s_camera_capture_restart_pending = 0u;
    s_camera_frame_sync_locked = 0u;
    s_camera_frame_end_pending = 0u;
    s_camera_line_overflow = 0u;
    s_camera_wait_vsync_start = 0u;
    s_camera_line_index = 0u;
    s_camera_pending_seq = 0u;
    s_camera_error_code = HAL_DCMI_ERROR_NONE;
    s_camera_dma_error_code = HAL_DMA_ERROR_NONE;
    if (reset_seq != 0u)
    {
        s_camera_frame_seq = 0u;
    }
}

/** @brief 复位发布状态及引用计数 */
static void s_camera_reset_published_state(uint8_t clear_counters)
{
    uint8_t i;

    s_camera_frame_valid = 0u;
    s_camera_pub_index = 0u;
    s_camera_published_seq = 0u;
    for (i = 0u; i < APP_CAMERA_PUB_COUNT; i++)
    {
        s_camera_pub_refcount[i] = 0u;
    }
    if (clear_counters != 0u)
    {
        s_camera_restart_count = 0u;
        s_camera_restart_fail_count = 0u;
        s_camera_publish_count = 0u;
        s_camera_publish_drop_count = 0u;
        s_camera_dma_done_count = 0u;
        s_camera_frame_event_count = 0u;
        s_camera_arm_count = 0u;
        s_camera_arm_fail_count = 0u;
    }
}

/** @brief 强制停止 DCMI/DMA 并清除所有标志
 * @details 无条件关闭 DCMI 采集、禁用所有中断、禁用 DMA Stream 并轮询等待其停止。
 *          用于错误恢复或重启前的安全复位，不依赖 HAL 状态机。
 */
static void s_camera_force_idle(void)
{
    uint32_t wait_count = 1024u; /* DMA Stream 停止超时轮询上限，防止死循环 */

    if ((s_hdcmi.Instance == NULL) || (s_hdma_dcmi.Instance == NULL))
    {
        return; /* 句柄未初始化，直接返回，避免空指针访问 */
    }

    /* 关闭全部 DCMI 中断，防止后续操作触发错误 ISR */
    __HAL_DCMI_DISABLE_IT(&s_hdcmi, DCMI_IT_FRAME | DCMI_IT_LINE | DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
    CLEAR_BIT(s_hdcmi.Instance->CR, DCMI_CR_CAPTURE); /* 清除 CAPTURE 位，停止帧采集（不复位 DCMI EN）*/
    __HAL_DCMI_DISABLE(&s_hdcmi);                     /* 清除 ENABLE 位，完全关闭 DCMI 外设 */

    __HAL_DMA_DISABLE(&s_hdma_dcmi); /* 向 DMA Stream 发送禁用请求（置 CR.EN=0）*/
    /* 轮询等待 DMA Stream EN 位硬件清零
     * [注意] DMA v2 规范要求等待 EN 位实际清零，否则立即重启可能导致数据丢失 */
    while ((((DMA_Stream_TypeDef *)s_hdma_dcmi.Instance)->CR & DMA_SxCR_EN) != 0u)
    {
        if (wait_count == 0u)
        {
            /* [改进] 超时后应记录错误并触发系统复位，当前仅断开轮询 */
            break;
        }
        wait_count--;
    }

    s_camera_clear_pending_flags();

    s_hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
    s_hdcmi.State = HAL_DCMI_STATE_READY;
    s_hdma_dcmi.ErrorCode = HAL_DMA_ERROR_NONE;
    s_hdma_dcmi.State = HAL_DMA_STATE_READY;
    __HAL_UNLOCK(&s_hdcmi);
    __HAL_UNLOCK(&s_hdma_dcmi);
}

/** @brief 启动 DCMI 连续采集流
 * @param  reset_seq     非零时重置帧序列号（首次启动）；0 保留序列号（错误后重启）
 * @param  announce_start 非零时通过 UART 打印启动日志（仅首次启动调用）
 * @details 完整流程：force_idle → 重置状态机 → 强制解锁 HAL 句柄 → 使能 DCMI →
 *          设置连续模式 → 注册 DMA 回调 → 启动 DMA 双缓冲中断模式 →
 *          禁用无用 DMA 中断 → 等待首个 VSYNC 建立帧边界同步。
 */
static HAL_StatusTypeDef s_camera_start_stream(uint8_t reset_seq, uint8_t announce_start)
{
    s_camera_force_idle();                    /* 先无条件停止，确保从干净状态启动 */
    s_camera_reset_capture_state(reset_seq);  /* 清零行索引/帧边界/溢出标志等状态机变量 */

    /* __HAL_UNLOCK 是防御性操作：force_idle 后 HAL 可能残留 LOCKED 状态，强制解锁 */
    __HAL_UNLOCK(&s_hdcmi);
    __HAL_UNLOCK(&s_hdma_dcmi);
    s_camera_arm_count++;                     /* 统计启动尝试次数，供诊断使用 */
    __HAL_DCMI_ENABLE(&s_hdcmi);             /* 使能 DCMI 外设（置 CR.ENABLE 位）*/
    s_hdcmi.State = HAL_DCMI_STATE_BUSY;     /* 手动设置 HAL 状态，绕过 HAL_DCMI_Start_DMA 的前置检查 */
    s_hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
    s_hdcmi.Instance->CR &= ~(DCMI_CR_CM);       /* 清除 Capture Mode 位：0=连续，1=快照 */
    s_hdcmi.Instance->CR |= DCMI_MODE_CONTINUOUS; /* 写入连续采集模式码 */

    /* 注册 DMA 双缓冲回调（DMA 每完成一次行传输触发 M0 或 M1 回调，交替切换）*/
    s_hdma_dcmi.XferCpltCallback   = s_camera_dma_m0_complete_callback; /* Memory 0 完成（偶数次）*/
    s_hdma_dcmi.XferM1CpltCallback = s_camera_dma_m1_complete_callback; /* Memory 1 完成（奇数次）*/
    s_hdma_dcmi.XferErrorCallback  = s_camera_dma_error_callback;       /* DMA 传输错误 */
    s_hdma_dcmi.XferAbortCallback  = NULL;                              /* 不使用中止回调 */

    /* 以中断模式启动 DMA 双缓冲传输：
     *   src  = DCMI->DR（固定地址，禁止递增）
     *   dst0 = s_camera_line_buffers[0]（Memory 0 行缓冲）
     *   dst1 = s_camera_line_buffers[1]（Memory 1 行缓冲，交替接收）
     *   len  = APP_CAMERA_DMA_LINE_WORDS = PREVIEW_W/2 = 160 个 32 位字 = 640 字节/行 */
    if (HAL_DMAEx_MultiBufferStart_IT(&s_hdma_dcmi,
                                      (uint32_t)&s_hdcmi.Instance->DR,
                                      (uint32_t)&s_camera_line_buffers[0][0],
                                      (uint32_t)&s_camera_line_buffers[1][0],
                                      APP_CAMERA_DMA_LINE_WORDS) != HAL_OK)
    {
        s_camera_arm_fail_count++;                          /* 记录启动失败次数 */
        s_camera_streaming = 0u;
        s_camera_error_code    = HAL_DCMI_GetError(&s_hdcmi); /* 保存 DCMI 错误码 */
        s_camera_dma_error_code = s_hdma_dcmi.ErrorCode;      /* 保存 DMA 错误码 */
        s_camera_pending_restart = 1u;                        /* 请求服务任务在下一周期重试 */
        return HAL_ERROR;
    }

    /* 禁用 DME（直接模式错误）和 FE（FIFO 错误）中断：
     * 双缓冲模式下这两类错误频繁但无害，不禁用会造成中断风暴 */
    __HAL_DMA_DISABLE_IT(&s_hdma_dcmi, DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(&s_hdma_dcmi, DMA_IT_FE);
    s_camera_disable_nonframe_interrupts(); /* 关闭非必要 DCMI 中断（LINE/ERR/OVR）*/
    s_camera_capture_index    = 0u;        /* 初始写入发布缓冲 0 */
    s_camera_streaming        = 0u;        /* 等待 VSYNC 后才标记为 streaming（由 ISR 置位）*/
    s_camera_wait_vsync_start = 1u;        /* 标志：等待首个 VSYNC 建立帧边界同步 */
    s_camera_pending_restart  = 0u;        /* 清除重启请求，标志本次启动已尝试 */
    __HAL_DCMI_ENABLE_IT(&s_hdcmi, DCMI_IT_VSYNC); /* 使能 VSYNC 中断用于检测首帧边界 */

    if (announce_start != 0u)
    {
        /* 仅在首次启动时打印：避免错误重启刷屏 */
        printf("CAM: stream started line_words=%lu fb0=0x%08lX fb1=0x%08lX\r\n",
               (unsigned long)APP_CAMERA_DMA_LINE_WORDS,
               (unsigned long)APP_CAMERA_FB0_ADDR,
               (unsigned long)APP_CAMERA_FB1_ADDR);
    }

    return HAL_OK;
}

/** @brief 从 ISR 中通知服务任务 */
static void s_camera_notify_service_from_isr(void)
{
    BaseType_t task_woken = pdFALSE;

    if (s_camera_service_task_handle == NULL)
    {
        return;
    }

    vTaskNotifyGiveFromISR(s_camera_service_task_handle, &task_woken);
    portYIELD_FROM_ISR(task_woken);
}

/** @brief 尝试将已完成的帧加入发布队列（ISR 上下文）
 * @details 更新 pending 帧元数据（索引/序列号/有效标志），
 *          并为下一帧选择空闲采集缓冲。若无空闲缓冲则丢帧并重置同步。
 */
static void s_camera_try_queue_frame_from_isr(uint8_t completed_index)
{
    uint8_t next_index = completed_index;

    /* 检查是否存在未被消费的 pending 帧：若 pending_seq != published_seq 说明服务任务来不及处理，丢帧 */
    if ((s_camera_pending_valid != 0u) &&
        (s_camera_pending_seq != s_camera_published_seq))
    {
        s_camera_publish_drop_count++; /* 统计因来不及发布而丢弃的帧 */
    }

    /* 更新最新帧信息 */
    s_camera_latest_index = (uint8_t)(completed_index % APP_CAMERA_PUB_COUNT); /* 对四缓冲取模 */
    s_camera_frame_seq++;                    /* 单调递增的帧序列号，用于区分新旧帧 */
    s_camera_raw_valid = 1u;                 /* 标记原始帧已就绪（供 AcquireLatestFrame 首帧兜底）*/
    s_camera_pending_index = s_camera_latest_index;
    s_camera_pending_seq   = s_camera_frame_seq;
    s_camera_pending_valid = 1u;             /* 通知服务任务有新帧待发布 */

    /* 为下一帧采集选择一个不与 pending/published/引用计数 冲突的空闲缓冲 */
    if (s_camera_select_capture_index_from_isr(s_camera_latest_index, &next_index) != 0u)
    {
        s_camera_capture_index = next_index; /* 切换采集目标到新空闲缓冲 */
    }
    else
    {
        /* 全部缓冲被占用：强制重置同步，下一帧覆盖当前缓冲（最后手段）*/
        s_camera_publish_drop_count++;
        s_camera_frame_sync_locked = 0u;     /* 强制重新建立帧同步 */
        s_camera_frame_end_pending = 0u;
        s_camera_line_overflow     = 0u;
        s_camera_line_index        = 0u;
        s_camera_capture_index = s_camera_latest_index; /* 复用当前完成缓冲（牺牲一帧）*/
    }

    s_camera_notify_service_from_isr(); /* 唤醒服务任务处理发布 */
}

/** @brief 为下一帧选择空闲的采集缓冲索引（ISR 上下文）
 * @details 优先选满足全部条件的缓冲：非 completed、无引用、非已发布、非 pending。
 *          若找不到，退而求次：仅排除 completed 和有引用计数的缓冲。
 * @return  1=找到，0=全部被占用
 */
static uint8_t s_camera_select_capture_index_from_isr(uint8_t completed_index, uint8_t *next_index)
{
    uint8_t i;

    if (next_index == NULL)
    {
        return 0u; /* 参数校验：输出指针不可为空 */
    }

    /* === 第一轮：严格选择 === */
    for (i = 0u; i < APP_CAMERA_PUB_COUNT; i++)
    {
        if (i == completed_index) continue;               /* 不覆盖刚采集完的缓冲 */
        if (s_camera_pub_refcount[i] != 0u) continue;    /* 消费者持有引用，不可写 */
        if ((s_camera_frame_valid != 0u) && (i == s_camera_pub_index)) continue;    /* 当前发布帧 */
        if ((s_camera_pending_valid != 0u) && (i == s_camera_pending_index)) continue; /* 待发布帧 */

        *next_index = i;
        return 1u; /* 找到最优空闲缓冲 */
    }

    /* === 第二轮：宽松选择（允许覆盖旧发布帧，但不覆盖正在被引用的）=== */
    for (i = 0u; i < APP_CAMERA_PUB_COUNT; i++)
    {
        if (i == completed_index) continue;            /* 仍不覆盖 completed */
        if (s_camera_pub_refcount[i] != 0u) continue; /* 有引用的不可写 */

        *next_index = i;
        return 1u; /* 可能覆盖旧发布帧，但优于全局丢帧 */
    }

    return 0u; /* 所有缓冲均被占用（引用计数锁住），此帧必须丢弃 */
}

/** @brief 完成当前帧采集并提交到发布队列 */
/** @brief 标记当前帧采集完成并尝试发布（ISR 上下文）
 * @details 在两种场景下被调用：
 *   1. VSYNC 下降沿时 line_index 已达到完整行数；
 *   2. 最后一行 DMA 完成且 frame_end_pending 已置位。
 *
 *   函数完成后将 line_index 清零，为下一帧计数做准备。
 */
static void s_camera_complete_frame_from_isr(void)
{
    uint8_t completed_index = s_camera_capture_index; /* 记录刚完成的采集缓冲槽位 */

    s_camera_dma_done_count++;    /* 全局帧完成计数（含丢帧），用于诊断 */
    s_camera_frame_end_pending = 0u; /* 清除帧末尾等待标志：最后一行已就位 */
    s_camera_line_overflow = 0u;  /* 清除行溢出标志：此帧正常 */
    s_camera_line_index = 0u;     /* 行计数复位，下一帧从第 0 行开始 */
    s_camera_try_queue_frame_from_isr(completed_index); /* 尝试将此帧推入 pending 队列 */
}

/** @brief 丢弃不完整帧并重置行状态（ISR 上下文）
 * @details 在以下场景触发：
 *   1. VSYNC 到来时 line_overflow 已置位（DMA 行数超出帧高）；
 *   2. VSYNC 到来时行数不足且不处于倒数第一行等待状态。
 *
 *   丢帧不等于错误，只是本帧质量不满足要求，下一帧重新采集。
 */
static void s_camera_drop_partial_frame_from_isr(void)
{
    s_camera_publish_drop_count++; /* 累计丢帧次数（可在 GetStatus 中读取用于调试）*/
    s_camera_frame_end_pending = 0u; /* 清除末尾等待：此帧已废弃 */
    s_camera_line_overflow = 0u;  /* 清除溢出标志 */
    s_camera_line_index = 0u;     /* 行计数复位，下一帧重新开始 */
    /* [注意] 不自动选择新的 capture 槽位——当前 s_camera_capture_index 保持不变，
     *        下一帧会继续覆盖同一槽位；若需要干净写入则需在外层逻辑中切换。 */
}

/** @brief DMA 行数据完成回调：将行缓冲内容拷贝到帧缓冲对应行（ISR 上下文）
 * @param  completed_index  刚完成的 DMA bank 编号（0 = M0，1 = M1）
 * @details 双缓冲 DMA 以行为单位传输：每传完一行，M0/M1 交替触发对应回调。
 *          本函数负责从行缓冲（D2 SRAM，非缓存）memcpy 到帧缓冲（SDRAM）。
 *
 *          时序要求：
 *          - 16 路 PCM @ 48 kHz，帧率约 30 fps → 每行约 55 µs 预算
 *          - memcpy(640 B) 在 M7 @ 480 MHz 下约 0.5 µs，开销可接受
 *          - [注意] 若 SDRAM 带宽拥挤（LTDC + DMA2D 争用），memcpy 延迟会增加
 */
static void s_camera_on_dma_bank_complete(uint8_t completed_index)
{
    uint16_t *dst;          /* 目标行在帧缓冲中的起始地址 */
    const uint16_t *src;    /* 源：D2 SRAM 行缓冲（由 DMA 填充，非缓存，无需 Cache 操作）*/
    uint16_t line_index;    /* 当前正在拷贝的行号（0-based）*/

    /* 记录刚完成的 bank 编号（供调试用），并置有效标志 */
    s_camera_dma_done_index = (uint8_t)(completed_index & 1u); /* 防止异常值 */
    s_camera_dma_done_valid = 1u; /* 标记本 bank 已就绪（用于 s_camera_on_frame_boundary_isr 逻辑同步）*/

    /* 帧同步未建立时，抛弃行数据（等待第一个 VSYNC 对齐帧边界）*/
    if (s_camera_frame_sync_locked == 0u)
    {
        return; /* 丢弃 VSYNC 前的无效行 */
    }

    line_index = s_camera_line_index; /* 读取当前行号（原子读，单字节不会被中断分裂）*/
    if (line_index >= (uint16_t)APP_CAMERA_PREVIEW_H)
    {
        /* 行号已超出帧高——说明 DMA 速度快于 VSYNC，设置溢出标志 */
        s_camera_line_overflow = 1u; /* [改进] 可额外统计 line_overflow_count 用于测量帧同步质量 */
        return;
    }

    /* 计算 SDRAM 帧缓冲目标行地址（line_index × 宽度 × sizeof(uint16_t)）*/
    src = (const uint16_t *)&s_camera_line_buffers[s_camera_dma_done_index][0];
    dst = &s_camera_buffers[s_camera_capture_index][(uint32_t)line_index * (uint32_t)APP_CAMERA_PREVIEW_W];
    memcpy(dst, src, (size_t)APP_CAMERA_LINE_BYTES); /* 640 B = 320 words，约 0.5 µs */

    s_camera_line_index = (uint16_t)(line_index + 1u); /* 行号递增，下次回调对应下一行 */

    /* 如果上一个 VSYNC 已提前标记"这是最后一行"，则此次拷贝完成意味着整帧到齐 */
    if ((s_camera_frame_end_pending != 0u) &&
        (s_camera_line_index >= (uint16_t)APP_CAMERA_PREVIEW_H))
    {
        s_camera_complete_frame_from_isr(); /* 提交整帧 */
    }
}

/** @brief DCMI 帧边界中断处理（VSYNC 下降沿，代表新帧开始/上一帧结束）
 * @details OV2640 按以下顺序输出信号：
 *          VSYNC↓ → 行数据（HSYNC + PCLK × W） × H → VSYNC↓（下一帧开始）
 *
 *          本函数实现5种状态机路径：
 *          1. 帧同步未建立 → 锁定第一个 VSYNC，初始化行计数
 *          2. 行溢出（line_index > H）→ 丢帧
 *          3. 行到齐（line_index >= H）→ 正常完成
 *          4. 距完成只差最后一行（line_index == H-1）→ 设置 pending，等最后 DMA 完成再 complete
 *          5. 其他（行数不足且无法等待）→ 丢帧
 *
 *          [注意] 此函数在 DCMI_IRQHandler 中执行，必须极短（<10 µs）
 */
static void s_camera_on_frame_boundary_isr(void)
{
    s_camera_frame_event_count++; /* VSYNC 事件计数（用于帧率估算）*/
    s_camera_frame_boundary_valid = 1u; /* 通知服务任务有 VSYNC 事件（备用）*/

    /* === 情况 1：初次 VSYNC —— 建立帧同步锁 === */
    if (s_camera_frame_sync_locked == 0u)
    {
        s_camera_frame_sync_locked = 1u; /* 帧同步已建立，后续 DMA 行数据有效 */
        s_camera_frame_end_pending = 0u; /* 清除残留标志 */
        s_camera_line_overflow = 0u;
        s_camera_line_index = 0u;        /* 从第 0 行重新开始接收 */
        return; /* 此 VSYNC 本身不代表数据完整，不尝试发布 */
    }

    /* === 情况 2：行溢出 —— 上一帧 DMA 行数超限，丢帧 === */
    if (s_camera_line_overflow != 0u)
    {
        s_camera_drop_partial_frame_from_isr(); /* 丢弃并重置行计数 */
        return;
    }

    /* === 情况 3：行到齐 —— 正常完成，立即发布 === */
    if (s_camera_line_index >= (uint16_t)APP_CAMERA_PREVIEW_H)
    {
        s_camera_complete_frame_from_isr(); /* 推入 pending 队列 */
        return;
    }

    /* === 情况 4：还差最后一行 DMA 未完成 —— 置 pending 等最后一次 DMA 回调完成 ===
     * 场景：VSYNC 触发比最后一行 DMA TC 早几个周期（DCMI 和 DMA 竞争仲裁），
     *       此时在 DMA bank complete 回调中检测到 frame_end_pending 后再 complete。*/
    if (s_camera_line_index == (uint16_t)(APP_CAMERA_PREVIEW_H - 1u))
    {
        s_camera_frame_end_pending = 1u; /* 告知 DMA 回调："下一次 complete 即完整帧"*/
        return;
    }

    /* === 情况 5：行数严重不足，不可挽回，直接丢帧 === */
    s_camera_drop_partial_frame_from_isr();
}

/** @brief 摄像头服务 FreeRTOS 任务主体
 * @details 优先级低于音频任务（APP_CAMERA_SERVICE_TASK_PRIO），运行在非 ISR 上下文。
 *          工作流程：
 *          1. 阻塞等待来自 ISR 的任务通知（s_camera_notify_service_from_isr）
 *          2. 超时后自动唤醒（APP_CAMERA_SERVICE_WAKE_MS，防止通知丢失）
 *          3. 调用 UpdatePublishedFrame()：检查 pending 帧、重启逻辑、发布可读缓冲
 *
 *          [注意] LVGL UI 读取 AcquireLatestFrame() 的速度 ≤ 30 fps，
 *                 本任务只负责"搬运"，不做图像处理。
 */
static void s_camera_service_task(void *argument)
{
    (void)argument; /* 任务参数未用，消除编译器警告 */

    for (;;)
    {
        /* 等待 ISR 通知或超时；pdTRUE = 自动清除通知值（one-shot）*/
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_CAMERA_SERVICE_WAKE_MS));
        /* 尝试将最新 pending 帧推进为已发布帧（或处理流重启请求）*/
        (void)App_Camera_UpdatePublishedFrame();
    }
}

/** @brief DMA Memory-0 完成回调（由 HAL_DMA 在 DMA_IRQHandler 中调用）
 * @details 双缓冲 DMA 中 bank 0（Memory-0）传输完一行时触发。
 *          确认来源后路由到统一处理函数 s_camera_on_dma_bank_complete(0)。
 */
static void s_camera_dma_m0_complete_callback(DMA_HandleTypeDef *hdma)
{
    if ((hdma == NULL) || (hdma != &s_hdma_dcmi)) /* 防止其他 DMA 流误触（安全性）*/
    {
        return;
    }

    s_camera_on_dma_bank_complete(0u); /* bank_index=0 → 使用 s_camera_line_buffers[0] */
}

/** @brief DMA Memory-1 完成回调（由 HAL_DMA 在 DMA_IRQHandler 中调用）
 * @details 双缓冲 DMA 中 bank 1（Memory-1）传输完一行时触发。
 *          M0/M1 交替触发，频率 = 行率（约 30 fps × 480 行 = 14400 次/s）。
 *          [注意] ISR 必须在下一行 DMA 完成前退出（约 55 µs 预算），
 *                 目前 memcpy 约 0.5 µs，余量充足。
 */
static void s_camera_dma_m1_complete_callback(DMA_HandleTypeDef *hdma)
{
    if ((hdma == NULL) || (hdma != &s_hdma_dcmi)) /* 防止误路由 */
    {
        return;
    }

    s_camera_on_dma_bank_complete(1u); /* bank_index=1 → 使用 s_camera_line_buffers[1] */
}

/** @brief DMA 传输错误回调（FIFO 错误/传输错误/直接模式错误）
 * @details 错误原因通常为：
 *   - DMA FIFO 读超时（SDRAM 争用导致写延迟）
 *   - 总线仲裁失败（AXI 总线过载）
 *   - 传输地址对齐问题
 *
 *   处理策略：记录错误码后置 pending_restart 标志，
 *             由服务任务在下一次 UpdatePublishedFrame 中调用 s_camera_start_stream
 *             重新启动 DMA，而非在 ISR 中强行重启（避免重入风险）。
 *   [注意] restart_fail_count 持续增加说明 SDRAM/AXI 带宽不足，需查 LTDC/DMA2D 配置。
 */
static void s_camera_dma_error_callback(DMA_HandleTypeDef *hdma)
{
    if ((hdma == NULL) || (hdma != &s_hdma_dcmi)) /* 过滤非本 DMA 流的错误 */
    {
        return;
    }

    s_camera_error_code = HAL_DCMI_ERROR_DMA;     /* 记录 DCMI 层错误码（表示 DMA 故障）*/
    s_camera_dma_error_code = hdma->ErrorCode;    /* 记录 DMA 原始错误位字段（HAL_DMA_ERROR_*）*/
    s_camera_streaming = 0u;                      /* 清除 streaming 标志——当前帧流已失效 */
    s_camera_pending_restart = 1u;               /* 请求服务任务重启采集 */
    s_camera_notify_service_from_isr();          /* 唤醒服务任务（taskYIELD 可能在此触发）*/
}

#endif

/**
 * @brief  获取初始化阶段的可读名称
 * @param  stage 初始化阶段编号
 * @return 阶段名称字符串
 */
const char *App_Camera_InitStageName(uint8_t stage)
{
    switch ((App_CameraInitStage_t)stage)
    {
        case APP_CAMERA_INIT_STAGE_SENSOR_INIT: return "ov_init";
        case APP_CAMERA_INIT_STAGE_PREVIEW_CFG: return "preview";
        case APP_CAMERA_INIT_STAGE_DCMI_INIT:   return "dcmi";
        case APP_CAMERA_INIT_STAGE_READY:       return "ready";
        case APP_CAMERA_INIT_STAGE_IDLE:
        default:
            return "idle";
    }
}

/**
 * @brief  初始化摄像头模块（三阶段初始化）
 * @details 阶段 1（APP_CAMERA_INIT_STAGE_SENSOR_INIT）：
 *            调用 Camera_OV2640_Init() 进行 SCCB I²C 软件序列配置。
 *            失败时输出 mid/pid/段阶 试刳信息并返回。
 *          阶段 2（APP_CAMERA_INIT_STAGE_PREVIEW_CFG）：
 *            发送 RGB565 预览配置（APP_CAMERA_PREVIEW_W × APP_CAMERA_PREVIEW_H）。
 *          阶段 3（APP_CAMERA_INIT_STAGE_DCMI_INIT）：
 *            调用 HAL_DCMI_Init()（重要：这里能触发 MSP init 居导）。
 *
 *          全部提前零清 FB0、FB1（双采集缓冲）和 4 个发布缓冲，
 *          防止首帧读到垃圾数据。
 *
 *          函数具备幂等性：已初始化时直接返回。
 */
void App_Camera_Init(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return; /* 编译开关关闭时，摄像头模块全部被空制化处理 */
#else
    if (s_camera_initialized != 0u)
    {
        return; /* 已初始化，重入无副作用 */
    }

    /* 第一次初始化时完全清除发布状态；重试时保留上一次的帧序列号 */
    if (s_camera_init_attempt_count == 0u)
    {
        s_camera_reset_published_state(1u); /* 1 = 全重置（包括 seq）*/
    }
    else
    {
        s_camera_reset_published_state(0u); /* 0 = 保留 seq，使消费者能识别重启 */
    }

    s_camera_init_attempt_count++;            /* 初始化尝试计数（用于诊断）*/
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_SENSOR_INIT; /* 进入阶段一 */

    /* 阶段 1：OV2640 传感器初始化（内部通过 SCCB 写入寄存器）*/
    if (Camera_OV2640_Init() != 0u)
    {
        Camera_OV2640_Diag_t sensor_diag;
        Camera_OV2640_GetDiag(&sensor_diag); /* 获取传感器试刳信息：mid/pid/阶段/最后读写状态 */
        printf("CAM: OV2640 diag mid=0x%04X pid=0x%04X stage=%u wr=%u rd=%u\r\n",
               (unsigned int)sensor_diag.mid,
               (unsigned int)sensor_diag.pid,
               (unsigned int)sensor_diag.diag_stage,
               (unsigned int)sensor_diag.last_write_status,
               (unsigned int)sensor_diag.last_read_status);
        printf("CAM: OV2640 init failed\r\n"); /* 常见原因：I²C 拉低失败/没有 reset OV2640 */
        return; /* 初始化失败，s_camera_initialized 保持 0 */
    }

    /* 阶段 2：配置 OV2640 输出 RGB565 分辨率预览 */
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_PREVIEW_CFG;
    if (Camera_OV2640_ConfigRgb565Preview((uint16_t)APP_CAMERA_PREVIEW_W,
                                          (uint16_t)APP_CAMERA_PREVIEW_H) != 0u)
    {
        printf("CAM: preview config failed\r\n"); /* 分辨率超出支持范围或寄存器写入失败 */
        return;
    }

    printf("CAM: OV2640 ready preview=%ux%u RGB565\r\n",
           (unsigned int)APP_CAMERA_PREVIEW_W,
           (unsigned int)APP_CAMERA_PREVIEW_H); /* 万年调平行印，确认分辨率 */

    HAL_Delay(20u); /* 等待 OV2640 内部寄存器生效（少于一帧时间）*/

    /* 清除 SDRAM 采集缓冲和发布缓冲，防止首帧显示随机数据 */
    memset((void *)APP_CAMERA_FB0_ADDR, 0, (size_t)(APP_CAMERA_FRAME_BYTES * 2u));          /* FB0 + FB1 全零 */
    memset((void *)APP_CAMERA_PUB0_ADDR, 0, (size_t)(APP_CAMERA_FRAME_BYTES * APP_CAMERA_PUB_COUNT)); /* PUB0–3 全零 */

    /* 阶段 3：初始化 DCMI 外设寄存器和 DMA */
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_DCMI_INIT;
    s_camera_setup_handle(); /* 填充 s_hdcmi 字段（时序、频率、模式）*/
    s_camera_msp_error = 0u; /* 先清除 MSP 错误标志， HAL_DCMI_Init 内部会调用 MSP */
    if ((HAL_DCMI_Init(&s_hdcmi) != HAL_OK) || (s_camera_msp_error != 0u))
    {
        printf("CAM: DCMI init failed\r\n"); /* MSP 内 DMA 申请失败或 GPIO 配置错误 */
        return;
    }

    s_camera_disable_nonframe_interrupts(); /* 关闭 HSYNC/VSYNC 配置 中无用的中断 */
    s_camera_clear_pending_flags();         /* 清除残留中断标志，防止启动时虚假触发 */
    s_camera_reset_capture_state(1u);       /* 重置行计数、带帧计数、捕获索引 */
    s_camera_pending_restart = 0u;          /* 清除任何残留的重启请求 */
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_READY; /* 标记就绪 */
    s_camera_initialized = 1u;              /* 初始化完成——其他函数可以使用 */

    printf("CAM: DCMI ready fb0=0x%08lX fb1=0x%08lX pub0=0x%08lX pub1=0x%08lX pub2=0x%08lX pub3=0x%08lX\r\n",
           (unsigned long)APP_CAMERA_FB0_ADDR,
           (unsigned long)APP_CAMERA_FB1_ADDR,
           (unsigned long)APP_CAMERA_PUB0_ADDR,
           (unsigned long)APP_CAMERA_PUB1_ADDR,
           (unsigned long)APP_CAMERA_PUB2_ADDR,
           (unsigned long)APP_CAMERA_PUB3_ADDR); /* 打印全部缓冲地址以展示 SDRAM 布局 */
#endif
}

/**
 * @brief  启动摄像头连续采集
 * @details 若尚未初始化则先自动调用 App_Camera_Init()。
 *          已在流播中则跳过以避免重复启动。
 *
 *          [\u6ce8\u610f] 貃入条件：已初始化且尚未流播。
 *          [\u6ce8\u610f] s_camera_start_stream() 内部需要 DMA 处于就绪状态，
 *                    若之前异常停止则可能需要调用 Retry()。
 */
void App_Camera_Start(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return; /* 摄像头功能已禁用 */
#else
    if (s_camera_initialized == 0u)
    {
        App_Camera_Init(); /* 懒初始化：第一次 Start() 时自动完成初始化 */
    }
    if ((s_camera_initialized == 0u) || (s_camera_streaming != 0u))
    {
        return; /* 初始化失败或已在流播中，不重复启动 */
    }

    /* clear_irq=1 → 启动前先清除未处理的 TC/HT/ERR 标志 */
    /* reset_state=1 → 行计数、帧计数等全清零 */
    if (s_camera_start_stream(1u, 1u) != HAL_OK)
    {
        printf("CAM: start failed err=0x%08lX\r\n", (unsigned long)s_camera_error_code);
        /* [注意] 启动失败不会自动重试，需上层代码调用 Retry() */
    }
#endif
}

/**
 * @brief  手动重试摄像头初始化与采集
 * @details 应用场景：
 *   - 首次操作平面提供的「摄像头重连」按钮
 *   - 任务检测到连续帧内容全黑或哈希不变时自动采取
 *
 *   步骤：
 *   1. 停止当前流（若有）
 *   2. 叛立强制 DCMI/DMA 进入空闲
 *   3. 反初始化 HAL 底层结构体（memset、DeInit）
 *   4. 重新调用 Init() + Start()
 *
 * @return 0 = 成功，非零 = 失败
 */
uint8_t App_Camera_Retry(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return 1u; /* 功能禁用时始终返回失败（表示无可重试的摄像头）*/
#else
    printf("CAM: manual retry requested\r\n");

    /* 步骤 1：如果流播中，先夬平停止 */
    if (s_camera_streaming != 0u)
    {
        App_Camera_Stop(); /* s_camera_force_idle() */
    }

    /* 步骤 2：反初始化 DCMI */
    if (s_camera_initialized != 0u)
    {
        s_camera_force_idle();            /* 确保 DMA 已研DI */
        (void)HAL_DCMI_DeInit(&s_hdcmi);  /* 釋放 DCMI 回调 注册 */
    }

    /* 步骤 3：将 HAL 结构体全部清零（避免 HAL_Init 护将旧状态）*/
    memset(&s_hdcmi, 0, sizeof(s_hdcmi));       /* DCMI_HandleTypeDef */
    memset(&s_hdma_dcmi, 0, sizeof(s_hdma_dcmi)); /* DMA_HandleTypeDef */

    /* 步骤 4：重置所有状态变量 */
    s_camera_initialized = 0u;
    s_camera_streaming = 0u;
    s_camera_pending_restart = 0u;
    s_camera_msp_error = 0u;
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_IDLE; /* 回到空闲段阶 */
    s_camera_error_code = HAL_DCMI_ERROR_NONE;        /* 清除错误码 */
    s_camera_dma_error_code = HAL_DMA_ERROR_NONE;     /* 清除 DMA 错误码 */
    s_camera_reset_capture_state(1u);  /* 重置行状态模块 */
    s_camera_reset_published_state(0u); /* 保留 seq （不重置），上层代码可识别重启 */

    /* 步骤 5：重新初始化和启动 */
    App_Camera_Init();
    if (s_camera_initialized == 0u)
    {
        return 1u; /* Init 失败，通常是 OV2640 I²C 慢 */
    }

    App_Camera_Start();
    /* 启动后：要么 streaming=1（DMA 运行中），要么处于 wait_vsync_start 状态（等待第一个 VSYNC）*/
    return ((s_camera_streaming != 0u) || (s_camera_wait_vsync_start != 0u)) ? 0u : 1u;
#endif
}

/**
 * @brief  停止摄像头采集
 * @details 调用 s_camera_force_idle() 强制根DI DMA 和 DCMI，
 *          然后清除流播期间的临时状态。
 *          [\u6ce8\u610f] 调用后 s_camera_initialized 依然为 1，可直接调用 Start() 重新启动。
 */
void App_Camera_Stop(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return; /* 功能禁用，无需停止 */
#else
    if (s_camera_streaming == 0u)
    {
        return; /* 已处于停止状态，无需重复停止 */
    }

    s_camera_force_idle(); /* 清除 DMA EN 位，确保 DMA 已唅 */
    s_camera_streaming = 0u;            /* 展示状态：已挂起 */
    s_camera_raw_valid = 0u;            /* 弃置任何未处理的原始数据 */
    s_camera_pending_valid = 0u;        /* 弃置待发布帧 */
    s_camera_dma_done_valid = 0u;       /* 弃置 DMA bank 完成标志 */
    s_camera_frame_boundary_valid = 0u; /* 弃置 VSYNC 事件标志 */
    s_camera_pending_seq = 0u;          /* 清除带帧序列号（防止 Update 带帧时回放旧帧）*/
    s_camera_pending_restart = 0u;      /* 如果未处理的重启请求在队，一并取消 */
    s_camera_capture_restart_pending = 0u; /* 清除单帧捕获重启标志 */
#endif
}

/**
 * @brief  创建摄像头服务 FreeRTOS 任务
 * @details 必须在 FreeRTOS 调度器启动前调用（通常在 main() 的 MX_FREERTOS_Init 柯内）。
 *          具备幂等性：任务已创建时直接返回。
 *
 *          任务参数：
 *          - 名称："Cam_Service"
 *          - 栈大小：APP_CAMERA_SERVICE_TASK_STACK_WORDS
 *          - 优先级：APP_CAMERA_SERVICE_TASK_PRIO
 *          - 保存句柄：s_camera_service_task_handle（用于 ISR 通知）
 *
 *          [\u6ce8\u610f] configASSERT 在 xTaskCreate 失败时会触发调试断点，
 *                    通常是堆内存不足。
 */
void App_Camera_TaskInit(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return; /* 摄像头禁用时不创建任务 */
#else
    BaseType_t task_ok;

    if (s_camera_service_task_handle != NULL)
    {
        return; /* 已创建，防止重入 */
    }

    task_ok = xTaskCreate(s_camera_service_task,        /* 任务函数 */
                          "Cam_Service",               /* 任务名称（调试器颜示）*/
                          APP_CAMERA_SERVICE_TASK_STACK_WORDS, /* 栈大小（单位: word）*/
                          NULL,                              /* pvParameters: 未用 */
                          APP_CAMERA_SERVICE_TASK_PRIO,    /* 优先级 */
                          &s_camera_service_task_handle);  /* 输出句柄（用于 ISR xTaskNotifyGive）*/
    configASSERT(task_ok == pdPASS); /* FreeRTOS API 失败时进入调试 BKPT */
#endif
}

/**
 * @brief  将最新原始帧发布到可读缓冲（非 ISR 上下文）
 * @details 工作流程：
 *   1. 樼查 s_camera_pending_restart — 若置位则先重启 DMA 流
 *   2. 临界区读取 pending_valid/index/seq
 *   3. 若 pending 有效且 seq 新于已发布，则屁 FNV-1a 哈希校验：
 *      - 全零帧（hash==0）丢弃
 *      - 通过则将 pub_index 指向新帧并更新 published_seq
 *   4. 清除 pending_valid（防止重复发布）
 *
 * @return 1 = 成功发布新帧，0 = 无新帧
 *
 * @note   必须在任务上下文中调用，不得在 ISR 中调用。
 */
uint8_t App_Camera_UpdatePublishedFrame(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return 0u; /* 摄像头禁用，直接返回无新帧 */
#else
    uint32_t primask;       /* 保存中断状态，用于可重入临界防护 */
    uint32_t raw_seq;       /* 临界区内拷贝的 pending seq */
    uint32_t published_seq; /* 临界区内拷贝的已发布 seq */
    uint8_t raw_valid;      /* pending_valid 快照 */
    uint8_t raw_index;      /* pending_index 快照 */
    uint8_t current_pub_valid; /* 已发布帧有效标志快照 */
    uint8_t target_index;   /* 将要发布的缓冲槽位 */
    uint8_t published = 0u; /* 返回值：初始为未发布 */

    if (s_camera_initialized == 0u)
    {
        return 0u; /* 未初始化则没有有效帧 */
    }

    /* --- 处理 DMA 错误重启请求 --- */
    if (s_camera_pending_restart != 0u)
    {
        /* clear_irq=0：不清除中断（保留现有状态）； reset_state=0：保留 seq */
        if (s_camera_start_stream(0u, 0u) == HAL_OK)
        {
            s_camera_restart_count++; /* 自动重启次数计数 */
        }
        else
        {
            s_camera_restart_fail_count++; /* 重启失败——带宽不足或 DCMI 异常 */
            return 0u; /* 失败时不返回帧，等下一个 tick */
        }
    }

    /* --- 临界区：获取 pending 帧信息 --- */
    primask = __get_PRIMASK(); /* 读取当前 PRIMASK（不能假设 ISR 已关）*/
    __disable_irq();           /* 关中断（单核 M7 上等安事）*/
    raw_valid = s_camera_pending_valid;     /* ISR 写入的 pending 有效标志 */
    raw_index = s_camera_pending_index;     /* ISR 写入的 pending 缓冲槽位 */
    raw_seq = s_camera_pending_seq;         /* ISR 写入的帧序列号 */
    published_seq = s_camera_published_seq; /* 已发布帧的序列号（用于去重复）*/
    current_pub_valid = s_camera_frame_valid; /* 已发布帧是否有效 */
    if (primask == 0u)
    {
        __enable_irq(); /* 仅在进入临界区前中断已开时才脚开 */
    }

    /* --- 无新帧或重复帧 --- */
    if ((raw_valid == 0u) || (raw_seq == published_seq))
    {
        return 0u; /* pending 无效，或常序列号跟已发布一致（重复发布无意义）*/
    }

    /* --- 尝试发布（需要快照枚举 + 否寻子）--- */
    if ((s_camera_freeze_publish == 0u) || (current_pub_valid == 0u))
    {
        /* 冠止冐：未密封（正常运行）或者还没有任何已发布帧时强制发布一帧 */
        uint8_t can_publish = 0u;

        /* FNV-1a 哈希校验：否必 帧内容非全零 */
        if (s_camera_sample_hash(s_camera_buffers[raw_index % APP_CAMERA_PUB_COUNT]) == 0u)
        {
            s_camera_publish_drop_count++; /* 全零帧丢弃（摄像头未输出或 DMA 找错源）*/
        }
        else
        {
            target_index = (uint8_t)(raw_index % APP_CAMERA_PUB_COUNT); /* 4 槽位循环 */
            can_publish = 1u; /* 帧内容有效 */
        }

        if (can_publish != 0u)
        {
            /* 临界区：原子更新 pub_index + published_seq */
            primask = __get_PRIMASK();
            __disable_irq();
            s_camera_pub_index = target_index;  /* 将 UI 指向新帧 */
            s_camera_published_seq = raw_seq;   /* 更新已发布序列号 */
            s_camera_frame_valid = 1u;          /* 标记已有有效帧 */
            s_camera_publish_count++;           /* 发布成功计数 */
            published = 1u;                     /* 函数返回值 = 有新帧 */
            /* 如果 ISR 没有在此期间覆写 pending，则清除标志以将容接下一帧 */
            if ((s_camera_pending_valid != 0u) && (s_camera_pending_seq == raw_seq))
            {
                s_camera_pending_valid = 0u; /* 巾降随意：已发布的 pending 可以清掉 */
            }
            if (primask == 0u)
            {
                __enable_irq();
            }
        }
    }

    /* --- 收尾：无论是否发布成功，都清除本轮 pending --- */
    primask = __get_PRIMASK();
    __disable_irq();
    if ((s_camera_pending_valid != 0u) && (s_camera_pending_seq == raw_seq))
    {
        s_camera_pending_valid = 0u; /* 只清除匹配本轮 seq 的 pending，避免将 ISR 新更新的 pending 误清 */
    }
    if (primask == 0u)
    {
        __enable_irq();
    }

    return published; /* 1 = 新帧已就绪，0 = 未发布 */
#endif
}

/**
 * @brief  设置发布冻结状态
 * @param  enable 非零 = 冻结（不更新发布帧），0 = 恢复
 */
void App_Camera_SetFreeze(uint8_t enable)
{
#if (APP_CAMERA_ENABLE != 0u)
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_camera_freeze_publish = (enable != 0u) ? 1u : 0u;
    if (primask == 0u)
    {
        __enable_irq();
    }
#else
    (void)enable;
#endif
}

/**
 * @brief  获取发布冻结状态
 * @return 1 = 已冻结，0 = 正常
 */
uint8_t App_Camera_GetFreeze(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return 0u;
#else
    return s_camera_freeze_publish;
#endif
}

/**
 * @brief  获取最新发布帧并增加引用计数
 * @param  frame 接收帧信息的结构体指针
 * @return 1 = 成功，0 = 无有效帧
 * @details 使用后必须调用 App_Camera_ReleaseFrame() 释放引用。
 */
uint8_t App_Camera_AcquireLatestFrame(App_CameraFrame_t *frame)
{
#if (APP_CAMERA_ENABLE == 0u)
    (void)frame;
    return 0u;
#else
    uint32_t primask;
    uint8_t index;

    if (frame == NULL)
    {
        return 0u;
    }

    memset(frame, 0, sizeof(*frame));

    /* 摄像头发布以独立服务任务为主。
     * 这里仅保留“首帧兜底”：如果 raw 已经在增长但 published 还没建立，
     * 则由当前调用者补一次发布，避免因为调度时序问题导致预览长期黑屏。 */
    if ((s_camera_frame_valid == 0u) && (s_camera_raw_valid != 0u))
    {
        (void)App_Camera_UpdatePublishedFrame();
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_camera_frame_valid == 0u)
    {
        if (primask == 0u)
        {
            __enable_irq();
        }
        return 0u;
    }

    index = s_camera_pub_index;
    if (s_camera_pub_refcount[index] < 0xFFu)
    {
        s_camera_pub_refcount[index]++;
    }

    frame->pixels = s_camera_pub_buffers[index];
    frame->width = (uint16_t)APP_CAMERA_PREVIEW_W;
    frame->height = (uint16_t)APP_CAMERA_PREVIEW_H;
    frame->stride = (uint16_t)APP_CAMERA_PREVIEW_W;
    frame->seq = s_camera_published_seq;
    frame->buffer_id = index;
    frame->valid = 1u;

    if (primask == 0u)
    {
        __enable_irq();
    }

    return 1u;
#endif
}

/**
 * @brief  释放通过 AcquireLatestFrame 获取的帧引用
 * @param  frame 要释放的帧结构体指针
 * @details 引用计数减 1；减到 0 时 ISR 可以重新使用该槽位。
 *          引用-释放必须配对，换帧时先 Release 旧帧再 Acquire 新帧。
 *          [注意] 多次 Release 同一帧不会崩溃（有 >0 保护），但一般不应出现。
 */
void App_Camera_ReleaseFrame(const App_CameraFrame_t *frame)
{
#if (APP_CAMERA_ENABLE != 0u)
    uint32_t primask;
    uint8_t index;

    /* 参数投识成际：必须有效帧且 buffer_id 合法 */
    if ((frame == NULL) || (frame->valid == 0u) || (frame->pixels == NULL) || (frame->buffer_id >= APP_CAMERA_PUB_COUNT))
    {
        return; /* 无效帧，跳过（防止双释放或无效调用）*/
    }

    index = frame->buffer_id; /* 从帧元数据中取得槽位索引 */
    primask = __get_PRIMASK();
    __disable_irq(); /* 屏蔽 ISR 修改引用计数 */
    if (s_camera_pub_refcount[index] > 0u)
    {
        s_camera_pub_refcount[index]--; /* 引用计数-1，减到 0 时槽位解除锁 */
    }
    if (primask == 0u)
    {
        __enable_irq();
    }
#else
    (void)frame; /* 功能禁用时消除警告 */
#endif
}

/**
 * @brief  获取最新发布帧（无引用计数，仅拷贝元数据）
 * @param  frame 接收帧信息的结构体指针
 * @details 与 Acquire 的区别：不增加引用计数，直接返回帧元数据。
 *          适合仅读取帧序列号/宽高等元信息时使用。
 *
 *          [注意] 调用后即刻读完像素指针，ISR 可能在任何时就覆写该帧！
 *                    如果需长时间读内容，应使用 Acquire/Release。
 */
void App_Camera_GetLatestFrame(App_CameraFrame_t *frame)
{
    uint32_t primask;

    if (frame == NULL)
    {
        return; /* 参数校验 */
    }

    memset(frame, 0, sizeof(*frame)); /* 安全初始化，失败时 valid=0 出口 */

#if (APP_CAMERA_ENABLE == 0u)
    return; /* 功能禁用，返回空帧 */
#else
    /* 临界区只读（不增引用计数）*/
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_camera_frame_valid != 0u)
    {
        frame->pixels = s_camera_pub_buffers[s_camera_pub_index]; /* 指针，不拷贝像素 */
        frame->width = (uint16_t)APP_CAMERA_PREVIEW_W;
        frame->height = (uint16_t)APP_CAMERA_PREVIEW_H;
        frame->stride = (uint16_t)APP_CAMERA_PREVIEW_W;
        frame->seq = s_camera_published_seq;
        frame->buffer_id = s_camera_pub_index;
        frame->valid = 1u;
    }
    if (primask == 0u)
    {
        __enable_irq();
    }
#endif
}

/**
 * @brief  获取摄像头详细运行状态（诊断信息）
 * @param  status 接收状态信息的结构体指针
 * @details 整合了临界区内的所有运行统计量、OV2640 传感器试刳信息以及
 *          帧样本像素值。适用于：
 *   - 调试界面 CLI 命令 "caminfo"
 *   - 异常恢复逻辑中判断采集状态
 *   - 个人周期测试
 *
 * @note   OV2640_GetDiag() 通过 SCCB 读寄存器，耐时较長，不应在高频路径中调用。
 */
void App_Camera_GetStatus(App_CameraStatus_t *status)
{
    uint32_t primask;
    Camera_OV2640_Diag_t sensor_diag;

    if (status == NULL)
    {
        return; /* 输出指针必须有效 */
    }

    memset(status, 0, sizeof(*status)); /* 默认全零，未就绪的字段返回 0 */

#if (APP_CAMERA_ENABLE == 0u)
    return; /* 功能禁用，返回全零状态 */
#else
    /* 临界区：一次性拷贝所有 volatile 运行状态 */
    primask = __get_PRIMASK();
    __disable_irq();
    status->initialized = s_camera_initialized;       /* 1=已初始化 */
    status->streaming = s_camera_streaming;           /* 1=DMA 运行中 */
    status->valid = s_camera_frame_valid;             /* 1=至少有一帧已发布 */
    status->latest_index = s_camera_latest_index;     /* 最新采集索引 */
    status->published_index = s_camera_pub_index;     /* 已发布缓冲索引 */
    status->frame_seq = s_camera_frame_seq;           /* 采集帧累计序号 */
    status->published_seq = s_camera_published_seq;   /* 已发布帧序列号 */
    status->error_code = s_camera_error_code;         /* DCMI 错误码（HAL_DCMI_ERROR_*）*/
    status->dma_error_code = s_camera_dma_error_code; /* DMA 错误码（HAL_DMA_ERROR_*）*/
    status->restart_count = s_camera_restart_count;   /* DMA 自动重启核数 */
    status->restart_fail_count = s_camera_restart_fail_count; /* 重启失败次数 */
    status->init_attempt_count = s_camera_init_attempt_count; /* 初始化尝试次数 */
    status->publish_count = s_camera_publish_count;   /* 成功发布帧数 */
    status->publish_drop_count = s_camera_publish_drop_count; /* 丢帧数（包括全零帧和不完整帧）*/
    status->dma_done_count = s_camera_dma_done_count; /* DMA 行完成计数 */
    status->frame_event_count = s_camera_frame_event_count; /* VSYNC 事件数 ≈ 帧率×时间 */
    status->arm_count = s_camera_arm_count;           /* DMA 重新武装计数 */
    status->arm_fail_count = s_camera_arm_fail_count; /* DMA 重新武装失败次数 */
    status->init_stage = s_camera_init_stage;         /* 当前初始化阶段（使用 InitStageName 转字符串）*/
    status->pending_restart = s_camera_pending_restart; /* 是否有待处理的 DMA 重启请求 */
    status->freeze_enabled = s_camera_freeze_publish; /* 1=已密封帧，截图模式下开头 */
    status->dcmi_state = (uint32_t)s_hdcmi.State;     /* HAL DCMI 状态机状态 */
    status->dma_state = (uint32_t)s_hdma_dcmi.State;  /* HAL DMA 状态机状态 */
    if (primask == 0u)
    {
        __enable_irq();
    }

    /* ISR 临界区外：通过 SCCB 读取传感器试刳信息（积极读 mid/pid）*/
    Camera_OV2640_GetDiag(&sensor_diag);
    status->sensor_mid = sensor_diag.mid;               /* 制造商 ID，预期 0x7FA2 */
    status->sensor_pid = sensor_diag.pid;               /* 产品 ID，预期 0x2642（OV2640）*/
    status->sensor_diag_stage = sensor_diag.diag_stage; /* 试刳时第几步失败 */
    status->sensor_last_write_status = sensor_diag.last_write_status; /* 最后 SCCB 写状态 */
    status->sensor_last_read_status = sensor_diag.last_read_status;   /* 最后 SCCB 读状态 */
    {
        /* 取 3 个代表性像素样本：左上角/中心/右下角 —— 快速评估帧内容 */
        const uint16_t *raw_pixels = s_camera_buffers[status->latest_index % APP_CAMERA_PUB_COUNT];
        const uint16_t *pub_pixels = s_camera_pub_buffers[status->published_index % APP_CAMERA_PUB_COUNT];
        uint32_t mid_idx = ((uint32_t)APP_CAMERA_PREVIEW_H / 2u) * (uint32_t)APP_CAMERA_PREVIEW_W
                         + ((uint32_t)APP_CAMERA_PREVIEW_W / 2u); /* 帧中心像素索引 */

        status->raw_sample0 = raw_pixels[0];                          /* 左上角 */
        status->raw_sample1 = raw_pixels[mid_idx];                    /* 中心 */
        status->raw_sample2 = raw_pixels[APP_CAMERA_FRAME_PIXELS - 1u]; /* 右下角 */
        status->pub_sample0 = pub_pixels[0];                          /* 已发布帧左上角 */
        status->pub_sample1 = pub_pixels[mid_idx];                    /* 已发布帧中心 */
        status->pub_sample2 = pub_pixels[APP_CAMERA_FRAME_PIXELS - 1u]; /* 已发布帧右下角 */
        status->raw_hash = s_camera_sample_hash(raw_pixels);          /* 原始帧 FNV 哈希 */
        status->pub_hash = s_camera_sample_hash(pub_pixels);          /* 已发布帧 FNV 哈希 */
    }
#endif
}

/**
 * @brief  DCMI 中断服务入口（从 stm32h7xx_it.c 调用）
 * @details HAL_DCMI_IRQHandler() 内部会调用 SYNC/VSYNC/FRAME 回调。
 *          [\u6ce8\u610f] 必须确认 HAL_DCMI_FRAME_EVENT_CB_ID 注册了
 *                    s_camera_on_frame_boundary_isr，否则 VSYNC 不会被处理。
 */
void App_Camera_DCMI_IRQHandler(void)
{
#if (APP_CAMERA_ENABLE != 0u)
    if (s_camera_initialized != 0u) /* 不处理初始化前的虚假中断 */
    {
        HAL_DCMI_IRQHandler(&s_hdcmi); /* 派发 DCMI 中断，内部判断VSYNC/HSYNC */
    }
#endif
}

/**
 * @brief  DCMI 关联 DMA 中断服务入口
 * @details HAL_DMA_IRQHandler() 内部判断 TC/HT/TE/DME/FE,
 *          并调用对应的 M0/M1 Complete callback。
 * @note   DMA1_Stream1_IRQHandler 在 stm32h7xx_it.c 居导这里。
 */
void App_Camera_DMA_IRQHandler(void)
{
#if (APP_CAMERA_ENABLE != 0u)
    if (s_camera_initialized != 0u) /* 初始化前不调用（s_hdma_dcmi 可能未就绪）*/
    {
        HAL_DMA_IRQHandler(&s_hdma_dcmi); /* 居导到 m0/m1/error callback */
    }
#endif
}

#if (APP_CAMERA_ENABLE != 0u)
/**
 * @brief  DCMI MSP 初始化回调（HAL 内部调用）
 * @param  hdcmi DCMI 句柄指针
 * @details 配置 DCMI 所需的全部外设资源：
 *   - 时钟：RCC 使能 DCMI + DMA1 + 各 GPIO 时钟
 *   - GPIO：
 *       PA4 = DCMI_HSYNC, PA6 = DCMI_PIXCK
 *       PB7 = DCMI_VSYNC, PB8 = DCMI_D6, PB9 = DCMI_D7
 *       PC6 = DCMI_D0, PC7 = DCMI_D1, PC8 = DCMI_D2, PC9 = DCMI_D3, PC11 = DCMI_D4
 *       PD3 = DCMI_D5
 *       PC4 = 摄像头 RESET´´（主动拉低喐）
 *   - DMA1_Stream1 配置：
 *       DMA_REQUEST_DCMI → D2 SRAM 行缓冲（非缓存区）
 *       半字目标对齐（RGB565），全字外设对齐（DCMI->DR）
 *       循环模式（DMA_CIRCULAR），FIFO 中层门限
 *   - NVIC：
 *       DMA1_Stream1_IRQn 优先级 6-1（低于 FreeRTOS 任务临界 5）
 *       DCMI_IRQn 优先级 6-0
 *
 * [注意] MSP 由 HAL_DCMI_Init() 内部调用，不应手动调用。
 */
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
    GPIO_InitTypeDef gpio_init = {0}; /* 零初始化，防止未使用字段残留随机值 */

    if ((hdcmi == NULL) || (hdcmi->Instance != DCMI)) /* 过滤错误句柄 */
    {
        return;
    }

    __HAL_RCC_DCMI_CLK_ENABLE();   /* 使能 DCMI 外设时钟 */
    __HAL_RCC_DMA1_CLK_ENABLE();   /* 使能 DMA1 控制器时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();  /* HSYNC/PIXCK 引脚 */
    __HAL_RCC_GPIOB_CLK_ENABLE();  /* VSYNC/D6/D7 引脚 */
    __HAL_RCC_GPIOC_CLK_ENABLE();  /* D0–4/RESET 引脚 */
    __HAL_RCC_GPIOD_CLK_ENABLE();  /* D5 引脚 */

    /* 配置 DCMI 数据/控制引脚（复用 AF 模式）*/
    gpio_init.Mode = GPIO_MODE_AF_PP;          /* 强注 AF，推戋输出 */
    gpio_init.Pull = GPIO_PULLUP;              /* 上拉干扰源 */
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH; /* 第三层速度，适配 24 MHz PCLK */
    gpio_init.Alternate = GPIO_AF13_DCMI;      /* 所有 DCMI 引脚 AF 选择 AF13 */

    gpio_init.Pin = GPIO_PIN_4 | GPIO_PIN_6;  /* PA4=DCMI_HSYNC, PA6=DCMI_PIXCK */
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9; /* PB7=DCMI_VSYNC, PB8=D6, PB9=D7 */
    HAL_GPIO_Init(GPIOB, &gpio_init);

    gpio_init.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11; /* PC6=D0,PC7=D1,PC8=D2,PC9=D3,PC11=D4 */
    HAL_GPIO_Init(GPIOC, &gpio_init);

    gpio_init.Pin = GPIO_PIN_3; /* PD3=DCMI_D5 */
    HAL_GPIO_Init(GPIOD, &gpio_init);

    /* PC4 = OV2640 RESET'（低电平复位）—— 配置为推挽输出并立即拉低 */
    gpio_init.Pin = GPIO_PIN_4;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;      /* 推挽输出 */
    gpio_init.Pull = GPIO_NOPULL;              /* 无需外部上/下拉 */
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;     /* 复位信号无高速需求 */
    HAL_GPIO_Init(GPIOC, &gpio_init);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET); /* RESET' = 0 → 传感器复位 */

    /* === DMA1_Stream1 配置 === */
    memset(&s_hdma_dcmi, 0, sizeof(s_hdma_dcmi)); /* 清零，防止旧值干扰 */
    s_hdma_dcmi.Instance = DMA1_Stream1;          /* 使用 DMA1 数据流 1 */
    s_hdma_dcmi.Init.Request = DMA_REQUEST_DCMI;  /* 触发源：DCMI 帧数据就绪 */
    s_hdma_dcmi.Init.Direction = DMA_PERIPH_TO_MEMORY; /* 外设→内存 */
    s_hdma_dcmi.Init.PeriphInc = DMA_PINC_DISABLE; /* 外设地址不递增（DCMI->DR 固定）*/
    s_hdma_dcmi.Init.MemInc = DMA_MINC_ENABLE;     /* 内存地址递增（逐字节填充行缓冲）*/
    s_hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD; /* DCMI->DR 为 32-bit */
    s_hdma_dcmi.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD; /* 目标行缓冲 uint16_t */
    s_hdma_dcmi.Init.Mode = DMA_CIRCULAR;          /* 循环模式：M0/M1 自动切换 */
    s_hdma_dcmi.Init.Priority = DMA_PRIORITY_HIGH; /* 高优先级，优于 SDRAM DMA2 */
    s_hdma_dcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE; /* 开启 FIFO 减少总线占用 */
    s_hdma_dcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL; /* FIFO 半满触发 */
    s_hdma_dcmi.Init.MemBurst = DMA_MBURST_SINGLE;   /* 内存突发 = 单次（行缓冲在 D2 SRAM 连续）*/
    s_hdma_dcmi.Init.PeriphBurst = DMA_PBURST_SINGLE; /* 外设突发 = 单次 */

    __HAL_LINKDMA(hdcmi, DMA_Handle, s_hdma_dcmi); /* 将 DMA 句柄与 DCMI 关联 */
    if (HAL_DMA_Init(&s_hdma_dcmi) != HAL_OK)
    {
        s_camera_msp_error = 1u; /* 记录 MSP 错误，App_Camera_Init 会检查此标志 */
        return;
    }

    /* NVIC 优先级设置 */
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6u, 1u); /* 6.1：低于 FreeRTOS 任务临界值 5 */
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);            /* 使能 DMA 中断 */
    HAL_NVIC_SetPriority(DCMI_IRQn, 6u, 0u);         /* 6.0：与 DMA 同组但优先 */
    HAL_NVIC_EnableIRQ(DCMI_IRQn);                   /* 使能 DCMI 中断 */
}

/**
 * @brief  DCMI MSP 反初始化回调
 * @param  hdcmi DCMI 句柄指针
 * @details 禁用并反初始化 DMA 和 DCMI 时钟。
 *          [\u6ce8\u610f] GPIO 未反初始化（DeInit GPIO），如需防弹兼容应觃倒；
 *                    当前实现中 GPIO 不重用，可考虑宼释。
 */
void HAL_DCMI_MspDeInit(DCMI_HandleTypeDef *hdcmi)
{
    if ((hdcmi == NULL) || (hdcmi->Instance != DCMI))
    {
        return;
    }

    HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn); /* 先禁用 DMA 中断 */
    HAL_NVIC_DisableIRQ(DCMI_IRQn);         /* 再禁用 DCMI 中断 */
    HAL_DMA_DeInit(&s_hdma_dcmi);           /* 释放 DMA 垂偃 */
    __HAL_RCC_DCMI_CLK_DISABLE();           /* 关闭 DCMI 时钟（降低功耗）*/
}

/**
 * @brief  DCMI 帧事件回调（HAL 内部调用）
 * @param  hdcmi DCMI 句柄指针
 * @details DCMI 在 VSYNC 边沿（帧尾）自动触发 FRAME 事件。
 *          HAL 调用此回调后 HAL 任务会关闭 FRAME 中断，
 *          所以这里必须重新开启。
 */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    if ((hdcmi == NULL) || (hdcmi != &s_hdcmi))
    {
        return;
    }

    s_camera_on_frame_boundary_isr();        /* 帧边界状态机 */
    __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME); /* 重新开启 FRAME 中断（HAL 内会禁用）*/
}

/**
 * @brief  DCMI VSYNC 事件回调，用于首帧同步
 * @param  hdcmi DCMI 句柄指针
 * @details 设计考虑：
 *   摄像头输出第一个 VSYNC 前， DCMI CR.CAPTURE 不应开启以避免捕获不完整首帧。
 *   s_camera_start_stream 先等 VSYNC Callback 哤常0x，确认同步后再开启采集。
 *
 *   步骤：
 *   1. 清除 wait_vsync_start 标志
 *   2. 设置 streaming=1
 *   3. 关闭 VSYNC 中断，开启 FRAME 中断
 *   4. 设置 DCMI_CR_CAPTURE 开始捕获（软件强制开启）
 */
void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    if ((hdcmi == NULL) || (hdcmi != &s_hdcmi))
    {
        return;
    }

    if (s_camera_wait_vsync_start != 0u)
    {
        s_camera_wait_vsync_start = 0u;              /* 首个 VSYNC 已收到，无需继续等待 */
        s_camera_streaming = 1u;                     /* 标记流播开始（有效数据从下一帧起）*/
        __HAL_DCMI_DISABLE_IT(hdcmi, DCMI_IT_VSYNC); /* 不再需要 VSYNC 中断 */
        __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME);  /* 换成帧完成中断 */
        SET_BIT(hdcmi->Instance->CR, DCMI_CR_CAPTURE); /* 软件启动捕获（从下一帧开始）*/
    }
}

/**
 * @brief  DCMI 错误回调，处理捕获失败并触发重启
 * @param  hdcmi DCMI 句柄指针
 * @details 对 FIFO 错误（HAL_DMA_ERROR_FE）特殊处理：
 *          双缓冲 DMA 模式下 FIFO 错误为正常现象，不影响数据，
 *          只清除错误码后返回，不触发重启。
 *          其他错误（DMA 传输错误/FIFO 总线错误）则设置 pending_restart。
 */
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
    uint32_t error_code;     /* DCMI 错误码 */
    uint32_t dma_error_code; /* DMA 错误码 */

    if ((hdcmi == NULL) || (hdcmi != &s_hdcmi))
    {
        return;
    }

    error_code = HAL_DCMI_GetError(hdcmi);      /* 读取 DCMI 错误状态 */
    dma_error_code = s_hdma_dcmi.ErrorCode;     /* 读取 DMA 错误寄存器 */
    s_camera_error_code = error_code;           /* 全局保存，供 GetStatus 查询 */
    s_camera_dma_error_code = dma_error_code;   /* 全局保存 DMA 错误信息 */

    /* FIFO 错误在双缓冲模式下是正常的，不触发重启 */
    if ((error_code == HAL_DCMI_ERROR_NONE) && (dma_error_code == HAL_DMA_ERROR_FE))
    {
        s_hdma_dcmi.ErrorCode = HAL_DMA_ERROR_NONE; /* 平执错误状态，恢复正常运行 */
        return;
    }

    /* 其他错误：建议通过服务任务安全重启 */
    s_camera_streaming = 0u;         /* 立即标记流已中途 */
    s_camera_raw_valid = 0u;         /* 原始帧数据不再可信 */
    s_camera_pending_restart = 1u;   /* 请求服务任务重启采集 */
    s_camera_notify_service_from_isr(); /* 唤醒服务任务 */
}
#endif
