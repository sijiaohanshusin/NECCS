#ifndef APP_BOOT_DIAG_H
#define APP_BOOT_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_BOOT_DIAG_STAGE_IDLE = 0u,
    APP_BOOT_DIAG_STAGE_SOFT_I2C = 1u,
    APP_BOOT_DIAG_STAGE_APP_STREAM = 2u,
    APP_BOOT_DIAG_STAGE_APP_TASK = 3u,
    APP_BOOT_DIAG_STAGE_SAI_DMA = 4u,
    APP_BOOT_DIAG_STAGE_CLOCK_WAIT = 5u,
    APP_BOOT_DIAG_STAGE_PCMD0 = 6u,
    APP_BOOT_DIAG_STAGE_PCMD1 = 7u,
    APP_BOOT_DIAG_STAGE_CAMERA_INIT = 8u,
    APP_BOOT_DIAG_STAGE_CAMERA_START = 9u,
    APP_BOOT_DIAG_STAGE_DONE = 10u
} App_BootDiag_Stage_t;

typedef struct
{
    uint32_t stage;
    uint32_t stack_high_water_words;
    uint8_t completed;
} App_BootDiag_Status_t;

void App_BootDiag_GetStatus(App_BootDiag_Status_t *status);
const char *App_BootDiag_StageName(uint32_t stage);

#ifdef __cplusplus
}
#endif

#endif /* APP_BOOT_DIAG_H */
