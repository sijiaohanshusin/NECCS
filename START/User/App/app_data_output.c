#include "app_data_output.h"

#include "ai_beamforming.h"
#include "ai_config.h"
#include "app_data_stream.h"
#include "usart.h"

#include <math.h>
#include <string.h>

#define VOFA_MAX_FLOATS        (FRAME_LEN / 2u)
#define VOFA_DIAG_FLOATS       5u
#define VOFA_SRP_FLOATS        (3u + COARSE_TOTAL + VOFA_DIAG_FLOATS)
#define VOFA_FRAME_MAX_BYTES   ((VOFA_MAX_FLOATS * sizeof(float32_t)) + 4u)
#define VOFA_UART_TX_TIMEOUT   5u

static const uint8_t s_vofa_tail[4] = {0x00, 0x00, 0x80, 0x7F};

static uint8_t s_tx_buf[VOFA_FRAME_MAX_BYTES];
static volatile uint32_t s_vofa_tx_drop_count = 0u;
static uint32_t s_vofa_frame_seq = 0u;

static float32_t s_last_coarse_power[COARSE_TOTAL];
static uint8_t s_has_last_coarse = 0u;
static uint8_t s_coarse_bad_streak = 0u;
static volatile uint32_t s_coarse_hold_count = 0u;

static void VOFA_JustFloat_Send(const float32_t *data, uint16_t count)
{
    if ((data == NULL) || (count == 0u) || (count > VOFA_MAX_FLOATS))
    {
        return;
    }

    if (huart1.gState != HAL_UART_STATE_READY)
    {
        s_vofa_tx_drop_count++;
        return;
    }

    uint16_t payload_bytes = (uint16_t)(count * sizeof(float32_t));
    memcpy(&s_tx_buf[0], data, payload_bytes);
    memcpy(&s_tx_buf[payload_bytes], s_vofa_tail, sizeof(s_vofa_tail));

    if (HAL_UART_Transmit(&huart1,
                          s_tx_buf,
                          (uint16_t)(payload_bytes + sizeof(s_vofa_tail)),
                          VOFA_UART_TX_TIMEOUT) != HAL_OK)
    {
        s_vofa_tx_drop_count++;
    }
}

void VOFA_Send_Channel_RMS(void)
{
    float32_t ac_rms_buf[MIC_CHANNELS];

    for (uint32_t ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        arm_std_f32(&Mic_Process_Buffer[ch * FRAME_LEN], FRAME_LEN, &ac_rms_buf[ch]);
    }

    VOFA_JustFloat_Send(ac_rms_buf, MIC_CHANNELS);
}

void VOFA_Send_FFT_Magnitude(uint8_t channel)
{
    if (channel >= MIC_CHANNELS)
    {
        return;
    }

    const float32_t *p_freq = &Mic_Freq_Buffer[(uint32_t)channel * FRAME_LEN];
    float32_t mag_buf[FRAME_LEN / 2u];

    mag_buf[0] = fabsf(p_freq[0]);

    arm_cmplx_mag_f32(&p_freq[2],
                      &mag_buf[1],
                      (FRAME_LEN / 2u) - 2u);

    mag_buf[(FRAME_LEN / 2u) - 1u] = fabsf(p_freq[1]);

    VOFA_JustFloat_Send(mag_buf, FRAME_LEN / 2u);
}

void VOFA_Send_SRP_Result(const Sound_Pos_t *pos)
{
    if (pos == NULL)
    {
        return;
    }

    float32_t send_buf[VOFA_SRP_FLOATS];
    float32_t coarse_abs_sum = 0.0f;
    uint32_t coarse_nonfinite_count = 0u;

    send_buf[0] = pos->x_angle;
    send_buf[1] = pos->y_angle;
    send_buf[2] = pos->energy;

    memcpy(&send_buf[3], SRP_Power, COARSE_TOTAL * sizeof(float32_t));

    for (uint32_t i = 0u; i < COARSE_TOTAL; i++)
    {
        float32_t v = send_buf[3u + i];
        if (!isfinite(v))
        {
            coarse_nonfinite_count++;
            continue;
        }
        coarse_abs_sum += fabsf(v);
    }

    if ((coarse_nonfinite_count > 0u) || (coarse_abs_sum < 1.0e-6f))
    {
        if ((s_has_last_coarse == 1u) && (s_coarse_bad_streak == 0u))
        {
            memcpy(&send_buf[3], s_last_coarse_power, COARSE_TOTAL * sizeof(float32_t));
            s_coarse_hold_count++;
        }

        if (s_coarse_bad_streak < 255u)
        {
            s_coarse_bad_streak++;
        }
    }
    else
    {
        memcpy(s_last_coarse_power, &send_buf[3], COARSE_TOTAL * sizeof(float32_t));
        s_has_last_coarse = 1u;
        s_coarse_bad_streak = 0u;
    }

    send_buf[3 + COARSE_TOTAL + 0] = (float32_t)g_audio_both_flags_count;
    send_buf[3 + COARSE_TOTAL + 1] = (float32_t)s_vofa_tx_drop_count;
    send_buf[3 + COARSE_TOTAL + 2] = (float32_t)g_srp_invalid_count;
    send_buf[3 + COARSE_TOTAL + 3] = (float32_t)s_coarse_hold_count;
    send_buf[3 + COARSE_TOTAL + 4] = (float32_t)(++s_vofa_frame_seq);

    for (uint32_t i = 0u; i < VOFA_SRP_FLOATS; i++)
    {
        if (!isfinite(send_buf[i]))
        {
            send_buf[i] = 0.0f;
        }
    }

    VOFA_JustFloat_Send(send_buf, VOFA_SRP_FLOATS);
}

