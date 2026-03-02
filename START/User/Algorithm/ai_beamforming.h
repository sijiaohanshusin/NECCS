#ifndef AI_BEAMFORMING_H
#define AI_BEAMFORMING_H

#include "arm_math.h"
#include "app_main_task.h"

#ifdef __cplusplus
extern "C" {
#endif

void AI_FFT_Process(void);
void AI_SRP_PHAT_Init(void);
void AI_SRP_PHAT_Process(Sound_Pos_t *result);

extern volatile uint32_t g_srp_invalid_count;
extern volatile uint32_t g_srp_low_contrast_count;
extern volatile float32_t g_srp_last_contrast;
extern volatile float32_t g_srp_last_quality;

#ifdef __cplusplus
}
#endif

#endif /* AI_BEAMFORMING_H */
