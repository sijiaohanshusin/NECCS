# NECCS Code Review Checklist

## Naming Conventions

- [ ] Public functions: `Module_FunctionName()` (e.g., `AI_FFT_Process()`)
- [ ] Local variables: `lowercase_snake_case`
- [ ] Constants/macros: `UPPER_SNAKE_CASE`
- [ ] Typedef structs: `ModuleName_t` (e.g., `Sound_Pos_t`)
- [ ] File names match module: `ai_beamforming.c` → `AI_Beamforming_*` functions

## Header Guards

- [ ] Format: `#ifndef __FILENAME_H` / `#define __FILENAME_H` (double underscore prefix)
- [ ] Guard name matches filename exactly (uppercase, underscores for separators)
- [ ] Closing `#endif` has comment: `#endif /* __FILENAME_H */`

## ARM Compiler 5 Compatibility

- [ ] No VLA (variable-length arrays)
- [ ] No `_Static_assert`, `_Generic`, `_Alignas`
- [ ] No `typeof()`, `__auto_type`, statement expressions
- [ ] No `__attribute__((cleanup(...)))` or `__attribute__((constructor))`
- [ ] Bit-fields explicitly marked `signed` or `unsigned`
- [ ] No flexible array members (`arr[]`) — use `arr[1]` instead

## Real-Time Safety

- [ ] No `malloc` / `free` / `calloc` / `pvPortMalloc` in Audio task or ISR
- [ ] No `printf` / `sprintf` / logging in time-critical paths
- [ ] No blocking calls (`vTaskDelay`, `xQueueReceive` without timeout) in ISR
- [ ] ISR callbacks use `FromISR` variants: `xQueueSendFromISR`, `xSemaphoreGiveFromISR`
- [ ] No LVGL calls outside `UI_Task`

## Memory Placement

- [ ] DMA buffers in D2 SRAM: `__attribute__((section(".RAM_D2")))`
- [ ] DMA buffers 32-byte aligned: `__attribute__((aligned(32)))`
- [ ] FFT scratch in DTCM: `__attribute__((section(".dtcm")))`
- [ ] No stack-allocated buffers passed to DMA
- [ ] Cache management calls present for cacheable-region DMA buffers

## Error Handling

- [ ] New functions return `Err_t` from `common/error_code.h`
- [ ] Error returns are checked at every callsite (no silent drops)
- [ ] HAL return values (`HAL_OK`/`HAL_ERROR`) are checked
- [ ] Input parameters validated at public API boundary

## Documentation

- [ ] All new public functions have `/** @brief ... */` Doxygen comment
- [ ] Non-obvious decisions have inline comments explaining "why"
- [ ] Magic numbers replaced with named constants or `#define`

## Include Order

- [ ] HAL headers → CMSIS → FreeRTOS → BSP → App → Algorithm
- [ ] No circular includes
- [ ] All `#include` paths resolve correctly

## Files You Must NOT Modify

- [ ] No changes to `Drivers/CMSIS/**` or `Drivers/STM32H7xx_HAL_Driver/**`
- [ ] No changes to `Middlewares/Third_Party/FreeRTOS/**` or `Middlewares/LVGL/**`
- [ ] No changes to `User/Algorithm/ai_srp_lut.c` (auto-generated)

## Commit Message

- [ ] Format: `<type>(<scope>): <subject>`
- [ ] Type is one of: feat, fix, refactor, docs, style, perf, test, chore
- [ ] Scope matches: algorithm, app, bsp, hardware, common, docs, tools
