/**
 * @file    mpu.h
 * @author  四角函数sin
 * @brief   STM32H7 内存保护单元 (MPU) 与内存布局定义
 * @details 定义内存区域宏和 MPU 配置函数
 *
 * 内存布局：
 * - SRAM1 (0x30000000): Non-Cacheable, DMA 缓冲区
 * - DTCM  (0x20000000): 零等待, CPU 密集计算区
 * - AXI SRAM (0x24000000): Cacheable, 大容量缓冲区
 * - SDRAM (0xC0000000): Cacheable/Non-Cacheable 混合
 *
 * 使用方法：
 * - 在变量定义前添加内存区域宏
 * - 例如：__SECTION_DMA_BUFFER int16_t dma_buf[1024];
 */

#ifndef __MPU_H__
#define __MPU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ============================================================================
 * 内存区域宏定义 (Memory Section Macros)
 * ============================================================================ */

/**
 * @brief   DMA 专用缓冲段 (SRAM1 - 0x30000000)
 * @details 此区域在 MPU 中被配置为 Non-Cacheable
 *
 * 适用场景：
 * - SAI 接收缓冲 (Mic_Rx_Buffer)
 * - UART DMA 缓冲
 * - ADC DMA 缓冲
 * - 其他 DMA 外设缓冲
 *
 * 特点：
 * - CPU 读取速度较慢 (无缓存加速)
 * - 保证与 DMA 数据完全一致
 * - 无需手动 Invalidate 缓存
 *
 * 为什么 Non-Cacheable？
 * - DMA 直接写入内存，CPU 读取时必须从内存读
 * - 避免 CPU 读到缓存中的旧数据
 * - 牺牲读取速度，换取数据正确性
 *
 * 使用示例：
 * @code
 * __SECTION_DMA_BUFFER int16_t audio_buffer[8192];
 * @endcode
 */
#if defined (__GNUC__)
  #define __SECTION_DMA_BUFFER   __attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
#else
  #define __SECTION_DMA_BUFFER    __attribute__((section(".dma_buffer")))  // Keil/IAR 请自行适配 section 定义
#endif

/**
 * @brief   DSP 极速计算段 (DTCM - 0x20000000)
 * @details 此区域是 Cortex-M7 的紧耦合内存，性能最强
 *
 * 适用场景：
 * - FFT 输入/输出数组 (Mic_Process_Buffer, Mic_Freq_Buffer)
 * - 算法中间变量 (Hanning_Window, S_Rfft)
 * - 堆栈 (Stack)
 * - 频繁访问的小数组
 *
 * 特点：
 * - CPU 读写 0 等待 (最快)
 * - 无需 Cache (紧耦合到 CPU)
 * - DMA 无法访问 (仅 CPU 可访问)
 * - 容量有限 (128KB)
 *
 * 为什么 0 等待？
 * - DTCM 直接连接到 CPU 核心
 * - 无需经过总线仲裁
 * - 无需等待内存响应
 *
 * 使用示例：
 * @code
 * __SECTION_DTCM float32_t fft_buffer[4096];
 * @endcode
 */
#if defined (__GNUC__)
  #define __SECTION_DTCM         __attribute__((section(".dtcm_data"))) __attribute__((aligned(32)))
#else
  #define __SECTION_DTCM          __attribute__((section(".dtcm_data")))  // Keil/IAR 请自行适配 section 定义
#endif

/**
 * @brief   大容量通用段 (AXI SRAM - 0x24000000)
 * @details 默认开启 Cache (Write-Back)
 *
 * 适用场景：
 * - 图像处理缓冲 (非 DMA 部分)
 * - 大数组 (GCC_PHAT_Buffer, SRP_Power)
 * - FreeRTOS Heap
 * - 通用数据存储
 *
 * 特点：
 * - 容量大 (512KB)
 * - Cacheable (顺序访问性能好)
 * - DMA 可访问 (需手动刷新缓存)
 * - 访问速度中等
 *
 * 为什么 Cacheable？
 * - 大数组顺序访问时，缓存命中率高
 * - 提升 CPU 访问性能
 * - 适合算法中间结果存储
 *
 * 使用示例：
 * @code
 * __SECTION_AXI_SRAM float32_t large_buffer[100000];
 * @endcode
 */
#define __SECTION_AXI_SRAM     __attribute__((section(".axi_sram_data"))) __attribute__((aligned(32)))  // 默认就是这里，通常无需特殊修饰

/* ============================================================================
 * 函数声明 (Function Declarations)
 * ============================================================================ */

/**
 * @brief   配置 MPU (Memory Protection Unit)
 * @details 配置 3 个 MPU 区域，优化内存访问性能
 *
 * 配置区域：
 * - Region 0: SRAM1 (0x30000000, 256KB) - Non-Cacheable
 * - Region 1: SDRAM (0xC0000000, 32MB) - Cacheable
 * - Region 2: SDRAM (0xC0000000, 2MB) - Non-Cacheable (覆盖 Region 1)
 *
 * 调用时机：
 * - 必须在 HAL_Init() 之后
 * - 必须在 SystemClock_Config() 之后
 * - 必须在 FreeRTOS 启动前
 *
 * 调用位置：
 * - main.c 中 main() 函数
 * - 在 HAL_Init() 和 SystemClock_Config() 之间
 *
 * @note    配置错误会导致 HardFault 或数据错误
 * @note    修改 MPU 配置后需要重新编译和烧录
 */
void App_MPU_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* __MPU_H__ */