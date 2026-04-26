/**
 * @file    app_sd.c
 * @brief   SD 卡应用层抽象实现
 * @details 负责 FatFS 挂载、目录创建和容量查询。
 *          所有文件 I/O (BMP/WAV) 由 app_storage_task 内部完成。
 */
#include "app_sd.h"
#include "SD/bsp_sd.h"
#include "ff.h"

#include <string.h>
#include <stdio.h>

/* ========== 模块状态 ========== */

/** @brief 挂载状态 */
static volatile App_SD_MountState_t s_state = APP_SD_NOT_MOUNTED;

/** @brief FatFS 文件系统对象 */
static FATFS s_fatfs;

/** @brief 文件序号计数器 (全局递增) */
static uint32_t s_file_seq = 0u;

/** @brief 缓存的容量信息 (由 RefreshSpace 更新) */
static App_SD_SpaceInfo_t s_cached_space;

/* ========== 公开 API ========== */

Err_t App_SD_Init(void)
{
    BSP_SD_Status_t sd_ret;
    FRESULT fr;

    /* 如果之前曾初始化失败, 先清理再重试 */
    if (s_state == APP_SD_ERROR)
    {
        App_SD_DeInit();
    }

    /* 已挂载 — 直接返回成功 */
    if (s_state == APP_SD_MOUNTED)
    {
        return ERR_OK;
    }

    /* Step 1: 初始化 SD 卡硬件 */
    sd_ret = BSP_SD_Init();
    printf("[SD] BSP_SD_Init ret=%d\r\n", (int)sd_ret);
    if (sd_ret != BSP_SD_OK)
    {
        s_state = APP_SD_ERROR;
        return ERR_IO_FAILED;
    }

    /* Step 2: 挂载 FatFS (会触发 disk_initialize → BSP_SD_Init, 由 init guard 跳过) */
    fr = f_mount(&s_fatfs, "0:", 1u);
    printf("[SD] f_mount ret=%d\r\n", (int)fr);
    if (fr != FR_OK)
    {
        s_state = APP_SD_ERROR;
        return ERR_IO_FAILED;
    }

    /* Step 3: 创建工作目录 (忽略 FR_EXIST) */
    fr = f_mkdir("0:/NECCS");
    if ((fr != FR_OK) && (fr != FR_EXIST))
    {
        /* 目录创建失败但不影响挂载状态 */
    }
    f_mkdir("0:/NECCS/IMG");
    f_mkdir("0:/NECCS/WAV");

    s_state = APP_SD_MOUNTED;
    s_file_seq = 0u;
    memset(&s_cached_space, 0, sizeof(s_cached_space));

    /* 初始刷新容量信息 */
    App_SD_RefreshSpace();

    return ERR_OK;
}

void App_SD_DeInit(void)
{
    f_mount(NULL, "0:", 0u);
    BSP_SD_DeInit();
    s_state = APP_SD_NOT_MOUNTED;
}

App_SD_MountState_t App_SD_GetState(void)
{
    return s_state;
}

Err_t App_SD_GetSpace(App_SD_SpaceInfo_t *info)
{
    if (info == NULL)
    {
        return ERR_INVALID_ARG;
    }

    if (s_state != APP_SD_MOUNTED)
    {
        return ERR_NOT_INIT;
    }

    /* 返回缓存值 — 非阻塞 */
    *info = s_cached_space;
    return ERR_OK;
}

Err_t App_SD_RefreshSpace(void)
{
    FATFS *fs_ptr;
    DWORD free_clust;
    FRESULT fr;
    uint32_t sect_per_clust;
    uint32_t total_sect;
    uint32_t free_sect;

    if (s_state != APP_SD_MOUNTED)
    {
        return ERR_NOT_INIT;
    }

    fr = f_getfree("0:", &free_clust, &fs_ptr);
    if (fr != FR_OK)
    {
        return ERR_IO_FAILED;
    }

    sect_per_clust = (uint32_t)fs_ptr->csize;
    total_sect = (fs_ptr->n_fatent - 2u) * sect_per_clust;
    free_sect  = free_clust * sect_per_clust;

    /* 转换为 MB (512 字节/扇区) */
    s_cached_space.total_mb = total_sect / 2048u;
    s_cached_space.free_mb  = free_sect  / 2048u;
    s_cached_space.used_mb  = s_cached_space.total_mb - s_cached_space.free_mb;

    return ERR_OK;
}

Err_t App_SD_MakeFilePath(const char *dir, const char *prefix,
                           const char *ext, char *out_path, uint16_t path_len)
{
    int n;

    if ((dir == NULL) || (prefix == NULL) || (ext == NULL) ||
        (out_path == NULL) || (path_len < 32u))
    {
        return ERR_INVALID_ARG;
    }

    s_file_seq++;

    /* 生成路径: 0:/NECCS/dir/prefix_XXXXXX.ext */
    n = snprintf(out_path, path_len, "0:/NECCS/%s/%s_%06lu.%s",
                 dir, prefix, (unsigned long)s_file_seq, ext);
    if ((n < 0) || (n >= (int)path_len))
    {
        return ERR_INVALID_ARG;
    }

    return ERR_OK;
}
