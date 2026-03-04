# Display/Link Memory Notes

Date: 2026-03-04

## Current status

- Scatter layout is now fixed and tracked:
  - keep `.dtcm_data` + stack/heap in DTCM
  - place generic `+RW/+ZI` in AXI SRAM
  - keep D2 SRAM explicit for `.dma_buffer` and `.d2_sram_data`
  - keep optional `.sdram_data` section in SDRAM
- Tracked scatter files:
  - `START/MDK/START.sct`
  - `START/MDK-ARM/START/START.sct`

Important:
- `.gitignore` now explicitly keeps the above scatter files (and `START/MDK-ARM/START.uvprojx`) under version control.

## Display-side RAM pressure knob

- `START/User/App/app_display_cfg.h`
- Macro: `APP_DISPLAY_RAM_SAVE_LEVEL`
  - `0`: quality first
  - `1`: balanced
  - `2`: memory saver

If link overflow returns, raise this value first.

## Remaining recommendation

- Continue moving any future large arrays into explicit sections (`.axi_sram_data`, `.d2_sram_data`, `.dma_buffer`, `.dtcm_data`) instead of relying on default placement.
- Revisit RTOS heap/stack budgets with map-file based sizing.
