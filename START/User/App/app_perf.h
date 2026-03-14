/**
 * @file    app_perf.h
 * @brief   Runtime performance profiling interfaces
 */
#ifndef APP_PERF_H
#define APP_PERF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_PERF_SEC_AUDIO_TOTAL = 0u,
    APP_PERF_SEC_AUDIO_DEINT = 1u,
    APP_PERF_SEC_AUDIO_FFT = 2u,
    APP_PERF_SEC_AUDIO_SRP = 3u,
    APP_PERF_SEC_UI_LOOP = 4u,
    APP_PERF_SEC_UI_SNAPSHOT = 5u,
    APP_PERF_SEC_UI_RENDER = 6u,
    APP_PERF_SEC_DISP_PREPARE = 7u,
    APP_PERF_SEC_DISP_NORM = 8u,
    APP_PERF_SEC_DISP_RENDER = 9u,
    APP_PERF_SEC_DISP_OVERLAY = 10u,
    APP_PERF_SEC_DISP_COMMIT = 11u,
    APP_PERF_SEC_COUNT
} App_Perf_Section_t;

/* runtime performance profiling */
/* runtime performance profiling */
void App_Perf_Init(void);
void App_Perf_SetEnabled(uint8_t enable);
uint8_t App_Perf_IsEnabled(void);
void App_Perf_Reset(void);
uint32_t App_Perf_BeginCycles(void);
void App_Perf_EndCycles(App_Perf_Section_t section, uint32_t start_cycles);
void App_Perf_CountAudioProc(void);
void App_Perf_CountUiLoop(void);
void App_Perf_MaybePrintRates(void);
void App_Perf_Dump(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_PERF_H */
