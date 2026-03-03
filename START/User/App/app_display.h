/**
 * @file    app_display.h
 * @brief   LCD 显示模块接口
 * @details 负责渲染声学相机的可视化界面
 *
 * 显示内容：
 * - 热力图：SRP 功率分布 (9×9 粗搜网格)
 * - 十字光标：声源定位结果 (x_angle, y_angle)
 * - 诊断信息：帧序号、能量、质量指标
 *
 * 刷新率：30 FPS (33ms 周期)
 * 分辨率：800×480 (RGB565)
 */

#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

#include "arm_math.h"
#include "app_main_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 显示模块接口函数 (Display Module Interface)
 * ============================================================================ */

/**
 * @brief   显示模块初始化
 * @details 执行以下初始化：
 *          1. LCD 硬件初始化 (LTDC + DMA2D)
 *          2. 帧缓冲区清零
 *          3. 背景网格绘制
 *          4. 状态变量重置
 *
 * 初始化阶段：
 * - Stage 0: 开始初始化
 * - Stage 1: LCD 硬件初始化
 * - Stage 2: 帧缓冲区清零
 * - Stage 3: 背景绘制
 * - Stage 4: 初始化完成
 *
 * @note    可重复调用，内部会重置状态
 * @note    初始化失败时，g_display_init_error 会记录错误码
 */
void App_Display_Init(void);

/**
 * @brief   渲染一帧 UI
 * @details 渲染流程：
 *          1. 绘制热力图 (9×9 网格，颜色映射功率值)
 *          2. 绘制十字光标 (标记声源位置)
 *          3. 绘制诊断信息 (帧序号、能量、质量)
 *          4. 刷新 LCD 显示
 *
 * 热力图颜色映射：
 * - 蓝色：低功率 (0.0 - 0.3)
 * - 绿色：中功率 (0.3 - 0.6)
 * - 黄色：高功率 (0.6 - 0.8)
 * - 红色：极高功率 (0.8 - 1.0)
 *
 * @param   pos           声源定位结果 (x_angle, y_angle, energy)
 * @param   coarse_power  粗搜功率数组 (9×9 = 81 个点)
 * @param   frame_seq     帧序号 (用于显示和调试)
 *
 * @note    耗时：约 10ms @ 480MHz (包含 DMA2D 传输)
 * @note    必须在 App_Display_Init() 成功后调用
 */
void App_Display_Render(const Sound_Pos_t *pos, const float32_t *coarse_power, uint32_t frame_seq);

/**
 * @brief   返回显示模块是否初始化成功
 * @details 用于任务启动前检查显示模块状态
 *
 * @return  1: 初始化成功，可以调用 App_Display_Render()
 *          0: 初始化失败或未初始化
 */
uint8_t App_Display_IsReady(void);

/* ============================================================================
 * 诊断变量 (Diagnostic Variables)
 * ============================================================================ */

/**
 * @brief   初始化阶段标识
 * @details 用于串口/调试器定位初始化问题
 *
 * 阶段定义：
 * - 0: 开始初始化
 * - 1: LCD 硬件初始化
 * - 2: 帧缓冲区清零
 * - 3: 背景绘制
 * - 4: 初始化完成
 */
extern volatile uint32_t g_display_init_stage;

/**
 * @brief   初始化错误码
 * @details 记录初始化失败的原因
 *
 * 错误码定义：
 * - 0: 无错误
 * - 1: LCD 硬件初始化失败
 * - 2: 帧缓冲区分配失败
 * - 3: DMA2D 初始化失败
 */
extern volatile uint32_t g_display_init_error;

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
