/**
  ******************************************************************************
  * @file    mpu.c
  * @author  四角函数sin
  * @brief   MPU Configuration for STM32H743IIT6
  ******************************************************************************
  */

#include "mpu.h"

void App_MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* 1. 禁止 MPU */
    HAL_MPU_Disable();

    /* ======================================================================
       配置区域 1: D2 SRAM1 & SRAM2 (0x30000000 - 0x30040000)
       目标: 设置为 Normal, Non-Cacheable (禁止缓存)
       用途: 存放 SAI DMA 的音频接收数据
       大小: 256KB (SRAM1 128KB + SRAM2 128KB 物理连续)
    ====================================================================== */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;       // 区域编号 0
    MPU_InitStruct.BaseAddress      = 0x30000000;               // SRAM1 起始地址
    MPU_InitStruct.Size             = MPU_REGION_SIZE_256KB;    // 覆盖 SRAM1 + SRAM2
    MPU_InitStruct.SubRegionDisable = 0x0;
    
    /* 核心配置: TEX=1, C=0, B=0 => Normal, Non-Cacheable */
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;     // 必须设为 Shareable 以供 DMA 访问
    
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   // 读写权限
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE; // 禁止代码执行(防止跑飞到数据区)
    
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* ======================================================================
       配置区域 2: AXI SRAM (0x24000000) - 可选
       目标: 确保开启 Cache (Write-Back)
       STM32H7 默认行为通常已是 WB，但显式配置更安全
    ====================================================================== */
    /* MPU_InitStruct.Number = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress = 0x24000000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
    // ... 配置为 Cacheable Write-Back ...
    // 既然你是初次调通，先保持默认即可，避免引入额外复杂度。
    // AXI SRAM 默认就是 Cacheable 的。
    */

    /* ======================================================================
       配置区域 1: SDRAM (0xC0000000 - 0xC2000000)
       目标: Normal, Write-Back, No Write-Allocate
       用途: 摄像头显存 + 热力图叠加缓冲
       大小: 32MB
    ====================================================================== */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress      = 0xC0000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;     /* Write-Back */
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;

    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 2. 使能 MPU */
    /* MPU_PRIVILEGED_DEFAULT: 开启 MPU 后，特权级模式下使用背景映射(即未定义区域使用默认属性) */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}