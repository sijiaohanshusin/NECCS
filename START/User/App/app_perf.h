/**
 * @file    app_perf.h
 * @brief   Runtime performance profiling interfaces
 */
#ifndef APP_PERF_H
#define APP_PERF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 性能剖析段枚举，标识各处理阶段 */
typedef enum {
    APP_PERF_SEC_AUDIO_TOTAL = 0u,   /**< 音频处理总计 */
    APP_PERF_SEC_AUDIO_DEINT = 1u,   /**< 音频解交织 */
    APP_PERF_SEC_AUDIO_FFT = 2u,     /**< 音频 FFT 变换 */
    APP_PERF_SEC_AUDIO_SRP = 3u,     /**< SRP 声源定位算法 */
    APP_PERF_SEC_UI_LOOP = 4u,       /**< UI 主循环 */
    APP_PERF_SEC_UI_SNAPSHOT = 5u,   /**< UI 数据快照 */
    APP_PERF_SEC_UI_RENDER = 6u,     /**< UI 渲染 */
    APP_PERF_SEC_DISP_PREPARE = 7u,  /**< 显示数据准备 */
    APP_PERF_SEC_DISP_NORM = 8u,     /**< 显示归一化 */
    APP_PERF_SEC_DISP_RENDER = 9u,   /**< 显示渲染 */
    APP_PERF_SEC_DISP_OVERLAY = 10u, /**< 显示叠加层绘制 */
    APP_PERF_SEC_DISP_COMMIT = 11u,  /**< 显示提交（刷屏） */
    APP_PERF_SEC_COUNT               /**< 剖析段总数（哨兵值） */
} App_Perf_Section_t;

/**
 * @brief 初始化性能剖析模块（清零所有计数器）
 */
void App_Perf_Init(void);

/**
 * @brief 设置性能剖析使能状态
 * @param enable 非零值使能，0 禁用
 */
void App_Perf_SetEnabled(uint8_t enable);

/**
 * @brief 查询性能剖析是否已使能
 * @return 非零值表示已使能，0 表示已禁用
 */
uint8_t App_Perf_IsEnabled(void);

/**
 * @brief 复位所有剖析段的统计数据
 */
void App_Perf_Reset(void);

/**
 * @brief 开始一次 CPU 周期计时
 * @return 起始周期计数值（用于传给 App_Perf_EndCycles）
 */
uint32_t App_Perf_BeginCycles(void);

/**
 * @brief 结束一次 CPU 周期计时并累加到指定剖析段
 * @param section      目标剖析段
 * @param start_cycles 由 App_Perf_BeginCycles 返回的起始周期值
 */
void App_Perf_EndCycles(App_Perf_Section_t section, uint32_t start_cycles);

/**
 * @brief 递增音频处理帧计数
 */
void App_Perf_CountAudioProc(void);

/**
 * @brief 递增 UI 循环帧计数
 */
void App_Perf_CountUiLoop(void);

/**
 * @brief 按周期性条件打印音频/UI 处理速率
 */
void App_Perf_MaybePrintRates(void);

/**
 * @brief 输出所有剖析段的详细统计信息
 */
void App_Perf_Dump(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_PERF_H */
