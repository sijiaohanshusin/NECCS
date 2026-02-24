#ifndef __APP_DATA_STREAM_H
#define __APP_DATA_STREAM_H

#include "ai_config.h"
#include "arm_math.h" // 引入 float32_t 定义

// ==========================================
// 外部数据引用
// ==========================================

// 1. DMA 接收原始缓冲区 (Ping-Pong 双缓冲)
// 物理位置: SRAM1 (D2 Domain)
// 数据格式: Interleaved (交织) int16_t [L1, R1, L2, R2...]
extern int16_t Mic_Rx_Buffer[DMA_BUFFER_SIZE];

// 2. 信号处理核心缓冲区 (单帧)
// 物理位置: DTCM (D1 Domain, Core Coupled)
// 数据格式: De-interleaved (解交织) float32_t [Ch0...][Ch1...]
extern float32_t Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN];

// 3. 频域数据缓冲区 (FFT 输出)
// 物理位置: DTCM
// 数据格式: Complex Float [Real, Imag, Real, Imag...]
extern float32_t Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN]; // 注意：RFFT长度通常与输入相同

// 4. FFT 句柄 (CMSIS-DSP)
extern arm_rfft_fast_instance_f32 S_Rfft;

#endif /* __APP_DATA_STREAM_H */