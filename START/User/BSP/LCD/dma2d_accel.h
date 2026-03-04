#ifndef DMA2D_ACCEL_H
#define DMA2D_ACCEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void DMA2D_Accel_Init(void);
void DMA2D_Accel_IRQHandler(void);

uint8_t DMA2D_Accel_EnqueueFill(uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t dst_line_offset,
                                uint32_t color);

uint8_t DMA2D_Accel_EnqueueCopy(uint32_t src_addr,
                                uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t src_line_offset,
                                uint16_t dst_line_offset);

uint8_t DMA2D_Accel_EnqueueBlitL8(uint32_t src_addr,
                                  uint32_t dst_addr,
                                  uint16_t width,
                                  uint16_t height,
                                  uint16_t src_line_offset,
                                  uint16_t dst_line_offset);

uint8_t DMA2D_Accel_EnqueueBlendA8(uint32_t src_addr,
                                   uint32_t dst_addr,
                                   uint16_t width,
                                   uint16_t height,
                                   uint16_t src_line_offset,
                                   uint16_t dst_line_offset,
                                   uint16_t color565);

uint8_t DMA2D_Accel_Flush(uint32_t timeout_loop);
void DMA2D_Accel_Reset(void);
void DMA2D_Accel_LoadClutFromRgb565(const uint16_t *lut, uint16_t count);

extern volatile uint32_t g_dma2d_queue_overflow_count;
extern volatile uint32_t g_dma2d_queue_error_count;
extern volatile uint32_t g_dma2d_queue_depth_peak;

#ifdef __cplusplus
}
#endif

#endif /* DMA2D_ACCEL_H */
