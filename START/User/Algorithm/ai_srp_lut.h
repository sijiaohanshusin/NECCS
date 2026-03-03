/**
 * @file    ai_srp_lut.h
 * @brief   SRP-PHAT 预计算查找表 (LUT) 声明
 * @details 包含麦克风对配置、粗搜角度和 TDOA 查找表
 *
 * 数据来源：
 * - 由 Python 脚本 tools/generate_srp_lut.py 自动生成
 * - 实际数据定义在 START/User/Algorithm/ai_srp_lut.c
 *
 * 坐标系约定：
 * - 原点：麦克风阵列中心
 * - +X 轴：向右
 * - +Y 轴：向上
 * - +Z 轴：向前 (垂直于阵列平面)
 *
 * 麦克风对位移约定 (重要)：
 * - dx = x_i - x_j (麦克风 i 相对于麦克风 j 的 X 方向位移)
 * - dy = y_i - y_j (麦克风 i 相对于麦克风 j 的 Y 方向位移)
 * - 此约定与 TDOA 计算公式一致
 *
 * TDOA 计算公式：
 * - tau = (dx * sin(theta) * cos(phi) + dy * sin(phi)) / c
 * - theta: 水平角 (方位角)
 * - phi: 垂直角 (俯仰角)
 * - c: 声速 (343 m/s)
 */

#ifndef AI_SRP_LUT_H
#define AI_SRP_LUT_H

#include "arm_math.h"
#include "ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 麦克风对配置表 (Microphone Pair Configuration)
 * ============================================================================ */

/**
 * @brief   麦克风对索引表
 * @details 存储 40 对麦克风的索引 (i, j)
 *
 * 选择原则：
 * - 按基线长度降序排列 (长基线优先)
 * - 长基线：方向分辨率高，但易产生空间模糊
 * - 短基线：消除空间模糊，但方向分辨率低
 * - 组合使用：兼顾分辨率和鲁棒性
 *
 * 数据格式：
 * - srp_pair_idx[p][0]: 麦克风 i 的索引 (0-15)
 * - srp_pair_idx[p][1]: 麦克风 j 的索引 (0-15)
 */
extern const uint8_t srp_pair_idx[SRP_PAIR_COUNT][2];

/**
 * @brief   麦克风对 X 方向位移表 (单位：米)
 * @details 存储 40 对麦克风的 X 方向位移 dx = x_i - x_j
 *
 * 用途：
 * - 计算 TDOA 时的 X 分量
 * - 配合 sin(theta) * cos(phi) 计算水平方向时延
 */
extern const float32_t srp_pair_dx[SRP_PAIR_COUNT];

/**
 * @brief   麦克风对 Y 方向位移表 (单位：米)
 * @details 存储 40 对麦克风的 Y 方向位移 dy = y_i - y_j
 *
 * 用途：
 * - 计算 TDOA 时的 Y 分量
 * - 配合 sin(phi) 计算垂直方向时延
 */
extern const float32_t srp_pair_dy[SRP_PAIR_COUNT];

/* ============================================================================
 * 粗搜角度表 (Coarse Scan Angles)
 * ============================================================================ */

/**
 * @brief   粗搜水平角度表 (单位：度)
 * @details 9 个角度点：[-60, -45, -30, -15, 0, 15, 30, 45, 60]
 *
 * 覆盖范围：±60° (120° 视场角)
 * 角度步长：15°
 * 用途：粗搜阶段快速定位声源大致方向
 */
extern const float32_t coarse_theta_deg[COARSE_GRID_SIZE];

/**
 * @brief   粗搜垂直角度表 (单位：度)
 * @details 9 个角度点：[-60, -45, -30, -15, 0, 15, 30, 45, 60]
 *
 * 覆盖范围：±60° (120° 视场角)
 * 角度步长：15°
 * 用途：粗搜阶段快速定位声源大致方向
 */
extern const float32_t coarse_phi_deg[COARSE_GRID_SIZE];

/* ============================================================================
 * TDOA 查找表 (TDOA Lookup Table)
 * ============================================================================ */

/**
 * @brief   粗搜 TDOA 查找表 (单位：秒)
 * @details 预计算的时延差表，加速 SRP 扫描
 *
 * 数据维度：
 * - 第一维：COARSE_TOTAL (81 个扫描点)
 * - 第二维：SRP_PAIR_COUNT (40 对麦克风)
 *
 * 数据内容：
 * - tdoa_coarse_lut[g][p]: 扫描点 g 对麦克风对 p 的 TDOA
 * - 计算公式：tau = (dx * sin(theta) * cos(phi) + dy * sin(phi)) / c
 *
 * 用途：
 * - 粗搜阶段直接查表，避免实时计算三角函数
 * - 精搜阶段实时计算 (因为角度不固定)
 *
 * 存储位置：Flash (约 13KB)
 * 访问速度：约 0.1ms (81 次查表)
 */
extern const float32_t tdoa_coarse_lut[COARSE_TOTAL][SRP_PAIR_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* AI_SRP_LUT_H */
