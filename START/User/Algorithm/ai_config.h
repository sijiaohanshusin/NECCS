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

#endif /* __AI_CONFIG_H */