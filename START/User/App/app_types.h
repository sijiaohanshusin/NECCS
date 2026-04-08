/**
 * @file    app_types.h
 * @brief   Shared application data types
 */
#ifndef APP_TYPES_H
#define APP_TYPES_H

#include "app_user_config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 数据结构定义 (Data Structures)
 * ============================================================================ */

/**
 * @brief   DMA 半缓冲标识
 * @details 用于标识 PING/PONG 双缓冲的哪一半已完成
 *
 * PING/PONG 双缓冲机制：
 * - DMA 写入 PING 区时，CPU 处理 PONG 区
 * - DMA 写入 PONG 区时，CPU 处理 PING 区
 * - 避免数据覆盖，实现零拷贝流水线
 */
typedef enum {
    AUDIO_DMA_HALF_PING = 0u,  /**< Mic_Rx_Buffer 前半区 (0 ~ DMA_BUFFER_SIZE/2-1) */
    AUDIO_DMA_HALF_PONG = 1u   /**< Mic_Rx_Buffer 后半区 (DMA_BUFFER_SIZE/2 ~ DMA_BUFFER_SIZE-1) */
} Audio_DmaHalf_t;

/**
 * @brief   音频帧事件
 * @details DMA 中断生产，音频任务消费
 *
 * 队列机制：
 * - 队列长度：1 (仅保留最新帧)
 * - ISR 发送：xQueueOverwrite (覆盖旧数据)
 * - 任务接收：xQueueReceive (阻塞等待)
 *
 * 为什么队列长度为 1？
 * - 实时系统，只关心最新数据
 * - 避免队列积压导致延迟
 * - 丢帧策略：丢弃旧帧，处理新帧
 */
typedef struct {
    uint8_t half_id;        /**< DMA 半缓冲标识 @ref Audio_DmaHalf_t */
    uint8_t reserved[3];    /**< 对齐/预留字段 (保证 4 字节对齐) */
    uint32_t seq;           /**< ISR 侧单调递增序号 (用于丢帧检测) */
} Audio_FrameEvent_t;

/**
 * @brief   声源定位结果
 * @details 包含声源的方位角和能量信息
 *
 * 坐标系约定：
 * - x_angle: 水平角 (方位角)，正值向右，负值向左
 * - y_angle: 垂直角 (俯仰角)，正值向上，负值向下
 * - 原点：麦克风阵列中心正前方
 *
 * 能量归一化：
 * - 范围：[0, 1]
 * - 0: 无声源或噪声
 * - 1: 强声源 (理想情况)
 * - 实际值：通常 0.3-0.8
 */
typedef struct {
    float x_angle;  /**< 水平角 (度)，范围 [-60, 60] */
    float y_angle;  /**< 垂直角 (度)，范围 [-60, 60] */
    float energy;   /**< 归一化能量 [0, 1] */
} Sound_Pos_t;

/** @brief 多声源最大追踪数 */
#define MULTI_SOURCE_MAX    3u

/**
 * @brief   多声源定位结果
 * @details 同时追踪 Top-K 个声源位置
 */
typedef struct {
    Sound_Pos_t sources[MULTI_SOURCE_MAX]; /**< 各声源位置（按能量降序） */
    uint8_t     count;                     /**< 有效声源数 [0, MULTI_SOURCE_MAX] */
} Sound_MultiPos_t;

/** @brief 全局多声源结果（音频任务写入，UI/显示任务读取） */
extern Sound_MultiPos_t g_multi_source;

#define APP_SPECTRUM_BIN_COUNT    (FRAME_LEN / 2u)

typedef struct {
    uint16_t start_bin;
    uint16_t end_bin;
} App_FreqBand_t;

typedef struct {
    uint32_t seq;
    uint16_t bin_count;
    float delta_f_hz;
    App_FreqBand_t active_band;
    App_FreqBand_t preview_band;
    float magnitude[APP_SPECTRUM_BIN_COUNT];
} App_SpectrumFrame_t;
#ifdef __cplusplus
}
#endif

#endif /* APP_TYPES_H */
