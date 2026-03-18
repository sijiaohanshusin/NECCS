# NECCS Project Snapshot

- generated_at: 2026-03-18T19:18:28+08:00
- repo_root: D:\HEU\NECCS\NECCS
- git_branch: camera
- git_head: 5b76b89240baf3dfccbe721a20d7860ae93f7ac9

## Git Summary

- latest_commit: feat:
- latest_commit_date: 2026-03-15T20:59:28+08:00
- working_tree_dirty_files: 25

### Recent Commits

- 5b76b89 feat:
- a4bbcd5 refactor: :art: 工程整理/清空工作区，为摄像头接入做准备
- 13a939c style: :building_construction: 工程重构
- e9d4d42 style: :art: 继续完善注释
- c9e923b style: :art: 添加完善注释
- b340fd7 refactor(runtime): unify display runtime config and refresh module docs
- 3628151 fix(cli): improve uart cfg command handling and sync latest runtime updates
- f0b1323 feat(ltdc): add dma2d accel path and runtime diagnostics updates

## Key Modules

| Module | Path | Note |
| --- | --- | --- |
| Boot and platform init | `START/Core/Src/main.c` | HAL init, MPU/cache, clocks, peripheral setup. |
| MPU memory layout | `START/Core/Src/mpu.c` | DMA-safe vs cacheable memory regions. |
| Main RTOS tasks | `START/User/App/app_main_task.c` | Audio pipeline and UI loop, runtime knobs, CLI. |
| Task API and perf enum | `START/User/App/app_main_task.h` | Public handles/counters and perf section ids. |
| Display render pipeline | `START/User/App/app_display.c` | Field prep, norm, upsample, overlay, commit. |
| Display runtime config API | `START/User/App/app_display.h` | Modes, interpolation, normalization config. |
| Display compile-time knobs | `START/User/App/app_display_cfg.h` | Field size, smoothing, dynamic range, defaults. |
| UART/VOFA output | `START/User/App/app_data_output.c` | Data streaming and baud assumptions. |

## Runtime Knobs

- UI_FPS_MIN: (not found)
- UI_FPS_MAX: (not found)
- UI_FPS_DEFAULT: (not found)
- AUDIO_ALGO_DECIM_DEFAULT: (not found)
- APP_DISPLAY_FIELD_W: (not found)
- APP_DISPLAY_FIELD_H: (not found)
- APP_DISPLAY_DYNAMIC_DB_FLOOR: (not found)
- APP_DISPLAY_DYNAMIC_GAMMA: (not found)
- APP_DISPLAY_BILINEAR_SAMPLING: (not found)

## RTOS Objects

- tasks:
  - (none)
- queues:
  - xAudioFrameQueue
  - xPositionQueue

## UI CLI Commands

- (none found)

## Performance Sections

- (none found)

## Manual Context

- See `references/project_manual.md` for stable notes that should not be auto-generated.
