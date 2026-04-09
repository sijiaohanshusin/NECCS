/**
 * @file    bsp_sd.h
 * @brief   SD 卡 BSP 驱动 (SDMMC1, 4-bit)
 * @details 封装 HAL_SD API，提供 GPIO 初始化、卡检测、读写接口。
 *          硬件: SDMMC1 — PC8(D0), PC9(D1), PC10(D2), PC11(D3), PC12(CLK), PD2(CMD)
 */
#ifndef __BSP_SD_H
#define __BSP_SD_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SD 卡状态 */
typedef enum {
    BSP_SD_OK       = 0u,
    BSP_SD_ERROR    = 1u,
    BSP_SD_TIMEOUT  = 2u,
    BSP_SD_NOT_INIT = 3u
} BSP_SD_Status_t;

/** @brief SD 卡信息 */
typedef struct {
    uint32_t card_type;         /**< 卡类型 (SDSC/SDHC/SDXC) */
    uint32_t block_count;       /**< 块总数 */
    uint32_t block_size;        /**< 块大小 (字节) */
    uint64_t capacity_bytes;    /**< 总容量 (字节) */
} BSP_SD_CardInfo_t;

/**
 * @brief 初始化 SD 卡 (GPIO + SDMMC1 + 卡识别)
 * @return BSP_SD_OK 或 BSP_SD_ERROR
 */
BSP_SD_Status_t BSP_SD_Init(void);

/**
 * @brief 反初始化 SD 卡
 * @return BSP_SD_OK
 */
BSP_SD_Status_t BSP_SD_DeInit(void);

/**
 * @brief 读取 SD 卡扇区
 * @param pData     输出缓冲
 * @param block_addr 起始块地址
 * @param num_blocks 块数
 * @param timeout   超时 (ms)
 * @return BSP_SD_OK 或 BSP_SD_ERROR
 */
BSP_SD_Status_t BSP_SD_ReadBlocks(uint8_t *pData, uint32_t block_addr,
                                   uint32_t num_blocks, uint32_t timeout);

/**
 * @brief 写入 SD 卡扇区
 * @param pData     输入缓冲
 * @param block_addr 起始块地址
 * @param num_blocks 块数
 * @param timeout   超时 (ms)
 * @return BSP_SD_OK 或 BSP_SD_ERROR
 */
BSP_SD_Status_t BSP_SD_WriteBlocks(const uint8_t *pData, uint32_t block_addr,
                                    uint32_t num_blocks, uint32_t timeout);

/**
 * @brief 获取 SD 卡信息
 * @param info 输出卡信息
 * @return BSP_SD_OK 或 BSP_SD_ERROR
 */
BSP_SD_Status_t BSP_SD_GetCardInfo(BSP_SD_CardInfo_t *info);

/**
 * @brief 检测 SD 卡是否已初始化
 * @return 1=已初始化, 0=未初始化
 */
uint8_t BSP_SD_IsInitialized(void);

/**
 * @brief 获取 HAL SD 句柄（供 FatFS diskio 使用）
 * @return SD_HandleTypeDef 指针
 */
SD_HandleTypeDef *BSP_SD_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SD_H */
