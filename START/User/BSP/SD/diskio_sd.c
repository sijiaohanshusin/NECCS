/**
 * @file    diskio_sd.c
 * @brief   FatFS diskio backend for SDMMC1 (NECCS project)
 * @details Maps FatFS disk_* API to BSP_SD_* layer.
 *          Single drive (pdrv 0 = SD card).
 *          HAL_SD polling mode uses CPU FIFO — no IDMA, no cache coherency
 *          hazard on the data path (verified from HAL source).
 */
#include "diskio.h"
#include "SD/bsp_sd.h"

/*---------------------------------------------------------------------------*/
/* Drive mapping                                                             */
/*---------------------------------------------------------------------------*/
#define SD_CARD     0u

/*---------------------------------------------------------------------------*/
/* diskio interface                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief  Get disk status
 */
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SD_CARD)
    {
        return STA_NOINIT;
    }

    if (!BSP_SD_IsInitialized())
    {
        return STA_NOINIT;
    }

    return 0u;  /* OK */
}

/**
 * @brief  Initialize disk drive
 */
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != SD_CARD)
    {
        return STA_NOINIT;
    }

    if (BSP_SD_Init() != BSP_SD_OK)
    {
        return STA_NOINIT;
    }

    return 0u;  /* OK */
}

/**
 * @brief  Read sectors
 * @note   HAL_SD_ReadBlocks uses CPU FIFO polling — buffer can be anywhere,
 *         no cache ops needed (no DMA master touches the buffer).
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    BSP_SD_Status_t res;

    if (pdrv != SD_CARD || !count)
    {
        return RES_PARERR;
    }

    res = BSP_SD_ReadBlocks(buff, (uint32_t)sector, (uint32_t)count, 5000u);

    return (res == BSP_SD_OK) ? RES_OK : RES_ERROR;
}

/**
 * @brief  Write sectors
 * @note   Same polling-mode rationale — no cache ops needed.
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    BSP_SD_Status_t res;

    if (pdrv != SD_CARD || !count)
    {
        return RES_PARERR;
    }

    res = BSP_SD_WriteBlocks(buff, (uint32_t)sector, (uint32_t)count, 5000u);

    return (res == BSP_SD_OK) ? RES_OK : RES_ERROR;
}

/**
 * @brief  Disk I/O control
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    BSP_SD_CardInfo_t info;

    if (pdrv != SD_CARD)
    {
        return RES_PARERR;
    }

    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(DWORD *)buff = 512u;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (BSP_SD_GetCardInfo(&info) != BSP_SD_OK)
            {
                return RES_ERROR;
            }
            *(DWORD *)buff = (info.block_size > 0u) ? (info.block_size / 512u) : 1u;
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (BSP_SD_GetCardInfo(&info) != BSP_SD_OK)
            {
                return RES_ERROR;
            }
            *(LBA_t *)buff = (LBA_t)info.block_count;
            return RES_OK;

        default:
            break;
    }

    return RES_PARERR;
}
