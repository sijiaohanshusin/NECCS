/**
 * @file    dwt_timer.c
 * @brief   DWT 周期计数器公共工具实现
 * @details 将 soft_i2c.c 和 camera_ov2640.c 中重复的 DWT 初始化代码统一封装
 *
 * 硬件依赖：
 * - ARM Cortex-M7 DWT 单元 (所有 STM32H7 均支持)
 * - CoreDebug 外设 (ARM 调试架构标准)
 * - SystemCoreClock 全局变量 (CMSIS 标准)
 */

#include "dwt_timer.h"
#include "stm32h7xx.h"  /* 提供 CoreDebug, DWT, SystemCoreClock */

/* ---------- 内部状态 ---------- */

/** @brief  CYCCNT 寄存器指针 (供内联函数使用) */
volatile uint32_t *const _dwt_cyccnt_ptr = &(DWT->CYCCNT);

/** @brief  初始化标志，防止重复初始化 */
static uint8_t s_dwt_inited = 0u;

/* ---------- 公共接口 ---------- */

void DWT_Timer_Init(void)
{
    if (s_dwt_inited != 0u)
    {
        return;  /* 已初始化，跳过 */
    }

    /* 使能跟踪单元 (Trace Enable) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* 清零并使能周期计数器 */
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    s_dwt_inited = 1u;
}

void DWT_Timer_DelayUs(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;

    if (us == 0u)
    {
        return;
    }

    /* 自动初始化 (安全保护) */
    if (s_dwt_inited == 0u)
    {
        DWT_Timer_Init();
    }

    ticks = (SystemCoreClock / 1000000u) * us;
    start = DWT->CYCCNT;

    /* 忙等待 (无符号差值自动处理溢出) */
    while ((uint32_t)(DWT->CYCCNT - start) < ticks)
    {
        /* spin */
    }
}
