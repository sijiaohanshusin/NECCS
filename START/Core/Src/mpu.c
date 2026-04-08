/**
 * @file    mpu.c
 * @brief   STM32H743 MPU 内存保护单元配置
 * @details 为 NECCS 声学相机系统各内存区域配置 MPU 属性，
 *          确保 DMA 一致性与 Cache 策略正确。
 *
 * 区域划分：
 * - Region 0: D2 SRAM (0x30000000, 256KB) — 非缓存、可共享，供 DMA 直接访问
 * - Region 1: SDRAM 全局 (0xC0000000, 32MB) — 可缓存、可缓冲，CPU 数据访问
 * - Region 2: LTDC 帧缓冲 (0xC0000000, 4MB) — 非缓存，避免显示撕裂
 * - Region 3: DCMI 采集窗口 (0xC0400000, 1MB) — 非缓存，摄像头 DMA 目标
 *
 * MPU 区域优先级规则：
 * - 编号大的区域优先级高，Region 2/3 覆盖 Region 1 对应子区间的属性
 *
 * @note    由 main.c 中 App_MPU_Config() 调用，在 HAL_Init() 之后、时钟配置之前执行
 */

#include "mpu.h"

/**
 * @brief   配置工程自定义 MPU 区域
 * @details 禁用 MPU 后逐区域配置内存属性，最后重新启用。
 *          各区域的缓存策略需与外设 DMA 行为匹配，否则会引发数据不一致。
 *
 * @note    调用前需确保 MPU 已处于可配置状态
 * @note    配置完成后以 MPU_PRIVILEGED_DEFAULT 模式启用，
 *          未映射区域使用默认背景区属性
 */
void App_MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* 配置前必须先禁用 MPU，否则修改区域属性会触发 HardFault */
    HAL_MPU_Disable();

    /* Region 0: D2 SRAM (0x30000000, 256KB)
     * 用途：SAI DMA 接收缓冲区（Mic_Rx_Buffer）所在区域
     * 策略：非缓存、可共享 — DMA 直接读写，CPU 无需手动 Cache 维护
     * TEX=1, C=0, B=0, S=1 对应 Strongly-ordered 等效属性 */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x30000000u;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_256KB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* Region 1: SDRAM 全局 (0xC0000000, 32MB)
     * 用途：通用数据存储（算法缓冲、纹理、字库等）
     * 策略：可缓存、可缓冲、不共享 — 提供最佳 CPU 访问性能
     * TEX=0, C=1, B=1, S=0 对应 Write-Back, No Write-Allocate */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress      = 0xC0000000u;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* Region 2: LTDC 帧缓冲 (0xC0000000, 4MB)
     * 用途：LCD 显示帧缓冲区（LTDC 外设直接读取）
     * 策略：非缓存、可共享 — 避免 Cache 延迟导致显示撕裂/花屏
     * 优先级高于 Region 1，覆盖 SDRAM 起始 4MB 的缓存属性 */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
    MPU_InitStruct.BaseAddress      = 0xC0000000u;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_4MB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* Region 3: DCMI 摄像头采集窗口 (0xC0400000, 1MB)
     * 用途：OV2640 摄像头 DMA 原始帧数据目标地址
     * 策略：非缓存、可共享 — DCMI DMA 直接写入，CPU 直接读取
     * 地址在 Region 1 范围内，通过更高编号覆盖缓存属性 */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER3;
    MPU_InitStruct.BaseAddress      = 0xC0400000u;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_1MB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 以特权默认模式启用 MPU：未映射区域按默认背景区处理 */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
