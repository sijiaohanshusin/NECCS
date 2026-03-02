#ifndef AI_SRP_LUT_H
#define AI_SRP_LUT_H

#include "arm_math.h"
#include "ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Auto-generated table declarations used by SRP-PHAT.
 * Data source: START/User/Algorithm/ai_srp_lut.c
 *
 * Coordinate convention:
 * - origin: array center
 * - +X: right
 * - +Y: up
 *
 * Pair displacement convention (important):
 *   dx = x_i - x_j
 *   dy = y_i - y_j
 * This matches the generated TDOA table and runtime formula.
 */

extern const uint8_t srp_pair_idx[SRP_PAIR_COUNT][2];
extern const float32_t srp_pair_dx[SRP_PAIR_COUNT];
extern const float32_t srp_pair_dy[SRP_PAIR_COUNT];

/* Coarse scan angles, 9 points each: [-60, -45, ..., 60] */
extern const float32_t coarse_theta_deg[COARSE_GRID_SIZE];
extern const float32_t coarse_phi_deg[COARSE_GRID_SIZE];

/* Coarse TDOA LUT: [COARSE_TOTAL][SRP_PAIR_COUNT], unit: seconds */
extern const float32_t tdoa_coarse_lut[COARSE_TOTAL][SRP_PAIR_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* AI_SRP_LUT_H */
