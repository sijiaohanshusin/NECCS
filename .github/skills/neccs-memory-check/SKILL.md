---
name: neccs-memory-check
description: "Verify memory placement correctness for DMA buffers, FFT scratch, framebuffers, and other critical data structures. Use when adding new buffers, reviewing DMA code, or debugging data corruption that may be caused by wrong memory region placement."
argument-hint: "Optional: file or buffer name to check"
---
# NECCS Memory Placement Verification

## When to Use

- Adding a new DMA buffer or modifying an existing one
- Adding a new large data array (FFT, beamforming, display)
- Debugging silent data corruption (classic symptom of wrong memory region)
- Reviewing code that touches SAI, DMA, LTDC, or DCMI peripherals

## Verification Procedure

### Step 1: Identify All Buffers

Search the codebase for buffer declarations. Focus on:

```
grep -rn "__attribute__.*section" START/User/
grep -rn "DMA\|dma\|Dma" START/User/ --include="*.c" --include="*.h"
grep -rn "uint8_t.*\[.*\]\|uint16_t.*\[.*\]\|uint32_t.*\[.*\]\|float.*\[.*\]" START/User/ --include="*.c"
```

### Step 2: Check Against Memory Map

For each buffer, verify it is in the correct region:

| Buffer Type | Required Region | Address Range | Attribute |
|-------------|----------------|---------------|-----------|
| DMA buffers (SAI, SPI, UART) | D2 SRAM | `0x30000000–0x3003FFFF` | `__attribute__((section(".RAM_D2")))` |
| FFT scratch / SRP workspace | DTCM | `0x20000000–0x2001FFFF` | `__attribute__((section(".dtcm")))` |
| GCC-PHAT cross-spectrum data | AXI SRAM | `0x24000000–0x2407FFFF` | (default, no attribute needed) |
| LVGL framebuffers | SDRAM | `0xC0000000–0xC1FFFFFF` | (external memory) |
| Camera frame buffers | SDRAM | `0xC0000000–0xC1FFFFFF` | (external memory) |

See [references/layout.md](./references/layout.md) for full memory map details.

### Step 3: Verify Bus Accessibility

**Critical check for DMA buffers:**
- DMA1/DMA2 connect via AHB → can access D2 SRAM ✓, AXI SRAM ✓, SDRAM ✓
- DMA1/DMA2 **CANNOT** access DTCM (different bus matrix)
- MDMA can access all regions but is not used for audio

### Step 4: Verify Cache Coherency

- D2 SRAM: configured as non-cacheable via MPU → no cache management needed ✓
- AXI SRAM: cacheable → need `SCB_InvalidateDCache_by_Addr()` after DMA write, `SCB_CleanDCache_by_Addr()` before DMA read
- SDRAM: write-through cacheable → writes are visible without clean, but invalidate after DMA write

### Step 5: Check Alignment

- DMA buffers: must be 32-byte aligned for cache line operations: `__attribute__((aligned(32)))`
- FFT buffers: must be aligned to the FFT instance requirements (typically 4-byte for float)

## Red Flags

- Buffer in DTCM used with DMA1/DMA2 → **WILL silently corrupt data**
- Buffer in AXI SRAM used with DMA but no cache invalidation → **stale data reads**
- Stack-allocated buffer passed to DMA → **use-after-return, undefined behavior**
- Buffer size not matching DMA transfer count → **buffer overflow**
