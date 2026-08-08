#ifndef __COMP_BUZZER_H
#define __COMP_BUZZER_H

#include "./SYSTEM/sys/sys.h"

void comp_buzzer_init(void);
void comp_buzzer_set(uint8_t on);
void comp_buzzer_beep(uint32_t duration_ms);

#endif
