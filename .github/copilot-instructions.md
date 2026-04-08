# NECCS Project — Copilot Instructions

> **STM32H743 Acoustic Camera — 16-Channel Real-Time Sound Source Localization**

## Project Basics

- **MCU**: STM32H743IIT6 @ 480 MHz, Cortex-M7, 2 MB Flash, 1 MB on-chip SRAM
- **RTOS**: FreeRTOS v10.3.1, CMSIS-RTOS v2
- **Toolchain**: Keil MDK-ARM V5, ARM Compiler 5 (`armcc`, **NOT** `armclang`/`gcc`)
- **Language**: C99
- **Debugger**: ST-Link V2/V3
- **Active branch**: `touch_part`

## Audio Pipeline

1. **16-channel PDM microphone array**
2. **ADC**: PCMD3180 ×2 (I²C addr `0x4C`, `0x4D`), PDM → TDM conversion
3. **SAI**: TDM16 mode @ 48 kHz, 32-bit per slot
4. **DMA**: Circular double-buffer mode, buffers in D2 SRAM (non-cacheable)
5. **Processing**: Deinterleave (SIMD) → FFT (CMSIS DSP `arm_cfft_f32`) → SRP-PHAT → `Sound_Pos_t`

## Algorithm — SRP-PHAT

- Phase transform: GCC-PHAT
- **Coarse scan**: 9×9 grid (±60°, 15° step = 81 points)
- **Fine scan**: 4×4 around Top-3 peaks (±10°, 5° step = 48 points)
- **Total**: 129 scan points per frame
- LUT-based: steering vectors pre-computed by `tools/generate_srp_lut.py`

## Memory Layout (CRITICAL — must be respected)

| Region | Address | Size | Usage | Attribute |
|--------|---------|------|-------|-----------|
| DTCM | `0x20000000` | 128 KB | Zero-wait-state, FFT/SRP scratch | `__attribute__((section(".dtcm")))` |
| AXI SRAM | `0x24000000` | 512 KB | Cacheable, GCC-PHAT data | (default) |
| D2 SRAM | `0x30000000` | 256 KB | Non-cacheable, DMA buffers | `__attribute__((section(".RAM_D2")))` |
| SDRAM | `0xC0000000` | 32 MB | External via FMC, display framebuffers | (external) |

## FreeRTOS Tasks

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `PCMD3180_Init_Task` | osPriorityRealtime (6) | 512 words | One-shot ADC init, self-deletes |
| `Audio_Pipeline_Task` | osPriorityHigh (4) | 4096 words | Audio capture + SRP-PHAT |
| `UI_Task` | osPriorityHigh (4) | 4096 words | LVGL + display + touch |
| `Default_Task` | osPriorityNormal (1) | 1024 words | CLI + idle diagnostics |

## Display

- 800×480 LCD via LTDC + DMA2D, double-buffered
- LVGL v8 GUI framework
- Touch: GT9xxx or FT5206 via software I²C

## Directory Structure

```
START/
  Core/        — CubeMX-generated HAL init (main.c, stm32h7xx_it.c, etc.)
  Drivers/     — CMSIS + HAL (DO NOT MODIFY)
  Middlewares/ — LVGL, FreeRTOS (DO NOT MODIFY)
  User/
    Algorithm/ — SRP-PHAT core (ai_beamforming, ai_preprocess, ai_srp_lut, ai_config)
    App/       — Application tasks, UI, CLI, display rendering
    BSP/       — Board support (LCD, SDRAM, TOUCH, etc.)
    Hardware/  — Low-level drivers (camera_ov2640, soft_i2c)
    common/    — Shared utilities (error_code.h, dwt_timer.h/c)
  MDK-ARM/     — Keil project files
```

## Code Style Requirements

1. **Naming**: `Module_FunctionName()` for public API (e.g., `AI_FFT_Process()`), `lowercase_snake` for local vars
2. **Header guards**: `#ifndef __FILENAME_H` / `#define __FILENAME_H` (double underscore prefix)
3. **Comments**: Doxygen `/** @brief ... */` for all public functions
4. **Include order**: HAL headers → CMSIS → FreeRTOS → BSP → App → Algorithm
5. **No dynamic allocation** in real-time paths (Audio task). Use static buffers.
6. **No printf/logging** in time-critical code paths (use DWT cycle counters via `dwt_timer.h`)

## Key Constraints (MUST OBSERVE)

1. DMA buffers **MUST** be in D2 SRAM (`__attribute__((section(".RAM_D2")))`)
2. DMA buffers **MUST** have cache management (`SCB_CleanDCache` / `SCB_InvalidateDCache`) or be in non-cacheable region
3. MPU is configured — do **NOT** change MPU region settings without understanding the full memory map
4. `arm_cfft_f32` works **IN-PLACE** — input buffer is overwritten with output
5. SAI DMA callback is ISR context — minimize work, use `xQueueSendFromISR`
6. LVGL is **NOT** thread-safe — all LVGL calls must be in `UI_Task` only
7. UART baud rate is **921600** (not 115200)
8. ARM Compiler 5 (`armcc`) — does **NOT** support all C11/GNU extensions

## Files You Must NOT Modify

- `Drivers/CMSIS/**` — ARM CMSIS headers
- `Drivers/STM32H7xx_HAL_Driver/**` — ST HAL library
- `Middlewares/Third_Party/FreeRTOS/**` — FreeRTOS kernel
- `Middlewares/LVGL/**` — LVGL library
- `User/Algorithm/ai_srp_lut.c` — Auto-generated LUT data

## Error Handling

- New code should use `Err_t` from `common/error_code.h`:
  - `ERR_OK` (0), `ERR_INVALID_ARG` (-1), `ERR_IO_FAILED` (-2), `ERR_TIMEOUT` (-3), `ERR_NOT_INIT` (-4), `ERR_BUSY` (-5), `ERR_NOT_FOUND` (-6)
- Legacy code uses mixed `0`/`1`/`HAL_StatusTypeDef` — do not change existing patterns unless refactoring

## Commit Message Convention

```
<type>(<scope>): <subject>

Types: feat, fix, refactor, docs, style, perf, test, chore
Scopes: algorithm, app, bsp, hardware, common, docs, tools
```

## CLI Commands (via UART at 921600 baud)

The project has a CLI accessible via `app_ui_cli.c` with ~20 commands for runtime configuration:

- `srp scan`, `srp scan_fine`, `srp alpha <val>`, `srp nms <val>`
- `disp mode <heatmap|spectrum|camera>`, `disp alpha <val>`
- `cam init`, `cam start`, `cam stop`
- `perf report`, `perf toggle`
- `sys reboot`, `sys heap`

## Build & Debug

1. Open `START/MDK-ARM/START.uvprojx` in Keil MDK
2. Build target: `START`
3. Flash via ST-Link
4. Serial monitor: **921600** baud, 8N1

## References

- `docs/DEVELOPER_GUIDE.md` — Full architecture and developer documentation
- `docs/USER_MANUAL.md` — End-user operation guide
- `docs/PROJECT_REVIEW.md` — Known issues and improvement roadmap
- `tools/generate_srp_lut.py` — LUT generation script with array geometry
- `tools/array_32ch_design.py` — Future 32-channel array design tool
