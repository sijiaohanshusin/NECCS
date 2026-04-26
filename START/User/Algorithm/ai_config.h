/**
 * @file    ai_config.h
 * @brief   算法模块编译期派生常量 — 基于 app_user_config.h 计算的衍生参数
 * @details
 * 本文件设计原则（演化历史）：
 *   初期所有可调参数（麦克风数量、FFT 点数、SRP 网格大小等）都在本文件中定义。
 *   随着可调参数越来越多，为避免用户修改算法内部文件，
 *   将所有用户可调参数统一迁移到 `app_user_config.h`。
 *   本文件现在仅保留从用户参数派生的算法内部常量（不可直接修改，修改前提是修改源头参数）。
 *
 * 包含 `app_user_config.h` 的含义：
 *   所有算法 C/H 文件通过 `#include "ai_config.h"` 间接获得
 *   MIC_CHANNELS、FRAME_LEN、SAMPLING_RATE 等全部用户参数。
 *   因此算法文件无需直接包含 `app_user_config.h`。
 *
 * 重要数值举例（默认配置下）：
 *   MIC_CHANNELS = 16, FRAME_LEN = 256, SAMPLING_RATE = 48000Hz
 *   DMA_BUFFER_SIZE = 16 × 256 × 2 = 8192 个 int16_t 样本 = 16384 字节
 *   DELTA_F = 48000 / 256 = 187.5 Hz/bin
 *
 *   SRP_FREQ_BIN_START = 6 → 6 × 187.5 = 1125 Hz
 *   SRP_FREQ_BIN_END   = 45 → 45 × 187.5 = 8437.5 Hz
 *   SRP_FREQ_BINS = 45 - 6 + 1 = 40 个积分频率 bin
 *
 *   COARSE_GRID_SIZE = 9 → COARSE_TOTAL = 9 × 9 = 81 个粗搜索点
 *   FINE_GRID_SIZE   = 4 → FINE_TOTAL_PER_TOP = 4 × 4 = 16 个精搜索点/峰值
 *   FINE_TOP_K       = 3 → FINE_TOTAL = 3 × 16 = 48 个精搜索总点数
 *   SRP_GRID_TOTAL   = 81 + 48 = 129 个全局搜索点
 *
 * [注意] 修改 app_user_config.h 中的参数后，本文件的衍生值会自动随之改变。
 *        但 LUT 数据（ai_srp_lut.c）不会自动更新，需要重新运行 generate_srp_lut.py 生成。
 */
#ifndef AI_CONFIG_H                     /* 头文件防重复包含保护（开始）*/
#define AI_CONFIG_H                     /* 定义本文件标识宏 */

#include <stdint.h>                     /* uint32_t 等标准整数类型（派生常量运算中可能用到）*/

#include "app_user_config.h"           /* 引入全部用户可调参数（MIC_CHANNELS、FRAME_LEN 等）*/

/**
 * @brief DMA 双缓冲总大小（int16_t 元素个数）
 * @details PING/PONG 双缓冲各包含 MIC_CHANNELS × FRAME_LEN 个 q15 采样值，
 *          总共 MIC_CHANNELS × FRAME_LEN × 2 个元素。
 *          默认值：16 × 256 × 2 = 8192 个 int16_t = 16384 字节（约 16 KB）。
 *          [注意] DMA 缓冲区必须放在 D2 SRAM（非可缓存区），
 *                 否则 CPU 可能读到缓存脏数据（DMA 写入不经过 CPU 缓存）。
 */
#define DMA_BUFFER_SIZE     (MIC_CHANNELS * FRAME_LEN * 2u)

/**
 * @brief FFT 频率分辨率（单位：Hz/bin）
 * @details 由采样率和 FFT 点数决定：Δf = f_s / N
 *          默认值：48000 / 256 = 187.5 Hz/bin
 *          含义：每个 FFT 频率 bin 代表 187.5 Hz 的频带宽度。
 *          要将 bin 号 k 转换为中心频率（Hz）：f_k = k × DELTA_F
 */
#define DELTA_F             ((float)SAMPLING_RATE / (float)FRAME_LEN)

/**
 * @brief SRP-PHAT 算法参与积分的频率 bin 总数
 * @details 仅对 [SRP_FREQ_BIN_START, SRP_FREQ_BIN_END] 区间内的频率 bin 进行 GCC-PHAT 积分。
 *          选择此区间的原因：
 *          - 排除直流和极低频（bin 0-5，< 1125 Hz）：麦克风低频响应差，噪声大
 *          - 排除高频（bin > 45，> 8437 Hz）：语音信号主要能量在此区间内，避免混叠
 *          默认值：45 - 6 + 1 = 40 bins
 *          [注意] 分配给 GCC_PHAT_Buffer 的大小依赖此值：SRP_PAIR_COUNT × SRP_FREQ_BINS × 2 float
 */
#define SRP_FREQ_BINS       (SRP_FREQ_BIN_END - SRP_FREQ_BIN_START + 1u)

/**
 * @brief 粗搜索网格总点数
 * @details 粗搜索在 COARSE_GRID_SIZE × COARSE_GRID_SIZE 的二维网格上进行扫描，
 *          覆盖 ±60° × ±60° 的空间范围（默认步长 15°）。
 *          默认值：9 × 9 = 81 个搜索点
 *          在 SRP_Power 数组中，索引 [0, COARSE_TOTAL) 存储粗搜索功率。
 */
#define COARSE_TOTAL        (COARSE_GRID_SIZE * COARSE_GRID_SIZE)

/**
 * @brief 每个粗搜索峰值对应的精搜索点数
 * @details 精搜索在峰值周围 FINE_GRID_SIZE × FINE_GRID_SIZE 的小网格内细化，
 *          覆盖 ±10°（相对于粗搜峰值位置）的区域（默认步长 5°）。
 *          默认值：4 × 4 = 16 个精搜索点/峰值
 */
#define FINE_TOTAL_PER_TOP  (FINE_GRID_SIZE * FINE_GRID_SIZE)

/**
 * @brief 精搜索总点数（所有 Top-K 峰值的精搜索之和）
 * @details 对最佳 FINE_TOP_K（默认 3）个粗搜索峰值，
 *          各执行 FINE_TOTAL_PER_TOP 个精搜索点的扫描。
 *          默认值：3 × 16 = 48 个精搜索点
 *          在 SRP_Power 数组中，索引 [COARSE_TOTAL, SRP_GRID_TOTAL) 存储精搜索功率。
 */
#define FINE_TOTAL          (FINE_TOP_K * FINE_TOTAL_PER_TOP)

/**
 * @brief SRP 搜索网格全局总点数（粗搜 + 精搜之和）
 * @details SRP_Power 数组的总大小，每次 SRP-PHAT_Process 调用都会填充全部 SRP_GRID_TOTAL 个值。
 *          默认值：81 + 48 = 129 个点
 *          [注意] LUT 文件 ai_srp_lut.c 中的 tau_table 维度为
 *                 [SRP_PAIR_COUNT][SRP_GRID_TOTAL]，修改网格大小需重新生成 LUT。
 */
#define SRP_GRID_TOTAL      (COARSE_TOTAL + FINE_TOTAL)

#endif /* AI_CONFIG_H */                /* 头文件防重复包含保护（结束）*/
