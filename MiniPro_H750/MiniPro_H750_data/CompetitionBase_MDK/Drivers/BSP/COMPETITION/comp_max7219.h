#ifndef __COMP_MAX7219_H
#define __COMP_MAX7219_H

#include "./SYSTEM/sys/sys.h"

/*
 * MAX7219 + FJ5463A common-cathode four-digit display.
 * The three MCU signals MUST pass through a 5 V-powered 74AHCT125:
 *
 * P5-2 / PB12 -> 74AHCT125 -> MAX7219 DIN
 * P5-8 / PA2 -> 74AHCT125 -> MAX7219 CLK
 * P5-6 / PE0 -> 74AHCT125 -> MAX7219 LOAD
 *
 * See the competition wiring manual for the complete, pin-numbered circuit.
 */

void comp_max7219_init(void);
void comp_max7219_clear(void);
void comp_max7219_set_intensity(uint8_t level);
void comp_max7219_show_number(int16_t value, uint8_t leading_zero);
void comp_max7219_show_fixed1(int16_t tenths);
void comp_max7219_set_digit(uint8_t position, uint8_t code_b_data);

#define COMP_MAX7219_BLANK  0x0FU
#define COMP_MAX7219_MINUS  0x0AU
#define COMP_MAX7219_DP     0x80U

#endif
