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
void VOFA_Send_SRP_Result(const Sound_Pos_t *pos);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_OUTPUT_H */

