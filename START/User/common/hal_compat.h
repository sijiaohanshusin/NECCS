/**
 * @file    hal_compat.h
 * @brief   HAL 兼容层 —— 为 STM32N657 移植预留的抽象接口
 * @details
 * 本头文件定义了一组平台无关的宏和类型别名，
 * 使应用层代码通过这些宏与底层 HAL 交互，
 * 而不是直接调用 STM32H743 的 HAL 函数。
 *
 * 当前（H743 构建）：宏直接映射到 HAL_xxx 函数。
 * 未来（N657 构建）：修改本文件中的映射关系即可完成移植，
 * 应用层代码无需改动。
 *
 * 使用示例：
 * @code
 *   #include "hal_compat.h"
 *   COMPAT_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
 *   COMPAT_SAI_Receive_DMA(&hsai_BlockA1, buf, len);
 * @endcode
 */
#ifndef __HAL_COMPAT_H
#define __HAL_COMPAT_H

/* ============================================================================
 * 平台检测 (Platform Detection)
 * ============================================================================ */

#if defined(STM32H743xx) || defined(STM32H7xx)
  #define COMPAT_PLATFORM_H743   1
  #define COMPAT_PLATFORM_N657   0
  #include "stm32h7xx_hal.h"
#elif defined(STM32N657xx) || defined(STM32N6xx)
  #define COMPAT_PLATFORM_H743   0
  #define COMPAT_PLATFORM_N657   1
  /* #include "stm32n6xx_hal.h" */  /* TODO: N657 HAL 头文件 */
  #error "STM32N657 HAL not yet integrated — update hal_compat.h"
#else
  #error "Unsupported platform — define STM32H743xx or STM32N657xx"
#endif

/* ============================================================================
 * GPIO 抽象 (GPIO Abstraction)
 * ============================================================================ */

#define COMPAT_GPIO_WritePin(port, pin, state) \
    HAL_GPIO_WritePin((port), (pin), (state))

#define COMPAT_GPIO_ReadPin(port, pin) \
    HAL_GPIO_ReadPin((port), (pin))

#define COMPAT_GPIO_TogglePin(port, pin) \
    HAL_GPIO_TogglePin((port), (pin))

/* ============================================================================
 * SAI 抽象 (SAI Abstraction)
 * ============================================================================ */

#define COMPAT_SAI_Receive_DMA(hsai, buf, len) \
    HAL_SAI_Receive_DMA((hsai), (buf), (len))

#define COMPAT_SAI_DMAStop(hsai) \
    HAL_SAI_DMAStop((hsai))

/* ============================================================================
 * I2C 抽象 (I2C Abstraction)
 * ============================================================================ */

#define COMPAT_I2C_Mem_Write(hi2c, addr, reg, regsize, data, len, timeout) \
    HAL_I2C_Mem_Write((hi2c), (addr), (reg), (regsize), (data), (len), (timeout))

#define COMPAT_I2C_Mem_Read(hi2c, addr, reg, regsize, data, len, timeout) \
    HAL_I2C_Mem_Read((hi2c), (addr), (reg), (regsize), (data), (len), (timeout))

/* ============================================================================
 * DMA Cache 管理 (Cache Management)
 * ============================================================================ */

#define COMPAT_CacheClean(addr, size) \
    SCB_CleanDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))

#define COMPAT_CacheInvalidate(addr, size) \
    SCB_InvalidateDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))

/* ============================================================================
 * 延迟 (Delay)
 * ============================================================================ */

#define COMPAT_Delay_ms(ms)   HAL_Delay((ms))

/* ============================================================================
 * 内存域标注 (Memory Section Attributes)
 * ============================================================================ */

/** @brief DTCM 数据段（零等待周期，仅 CPU 可访问） */
#define COMPAT_SECTION_DTCM     __attribute__((section(".dtcm_data")))

/** @brief D2 SRAM（DMA 可访问，非缓存区） */
#define COMPAT_SECTION_DMA_BUF  __attribute__((section(".dma_buffer")))

/** @brief AXI SRAM 显式分配 */
#define COMPAT_SECTION_AXI      __attribute__((section(".axi_sram_data")))

#endif /* __HAL_COMPAT_H */
