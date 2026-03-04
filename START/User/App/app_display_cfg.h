#ifndef APP_DISPLAY_CFG_H
#define APP_DISPLAY_CFG_H

/*
 * Compile-time display configuration.
 * Keep this file focused on UI visualization tuning knobs.
 */

/* Layout */
#define APP_DISPLAY_MARGIN_PX              8u
#define APP_DISPLAY_TEXT_WIDTH_PX          180u
#define APP_DISPLAY_MAX_LINE_PIXELS        1280u
#define APP_DISPLAY_BLIT_ROWS_MAX          8u

/* Temporary RAM pressure knob:
 * 0 = quality first (default), 1 = balanced, 2 = memory saver.
 * If link overflow appears again, increase this first.
 */
#define APP_DISPLAY_RAM_SAVE_LEVEL         0u

/* Low-resolution scalar field used for smoothing and upsampling */
#if (APP_DISPLAY_RAM_SAVE_LEVEL == 0u)
#define APP_DISPLAY_FIELD_W                96u
#define APP_DISPLAY_FIELD_H                96u
#elif (APP_DISPLAY_RAM_SAVE_LEVEL == 1u)
#define APP_DISPLAY_FIELD_W                72u
#define APP_DISPLAY_FIELD_H                72u
#else
#define APP_DISPLAY_FIELD_W                56u
#define APP_DISPLAY_FIELD_H                56u
#endif

/* Coarse field blur (separable Gaussian-like) */
#define APP_DISPLAY_SMOOTH_ENABLE          1u
#define APP_DISPLAY_SMOOTH_RADIUS          2u
#define APP_DISPLAY_SMOOTH_SIGMA           1.05f
#define APP_DISPLAY_SMOOTH_PASSES          1u

/* Fine-grid fusion into the low-resolution field */
#define APP_DISPLAY_FINE_FUSION_ENABLE     1u
#define APP_DISPLAY_FINE_KERNEL_RADIUS     4u
#define APP_DISPLAY_FINE_KERNEL_SIGMA      2.0f
#define APP_DISPLAY_FINE_GAIN              0.65f
#define APP_DISPLAY_FINE_MIN_RATIO         0.10f

/* Dynamic normalization (dB mapping + EMA peak tracking) */
#define APP_DISPLAY_DYNAMIC_DB_FLOOR       (-45.0f)
#define APP_DISPLAY_EMA_ATTACK             0.65f
#define APP_DISPLAY_EMA_DECAY              0.08f
#define APP_DISPLAY_EMA_MIN_PEAK           (1.0e-7f)
#define APP_DISPLAY_DYNAMIC_GAMMA          1.10f
#define APP_DISPLAY_NOISE_GATE_RATIO       0.08f
#define APP_DISPLAY_NOISE_ADAPT_GAIN       2.00f

/* Overlay and diagnostics */
#define APP_DISPLAY_DIAG_OVERLAY           1u
#define APP_DISPLAY_DRAW_COARSE_GRID       0u
#define APP_DISPLAY_DEBUG_LOG              0u
#define APP_DISPLAY_IDLE_TEST_PATTERN      0u
#define APP_DISPLAY_TEXT_REFRESH_DIV       2u
#define APP_DISPLAY_BILINEAR_SAMPLING      1u

/* Markers */
#define APP_DISPLAY_CROSSHAIR_HALF_PX      6u
#define APP_DISPLAY_PEAK_MARKER_RADIUS_PX  4u

/* SAI DMA "alive" hold window in UI frames */
#define APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES 10u

#if (APP_DISPLAY_FIELD_W < 16u) || (APP_DISPLAY_FIELD_H < 16u)
#error "APP_DISPLAY_FIELD_W/H too small"
#endif

#if (APP_DISPLAY_SMOOTH_RADIUS > 4u)
#error "APP_DISPLAY_SMOOTH_RADIUS too large"
#endif

#if (APP_DISPLAY_FINE_KERNEL_RADIUS > 8u)
#error "APP_DISPLAY_FINE_KERNEL_RADIUS too large"
#endif

#if (APP_DISPLAY_BLIT_ROWS_MAX < 1u) || (APP_DISPLAY_BLIT_ROWS_MAX > 16u)
#error "APP_DISPLAY_BLIT_ROWS_MAX must be in [1,16]"
#endif

#endif /* APP_DISPLAY_CFG_H */
