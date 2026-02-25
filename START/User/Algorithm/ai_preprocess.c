#include "arm_math.h"
#include "ai_preprocess.h"

// 假设这些是你的缓冲区 (确保在 DTCM 中以加速访问)
// src_interleaved_q15: DMA 进来的源数据 (大小 256 * 16)
// dst_planar_q15: 转置后的临时 16-bit 平面数据 (大小 16 * 256)
// dst_planar_f32: 最终用于 DSP 处理的浮点平面数据 (大小 16 * 256)

void Deinterleave_Using_Matrix(q15_t *src_interleaved_q15, 
                               q15_t *dst_planar_q15, 
                               float32_t *dst_planar_f32, 
                               uint16_t frame_size, 
                               uint16_t num_channels) 
{
    // 1. 定义源矩阵和目标矩阵实例
    arm_matrix_instance_q15 mat_src;
    arm_matrix_instance_q15 mat_dst;

    // 2. 初始化矩阵结构体
    // 源矩阵: frame_size 行，num_channels 列
    arm_mat_init_q15(&mat_src, frame_size, num_channels, src_interleaved_q15);
    // 目标矩阵: num_channels 行，frame_size 列
    arm_mat_init_q15(&mat_dst, num_channels, frame_size, dst_planar_q15);

    // 3. 执行矩阵转置 (底层自动调用 SIMD 优化指令，速度极快)
    arm_mat_trans_q15(&mat_src, &mat_dst);

    // 4. 将 16-bit 的 q15 平面数据批量转换为 float32_t (也是 SIMD 加速)
    arm_q15_to_float(dst_planar_q15, dst_planar_f32, frame_size * num_channels);
}