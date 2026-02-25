/**
 * @file ai_preprocess.h
 * @brief AI 数据预处理 - 简洁调试版
 */

#ifndef AI_PREPROCESS_H
#define AI_PREPROCESS_H

#include "arm_math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解交错 + 转置 + 类型转换
 * @note 调试阶段使用，确保缓冲区不重叠且已正确分配
 */
void Deinterleave_Using_Matrix(q15_t *src_interleaved_q15, 
                               q15_t *dst_planar_q15, 
                               float32_t *dst_planar_f32, 
                               uint16_t frame_size, 
                               uint16_t num_channels);

#ifdef __cplusplus
}
#endif

#endif /* AI_PREPROCESS_H */