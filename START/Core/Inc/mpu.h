/**
 * @file    mpu.h
 * @author  Four-Corner Function sin
 * @brief   STM32H7 内存保护单元 (MPU) 与内存分区宏定义
 * @details 定义常用段属性宏与 MPU 配置函数声明。
 *
 * 当前内存划分（与 `App_MPU_Config()` 保持一致）：
 * - SRAM1/SRAM2 (0x30000000 起，256KB)：MPU 设为 Non-Cacheable，适合 DMA 共享缓冲
 * - DTCM (0x20000000 起)：CPU 紧耦合内存，低延迟，不适合 DMA 访问
 * - AXI SRAM (0x24000000 起)：默认 Cacheable，适合大容量通用数据
 * - SDRAM (0xC0000000 起)：大部分 Cacheable，前 2MB 被单独覆盖为 Non-Cacheable
 *
 * 使用方式：
 * - 在变量定义前添加对应段属性宏
 * - 示例：`__SECTION_DMA_BUFFER int16_t dma_buf[1024];`
 */

#ifndef __MPU_H__
#define __MPU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ============================================================================
 * 内存分区宏 (Memory Section Macros)
 * ============================================================================ */

/**
 * @brief   DMA 专用缓冲段（SRAM1/SRAM2，0x30000000 起）
 * @details 该区域在 MPU 中配置为 Non-Cacheable，用于 DMA 与 CPU 共享数据。
 *
 * 适用场景：
 * - SAI 接收缓冲（如 Mic_Rx_Buffer）
 * - UART / ADC 等 DMA 缓冲
 * - 其他外设 DMA 读写区
 *
 * 注意事项：
 * - 该区读写不走 D-Cache，CPU 访问速度可能低于 Cacheable 区域
 * - 优点是避免缓存一致性问题，通常无需手动清/失效缓存
 *
 * 使用示例：
 * @code
 * __SECTION_DMA_BUFFER int16_t audio_buffer[8192];
 * @endcode
 */
#if defined (__GNUC__)
  #define __SECTION_DMA_BUFFER   __attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
  #define __SECTION_D2_SRAM       __attribute__((section(".d2_sram_data"))) __attribute__((aligned(32)))
#else
  #define __SECTION_DMA_BUFFER    __attribute__((section(".dma_buffer")))  // Keil/IAR 需按工程链接脚本适配
  #define __SECTION_D2_SRAM       __attribute__((section(".d2_sram_data")))
#endif

/**
 * @brief   DSP 高速计算段（DTCM，0x20000000 起）
 * @details 该区域为 Cortex-M7 的紧耦合内存（TCM），访问延迟低。
 *
 * 适用场景：
 * - FFT 输入/输出数组（如 Mic_Process_Buffer、Mic_Freq_Buffer）
 * - 算法中间变量（如 Hanning_Window、S_Rfft）
 * - 高频访问的小型数据块
 *
 * 注意事项：
 * - 对 CPU 读写非常友好，但容量有限
 * - 通常 DMA 无法直接访问 DTCM，DMA 共享数据请放到 SRAM/SDRAM 对应分区
 *
 * 使用示例：
 * @code
 * __SECTION_DTCM float32_t fft_buffer[4096];
 * @endcode
 */
#if defined (__GNUC__)
  #define __SECTION_DTCM         __attribute__((section(".dtcm_data"))) __attribute__((aligned(32)))
#else
  #define __SECTION_DTCM          __attribute__((section(".dtcm_data")))  // Keil/IAR 需按工程链接脚本适配
#endif

/**
 * @brief   大容量通用段（AXI SRAM，0x24000000 起）
 * @details 该区域默认可缓存（Cacheable），适合 CPU 主导的通用数据。
 *
 * 适用场景：
 * - 图像/信号处理中的非 DMA 区域
 * - 大数组（如 GCC_PHAT_Buffer、SRP_Power）
 * - FreeRTOS Heap 与其他通用数据
 *
 * 注意事项：
 * - 容量大、CPU 顺序访问性能好
 * - 若 DMA 与 CPU 同时访问，需自行管理缓存一致性
 *
 * 使用示例：
 * @code
 * __SECTION_AXI_SRAM float32_t large_buffer[100000];
 * @endcode
 */
#define __SECTION_AXI_SRAM     __attribute__((section(".axi_sram_data"))) __attribute__((aligned(32)))  // 默认放入 AXI SRAM；通常无需额外处理

/* ============================================================================
 * 函数声明 (Function Declarations)
 * ============================================================================ */

/**
 * @brief   配置 MPU (Memory Protection Unit)
 * @details 配置 3 个 MPU 区域，平衡性能与 DMA/显示一致性。
 *
 * 当前区域配置：
 * - Region 0: SRAM1 (0x30000000, 256KB) - Non-Cacheable
 * - Region 1: SDRAM (0xC0000000, 32MB) - Cacheable
 * - Region 2: SDRAM (0xC0000000, 2MB) - Non-Cacheable（覆盖 Region 1 的前 2MB）
 *
 * 调用时机：
 * - 建议在 `HAL_Init()` 与 `SystemClock_Config()` 完成后调用
 * - 必须在 FreeRTOS 启动前完成
 *
 * 调用位置：
 * - `main.c` 的 `main()` 初始化阶段
 *
 * @note    区域大小、地址、属性不匹配可能导致 HardFault 或数据异常
 * @note    修改 MPU 配置后需重新编译并完整下载验证
 */
void App_MPU_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* __MPU_H__ */
