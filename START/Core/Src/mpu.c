/**
 * @file    mpu.c
 * @brief   STM32H7 内存保护单元 (MPU) 配置实现
 * @details 配置多个内存区域属性，在性能与 DMA/显示一致性之间取得平衡。
 *
 * 当前策略：
 * - Region 0: SRAM1/SRAM2（DMA 共享缓冲）设为 Non-Cacheable
 * - Region 1: SDRAM 全区默认设为 Cacheable
 * - Region 2: SDRAM 前 2MB（帧缓冲）覆盖为 Non-Cacheable
 *
 * 这样可以减少手动缓存维护开销，并避免 DMA/LTDC 读取到过期缓存数据。
 */

#include "mpu.h"

/**
 * @brief   配置 MPU (Memory Protection Unit)
 * @details 初始化并启用 3 个 MPU 区域。
 *
 * 配置原则：
 * 1. DMA 共享区域：Non-Cacheable，优先保证一致性
 * 2. CPU 主访问区域：Cacheable，优先保证吞吐性能
 * 3. 帧缓冲区域：Non-Cacheable，确保 LTDC 读到最新像素
 *
 * 区域优先级：
 * - 区域编号越大，优先级越高
 * - Region 2 会覆盖 Region 1 的前 2MB
 *
 * @note    建议在 HAL/时钟初始化完成后调用，且需早于 FreeRTOS 启动
 */
void App_MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* 先关闭 MPU，避免在修改区域属性过程中出现不一致行为 */
    HAL_MPU_Disable();

    /* ========================================================================
     * Region 0: D2 SRAM1 + SRAM2 (0x30000000, 256KB)
     * ======================================================================== */
    /**
     * 用途：DMA 共享缓冲区（例如 SAI Rx 缓冲）。
     *
     * 属性：Non-Cacheable + Non-Bufferable + Shareable
     * - Non-Cacheable：DMA 写入后 CPU 直接从内存读取，避免旧缓存
     * - Non-Bufferable：CPU 写操作尽快落地，减少外设读取到旧值的风险
     * - Shareable：明确该区域由 CPU 与 DMA 共同访问
     */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x30000000;  /* SRAM1 基地址 */
    MPU_InitStruct.Size             = MPU_REGION_SIZE_256KB;  /* 256KB（SRAM1 + SRAM2） */
    MPU_InitStruct.SubRegionDisable = 0x0;  /* 不屏蔽子区域 */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;  /* TEX=001（Normal memory） */
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;  /* 不可缓存 */
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;  /* 不可缓冲 */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;  /* 可共享 */
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;  /* 全访问权限 */
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* 禁止执行 */
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* ========================================================================
     * Region 1: SDRAM 全区 (0xC0000000, 32MB)
     * ======================================================================== */
    /**
     * 用途：通用 SDRAM 数据区（大数组、通用缓冲等）。
     *
     * 属性：Cacheable + Bufferable + Non-Shareable
     * - Cacheable：提升 CPU 对 SDRAM 的读性能
     * - Bufferable：提升连续写入吞吐
     * - Non-Shareable：作为默认 CPU 主访问区，简化一致性模型
     *
     * 注意：该区前 2MB 会被 Region 2 覆盖为 Non-Cacheable。
     */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress      = 0xC0000000;  /* SDRAM 基地址 */
    MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;  /* 32MB */
    MPU_InitStruct.SubRegionDisable = 0x0;  /* 不屏蔽子区域 */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;  /* TEX=000（Normal memory, Write-Back） */
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;  /* 可缓存 */
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;  /* 可缓冲 */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;  /* 不可共享 */
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;  /* 全访问权限 */
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* 禁止执行 */
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* ========================================================================
     * Region 2: LTDC 帧缓冲区 (0xC0000000, 2MB)
     * ======================================================================== */
    /**
     * 用途：LCD 帧缓冲区（LTDC DMA 直接读取）。
     *
     * 属性：Non-Cacheable + Non-Bufferable + Shareable
     * - 覆盖 Region 1 的前 2MB，使帧缓冲与 LTDC 访问保持一致
     * - CPU 写像素后无需额外 Cache 维护，LTDC 可读取到最新数据
     *
     * 大小说明：2MB 高于单帧 RGB565(800x480)需求，留有双缓冲/界面元素余量。
     */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
    MPU_InitStruct.BaseAddress      = 0xC0000000;  /* SDRAM 基地址（与 Region 1 重叠） */
    MPU_InitStruct.Size             = MPU_REGION_SIZE_2MB;  /* 2MB（覆盖 Region 1 前 2MB） */
    MPU_InitStruct.SubRegionDisable = 0x0;  /* 不屏蔽子区域 */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;  /* TEX=001（Normal memory） */
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;  /* 不可缓存 */
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;  /* 不可缓冲 */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;  /* 可共享 */
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;  /* 全访问权限 */
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;  /* 禁止执行 */
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 重新启用 MPU：特权模式下未命中区域按默认内存映射处理 */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
