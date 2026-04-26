/**
 * @file    app_storage_task.c
 * @brief   SD 卡异步存储任务实现（BMP 截图 + WAV 录音 FatFS I/O）
 * @details FreeRTOS 任务，负责 BMP 截图和 WAV 录音的全部文件 I/O 操作。
 *          所有 FatFS FIL 调用（f_open / f_write / f_close / f_lseek）
 *          集中在本任务中执行，其他任务通过命令队列（s_cmd_queue）发送请求。
 *
 * 设计决策（来自 T2 团队讨论）：
 * - FIL 句柄设为 static（s_fil），不在栈上分配，节省约 600 字节 FreeRTOS 栈空间
 * - BMP 截图：单次 f_open → 写 BMP 头 → 逐行 bottom-up 写入 → f_close（不重复 open/close）
 * - WAV 录音：open → 写初始头（data_size=0）→ 循环定期 flush → 停止时 seek(0) → 回填完整头 → close
 * - 截图期间启用 g_display_swap_inhibit 防止 LTDC 切换前后缓冲导致图像撕裂
 *
 * BMP 格式说明：
 *   使用 RGB565（16bpp）格式，非标准的 BI_BITFIELDS 压缩类型（compress=3），
 *   头总大小 = 14（FileHeader）+ 40（InfoHeader）+ 12（Color Masks）= 66字节。
 *   注意：BMP 规范要求 bottom-up 行序（即行 599 先写，行 0 最后写）。
 */
#include "app_storage_task.h"           /* 本模块公开接口（命令类型、状态枚举、API）*/
#include "app_sd.h"                     /* SD 卡状态查询（App_SD_GetState）、路径生成（App_SD_MakeFilePath）、容量刷新 */
#include "app_capture.h"               /* 截图状态管理（App_Capture_SetState、App_Capture_IncrementCount）*/
#include "app_recorder.h"              /* WAV 录音引擎（App_Recorder_Start/Stop/Feed/FlushPending/FillWAVHeader）*/
#include "ff.h"                         /* FatFS 文件系统 API（f_open / f_write / f_close / f_lseek 等）*/
#include "LCD/ltdc.h"                   /* LTDC 前缓冲地址查询（ltdc_get_frontbuf_addr）*/

#include "FreeRTOS.h"                   /* FreeRTOS 基础（xTaskCreate、vTaskDelay、pdMS_TO_TICKS 等）*/
#include "task.h"                       /* FreeRTOS 任务 API（xTaskGetTickCount、TaskHandle_t）*/
#include "queue.h"                      /* FreeRTOS 队列 API（xQueueCreate、xQueueReceive、xQueueSend）*/

#include <string.h>                     /* memset、memcpy（用于 BMP 头填充和像素行复制）*/

/* ========== 任务参数 ========== */

#define STORAGE_TASK_STACK_WORDS   2048u    /* 任务栈大小（单位: 字 = 4字节），共 8192 字节；
                                             * BMP 截图时会分配 s_row_buf(2048B) + 局部变量，
                                             * 确保有足够栈空间 */
#define STORAGE_TASK_PRIO          2u       /**< 任务优先级：osPriorityBelowNormal（低于音频 prio=4 和 UI）*/
                                             /* [注意] 故意低于音频和 UI 任务，保证 SD 写入不抢占实时处理 */
#define STORAGE_CMD_QUEUE_DEPTH    8u       /* 命令队列深度：最多缓存 8 条待处理命令 */
                                             /* [改进] 深度 8 主要防止 UI 快速连点截图时队列满；
                                             *         若录音和截图并发操作合法化，需要更大队列 */
#define STORAGE_FLUSH_INTERVAL_MS  20u      /**< 录音期间 flush 环形缓冲的轮询间隔（毫秒）*/
                                             /* 20ms flush 间隔对应约 48kHz × 0.02s = 960 采样点的数据积压上限 */
#define STORAGE_SPACE_REFRESH_MS   5000u    /**< SD 卡剩余容量信息刷新间隔（毫秒，5秒一次）*/
                                             /* [改进] f_getfree 可能阻塞 50-200ms，已避开录音期间调用 */
#define STORAGE_SD_CHECK_MS        2000u    /**< SD 卡健康检查间隔（毫秒，2秒一次）*/
                                             /* 检查包括 SD 未挂载时自动重新初始化 */

/* ========== BMP 文件格式 ========== */

#define BMP_WIDTH          1024u            /* BMP 图像宽度（像素），与显示屏分辨率对应 */
#define BMP_HEIGHT         600u             /* BMP 图像高度（像素），与显示屏分辨率对应 */
#define BMP_BPP            2u               /* 每像素字节数：RGB565 = 16bit = 2字节 */
#define BMP_ROW_BYTES      (BMP_WIDTH * BMP_BPP)   /* 每行字节数 = 1024 × 2 = 2048 字节 */
#define BMP_PIXEL_SIZE     (BMP_WIDTH * BMP_HEIGHT * BMP_BPP)  /* 像素数据总字节数 = 1024×600×2 = 1,228,800 字节（约 1.17MB）*/

/** BMP 完整头 = FileHeader(14) + InfoHeader(40) + RGB565 Color Masks(12) = 66 字节 */
/** [注意] 标准 BMP 头通常 54 字节，但 RGB565（BI_BITFIELDS）需额外 12 字节颜色掩码 */
#define BMP_HEADER_SIZE    66u

#include "app_display.h"                   /* g_display_swap_inhibit：禁止 LTDC 交换缓冲（防撕裂）*/

/* ========== 模块状态 ========== */

static volatile App_StorageState_e s_state = STORAGE_STATE_IDLE;  /* 当前存储状态（volatile：多任务可见）*/

/** @brief 命令队列句柄（由 App_Storage_Init 创建，队列深度 STORAGE_CMD_QUEUE_DEPTH = 8）*/
static QueueHandle_t s_cmd_queue = NULL;   /* 初始为 NULL，Init 后才有效；为 NULL 时 SendCmd 返回 ERR_NOT_INIT */

/** @brief Storage_Task 任务句柄（可用于调试器查看任务状态，当前未使用于业务逻辑）*/
static TaskHandle_t s_task_handle = NULL;  /* [改进] 可在关键错误时通过 vTaskDelete(s_task_handle) 回收资源 */

/** @brief 共享 FIL 句柄（BMP 截图和 WAV 录音共用，同一时间只能进行一种操作）*/
static FIL s_fil;                          /* [注意] BMP 和 WAV 不能并发 —— B1 fix 已在业务逻辑中保护 */

/** @brief 录音活跃标志（1=录音进行中，需要定期 flush；0=空闲）*/
static uint8_t s_recording_active = 0u;   /* 保护 s_fil 句柄被截图操作意外覆盖 */

/** @brief BMP 行缓冲：一次写入一行像素（1024 × 2 = 2048 字节）*/
static uint8_t s_row_buf[BMP_ROW_BYTES];  /* [注意] static 防止放在栈上；栈上 2048B 会消耗大量任务栈空间 */

/* ========== BMP 头填充 ========== */

/**
 * @brief   填充 66 字节 BMP 文件头（BI_BITFIELDS, RGB565, 1024×600, bottom-up）
 * @details 手动按小端字节序（ARM Cortex-M7 为小端）填写 BMP 二进制格式字段。
 *
 * BMP 文件头布局（本函数填充后）：
 *   偏移  0- 1:  "BM"（文件类型标识）
 *   偏移  2- 5:  文件总大小（字节）= BMP_HEADER_SIZE + BMP_PIXEL_SIZE = 1,228,866
 *   偏移  6- 9:  保留字段（固定 0）
 *   偏移 10-13:  像素数据偏移 = BMP_HEADER_SIZE = 66
 *   偏移 14-17:  信息头大小（固定 40）
 *   偏移 18-21:  图像宽度（像素）= 1024
 *   偏移 22-25:  图像高度（像素）= 600（正值表示 bottom-up 行序）
 *   偏移 26-27:  颜色平面数（固定 1）
 *   偏移 28-29:  位深（16 = RGB565）
 *   偏移 30-33:  压缩类型（3 = BI_BITFIELDS，表示使用位掩码描述颜色分量）
 *   偏移 34-37:  像素数据大小 = BMP_PIXEL_SIZE
 *   偏移 38-53:  分辨率/颜色表（填 0 即可，大多数图像查看器可接受）
 *   偏移 54-57:  红色分量掩码  = 0x0000F800（RGB565 高 5 位）
 *   偏移 58-61:  绿色分量掩码  = 0x000007E0（RGB565 中 6 位）
 *   偏移 62-65:  蓝色分量掩码  = 0x0000001F（RGB565 低 5 位）
 *
 * @param   buf  输出缓冲区，大小必须 >= BMP_HEADER_SIZE（66 字节）
 */
static void s_fill_bmp_header(uint8_t *buf)
{
    memset(buf, 0, BMP_HEADER_SIZE);          /* 先全部清零，设置默认保留字段为 0 */

    /* ---- File Header (14 bytes, 偏移 0) ---- */
    buf[0] = 'B'; buf[1] = 'M';               /* 魔数：标识这是 BMP 文件 */
    {
        uint32_t file_size = BMP_HEADER_SIZE + BMP_PIXEL_SIZE;  /* 计算文件总字节数 */
        memcpy(&buf[2], &file_size, 4u);       /* 写入小端 4 字节文件大小 */
    }
    {
        uint32_t offset = BMP_HEADER_SIZE;     /* 像素数据在文件中的起始偏移（66 字节头之后）*/
        memcpy(&buf[10], &offset, 4u);         /* 写入 bfOffBits 字段 */
    }

    /* ---- Info Header (40 bytes, 偏移 14) ---- */
    {
        uint32_t info_size = 40u;              /* BITMAPINFOHEADER 固定大小为 40 字节 */
        int32_t  width     = (int32_t)BMP_WIDTH;    /* 图像宽度（有符号）*/
        int32_t  height    = (int32_t)BMP_HEIGHT;   /* 图像高度（正值→ bottom-up 行序，BMP 规范）*/
        uint16_t planes    = 1u;               /* 颜色平面数：固定为 1 */
        uint16_t bpp       = 16u;              /* 每像素位数：16 = RGB565 */
        uint32_t compress  = 3u;              /* 压缩类型：3 = BI_BITFIELDS（使用颜色掩码）*/
        uint32_t img_size  = BMP_PIXEL_SIZE;  /* 像素数据总字节数（不含头）*/

        memcpy(&buf[14], &info_size, 4u);      /* biSize：信息头自身大小 */
        memcpy(&buf[18], &width, 4u);          /* biWidth：图像宽度 */
        memcpy(&buf[22], &height, 4u);         /* biHeight：图像高度（正值=bottom-up）*/
        memcpy(&buf[26], &planes, 2u);         /* biPlanes：颜色平面数 */
        memcpy(&buf[28], &bpp, 2u);            /* biBitCount：位深 */
        memcpy(&buf[30], &compress, 4u);       /* biCompression：BI_BITFIELDS */
        memcpy(&buf[34], &img_size, 4u);       /* biSizeImage：像素数据大小 */
        /* 偏移 38-53（biXPelsPerMeter 等）已被 memset 清零，查看器通常可接受 */
    }

    /* ---- RGB565 Color Masks (12 bytes, 偏移 54) ---- */
    /* BI_BITFIELDS 格式需要在头中紧跟 InfoHeader 附上三个 32 位颜色掩码 */
    {
        uint32_t r = 0x0000F800u;             /* 红色掩码：RGB565 版中的高 5 位（bits 15:11）*/
        uint32_t g = 0x000007E0u;             /* 绿色掩码：RGB565 中的中 6 位（bits 10:5）*/
        uint32_t b = 0x0000001Fu;             /* 蓝色掩码：RGB565 中的低 5 位（bits 4:0）*/

        memcpy(&buf[54], &r, 4u);             /* 写入红色分量掩码 */
        memcpy(&buf[58], &g, 4u);             /* 写入绿色分量掩码 */
        memcpy(&buf[62], &b, 4u);             /* 写入蓝色分量掩码 */
    }
}

/* ========== BMP 截图处理 ========== */

/**
 * @brief   执行 BMP 截图：读取 LTDC 前缓冲区 → 写入 SD 卡 BMP 文件
 * @details 完整流程：
 *          1. 检查录音互斥（B1-fix）
 *          2. 检查 SD 已挂载
 *          3. 生成唯一文件路径（IMG_00001.bmp 等）
 *          4. 打开文件，写入 66 字节 BMP 头
 *          5. 拉高 swap_inhibit 防止 LTDC 双缓冲切换
 *          6. 从 LTDC 前缓冲地址按 BMP bottom-up 顺序逐行读取像素写入文件
 *          7. 释放 swap_inhibit，关闭文件
 * @return  ERR_OK       截图成功
 * @return  ERR_BUSY     录音进行中，无法同时截图（共用 s_fil 句柄）
 * @return  ERR_NOT_INIT SD 卡未挂载
 * @return  ERR_IO_FAILED FatFS 写入失败
 */
static Err_t s_handle_capture_bmp(void)
{
    char filepath[64];                     /* 文件路径缓冲区（如 "0:/IMG/IMG_00001.bmp"）*/
    uint8_t bmp_header[BMP_HEADER_SIZE];  /* BMP 头临时缓冲区（66 字节，在栈上）*/
    FRESULT fr;                            /* FatFS 函数返回值 */
    UINT bw;                               /* f_write 实际写入字节数（用于验证是否完整写入）*/
    uint32_t fb_addr;                      /* LTDC 前缓冲区的起始物理地址（SDRAM 地址）*/
    const uint16_t *fb_pixels;            /* 指向前缓冲区像素数组的 RGB565 指针 */
    uint32_t row;                          /* 循环变量：当前写入的 BMP 行号（0=最底行）*/
    Err_t ret;                             /* 中间操作的错误码临时变量 */

    /* B1 fix: 录音期间不可截图，因为两者共用 s_fil 句柄（同时 open 会损坏文件） */
    if (s_recording_active != 0u)
    {
        App_Capture_SetState(CAPTURE_ERROR);  /* 通知截图模块：本次截图失败 */
        return ERR_BUSY;                       /* 调用方收到 ERR_BUSY 可提示用户"录音中无法截图" */
    }

    App_Capture_SetState(CAPTURE_BUSY);        /* 截图开始：标记为忙碌状态（UI 可据此显示进度）*/
    s_state = STORAGE_STATE_CAPTURING;         /* 更新存储任务全局状态 */

    /* 检查 SD 卡是否就绪（未插卡或初始化失败时 App_SD_GetState() 返回非 MOUNTED）*/
    if (App_SD_GetState() != APP_SD_MOUNTED)
    {
        App_Capture_SetState(CAPTURE_ERROR);   /* 通知截图模块：SD 未就绪 */
        s_state = STORAGE_STATE_ERROR;         /* 标记存储任务为错误状态 */
        return ERR_NOT_INIT;                    /* SD 未挂载，视为"未初始化"错误 */
    }

    /* 生成唯一文件路径（格式："0:/IMG/IMG_00001.bmp"，序号自动递增）*/
    ret = App_SD_MakeFilePath("IMG", "IMG", "bmp", filepath, sizeof(filepath));
    if (ret != ERR_OK)
    {
        App_Capture_SetState(CAPTURE_ERROR);   /* 路径生成失败（如 SD 已满序号溢出）*/
        s_state = STORAGE_STATE_ERROR;
        return ret;                             /* 将 App_SD_MakeFilePath 的错误码直接透传 */
    }

    /* 打开文件（FA_CREATE_ALWAYS：若存在则截断重写，保证文件内容是完整的 BMP）*/
    fr = f_open(&s_fil, filepath, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        App_Capture_SetState(CAPTURE_ERROR);
        s_state = STORAGE_STATE_ERROR;
        return ERR_IO_FAILED;                   /* f_open 失败（SD 写保护 / 目录不存在等）*/
    }

    /* 构造并写入 BMP 头（66 字节）*/
    s_fill_bmp_header(bmp_header);             /* 填充 FileHeader + InfoHeader + RGB565 Masks */
    fr = f_write(&s_fil, bmp_header, BMP_HEADER_SIZE, &bw);  /* 写入头部 */
    if ((fr != FR_OK) || (bw != BMP_HEADER_SIZE))             /* 验证全部 66 字节写入成功 */
    {
        f_close(&s_fil);                        /* 写头失败：关闭文件（即使残缺也需释放句柄）*/
        App_Capture_SetState(CAPTURE_ERROR);
        s_state = STORAGE_STATE_ERROR;
        return ERR_IO_FAILED;
    }

    /* 启用 swap-inhibit：阻止 LTDC vsync 回调切换前后缓冲，防止读取时图像撕裂 */
    /* 注意：此标志必须在 ltdc_get_frontbuf_addr() 之前设置，否则地址和内容可能不一致 */
    g_display_swap_inhibit = 1u;

    fb_addr = ltdc_get_frontbuf_addr();        /* 获取当前 LTDC 正在显示的前缓冲区地址（SDRAM 中）*/
    fb_pixels = (const uint16_t *)fb_addr;    /* 转换为 RGB565 像素指针（每个元素 2 字节）*/

    /* BMP bottom-up 行序：row 0 = 图像最底行（LTDC 行 599），row 599 = 图像最顶行（LTDC 行 0）*/
    for (row = 0u; row < BMP_HEIGHT; row++)
    {
        /* 计算本 BMP 行对应的 LTDC 源行号：BMP 行 0 → LTDC 行 599，BMP 行 599 → LTDC 行 0 */
        uint32_t src_row = (BMP_HEIGHT - 1u) - row;

        /* 源行像素起始地址（偏移 = src_row × 1024 像素 × 2 字节/像素）*/
        const uint16_t *src = &fb_pixels[src_row * BMP_WIDTH];

        /* 将一行像素（2048 字节）复制到行缓冲区（避免 f_write 直接读 SDRAM 引起缓存问题）*/
        memcpy(s_row_buf, src, BMP_ROW_BYTES);

        /* 写入一行像素数据到文件 */
        fr = f_write(&s_fil, s_row_buf, BMP_ROW_BYTES, &bw);
        if ((fr != FR_OK) || (bw != BMP_ROW_BYTES))   /* 验证 2048 字节完整写入 */
        {
            g_display_swap_inhibit = 0u;                /* 写失败：先释放 swap 锁（否则 UI 卡死）*/
            f_close(&s_fil);                            /* 关闭文件（文件不完整，无法播放，但句柄需释放）*/
            App_Capture_SetState(CAPTURE_ERROR);
            s_state = STORAGE_STATE_ERROR;
            return ERR_IO_FAILED;                        /* [注意] 已写入的部分行留在文件中，文件损坏 */
        }
    }

    g_display_swap_inhibit = 0u;               /* 释放 swap 锁：允许 LTDC 恢复帧缓冲切换 */
    f_close(&s_fil);                           /* 关闭文件（刷新 FatFS 缓存，写入 FAT 目录表）*/

    App_Capture_IncrementCount();              /* 递增截图计数（UI 或 CLI 可查询总截图数）*/
    App_Capture_SetState(CAPTURE_DONE);        /* 通知截图模块：截图完成（UI 可显示完成提示）*/
    s_state = STORAGE_STATE_IDLE;              /* 恢复空闲状态，可以接受下一次操作 */

    return ERR_OK;
}

/* ========== WAV 录音处理 ========== */

/**
 * @brief   开始 WAV 录音：打开文件并写入初始文件头
 * @details 写入 44 字节初始 WAV 头（data chunk 大小字段为 0），
 *          然后初始化录音模块并开始喂入 PCM 数据。
 *          实际的 data_size 在停止录音时通过 f_lseek(0) + 回填头的方式写入。
 * @param   mode_param  App_RecorderMode_t 枚举值转 uint32_t：
 *                      RECORDER_MODE_RAW16 = 16 通道原始数据，
 *                      其他值 = 1 通道（DAS 波束成形后的单声道）
 * @return  ERR_OK       录音已开始
 * @return  ERR_BUSY     已在录音中（s_recording_active = 1，防止重复开始）
 * @return  ERR_NOT_INIT SD 卡未挂载
 * @return  ERR_IO_FAILED 文件创建或头写入失败
 */
static Err_t s_handle_rec_start(uint32_t mode_param)
{
    char filepath[64];                        /* WAV 文件路径缓冲区（如 "0:/WAV/REC_00001.wav"）*/
    uint8_t wav_header[44];                  /* WAV 头临时缓冲区（RIFF + fmt + data 共 44 字节）*/
    FRESULT fr;                               /* FatFS 返回值 */
    UINT bw;                                  /* f_write 实际写入字节数 */
    Err_t ret;                                /* 中间操作错误码 */
    App_RecorderMode_t mode = (App_RecorderMode_t)mode_param;  /* 将参数还原为录音模式枚举 */
    uint16_t num_ch = (mode == RECORDER_MODE_RAW16) ? 16u : 1u;  /* 按模式确定通道数 */
    /* [注意] RECORDER_MODE_RAW16 = 16 通道（原始 TDM 数据），其他模式 = 1 通道（DAS 单声道）*/

    /* H1 fix: 防止重复开始录音（s_fil 句柄已被占用时再次 open 会导致内存泄漏）*/
    if (s_recording_active != 0u)
    {
        return ERR_BUSY;                       /* 告知调用方当前正在录音，不能再次 start */
    }

    if (App_SD_GetState() != APP_SD_MOUNTED)   /* 检查 SD 卡是否已挂载 */
    {
        return ERR_NOT_INIT;
    }

    /* 生成唯一 WAV 文件路径（格式："0:/WAV/REC_00001.wav"）*/
    ret = App_SD_MakeFilePath("WAV", "REC", "wav", filepath, sizeof(filepath));
    if (ret != ERR_OK)
    {
        return ret;                             /* 路径生成失败（如目录创建失败）透传错误 */
    }

    /* 创建并打开 WAV 文件（FA_CREATE_ALWAYS：若已存在则覆盖，保证文件干净）*/
    fr = f_open(&s_fil, filepath, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        return ERR_IO_FAILED;                   /* 文件创建失败（SD 满、目录层级错误等）*/
    }

    /* 写入初始 WAV 头（data chunk 大小字段 = 0，停止录音后回填真实大小）*/
    App_Recorder_FillWAVHeader(wav_header, num_ch, 0u);  /* 生成 44 字节头，data_size=0 */
    fr = f_write(&s_fil, wav_header, 44u, &bw);          /* 写入头到文件 */
    if ((fr != FR_OK) || (bw != 44u))                     /* 44 字节必须全部写入 */
    {
        f_close(&s_fil);                        /* 写头失败：清理文件句柄 */
        return ERR_IO_FAILED;
    }

    /* 初始化录音模块（分配/重置内部环形缓冲区、通道配置、字节计数清零）*/
    ret = App_Recorder_Start(mode);
    if (ret != ERR_OK)
    {
        f_close(&s_fil);                        /* App_Recorder_Start 失败：不录音，关闭文件 */
        return ret;
    }

    s_recording_active = 1u;                    /* 标记录音活跃（Storage_Task 主循环据此定期 flush）*/
    s_state = STORAGE_STATE_RECORDING;          /* 更新全局状态供外部查询（UI 状态显示）*/
    return ERR_OK;
}

/**
 * @brief   停止 WAV 录音：刷出残余数据 → 回填真实 WAV 头 → 关闭文件
 * @details 录音停止顺序（顺序重要，不可颠倒）：
 *          1. App_Recorder_FlushPending()：将环形缓冲区中尚未写入 SD 的残余数据写出
 *          2. App_Recorder_GetDataBytesWritten()：获取已写入总字节数（含本次 flush）
 *          3. App_Recorder_FillWAVHeader()：用真实 data_size 生成完整 WAV 头
 *          4. f_lseek(0) + f_write()：跳回文件头位置，覆盖写入完整 WAV 头
 *          5. f_close()：关闭文件，FatFS 刷新 FAT 目录表
 * @return  ERR_OK  录音已停止（即使 WAV 头回填失败，也尽量关闭文件）
 * @note    H2-fix：WAV 头回填失败时文件可能无法正常播放，
 *          但文件已关闭（不阻塞后续操作）。
 */
static Err_t s_handle_rec_stop(void)
{
    uint8_t wav_header[44];                   /* WAV 头缓冲区（含真实 data_size 的完整 WAV 头）*/
    FRESULT fr;                                /* FatFS 返回值 */
    UINT bw;                                   /* f_write 实际写入字节数 */
    uint32_t data_bytes;                       /* 录音数据总字节数（用于回填 WAV 头）*/
    uint16_t num_ch;                           /* 录音通道数（1 或 16，从录音模块获取）*/

    if (s_recording_active == 0u)              /* 如果当前未在录音，直接返回成功（幂等操作）*/
    {
        return ERR_OK;
    }

    /* 步骤 1：将 App_Recorder 环形缓冲中未写出的残余 PCM 数据刷入 SD 卡 */
    App_Recorder_FlushPending(&s_fil);         /* 将缓冲区尾部数据（<一个完整块）写到文件 */

    /* 步骤 2：查询实际写入的数据字节总数（不含 44 字节头，仅 pcm data 净数据）*/
    data_bytes = App_Recorder_GetDataBytesWritten();  /* 用于填入 WAV data chunk 大小字段 */

    /* 步骤 3：生成含真实 data_size 的完整 WAV 头 */
    num_ch = App_Recorder_GetNumChannels();           /* 获取录音时的通道数（1 或 16）*/
    App_Recorder_FillWAVHeader(wav_header, num_ch, data_bytes);  /* 填充 44 字节完整 WAV 头 */

    /* 步骤 4：跳回文件起始位置并覆盖写入完整 WAV 头 */
    fr = f_lseek(&s_fil, 0u);                 /* 将文件读写指针移到偏移 0（文件起始处）*/
    if (fr == FR_OK)
    {
        fr = f_write(&s_fil, wav_header, 44u, &bw);   /* 覆盖写入完整头（含正确 data_size）*/
        if ((fr != FR_OK) || (bw != 44u))              /* H2 fix: 回填失败的处理 */
        {
            /* [注意] 头回填失败：WAV 文件会被大多数播放器拒绝（因为 data_size=0 或不一致）
             *         但仍需继续执行 f_close 释放文件句柄，否则后续无法新建文件 */
        }
    }
    /* f_lseek 失败时也继续执行关闭（不因 seek 失败而永久持有文件句柄）*/

    /* 步骤 5：关闭文件（FatFS 将未写缓冲区刷到 SD，并更新 FAT 目录表记录文件大小）*/
    f_close(&s_fil);

    /* 步骤 6：重置录音状态 */
    App_Recorder_Stop();                       /* 停止录音模块（重置内部状态，释放 DMA 依赖等）*/
    s_recording_active = 0u;                   /* 清除录音标志（Storage_Task 主循环停止定期 flush）*/
    s_state = STORAGE_STATE_IDLE;              /* 恢复 IDLE 状态（即使头回填失败也恢复，文件已关闭）*/

    /* 刷新 SD 卡剩余容量信息（录音结束后立即更新，方便 UI 显示新的剩余空间）*/
    App_SD_RefreshSpace();

    return ERR_OK;
}

/* ========== 任务主体 ========== */

/**
 * @brief   Storage_Task FreeRTOS 任务入口函数
 * @details 主循环逻辑：
 *          - 空闲时：以 2000ms 超时阻塞在 xQueueReceive，等待命令或执行 SD 健康检查
 *          - 录音中：以 20ms 超时阻塞，每 20ms 定期执行 App_Recorder_FlushPending
 *          - 收到命令后：先检查 SD 状态（自动恢复），再按 switch 分发执行
 * @param   pvParameters  FreeRTOS 任务参数（未使用）
 */
static void Storage_Task(void *pvParameters)
{
    App_StorageMsg_t msg;                      /* 从命令队列接收到的消息 */
    TickType_t wait_ticks;                     /* 本次 xQueueReceive 等待的 tick 数 */
    uint32_t space_refresh_tick = 0u;          /* 上次容量刷新的 tick 时间戳 */
    uint32_t sd_check_tick = 0u;               /* 上次 SD 健康检查的 tick 时间戳 */

    (void)pvParameters;                        /* 未使用参数，显式转换消除 -Wunused-parameter 警告 */

    for (;;)                                   /* 任务主循环（永不退出）*/
    {
        /* 动态调整超时：录音期间短超时（20ms）确保定期 flush；空闲时长等待（2s）省 CPU */
        wait_ticks = (s_recording_active != 0u) ?
                     pdMS_TO_TICKS(STORAGE_FLUSH_INTERVAL_MS) :   /* 录音中：20ms timeout */
                     pdMS_TO_TICKS(STORAGE_SD_CHECK_MS);           /* 空闲中：2000ms timeout */

        if (xQueueReceive(s_cmd_queue, &msg, wait_ticks) == pdTRUE)   /* 有命令到达 */
        {
            /* ---- 命令到达前先尝试 SD 自动恢复（防止 SD 拔插后遗留错误状态）---- */
            if (App_SD_GetState() != APP_SD_MOUNTED)               /* SD 未就绪 */
            {
                (void)App_SD_Init();  /* 尝试重新挂载（失败则状态维持，下次命令再试）*/
            }

            switch (msg.cmd)                                        /* 按命令类型分发 */
            {
            case STORAGE_CMD_CAPTURE_BMP:                          /* 截图命令 */
                if (s_handle_capture_bmp() != ERR_OK)
                {
                    s_state = STORAGE_STATE_ERROR;                  /* 截图失败：置错误状态 */
                }
                break;

            case STORAGE_CMD_REC_START:                            /* 开始录音命令 */
                if (s_handle_rec_start(msg.param) != ERR_OK)
                {
                    s_state = STORAGE_STATE_ERROR;                  /* 录音开始失败：置错误状态 */
                }
                break;

            case STORAGE_CMD_REC_STOP:                             /* 停止录音命令 */
                s_handle_rec_stop();                                /* 内部处理所有清理逻辑 */
                break;                                              /* REC_STOP 内部已处理错误状态 */

            default:                                               /* 未知命令：静默忽略 */
                break;                                              /* [改进] 可增加未知命令错误计数 */
            }
        }
        /* xQueueReceive 超时（无命令）—— 继续执行以下定期任务 */

        /* ---- 录音期间：定期将 App_Recorder 环形缓冲刷入 SD 卡 ---- */
        if (s_recording_active != 0u)
        {
            App_Recorder_FlushPending(&s_fil);  /* 将 >SD 块大小 的积累数据写出（约每 20ms 一次）*/
        }

        /* ---- 空闲时：执行 SD 健康检查 + 剩余容量刷新 ---- */
        /* [注意] 故意跳过录音期间：f_getfree 可能阻塞 50-200ms，影响录音 flush 实时性 */
        if (s_recording_active == 0u)
        {
            uint32_t now = xTaskGetTickCount();  /* 获取当前 tick（FreeRTOS 系统时间）*/

            /* ---- SD 卡自动恢复检查（每 2 秒）---- */
            if ((now - sd_check_tick) >= pdMS_TO_TICKS(STORAGE_SD_CHECK_MS))
            {
                sd_check_tick = now;             /* 更新时间戳，防止下次立即再触发 */

                if (App_SD_GetState() != APP_SD_MOUNTED)    /* SD 卡未就绪 */
                {
                    /* 尝试重新初始化 SD 卡（用户可能中途插拔，自动恢复提升鲁棒性）*/
                    if (App_SD_Init() == ERR_OK)
                    {
                        s_state = STORAGE_STATE_IDLE;        /* 恢复成功：回到 IDLE 状态 */
                        printf("[Storage] SD re-init OK\r\n");  /* [改进] 此 printf 应改为事件日志（UART CLI 统一管理）*/
                    }
                    else
                    {
                        s_state = STORAGE_STATE_ERROR;       /* 恢复失败：保持错误状态 */
                    }
                }
                else if (s_state == STORAGE_STATE_ERROR)
                {
                    /* SD 已挂载但 storage 状态卡在 ERROR（可能是上次操作遗留）—— 自动清除 */
                    s_state = STORAGE_STATE_IDLE;
                    printf("[Storage] error cleared\r\n");   /* [改进] 同上，改为统一日志机制 */
                }
            }

            /* ---- 剩余容量刷新（每 5 秒）---- */
            if ((now - space_refresh_tick) >= pdMS_TO_TICKS(STORAGE_SPACE_REFRESH_MS))
            {
                space_refresh_tick = now;        /* 更新时间戳 */
                App_SD_RefreshSpace();            /* 调用 f_getfree 更新 App_SD 内部缓存的容量信息 */
                /* [注意] f_getfree 可能阻塞 50-200ms（SD 总线速度影响），已保证仅在空闲时调用 */
            }
        }
    }
}

/* ========== 公开 API ========== */

/**
 * @brief   初始化存储模块（创建队列和任务）
 * @details 操作顺序：xQueueCreate → xTaskCreate。
 *          若任一步骤失败，s_state = ERROR，任务不启动。
 */
void App_Storage_Init(void)
{
    BaseType_t task_ok;                        /* xTaskCreate 返回值（pdPASS 或 errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY）*/

    /* 创建命令队列（深度=8，消息大小=sizeof(App_StorageMsg_t)）*/
    s_cmd_queue = xQueueCreate(STORAGE_CMD_QUEUE_DEPTH, sizeof(App_StorageMsg_t));
    if (s_cmd_queue == NULL)                   /* FreeRTOS heap 不足时 xQueueCreate 返回 NULL */
    {
        s_state = STORAGE_STATE_ERROR;         /* 标记错误（但此时无法 printf，静默失败）*/
        return;                                 /* [改进] 可添加断言 configASSERT(0) 触发 fault handler */
    }

    /* 创建 Storage_Task（优先级 2，低于音频和 UI，保证 SD I/O 不干扰实时任务）*/
    task_ok = xTaskCreate(Storage_Task,        /* 任务入口函数 */
                          "Storage",           /* 任务名（调试器/RTOS 查看器中显示）*/
                          STORAGE_TASK_STACK_WORDS,  /* 栈大小（字）= 2048 × 4 = 8192 字节 */
                          NULL,                /* 传给 pvParameters 的参数（不需要，传 NULL）*/
                          STORAGE_TASK_PRIO,   /* 优先级 = 2（低于 Audio=4 和 UI=4）*/
                          &s_task_handle);     /* 输出任务句柄（可用于调试）*/
    if (task_ok != pdPASS)
    {
        s_state = STORAGE_STATE_ERROR;         /* heap 不足时 xTaskCreate 失败 */
    }
}

/**
 * @brief   发送存储命令（线程安全，可在任意任务上下文调用）
 * @details 以 0 超时调用 xQueueSend（非阻塞发送），队列满则立即返回 ERR_BUSY。
 */
Err_t App_Storage_SendCmd(App_StorageCmd_e cmd, uint32_t param)
{
    App_StorageMsg_t msg;                      /* 命令消息结构体（值类型，入队时复制）*/

    if (s_cmd_queue == NULL)                   /* 队列未创建（Init 未调用或 Init 失败）*/
    {
        return ERR_NOT_INIT;
    }

    msg.cmd   = cmd;                           /* 设置命令类型 */
    msg.param = param;                         /* 设置命令参数 */

    /* xQueueSend with timeout=0：非阻塞，队列满则立即失败 */
    if (xQueueSend(s_cmd_queue, &msg, 0u) != pdTRUE)
    {
        return ERR_BUSY;                        /* 队列满（正常情况极少：深度 8，Speed < 8 cmd/task_cycle）*/
    }

    return ERR_OK;                             /* 命令已入队，等待 Storage_Task 处理 */
}

/**
 * @brief   查询存储任务当前状态（线程安全读取）
 * @return  最新的 App_StorageState_e 值（从 volatile 变量直接读取）
 */
App_StorageState_e App_Storage_GetState(void)
{
    return s_state;                            /* volatile 读取保证可见性，ARM 单字读为原子 */
}
