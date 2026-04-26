/**
 * @file   app_spectrum.h
 * @brief  FFT 频谱分析/发布模块头文件
 * @details
 * 架构说明：
 *   频谱模块负责在音频任务（写入方）和 UI 任务（读取方）之间传递 FFT 幅度谱数据。
 *   内部使用「seqlock + 双缓冲」无锁发布机制：
 *     - 音频任务 PublishFromFft() 写入另一个缓冲，写完后带内存屏障切换索引
 *     - UI 任务 GetLatestFrame() 用 seqlock 读取，如果读取期间被写入感知则重试
 *
 * seqlock 原理：
 *   seqlock 是 Linux 内核常用的读/写无锁技术：
 *     写者：seq 先加一（居奇）→写入数据→seq 再加一（居偶）, 写入期间 seq 是奇数
 *     读者：读 seq(开始) →读数据 →读 seq(结束)，若两次 seq 相等且为偶数，则数据一致
 *     若读写冲突，读者重试（最多 3 次）
 *
 * 双缓冲原理：
 *   写者始终写入「非当前」缓冲（索引异或，0→1→0→1…），
 *   写完后切换索引，读者始终读当前索引。
 *   两者磁道工作在不同缓冲上，几乎无数据竞争。
 *
 * [注意] PanelAxisToBin() 充分利用了对数坐标系，适合音频频谱的时候
 *        展示（人耳对频率的感知是对数尺度的）。
 */
#ifndef APP_SPECTRUM_H                  /* 防止头文件被多次包含 */
#define APP_SPECTRUM_H

#include "app_types.h"                  /* App_SpectrumFrame_t, App_FreqBand_t 类型定义 */

#include <stdint.h>                     /* uint8_t, uint16_t, uint32_t */

#ifdef __cplusplus
extern "C" {                            /* 允许 C++ 工程包含此头文件 */
#endif

/** @brief 初始化频谱模块，清零双缓冲并设置默认频段 */
void App_Spectrum_Init(void);

/**
 * @brief  从 FFT 结果发布一帧频谱数据（seqlock 写者端）
 * @details 计算所有麦克风通道各 bin 的平均幅度，写入非当前缓冲，
 *          然后带内存屏障（__DMB）切换 s_publish_index。
 *          应在音频任务的 FFT 处理完成后立即调用。
 * @param  seq 当前音频帧序列号，写入 frame->seq，供 UI 检测是否有新帧到来
 */
void App_Spectrum_PublishFromFft(uint32_t seq);

/**
 * @brief  无保护地直接拷贝最新频谱帧（单读者场景使用）
 * @details 直接 memcpy，没有 seqlock 重试保护。只在确保只有一个读者
 *          且读嵌入性能要求更高时使用此功能。
 *          如果存在超过一个读者，应使用 GetLatestFrame() 代替。
 * @param  frame 接收缓冲指针（NULL 安全）
 */
void App_Spectrum_CopyFrame(App_SpectrumFrame_t *frame);

/**
 * @brief  安全读取最新频谱帧（seqlock 读者端）
 * @details 使用 seqlock 读取，若写入冲突则重试（最多 3 次）。
 *          适用于 UI 任务轮询场景：每帧尝试一次，失败则保留旧数据。
 * @param  frame 输出结构体指针
 * @return 1 = 成功读取，0 = 读写冲突导致 3 次重试后仍失败
 */
uint8_t App_Spectrum_GetLatestFrame(App_SpectrumFrame_t *frame);

/**
 * @brief  获取默认频段配置（基于 SRP 配置中的起止 bin）
 * @details 返回值由 SRP_FREQ_BIN_START 和 SRP_FREQ_BIN_END 决定，
 *          定义了波束成形算法的工作频带（例：1kHz~8kHz）。
 * @return 默认频段结构体
 */
App_FreqBand_t App_Spectrum_DefaultBand(void);

/**
 * @brief  设置当前活动频段（用于 SRP-PHAT 波束成形计算）
 * @details 切换频段会响应 AI_SRP_SetActiveFreqRange()，即时影响绞齐向事量的频段分增。
 *          [注意] 切换频段会使 SRP 功率图当帧失效，需等待下一帧才能看到新频段的效果。
 *          [注意] 内部用 32 位整数存储就 packed band，保证单次访问的原子性。
 * @param  band 新频段（超过最大 bin 的部分会被自动欲不超过限比）
 */
void App_Spectrum_SetActiveBand(App_FreqBand_t band);

/**
 * @brief  获取当前活动频段
 * @return 当前活动频段（其中 start_bin 和 end_bin 均已此不超过范围）
 */
App_FreqBand_t App_Spectrum_GetActiveBand(void);

/**
 * @brief  设置预览频段（不影响 SRP 算法，仅用于 UI 频谱面板高亮显示）
 * @details 预览频段是用户正在调节中的频带，它听答 UI 不会影响 SRP 算法。
 *          用户确认后会将预览频段提交为活动频段。
 * @param  band 预览频段
 */
void App_Spectrum_SetPreviewBand(App_FreqBand_t band);

/**
 * @brief  获取当前预览频段
 * @return 当前预览频段
 */
App_FreqBand_t App_Spectrum_GetPreviewBand(void);

/**
 * @brief  将 FFT bin 索引转换为对应频率（Hz）
 * @details 公式：f = bin × Δf，其中 Δf = 采样率 / FFT 点数 = 187.5 Hz/bin。
 *          超过最大 bin (奈奎斯特频率) 会被自动止不超过限比。
 * @param  bin FFT bin 索引（从 0 开始，0 = DC）
 * @return 对应频率（Hz）
 */
float App_Spectrum_BinToHz(uint16_t bin);

/**
 * @brief  将频率值（Hz）转换为最近的 FFT bin 索引
 * @details 公式：bin = round(hz / Δf)；由5入次近取。
 *          如果 hz <= 0 返回 0（DC bin），超过最高频返回最大 bin。
 * @param  hz 频率值（Hz）
 * @return 最接近的 FFT bin 索引
 */
uint16_t App_Spectrum_HzToBin(float hz);

/**
 * @brief  将频谱面板坐标轴（像素）转换为 FFT bin 索引
 * @details 支持线性其标尺度和对数尺度，以及坐标轴反转。
 *          对数尺度适合频谱显示（人耳对频率的感知是对数比例的，一倍可呈现更宽的频率范围）。
 * @param  axis_px     面板坐标轴上的像素坐标
 * @param  axis_px0    坐标轴起始像素偏移
 * @param  axis_length 坐标轴总像素长度
 * @param  min_hz      坐标轴对应的最低频率（Hz）
 * @param  scale_mode  缩放模式：0 = 线性分布，非0 = 对数分布
 * @param  invert      是否反转坐标轴方向：1 = 反转
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
 * @details 继承自 PanelAxisToBin，使用线性分布、不反转、最低频率 = DELTA_F。
 * @param  x_px       X 轴像素坐标
 * @param  plot_x0    绘图区域起始 X 偏移
 * @param  plot_width 绘图区域宽度（像素）
 * @return 对应的 FFT bin 索引
 */
uint16_t App_Spectrum_PanelXToBin(uint16_t x_px, uint16_t plot_x0, uint16_t plot_width);

#ifdef __cplusplus
}                                       /* extern "C" 结束 */
#endif

#endif /* APP_SPECTRUM_H */

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
