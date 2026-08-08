#ifndef __COMP_KEYS_H
#define __COMP_KEYS_H

#include "./SYSTEM/sys/sys.h"

typedef enum
{
    COMP_KEY_NONE = 0,
    COMP_KEY_BOARD_0 = 1,
    COMP_KEY_BOARD_1 = 2,
    COMP_KEY_EXT_0 = 3,
    COMP_KEY_EXT_1 = 4
} comp_key_t;

void comp_keys_init(void);
uint8_t comp_keys_read_mask(void);
comp_key_t comp_keys_poll_event(void);

#endif
