#ifndef __COMP_LED_H
#define __COMP_LED_H

#include "./SYSTEM/sys/sys.h"

typedef enum
{
    COMP_LED1 = 0,
    COMP_LED2,
    COMP_LED3,
    COMP_LED4,
    COMP_LED_COUNT
} comp_led_t;

void comp_led_init(void);
void comp_led_set(comp_led_t led, uint8_t on);
void comp_led_toggle(comp_led_t led);
void comp_led_set_mask(uint8_t mask);

#endif
