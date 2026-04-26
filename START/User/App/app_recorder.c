/**
 * @file    app_recorder.c
 * @brief   WAV 录音实现
 * @details WAV 格式: PCM, 16-bit, 48kHz
 *          - MONO 模式: 1ch, ~96 KB/s
 *          - RAW16 模式: 16ch, ~1536 KB/s
 *
 *          帧处理流程:
 *          Feed() 在音频任务中将帧数据转换为 int16_t 并写入环形缓冲。
 *          FlushPending() 在 Storage_Task 中将缓冲数据通过 FIL 写入 SD 卡。
 *          Stop() 由 Storage_Task 调用, 仅设置状态。
 */
#include "app_recorder.h"
#include "ff.h"

#include <string.h>

/* ========== WAV 文件头结构 ========== */

#pragma pack(push, 1)
typedef struct {
    /* RIFF Chunk */
    uint8_t  riff_id[4];       /**< "RIFF" */
    uint32_t riff_size;        /**< 文件总大小 - 8 */
    uint8_t  wave_id[4];       /**< "WAVE" */
    /* fmt sub-chunk */
    uint8_t  fmt_id[4];        /**< "fmt " */
    uint32_t fmt_size;         /**< 16 (PCM) */
    uint16_t audio_format;     /**< 1 = PCM */
    uint16_t num_channels;     /**< 1 或 16 */
    uint32_t sample_rate;      /**< 48000 */
    uint32_t byte_rate;        /**< sample_rate × num_channels × 2 */
    uint16_t block_align;      /**< num_channels × 2 */
    uint16_t bits_per_sample;  /**< 16 */
    /* data sub-chunk */
    uint8_t  data_id[4];       /**< "data" */
    uint32_t data_size;        /**< PCM 数据总大小 */
} WAV_Header_t;
#pragma pack(pop)

#define WAV_HEADER_SIZE     sizeof(WAV_Header_t)   /* 44 bytes */

/* ========== 缓冲配置 ========== */

/** @brief 环形缓冲大小: 8 帧 × 16ch × 256 × 2字节 = 64 KB */
#define RING_BUF_FRAMES     8u
#define MAX_FRAME_BYTES     (16u * 256u * 2u)   /* 16ch × 256 × int16_t */
#define RING_BUF_SIZE       (RING_BUF_FRAMES * MAX_FRAME_BYTES)

/* ========== 模块状态 ========== */

static volatile App_RecorderState_t s_state = RECORDER_IDLE;
static volatile App_RecorderMode_t  s_mode  = RECORDER_MODE_MONO;

static App_RecorderStats_t s_stats;
static uint32_t s_max_duration_sec = 0u;

/** @brief 当前录音通道数 (由 Start 设置) */
static uint16_t s_num_channels = 1u;

/** @brief 环形缓冲 */
static int16_t s_ring_buf[RING_BUF_SIZE / 2u]; /* int16_t 单位 */
static volatile uint32_t s_ring_wr = 0u;  /* 写指针 (int16_t offset) */
static volatile uint32_t s_ring_rd = 0u;  /* 读指针 (int16_t offset) */
static uint32_t s_ring_capacity = 0u;     /* 有效容量 (int16_t 单位) */

/** @brief WAV 数据已写字节数 (用于回填头) */
static uint32_t s_data_bytes_written = 0u;

/* ========== WAV 头填充 (由 Storage_Task 调用) ========== */

void App_Recorder_FillWAVHeader(uint8_t *buf, uint16_t num_ch, uint32_t data_size)
{
    WAV_Header_t *hdr = (WAV_Header_t *)buf;  /* 将输出缓冲强转为 WAV 头结构体指针 */

    /* ---- RIFF 主块 ---- */
    memcpy(hdr->riff_id, "RIFF", 4u);                   /* 4字节标识符 "RIFF" */
    hdr->riff_size = WAV_HEADER_SIZE - 8u + data_size;   /* 文件总大小 - 8（不含 "RIFF" 和 riff_size 自身） */
    memcpy(hdr->wave_id, "WAVE", 4u);                    /* 格式标识符 "WAVE" */

    /* ---- fmt 子块 ---- */
    memcpy(hdr->fmt_id,  "fmt ", 4u);                    /* fmt 子块标识符（注意含空格） */
    hdr->fmt_size        = 16u;                          /* fmt 子块数据长度，PCM 固定为 16 字节 */
    hdr->audio_format    = 1u;                           /* 音频格式：1 = PCM（线性）无压缩 */
    hdr->num_channels    = num_ch;                       /* 通道数：MONO=1，RAW16=16 */
    hdr->sample_rate     = 48000u;                       /* 采样率：48kHz（与 SAI 配置一致） */
    hdr->byte_rate       = 48000u * (uint32_t)num_ch * 2u; /* 每秒字节数 = 采样率 × 通道数 × 位宽/8 */
    hdr->block_align     = num_ch * 2u;                  /* 块对齐 = 通道数 × 每样本字节数（2=16bit） */
    hdr->bits_per_sample = 16u;                          /* 每样本位数：16-bit PCM */

    /* ---- data 子块 ---- */
    memcpy(hdr->data_id, "data", 4u);   /* 数据子块标识符 "data" */
    hdr->data_size       = data_size;   /* PCM 原始音频数据的总字节数 */
}

/**
 * @brief 环形缓冲可用数据 (int16_t 单位)
 * @details 计算当前可供 FlushPending 读取的有效样本数。
 *          使用无符号回绕处理写指针已绕圈而读指针未绕的情况。
 */
static uint32_t s_ring_available(void)
{
    uint32_t wr = s_ring_wr;  /* 一次性读取写指针快照（避免 volatile 多次读取不一致） */
    uint32_t rd = s_ring_rd;  /* 一次性读取读指针快照 */
    if (wr >= rd)
    {
        return wr - rd;  /* 正常情况：wr 在 rd 后面，直接相减得可用数量 */
    }
    /* 写指针已绕圈，读指针还在后半段：可用 = 尾端剩余 + 头端已写 */
    return s_ring_capacity - rd + wr;
}

/**
 * @brief 环形缓冲剩余空间 (int16_t 单位)
 * @details 预留 1 个单位分隔满与空状态（避免 wr==rd 歧义）。
 */
static uint32_t s_ring_free(void)
{
    /* 总容量 - 1（满/空分隔位）- 已用数量 = 可写空间 */
    return s_ring_capacity - 1u - s_ring_available();
}

/* ========== 公开 API ========== */

void App_Recorder_Init(void)
{
    s_state = RECORDER_IDLE;                           /* 初始状态为空闲 */
    memset(&s_stats, 0, sizeof(s_stats));              /* 清零所有录音统计（帧数/时长/丢帧） */
    s_ring_wr = 0u;                                    /* 写指针归零 */
    s_ring_rd = 0u;                                    /* 读指针归零 */
    s_ring_capacity = RING_BUF_SIZE / 2u;             /* 以 int16_t 为单位计算容量 */
    s_data_bytes_written = 0u;                         /* 已写字节数清零（用于 WAV 头回填） */
    s_max_duration_sec = 0u;                           /* 最大录音时长清零（0=无限制） */
}

Err_t App_Recorder_Start(App_RecorderMode_t mode)
{
    if (s_state == RECORDER_RECORDING)
    {
        return ERR_BUSY;  /* 已在录音状态，拒绝重复启动 */
    }

    s_mode = mode;  /* 保存录音模式（MONO 或 RAW16） */
    /* 根据模式设置通道数：MONO=1ch，RAW16=16ch */
    s_num_channels = (mode == RECORDER_MODE_RAW16) ? 16u : 1u;

    /* 重置环形缓冲和统计 — 文件 I/O 由 Storage_Task 完成 */
    s_ring_wr = 0u;                          /* 写指针归零（丢弃上轮残留数据） */
    s_ring_rd = 0u;                          /* 读指针归零 */
    s_data_bytes_written = 0u;               /* 清零写入字节记录 */
    memset(&s_stats, 0, sizeof(s_stats));    /* 清零统计（帧数/时长/丢帧） */

    s_state = RECORDER_RECORDING;  /* 切换到录音状态，Feed() 将开始接收数据 */
    return ERR_OK;
}

Err_t App_Recorder_Stop(void)
{
    s_state = RECORDER_IDLE;  /* 设置为空闲，Feed() 调用将立即返回（不再接收数据） */
    /* [注意] 环形缓冲中已有的数据仍需由 FlushPending 写入 SD 卡后，
     * Storage_Task 才能关闭文件并回填 WAV 头 */
    return ERR_OK;
}

void App_Recorder_Feed(const float *mono_frame,
                       const int16_t *raw_frame,
                       uint16_t frame_len)
{
    uint32_t i;                    /* 循环计数器 */
    uint32_t samples_to_write;    /* 本帧需写入的 int16_t 样本数 */
    uint32_t free_space;          /* 当前环形缓冲剩余可写空间（int16_t 单位） */

    if (s_state != RECORDER_RECORDING)
    {
        return;  /* 非录音状态（空闲/停止中/错误）直接丢弃数据 */
    }

    /* 检查最大录音时长 (0=无限制，非零=有限) */
    if ((s_max_duration_sec > 0u) &&
        (s_stats.duration_ms >= (s_max_duration_sec * 1000u)))
    {
        return;  /* 已超出设定时长，停止接收新帧 */
    }

    if (s_mode == RECORDER_MODE_MONO)
    {
        /* ---- MONO 模式：DAS 波束成形输出，float → int16_t 转换 ---- */
        if (mono_frame == NULL)
        {
            return;  /* mono_frame 为空时无法录音，直接返回 */
        }
        samples_to_write = (uint32_t)frame_len;  /* MONO：每帧 frame_len 个 int16_t */

        free_space = s_ring_free();              /* 查询可写空间 */
        if (free_space < samples_to_write)       /* 空间不足：丢弃本帧并计数 */
        {
            s_stats.dropped_frames++;
            /* [改进] 可记录连续丢帧数并发出警告，提示 SD 写速过慢 */
            return;
        }

        /* float → int16_t 饱和截断转换 + NaN 防护，写入环形缓冲 */
        for (i = 0u; i < samples_to_write; i++)
        {
            float val = mono_frame[i] * 32767.0f;  /* 将 [-1.0, 1.0] 映射到 int16_t 范围 */
            int32_t ival;
            if (val != val)               { ival = 0;      }  /* NaN 检测：输出静音（0） */
            else if (val > 32767.0f)      { ival = 32767;  }  /* 正饱和截断 */
            else if (val < -32768.0f)     { ival = -32768; }  /* 负饱和截断 */
            else                          { ival = (int32_t)val; } /* 正常范围内截断取整 */

            s_ring_buf[s_ring_wr] = (int16_t)ival;  /* 写入环形缓冲目标位置 */
            s_ring_wr++;                             /* 写指针前进 */
            if (s_ring_wr >= s_ring_capacity)
            {
                s_ring_wr = 0u;  /* 写指针到达末尾，回绕到缓冲头部 */
            }
        }
    }
    else /* RECORDER_MODE_RAW16 */
    {
        /* ---- RAW16 模式：直接写入原始 16 通道 int16_t 数据 ---- */
        if (raw_frame == NULL)
        {
            return;  /* raw_frame 为空时无法录音，直接返回 */
        }
        /* RAW16 每帧样本数 = frame_len × 16通道 = 4096 int16_t (约 8KB/帧) */
        samples_to_write = (uint32_t)frame_len * 16u;

        free_space = s_ring_free();              /* 查询可写空间 */
        if (free_space < samples_to_write)       /* 空间不足：丢弃本帧（写速赶不上录音速率） */
        {
            s_stats.dropped_frames++;
            /* [改进] RAW16 模式带宽约 1536 KB/s，需 Class 10 及以上 SD 卡 */
            return;
        }

        /* 直接拷贝 int16_t 数据到环形缓冲（无类型转换，保留原始位模式） */
        for (i = 0u; i < samples_to_write; i++)
        {
            s_ring_buf[s_ring_wr] = raw_frame[i];  /* 交织格式（ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ...） */
            s_ring_wr++;                            /* 写指针前进 */
            if (s_ring_wr >= s_ring_capacity)
            {
                s_ring_wr = 0u;  /* 写指针回绕 */
            }
        }
    }

    s_stats.frames_captured++;  /* 成功写入帧，递增捕获帧数统计 */
    /* 从帧计数精确计算时长，避免逐帧截断误差
     * 公式: duration_ms = frames × samples_per_frame × 1000 / sample_rate
     *       = frames × 256 × 1000 / 48000（精确到毫秒，无浮点误差累积） */
    s_stats.duration_ms = (uint32_t)((uint64_t)s_stats.frames_captured *
                                     (uint64_t)frame_len * 1000u / 48000u);
}

Err_t App_Recorder_FlushPending(void *fil_handle)
{
    FIL *fp = (FIL *)fil_handle;   /* 将 void* 强转为 FatFS 文件句柄（避免头文件循环依赖） */
    uint32_t available;            /* 环形缓冲中待写入的 int16_t 样本数 */
    uint32_t chunk_samples;       /* 本次循环可连续写入的样本数（不超过缓冲末尾） */
    uint32_t chunk_bytes;         /* 本次写入的字节数 = chunk_samples × 2 */
    FRESULT fr;                   /* FatFS 返回值 */
    UINT bw;                      /* 实际写入字节数（由 f_write 输出） */

    /* 仅在录音或停止中状态写入；文件句柄为空时跳过 */
    if ((s_state != RECORDER_RECORDING && s_state != RECORDER_STOPPING) ||
        (fp == NULL))
    {
        return ERR_OK;  /* 非录音状态直接返回成功（无数据需写） */
    }

    available = s_ring_available();  /* 查询待写入的样本数 */
    if (available == 0u)
    {
        return ERR_OK;  /* 缓冲为空，无需写入 */
    }

    /* ---- 逐块写入环形缓冲（处理写指针绕圈情况） ---- */
    /* 最多一次写 256 × 16 个 int16_t（4KB/帧）。
     * 由于环形缓冲末尾到缓冲首部的跨越不连续，需要分两段写入：
     *   段1: s_ring_rd → buf 末尾
     *   段2: buf 头部 → 新的 s_ring_rd
     * 每次循环处理一段连续区间，直到 available 清零。 */
    while (available > 0u)
    {
        /* 计算本次可连续读取的最大长度（不越过缓冲末尾） */
        if (s_ring_rd + available > s_ring_capacity)
        {
            /* 读指针到缓冲末尾的剩余空间就是本次能连续读取的量 */
            chunk_samples = s_ring_capacity - s_ring_rd;
        }
        else
        {
            chunk_samples = available;  /* 未绕圈，一次性写完 */
        }

        chunk_bytes = chunk_samples * 2u;  /* int16_t → 字节（每样本 2 字节） */

        /* 通过 FatFS f_write 将连续内存块写入 SD 卡文件 */
        fr = f_write(fp, (const uint8_t *)&s_ring_buf[s_ring_rd],
                     chunk_bytes, &bw);
        if ((fr != FR_OK) || (bw != chunk_bytes))
        {
            /* 写入失败：切换到错误状态，后续 Feed/Flush 调用均会返回 */
            s_state = RECORDER_ERROR;
            return ERR_IO_FAILED;  /* 返回 IO 错误，由调用方（Storage_Task）处理 */
        }

        s_data_bytes_written += chunk_bytes;   /* 累加总写入字节数（用于回填 WAV 头） */
        s_stats.bytes_written += chunk_bytes;  /* 同步更新统计信息 */

        s_ring_rd += chunk_samples;            /* 读指针前进 */
        if (s_ring_rd >= s_ring_capacity)
        {
            s_ring_rd = 0u;  /* 读指针到达末尾，回绕到头部 */
        }

        available = s_ring_available();  /* 重新查询剩余待写量（准备下次循环） */
    }

    return ERR_OK;
}

App_RecorderState_t App_Recorder_GetState(void)
{
    return s_state;  /* 直接返回当前录音状态（单字节读取原子，无需临界区） */
}

void App_Recorder_GetStats(App_RecorderStats_t *stats)
{
    if (stats != NULL)       /* 空指针保护 */
    {
        *stats = s_stats;    /* 结构体整体拷贝，返回统计快照 */
    }
}

void App_Recorder_SetMaxDuration(uint32_t max_sec)
{
    s_max_duration_sec = max_sec;  /* 设置最大录音时长（0=无限制） */
}

uint32_t App_Recorder_GetDataBytesWritten(void)
{
    return s_data_bytes_written;  /* 返回已写入 SD 卡的 PCM 数据字节数（用于 WAV 头回填） */
}

uint16_t App_Recorder_GetNumChannels(void)
{
    return s_num_channels;  /* 返回当前录音通道数：MONO=1，RAW16=16 */
}
