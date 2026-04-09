/**
 * @file    app_storage_task.c
 * @brief   SD 卡异步存储任务实现
 * @details FreeRTOS 任务, 负责 BMP 截图和 WAV 录音的文件 I/O。
 *          所有 FatFS FIL 操作集中在此任务中, 其他任务通过命令队列交互。
 *
 * 设计决策 (来自 T2 团队讨论):
 * - FIL 句柄为 static, 不在栈上分配 (节省 ~600B 栈空间)
 * - BMP 截图: 单次 f_open → 逐行写入 → f_close (不重复开关文件)
 * - WAV 录音: open → header → 循环 flush → stop: seek(0) → 回填 → close
 * - 帧缓冲读取期间启用 swap-inhibit 防止撕裂
 */
#include "app_storage_task.h"
#include "app_sd.h"
#include "app_capture.h"
#include "app_recorder.h"
#include "ff.h"
#include "LCD/ltdc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <string.h>

/* ========== 任务参数 ========== */

#define STORAGE_TASK_STACK_WORDS   2048u
#define STORAGE_TASK_PRIO          2u       /**< osPriorityBelowNormal */
#define STORAGE_CMD_QUEUE_DEPTH    8u
#define STORAGE_FLUSH_INTERVAL_MS  20u      /**< 录音 flush 轮询间隔 */
#define STORAGE_SPACE_REFRESH_MS   5000u    /**< 容量信息刷新间隔 */

/* ========== BMP 文件格式 ========== */

#define BMP_WIDTH          800u
#define BMP_HEIGHT         480u
#define BMP_BPP            2u   /* RGB565 = 2 bytes/pixel */
#define BMP_ROW_BYTES      (BMP_WIDTH * BMP_BPP)
#define BMP_PIXEL_SIZE     (BMP_WIDTH * BMP_HEIGHT * BMP_BPP)

/** BMP 完整头 = FileHeader(14) + InfoHeader(40) + RGB565Masks(12) = 66 字节 */
#define BMP_HEADER_SIZE    66u

#include "app_display.h"

/* ========== 模块状态 ========== */

static volatile App_StorageState_e s_state = STORAGE_STATE_IDLE;

/** @brief 命令队列句柄 */
static QueueHandle_t s_cmd_queue = NULL;

/** @brief 任务句柄 */
static TaskHandle_t s_task_handle = NULL;

/** @brief 共享 FIL 句柄 (同一时间只进行一种 I/O) */
static FIL s_fil;

/** @brief 录音中标志 (需要持续 flush) */
static uint8_t s_recording_active = 0u;

/** @brief BMP 行缓冲: 800 × 2 = 1600 字节 */
static uint8_t s_row_buf[BMP_ROW_BYTES];

/* ========== BMP 头填充 ========== */

/**
 * @brief 填充 66 字节 BMP 头 (BI_BITFIELDS, RGB565, 800×480, bottom-up)
 */
static void s_fill_bmp_header(uint8_t *buf)
{
    memset(buf, 0, BMP_HEADER_SIZE);

    /* File Header (14 bytes) */
    buf[0] = 'B'; buf[1] = 'M';
    {
        uint32_t file_size = BMP_HEADER_SIZE + BMP_PIXEL_SIZE;
        memcpy(&buf[2], &file_size, 4u);
    }
    {
        uint32_t offset = BMP_HEADER_SIZE;
        memcpy(&buf[10], &offset, 4u);
    }

    /* Info Header (40 bytes) @ offset 14 */
    {
        uint32_t info_size = 40u;
        int32_t  width     = (int32_t)BMP_WIDTH;
        int32_t  height    = (int32_t)BMP_HEIGHT;  /* 正=bottom-up */
        uint16_t planes    = 1u;
        uint16_t bpp       = 16u;
        uint32_t compress  = 3u;  /* BI_BITFIELDS */
        uint32_t img_size  = BMP_PIXEL_SIZE;

        memcpy(&buf[14], &info_size, 4u);
        memcpy(&buf[18], &width, 4u);
        memcpy(&buf[22], &height, 4u);
        memcpy(&buf[26], &planes, 2u);
        memcpy(&buf[28], &bpp, 2u);
        memcpy(&buf[30], &compress, 4u);
        memcpy(&buf[34], &img_size, 4u);
    }

    /* RGB565 Masks (12 bytes) @ offset 54 */
    {
        uint32_t r = 0x0000F800u;
        uint32_t g = 0x000007E0u;
        uint32_t b = 0x0000001Fu;

        memcpy(&buf[54], &r, 4u);
        memcpy(&buf[58], &g, 4u);
        memcpy(&buf[62], &b, 4u);
    }
}

/* ========== BMP 截图处理 ========== */

/**
 * @brief 执行 BMP 截图: 读取 LTDC 前缓冲 → 写入 BMP 文件
 */
static Err_t s_handle_capture_bmp(void)
{
    char filepath[64];
    uint8_t bmp_header[BMP_HEADER_SIZE];
    FRESULT fr;
    UINT bw;
    uint32_t fb_addr;
    const uint16_t *fb_pixels;
    uint32_t row;
    Err_t ret;

    /* B1 fix: 录音期间不可截图 — 共用 s_fil 句柄 */
    if (s_recording_active != 0u)
    {
        App_Capture_SetState(CAPTURE_ERROR);
        return ERR_BUSY;
    }

    App_Capture_SetState(CAPTURE_BUSY);
    s_state = STORAGE_STATE_CAPTURING;

    /* 检查 SD 卡 */
    if (App_SD_GetState() != APP_SD_MOUNTED)
    {
        App_Capture_SetState(CAPTURE_ERROR);
        s_state = STORAGE_STATE_ERROR;
        return ERR_NOT_INIT;
    }

    /* 生成文件路径 */
    ret = App_SD_MakeFilePath("IMG", "IMG", "bmp", filepath, sizeof(filepath));
    if (ret != ERR_OK)
    {
        App_Capture_SetState(CAPTURE_ERROR);
        s_state = STORAGE_STATE_ERROR;
        return ret;
    }

    /* 打开文件 — 单次 open, 批量写入 (Embedded: 不可逐行开关文件) */
    fr = f_open(&s_fil, filepath, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        App_Capture_SetState(CAPTURE_ERROR);
        s_state = STORAGE_STATE_ERROR;
        return ERR_IO_FAILED;
    }

    /* 写入 BMP 头 (66 字节) */
    s_fill_bmp_header(bmp_header);
    fr = f_write(&s_fil, bmp_header, BMP_HEADER_SIZE, &bw);
    if ((fr != FR_OK) || (bw != BMP_HEADER_SIZE))
    {
        f_close(&s_fil);
        App_Capture_SetState(CAPTURE_ERROR);
        s_state = STORAGE_STATE_ERROR;
        return ERR_IO_FAILED;
    }

    /* 获取前缓冲地址, 启用 swap-inhibit 防止撕裂 */
    g_display_swap_inhibit = 1u;
    fb_addr = ltdc_get_frontbuf_addr();
    fb_pixels = (const uint16_t *)fb_addr;

    /* BMP bottom-up: 逐行写入 (从最后一行到第一行) */
    for (row = 0u; row < BMP_HEIGHT; row++)
    {
        uint32_t src_row = (BMP_HEIGHT - 1u) - row;
        const uint16_t *src = &fb_pixels[src_row * BMP_WIDTH];

        memcpy(s_row_buf, src, BMP_ROW_BYTES);

        fr = f_write(&s_fil, s_row_buf, BMP_ROW_BYTES, &bw);
        if ((fr != FR_OK) || (bw != BMP_ROW_BYTES))
        {
            g_display_swap_inhibit = 0u;
            f_close(&s_fil);
            App_Capture_SetState(CAPTURE_ERROR);
            s_state = STORAGE_STATE_ERROR;
            return ERR_IO_FAILED;
        }
    }

    g_display_swap_inhibit = 0u;
    f_close(&s_fil);

    App_Capture_IncrementCount();
    App_Capture_SetState(CAPTURE_DONE);
    s_state = STORAGE_STATE_IDLE;

    return ERR_OK;
}

/* ========== WAV 录音处理 ========== */

/**
 * @brief 开始 WAV 录音: 打开文件并写入初始头
 */
static Err_t s_handle_rec_start(uint32_t mode_param)
{
    char filepath[64];
    uint8_t wav_header[44];
    FRESULT fr;
    UINT bw;
    Err_t ret;
    App_RecorderMode_t mode = (App_RecorderMode_t)mode_param;
    uint16_t num_ch = (mode == RECORDER_MODE_RAW16) ? 16u : 1u;

    /* H1 fix: 防止重复开始 — 保护 s_fil 句柄 */
    if (s_recording_active != 0u)
    {
        return ERR_BUSY;
    }

    if (App_SD_GetState() != APP_SD_MOUNTED)
    {
        return ERR_NOT_INIT;
    }

    /* 生成文件路径 */
    ret = App_SD_MakeFilePath("WAV", "REC", "wav", filepath, sizeof(filepath));
    if (ret != ERR_OK)
    {
        return ret;
    }

    /* 打开文件 */
    fr = f_open(&s_fil, filepath, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        return ERR_IO_FAILED;
    }

    /* 写入初始 WAV 头 (data_size=0, Stop 时回填) */
    App_Recorder_FillWAVHeader(wav_header, num_ch, 0u);
    fr = f_write(&s_fil, wav_header, 44u, &bw);
    if ((fr != FR_OK) || (bw != 44u))
    {
        f_close(&s_fil);
        return ERR_IO_FAILED;
    }

    /* 初始化录音模块 (重置环形缓冲和统计) */
    ret = App_Recorder_Start(mode);
    if (ret != ERR_OK)
    {
        f_close(&s_fil);
        return ret;
    }

    s_recording_active = 1u;
    s_state = STORAGE_STATE_RECORDING;
    return ERR_OK;
}

/**
 * @brief 停止 WAV 录音: 刷出残余 → 回填头 → 关闭
 */
static Err_t s_handle_rec_stop(void)
{
    uint8_t wav_header[44];
    FRESULT fr;
    UINT bw;
    uint32_t data_bytes;
    uint16_t num_ch;

    if (s_recording_active == 0u)
    {
        return ERR_OK;
    }

    /* 刷出环形缓冲残余数据 */
    App_Recorder_FlushPending(&s_fil);

    /* 获取实际写入字节数 */
    data_bytes = App_Recorder_GetDataBytesWritten();

    /* 回填 WAV 头: 使用录音时的通道数 */
    num_ch = App_Recorder_GetNumChannels();
    App_Recorder_FillWAVHeader(wav_header, num_ch, data_bytes);

    fr = f_lseek(&s_fil, 0u);
    if (fr == FR_OK)
    {
        fr = f_write(&s_fil, wav_header, 44u, &bw);
        if ((fr != FR_OK) || (bw != 44u))
        {
            /* H2 fix: WAV 头回填失败 — 文件可能无法播放 */
            s_state = STORAGE_STATE_ERROR;
        }
    }
    else
    {
        s_state = STORAGE_STATE_ERROR;
    }

    f_close(&s_fil);
    App_Recorder_Stop();

    s_recording_active = 0u;
    s_state = STORAGE_STATE_IDLE;

    /* 刷新容量信息 */
    App_SD_RefreshSpace();

    return ERR_OK;
}

/* ========== 任务主体 ========== */

/**
 * @brief Storage_Task 入口
 */
static void Storage_Task(void *pvParameters)
{
    App_StorageMsg_t msg;
    TickType_t wait_ticks;
    uint32_t space_refresh_tick = 0u;

    (void)pvParameters;

    for (;;)
    {
        /* 录音期间: 短超时轮询 flush; 空闲: 长等待命令 */
        wait_ticks = (s_recording_active != 0u) ?
                     pdMS_TO_TICKS(STORAGE_FLUSH_INTERVAL_MS) :
                     portMAX_DELAY;

        if (xQueueReceive(s_cmd_queue, &msg, wait_ticks) == pdTRUE)
        {
            switch (msg.cmd)
            {
            case STORAGE_CMD_CAPTURE_BMP:
                s_handle_capture_bmp();
                break;

            case STORAGE_CMD_REC_START:
                /* M5 fix: 记录失败状态以便 UI 反馈 */
                if (s_handle_rec_start(msg.param) != ERR_OK)
                {
                    s_state = STORAGE_STATE_ERROR;
                }
                break;

            case STORAGE_CMD_REC_STOP:
                s_handle_rec_stop();
                break;

            default:
                break;
            }
        }

        /* 录音期间定期 flush 环形缓冲 */
        if (s_recording_active != 0u)
        {
            App_Recorder_FlushPending(&s_fil);
        }

        /* 定期刷新容量信息 — 录音期间跳过 (f_getfree 可阻塞 50-200ms, MQA3) */
        if (s_recording_active == 0u)
        {
            uint32_t now = xTaskGetTickCount();
            if ((now - space_refresh_tick) >= pdMS_TO_TICKS(STORAGE_SPACE_REFRESH_MS))
            {
                space_refresh_tick = now;
                App_SD_RefreshSpace();
            }
        }
    }
}

/* ========== 公开 API ========== */

void App_Storage_Init(void)
{
    BaseType_t task_ok;

    s_cmd_queue = xQueueCreate(STORAGE_CMD_QUEUE_DEPTH, sizeof(App_StorageMsg_t));
    if (s_cmd_queue == NULL)
    {
        s_state = STORAGE_STATE_ERROR;
        return;
    }

    task_ok = xTaskCreate(Storage_Task,
                          "Storage",
                          STORAGE_TASK_STACK_WORDS,
                          NULL,
                          STORAGE_TASK_PRIO,
                          &s_task_handle);
    if (task_ok != pdPASS)
    {
        s_state = STORAGE_STATE_ERROR;
    }
}

Err_t App_Storage_SendCmd(App_StorageCmd_e cmd, uint32_t param)
{
    App_StorageMsg_t msg;

    if (s_cmd_queue == NULL)
    {
        return ERR_NOT_INIT;
    }

    msg.cmd   = cmd;
    msg.param = param;

    if (xQueueSend(s_cmd_queue, &msg, 0u) != pdTRUE)
    {
        return ERR_BUSY;
    }

    return ERR_OK;
}

App_StorageState_e App_Storage_GetState(void)
{
    return s_state;
}
