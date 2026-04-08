/**
 * @file    dwt_timer.h
 * @brief   DWT 周期计数器公共工具
 * @details 封装 ARM Cortex-M7 Data Watchpoint and Trace (DWT) 单元，
 *          提供微秒级延时和周期计数功能。
 *
 * 背景：
 * - soft_i2c.c 和 camera_ov2640.c 各自独立初始化 DWT，存在代码重复
 * - 本模块将 DWT 初始化统一为一个入口，确保只初始化一次
 *
 * DWT 原理：
 * - CYCCNT 寄存器是 32-bit 自由运行计数器
 * - 每个 CPU 时钟周期加 1
 * - @ 480MHz: 1 tick ≈ 2.08ns, 最大计时 ≈ 8.9 秒
 *
 * 使用方式：
 * @code
 *   DWT_Timer_Init();                        // 系统启动时调用一次
 *   uint32_t start = DWT_Timer_GetCycles();  // 记录起始周期
 *   // ... 待测量代码 ...
 *   uint32_t elapsed = DWT_Timer_GetCycles() - start;
 *   float us = (float)elapsed / (SystemCoreClock / 1000000);
 * @endcode
 */

#ifndef DWT_TIMER_H
#define DWT_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   初始化 DWT 周期计数器
 * @details 使能跟踪单元和 CYCCNT 计数器。
 *          多次调用安全 (内部检查已使能标志)。
 *
 * 寄存器操作：
 * - CoreDebug->DEMCR |= TRCENA  (使能跟踪/调试)
 * - DWT->CYCCNT = 0              (清零计数器)
 * - DWT->CTRL |= CYCCNTENA       (使能周期计数)
 */
void DWT_Timer_Init(void);

/**
 * @brief   获取当前 DWT 周期计数值
 * @return  CYCCNT 当前值 (32-bit, 自由运行)
 * @note    差值计算自动处理溢出: elapsed = now - start (无符号减法)
 */
static inline uint32_t DWT_Timer_GetCycles(void)
{
    extern volatile uint32_t *const _dwt_cyccnt_ptr;
    return *_dwt_cyccnt_ptr;
}

/**
 * @brief   DWT 微秒级延时
 * @param   us  延时时间 (微秒)
 * @note    精度受中断影响，适用于软件 I2C 等非实时场景
 * @note    @ 480MHz: 最大延时约 8.9 秒
 */
void DWT_Timer_DelayUs(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* DWT_TIMER_H */
