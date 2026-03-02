#ifndef __AI_CONFIG_H
#define __AI_CONFIG_H

#include <stdint.h>

/* Audio I/O */
#define MIC_CHANNELS        16u
#define FRAME_LEN           256u
#define SAMPLING_RATE       48000u
#define DMA_BUFFER_SIZE     (MIC_CHANNELS * FRAME_LEN * 2u)

/* SRP-PHAT core */
#define SPEED_OF_SOUND      343.0f
#define DELTA_F             ((float)SAMPLING_RATE / (float)FRAME_LEN)

#define SRP_FREQ_BIN_START  3u
#define SRP_FREQ_BIN_END    42u
#define SRP_FREQ_BINS       (SRP_FREQ_BIN_END - SRP_FREQ_BIN_START + 1u)

#define SRP_PAIR_COUNT      40u

/* Coarse/fine scan grid */
#define COARSE_GRID_SIZE    9u
#define COARSE_TOTAL        (COARSE_GRID_SIZE * COARSE_GRID_SIZE)

#define FINE_TOP_K          3u
#define FINE_GRID_SIZE      4u
#define FINE_TOTAL_PER_TOP  (FINE_GRID_SIZE * FINE_GRID_SIZE)
#define FINE_TOTAL          (FINE_TOP_K * FINE_TOTAL_PER_TOP)

#define SRP_GRID_TOTAL      (COARSE_TOTAL + FINE_TOTAL)

/* Numerical stability */
#define PHAT_EPSILON        1.0e-10f

/* Low-confidence policy */
#define SRP_LOWCONF_REPORT_NEW         0u
#define SRP_LOWCONF_HOLD_LAST          1u
#define SRP_LOWCONF_MIXED              2u
#define SRP_LOWCONF_POLICY             SRP_LOWCONF_REPORT_NEW
#define SRP_LOWCONF_MIXED_HOLD_FRAMES  6u

/* Quality and robustness gates */
#define SRP_CONTRAST_MIN_RATIO               0.03f
#define SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG    5.0f
#define SRP_TOPK_NMS_RADIUS                  1u

/* Valid-result latch gate */
#define SRP_VALID_MIN_ENERGY      0.05f
#define SRP_VALID_MIN_QUALITY     0.01f

/* Optional low-confidence energy soft-cap */
#define SRP_ENABLE_ENERGY_SOFTCAP 0u
#define SRP_AMBIGUOUS_ENERGY_MAX  0.30f

/* Output angle remap (for installation orientation alignment) */
#define SRP_OUTPUT_SWAP_XY  0u
#define SRP_OUTPUT_INVERT_X 0u
#define SRP_OUTPUT_INVERT_Y 0u

#endif /* __AI_CONFIG_H */
