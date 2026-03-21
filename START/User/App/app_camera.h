#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint32_t seq;
    uint8_t buffer_id;
    uint8_t valid;
} App_CameraFrame_t;

typedef enum
{
    APP_CAMERA_INIT_STAGE_IDLE = 0u,
    APP_CAMERA_INIT_STAGE_SENSOR_INIT = 1u,
    APP_CAMERA_INIT_STAGE_PREVIEW_CFG = 2u,
    APP_CAMERA_INIT_STAGE_DCMI_INIT = 3u,
    APP_CAMERA_INIT_STAGE_READY = 4u
} App_CameraInitStage_t;

typedef struct
{
    uint32_t frame_seq;
    uint32_t published_seq;
    uint32_t error_code;
    uint32_t dma_error_code;
    uint32_t restart_count;
    uint32_t restart_fail_count;
    uint32_t init_attempt_count;
    uint32_t publish_count;
    uint32_t publish_drop_count;
    uint32_t dma_done_count;
    uint32_t frame_event_count;
    uint32_t arm_count;
    uint32_t arm_fail_count;
    uint32_t raw_hash;
    uint32_t pub_hash;
    uint32_t dcmi_state;
    uint32_t dma_state;
    uint16_t sensor_mid;
    uint16_t sensor_pid;
    uint16_t raw_sample0;
    uint16_t raw_sample1;
    uint16_t raw_sample2;
    uint16_t pub_sample0;
    uint16_t pub_sample1;
    uint16_t pub_sample2;
    uint8_t initialized;
    uint8_t streaming;
    uint8_t valid;
    uint8_t latest_index;
    uint8_t published_index;
    uint8_t init_stage;
    uint8_t pending_restart;
    uint8_t freeze_enabled;
    uint8_t sensor_diag_stage;
    uint8_t sensor_last_write_status;
    uint8_t sensor_last_read_status;
} App_CameraStatus_t;

void App_Camera_Init(void);
void App_Camera_Start(void);
uint8_t App_Camera_Retry(void);
void App_Camera_Stop(void);
void App_Camera_TaskInit(void);
uint8_t App_Camera_UpdatePublishedFrame(void);
void App_Camera_SetFreeze(uint8_t enable);
uint8_t App_Camera_GetFreeze(void);
uint8_t App_Camera_AcquireLatestFrame(App_CameraFrame_t *frame);
void App_Camera_ReleaseFrame(const App_CameraFrame_t *frame);
void App_Camera_GetLatestFrame(App_CameraFrame_t *frame);
void App_Camera_GetStatus(App_CameraStatus_t *status);
const char *App_Camera_InitStageName(uint8_t stage);

void App_Camera_DCMI_IRQHandler(void);
void App_Camera_DMA_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */
