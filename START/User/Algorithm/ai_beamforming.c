#include "ai_beamforming.h"

#include "ai_config.h"
#include "ai_srp_lut.h"
#include "app_data_stream.h"

#include <math.h>

volatile uint32_t g_srp_invalid_count = 0u;
volatile uint32_t g_srp_low_contrast_count = 0u;
volatile float32_t g_srp_last_contrast = 0.0f;
volatile float32_t g_srp_last_quality = 0.0f;

static uint8_t s_has_last_valid = 0u;
static Sound_Pos_t s_last_valid = {0.0f, 0.0f, 0.0f};
static uint32_t s_low_conf_streak = 0u;

static float32_t s_fine_theta_table[FINE_TOTAL];
static float32_t s_fine_phi_table[FINE_TOTAL];

#if (SRP_LOWCONF_POLICY != SRP_LOWCONF_REPORT_NEW) && \
    (SRP_LOWCONF_POLICY != SRP_LOWCONF_HOLD_LAST) && \
    (SRP_LOWCONF_POLICY != SRP_LOWCONF_MIXED)
#error "Invalid SRP_LOWCONF_POLICY"
#endif

static uint8_t coarse_idx_is_neighbor(uint32_t a, uint32_t b)
{
    uint32_t ai = a / COARSE_GRID_SIZE;
    uint32_t ap = a % COARSE_GRID_SIZE;
    uint32_t bi = b / COARSE_GRID_SIZE;
    uint32_t bp = b % COARSE_GRID_SIZE;

    uint32_t dti = (ai > bi) ? (ai - bi) : (bi - ai);
    uint32_t dpi = (ap > bp) ? (ap - bp) : (bp - ap);

    return (uint8_t)((dti <= SRP_TOPK_NMS_RADIUS) && (dpi <= SRP_TOPK_NMS_RADIUS));
}

static void find_top_k_indices_nms(const float32_t *arr, uint32_t *top_idx, uint32_t k)
{
    uint8_t used[COARSE_TOTAL] = {0};
    uint32_t chosen = 0u;

    for (uint32_t s = 0u; s < k; s++)
    {
        float32_t best_val = -1.0e30f;
        uint32_t best_idx = 0u;
        uint8_t found = 0u;

        for (uint32_t n = 0u; n < COARSE_TOTAL; n++)
        {
            if (used[n] != 0u)
            {
                continue;
            }

            uint8_t allow = 1u;
            for (uint32_t c = 0u; c < chosen; c++)
            {
                if (coarse_idx_is_neighbor(n, top_idx[c]) != 0u)
                {
                    allow = 0u;
                    break;
                }
            }
            if ((allow == 0u) || (arr[n] <= best_val))
            {
                continue;
            }

            best_val = arr[n];
            best_idx = n;
            found = 1u;
        }

        if (found == 0u)
        {
            for (uint32_t n = 0u; n < COARSE_TOTAL; n++)
            {
                if ((used[n] == 0u) && (arr[n] > best_val))
                {
                    best_val = arr[n];
                    best_idx = n;
                    found = 1u;
                }
            }
        }

        top_idx[s] = best_idx;
        if (found != 0u)
        {
            used[best_idx] = 1u;
            chosen++;
        }
    }
}

static void get_grid_angle(uint32_t idx, float32_t *theta_deg, float32_t *phi_deg)
{
    if (idx < COARSE_TOTAL)
    {
        uint32_t ti = idx / COARSE_GRID_SIZE;
        uint32_t pi = idx % COARSE_GRID_SIZE;
        *theta_deg = coarse_theta_deg[ti];
        *phi_deg = coarse_phi_deg[pi];
        return;
    }

    idx -= COARSE_TOTAL;
    if (idx < FINE_TOTAL)
    {
        *theta_deg = s_fine_theta_table[idx];
        *phi_deg = s_fine_phi_table[idx];
        return;
    }

    *theta_deg = 0.0f;
    *phi_deg = 0.0f;
}

static float32_t compute_second_max_all(uint32_t max_idx)
{
    float32_t second_max = -1.0e30f;

    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if ((i != max_idx) && (SRP_Power[i] > second_max))
        {
            second_max = SRP_Power[i];
        }
    }

    return second_max;
}

static float32_t compute_second_max_excluding_neighbor(uint32_t max_idx)
{
    float32_t max_theta, max_phi;
    float32_t second_max = -1.0e30f;
    uint8_t found = 0u;

    get_grid_angle(max_idx, &max_theta, &max_phi);

    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if (i == max_idx)
        {
            continue;
        }

        float32_t theta, phi;
        get_grid_angle(i, &theta, &phi);

        if ((fabsf(theta - max_theta) <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG) &&
            (fabsf(phi - max_phi) <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG))
        {
            continue;
        }

        if (SRP_Power[i] > second_max)
        {
            second_max = SRP_Power[i];
            found = 1u;
        }
    }

    if (found == 0u)
    {
        return compute_second_max_all(max_idx);
    }

    return second_max;
}

static float32_t apply_lowconf_energy(float32_t energy)
{
#if (SRP_ENABLE_ENERGY_SOFTCAP != 0u)
    if (energy > SRP_AMBIGUOUS_ENERGY_MAX)
    {
        return SRP_AMBIGUOUS_ENERGY_MAX;
    }
#endif
    return energy;
}

static void remap_output_angles(float32_t *x_angle, float32_t *y_angle)
{
#if (SRP_OUTPUT_SWAP_XY != 0u)
    float32_t tmp = *x_angle;
    *x_angle = *y_angle;
    *y_angle = tmp;
#endif

#if (SRP_OUTPUT_INVERT_X != 0u)
    *x_angle = -*x_angle;
#endif

#if (SRP_OUTPUT_INVERT_Y != 0u)
    *y_angle = -*y_angle;
#endif
}

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

void AI_SRP_PHAT_Init(void)
{
    g_srp_invalid_count = 0u;
    g_srp_low_contrast_count = 0u;
    g_srp_last_contrast = 0.0f;
    g_srp_last_quality = 0.0f;

    s_has_last_valid = 0u;
    s_last_valid.x_angle = 0.0f;
    s_last_valid.y_angle = 0.0f;
    s_last_valid.energy = 0.0f;
    s_low_conf_streak = 0u;
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
    find_top_k_indices_nms(SRP_Power, top_idx, FINE_TOP_K);

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
        g_srp_last_contrast = 0.0f;
        g_srp_last_quality = 0.0f;

        if (s_has_last_valid != 0u)
        {
            *result = s_last_valid;
            result->energy *= 0.90f;
            remap_output_angles(&result->x_angle, &result->y_angle);
        }
        else
        {
            result->x_angle = 0.0f;
            result->y_angle = 0.0f;
            result->energy = 0.0f;
        }
        return;
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

    float32_t second_max_raw = compute_second_max_all(max_idx);
    float32_t second_max_quality = compute_second_max_excluding_neighbor(max_idx);

    float32_t contrast_raw = (max_val - second_max_raw) / (fabsf(max_val) + 1.0e-6f);
    float32_t quality = (max_val - second_max_quality) / (fabsf(max_val) + 1.0e-6f);

    g_srp_last_contrast = contrast_raw;
    g_srp_last_quality = quality;

    uint8_t low_conf = (uint8_t)(quality < SRP_CONTRAST_MIN_RATIO);
    if (low_conf != 0u)
    {
        g_srp_low_contrast_count++;
        s_low_conf_streak++;
    }
    else
    {
        s_low_conf_streak = 0u;
    }

    uint8_t hold_last = 0u;
#if (SRP_LOWCONF_POLICY == SRP_LOWCONF_HOLD_LAST)
    hold_last = (uint8_t)(s_has_last_valid != 0u);
#elif (SRP_LOWCONF_POLICY == SRP_LOWCONF_MIXED)
    hold_last = (uint8_t)((s_has_last_valid != 0u) && (s_low_conf_streak <= SRP_LOWCONF_MIXED_HOLD_FRAMES));
#endif

    float32_t base_x = ((low_conf != 0u) && (hold_last != 0u)) ? s_last_valid.x_angle : cand_x;
    float32_t base_y = ((low_conf != 0u) && (hold_last != 0u)) ? s_last_valid.y_angle : cand_y;
    remap_output_angles(&base_x, &base_y);

    result->x_angle = base_x;
    result->y_angle = base_y;
    result->energy = (low_conf != 0u) ? apply_lowconf_energy(norm) : norm;

    if ((low_conf == 0u) &&
        (result->energy >= SRP_VALID_MIN_ENERGY) &&
        (quality >= SRP_VALID_MIN_QUALITY))
    {
        /* Cache canonical angle before output remap. */
        s_last_valid.x_angle = cand_x;
        s_last_valid.y_angle = cand_y;
        s_last_valid.energy = result->energy;
        s_has_last_valid = 1u;
    }
}
