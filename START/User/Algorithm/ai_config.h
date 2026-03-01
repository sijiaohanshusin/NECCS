#ifndef __AI_CONFIG_H
#define __AI_CONFIG_H

#include <stdint.h>

/* Audio I/O */
#define MIC_CHANNELS        16u
#define FRAME_LEN           256u
#define SAMPLING_RATE       48000u

/* Double-buffered interleaved DMA buffer, int16_t units */
#define DMA_BUFFER_SIZE     (MIC_CHANNELS * FRAME_LEN * 2u)

/* SRP-PHAT configuration */
#define SPEED_OF_SOUND      343.0f
#define DELTA_F             ((float)SAMPLING_RATE / (float)FRAME_LEN)

/* Use 500Hz~8kHz at Fs=48k, N=256 => bin 3..42 (40 bins) */
#define SRP_FREQ_BIN_START  3u
#define SRP_FREQ_BIN_END    42u
#define SRP_FREQ_BINS       (SRP_FREQ_BIN_END - SRP_FREQ_BIN_START + 1u)

/* Pair selection */
#define SRP_PAIR_COUNT      40u

/* Coarse-to-fine grid */
#define COARSE_GRID_SIZE    8u
#define COARSE_TOTAL        (COARSE_GRID_SIZE * COARSE_GRID_SIZE)   /* 64 */

#define FINE_TOP_K          3u
#define FINE_GRID_SIZE      4u
#define FINE_TOTAL_PER_TOP  (FINE_GRID_SIZE * FINE_GRID_SIZE)       /* 16 */
#define FINE_TOTAL          (FINE_TOP_K * FINE_TOTAL_PER_TOP)       /* 48 */

#define SRP_GRID_TOTAL      (COARSE_TOTAL + FINE_TOTAL)             /* 112 */

#define PHAT_EPSILON        1.0e-10f

#endif /* __AI_CONFIG_H */

