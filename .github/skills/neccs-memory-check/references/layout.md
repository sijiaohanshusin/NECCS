# NECCS Memory Layout — Complete Reference

## STM32H743 Memory Map

```
0x00000000 ┌──────────────────┐
           │  ITCM (64 KB)    │  Instruction TCM — not used for data
0x00010000 ├──────────────────┤
           │  ...             │
0x08000000 ├──────────────────┤
           │  Flash (2 MB)    │  Code + const data
0x080FFFFF ├──────────────────┤
           │  ...             │
0x20000000 ├──────────────────┤
           │  DTCM (128 KB)   │  ← FFT scratch, SRP workspace
           │                  │    Zero-wait-state, no DMA access
0x2001FFFF ├──────────────────┤
           │  ...             │
0x24000000 ├──────────────────┤
           │  AXI SRAM        │  ← Default heap/stack, GCC-PHAT data
           │  (512 KB)        │    Write-back cacheable
0x2407FFFF ├──────────────────┤
           │  ...             │
0x30000000 ├──────────────────┤
           │  D2 SRAM         │  ← DMA buffers (SAI, SPI, UART)
           │  (256 KB)        │    Non-cacheable via MPU
0x3003FFFF ├──────────────────┤
           │  ...             │
0x38000000 ├──────────────────┤
           │  D3 SRAM (64 KB) │  Not actively used in this project
0x3800FFFF ├──────────────────┤
           │  ...             │
0xC0000000 ├──────────────────┤
           │  SDRAM (32 MB)   │  ← Framebuffers (LVGL, camera)
           │  via FMC         │    Write-through cacheable
0xC1FFFFFF └──────────────────┘
```

## Section Attribute Quick Reference

```c
// DMA buffer — MUST be in D2 SRAM
__attribute__((section(".RAM_D2"), aligned(32)))
static uint8_t sai_dma_buf[BUF_SIZE];

// FFT scratch — best in DTCM for zero-wait-state
__attribute__((section(".dtcm")))
static float fft_scratch[FFT_SIZE * 2];

// Default placement — goes to AXI SRAM
static float gcc_phat_data[N_PAIRS * FFT_SIZE];

// SDRAM — use pointer, initialized at runtime
static uint16_t *lvgl_fb = (uint16_t *)0xC0000000;
```

## Scatter File Sections (MDK/START.sct)

| Section Name | Region | Used For |
|-------------|--------|----------|
| `.dtcm` | DTCM | FFT/SRP scratch arrays |
| `.RAM_D2` | D2 SRAM | DMA buffers |
| (default RW) | AXI SRAM | Global/static variables |
| (heap) | AXI SRAM | FreeRTOS heap (pvPortMalloc) |

## DMA Accessibility Matrix

| Memory Region | DMA1/DMA2 | MDMA | CPU |
|--------------|:---------:|:----:|:---:|
| DTCM | ✗ | ✓ | ✓ |
| AXI SRAM | ✓ | ✓ | ✓ |
| D2 SRAM | ✓ | ✓ | ✓ |
| D3 SRAM | ✓ | ✓ | ✓ |
| SDRAM | ✓ | ✓ | ✓ |
| Flash | ✓ (read) | ✓ | ✓ |

## Cache Configuration per Region

| Region | Cache Policy | DMA requires cache mgmt? |
|--------|-------------|--------------------------|
| DTCM | Non-cacheable | N/A (DMA can't access) |
| AXI SRAM | Write-back | Yes — Clean before DMA TX, Invalidate after DMA RX |
| D2 SRAM | Non-cacheable (MPU) | No |
| D3 SRAM | Non-cacheable (MPU) | No |
| SDRAM | Write-through | Invalidate after DMA RX only |
