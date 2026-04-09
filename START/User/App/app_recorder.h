/**
 * @file    app_recorder.h
 * @brief   WAV 录音模块 — 环形缓冲 + 状态管理
 * @details 支持两种录音模式:
 *          - MONO:  DAS 波束成形输出 (1ch, 48kHz, 16bit)
 *          - RAW16: 原始 16 通道数据 (16ch, 48kHz, 16bit)
 *          数据通过环形缓冲逐帧累积, 由 Storage_Task 异步写入 SD 卡。
 *          文件 I/O 由 Storage_Task 通过 FlushPending(fil_handle) 完成。
 */
#ifndef __APP_RECORDER_H
#define __APP_RECORDER_H

#include <stdint.h>
#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 录音模式 */
typedef enum {
    RECORDER_MODE_MONO  = 0u,  /**< 波束成形单通道 */
    RECORDER_MODE_RAW16 = 1u   /**< 原始 16 通道 */
} App_RecorderMode_t;

/** @brief 录音状态 */
typedef enum {
    RECORDER_IDLE      = 0u,
    RECORDER_RECORDING = 1u,
    RECORDER_STOPPING  = 2u,
    RECORDER_ERROR     = 3u
} App_RecorderState_t;

/** @brief 录音统计 */
typedef struct {
    uint32_t frames_captured;  /**< 已捕获帧数 */
    uint32_t bytes_written;    /**< 已写入字节数 */
    uint32_t duration_ms;      /**< 录音时长 (ms) */
    uint32_t dropped_frames;   /**< 丢帧数 */
} App_RecorderStats_t;

/** @brief 初始化录音模块 */
void App_Recorder_Init(void);

/**
 * @brief 开始录音 (重置环形缓冲和状态, 不做文件 I/O)
 * @param mode 录音模式
 * @return ERR_OK 成功, ERR_BUSY 已在录音
 * @note  文件打开和 WAV 头写入由 Storage_Task 完成
 */
Err_t App_Recorder_Start(App_RecorderMode_t mode);

/**
 * @brief 停止录音 (设置状态为 IDLE)
 * @return ERR_OK 成功
 * @note  文件关闭和 WAV 头回填由 Storage_Task 完成
 */
Err_t App_Recorder_Stop(void);

/**
 * @brief 送入一帧数据 (在音频任务中调用)
 * @param mono_frame  波束成形单通道输出 (float[256]), 可为 NULL
 * @param raw_frame   原始多通道 q15_t 数据 (int16_t[16*256]), 可为 NULL
 * @param frame_len   帧长度 (256)
 */
void App_Recorder_Feed(const float *mono_frame,
                       const int16_t *raw_frame,
                       uint16_t frame_len);

/**
 * @brief 将环形缓冲中的数据写入文件 (在 Storage_Task 中调用)
 * @param fil_handle  FatFS FIL 句柄指针 (传入 void* 避免头文件依赖 ff.h)
 * @return ERR_OK 或 ERR_IO_FAILED
 */
Err_t App_Recorder_FlushPending(void *fil_handle);

/**
 * @brief 获取录音状态
 * @return 当前状态
 */
App_RecorderState_t App_Recorder_GetState(void);

/**
 * @brief 获取录音统计
 * @param stats 输出统计信息
 */
void App_Recorder_GetStats(App_RecorderStats_t *stats);

/**
 * @brief 设置最大录音时长 (0=无限)
 * @param max_sec 最大秒数
 */
void App_Recorder_SetMaxDuration(uint32_t max_sec);

/**
 * @brief 填充 44 字节 WAV 头 (由 Storage_Task 调用)
 * @param buf       输出缓冲 (最少 44 字节)
 * @param num_ch    通道数 (1 或 16)
 * @param data_size PCM 数据总字节数
 */
void App_Recorder_FillWAVHeader(uint8_t *buf, uint16_t num_ch, uint32_t data_size);

/**
 * @brief 获取已写入的 PCM 数据字节数 (用于 WAV 头回填)
 * @return 字节数
 */
uint32_t App_Recorder_GetDataBytesWritten(void);

/**
 * @brief 获取当前录音通道数
 * @return 1 (MONO) 或 16 (RAW16)
 */
uint16_t App_Recorder_GetNumChannels(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_RECORDER_H */
