#ifndef APP_SPECTRUM_H
#define APP_SPECTRUM_H

#include "app_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void App_Spectrum_Init(void);
void App_Spectrum_PublishFromFft(uint32_t seq);
void App_Spectrum_CopyFrame(App_SpectrumFrame_t *frame);
uint8_t App_Spectrum_GetLatestFrame(App_SpectrumFrame_t *frame);

App_FreqBand_t App_Spectrum_DefaultBand(void);
void App_Spectrum_SetActiveBand(App_FreqBand_t band);
App_FreqBand_t App_Spectrum_GetActiveBand(void);
void App_Spectrum_SetPreviewBand(App_FreqBand_t band);
App_FreqBand_t App_Spectrum_GetPreviewBand(void);
float App_Spectrum_BinToHz(uint16_t bin);
uint16_t App_Spectrum_HzToBin(float hz);
uint16_t App_Spectrum_PanelAxisToBin(uint16_t axis_px,
                                     uint16_t axis_px0,
                                     uint16_t axis_length,
                                     float min_hz,
                                     uint8_t scale_mode,
                                     uint8_t invert);
uint16_t App_Spectrum_PanelXToBin(uint16_t x_px, uint16_t plot_x0, uint16_t plot_width);

#ifdef __cplusplus
}
#endif

#endif /* APP_SPECTRUM_H */
