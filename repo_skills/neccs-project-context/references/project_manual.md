# NECCS Manual Context

本文件用于记录稳定且高价值的项目基线信息，供 AI 和工程协作复用。
自动刷新脚本会更新 `project_snapshot.*`，不会覆盖本文件。

## Environment

- MCU/SoC: `STM32H743IIT6 @ 480MHz`
- Board: `STM32H743IIT6 Core Board V1.3`（依据仓库资料文件名，若硬件版本变化需同步更新）
- IDE/Toolchain: `Keil MDK-ARM V5` + `ARM Compiler 5 (AC5)`
- Flash tool: `ST-Link V2/V3`（通过 Keil Download 烧录）
- Serial monitor setup:
  - 运行期 CLI: `USART1`, `921600`, `8N1`（参考 `APP_RUNTIME_NOTES.md`）
  - 调试观察: `VOFA+`（若仅看 CLI 文本，普通串口工具也可）
- Host development environment: `Windows + PowerShell`

## Build And Run

- Main build command:
  - 打开 `START/MDK-ARM/START.uvprojx`，选择目标配置后 `F7` 编译
  - 命令行构建（已确认 UV4 路径）：
    - `C:\Keil_v5\UV4\UV4.exe -b START\MDK-ARM\START.uvprojx`
- Main flash command:
  - 在 Keil 中 `F8` 烧录（ST-Link）
  - 当前无批处理/一键下载脚本要求
- Common debug command:
  - 配置与状态: `cfg help`, `cfg status`
  - 性能诊断: `cfg perf on`, `cfg perf dump`, `cfg perf off`
  - 刷新率/算法节流: `cfg uifps <5..30>`, `cfg algodecim <1..8>`
  - 显示质量开关: `cfg mode ...`, `cfg interp ...`, `cfg norm fast|full`

## Repo Conventions

- Branch strategy:
  - 当前活动分支常见为 `main`（见 snapshot）
  - 当前无强制 `feature/* -> PR -> main` 固定流程要求
- Commit message style:
  - 采用类似 Conventional Commits 的风格：`type(scope): message`
  - 当前仓库可见样式示例：`feat(display): ...`, `fix(cli): ...`, `refactor ...`
- Testing checklist before commit:
  - Keil 编译通过（至少当前使用的目标配置）
  - 上电后 UI 与音频链路正常，无明显卡死
  - `cfg status` 中关键计数无异常飙升（如 DMA2D timeout/fallback）
  - 性能相关改动需执行一次 `cfg perf dump` 并记录关键段耗时

## Critical Runtime Constraints

- Real-time constraints:
  - 音频链路 `48kHz`，`FRAME_LEN=256` 时单帧预算约 `5.33ms`
  - UI 刷新目标默认 `20fps`（范围 `5..30`），与显示渲染耗时强相关
- Memory limits:
  - DMA 相关缓冲需位于 Non-Cacheable 区域（D2 SRAM）
  - SDRAM 存在 Cacheable/Non-Cacheable 分区，帧缓冲区域需与 LTDC 一致性匹配
  - 当前无固定 ROM/RAM map 阈值要求（以链接通过和实机稳定为准）
- Key attention modules (pending continuous validation):
  - `START/User/App/app_display.c`: 归一化与插值路径调整后，可能带来 FPS 变化，建议配合 `cfg perf dump` 复核
  - `START/User/App/app_main_task.c`: `uifps/algodecim/perf` 参数会改变调度与观测行为，建议变更后做回归对比
  - `START/Core/Src/main.c` + `START/Core/Src/mpu.c`: cache/MPU 调整可能影响性能与数据一致性，建议改动后做专项验证
  - `START/Core/Src/usart.c` + 中断文件：CLI 收发链路异常可能影响在线调参效率，建议通过 `cfg status` 与实机串口观察确认

## Notes For Future Sessions

- Current milestone:
  - 稳定运行期调参链路（CLI + perf）并持续压缩显示渲染开销
- Main pending risks:
  - 文档漂移风险：`README_CN.md` 仍有历史参数（如串口波特率）可能与现状不一致
  - 性能回退风险：显示路径小改动可能引发 FPS 明显波动
  - 流程风险：分支/评审/发布流程尚未在仓库中标准化
- Important follow-ups:
  - 当前无新增硬性必填项，后续按实际协作需要再补充
