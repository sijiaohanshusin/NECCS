---
description: "Use when editing board support packages, hardware drivers, I2C, camera, SDRAM, or any low-level peripheral code in User/Hardware/ or User/BSP/."
applyTo: "START/User/Hardware/**,START/User/BSP/**"
---
# Hardware & BSP Constraints

## DMA Bus Access (CRITICAL)

- DMA1/DMA2 masters connect via AHB — they can access D2 SRAM (`0x30000000`) but **NOT** DTCM (`0x20000000`)
- MDMA can access all memory regions but is not used for audio
- Always verify: which bus can the DMA master reach? Place buffers accordingly.

## MPU Configuration

- MPU regions are pre-configured for the memory layout — do **NOT** modify without explicit approval
- Key regions: DTCM (non-cacheable, non-shareable), AXI SRAM (write-back cacheable), D2 SRAM (non-cacheable), SDRAM (write-through cacheable)
- Changing MPU settings can silently corrupt DMA data or cause hard faults

## Software I²C (`User/Hardware/soft_i2c`)

- Bit-banged I²C for touch controller and camera — not HAL I²C
- Timing is GPIO-toggle based — sensitive to interrupt latency
- Do not call from ISR context
- PCMD3180 ADC init uses this I²C (addresses `0x4C`, `0x4D`)

## Camera (OV2640)

- Connected via DCMI interface
- Init sequence in `camera_ov2640.c` — register writes via software I²C
- Camera and audio can run simultaneously but share I²C bus — guard with mutex if needed

## SDRAM (FMC)

- 32 MB external SDRAM at `0xC0000000`
- Used for: LVGL framebuffers, camera frame buffers, large data arrays
- FMC timing is configured in `BSP_SDRAM_Init()` — do not modify timing parameters
- SDRAM refresh must be maintained — long interrupt-disable windows can cause data loss

## LCD (LTDC)

- RGB interface via LTDC peripheral
- Two layers available: background (camera/video) + foreground (LVGL overlay)
- Layer swap is synchronized to VSYNC to avoid tearing
- DMA2D is used for fast rectangular fills and pixel format conversion

## HAL Usage Patterns

- Use `HAL_*` functions for peripheral init and control
- Check return values: `HAL_OK`, `HAL_ERROR`, `HAL_BUSY`, `HAL_TIMEOUT`
- For GPIO: use `HAL_GPIO_WritePin()` / `HAL_GPIO_ReadPin()`, not register-direct access
- Clock enable must precede any peripheral access: `__HAL_RCC_*_CLK_ENABLE()`
