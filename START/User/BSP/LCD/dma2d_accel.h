/**
 * @file    dma2d_accel.h
 * @brief   DMA2D 异步命令队列加速接口
 * @details 为 LTDC 渲染路径提供基于中断驱动的 DMA2D 任务排队能力。
 *          支持纯色填充、同格式拷贝、L8 伪彩贴图和 A8 透明混合。
 *
 * 设计要点：
 * - 单生产者/多调用点通过短临界区保护环形队列。
 * - DMA2D 由中断回调串行执行队列命令，避免上层阻塞等待。
 * - 提供 Flush 接口，供帧提交前按需等待队列排空。
 */
#ifndef DMA2D_ACCEL_H
#define DMA2D_ACCEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 DMA2D 队列加速模块。
 * @note  仅在显示驱动初始化阶段调用一次。
 */
void DMA2D_Accel_Init(void);

/**
 * @brief DMA2D 中断处理入口。
 * @note  需在 `DMA2D_IRQHandler` 中转调。
 */
void DMA2D_Accel_IRQHandler(void);

/**
 * @brief 入队纯色矩形填充命令（R2M）。
 * @param dst_addr         目标起始地址。
 * @param width            区域宽度（像素）。
 * @param height           区域高度（像素）。
 * @param dst_line_offset  目标行间偏移（像素）。
 * @param color            填充值（按当前 LTDC 像素格式解释）。
 * @return 1=入队成功，0=入队失败（队列满或参数非法）。
 */
uint8_t DMA2D_Accel_EnqueueFill(uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t dst_line_offset,
                                uint32_t color);

/**
 * @brief 入队同像素格式内存拷贝命令（M2M）。
 */
uint8_t DMA2D_Accel_EnqueueCopy(uint32_t src_addr,
                                uint32_t dst_addr,
                                uint16_t width,
                                uint16_t height,
                                uint16_t src_line_offset,
                                uint16_t dst_line_offset);

/**
 * @brief 入队 L8 源图到目标帧缓冲的像素格式转换拷贝（M2M_PFC）。
 */
uint8_t DMA2D_Accel_EnqueueBlitL8(uint32_t src_addr,
                                  uint32_t dst_addr,
                                  uint16_t width,
                                  uint16_t height,
                                  uint16_t src_line_offset,
                                  uint16_t dst_line_offset);

/**
 * @brief 入队 A8 Alpha 蒙版与目标图层混合命令（M2M_BLEND）。
 * @param color565 前景颜色（RGB565），Alpha 来自 A8 源。
 */
uint8_t DMA2D_Accel_EnqueueBlendA8(uint32_t src_addr,
                                   uint32_t dst_addr,
                                   uint16_t width,
                                   uint16_t height,
                                   uint16_t src_line_offset,
                                   uint16_t dst_line_offset,
                                   uint16_t color565);

/**
 * @brief 等待 DMA2D 队列排空。
 * @param timeout_loop 轮询超时次数。
 * @return 0=成功排空，1=超时。
 */
uint8_t DMA2D_Accel_Flush(uint32_t timeout_loop);

/**
 * @brief 复位队列与 DMA2D 忙状态，不重置统计峰值。
 */
void DMA2D_Accel_Reset(void);

/**
 * @brief 从 RGB565 调色板加载 DMA2D 前景 CLUT（最多 256 项）。
 */
void DMA2D_Accel_LoadClutFromRgb565(const uint16_t *lut, uint16_t count);

/** @brief 队列溢出计数（入队失败，通常表示渲染负载过高）。 */
extern volatile uint32_t g_dma2d_queue_overflow_count;
/** @brief 命令执行异常计数（DMA2D 错误中断或非法命令）。 */
extern volatile uint32_t g_dma2d_queue_error_count;
/** @brief 运行期队列深度峰值。 */
extern volatile uint32_t g_dma2d_queue_depth_peak;

#ifdef __cplusplus
}
#endif

#endif /* DMA2D_ACCEL_H */
