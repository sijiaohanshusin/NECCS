#include "app_camera.h"

#include "app_user_config.h"
#include "camera_ov2640.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define APP_CAMERA_FB0_ADDR             0xC0177000u
#define APP_CAMERA_FB1_ADDR             0xC019C800u
#define APP_CAMERA_PUB0_ADDR            0xC01C2000u
#define APP_CAMERA_PUB1_ADDR            (APP_CAMERA_PUB0_ADDR + APP_CAMERA_FRAME_BYTES)
#define APP_CAMERA_FRAME_BYTES          (APP_CAMERA_PREVIEW_W * APP_CAMERA_PREVIEW_H * 2u)
#define APP_CAMERA_DMA_FRAME_WORDS      (APP_CAMERA_FRAME_BYTES / 4u)
#define APP_CAMERA_DMA_TOTAL_WORDS      (APP_CAMERA_DMA_FRAME_WORDS * 2u)
#define APP_CAMERA_VIDEO_WINDOW_LIMIT   0xC0400000u

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FRAME_BYTES % 4u) != 0u))
#error "Camera frame buffer size must be aligned to 32-bit DMA transfers"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FB1_ADDR - APP_CAMERA_FB0_ADDR) != APP_CAMERA_FRAME_BYTES))
#error "Camera framebuffer addresses must match the configured frame size"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_PUB1_ADDR + APP_CAMERA_FRAME_BYTES) > APP_CAMERA_VIDEO_WINDOW_LIMIT))
#error "Camera buffers must stay inside the non-cacheable SDRAM window"
#endif

#if (APP_CAMERA_ENABLE != 0u)
static DCMI_HandleTypeDef s_hdcmi;
static DMA_HandleTypeDef s_hdma_dcmi;

static volatile uint8_t s_camera_initialized = 0u;
static volatile uint8_t s_camera_streaming = 0u;
static volatile uint8_t s_camera_raw_valid = 0u;
static volatile uint8_t s_camera_frame_valid = 0u;
static volatile uint8_t s_camera_latest_index = 0u;
static volatile uint8_t s_camera_pub_index = 0u;
static volatile uint8_t s_camera_pending_restart = 0u;
static volatile uint8_t s_camera_msp_error = 0u;
static volatile uint8_t s_camera_pub_refcount[2] = {0u, 0u};
static volatile uint32_t s_camera_frame_seq = 0u;
static volatile uint32_t s_camera_published_seq = 0u;
static volatile uint32_t s_camera_error_code = 0u;
static volatile uint32_t s_camera_dma_error_code = 0u;
static volatile uint32_t s_camera_restart_count = 0u;
static volatile uint32_t s_camera_restart_fail_count = 0u;
static volatile uint32_t s_camera_init_attempt_count = 0u;
static volatile uint32_t s_camera_publish_count = 0u;
static volatile uint32_t s_camera_publish_drop_count = 0u;
static volatile uint8_t s_camera_init_stage = APP_CAMERA_INIT_STAGE_IDLE;

static uint16_t *const s_camera_buffers[2] = {
    (uint16_t *)APP_CAMERA_FB0_ADDR,
    (uint16_t *)APP_CAMERA_FB1_ADDR
};

static uint16_t *const s_camera_pub_buffers[2] = {
    (uint16_t *)APP_CAMERA_PUB0_ADDR,
    (uint16_t *)APP_CAMERA_PUB1_ADDR
};

static void s_camera_setup_handle(void)
{
    memset(&s_hdcmi, 0, sizeof(s_hdcmi));
    s_hdcmi.Instance = DCMI;
    s_hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
    s_hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
    s_hdcmi.Init.VSPolarity = DCMI_VSPOLARITY_LOW;
    s_hdcmi.Init.HSPolarity = DCMI_HSPOLARITY_LOW;
    s_hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
    s_hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
    s_hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
    s_hdcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
    s_hdcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
    s_hdcmi.Init.LineSelectMode = DCMI_LSM_ALL;
    s_hdcmi.Init.LineSelectStart = DCMI_OELS_ODD;
}

static void s_camera_disable_nonframe_interrupts(void)
{
    __HAL_DCMI_DISABLE_IT(&s_hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
}

static void s_camera_clear_pending_flags(void)
{
    uint32_t dma_flags;

    dma_flags = __HAL_DMA_GET_TC_FLAG_INDEX(&s_hdma_dcmi) |
                __HAL_DMA_GET_HT_FLAG_INDEX(&s_hdma_dcmi) |
                __HAL_DMA_GET_TE_FLAG_INDEX(&s_hdma_dcmi) |
                __HAL_DMA_GET_DME_FLAG_INDEX(&s_hdma_dcmi) |
                __HAL_DMA_GET_FE_FLAG_INDEX(&s_hdma_dcmi);
    if (dma_flags != 0u)
    {
        __HAL_DMA_CLEAR_FLAG(&s_hdma_dcmi, dma_flags);
    }

    __HAL_DCMI_CLEAR_FLAG(&s_hdcmi,
                          DCMI_FLAG_FRAMERI |
                          DCMI_FLAG_OVRRI |
                          DCMI_FLAG_ERRRI |
                          DCMI_FLAG_VSYNCRI |
                          DCMI_FLAG_LINERI);
}

static void s_camera_reset_capture_state(uint8_t reset_seq)
{
    s_camera_raw_valid = 0u;
    s_camera_latest_index = 0u;
    s_camera_error_code = HAL_DCMI_ERROR_NONE;
    s_camera_dma_error_code = HAL_DMA_ERROR_NONE;
    if (reset_seq != 0u)
    {
        s_camera_frame_seq = 0u;
    }
}

static void s_camera_reset_published_state(uint8_t clear_counters)
{
    s_camera_frame_valid = 0u;
    s_camera_pub_index = 0u;
    s_camera_published_seq = 0u;
    s_camera_pub_refcount[0] = 0u;
    s_camera_pub_refcount[1] = 0u;
    if (clear_counters != 0u)
    {
        s_camera_restart_count = 0u;
        s_camera_restart_fail_count = 0u;
        s_camera_publish_count = 0u;
        s_camera_publish_drop_count = 0u;
    }
}

static void s_camera_force_idle(void)
{
    uint32_t wait_count = 1024u;

    if ((s_hdcmi.Instance == NULL) || (s_hdma_dcmi.Instance == NULL))
    {
        return;
    }

    __HAL_DCMI_DISABLE_IT(&s_hdcmi, DCMI_IT_FRAME | DCMI_IT_LINE | DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
    CLEAR_BIT(s_hdcmi.Instance->CR, DCMI_CR_CAPTURE);
    __HAL_DCMI_DISABLE(&s_hdcmi);

    __HAL_DMA_DISABLE(&s_hdma_dcmi);
    while ((((DMA_Stream_TypeDef *)s_hdma_dcmi.Instance)->CR & DMA_SxCR_EN) != 0u)
    {
        if (wait_count == 0u)
        {
            break;
        }
        wait_count--;
    }

    s_camera_clear_pending_flags();

    s_hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
    s_hdcmi.State = HAL_DCMI_STATE_READY;
    s_hdma_dcmi.ErrorCode = HAL_DMA_ERROR_NONE;
    s_hdma_dcmi.State = HAL_DMA_STATE_READY;
    __HAL_UNLOCK(&s_hdcmi);
    __HAL_UNLOCK(&s_hdma_dcmi);
}

static HAL_StatusTypeDef s_camera_start_stream(uint8_t reset_seq, uint8_t announce_start)
{
    s_camera_force_idle();
    s_camera_reset_capture_state(reset_seq);
    s_camera_pending_restart = 0u;

    __HAL_UNLOCK(&s_hdcmi);
    __HAL_UNLOCK(&s_hdma_dcmi);
    if (HAL_DCMI_Start_DMA(&s_hdcmi,
                           DCMI_MODE_CONTINUOUS,
                           APP_CAMERA_FB0_ADDR,
                           APP_CAMERA_DMA_TOTAL_WORDS) != HAL_OK)
    {
        s_camera_streaming = 0u;
        s_camera_error_code = HAL_DCMI_GetError(&s_hdcmi);
        s_camera_dma_error_code = s_hdma_dcmi.ErrorCode;
        return HAL_ERROR;
    }

    __HAL_DMA_DISABLE_IT(&s_hdma_dcmi, DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(&s_hdma_dcmi, DMA_IT_FE);
    s_camera_disable_nonframe_interrupts();
    __HAL_DCMI_ENABLE_IT(&s_hdcmi, DCMI_IT_FRAME);
    s_camera_streaming = 1u;

    if (announce_start != 0u)
    {
        printf("CAM: stream started words=%lu fb0=0x%08lX fb1=0x%08lX\r\n",
               (unsigned long)APP_CAMERA_DMA_TOTAL_WORDS,
               (unsigned long)APP_CAMERA_FB0_ADDR,
               (unsigned long)APP_CAMERA_FB1_ADDR);
    }

    return HAL_OK;
}

static void s_camera_copy_frame(uint8_t dst_index, uint8_t src_index)
{
    memcpy(s_camera_pub_buffers[dst_index & 1u],
           s_camera_buffers[src_index & 1u],
           (size_t)APP_CAMERA_FRAME_BYTES);
}
#endif

const char *App_Camera_InitStageName(uint8_t stage)
{
    switch ((App_CameraInitStage_t)stage)
    {
        case APP_CAMERA_INIT_STAGE_SENSOR_INIT: return "ov_init";
        case APP_CAMERA_INIT_STAGE_PREVIEW_CFG: return "preview";
        case APP_CAMERA_INIT_STAGE_DCMI_INIT:   return "dcmi";
        case APP_CAMERA_INIT_STAGE_READY:       return "ready";
        case APP_CAMERA_INIT_STAGE_IDLE:
        default:
            return "idle";
    }
}

void App_Camera_Init(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return;
#else
    if (s_camera_initialized != 0u)
    {
        return;
    }

    if (s_camera_init_attempt_count == 0u)
    {
        s_camera_reset_published_state(1u);
    }
    else
    {
        s_camera_reset_published_state(0u);
    }

    s_camera_init_attempt_count++;
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_SENSOR_INIT;

    if (Camera_OV2640_Init() != 0u)
    {
        printf("CAM: OV2640 init failed\r\n");
        return;
    }

    s_camera_init_stage = APP_CAMERA_INIT_STAGE_PREVIEW_CFG;
    if (Camera_OV2640_ConfigRgb565Preview((uint16_t)APP_CAMERA_PREVIEW_W,
                                          (uint16_t)APP_CAMERA_PREVIEW_H) != 0u)
    {
        printf("CAM: preview config failed\r\n");
        return;
    }

    printf("CAM: OV2640 ready preview=%ux%u RGB565\r\n",
           (unsigned int)APP_CAMERA_PREVIEW_W,
           (unsigned int)APP_CAMERA_PREVIEW_H);

    memset((void *)APP_CAMERA_FB0_ADDR, 0, (size_t)(APP_CAMERA_FRAME_BYTES * 2u));
    memset((void *)APP_CAMERA_PUB0_ADDR, 0, (size_t)(APP_CAMERA_FRAME_BYTES * 2u));

    s_camera_init_stage = APP_CAMERA_INIT_STAGE_DCMI_INIT;
    s_camera_setup_handle();
    s_camera_msp_error = 0u;
    if ((HAL_DCMI_Init(&s_hdcmi) != HAL_OK) || (s_camera_msp_error != 0u))
    {
        printf("CAM: DCMI init failed\r\n");
        return;
    }

    s_camera_disable_nonframe_interrupts();
    s_camera_clear_pending_flags();
    s_camera_reset_capture_state(1u);
    s_camera_pending_restart = 0u;
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_READY;
    s_camera_initialized = 1u;

    printf("CAM: DCMI ready fb0=0x%08lX fb1=0x%08lX pub0=0x%08lX pub1=0x%08lX\r\n",
           (unsigned long)APP_CAMERA_FB0_ADDR,
           (unsigned long)APP_CAMERA_FB1_ADDR,
           (unsigned long)APP_CAMERA_PUB0_ADDR,
           (unsigned long)APP_CAMERA_PUB1_ADDR);
#endif
}

void App_Camera_Start(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return;
#else
    if (s_camera_initialized == 0u)
    {
        App_Camera_Init();
    }
    if ((s_camera_initialized == 0u) || (s_camera_streaming != 0u))
    {
        return;
    }

    if (s_camera_start_stream(1u, 1u) != HAL_OK)
    {
        printf("CAM: start failed err=0x%08lX\r\n", (unsigned long)s_camera_error_code);
    }
#endif
}

uint8_t App_Camera_Retry(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return 1u;
#else
    printf("CAM: manual retry requested\r\n");

    if (s_camera_streaming != 0u)
    {
        App_Camera_Stop();
    }

    if (s_camera_initialized != 0u)
    {
        s_camera_force_idle();
        (void)HAL_DCMI_DeInit(&s_hdcmi);
    }

    memset(&s_hdcmi, 0, sizeof(s_hdcmi));
    memset(&s_hdma_dcmi, 0, sizeof(s_hdma_dcmi));
    s_camera_initialized = 0u;
    s_camera_streaming = 0u;
    s_camera_pending_restart = 0u;
    s_camera_msp_error = 0u;
    s_camera_init_stage = APP_CAMERA_INIT_STAGE_IDLE;
    s_camera_error_code = HAL_DCMI_ERROR_NONE;
    s_camera_dma_error_code = HAL_DMA_ERROR_NONE;
    s_camera_reset_capture_state(1u);
    s_camera_reset_published_state(0u);

    App_Camera_Init();
    if (s_camera_initialized == 0u)
    {
        return 1u;
    }

    App_Camera_Start();
    return (s_camera_streaming != 0u) ? 0u : 1u;
#endif
}

void App_Camera_Stop(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return;
#else
    if (s_camera_streaming == 0u)
    {
        return;
    }

    s_camera_force_idle();
    s_camera_streaming = 0u;
    s_camera_raw_valid = 0u;
    s_camera_pending_restart = 0u;
#endif
}

uint8_t App_Camera_UpdatePublishedFrame(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return 0u;
#else
    uint32_t primask;
    uint32_t raw_seq;
    uint32_t published_seq;
    uint8_t raw_valid;
    uint8_t raw_index;
    uint8_t current_pub_valid;
    uint8_t current_pub_index;
    uint8_t target_index;

    if (s_camera_initialized == 0u)
    {
        return 0u;
    }

    if (s_camera_pending_restart != 0u)
    {
        if (s_camera_start_stream(0u, 0u) == HAL_OK)
        {
            s_camera_restart_count++;
        }
        else
        {
            s_camera_restart_fail_count++;
            return 0u;
        }
    }

    primask = __get_PRIMASK();
    __disable_irq();
    raw_valid = s_camera_raw_valid;
    raw_index = s_camera_latest_index;
    raw_seq = s_camera_frame_seq;
    published_seq = s_camera_published_seq;
    current_pub_valid = s_camera_frame_valid;
    current_pub_index = s_camera_pub_index;
    if (primask == 0u)
    {
        __enable_irq();
    }

    if ((raw_valid == 0u) || (raw_seq == 0u) || (raw_seq == published_seq))
    {
        return 0u;
    }

    if (current_pub_valid == 0u)
    {
        if (s_camera_pub_refcount[0] == 0u)
        {
            target_index = 0u;
        }
        else if (s_camera_pub_refcount[1] == 0u)
        {
            target_index = 1u;
        }
        else
        {
            s_camera_publish_drop_count++;
            return 0u;
        }
    }
    else
    {
        target_index = (uint8_t)(current_pub_index ^ 1u);
        if (s_camera_pub_refcount[target_index] != 0u)
        {
            s_camera_publish_drop_count++;
            return 0u;
        }
    }

    s_camera_copy_frame(target_index, raw_index);

    primask = __get_PRIMASK();
    __disable_irq();
    s_camera_pub_index = target_index;
    s_camera_published_seq = raw_seq;
    s_camera_frame_valid = 1u;
    s_camera_publish_count++;
    if (primask == 0u)
    {
        __enable_irq();
    }

    return 1u;
#endif
}

uint8_t App_Camera_AcquireLatestFrame(App_CameraFrame_t *frame)
{
#if (APP_CAMERA_ENABLE == 0u)
    (void)frame;
    return 0u;
#else
    uint32_t primask;
    uint8_t index;

    if (frame == NULL)
    {
        return 0u;
    }

    memset(frame, 0, sizeof(*frame));

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_camera_frame_valid == 0u)
    {
        if (primask == 0u)
        {
            __enable_irq();
        }
        return 0u;
    }

    index = s_camera_pub_index;
    if (s_camera_pub_refcount[index] < 0xFFu)
    {
        s_camera_pub_refcount[index]++;
    }

    frame->pixels = s_camera_pub_buffers[index];
    frame->width = (uint16_t)APP_CAMERA_PREVIEW_W;
    frame->height = (uint16_t)APP_CAMERA_PREVIEW_H;
    frame->stride = (uint16_t)APP_CAMERA_PREVIEW_W;
    frame->seq = s_camera_published_seq;
    frame->buffer_id = index;
    frame->valid = 1u;

    if (primask == 0u)
    {
        __enable_irq();
    }

    return 1u;
#endif
}

void App_Camera_ReleaseFrame(const App_CameraFrame_t *frame)
{
#if (APP_CAMERA_ENABLE != 0u)
    uint32_t primask;
    uint8_t index;

    if ((frame == NULL) || (frame->valid == 0u) || (frame->pixels == NULL) || (frame->buffer_id > 1u))
    {
        return;
    }

    index = frame->buffer_id;
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_camera_pub_refcount[index] > 0u)
    {
        s_camera_pub_refcount[index]--;
    }
    if (primask == 0u)
    {
        __enable_irq();
    }
#else
    (void)frame;
#endif
}

void App_Camera_GetLatestFrame(App_CameraFrame_t *frame)
{
    uint32_t primask;

    if (frame == NULL)
    {
        return;
    }

    memset(frame, 0, sizeof(*frame));

#if (APP_CAMERA_ENABLE == 0u)
    return;
#else
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_camera_frame_valid != 0u)
    {
        frame->pixels = s_camera_pub_buffers[s_camera_pub_index];
        frame->width = (uint16_t)APP_CAMERA_PREVIEW_W;
        frame->height = (uint16_t)APP_CAMERA_PREVIEW_H;
        frame->stride = (uint16_t)APP_CAMERA_PREVIEW_W;
        frame->seq = s_camera_published_seq;
        frame->buffer_id = s_camera_pub_index;
        frame->valid = 1u;
    }
    if (primask == 0u)
    {
        __enable_irq();
    }
#endif
}

void App_Camera_GetStatus(App_CameraStatus_t *status)
{
    uint32_t primask;

    if (status == NULL)
    {
        return;
    }

    memset(status, 0, sizeof(*status));

#if (APP_CAMERA_ENABLE == 0u)
    return;
#else
    primask = __get_PRIMASK();
    __disable_irq();
    status->initialized = s_camera_initialized;
    status->streaming = s_camera_streaming;
    status->valid = s_camera_frame_valid;
    status->latest_index = s_camera_latest_index;
    status->published_index = s_camera_pub_index;
    status->frame_seq = s_camera_frame_seq;
    status->published_seq = s_camera_published_seq;
    status->error_code = s_camera_error_code;
    status->dma_error_code = s_camera_dma_error_code;
    status->restart_count = s_camera_restart_count;
    status->restart_fail_count = s_camera_restart_fail_count;
    status->init_attempt_count = s_camera_init_attempt_count;
    status->publish_count = s_camera_publish_count;
    status->publish_drop_count = s_camera_publish_drop_count;
    status->init_stage = s_camera_init_stage;
    status->pending_restart = s_camera_pending_restart;
    if (primask == 0u)
    {
        __enable_irq();
    }
#endif
}

void App_Camera_DCMI_IRQHandler(void)
{
#if (APP_CAMERA_ENABLE != 0u)
    if (s_camera_initialized != 0u)
    {
        HAL_DCMI_IRQHandler(&s_hdcmi);
    }
#endif
}

void App_Camera_DMA_IRQHandler(void)
{
#if (APP_CAMERA_ENABLE != 0u)
    if (s_camera_initialized != 0u)
    {
        HAL_DMA_IRQHandler(&s_hdma_dcmi);
    }
#endif
}

#if (APP_CAMERA_ENABLE != 0u)
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
    GPIO_InitTypeDef gpio_init = {0};

    if ((hdcmi == NULL) || (hdcmi->Instance != DCMI))
    {
        return;
    }

    __HAL_RCC_DCMI_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF13_DCMI;

    gpio_init.Pin = GPIO_PIN_4 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    gpio_init.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    gpio_init.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    gpio_init.Pin = GPIO_PIN_4;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET);

    memset(&s_hdma_dcmi, 0, sizeof(s_hdma_dcmi));
    s_hdma_dcmi.Instance = DMA1_Stream1;
    s_hdma_dcmi.Init.Request = DMA_REQUEST_DCMI;
    s_hdma_dcmi.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_hdma_dcmi.Init.PeriphInc = DMA_PINC_DISABLE;
    s_hdma_dcmi.Init.MemInc = DMA_MINC_ENABLE;
    s_hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    s_hdma_dcmi.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    s_hdma_dcmi.Init.Mode = DMA_CIRCULAR;
    s_hdma_dcmi.Init.Priority = DMA_PRIORITY_HIGH;
    s_hdma_dcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    s_hdma_dcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL;
    s_hdma_dcmi.Init.MemBurst = DMA_MBURST_SINGLE;
    s_hdma_dcmi.Init.PeriphBurst = DMA_PBURST_SINGLE;

    __HAL_LINKDMA(hdcmi, DMA_Handle, s_hdma_dcmi);
    if (HAL_DMA_Init(&s_hdma_dcmi) != HAL_OK)
    {
        s_camera_msp_error = 1u;
        return;
    }

    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6u, 1u);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_SetPriority(DCMI_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

void HAL_DCMI_MspDeInit(DCMI_HandleTypeDef *hdcmi)
{
    if ((hdcmi == NULL) || (hdcmi->Instance != DCMI))
    {
        return;
    }

    HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_DisableIRQ(DCMI_IRQn);
    HAL_DMA_DeInit(&s_hdma_dcmi);
    __HAL_RCC_DCMI_CLK_DISABLE();
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    uint32_t target_select;

    if ((hdcmi == NULL) || (hdcmi != &s_hdcmi))
    {
        return;
    }

    target_select = ((DMA_Stream_TypeDef *)s_hdma_dcmi.Instance)->CR & DMA_SxCR_CT;
    s_camera_latest_index = (target_select != 0u) ? 0u : 1u;
    s_camera_frame_seq++;
    s_camera_raw_valid = 1u;
    __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME);
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
    uint32_t error_code;
    uint32_t dma_error_code;

    if ((hdcmi == NULL) || (hdcmi != &s_hdcmi))
    {
        return;
    }

    error_code = HAL_DCMI_GetError(hdcmi);
    dma_error_code = s_hdma_dcmi.ErrorCode;
    s_camera_error_code = error_code;
    s_camera_dma_error_code = dma_error_code;

    if ((error_code == HAL_DCMI_ERROR_NONE) && (dma_error_code == HAL_DMA_ERROR_FE))
    {
        s_hdma_dcmi.ErrorCode = HAL_DMA_ERROR_NONE;
        return;
    }

    s_camera_streaming = 0u;
    s_camera_raw_valid = 0u;
    s_camera_pending_restart = 1u;
}
#endif
