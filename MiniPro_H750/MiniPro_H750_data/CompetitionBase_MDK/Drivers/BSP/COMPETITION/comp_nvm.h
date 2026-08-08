#ifndef __COMP_NVM_H
#define __COMP_NVM_H

#include "./SYSTEM/sys/sys.h"

#define COMP_NVM_CAPACITY_BYTES 1024U

uint8_t comp_nvm_init(void);
uint16_t comp_nvm_flash_id(void);
uint8_t comp_nvm_read(uint16_t offset, uint8_t *buffer, uint16_t length);
uint8_t comp_nvm_write(uint16_t offset, const uint8_t *buffer, uint16_t length);

#endif
