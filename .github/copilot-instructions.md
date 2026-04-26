# NECCS Project ！ Copilot Instructions

> **STM32H743 Acoustic Camera ！ 16-Channel Real-Time Sound Source Localization**

## Project Basics

- **MCU**: STM32H743IIT6 @ 480 MHz, Cortex-M7, 2 MB Flash, 1 MB on-chip SRAM
- **RTOS**: FreeRTOS v10.3.1, CMSIS-RTOS v2
- **Toolchain**: Keil MDK-ARM V5, ARM Compiler 5 (`armcc`, **NOT** `armclang`/`gcc`)
- **Language**: C99
- **Debugger**: ST-Link V2/V3
- **Active branch**: `touch_part`

## Memory Layout (CRITICAL)

| Region | Address | Size | Usage | Attribute |
|--------|---------|------|-------|-----------|
| DTCM | `0x20000000` | 128 KB | Zero-wait-state, FFT/SRP scratch | `__attribute__((section(".dtcm")))` |
| AXI SRAM | `0x24000000` | 512 KB | Cacheable, GCC-PHAT data | (default) |
| D2 SRAM | `0x30000000` | 256 KB | Non-cacheable, DMA buffers | `__attribute__((section(".RAM_D2")))` |
| SDRAM | `0xC0000000` | 32 MB | External via FMC, display framebuffers | (external) |

## Key Constraints (MUST OBSERVE)

1. DMA buffers **MUST** be in D2 SRAM (`__attribute__((section(".RAM_D2")))`)
2. DMA buffers **MUST** have cache management or be in non-cacheable region
3. MPU is configured ！ do **NOT** change MPU region settings without explicit approval
4. `arm_cfft_f32` works **IN-PLACE** ！ input buffer is overwritten with output
5. SAI DMA callback is ISR context ！ minimize work, use `xQueueSendFromISR`
6. LVGL is **NOT** thread-safe ！ all LVGL calls must be in `UI_Task` only
7. UART baud rate is **921600** (not 115200)
8. ARM Compiler 5 (`armcc`) ！ does **NOT** support all C11/GNU extensions
9. **No dynamic allocation** (`malloc`/`free`/`pvPortMalloc`) in Audio task or ISR handlers
10. **No printf/logging** in time-critical code paths ！ use DWT cycle counters via `dwt_timer.h`

## Code Style

1. **Naming**: `Module_FunctionName()` for public API, `lowercase_snake` for local vars
2. **Header guards**: `#ifndef __FILENAME_H` / `#define __FILENAME_H` (double underscore prefix)
3. **Comments**: Doxygen `/** @brief ... */` for all public functions
4. **Include order**: HAL -> CMSIS -> FreeRTOS -> BSP -> App -> Algorithm

## Directory Structure

```
START/
  Core/        ！ CubeMX-generated HAL init (DO NOT MODIFY for custom logic)
  Drivers/     ！ CMSIS + HAL (DO NOT MODIFY)
  Middlewares/ ！ LVGL, FreeRTOS (DO NOT MODIFY)
  User/
    Algorithm/ ！ SRP-PHAT core (ai_beamforming, ai_preprocess, ai_srp_lut, ai_config)
    App/       ！ Application tasks, UI, CLI, display rendering
    BSP/       ！ Board support (LCD, SDRAM, TOUCH, etc.)
    Hardware/  ！ Low-level drivers (camera_ov2640, soft_i2c)
    common/    ！ Shared utilities (error_code.h, dwt_timer.h/c)
  MDK-ARM/     ！ Keil project files
```

## Files You Must NOT Modify

- `Drivers/CMSIS/**` and `Drivers/STM32H7xx_HAL_Driver/**`
- `Middlewares/Third_Party/FreeRTOS/**` and `Middlewares/LVGL/**`
- `User/Algorithm/ai_srp_lut.c` ！ Auto-generated LUT data

## Error Handling

Use `Err_t` from `common/error_code.h`:
`ERR_OK` (0), `ERR_INVALID_ARG` (-1), `ERR_IO_FAILED` (-2), `ERR_TIMEOUT` (-3), `ERR_NOT_INIT` (-4), `ERR_BUSY` (-5), `ERR_NOT_FOUND` (-6)

Legacy code uses mixed `0`/`1`/`HAL_StatusTypeDef` ！ do not change existing patterns unless refactoring.

## Build & Debug

```powershell
# Build (from MDK-ARM directory)
C:\Keil_v5\UV4\UV4.exe -b START.uvprojx -j0 -t START -o build_log.txt
```

- Build target: `START`
- Flash via ST-Link
- Serial monitor: **921600** baud, 8N1

## Commit Convention

```
<type>(<scope>): <subject>
Types: feat, fix, refactor, docs, style, perf, test, chore
Scopes: algorithm, app, bsp, hardware, common, docs, tools
```

## References

- `docs/DEVELOPER_GUIDE.md` ！ Full architecture and API documentation
- `docs/USER_MANUAL.md` ！ End-user operation guide
- `docs/PROJECT_REVIEW.md` ！ Known issues and improvement roadmap
- `tools/generate_srp_lut.py` ！ LUT generation script with array geometry

## Team Protocol

The default agent operates as **NECCS Tech Lead** (`neccs-tech-lead`). Every code change goes through a structured multi-specialist discussion before implementation. See the Tech Lead agent for the full T0-T7 protocol.

| Agent | Domain |
|-------|--------|
| `neccs-architect` | Memory layout, module API, cross-module design, FreeRTOS topology |
| `neccs-algo-engineer` | SRP-PHAT math, FFT pipeline, DSP correctness, LUT, spectrum |
| `neccs-embedded-engineer` | HAL/DMA/ISR/RTOS/cache coherency/ARM compiler |
| `neccs-ui-engineer` | LVGL widgets, display pipeline, touch, chroma-key |
| `neccs-code-reviewer` | Quality, style, ARM Compiler 5 safety, error handling (**ALL code changes**) |
| `neccs-qa-engineer` | Build validation, regression risk, test scenarios (**ALL code changes**) |
