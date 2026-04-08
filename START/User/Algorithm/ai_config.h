/**
 * @file    ai_config.h
 * @brief   算法配置兼容入口与派生常量
 * @details
 * 用户可调参数已经统一迁移到 `app_user_config.h`。
 * 本文件只保留算法模块仍然需要的派生常量与编译期检查。
 */
#ifndef AI_CONFIG_H
#define AI_CONFIG_H

#include <stdint.h>

#include "app_user_config.h"

/** DMA 双缓冲总长度：通道数 × 帧长 × 2(PING/PONG)。 */
#define DMA_BUFFER_SIZE     (MIC_CHANNELS * FRAME_LEN * 2u)

/** FFT 频率分辨率，单位 Hz。 */
#define DELTA_F             ((float)SAMPLING_RATE / (float)FRAME_LEN)

/** SRP 参与积分的频率 bin 总数。 */
#define SRP_FREQ_BINS       (SRP_FREQ_BIN_END - SRP_FREQ_BIN_START + 1u)

/** 粗搜索总点数。 */
#define COARSE_TOTAL        (COARSE_GRID_SIZE * COARSE_GRID_SIZE)

/** 单个粗峰值对应的细搜索点数。 */
#define FINE_TOTAL_PER_TOP  (FINE_GRID_SIZE * FINE_GRID_SIZE)

/** 细搜索总点数。 */
#define FINE_TOTAL          (FINE_TOP_K * FINE_TOTAL_PER_TOP)

/** 粗搜索与细搜索合计总点数。 */
#define SRP_GRID_TOTAL      (COARSE_TOTAL + FINE_TOTAL)

#endif /* AI_CONFIG_H */
