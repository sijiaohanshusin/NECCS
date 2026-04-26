/**
 * @file    app_sound_level.h
 * @brief   声级计模块头文件 — A/C/Z 加权声级与 Leq 积分接口
 * @details
 * 功能说明：
 *   声级计（SLM = Sound Level Meter）模拟专业仪器对声压级的测量，
 *   将 FFT 幅度谱转换为感知加权分贝值（dB SPL）。
 *
 * 支持三种频率加权（参考 IEC 61672 标准）：
 *   A 加权（dB(A)）：模拟人耳在中小响度下的频率感知，工业/环境噪声标准
 *   C 加权（dB(C)）：对低频衰减更少，适合测量高声压级（爆炸/机器噪声）
 *   Z 加权（dB(Z)）：平坦响应（无加权），等同于直接测量声压级
 *
 * Leq（等效连续声级）计算：
 *   Leq = 10 × log10( (1/N) × Σ P_linear(i) ) + 参考偏移
 *   其中 P_linear(i) 是第 i 帧的加权线性功率总和。
 *   [注意] 使用 double 精度累加，防止长时间（>1 分钟）积分的精度损失。
 *
 * 调用顺序：
 *   1. App_SLM_Init()       — 系统启动时调用一次
 *   2. App_SLM_Feed(...)    — 每次新 FFT 帧可用时调用（由音频任务触发）
 *   3. App_SLM_GetReading() — UI 轮询时读取当前读数（可从任意任务调用）
 *   4. App_SLM_ResetLeq()   — 开始新一轮测量时手动调用
 *
 * [改进] 当前实现基于频域加权（逐 bin 乘增益），精度受 FFT 频率分辨率限制。
 *        时域 IIR A 加权滤波器（双二阶级联）可提供更高精度，但占用更多 CPU 周期。
 *        对于当前 256 点 FFT（187.5 Hz/bin），低频段精度较差（<500 Hz 区域）。
 */
#ifndef __APP_SOUND_LEVEL_H             /* 防止头文件被多次包含 */
#define __APP_SOUND_LEVEL_H

#include <stdint.h>                     /* uint32_t（Leq 帧计数）*/

#ifdef __cplusplus
extern "C" {                            /* 允许 C++ 工程混用此头文件 */
#endif

/**
 * @brief 频率加权类型枚举
 * @details 值固定为 0/1/2，便于用整数索引或 switch-case。
 */
typedef enum {
    APP_SLM_WEIGHT_A = 0u,   /**< A 加权（IEC 61672）：最常用，模拟人耳感知 */
    APP_SLM_WEIGHT_C = 1u,   /**< C 加权（IEC 61672）：低频衰减少，高声级场景 */
    APP_SLM_WEIGHT_Z = 2u    /**< Z 加权（无修正）：平坦频率响应，即 0 dB 修正 */
} App_SLM_Weight_t;

/**
 * @brief 声级计读数结构体
 * @details GetReading() 将所有读数打包到此结构体，避免多次调用导致数据不一致。
 */
typedef struct {
    float db_inst;           /**< 瞬时声级（dB）：最近一帧 Feed 的结果，帧率=音频任务频率 */
    float db_leq;            /**< 等效连续声级 Leq（dB）：自上次 ResetLeq() 以来的积分均值 */
    float db_max;            /**< 测量周期最大声级（dB）：峰值保持，Reset 前不自动清零 */
    float db_min;            /**< 测量周期最小声级（dB）：谷值保持，Reset 前不自动清零 */
    uint32_t leq_frames;     /**< Leq 积分帧数（可换算为积分时长）：leq_frames / 帧率 = 秒数 */
} App_SLM_Reading_t;

/**
 * @brief 初始化声级计（清零所有状态）
 * @details 设置默认 A 加权，将瞬时/Leq/峰值清零，确保 GetReading 返回有意义的初始值。
 *          应在系统启动时或模式切换到声级计模式时调用。
 */
void App_SLM_Init(void);

/**
 * @brief 馈送一帧 FFT 幅度数据，更新声级读数
 * @details 该函数会对每个 bin 计算加权功率，累加后转换为 dB SPL。
 *          同时更新 Leq 积分（线性功率域，不是 dB 域）。
 *          该函数每帧调用一次（约 48000/256 ≈ 187.5 次/秒）。
 *          [注意] 内部有 powf 调用（较慢），若 CPU 占用率过高，可预计算 A 权重 LUT。
 * @param magnitude FFT 幅度数组（从 bin 0 开始，bin 0 为 DC 分量，会被跳过）
 * @param bin_count 数组中 bin 的数量（有效索引 0..bin_count-1）
 * @param delta_f   频率分辨率（Hz/bin）= 采样率 / FFT 点数（本系统 = 187.5 Hz/bin）
 */
void App_SLM_Feed(const float *magnitude, uint16_t bin_count, float delta_f);

/**
 * @brief 获取当前所有声级读数（线程安全读取）
 * @details 将内部所有状态复制到调用方提供的结构体，避免外部直接访问静态变量。
 *          可从任意任务调用，读操作本身无竞态（float 32 位对齐赋值是原子的）。
 *          [注意] db_leq 的计算包含 log10 运算，调用频率不宜超过 UI 刷新率（~30Hz）。
 * @param reading 输出结构体指针（不得为 NULL）
 */
void App_SLM_GetReading(App_SLM_Reading_t *reading);

/**
 * @brief 设置频率加权类型（立即生效，影响下次 Feed 调用）
 * @details 切换加权类型不会重置已有的 Leq 积分。
 *          若需要在单一加权下进行完整测量，应先调用 ResetLeq() 再切换加权类型。
 * @param weight 加权类型（A/C/Z）
 */
void App_SLM_SetWeight(App_SLM_Weight_t weight);

/**
 * @brief 查询当前频率加权类型
 * @return 当前正在使用的加权类型
 */
App_SLM_Weight_t App_SLM_GetWeight(void);

/**
 * @brief 复位 Leq 积分（重新开始等效声级计时）
 * @details 清零内部的线性功率累加器和帧计数。
 *          典型用法：用户按下"开始测量"按钮时调用。
 */
void App_SLM_ResetLeq(void);

/**
 * @brief 复位峰值和谷值记录
 * @details 将 db_max 和 db_min 重置为初始状态，等待新的峰/谷值出现。
 *          典型用法：用户按下"清除峰值"按钮时调用。
 */
void App_SLM_ResetPeak(void);

#ifdef __cplusplus
}                                       /* extern "C" 结束 */
#endif

#endif /* __APP_SOUND_LEVEL_H */
