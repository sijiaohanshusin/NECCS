#ifndef CAMERA_OV2640_H
#define CAMERA_OV2640_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t Camera_OV2640_Init(void);
uint8_t Camera_OV2640_ReadId(uint16_t *mid, uint16_t *pid);
uint8_t Camera_OV2640_ConfigRgb565Preview(uint16_t width, uint16_t height);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_OV2640_H */
