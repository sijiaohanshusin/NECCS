#ifndef __COMP_PWM_H
#define __COMP_PWM_H

#include "./SYSTEM/sys/sys.h"

uint8_t comp_pwm_init(uint32_t frequency_hz, uint16_t duty_per_mille);
void comp_pwm_set_duty(uint16_t duty_per_mille);
void comp_pwm_stop(void);

#endif
