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
    WAV_Header_t *hdr = (WAV_Header_t *)buf;

    memcpy(hdr->riff_id, "RIFF", 4u);
    hdr->riff_size = WAV_HEADER_SIZE - 8u + data_size;
    memcpy(hdr->wave_id, "WAVE", 4u);
    memcpy(hdr->fmt_id,  "fmt ", 4u);
    hdr->fmt_size        = 16u;
    hdr->audio_format    = 1u;  /* PCM */
    hdr->num_channels    = num_ch;
    hdr->sample_rate     = 48000u;
    hdr->byte_rate       = 48000u * (uint32_t)num_ch * 2u;
    hdr->block_align     = num_ch * 2u;
    hdr->bits_per_sample = 16u;
    memcpy(hdr->data_id, "data", 4u);
    hdr->data_size       = data_size;
}

/**
 * @brief 环形缓冲可用数据 (int16_t 单位)
 */
static uint32_t s_ring_available(void)
{
    uint32_t wr = s_ring_wr;
    uint32_t rd = s_ring_rd;
    if (wr >= rd)
    {
        return wr - rd;
    }
    return s_ring_capacity - rd + wr;
}

/**
 * @brief 环形缓冲剩余空间 (int16_t 单位)
 */
static uint32_t s_ring_free(void)
{
    return s_ring_capacity - 1u - s_ring_available();
}

/* ========== 公开 API ========== */

void App_Recorder_Init(void)
{
    s_state = RECORDER_IDLE;
    memset(&s_stats, 0, sizeof(s_stats));
    s_ring_wr = 0u;
    s_ring_rd = 0u;
    s_ring_capacity = RING_BUF_SIZE / 2u;
    s_data_bytes_written = 0u;
    s_max_duration_sec = 0u;
}

Err_t App_Recorder_Start(App_RecorderMode_t mode)
{
    if (s_state == RECORDER_RECORDING)
    {
        return ERR_BUSY;
    }

    s_mode = mode;
    s_num_channels = (mode == RECORDER_MODE_RAW16) ? 16u : 1u;

    /* 重置环形缓冲和统计 — 文件 I/O 由 Storage_Task 完成 */
    s_ring_wr = 0u;
    s_ring_rd = 0u;
    s_data_bytes_written = 0u;
    memset(&s_stats, 0, sizeof(s_stats));

    s_state = RECORDER_RECORDING;
    return ERR_OK;
}

Err_t App_Recorder_Stop(void)
{
    s_state = RECORDER_IDLE;
    return ERR_OK;
}

void App_Recorder_Feed(const float *mono_frame,
                       const int16_t *raw_frame,
                       uint16_t frame_len)
{
    uint32_t i;
    uint32_t samples_to_write;
    uint32_t free_space;

    if (s_state != RECORDER_RECORDING)
    {
        return;
    }

    /* 检查最大录音时长 */
    if ((s_max_duration_sec > 0u) &&
        (s_stats.duration_ms >= (s_max_duration_sec * 1000u)))
    {
        return;
    }

    if (s_mode == RECORDER_MODE_MONO)
    {
        if (mono_frame == NULL)
        {
            return;
        }
        samples_to_write = (uint32_t)frame_len;

        free_space = s_ring_free();
        if (free_space < samples_to_write)
        {
            s_stats.dropped_frames++;
            return;
        }

        /* float -> int16_t, 饱和截断 + NaN 防护 */
        for (i = 0u; i < samples_to_write; i++)
        {
            float val = mono_frame[i] * 32767.0f;
            int32_t ival;
            if (val != val)               { ival = 0;      } /* NaN → 静音 */
            else if (val > 32767.0f)      { ival = 32767;  }
            else if (val < -32768.0f)     { ival = -32768; }
            else                          { ival = (int32_t)val; }

            s_ring_buf[s_ring_wr] = (int16_t)ival;
            s_ring_wr++;
            if (s_ring_wr >= s_ring_capacity)
            {
                s_ring_wr = 0u;
            }
        }
    }
    else /* RECORDER_MODE_RAW16 */
    {
        if (raw_frame == NULL)
        {
            return;
        }
        samples_to_write = (uint32_t)frame_len * 16u;

        free_space = s_ring_free();
        if (free_space < samples_to_write)
        {
            s_stats.dropped_frames++;
            return;
        }

        /* 直接拷贝 int16_t 数据 */
        for (i = 0u; i < samples_to_write; i++)
        {
            s_ring_buf[s_ring_wr] = raw_frame[i];
            s_ring_wr++;
            if (s_ring_wr >= s_ring_capacity)
            {
                s_ring_wr = 0u;
            }
        }
    }

    s_stats.frames_captured++;
    /* 从帧计数精确计算时长, 避免逐帧截断误差 (256/48000 = 5.333ms/frame) */
    s_stats.duration_ms = (uint32_t)((uint64_t)s_stats.frames_captured *
                                     (uint64_t)frame_len * 1000u / 48000u);
}

Err_t App_Recorder_FlushPending(void *fil_handle)
{
    FIL *fp = (FIL *)fil_handle;
    uint32_t available;
    uint32_t chunk_samples;
    uint32_t chunk_bytes;
    FRESULT fr;
    UINT bw;

    if ((s_state != RECORDER_RECORDING && s_state != RECORDER_STOPPING) ||
        (fp == NULL))
    {
        return ERR_OK;
    }

    available = s_ring_available();
    if (available == 0u)
    {
        return ERR_OK;
    }

    /* 逐块写入: 最多一次写 256 × 16 个 int16_t */
    while (available > 0u)
    {
        /* 计算连续可读长度 */
        if (s_ring_rd + available > s_ring_capacity)
        {
            chunk_samples = s_ring_capacity - s_ring_rd;
        }
        else
        {
            chunk_samples = available;
        }

        chunk_bytes = chunk_samples * 2u;

        fr = f_write(fp, (const uint8_t *)&s_ring_buf[s_ring_rd],
                     chunk_bytes, &bw);
        if ((fr != FR_OK) || (bw != chunk_bytes))
        {
            s_state = RECORDER_ERROR;
            return ERR_IO_FAILED;
        }

        s_data_bytes_written += chunk_bytes;
        s_stats.bytes_written += chunk_bytes;

        s_ring_rd += chunk_samples;
        if (s_ring_rd >= s_ring_capacity)
        {
            s_ring_rd = 0u;
        }

        available = s_ring_available();
    }

    return ERR_OK;
}

App_RecorderState_t App_Recorder_GetState(void)
{
    return s_state;
}

void App_Recorder_GetStats(App_RecorderStats_t *stats)
{
    if (stats != NULL)
    {
        *stats = s_stats;
    }
}

void App_Recorder_SetMaxDuration(uint32_t max_sec)
{
    s_max_duration_sec = max_sec;
}

uint32_t App_Recorder_GetDataBytesWritten(void)
{
    return s_data_bytes_written;
}

uint16_t App_Recorder_GetNumChannels(void)
{
    return s_num_channels;
}
