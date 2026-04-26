---
description: "Use when editing LVGL widgets, UI screens, display rendering, touch handling, LTDC/DMA2D pipeline, chroma-key overlay, or spectrum visualization."
applyTo: "START/User/App/app_ui*,START/User/App/app_display*,START/User/BSP/LCD/**,START/User/BSP/TOUCH/**"
---
# UI & Display Constraints

## Thread Safety (CRITICAL)

- LVGL v8 is **NOT thread-safe** — ALL `lv_*` calls MUST be in `UI_Task` only
- Never call `lv_obj_create`, `lv_label_set_text`, `lv_canvas_draw_*` etc. from Audio task, ISR, or Default task
- To update UI from another task: send data via FreeRTOS queue, consume in `UI_Task`

## Display Pipeline

```
LVGL render → framebuffer (SDRAM) → lv_disp_flush_cb → DCache clean → LTDC layer swap → LCD
```

- 800×480 LCD via LTDC + DMA2D, double-buffered
- Framebuffers are in SDRAM (`0xC0000000`)
- `lv_disp_flush_cb` must call `SCB_CleanDCache_by_Addr()` before LTDC swap

## Chroma-Key Overlay

- Chroma-key value: exactly `0xFF00FF` (RGB888) — magenta
- `app_display.c` erases overlay layer with DMA2D fill of this exact color
- LTDC is configured to treat this color as transparent
- Any 1-bit mismatch breaks transparency — never approximate this value

## Canvas Usage

- `lv_canvas` buffers must remain valid for the canvas lifetime — no stack allocation
- Place canvas buffers in SDRAM or AXI SRAM (large buffers)
- Use `lv_canvas_set_buffer()` with a static/global buffer, never a local array

## Touch Input

- GT9xxx (capacitive) or FT5206 via software I²C (`User/Hardware/soft_i2c`)
- Touch coordinates are raw panel coords — LVGL handles rotation/mapping
- Touch read runs inside `UI_Task` via `lv_indev` registered callback

## UI Task

- Priority: `osPriorityHigh` (4), Stack: 4096 words
- Main loop: `lv_timer_handler()` + touch polling + display update
- Frame budget: 30 FPS target = ~33 ms per frame
- Keep widget tree shallow — deep nesting hurts render performance

## Widget Best Practices

- Use `lv_label_set_text_static()` for string literals (avoids internal copy)
- For dynamic text: buffer must outlive the label, or use `lv_label_set_text_fmt()`
- Free screen objects properly on screen transitions: `lv_obj_del(old_screen)`
- Use styles (`lv_style_t`) for consistent theming — define once, apply many
