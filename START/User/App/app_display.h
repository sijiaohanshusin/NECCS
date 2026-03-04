#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

#include "app_display_cfg.h"
#include "app_main_task.h"
#include "ai_beamforming.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_DISPLAY_MODE_FAST = 0u,
    APP_DISPLAY_MODE_BALANCED = 1u,
    APP_DISPLAY_MODE_CLEAN = 2u
} App_Display_Mode_t;

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
    uint8_t bilinear_sampling;
    uint8_t text_refresh_div;
    uint8_t blit_rows;
} App_Display_RuntimeCfg_t;

void App_Display_Init(void);

void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active);

uint8_t App_Display_IsReady(void);

void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg);

void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg);

void App_Display_SetMode(App_Display_Mode_t mode);

App_Display_Mode_t App_Display_GetMode(void);

const char *App_Display_ModeName(App_Display_Mode_t mode);

extern volatile uint32_t g_display_init_stage;
extern volatile uint32_t g_display_init_error;

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
