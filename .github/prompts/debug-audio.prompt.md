---
description: "Debug audio pipeline issues: SAI, DMA, microphone data, beamforming, or sound localization problems."
agent: "neccs-embedded-engineer"
tools: [read, search]
argument-hint: "Describe the audio problem: no data, wrong angles, noise, dropouts, etc."
---
Diagnose audio pipeline issues in the NECCS acoustic camera.

## Diagnostic Checklist

### 1. No Audio Data
- Is PCMD3180 initialized? Check `PCMD3180_Init_Task` completion
- Are I²C addresses correct? (`0x4C` for ch 0–7, `0x4D` for ch 8–15)
- Is SAI clock configured and running?
- Is DMA started? Check `HAL_SAI_Receive_DMA()` return value
- Are DMA buffers in D2 SRAM? (DTCM buffers = silent failure)
- Are DMA callbacks firing? (`HAL_SAI_RxHalfCpltCallback`, `HAL_SAI_RxCpltCallback`)

### 2. Corrupted/Garbled Data
- Cache coherency: is D2 SRAM configured as non-cacheable in MPU?
- Buffer alignment: is DMA buffer 32-byte aligned?
- Buffer size: does it match the DMA transfer count?
- Deinterleave: are channels being extracted in the correct TDM slot order?
- Endianness: PCMD3180 outputs MSB-first 32-bit samples

### 3. Wrong Beamforming Angles
- Is the complex conjugate applied to the correct operand in GCC-PHAT?
- Are steering vectors (LUT) consistent with physical mic positions?
- Is the noise floor converged? (needs ~10 frames of data)
- Run `tools/srp_doa_sanity_check.py` with known source positions to validate

### 4. Audio Dropouts
- Is Audio_Pipeline_Task priority high enough? Check for priority inversion
- Is processing completing within the frame period (~5.3 ms)?
- Use DWT timer to measure actual processing time
- Check for other tasks disabling interrupts for too long

### 5. Performance Issues
- Profile with `DWT_StartTimer()` / `DWT_GetElapsed_us()` from `dwt_timer.h`
- Check FFT size vs. available DTCM space
- Are SIMD intrinsics used for deinterleave? (`__PKHTB`, `__PKHBT`)

Read the relevant source files and report findings with specific file/line references.
