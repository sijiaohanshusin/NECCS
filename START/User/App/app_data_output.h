#ifndef APP_DATA_OUTPUT_H
#define APP_DATA_OUTPUT_H

#include "arm_math.h"
#include "app_main_task.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void VOFA_Send_Channel_RMS(void);
void VOFA_Send_FFT_Magnitude(uint8_t channel);

/**
 * Mode 3 payload format:
 * [0] x_angle
 * [1] y_angle
 * [2] energy
 * [3 ... 3+COARSE_TOTAL-1] coarse SRP power map
 * diagnostics at base = 3 + COARSE_TOTAL:
 * [base+0] g_audio_both_flags_count
 * [base+1] uart_tx_drop_count
 * [base+2] g_srp_invalid_count
 * [base+3] coarse_power_hold_count
 * [base+4] g_srp_low_contrast_count
 * [base+5] g_srp_last_contrast
 * [base+6] g_srp_last_quality
 * [base+7] frame_seq
 */
void VOFA_Send_SRP_Result(const Sound_Pos_t *pos);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_OUTPUT_H */
