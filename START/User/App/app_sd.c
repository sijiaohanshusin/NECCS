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
    fr = f_mount(&s_fatfs, "0:", 1u);  /* "0:" 为 FatFS 卷号，1=立即挂载 */
    printf("[SD] f_mount ret=%d\r\n", (int)fr);
    if (fr != FR_OK)
    {
        s_state = APP_SD_ERROR;  /* 挂载失败：可能是 SD 卡未插入或格式不支持 */
        return ERR_IO_FAILED;
    }

    /* Step 3: 创建工作目录 (忽略 FR_EXIST，目录已存在视为成功) */
    fr = f_mkdir("0:/NECCS");
    if ((fr != FR_OK) && (fr != FR_EXIST))
    {
        /* 目录创建失败但不影响挂载状态（如 SD 卡写保护或文件系统损坏） */
        /* [改进] 应记录错误并在 UI 显示警告 */
    }
    f_mkdir("0:/NECCS/IMG");   /* BMP 截图存放目录 */
    f_mkdir("0:/NECCS/WAV");   /* WAV 录音存放目录 */

    s_state = APP_SD_MOUNTED;                       /* 标记挂载成功 */
    s_file_seq = 0u;                                /* 文件序号从 0 开始 */
    memset(&s_cached_space, 0, sizeof(s_cached_space)); /* 清零容量缓存 */

    /* 初始刷新容量信息（挂载成功后立即获取可用空间） */
    App_SD_RefreshSpace();

    return ERR_OK;
}

void App_SD_DeInit(void)
{
    f_mount(NULL, "0:", 0u);  /* 卸载 FatFS（传 NULL 表示卸载） */
    BSP_SD_DeInit();           /* 释放 SD 卡硬件资源 */
    s_state = APP_SD_NOT_MOUNTED;  /* 重置挂载状态 */
}

App_SD_MountState_t App_SD_GetState(void)
{
    return s_state;  /* 返回当前挂载状态（NOT_MOUNTED / MOUNTED / ERROR） */
}

Err_t App_SD_GetSpace(App_SD_SpaceInfo_t *info)
{
    if (info == NULL)
    {
        return ERR_INVALID_ARG;  /* 空指针保护 */
    }

    if (s_state != APP_SD_MOUNTED)
    {
        return ERR_NOT_INIT;  /* 未挂载时不能查询容量 */
    }

    /* 返回缓存值（非阻塞，避免每次查询都执行耗时的 f_getfree） */
    *info = s_cached_space;
    return ERR_OK;
}

Err_t App_SD_RefreshSpace(void)
{
    FATFS *fs_ptr;         /* FatFS 文件系统对象指针（f_getfree 输出） */
    DWORD free_clust;      /* 可用簇数（f_getfree 输出） */
    FRESULT fr;            /* FatFS 操作返回值 */
    uint32_t sect_per_clust; /* 每簇扇区数 */
    uint32_t total_sect;     /* 总扇区数 */
    uint32_t free_sect;      /* 空闲扇区数 */

    if (s_state != APP_SD_MOUNTED)
    {
        return ERR_NOT_INIT;  /* 未挂载，无法查询容量 */
    }

    fr = f_getfree("0:", &free_clust, &fs_ptr);  /* 查询 FatFS 卷可用空间 */
    if (fr != FR_OK)
    {
        return ERR_IO_FAILED;  /* 查询失败（SD 卡通信错误等） */
    }

    sect_per_clust = (uint32_t)fs_ptr->csize;  /* 每簇扇区数（从文件系统元数据读取） */
    /* 计算总扇区数和空闲扇区数
     * FatFS 的 n_fatent 包含 2 个保留簇（簇0和簇1为系统保留），需减 2 */
    total_sect = (fs_ptr->n_fatent - 2u) * sect_per_clust;  /* 总可用扇区数 */
    free_sect  = free_clust * sect_per_clust;               /* 空闲扇区数 */

    /* 将扇区数转换为 MB
     * 换算：1MB = 2048 个 512 字节扇区（512B × 2048 = 1MB） */
    s_cached_space.total_mb = total_sect / 2048u;  /* 总容量（MB） */
    s_cached_space.free_mb  = free_sect  / 2048u;  /* 剩余空间（MB） */
    s_cached_space.used_mb  = s_cached_space.total_mb - s_cached_space.free_mb; /* 已用空间（MB） */

    return ERR_OK;
}

Err_t App_SD_MakeFilePath(const char *dir, const char *prefix,
                           const char *ext, char *out_path, uint16_t path_len)
{
    int n;  /* snprintf 返回值（写入字符数，负值表示编码错误） */

    /* 参数有效性检查：所有字符串指针非空，输出缓冲至少 32 字节 */
    if ((dir == NULL) || (prefix == NULL) || (ext == NULL) ||
        (out_path == NULL) || (path_len < 32u))
    {
        return ERR_INVALID_ARG;
    }

    s_file_seq++;  /* 文件序号单调递增，确保文件名不重复 */

    /* 生成路径格式: 0:/NECCS/{dir}/{prefix}_{000001..XXXXXX}.{ext}
     * 例如：0:/NECCS/IMG/SHOT_000042.bmp 或 0:/NECCS/WAV/REC_000001.wav */
    n = snprintf(out_path, path_len, "0:/NECCS/%s/%s_%06lu.%s",
                 dir, prefix, (unsigned long)s_file_seq, ext);
    if ((n < 0) || (n >= (int)path_len))
    {
        /* n < 0：字符串编码错误；n >= path_len：缓冲区截断 */
        return ERR_INVALID_ARG;
    }

    return ERR_OK;
}
