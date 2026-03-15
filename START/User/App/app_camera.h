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
    uint32_t seq;
    uint8_t valid;
} App_CameraFrame_t;

void App_Camera_Init(void);
void App_Camera_Start(void);
void App_Camera_Stop(void);
void App_Camera_GetLatestFrame(App_CameraFrame_t *frame);

void App_Camera_DCMI_IRQHandler(void);
void App_Camera_DMA_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */
