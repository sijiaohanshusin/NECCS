---
description: "Use when editing audio capture, SAI TDM16, DMA double-buffer, ISR callbacks, audio task, or any code that handles microphone data from SAI to beamforming."
applyTo: "START/User/App/app_audio*,START/Core/Src/stm32h7xx_it.c"
---
# Audio Pipeline Constraints

## Pipeline Overview

```
16ch PDM mics → PCMD3180 (PDM→TDM) → SAI (TDM16) → DMA → D2 SRAM buffer
→ Deinterleave (SIMD) → FFT (CMSIS DSP) → SRP-PHAT → Sound_Pos_t
```

## SAI Configuration

- Mode: TDM16 @ 48 kHz, 32-bit per slot
- Two PCMD3180 ADCs: I²C addresses `0x4C` (ch 0–7) and `0x4D` (ch 8–15)
- SAI clock must be configured before DMA starts

## DMA Rules (CRITICAL)

- **Circular double-buffer mode** — DMA continuously fills two halves
- All DMA buffers **MUST** be in D2 SRAM: `__attribute__((section(".RAM_D2")))`
- D2 SRAM is on the AHB bus that DMA1/DMA2 can access — DTCM is NOT accessible by DMA
- Buffer size: `NUM_CHANNELS * FRAME_SIZE * 2` (double-buffer)
- After DMA half-complete/complete callback: data is ready in the inactive half

## ISR Safety (CRITICAL)

- SAI DMA callbacks (`HAL_SAI_RxHalfCpltCallback`, `HAL_SAI_RxCpltCallback`) run in ISR context
- In ISR: **only** set flags or send to queue using `xQueueSendFromISR()` / `xSemaphoreGiveFromISR()`
- **NEVER** in ISR: `printf`, `malloc`, `vTaskDelay`, `xQueueSend` (non-ISR variant), LVGL calls
- Keep ISR execution under 1 µs — just notify the audio task

## Cache Coherency

- D2 SRAM is configured as non-cacheable via MPU — no manual cache management needed for DMA buffers there
- If any buffer is in cacheable region (AXI SRAM): call `SCB_InvalidateDCache_by_Addr()` before reading DMA data
- If CPU writes data for DMA to read: call `SCB_CleanDCache_by_Addr()` before starting DMA transfer

## Audio Task (`Audio_Pipeline_Task`)

- Priority: `osPriorityHigh` (4), Stack: 4096 words
- Blocks on queue/semaphore waiting for DMA half-complete notification
- Processing sequence: deinterleave → per-channel FFT → GCC-PHAT → SRP scan → result output
- **No dynamic allocation** — all buffers are statically allocated
- **No printf** — use DWT timer for profiling

## FreeRTOS Tasks Reference

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `PCMD3180_Init_Task` | osPriorityRealtime (6) | 512 words | One-shot ADC init, self-deletes |
| `Audio_Pipeline_Task` | osPriorityHigh (4) | 4096 words | Audio capture + SRP-PHAT |
| `UI_Task` | osPriorityHigh (4) | 4096 words | LVGL + display + touch |
| `Default_Task` | osPriorityNormal (1) | 1024 words | CLI + idle diagnostics |
