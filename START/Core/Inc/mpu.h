/**
  ******************************************************************************
  * @file    mpu.h
  * @author  四角函数sin
  * @brief   STM32H7 Memory Protection Unit & Memory Layout Definitions
  ******************************************************************************
  */

#ifndef __MPU_H__
#define __MPU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ============================================================================== */
/* 内存区域宏定义                                   */
/* ============================================================================== */

/**
 * @brief  DMA 专用缓冲段 (SRAM1 - 0x30000000)
 * @note   此区域在 MPU 中被配置为 [Non-Cacheable]。
 * 适用于: SAI接收缓冲、UART DMA缓冲、ADC DMA缓冲。
 * 特点: CPU读取速度较慢，但保证与 DMA 数据完全一致，无需手动 Invalidate。
 */
#if defined (__GNUC__)
  #define __SECTION_DMA_BUFFER   __attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
#else
  #define __SECTION_DMA_BUFFER    __attribute__((section(".dma_buffer")))// Keil/IAR 请自行适配 section 定义
#endif

/**
 * @brief  DSP 极速计算段 (DTCM - 0x20000000)
 * @note   此区域是 Cortex-M7 的紧耦合内存，性能最强，但 **DMA 无法访问**。
 * 适用于: FFT 输入/输出数组、算法中间变量、堆栈(Stack)。
 * 特点: CPU 读写 0 等待，无需 Cache。
 */
#if defined (__GNUC__)
  #define __SECTION_DTCM         __attribute__((section(".dtcm_data"))) __attribute__((aligned(32)))
#else
  #define __SECTION_DTCM          __attribute__((section(".dtcm_data")))// Keil/IAR 请自行适配 section 定义
#endif

/**
 * @brief  大容量通用段 (AXI SRAM - 0x24000000)
 * @note   默认开启 Cache (Write-Back)。
 * 适用于: 图像处理缓冲(非DMA部分)、大数组、FreeRTOS Heap。
 */
#define __SECTION_AXI_SRAM     __attribute__((section(".axi_sram_data"))) __attribute__((aligned(32)))// 默认就是这里，通常无需特殊修饰


/* ============================================================================== */
/* 函数声明                                         */
/* ============================================================================== */

/**
 * @brief  配置 MPU (Memory Protection Unit)
 * @note   必须在 HAL_Init() 之后，SystemClock_Config() 之后，且在 FreeRTOS 启动前调用。
 */
void App_MPU_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* __MPU_H__ */