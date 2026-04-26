---
description: "Use when editing SRP-PHAT beamforming, GCC-PHAT, FFT pipeline, steering vector LUT, noise floor, spectrum analysis, or any code in User/Algorithm/."
applyTo: "START/User/Algorithm/**"
---
# Algorithm Module Constraints

## SRP-PHAT Pipeline

- **Coarse scan**: 9×9 grid (±60°, 15° step = 81 points)
- **Fine scan**: 4×4 around Top-3 peaks (±10°, 5° step = 48 points)
- **Total**: 129 scan points per frame
- Phase transform: GCC-PHAT (cross-power spectrum normalized by magnitude)

## FFT Rules

- Use `arm_cfft_f32()` from CMSIS DSP — it works **IN-PLACE** (input buffer is overwritten)
- FFT scratch buffers → place in DTCM (`__attribute__((section(".dtcm")))`) for zero-wait-state access
- Never allocate FFT buffers dynamically — use static arrays
- Always verify FFT size matches `arm_cfft_sR_f32_lenN` instance (N = 256, 512, 1024, etc.)

## LUT (Lookup Table)

- `ai_srp_lut.c` is **auto-generated** by `tools/generate_srp_lut.py` — **NEVER edit manually**
- LUT contains pre-computed steering vectors for all scan points × mic pairs
- To regenerate: `python tools/generate_srp_lut.py` (updates both `.c` and `.h`)
- Array geometry is defined in the generation script, not in firmware

## GCC-PHAT Implementation

- Cross-spectrum: `X_ij(f) = X_i(f) * conj(X_j(f))` — apply `conj()` to the **second** operand
- Normalize: `R_ij(f) = X_ij(f) / |X_ij(f)|` — add epsilon (`1e-10f`) to denominator to prevent NaN
- The result is complex-valued; both real and imaginary parts matter

## Numerical Safety

- Use `float` (not `double`) — Cortex-M7 FPU is single-precision only
- Add epsilon to all divisors: `1.0f / (magnitude + 1e-10f)`
- Clamp SRP power values to `[0, MAX_POWER]` before display mapping
- Noise floor must converge before beamforming results are trustworthy (typically 10+ frames)

## Performance Budget

- Full SRP-PHAT frame must complete within audio frame period (~5.3 ms at 48 kHz / 256 samples)
- Use `DWT_StartTimer()` / `DWT_GetElapsed_us()` from `dwt_timer.h` for cycle-accurate profiling
- No printf/logging in the processing loop

## Validation Reference

- `tools/srp_doa_sanity_check.py` — Python reference implementation for DOA angle validation
- Known-good test cases: point source at 0°, 30°, 60° azimuth should produce matching peaks
