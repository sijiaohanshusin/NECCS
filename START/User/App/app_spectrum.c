#include "app_spectrum.h"

#include "ai_config.h"
#include "app_data_stream.h"
#include "app_user_config.h"

#include <math.h>
#include <string.h>

static App_SpectrumFrame_t s_publish_buffers[2];
static volatile uint8_t s_publish_index = 0u;
static volatile uint32_t s_publish_seq = 0u;
static volatile uint32_t s_active_band_packed = 0u;
static volatile uint32_t s_preview_band_packed = 0u;

static uint32_t s_pack_band(App_FreqBand_t band)
{
    return ((uint32_t)band.start_bin) | ((uint32_t)band.end_bin << 16);
}

static App_FreqBand_t s_unpack_band(uint32_t packed)
{
    App_FreqBand_t band;

    band.start_bin = (uint16_t)(packed & 0xFFFFu);
    band.end_bin = (uint16_t)(packed >> 16);

    return band;
}

static App_FreqBand_t s_clamp_band(App_FreqBand_t band)
{
    uint16_t last_bin = (uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u);

    if (band.start_bin > last_bin)
    {
        band.start_bin = last_bin;
    }
    if (band.end_bin > last_bin)
    {
        band.end_bin = last_bin;
    }
    if (band.end_bin < band.start_bin)
    {
        band.end_bin = band.start_bin;
    }

    return band;
}

static void s_fill_frame(App_SpectrumFrame_t *frame, uint32_t seq)
{
    App_FreqBand_t active_band = App_Spectrum_GetActiveBand();
    App_FreqBand_t preview_band = App_Spectrum_GetPreviewBand();
    uint32_t bin;
    uint32_t ch;

    if (frame == NULL)
    {
        return;
    }

    memset(frame->magnitude, 0, sizeof(frame->magnitude));

    frame->seq = seq;
    frame->bin_count = (uint16_t)APP_SPECTRUM_BIN_COUNT;
    frame->delta_f_hz = DELTA_F;
    frame->active_band = active_band;
    frame->preview_band = preview_band;

    for (ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        const float32_t *p_freq = &Mic_Freq_Buffer[ch * FRAME_LEN];

        frame->magnitude[0] += fabsf(p_freq[0]);
        for (bin = 1u; bin < (APP_SPECTRUM_BIN_COUNT - 1u); bin++)
        {
            float re = p_freq[bin * 2u];
            float im = p_freq[bin * 2u + 1u];

            frame->magnitude[bin] += sqrtf((re * re) + (im * im));
        }
        frame->magnitude[APP_SPECTRUM_BIN_COUNT - 1u] += fabsf(p_freq[1]);
    }

    for (bin = 0u; bin < APP_SPECTRUM_BIN_COUNT; bin++)
    {
        float avg_mag = frame->magnitude[bin] / (float)MIC_CHANNELS;

        frame->magnitude[bin] = isfinite(avg_mag) ? avg_mag : 0.0f;
    }
}

void App_Spectrum_Init(void)
{
    App_FreqBand_t default_band = App_Spectrum_DefaultBand();
    uint32_t i;

    memset(s_publish_buffers, 0, sizeof(s_publish_buffers));
    s_publish_index = 0u;
    s_publish_seq = 0u;
    s_active_band_packed = s_pack_band(default_band);
    s_preview_band_packed = s_pack_band(default_band);

    for (i = 0u; i < 2u; i++)
    {
        s_publish_buffers[i].bin_count = (uint16_t)APP_SPECTRUM_BIN_COUNT;
        s_publish_buffers[i].delta_f_hz = DELTA_F;
        s_publish_buffers[i].active_band = default_band;
        s_publish_buffers[i].preview_band = default_band;
    }
}

void App_Spectrum_PublishFromFft(uint32_t seq)
{
    uint8_t next_index = (uint8_t)(s_publish_index ^ 1u);

    __DMB();
    s_publish_seq++;
    __DMB();
    s_fill_frame(&s_publish_buffers[next_index], seq);
    __DMB();
    s_publish_index = next_index;
    s_publish_seq++;
}

void App_Spectrum_CopyFrame(App_SpectrumFrame_t *frame)
{
    if (frame == NULL)
    {
        return;
    }

    memcpy(frame, &s_publish_buffers[s_publish_index], sizeof(*frame));
}

uint8_t App_Spectrum_GetLatestFrame(App_SpectrumFrame_t *frame)
{
    uint32_t start_seq;
    uint32_t end_seq;
    uint8_t index;
    uint32_t attempt;

    if (frame == NULL)
    {
        return 0u;
    }

    for (attempt = 0u; attempt < 3u; attempt++)
    {
        start_seq = s_publish_seq;
        __DMB();
        if ((start_seq & 1u) != 0u)
        {
            continue;
        }

        index = s_publish_index;
        memcpy(frame, &s_publish_buffers[index], sizeof(*frame));
        __DMB();
        end_seq = s_publish_seq;
        if ((start_seq == end_seq) && ((end_seq & 1u) == 0u))
        {
            return 1u;
        }
    }

    return 0u;
}

App_FreqBand_t App_Spectrum_DefaultBand(void)
{
    App_FreqBand_t band = {
        (uint16_t)SRP_FREQ_BIN_START,
        (uint16_t)SRP_FREQ_BIN_END
    };

    return s_clamp_band(band);
}

void App_Spectrum_SetActiveBand(App_FreqBand_t band)
{
    App_FreqBand_t clamped = s_clamp_band(band);

    __DMB();
    s_active_band_packed = s_pack_band(clamped);
}

App_FreqBand_t App_Spectrum_GetActiveBand(void)
{
    uint32_t packed;

    __DMB();
    packed = s_active_band_packed;
    return s_clamp_band(s_unpack_band(packed));
}

void App_Spectrum_SetPreviewBand(App_FreqBand_t band)
{
    App_FreqBand_t clamped = s_clamp_band(band);

    __DMB();
    s_preview_band_packed = s_pack_band(clamped);
}

App_FreqBand_t App_Spectrum_GetPreviewBand(void)
{
    uint32_t packed;

    __DMB();
    packed = s_preview_band_packed;
    return s_clamp_band(s_unpack_band(packed));
}

float App_Spectrum_BinToHz(uint16_t bin)
{
    uint16_t last_bin = (uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u);

    if (bin > last_bin)
    {
        bin = last_bin;
    }

    return ((float)bin * DELTA_F);
}

uint16_t App_Spectrum_HzToBin(float hz)
{
    uint16_t last_bin = (uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u);
    float clamped_hz = hz;
    float max_hz = App_Spectrum_BinToHz(last_bin);
    uint32_t bin;

    if (clamped_hz <= 0.0f)
    {
        return 0u;
    }
    if (clamped_hz >= max_hz)
    {
        return last_bin;
    }

    bin = (uint32_t)((clamped_hz / DELTA_F) + 0.5f);
    if (bin > last_bin)
    {
        bin = last_bin;
    }

    return (uint16_t)bin;
}

uint16_t App_Spectrum_PanelAxisToBin(uint16_t axis_px,
                                     uint16_t axis_px0,
                                     uint16_t axis_length,
                                     float min_hz,
                                     uint8_t scale_mode,
                                     uint8_t invert)
{
    float max_hz = App_Spectrum_BinToHz((uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u));
    float clamped_min_hz = min_hz;
    float norm;
    float hz;

    if ((axis_length <= 1u) || (APP_SPECTRUM_BIN_COUNT <= 1u))
    {
        return 0u;
    }

    if (clamped_min_hz < DELTA_F)
    {
        clamped_min_hz = DELTA_F;
    }
    if (clamped_min_hz > max_hz)
    {
        clamped_min_hz = max_hz;
    }

    if (axis_px <= axis_px0)
    {
        norm = 0.0f;
    }
    else
    {
        uint32_t axis_local = (uint32_t)axis_px - (uint32_t)axis_px0;
        uint32_t span = (uint32_t)axis_length - 1u;

        if (axis_local >= span)
        {
            norm = 1.0f;
        }
        else
        {
            norm = (float)axis_local / (float)span;
        }
    }

    if (invert != 0u)
    {
        norm = 1.0f - norm;
    }

    if (scale_mode != 0u)
    {
        float log_min = logf(clamped_min_hz);
        float log_max = logf(max_hz);

        hz = expf(log_min + ((log_max - log_min) * norm));
    }
    else
    {
        hz = clamped_min_hz + ((max_hz - clamped_min_hz) * norm);
    }

    return App_Spectrum_HzToBin(hz);
}

uint16_t App_Spectrum_PanelXToBin(uint16_t x_px, uint16_t plot_x0, uint16_t plot_width)
{
    return App_Spectrum_PanelAxisToBin(x_px, plot_x0, plot_width, DELTA_F, 0u, 0u);
}
