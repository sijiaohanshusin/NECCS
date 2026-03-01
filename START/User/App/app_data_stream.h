#ifndef __APP_DATA_STREAM_H
#define __APP_DATA_STREAM_H

#include "ai_config.h"
#include "arm_math.h"

extern int16_t Mic_Rx_Buffer[DMA_BUFFER_SIZE];

extern float32_t Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN];
extern float32_t Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN];

extern arm_rfft_fast_instance_f32 S_Rfft;
extern float32_t Hanning_Window[FRAME_LEN];

extern float32_t GCC_PHAT_Buffer[SRP_PAIR_COUNT * SRP_FREQ_BINS * 2u];
extern float32_t SRP_Power[SRP_GRID_TOTAL];

void App_Stream_Init(void);

#endif /* __APP_DATA_STREAM_H */

