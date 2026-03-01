#include "ai_beamforming.h"
#include "ai_config.h"
#include "ai_srp_lut.h"
#include "app_data_stream.h"

#include <math.h>

volatile uint32_t g_srp_invalid_count = 0u;
volatile uint32_t g_srp_low_contrast_count = 0u;

static uint8_t s_has_last_valid = 0u;
static Sound_Pos_t s_last_valid = {0.0f, 0.0f, 0.0f};

static float32_t s_fine_theta_table[FINE_TOTAL];
static float32_t s_fine_phi_table[FINE_TOTAL];

#define SRP_CONTRAST_MIN_RATIO    (0.03f)
#define SRP_AMBIGUOUS_ENERGY_MAX  (0.30f)

void AI_FFT_Process(void)
{
    float32_t mean_val;

    for (uint32_t ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        float32_t *p_time = &Mic_Process_Buffer[ch * FRAME_LEN];
        float32_t *p_freq = &Mic_Freq_Buffer[ch * FRAME_LEN];

        arm_mean_f32(p_time, FRAME_LEN, &mean_val);
        arm_offset_f32(p_time, -mean_val, p_time, FRAME_LEN);

        arm_mult_f32(p_time, Hanning_Window, p_time, FRAME_LEN);

        arm_rfft_fast_f32(&S_Rfft, p_time, p_freq, 0);
    }
}

static void SRP_GCC_PHAT_Compute(void)
{
    float32_t conj_buf[SRP_FREQ_BINS * 2u];
    float32_t cross_buf[SRP_FREQ_BINS * 2u];
    float32_t mag_buf[SRP_FREQ_BINS];

    for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
    {
        uint8_t mi = srp_pair_idx[p][0];
        uint8_t mj = srp_pair_idx[p][1];

        const float32_t *xi = &Mic_Freq_Buffer[(uint32_t)mi * FRAME_LEN + (SRP_FREQ_BIN_START * 2u)];
        const float32_t *xj = &Mic_Freq_Buffer[(uint32_t)mj * FRAME_LEN + (SRP_FREQ_BIN_START * 2u)];

        arm_cmplx_conj_f32((float32_t *)xj, conj_buf, SRP_FREQ_BINS);
        arm_cmplx_mult_cmplx_f32((float32_t *)xi, conj_buf, cross_buf, SRP_FREQ_BINS);
        arm_cmplx_mag_f32(cross_buf, mag_buf, SRP_FREQ_BINS);

        float32_t *p_out = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2u];
        for (uint32_t k = 0u; k < SRP_FREQ_BINS; k++)
        {
            float32_t inv_mag = 1.0f / (mag_buf[k] + PHAT_EPSILON);
            p_out[2u * k] = cross_buf[2u * k] * inv_mag;
            p_out[2u * k + 1u] = cross_buf[2u * k + 1u] * inv_mag;
        }
    }
}

static float32_t SRP_Accumulate_Point(const float32_t *tau)
{
    float32_t power = 0.0f;

    for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
    {
        const float32_t *gcc = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2u];

        float32_t d_phi_deg = 360.0f * DELTA_F * tau[p];
        float32_t sin_d, cos_d;
        float32_t sin_phi, cos_phi;

        arm_sin_cos_f32(d_phi_deg, &sin_d, &cos_d);
        arm_sin_cos_f32(d_phi_deg * (float32_t)SRP_FREQ_BIN_START, &sin_phi, &cos_phi);

        float32_t pair_sum = 0.0f;
        for (uint32_t k = 0u; k < SRP_FREQ_BINS; k++)
        {
            pair_sum += gcc[2u * k] * cos_phi + gcc[2u * k + 1u] * sin_phi;

            float32_t c_new = cos_phi * cos_d - sin_phi * sin_d;
            float32_t s_new = sin_phi * cos_d + cos_phi * sin_d;
            cos_phi = c_new;
            sin_phi = s_new;
        }

        power += pair_sum;
    }

    return power;
}

static void find_top_k_indices(const float32_t *arr, uint32_t len, uint32_t *top_idx, uint32_t k)
{
    float32_t top_val[FINE_TOP_K];

    for (uint32_t i = 0u; i < k; i++)
    {
        top_val[i] = -1.0e30f;
        top_idx[i] = 0u;
    }

    for (uint32_t n = 0u; n < len; n++)
    {
        uint32_t min_slot = 0u;
        float32_t min_val = top_val[0];

        for (uint32_t i = 1u; i < k; i++)
        {
            if (top_val[i] < min_val)
            {
                min_val = top_val[i];
                min_slot = i;
            }
        }

        if (arr[n] > min_val)
        {
            top_val[min_slot] = arr[n];
            top_idx[min_slot] = n;
        }
    }
}

void AI_SRP_PHAT_Init(void)
{
    g_srp_invalid_count = 0u;
    g_srp_low_contrast_count = 0u;
    s_has_last_valid = 0u;
    s_last_valid.x_angle = 0.0f;
    s_last_valid.y_angle = 0.0f;
    s_last_valid.energy = 0.0f;
}

void AI_SRP_PHAT_Process(Sound_Pos_t *result)
{
    if (result == NULL)
    {
        return;
    }

    float32_t tau_buf[SRP_PAIR_COUNT];
    static const float32_t fine_offsets[FINE_GRID_SIZE] = {-7.5f, -2.5f, 2.5f, 7.5f};
    const float32_t inv_c = 1.0f / SPEED_OF_SOUND;

    SRP_GCC_PHAT_Compute();

    for (uint32_t g = 0u; g < COARSE_TOTAL; g++)
    {
        SRP_Power[g] = SRP_Accumulate_Point(tdoa_coarse_lut[g]);
    }

    uint32_t top_idx[FINE_TOP_K];
    find_top_k_indices(SRP_Power, COARSE_TOTAL, top_idx, FINE_TOP_K);

    for (uint32_t t = 0u; t < FINE_TOP_K; t++)
    {
        uint32_t coarse_g = top_idx[t];
        uint32_t ti = coarse_g / COARSE_GRID_SIZE;
        uint32_t pi = coarse_g % COARSE_GRID_SIZE;

        float32_t center_theta = coarse_theta_deg[ti];
        float32_t center_phi = coarse_phi_deg[pi];

        for (uint32_t fi = 0u; fi < FINE_GRID_SIZE; fi++)
        {
            float32_t theta_h = center_theta + fine_offsets[fi];
            float32_t sin_th, cos_th_unused;
            arm_sin_cos_f32(theta_h, &sin_th, &cos_th_unused);

            for (uint32_t fj = 0u; fj < FINE_GRID_SIZE; fj++)
            {
                uint32_t local_idx = fi * FINE_GRID_SIZE + fj;
                uint32_t global_idx = t * FINE_TOTAL_PER_TOP + local_idx;

                float32_t theta_v = center_phi + fine_offsets[fj];
                float32_t sin_tv, cos_tv;
                arm_sin_cos_f32(theta_v, &sin_tv, &cos_tv);

                s_fine_theta_table[global_idx] = theta_h;
                s_fine_phi_table[global_idx] = theta_v;

                float32_t sin_th_cos_tv = sin_th * cos_tv;

                for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
                {
                    tau_buf[p] = (srp_pair_dx[p] * sin_th_cos_tv + srp_pair_dy[p] * sin_tv) * inv_c;
                }

                SRP_Power[COARSE_TOTAL + global_idx] = SRP_Accumulate_Point(tau_buf);
            }
        }
    }

    float32_t max_val;
    uint32_t max_idx;
    arm_max_f32(SRP_Power, SRP_GRID_TOTAL, &max_val, &max_idx);

    if ((!isfinite(max_val)) || (max_idx >= SRP_GRID_TOTAL))
    {
        g_srp_invalid_count++;
        if (s_has_last_valid != 0u)
        {
            *result = s_last_valid;
            result->energy *= 0.90f;
        }
        else
        {
            result->x_angle = 0.0f;
            result->y_angle = 0.0f;
            result->energy = 0.0f;
        }
        return;
    }

    float32_t second_max = -1.0e30f;
    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if (i == max_idx)
        {
            continue;
        }
        if (SRP_Power[i] > second_max)
        {
            second_max = SRP_Power[i];
        }
    }

    float32_t cand_x;
    float32_t cand_y;
    if (max_idx < COARSE_TOTAL)
    {
        uint32_t ti = max_idx / COARSE_GRID_SIZE;
        uint32_t pi = max_idx % COARSE_GRID_SIZE;
        cand_x = coarse_theta_deg[ti];
        cand_y = coarse_phi_deg[pi];
    }
    else
    {
        uint32_t fine_idx = max_idx - COARSE_TOTAL;
        cand_x = s_fine_theta_table[fine_idx];
        cand_y = s_fine_phi_table[fine_idx];
    }

    float32_t norm = max_val / (float32_t)(SRP_PAIR_COUNT * SRP_FREQ_BINS);
    if (norm > 1.0f)
    {
        norm = 1.0f;
    }
    if (norm < 0.0f)
    {
        norm = 0.0f;
    }

    float32_t contrast = (max_val - second_max) / (fabsf(max_val) + 1.0e-6f);
    if ((s_has_last_valid != 0u) && (contrast < SRP_CONTRAST_MIN_RATIO))
    {
        g_srp_low_contrast_count++;
        result->x_angle = s_last_valid.x_angle;
        result->y_angle = s_last_valid.y_angle;
        result->energy = (norm < SRP_AMBIGUOUS_ENERGY_MAX) ? norm : SRP_AMBIGUOUS_ENERGY_MAX;
        return;
    }

    result->x_angle = cand_x;
    result->y_angle = cand_y;
    result->energy = norm;

    s_last_valid = *result;
    s_has_last_valid = 1u;
}

