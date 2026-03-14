/**
 * @file    app_runtime.h
 * @brief   Runtime config and UI renderer interfaces
 */
#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdint.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_RUNTIME_DISP_MODE_FAST = 0u,
    APP_RUNTIME_DISP_MODE_BALANCED = 1u,
    APP_RUNTIME_DISP_MODE_CLEAN = 2u
} App_Runtime_DisplayMode_t;

typedef enum {
    APP_RUNTIME_DISP_INTERP_NEAREST = 0u,
    APP_RUNTIME_DISP_INTERP_BILINEAR = 1u
} App_Runtime_DisplayInterp_t;

typedef enum {
    APP_RUNTIME_DISP_NORM_FAST = 0u,
    APP_RUNTIME_DISP_NORM_FULL = 1u
} App_Runtime_DisplayNorm_t;

typedef struct
{
    float ema_attack;
    float ema_decay;
    float db_floor;
    float fine_gain;
    float gamma;
    float noise_gate_ratio;
    float noise_adapt_gain;
    uint8_t smooth_passes;
    uint8_t fine_fusion_enable;
    uint8_t draw_coarse_grid;
    uint8_t interp_mode;
    uint8_t norm_mode;
    uint8_t text_refresh_div;
    uint8_t blit_rows;
} App_Runtime_DisplayCfg_t;

typedef struct
{
    uint32_t ui_target_fps;
    uint32_t audio_algo_decim;
    uint8_t perf_enabled;
    uint8_t reserved[3];
    App_Runtime_DisplayMode_t display_mode;
    App_Runtime_DisplayCfg_t display_cfg;
} App_Runtime_Config_t;

typedef enum
{
    APP_UI_RENDER_BACKEND_LEGACY = 0u
} App_UiRenderBackend_t;

/* unified runtime configuration */
void App_RuntimeConfig_Init(void);
void App_RuntimeConfig_Get(App_Runtime_Config_t *cfg);
void App_RuntimeConfig_SetUiTargetFps(uint32_t fps);
uint32_t App_RuntimeConfig_GetUiTargetFps(void);
void App_RuntimeConfig_SetAudioAlgoDecim(uint32_t decim);
uint32_t App_RuntimeConfig_GetAudioAlgoDecim(void);
void App_RuntimeConfig_SetPerfEnabled(uint8_t enable);
uint8_t App_RuntimeConfig_GetPerfEnabled(void);
void App_RuntimeConfig_SetDisplayMode(App_Runtime_DisplayMode_t mode);
App_Runtime_DisplayMode_t App_RuntimeConfig_GetDisplayMode(void);
void App_RuntimeConfig_SetDisplayCfg(const App_Runtime_DisplayCfg_t *cfg);
void App_RuntimeConfig_GetDisplayCfg(App_Runtime_DisplayCfg_t *cfg);
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend);
App_UiRenderBackend_t App_UiRenderer_GetBackend(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_RUNTIME_H */
