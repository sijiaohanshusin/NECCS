#include "./BSP/COMPETITION/comp_buzzer.h"
#include "./BSP/BEEP/beep.h"
#include "./SYSTEM/delay/delay.h"

void comp_buzzer_init(void)
{
    beep_init();
}

void comp_buzzer_set(uint8_t on)
{
    if (on)
    {
        BEEP(1);
    }
    else
    {
        BEEP(0);
    }
}

void comp_buzzer_beep(uint32_t duration_ms)
{
    BEEP(1);
    delay_ms(duration_ms);
    BEEP(0);
}
