/**
 * @file    app_data_stream.c
 * @brief   音频数据流缓冲区定义与初始化实现
 * @details 按内存特性分配缓冲区到不同的 RAM 区域，优化访问性能
 *
 * 内存分配策略：
 * - __SECTION_DMA_BUFFER: SRAM1 (0x30000000), Non-Cacheable, MPU 配置
 * - __SECTION_DTCM: DTCM (0x20000000), 零等待, 紧耦合内存
 * - __SECTION_AXI_SRAM: AXI SRAM (0x24000000), Cacheable, 大容量
 *
 * 这些宏定义在 mpu.h 中，通过链接脚本和 MPU 配置实现
 */

#include "app_data_stream.h"
#include "ai_beamforming.h"
#include "mpu.h"

#include <math.h>

/* ========== DMA 接收缓冲区 (SRAM1, Non-Cacheable) ========== */

/**
 * @brief   SAI DMA 双缓冲区
 * @note    - 内存区域：SRAM1, Non-Cacheable (避免缓存一致性问题)
 *          - 对齐：32 字节 (DMA 缓存行对齐，提升传输效率)
 *          - 初始化：零值 (避免启动时的随机噪声)
 */
__SECTION_DMA_BUFFER __attribute__((aligned(32)))
int16_t Mic_Rx_Buffer[DMA_BUFFER_SIZE] = {0};

/* ========== 处理缓冲区 (DTCM, 零等待) ========== */

/**
 * @brief   解交织后的时域浮点缓冲区
 * @note    - 内存区域：DTCM (零等待访问，CPU 密集计算区)
 *          - 对齐：32 字节 (SIMD 优化，arm_mult_f32 等函数要求)
 *          - 数据流：DMA → 解交织 → 此缓冲区 → FFT
 */
__SECTION_DTCM __attribute__((aligned(32)))
float32_t Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN] = {0.0f};

/**
 * @brief   FFT 频域复数缓冲区
 * @note    - 内存区域：DTCM (零等待访问，复数运算密集)
 *          - 对齐：32 字节 (SIMD 优化，arm_cmplx_* 函数要求)
 *          - 数据流：FFT → 此缓冲区 → GCC-PHAT
 */
__SECTION_DTCM __attribute__((aligned(32)))
float32_t Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN] = {0.0f};

/**
 * @brief   RFFT 实例
 * @note    - 内存区域：DTCM (包含旋转因子表，频繁访问)
 *          - 初始化：arm_rfft_fast_init_f32(&S_Rfft, FRAME_LEN)
 */
__SECTION_DTCM
arm_rfft_fast_instance_f32 S_Rfft;

/**
 * @brief   Hanning 窗函数
 * @note    - 内存区域：DTCM (FFT 预处理时逐点相乘)
 *          - 对齐：32 字节 (SIMD 优化，arm_mult_f32 要求)
 *          - 公式：w[n] = 0.5 * (1 - cos(2π*n/N))
 */
__SECTION_DTCM __attribute__((aligned(32)))
float32_t Hanning_Window[FRAME_LEN] = {0.0f};

/* ========== SRP-PHAT 缓冲区 (AXI SRAM, Cacheable) ========== */

/**
 * @brief   GCC-PHAT 白化互相关缓冲区
 * @note    - 内存区域：AXI SRAM (容量大，顺序访问友好)
 *          - 对齐：32 字节 (缓存行对齐，提升缓存命中率)
 *          - 大小：40 对 × 40 bins × 2 × 4 字节 = 12.8 KB
 */
__SECTION_AXI_SRAM __attribute__((aligned(32)))
float32_t GCC_PHAT_Buffer[SRP_PAIR_COUNT * SRP_FREQ_BINS * 2u] = {0.0f};

/**
 * @brief   SRP 功率网格缓冲区
 * @note    - 内存区域：AXI SRAM (全局最大值搜索，顺序访问)
 *          - 对齐：32 字节 (缓存行对齐)
 *          - 布局：[粗搜索 64 点 | 精细搜索 48 点]
 */
__SECTION_AXI_SRAM __attribute__((aligned(32)))
float32_t SRP_Power[SRP_GRID_TOTAL] = {0.0f};

/**
 * @brief   初始化音频数据流
 * @details 执行以下初始化：
 *          1. RFFT 实例初始化 (256 点)
 *          2. Hanning 窗函数生成
 *          3. SRP-PHAT 算法状态初始化
 *
 * @note    调用时机：main.c 中 FreeRTOS 启动前
 *          耗时：约 1ms (主要是窗函数生成)
 */
void App_Stream_Init(void)
{
    /* 1. 初始化 RFFT 实例 (256 点) */
    /* 内部会初始化旋转因子表 (Twiddle Factors) */
    arm_rfft_fast_init_f32(&S_Rfft, FRAME_LEN);

    /* 2. 生成 Hanning 窗函数 */
    /* 公式：w[n] = 0.5 * (1 - cos(2π*n/N)) */
    /* 作用：减少频谱泄漏，提升 FFT 频率分辨率 */
    for (uint32_t n = 0u; n < FRAME_LEN; n++)
    {
        Hanning_Window[n] = 0.5f *
                            (1.0f - cosf(2.0f * 3.14159265358979f * (float32_t)n / (float32_t)FRAME_LEN));
    }

    /* 3. 初始化 SRP-PHAT 算法状态 */
    /* 清零诊断计数器，重置上次有效定位结果 */
    AI_SRP_PHAT_Init();
}

