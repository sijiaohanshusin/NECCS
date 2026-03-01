#include "app_data_stream.h"
#include "ai_beamforming.h"
#include "mpu.h"

#include <math.h>

__SECTION_DMA_BUFFER __attribute__((aligned(32)))
int16_t Mic_Rx_Buffer[DMA_BUFFER_SIZE] = {0};

__SECTION_DTCM __attribute__((aligned(32)))
float32_t Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN] = {0.0f};

__SECTION_DTCM __attribute__((aligned(32)))
float32_t Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN] = {0.0f};

__SECTION_DTCM
arm_rfft_fast_instance_f32 S_Rfft;

__SECTION_DTCM __attribute__((aligned(32)))
float32_t Hanning_Window[FRAME_LEN] = {0.0f};

__SECTION_AXI_SRAM __attribute__((aligned(32)))
float32_t GCC_PHAT_Buffer[SRP_PAIR_COUNT * SRP_FREQ_BINS * 2u] = {0.0f};

__SECTION_AXI_SRAM __attribute__((aligned(32)))
float32_t SRP_Power[SRP_GRID_TOTAL] = {0.0f};

void App_Stream_Init(void)
{
    arm_rfft_fast_init_f32(&S_Rfft, FRAME_LEN);

    for (uint32_t n = 0u; n < FRAME_LEN; n++)
    {
        Hanning_Window[n] = 0.5f *
                            (1.0f - cosf(2.0f * 3.14159265358979f * (float32_t)n / (float32_t)FRAME_LEN));
    }

    AI_SRP_PHAT_Init();
}

