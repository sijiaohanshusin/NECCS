/**
 * @file    ai_config.h
 * @brief   声学相机算法全局配置参数
 * @details 包含音频采集、FFT、SRP-PHAT 波束成形的所有可调参数
 *
 * 配置分类：
 * - 音频输入输出参数
 * - SRP-PHAT 核心参数
 * - 粗搜/精搜网格参数
 * - 数值稳定性参数
 * - 低置信度处理策略
 * - 质量门限与鲁棒性参数
 * - 输出角度重映射
 */

#ifndef __AI_CONFIG_H
#define __AI_CONFIG_H

#include <stdint.h>

/* ============================================================================
 * 音频输入输出参数 (Audio I/O Parameters)
 * ============================================================================ */

/** @brief 麦克风通道数 (16 路 PDM 麦克风) */
#define MIC_CHANNELS        16u

/** @brief 单帧长度 (每通道采样点数，用于 FFT) */
#define FRAME_LEN           256u

/** @brief 采样率 (Hz)，决定频率分辨率和时间分辨率 */
#define SAMPLING_RATE       48000u

/** @brief DMA 缓冲区大小 (字节)
 *  计算：通道数 × 帧长 × 2 (PING/PONG 双缓冲)
 *  实际值：16 × 256 × 2 = 8192 字节
 */
#define DMA_BUFFER_SIZE     (MIC_CHANNELS * FRAME_LEN * 2u)

/* ============================================================================
 * SRP-PHAT 核心参数 (SRP-PHAT Core Parameters)
 * ============================================================================ */

/** @brief 声速 (m/s)，用于计算时延差 (TDOA)
 *  标准条件：20°C, 1 个大气压
 */
#define SPEED_OF_SOUND      343.0f

/** @brief 频率分辨率 (Hz)
 *  计算：采样率 / 帧长 = 48000 / 256 = 187.5 Hz
 *  决定 FFT bin 的频率间隔
 */
#define DELTA_F             ((float)SAMPLING_RATE / (float)FRAME_LEN)

/** @brief SRP-PHAT 使用的起始频率 bin (bin 3 = 562.5 Hz)
 *  跳过低频 bin (0-2) 以避免直流和低频噪声
 */
#define SRP_FREQ_BIN_START  3u

/** @brief SRP-PHAT 使用的结束频率 bin (bin 42 = 7875 Hz)
 *  限制在人声频段，避免高频噪声
 */
#define SRP_FREQ_BIN_END    42u

/** @brief SRP-PHAT 使用的频率 bin 总数
 *  计算：42 - 3 + 1 = 40 个 bin
 *  覆盖频率范围：562.5 Hz - 7875 Hz
 */
#define SRP_FREQ_BINS       (SRP_FREQ_BIN_END - SRP_FREQ_BIN_START + 1u)

/** @brief 麦克风对数量 (用于 GCC-PHAT 计算)
 *  从 16 个麦克风中选择 40 对，按基线长度降序排列
 *  长基线对方向分辨率高，短基线对消除模糊
 */
#define SRP_PAIR_COUNT      40u

/* ============================================================================
 * 粗搜/精搜网格参数 (Coarse/Fine Scan Grid)
 * ============================================================================ */

/** @brief 粗搜网格尺寸 (每个维度的点数)
 *  9×9 网格，覆盖 ±60° 范围，步长 15°
 */
#define COARSE_GRID_SIZE    9u

/** @brief 粗搜总点数
 *  计算：9 × 9 = 81 个扫描点
 */
#define COARSE_TOTAL        (COARSE_GRID_SIZE * COARSE_GRID_SIZE)

/** @brief 精搜选择的 Top-K 个粗搜峰值
 *  从粗搜结果中选择前 3 个峰值进行精搜
 */
#define FINE_TOP_K          3u

/** @brief 精搜网格尺寸 (每个 Top-K 峰值周围的点数)
 *  4×4 网格，覆盖 ±10° 范围，步长 5°
 */
#define FINE_GRID_SIZE      4u

/** @brief 每个 Top-K 峰值的精搜点数
 *  计算：4 × 4 = 16 个扫描点
 */
#define FINE_TOTAL_PER_TOP  (FINE_GRID_SIZE * FINE_GRID_SIZE)

/** @brief 精搜总点数
 *  计算：3 × 16 = 48 个扫描点
 */
#define FINE_TOTAL          (FINE_TOP_K * FINE_TOTAL_PER_TOP)

/** @brief SRP 总扫描点数
 *  计算：81 (粗搜) + 48 (精搜) = 129 个扫描点
 */
#define SRP_GRID_TOTAL      (COARSE_TOTAL + FINE_TOTAL)

/* ============================================================================
 * 数值稳定性参数 (Numerical Stability)
 * ============================================================================ */

/** @brief PHAT 白化时的除零保护常数
 *  防止幅度为 0 时除法溢出
 */
#define PHAT_EPSILON        1.0e-10f

/* ============================================================================
 * 低置信度处理策略 (Low-Confidence Policy)
 * ============================================================================ */

/** @brief 策略 0：报告新结果 (即使置信度低) */
#define SRP_LOWCONF_REPORT_NEW         0u

/** @brief 策略 1：保持上次有效结果 (置信度低时不更新) */
#define SRP_LOWCONF_HOLD_LAST          1u

/** @brief 策略 2：混合策略 (短期保持，长期更新) */
#define SRP_LOWCONF_MIXED              2u

/** @brief 当前使用的低置信度策略
 *  默认：SRP_LOWCONF_REPORT_NEW (实时响应)
 */
#define SRP_LOWCONF_POLICY             SRP_LOWCONF_REPORT_NEW

/** @brief 混合策略的保持帧数
 *  连续低置信度超过此帧数后，开始接受新结果
 */
#define SRP_LOWCONF_MIXED_HOLD_FRAMES  6u

/* ============================================================================
 * 质量门限与鲁棒性参数 (Quality and Robustness Gates)
 * ============================================================================ */

/** @brief 对比度最小比率 (质量门限)
 *  计算：(最大值 - 次大值) / 最大值 > 0.03
 *  低于此值认为结果模糊，置信度低
 */
#define SRP_CONTRAST_MIN_RATIO               0.03f

/** @brief 计算次大值时排除的邻域角度 (度)
 *  排除最大值周围 ±5° 的点，避免旁瓣干扰
 */
#define SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG    5.0f

/** @brief Top-K 选择时的 NMS (非极大值抑制) 半径
 *  防止选择相邻的峰值，确保 Top-K 峰值分散
 */
#define SRP_TOPK_NMS_RADIUS                  1u

/* ============================================================================
 * 有效结果锁存门限 (Valid-Result Latch Gate)
 * ============================================================================ */

/** @brief 有效结果的最小能量门限
 *  归一化能量 > 0.05 才认为是有效声源
 */
#define SRP_VALID_MIN_ENERGY      0.05f

/** @brief 有效结果的最小质量门限
 *  质量 > 0.01 才认为是可靠结果
 */
#define SRP_VALID_MIN_QUALITY     0.01f

/* ============================================================================
 * 可选的低置信度能量软上限 (Optional Low-Confidence Energy Soft-Cap)
 * ============================================================================ */

/** @brief 是否启用能量软上限 (0=禁用, 1=启用)
 *  启用后，低置信度结果的能量会被限制
 */
#define SRP_ENABLE_ENERGY_SOFTCAP 0u

/** @brief 模糊结果的能量上限
 *  低置信度结果的能量不超过 0.30
 */
#define SRP_AMBIGUOUS_ENERGY_MAX  0.30f

/* ============================================================================
 * 输出角度重映射 (Output Angle Remap)
 * ============================================================================ */

/** @brief 是否交换 X/Y 角度 (0=不交换, 1=交换)
 *  用于适配不同的安装方向
 */
#define SRP_OUTPUT_SWAP_XY  0u

/** @brief 是否反转 X 角度 (0=不反转, 1=反转)
 *  用于镜像翻转
 */
#define SRP_OUTPUT_INVERT_X 0u

/** @brief 是否反转 Y 角度 (0=不反转, 1=反转)
 *  用于镜像翻转
 */
#define SRP_OUTPUT_INVERT_Y 0u

#endif /* __AI_CONFIG_H */
