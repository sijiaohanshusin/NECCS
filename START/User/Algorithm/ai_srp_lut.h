/**
 * @file    ai_srp_lut.h
 * @brief   SRP-PHAT 预计算查找表声明
 * @note    数据由 tools/generate_srp_lut.py 生成, 定义在 ai_srp_lut.c
 */

#ifndef AI_SRP_LUT_H
#define AI_SRP_LUT_H

#include <stdint.h>
#include "ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 麦克风对索引 (按基线长度降序选取的前 SRP_PAIR_COUNT 对) */
extern const uint8_t srp_pair_idx[SRP_PAIR_COUNT][2];

/* 麦克风对坐标差 (米) */
extern const float srp_pair_dx[SRP_PAIR_COUNT];
extern const float srp_pair_dy[SRP_PAIR_COUNT];

/* 粗搜网格角度表 (度) */
extern const float coarse_theta_deg[COARSE_GRID_SIZE];
extern const float coarse_phi_deg[COARSE_GRID_SIZE];

/* 粗搜 TDOA 查找表 (秒): tdoa_coarse_lut[grid_point][pair] */
extern const float tdoa_coarse_lut[COARSE_TOTAL][SRP_PAIR_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* AI_SRP_LUT_H */
