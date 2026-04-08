/**
 * @file    app_data_stream.h
 * @brief   音频数据流缓冲区定义与初始化
 * @details 定义音频处理流水线的所有关键缓冲区，并按内存特性分配到不同区域
 *
 * 内存布局策略：
 * - SRAM1 (0x30000000): Non-Cacheable, DMA 安全区
 * - DTCM  (0x20000000): 零等待访问，CPU 密集计算区
 * - AXI SRAM (0x24000000): Cacheable, 大容量缓冲区
 */

#ifndef APP_DATA_STREAM_H
#define APP_DATA_STREAM_H

#include "ai_config.h"
#include "arm_math.h"

/* ========== DMA 接收缓冲区 (SRAM1, Non-Cacheable) ========== */

/**
 * @brief   SAI DMA 双缓冲区 (PING/PONG)
 * @note    内存区域：SRAM1 (0x30000000), Non-Cacheable
 *          数据格式：int16_t, 交织排列 (ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ...)
 *          大小：8192 × 2 字节 = 16 KB
 *          对齐：32 字节 (DMA 缓存行对齐)
 *
 * 为什么放在 SRAM1 Non-Cacheable 区？
 * - DMA 直接写入，避免缓存一致性问题
 * - CPU 读取时直接从内存读，保证数据最新
 * - 牺牲读取速度，换取数据正确性
 */
extern int16_t Mic_Rx_Buffer[DMA_BUFFER_SIZE];

/* ========== 处理缓冲区 (DTCM, 零等待) ========== */

/**
 * @brief   解交织后的时域浮点缓冲区
 * @note    内存区域：DTCM (0x20000000), 零等待访问
 *          数据格式：float32_t, 平面排列 (ch0: [0..255], ch1: [256..511], ...)
 *          大小：16 × 256 × 4 字节 = 16 KB
 *          对齐：32 字节 (SIMD 优化)
 *
 * 为什么放在 DTCM？
 * - FFT 预处理 (去直流、加窗) 需要频繁访问
 * - DTCM 零等待，无缓存延迟
 * - 紧耦合到 Cortex-M7 核心，访问速度最快
 */
extern float32_t Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN];

/**
 * @brief   FFT 频域复数缓冲区
 * @note    内存区域：DTCM (0x20000000), 零等待访问
 *          数据格式：float32_t, 复数交织 ([Re0, Im0, Re1, Im1, ...])
 *          大小：16 × 256 × 4 字节 = 16 KB
 *          对齐：32 字节 (SIMD 优化)
 *
 * RFFT 输出格式 (N=256):
 * - [0]   = DC 实部
 * - [1]   = 奈奎斯特频率实部
 * - [2k]  = bin k 实部, [2k+1] = bin k 虚部 (k=1..127)
 *
 * 为什么放在 DTCM？
 * - GCC-PHAT 计算需要频繁访问频域数据
 * - 复数运算密集，零等待访问提升性能
 */
extern float32_t Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN];

/**
 * @brief   RFFT 实例 (CMSIS-DSP)
 * @note    内存区域：DTCM (0x20000000)
 *          包含旋转因子表 (Twiddle Factors)
 *          初始化：arm_rfft_fast_init_f32(&S_Rfft, FRAME_LEN)
 */
extern arm_rfft_fast_instance_f32 S_Rfft;

/**
 * @brief   Hanning 窗函数
 * @note    内存区域：DTCM (0x20000000)
 *          公式：w[n] = 0.5 * (1 - cos(2π*n/N))
 *          大小：256 × 4 字节 = 1 KB
 *          初始化：App_Stream_Init() 中生成
 */
extern float32_t Hanning_Window[FRAME_LEN];

/* ========== SRP-PHAT 缓冲区 (AXI SRAM, Cacheable) ========== */

/**
 * @brief   GCC-PHAT 白化互相关缓冲区
 * @note    内存区域：AXI SRAM (0x24000000), Cacheable
 *          数据格式：float32_t, 复数交织
 *          大小：40 对 × 40 bins × 2 × 4 字节 = 12.8 KB
 *          对齐：32 字节 (缓存行对齐)
 *
 * 为什么放在 AXI SRAM？
 * - 容量较大 (512 KB)，适合存储中间结果
 * - Cacheable，顺序访问时性能良好
 * - 不需要 DTCM 级别的零等待访问
 */
extern float32_t GCC_PHAT_Buffer[SRP_PAIR_COUNT * SRP_FREQ_BINS * 2u];

/**
 * @brief   SRP 功率网格缓冲区
 * @note    内存区域：AXI SRAM (0x24000000), Cacheable
 *          数据格式：float32_t, 一维数组
 *          布局：[粗搜索 81 点 | 精细搜索 48 点]
 *          大小：129 × 4 字节 = 516 字节
 *          对齐：32 字节 (缓存行对齐)
 *
 * 为什么放在 AXI SRAM？
 * - 需要全局最大值搜索，顺序访问友好
 * - 大小较小，缓存命中率高
 */
extern float32_t SRP_Power[SRP_GRID_TOTAL];

/* ========== 初始化函数 ========== */

/**
 * @brief   初始化音频数据流
 * @details 执行以下初始化：
 *          1. RFFT 实例初始化 (arm_rfft_fast_init_f32)
 *          2. Hanning 窗函数生成
 *          3. SRP-PHAT 算法状态初始化
 * @note    在 FreeRTOS 启动前调用 (main.c 中)
 */
void App_Stream_Init(void);

#endif /* APP_DATA_STREAM_H */
