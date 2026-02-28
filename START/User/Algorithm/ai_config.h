#ifndef __AI_CONFIG_H
#define __AI_CONFIG_H

#include <stdint.h>

// --- 系统音频参数定义 ---
#define MIC_CHANNELS        16      // 麦克风通道数
#define FRAME_LEN           256     // 单帧采样点数 (5.33ms @ 48kHz)
#define SAMPLING_RATE       48000   // 采样率

// --- 数据流计算 ---
// 原始数据: 16ch * 256 * 2(16bit) = 8192 Bytes/Frame
// 双缓冲总大小: 16384 Bytes
#define DMA_BUFFER_SIZE     (MIC_CHANNELS * FRAME_LEN * 2) // 双缓冲，乘以2

// --- SRP-PHAT 波束成形参数 ---
#define SPEED_OF_SOUND      343.0f
#define SRP_PAIR_COUNT      40          // 最大基线的 40 对 (C(16,2)=120 中选取)
#define SRP_FREQ_BIN_START  1           // 起始 bin (187.5 Hz)
#define SRP_FREQ_BIN_END    32          // 终止 bin (6000 Hz)
#define SRP_FREQ_BINS       32          // 有用频率 bin 数
#define DELTA_F             ((float32_t)SAMPLING_RATE / (float32_t)FRAME_LEN)

#define COARSE_GRID_SIZE    7
#define COARSE_TOTAL        49          // 7×7
#define COARSE_ANGLE_MIN    (-60.0f)
#define COARSE_ANGLE_MAX    (60.0f)
#define COARSE_ANGLE_STEP   (20.0f)

#define FINE_TOP_K          3
#define FINE_GRID_SIZE      5
#define FINE_TOTAL_PER_TOP  25          // 5×5
#define FINE_TOTAL          75          // 3×25
#define FINE_HALF_RANGE     (10.0f)
#define FINE_ANGLE_STEP     (5.0f)

#define SRP_GRID_TOTAL      124         // 49 + 75
#define PHAT_EPSILON        (1e-10f)

#endif /* __AI_CONFIG_H */