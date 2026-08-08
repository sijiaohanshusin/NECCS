#ifndef __COMP_UI_H
#define __COMP_UI_H

#include "./SYSTEM/sys/sys.h"

void comp_ui_init(uint16_t lcd_id, uint16_t flash_id, uint8_t flash_ok);
void comp_ui_show_light(uint8_t percent);
void comp_ui_show_key(uint8_t key_code);
void comp_ui_show_led_mask(uint8_t mask);

#endif
