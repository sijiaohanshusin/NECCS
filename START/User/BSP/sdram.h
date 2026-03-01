/**
  ******************************************************************************
  * @file    sdram.h
  * @brief   W9825G6KH SDRAM 驱动 (适配自官方例程)
  ******************************************************************************
  */

#ifndef __SDRAM_H
#define __SDRAM_H

#include "main.h"

extern SDRAM_HandleTypeDef g_sdram_handle;

#define BANK5_SDRAM_ADDR    ((uint32_t)(0xC0000000))    /* SDRAM 起始地址 */

/* SDRAM Mode Register 配置位 */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

void sdram_init(void);
uint8_t sdram_send_cmd(uint8_t bankx, uint8_t cmd, uint8_t refresh, uint16_t regval);
void sdram_initialization_sequence(void);
void fmc_sdram_write_buffer(uint8_t *pbuf, uint32_t writeaddr, uint32_t n);
void fmc_sdram_read_buffer(uint8_t *pbuf, uint32_t readaddr, uint32_t n);

#endif /* __SDRAM_H */
