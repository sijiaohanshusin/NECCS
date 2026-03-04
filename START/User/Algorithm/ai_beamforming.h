/**
 * @file    ai_beamforming.h
 * @brief   声学波束成形与声源定位算法接口
 * @details 实现 FFT 频域变换和 SRP-PHAT 声源定位算法
 *
 * 算法流程：
 * 1. AI_FFT_Process(): 时域信号 → 频域信号 (去直流 + 加窗 + RFFT)
 * 2. AI_SRP_PHAT_Process(): 频域信号 → 声源方位角 (GCC-PHAT + SRP 扫描)
 *
 * SRP-PHAT 原理：
 * - GCC-PHAT: 广义互相关相位变换，计算麦克风对之间的时延差
 * - SRP: 可控响应功率，在空间网格上搜索能量最大的方向
 * - 粗搜 + 精搜：先 9×9 粗网格快速定位，再 3×4×4 精网格细化
 */

#ifndef AI_BEAMFORMING_H
#define AI_BEAMFORMING_H

#include "arm_math.h"
#include "ai_config.h"
#include "app_main_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 核心算法函数 (Core Algorithm Functions)
 * ============================================================================ */

/**
 * @brief   FFT 频域变换处理
 * @details 对 16 路麦克风信号进行频域变换，为 SRP-PHAT 准备数据
 *
 * 处理流程：
 * 1. 去直流：计算均值并减去 (arm_mean_f32 + arm_offset_f32)
 * 2. 加窗：乘以汉宁窗，减少频谱泄漏 (arm_mult_f32)
 * 3. RFFT：实数快速傅里叶变换 (arm_rfft_fast_f32)
 *
 * 输入数据：Mic_Process_Buffer (16ch × 256 点 float32, DTCM)
 * 输出数据：Mic_Freq_Buffer (16ch × 256 点复数, DTCM)
 *
 * RFFT 输出格式 (N=256)：
 * - [0]: DC 分量实部
 * - [1]: 奈奎斯特频率实部
 * - [2k, 2k+1]: bin k 的实部和虚部 (k=1..127)
 *
 * @note    耗时：约 0.8ms @ 480MHz (16ch × 256 点)
 * @note    必须在 Audio_Preprocess_Task 完成后调用
 */
void AI_FFT_Process(void);

/**
 * @brief   SRP-PHAT 算法初始化
 * @details 初始化统计计数器和历史状态
 *
 * 初始化内容：
 * - 清零无效结果计数器
 * - 清零低对比度计数器
 * - 重置上次有效结果缓存
 * - 重置低置信度连续帧计数
 *
 * @note    在系统启动时调用一次即可
 */
void AI_SRP_PHAT_Init(void);

/**
 * @brief   SRP-PHAT 声源定位处理
 * @details 基于频域数据计算声源方位角和能量
 *
 * 算法流程：
 * 1. GCC-PHAT 计算：对 40 对麦克风计算广义互相关
 *    - 互相关：X_i(f) * conj(X_j(f))
 *    - PHAT 白化：除以幅度，保留相位信息
 * 2. 粗搜：在 9×9 网格 (±60°, 步长 15°) 上计算 SRP 功率
 * 3. Top-K 选择：选择前 3 个峰值 (带 NMS 抑制相邻峰值)
 * 4. 精搜：在每个峰值周围 4×4 网格 (±10°, 步长 5°) 细化
 * 5. 质量评估：计算对比度和质量指标
 * 6. 结果输出：根据置信度策略输出最终结果
 *
 * 输入数据：Mic_Freq_Buffer (16ch × 256 点复数, DTCM)
 * 输出数据：result (声源方位角和能量)
 *
 * 质量指标：
 * - 对比度 (contrast): (最大值 - 次大值) / 最大值
 * - 质量 (quality): (最大值 - 远处次大值) / 最大值
 * - 能量 (energy): 归一化功率 [0, 1]
 *
 * @param   result  输出声源位置结构体指针
 *                  - x_angle: 水平角度 (度)
 *                  - y_angle: 垂直角度 (度)
 *                  - energy: 归一化能量 [0, 1]
 *
 * @note    耗时：约 4ms @ 480MHz (81 粗搜 + 48 精搜)
 * @note    必须在 AI_FFT_Process() 完成后调用
 */
void AI_SRP_PHAT_Process(Sound_Pos_t *result);

typedef struct
{
    float32_t power[SRP_GRID_TOTAL];
    float32_t theta_deg[SRP_GRID_TOTAL];
    float32_t phi_deg[SRP_GRID_TOTAL];
    uint32_t peak_idx;
    float32_t peak_value;
} SRP_VisFrame_t;

void AI_SRP_CopyVisualizationFrame(SRP_VisFrame_t *frame);

/* ============================================================================
 * 诊断统计变量 (Diagnostic Statistics)
 * ============================================================================ */

/** @brief 无效结果计数器 (NaN 或索引越界) */
extern volatile uint32_t g_srp_invalid_count;

/** @brief 低对比度结果计数器 (质量低于门限) */
extern volatile uint32_t g_srp_low_contrast_count;

/** @brief 上次计算的对比度 (用于调试) */
extern volatile float32_t g_srp_last_contrast;

/** @brief 上次计算的质量指标 (用于调试) */
extern volatile float32_t g_srp_last_quality;

#ifdef __cplusplus
}
#endif

#endif /* AI_BEAMFORMING_H */
