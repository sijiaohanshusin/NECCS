#ifndef CAMERA_OV2640_H
#define CAMERA_OV2640_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t mid;
    uint16_t pid;
    uint8_t diag_stage;
    uint8_t last_write_status;
    uint8_t last_read_status;
} Camera_OV2640_Diag_t;

uint8_t Camera_OV2640_Init(void);
uint8_t Camera_OV2640_ReadId(uint16_t *mid, uint16_t *pid);
uint8_t Camera_OV2640_ConfigRgb565Preview(uint16_t width, uint16_t height);
void Camera_OV2640_GetDiag(Camera_OV2640_Diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_OV2640_H */
