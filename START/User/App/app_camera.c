#include "app_camera.h"

#include "app_user_config.h"
#include "camera_ov2640.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define APP_CAMERA_FB0_ADDR             0xC0177000u
#define APP_CAMERA_FB1_ADDR             0xC019C800u
#define APP_CAMERA_FRAME_BYTES          (APP_CAMERA_PREVIEW_W * APP_CAMERA_PREVIEW_H * 2u)
#define APP_CAMERA_DMA_FRAME_WORDS      (APP_CAMERA_FRAME_BYTES / 4u)
#define APP_CAMERA_DMA_TOTAL_WORDS      (APP_CAMERA_DMA_FRAME_WORDS * 2u)

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FRAME_BYTES % 4u) != 0u))
#error "Camera frame buffer size must be aligned to 32-bit DMA transfers"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FB1_ADDR - APP_CAMERA_FB0_ADDR) != APP_CAMERA_FRAME_BYTES))
#error "Camera framebuffer addresses must match the configured frame size"
#endif

#if ((APP_CAMERA_ENABLE != 0u) && ((APP_CAMERA_FB1_ADDR + APP_CAMERA_FRAME_BYTES) > 0xC0200000u))
#error "Camera framebuffers must stay inside the 2MB non-cacheable SDRAM window"
#endif

#if (APP_CAMERA_ENABLE != 0u)
static DCMI_HandleTypeDef s_hdcmi;
static DMA_HandleTypeDef s_hdma_dcmi;
static volatile uint8_t s_camera_initialized = 0u;
static volatile uint8_t s_camera_streaming = 0u;
static volatile uint8_t s_camera_frame_valid = 0u;
static volatile uint8_t s_camera_latest_index = 0u;
static volatile uint8_t s_camera_msp_error = 0u;
static volatile uint32_t s_camera_frame_seq = 0u;
static volatile uint32_t s_camera_error_code = 0u;

static uint16_t *const s_camera_buffers[2] = {
    (uint16_t *)APP_CAMERA_FB0_ADDR,
    (uint16_t *)APP_CAMERA_FB1_ADDR
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
#endif

void App_Camera_Init(void)
{
#if (APP_CAMERA_ENABLE == 0u)
    return;
#else
    if (s_camera_initialized != 0u)
    {
        return;
    }

    if (Camera_OV2640_Init() != 0u)
    {
        printf("CAM: OV2640 init failed\r\n");
        return;
    }
    if (Camera_OV2640_ConfigRgb565Preview((uint16_t)APP_CAMERA_PREVIEW_W,
                                          (uint16_t)APP_CAMERA_PREVIEW_H) != 0u)
    {
        printf("CAM: preview config failed\r\n");
        return;
    }

    memset((void *)APP_CAMERA_FB0_ADDR, 0, (size_t)(APP_CAMERA_FRAME_BYTES * 2u));

    s_camera_setup_handle();
    s_camera_msp_error = 0u;
    if ((HAL_DCMI_Init(&s_hdcmi) != HAL_OK) || (s_camera_msp_error != 0u))
    {
        printf("CAM: DCMI init failed\r\n");
        return;
    }

    s_camera_frame_valid = 0u;
    s_camera_latest_index = 0u;
    s_camera_frame_seq = 0u;
    s_camera_error_code = 0u;
    s_camera_initialized = 1u;
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

    s_camera_frame_valid = 0u;
    s_camera_latest_index = 0u;
    s_camera_frame_seq = 0u;
    s_camera_error_code = 0u;

    __HAL_UNLOCK(&s_hdma_dcmi);
    if (HAL_DCMI_Start_DMA(&s_hdcmi,
                           DCMI_MODE_CONTINUOUS,
                           APP_CAMERA_FB0_ADDR,
                           APP_CAMERA_DMA_TOTAL_WORDS) != HAL_OK)
    {
        printf("CAM: start failed\r\n");
        return;
    }

    __HAL_DCMI_ENABLE_IT(&s_hdcmi, DCMI_IT_FRAME);
    s_camera_streaming = 1u;
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

    (void)HAL_DCMI_Stop(&s_hdcmi);
    s_camera_streaming = 0u;
    s_camera_frame_valid = 0u;
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
    frame->valid = s_camera_frame_valid;
    frame->seq = s_camera_frame_seq;
    frame->width = (uint16_t)APP_CAMERA_PREVIEW_W;
    frame->height = (uint16_t)APP_CAMERA_PREVIEW_H;
    frame->pixels = (s_camera_frame_valid != 0u) ? s_camera_buffers[s_camera_latest_index] : NULL;
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
    s_camera_frame_valid = 1u;
    __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME);
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
    if ((hdcmi == NULL) || (hdcmi != &s_hdcmi))
    {
        return;
    }

    s_camera_error_code = HAL_DCMI_GetError(hdcmi);
    s_camera_frame_valid = 0u;
}
#endif
