/**
 * @file    mpu.c
 * @brief   STM32H7 内存保护单元 (MPU) 配置
 * @details 配置 MPU 区域，优化内存访问性能和数据一致性
 *
 * MPU 配置策略：
 * - Region 0: SRAM1 (DMA 缓冲区) - Non-Cacheable
 * - Region 1: SDRAM (通用数据) - Cacheable
 * - Region 2: SDRAM (帧缓冲区) - Non-Cacheable (覆盖 Region 1)
 *
 * 为什么需要 MPU？
 * - DMA 和 CPU 共享内存时，缓存一致性问题
 * - MPU 可以配置特定区域为 Non-Cacheable，避免手动刷新缓存
 * - 提升系统稳定性和性能
 */

#include "mpu.h"

/**
 * @brief   配置 MPU (Memory Protection Unit)
 * @details 配置 3 个 MPU 区域，优化内存访问性能
 *
 * 配置原则：
 * 1. DMA 缓冲区：Non-Cacheable (避免缓存一致性问题)
 * 2. 通用数据：Cacheable (提升 CPU 访问速度)
 * 3. 帧缓冲区：Non-Cacheable (LTDC DMA 直接访问)
 *
 * 区域优先级：
 * - 编号越大，优先级越高
 * - Region 2 覆盖 Region 1 的前 2MB
 *
 * @note    必须在 HAL_Init() 之后，SystemClock_Config() 之后调用
 * @note    必须在 FreeRTOS 启动前调用
 */
void App_MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* 禁用 MPU (配置前必须禁用) */
    HAL_MPU_Disable();

    /* ========================================================================
     * Region 0: D2 SRAM1 + SRAM2 (0x30000000, 256KB)
     * ======================================================================== */
    /**
     * 用途：SAI DMA 接收缓冲区 (Mic_Rx_Buffer)
     *
     * 配置：Non-Cacheable, Non-Bufferable, Shareable
     *
     * 为什么 Non-Cacheable？
     * - DMA 直接写入内存，CPU 读取时必须从内存读
     * - 避免 CPU 读到缓存中的旧数据
     * - 牺牲读取速度，换取数据正确性
     *
     * 为什么 Non-Bufferable？
     * - 确保 CPU 写入立即生效，不经过写缓冲
     * - 对于 DMA 缓冲区，通常只读不写，影响不大
     *
     * 为什么 Shareable？
     * - DMA 和 CPU 共享此区域
     * - 确保多核或 DMA 访问时的一致性
     */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x30000000;  /* SRAM1 起始地址 */
    MPU_InitStruct.Size             = MPU_REGION_SIZE_256KB;  /* 256KB (SRAM1 + SRAM2) */
    MPU_InitStruct.SubRegionDisable = 0x0;  /* 不禁用子区域 */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;  /* TEX=001 (Normal memory) */
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;  /* 不可缓存 */
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;  /* 不可缓冲 */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;  /* 可共享 */
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;  /* 读写权限 */
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* 禁止执行 */
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* ========================================================================
     * Region 1: SDRAM 全窗口 (0xC0000000, 32MB)
     * ======================================================================== */
    /**
     * 用途：通用数据存储 (图像缓冲、大数组等)
     *
     * 配置：Cacheable, Bufferable, Non-Shareable
     *
     * 为什么 Cacheable？
     * - CPU 频繁访问的数据，缓存可大幅提升性能
     * - SDRAM 访问速度慢 (约 120MHz)，缓存命中率高时性能提升明显
     *
     * 为什么 Bufferable？
     * - CPU 写入时先写到写缓冲，不等待 SDRAM 响应
     * - 提升写入性能，减少 CPU 等待时间
     *
     * 为什么 Non-Shareable？
     * - 单核系统，无需多核一致性
     * - 简化缓存管理
     *
     * 注意：
     * - Region 2 会覆盖此区域的前 2MB (帧缓冲区)
     * - 实际 Cacheable 区域：2MB - 32MB
     */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress      = 0xC0000000;  /* SDRAM 起始地址 */
    MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;  /* 32MB */
    MPU_InitStruct.SubRegionDisable = 0x0;  /* 不禁用子区域 */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;  /* TEX=000 (Normal memory, Write-Back) */
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;  /* 可缓存 */
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;  /* 可缓冲 */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;  /* 不可共享 */
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;  /* 读写权限 */
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* 禁止执行 */
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* ========================================================================
     * Region 2: LTDC 帧缓冲区 (0xC0000000, 2MB)
     * ======================================================================== */
    /**
     * 用途：LCD 帧缓冲区 (LTDC DMA 访问)
     *
     * 配置：Non-Cacheable, Non-Bufferable, Shareable
     *
     * 为什么 Non-Cacheable？
     * - LTDC DMA 直接读取帧缓冲区
     * - CPU 写入像素后，LTDC 必须立即看到最新数据
     * - 避免 LTDC 读到缓存中的旧数据
     *
     * 为什么 Non-Bufferable？
     * - 确保 CPU 写入立即生效，不经过写缓冲
     * - 避免 LTDC 读到未刷新的数据
     *
     * 为什么 Shareable？
     * - LTDC DMA 和 CPU 共享此区域
     * - 确保访问一致性
     *
     * 优先级：
     * - Region 2 编号大于 Region 1，优先级更高
     * - 覆盖 Region 1 的前 2MB，配置为 Non-Cacheable
     * - 剩余 30MB 仍为 Cacheable (Region 1)
     *
     * 大小：
     * - 800×480×2 (RGB565) = 768KB
     * - 配置 2MB 留有余量 (双缓冲、UI 元素等)
     */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
    MPU_InitStruct.BaseAddress      = 0xC0000000;  /* SDRAM 起始地址 (与 Region 1 重叠) */
    MPU_InitStruct.Size             = MPU_REGION_SIZE_2MB;  /* 2MB (覆盖 Region 1 前 2MB) */
    MPU_InitStruct.SubRegionDisable = 0x0;  /* 不禁用子区域 */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;  /* TEX=001 (Normal memory) */
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;  /* 不可缓存 */
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;  /* 不可缓冲 */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;  /* 可共享 */
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;  /* 读写权限 */
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* 禁止执行 */
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 启用 MPU (特权模式默认允许访问) */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
