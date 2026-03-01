#ifndef AI_SRP_LUT_H
#define AI_SRP_LUT_H

#include "arm_math.h"
#include "ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t srp_pair_idx[SRP_PAIR_COUNT][2];
extern const float32_t srp_pair_dx[SRP_PAIR_COUNT];
extern const float32_t srp_pair_dy[SRP_PAIR_COUNT];

extern const float32_t coarse_theta_deg[COARSE_GRID_SIZE];
extern const float32_t coarse_phi_deg[COARSE_GRID_SIZE];

extern const float32_t tdoa_coarse_lut[COARSE_TOTAL][SRP_PAIR_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* AI_SRP_LUT_H */

