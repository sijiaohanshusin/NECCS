/**
 * @file    app_anomaly.h
 * @brief   异常声音检测与历史日志头文件
 * @details
 * 检测原理：
 *   遍历所有 FFT bin，将当前帧幅度与噪声底（noise floor）做比值：
 *     deviation = magnitude[i] / floor[i]
 *   如果任意 bin 的 deviation 超过阈值（默认 3.0×），则认为发生了异常声音事件。
 *   该方法本质上是"高于噪声底 N 倍"的简单能量检测，不涉及频率模式识别。
 *
 * 冷却机制：
 *   检测到异常后启动冷却计数（默认 10 帧），冷却期间跳过检测，
 *   避免同一个异常事件被重复记录多次。
 *
 * 环形日志：
 *   最新的 64 条异常记录存储在环形缓冲区中。
 *   GetEntry(0) = 最新，GetEntry(1) = 次新，…
 *   超过 64 条后最旧的记录被覆盖。
 *
 * [改进] 当前阈值是固定倍数，没有考虑不同频段噪声底方差的差异，
 *        可改为按频段自适应阈值（如 floor[i] + k×σ[i]）以减少误报。
 * [注意] 此模块无线程锁，仅由 Audio_Pipeline_Task 调用，无跨任务竞争。
 */
#ifndef __APP_ANOMALY_H                 /* 防止头文件被多次包含 */
#define __APP_ANOMALY_H

#include <stdint.h>                     /* uint8_t, uint16_t, uint32_t, float */

#ifdef __cplusplus
extern "C" {                            /* 允许 C++ 工程包含此头文件 */
#endif

/** @brief 异常日志容量（环形缓冲最多保留的历史记录数）*/
#define APP_ANOMALY_LOG_SIZE  64u

/**
 * @brief 单条异常事件记录
 * @details 每次检测到异常时，将当前时刻和声源信息打包为此结构体存入日志。
 *          结构体大小：4+4+4+4+4+2+2 = 24 字节，已 4 字节对齐。
 */
typedef struct {
    uint32_t tick;            /**< 事件发生时刻（FreeRTOS tick 计数，精度 1ms）*/
    float    x_angle;         /**< 声源水平角（度），来自当前帧 SRP 的 DOA 结果 */
    float    y_angle;         /**< 声源垂直角（度），来自当前帧 SRP 的 DOA 结果 */
    float    energy;          /**< 触发异常瞬间的声源能量值（线性） */
    float    deviation;       /**< 偏离倍数（触发 bin 的 magnitude/floor），>= 阈值 */
    uint16_t peak_bin;        /**< 偏离最大的 FFT bin 索引（可转换为触发频率）*/
    uint16_t reserved;        /**< 保留字段（用于 4 字节对齐，填 0）*/
} App_AnomalyEntry_t;

/** @brief 初始化异常检测模块（清空日志、复位状态、禁用检测）*/
void App_Anomaly_Init(void);

/**
 * @brief  馈送一帧频谱数据，检测异常声音事件
 * @details 遍历每个 bin，计算当前幅度相对噪声底的倍数，超过阈值则记录日志。
 * @param  magnitude  当前帧 FFT 幅度数组（长度 = bin_count）
 * @param  floor      噪声底数组（由 app_noise_floor 提供，长度 = bin_count）
 * @param  bin_count  数组长度（有效 bin 数）
 * @param  x_angle    当前帧 DOA 水平角（度），用于记录异常发生时的声源方向
 * @param  y_angle    当前帧 DOA 垂直角（度）
 * @param  energy     当前帧总能量，用于记录异常强度
 * @return 1 = 本帧检测到异常并记录到日志；0 = 无异常或模块未使能
 */
uint8_t App_Anomaly_Feed(const float *magnitude, const float *floor,
                         uint16_t bin_count,
                         float x_angle, float y_angle, float energy);

/**
 * @brief  设置异常检测阈值（偏离倍数）
 * @details 阈值越高，越难触发异常（减少误报）；越低，越容易触发（增加漏报）。
 *          建议范围：2.0~5.0；默认值 3.0（即幅度超过噪声底 3 倍时报警）。
 *          [注意] 阈值必须 > 1.0，否则任何信号都会触发异常，函数会拒绝无效值。
 * @param  ratio 阈值倍数（应 > 1.0）
 */
void App_Anomaly_SetThreshold(float ratio);

/**
 * @brief  获取当前异常检测阈值
 * @return 当前阈值倍数
 */
float App_Anomaly_GetThreshold(void);

/**
 * @brief  获取指定索引的异常日志条目
 * @details index=0 表示最新的异常记录，index=1 次新，以此类推。
 *          最多可访问 APP_ANOMALY_LOG_SIZE 条历史记录。
 * @param  index 条目索引（0 = 最新）
 * @param  entry 输出条目指针（调用方分配，不得为 NULL）
 * @return 1 = 有效数据；0 = 无效（索引超出范围或尚无数据）
 */
uint8_t App_Anomaly_GetEntry(uint32_t index, App_AnomalyEntry_t *entry);

/**
 * @brief  获取日志中有效条目数
 * @return 有效条目数，范围 [0, APP_ANOMALY_LOG_SIZE]
 */
uint32_t App_Anomaly_GetCount(void);

/** @brief 清空所有异常日志记录（不重置阈值和使能状态）*/
void App_Anomaly_ClearLog(void);

/**
 * @brief  使能或禁用异常检测
 * @details 禁用时 Feed() 调用直接返回 0，不做任何计算，节省 CPU 资源。
 * @param  enable 1 = 启用，0 = 禁用
 */
void App_Anomaly_SetEnabled(uint8_t enable);

/**
 * @brief  查询异常检测是否已使能
 * @return 1 = 已使能，0 = 已禁用
 */
uint8_t App_Anomaly_GetEnabled(void);

#ifdef __cplusplus
}                                       /* extern "C" 结束 */
#endif

#endif /* __APP_ANOMALY_H */

/** @brief 初始化异常检测模块 */
void App_Anomaly_Init(void);

/**
 * @brief 馈送频谱帧，检测异常
 * @param magnitude  当前帧幅度数组
 * @param floor      噪声底数组
 * @param bin_count  bin 数量
 * @param x_angle    当前声源水平角
 * @param y_angle    当前声源垂直角
 * @param energy     当前帧能量
 * @return 1=检测到异常，0=正常
 */
uint8_t App_Anomaly_Feed(const float *magnitude, const float *floor,
                         uint16_t bin_count,
                         float x_angle, float y_angle, float energy);

/**
 * @brief 设置异常检测阈值（偏离倍数）
 * @param ratio 阈值倍数，默认 3.0（即 3σ）
 */
void App_Anomaly_SetThreshold(float ratio);

/**
 * @brief 获取异常检测阈值
 * @return 当前阈值倍数
 */
float App_Anomaly_GetThreshold(void);

/**
 * @brief 获取异常日志条目
 * @param index 条目索引（0 = 最新）
 * @param entry 输出条目指针
 * @return 1=有效, 0=无效（索引超出范围或无数据）
 */
uint8_t App_Anomaly_GetEntry(uint32_t index, App_AnomalyEntry_t *entry);

/**
 * @brief 获取日志中有效条目数
 * @return 有效条目数 [0, APP_ANOMALY_LOG_SIZE]
 */
uint32_t App_Anomaly_GetCount(void);

/**
 * @brief 清空异常日志
 */
void App_Anomaly_ClearLog(void);

/**
 * @brief 使能/禁用异常检测
 * @param enable 1=启用，0=禁用
 */
void App_Anomaly_SetEnabled(uint8_t enable);

/**
 * @brief 获取异常检测使能状态
 * @return 1=已启用，0=已禁用
 */
uint8_t App_Anomaly_GetEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_ANOMALY_H */
