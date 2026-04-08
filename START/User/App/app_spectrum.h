/**
 * @file   app_spectrum.h
 * @brief  频谱分析应用层接口
 * @details 管理 FFT 频谱帧的发布与获取，提供频带选择（活动频带/预览频带）、
 *          频率-bin 互转及面板坐标到频率 bin 的映射功能，供 UI 频谱绘制使用。
 */
#ifndef APP_SPECTRUM_H
#define APP_SPECTRUM_H

#include "app_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化频谱模块，清零内部帧缓冲区 */
void App_Spectrum_Init(void);

/**
 * @brief 从 FFT 结果发布一帧频谱数据
 * @param seq 帧序列号，用于跟踪时序
 */
void App_Spectrum_PublishFromFft(uint32_t seq);

/**
 * @brief 将当前频谱帧数据复制到用户缓冲区
 * @param frame 输出频谱帧结构体指针
 */
void App_Spectrum_CopyFrame(App_SpectrumFrame_t *frame);

/**
 * @brief  获取最新频谱帧（带有效性检查）
 * @param  frame 输出频谱帧结构体指针
 * @return 1=获取成功，0=无新帧
 */
uint8_t App_Spectrum_GetLatestFrame(App_SpectrumFrame_t *frame);

/**
 * @brief  获取默认频带配置
 * @return 默认 App_FreqBand_t 值
 */
App_FreqBand_t App_Spectrum_DefaultBand(void);

/**
 * @brief 设置当前活动频带（用于波束成形计算）
 * @param band 频带参数
 */
void App_Spectrum_SetActiveBand(App_FreqBand_t band);

/**
 * @brief  获取当前活动频带
 * @return 当前活动 App_FreqBand_t
 */
App_FreqBand_t App_Spectrum_GetActiveBand(void);

/**
 * @brief 设置预览频带（用于 UI 频谱面板高亮显示）
 * @param band 频带参数
 */
void App_Spectrum_SetPreviewBand(App_FreqBand_t band);

/**
 * @brief  获取当前预览频带
 * @return 当前预览 App_FreqBand_t
 */
App_FreqBand_t App_Spectrum_GetPreviewBand(void);

/**
 * @brief  将 FFT bin 索引转换为频率值（Hz）
 * @param  bin FFT bin 索引
 * @return 对应的频率值（Hz）
 */
float App_Spectrum_BinToHz(uint16_t bin);

/**
 * @brief  将频率值（Hz）转换为最近的 FFT bin 索引
 * @param  hz 频率值（Hz）
 * @return 对应的 FFT bin 索引
 */
uint16_t App_Spectrum_HzToBin(float hz);

/**
 * @brief  将频谱面板轴坐标（像素）转换为 FFT bin 索引
 * @param  axis_px     面板轴上的像素坐标
 * @param  axis_px0    轴起始像素偏移
 * @param  axis_length 轴总像素长度
 * @param  min_hz      轴对应的最低频率（Hz）
 * @param  scale_mode  缩放模式（0=线性，1=对数）
 * @param  invert      是否反转轴方向：1=反转
 * @return 对应的 FFT bin 索引
 */
uint16_t App_Spectrum_PanelAxisToBin(uint16_t axis_px,
                                     uint16_t axis_px0,
                                     uint16_t axis_length,
                                     float min_hz,
                                     uint8_t scale_mode,
                                     uint8_t invert);

/**
 * @brief  将频谱面板 X 坐标（像素）转换为 FFT bin 索引（简化版）
 * @param  x_px       面板 X 像素坐标
 * @param  plot_x0    绘图区起始 X 偏移
 * @param  plot_width 绘图区宽度（像素）
 * @return 对应的 FFT bin 索引
 */
uint16_t App_Spectrum_PanelXToBin(uint16_t x_px, uint16_t plot_x0, uint16_t plot_width);

#ifdef __cplusplus
}
#endif

#endif /* APP_SPECTRUM_H */
