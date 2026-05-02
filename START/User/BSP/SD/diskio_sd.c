/**
 * @file    diskio_sd.c
 * @brief   FatFS diskio backend for SDMMC1 (NECCS project)
 * @details Maps FatFS disk_* API to BSP_SD_* layer.
 *          Single drive (pdrv 0 = SD card).
 *          HAL_SD polling mode uses CPU FIFO — no IDMA, no cache coherency
 *          hazard on the data path (verified from HAL source).
 *
 *          映射关系：
 *          - disk_initialize -> BSP_SD_Init
 *          - disk_status     -> BSP_SD_IsInitialized
 *          - disk_read/write -> BSP_SD_ReadBlocks/WriteBlocks
 *          - disk_ioctl      -> BSP_SD_GetCardInfo + 常量返回
 *
 * @note    [注意] 当前仅支持单盘符 pdrv=0；多盘符扩展需增加驱动映射表。
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
    /* FatFS 先通过 pdrv 选择物理盘，本工程只挂接 SD_CARD(0)。 */
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

    /* 由 BSP 层完成 GPIO/时钟/卡识别与总线宽度配置。 */
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

    /* FatFS sector 即逻辑块地址（LBA），底层按 512B 扇区传输。 */
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
            /* BSP_SD_* 当前为阻塞式读写，返回时传输已完成。 */
            return RES_OK;

        case GET_SECTOR_SIZE:
            /* SD/FatFS 典型逻辑扇区固定 512B。 */
            *(DWORD *)buff = 512u;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (BSP_SD_GetCardInfo(&info) != BSP_SD_OK)
            {
                return RES_ERROR;
            }
            /* 返回以 512B 扇区为单位的擦除块大小。 */
            *(DWORD *)buff = (info.block_size > 0u) ? (info.block_size / 512u) : 1u;
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (BSP_SD_GetCardInfo(&info) != BSP_SD_OK)
            {
                return RES_ERROR;
            }
            /* 总逻辑扇区数直接来自 HAL 的 LogBlockNbr。 */
            *(LBA_t *)buff = (LBA_t)info.block_count;
            return RES_OK;

        default:
            break;
    }

    return RES_PARERR;
}
