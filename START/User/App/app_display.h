/**
 * @file    app_display.h
 * @brief   声学相机 UI 显示模块接口
 * @details 负责将 SRP-PHAT 声源定位结果渲染为热力图并显示到 800×480 LCD
 *
 * 显示流水线：
 * 1. 接收声源位置 (Sound_Pos_t) 和 SRP 功率可视化帧 (SRP_VisFrame_t)
 * 2. 将 SRP 功率图映射到 96×96 标量场，进行平滑和归一化
 * 3. 伪彩色映射 + 双线性插值 → 800×480 热力图
 * 4. 叠加十字光标和文本信息
 * 5. 通过 LTDC 双缓冲刷新到 LCD
 *
 * 三种显示模式：
 * - FAST: 最高帧率，简化渲染
 * - BALANCED: 平衡质量与性能（默认）
 * - CLEAN: 最高质量，双线性插值
 */

#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

#include "app_display_cfg.h"
#include "app_main_task.h"
#include "ai_beamforming.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   显示模式枚举
 * @details 控制热力图渲染的质量与速度权衡
 */
typedef enum
{
    APP_DISPLAY_MODE_FAST     = 0u,  /**< 快速模式：最近邻插值，不平滑，帧率最高 */
    APP_DISPLAY_MODE_BALANCED = 1u,  /**< 平衡模式：双线性插值 + 轻度平滑（默认） */
    APP_DISPLAY_MODE_CLEAN    = 2u   /**< 清晰模式：完整质量，多次平滑，帧率较低 */
} App_Display_Mode_t;

/**
 * @brief   热力图插值方式枚举
 * @details 控制低分辨率标量场放大到屏幕分辨率时的插值算法
 */
typedef enum
{
    APP_DISPLAY_INTERP_NEAREST  = 0u,  /**< 最近邻插值：速度快，有像素化感 */
    APP_DISPLAY_INTERP_BILINEAR = 1u   /**< 双线性插值：平滑过渡，视觉效果好 */
} App_Display_Interp_t;

/**
 * @brief   热力图功率归一化方式枚举
 * @details 控制 SRP 功率值映射到 [0,1] 的算法
 */
typedef enum
{
    APP_DISPLAY_NORM_FAST = 0u,  /**< 快速归一化：仅用粗搜峰值做参考，速度快 */
    APP_DISPLAY_NORM_FULL = 1u   /**< 完整归一化：EMA 动态范围追踪 + dB 映射，视觉动态范围宽 */
} App_Display_Norm_t;

/**
 * @brief   显示运行时可调配置
 * @details 可通过 CLI 命令在运行时修改，无需重新编译
 *
 * EMA (指数移动平均) 参数说明：
 * - ema_attack: 峰值上升速度 (0~1，越大追踪越快，推荐 0.5~0.8)
 * - ema_decay:  峰值下降速度 (0~1，越小保持越久，推荐 0.05~0.15)
 * - db_floor:   dB 下限，低于此值视为噪声底噪 (推荐 -40 ~ -50 dB)
 *
 * 精搜融合参数：
 * - fine_gain: 精搜结果在热力图中的权重 (0~1，推荐 0.5~0.8)
 * - fine_fusion_enable: 是否将精搜结果叠加到热力图 (0=禁用, 1=启用)
 *
 * 平滑参数：
 * - smooth_passes: 高斯平滑迭代次数 (0=不平滑, 1~3=轻到重)
 * - gamma: 伽马校正系数 (1.0=线性, >1 压暗低值, 推荐 1.0~1.5)
 *
 * 噪声门限参数：
 * - noise_gate_ratio: 低于此比例的功率视为噪声置零 (0~0.2)
 * - noise_adapt_gain: 噪声门限自适应增益系数
 */
typedef struct
{
    float   ema_attack;          /**< EMA 峰值上升速度系数 [0,1] */
    float   ema_decay;           /**< EMA 峰值下降速度系数 [0,1] */
    float   db_floor;            /**< dB 域下限 (负值，如 -45.0f) */
    float   fine_gain;           /**< 精搜结果融合权重 [0,1] */
    float   gamma;               /**< 伽马校正系数 (≥1.0) */
    float   noise_gate_ratio;    /**< 噪声门限比例 [0,1] */
    float   noise_adapt_gain;    /**< 噪声门限自适应增益 */
    uint8_t smooth_passes;       /**< 高斯平滑迭代次数 [0,3] */
    uint8_t fine_fusion_enable;  /**< 精搜融合开关 (0=关, 1=开) */
    uint8_t draw_coarse_grid;    /**< 是否绘制粗搜网格点 (0=关, 1=开) */
    uint8_t interp_mode;         /**< 插值模式 @ref App_Display_Interp_t */
    uint8_t norm_mode;           /**< 归一化模式 @ref App_Display_Norm_t */
    uint8_t text_refresh_div;    /**< 文字刷新分频 (N = 每 N 帧刷新一次文字) */
    uint8_t blit_rows;           /**< DMA2D 每次传输的行数 (1~8，越大越快但占用总线) */
} App_Display_RuntimeCfg_t;

/**
 * @brief   初始化显示模块
 * @details 执行以下初始化：
 *          1. LTDC 控制器初始化 (时钟、图层配置)
 *          2. DMA2D 加速器初始化
 *          3. LCD 驱动初始化 (背光、复位)
 *          4. 双帧缓冲分配 (SDRAM 0xC0000000)
 *          5. 伪彩色调色板生成
 *          6. 初始化 SRP 可视化状态
 *
 * @note    在 main() 中调用，FreeRTOS 启动前
 * @note    耗时约 100ms (主要是 LCD 复位和时序稳定)
 */
void App_Display_Init(void);

/**
 * @brief   渲染并刷新一帧显示
 * @details 完整渲染流水线：
 *          1. 从 SRP 可视化帧提取功率数据
 *          2. 精搜结果高斯融合到标量场
 *          3. 高斯平滑标量场
 *          4. EMA 动态归一化 (dB 域)
 *          5. 噪声门限抑制
 *          6. 伽马校正
 *          7. 伪彩色映射 + 插值 → 帧缓冲
 *          8. 叠加十字光标 (声源方向指示)
 *          9. 叠加文字信息 (角度、能量、帧率)
 *          10. 请求 LTDC 双缓冲换页
 *
 * @param   pos           声源位置结构体 (x_angle, y_angle, energy)
 * @param   vis_frame     SRP 功率可视化帧 (粗搜 + 精搜功率数组)
 * @param   frame_seq     当前音频帧序号 (用于帧率计算)
 * @param   sai_dma_active SAI DMA 是否活跃 (0=无音频, 1=正常采集)
 *
 * @note    在 UI_Display_Task 中调用，约 33ms 周期
 */
void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active);

/**
 * @brief   查询显示模块是否初始化完成
 * @return  1: 初始化完成, 0: 初始化未完成或失败
 */
uint8_t App_Display_IsReady(void);

/**
 * @brief   设置运行时显示配置
 * @param   cfg  新配置结构体指针 (内容会被拷贝)
 * @note    线程安全：内部使用临界区保护
 */
void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg);

/**
 * @brief   获取当前运行时显示配置
 * @param   cfg  输出配置结构体指针
 */
void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg);

/**
 * @brief   设置显示模式
 * @param   mode  目标显示模式 @ref App_Display_Mode_t
 * @note    不同模式会自动调整插值、平滑等参数
 */
void App_Display_SetMode(App_Display_Mode_t mode);

/**
 * @brief   获取当前显示模式
 * @return  当前显示模式 @ref App_Display_Mode_t
 */
App_Display_Mode_t App_Display_GetMode(void);

/**
 * @brief   获取显示模式的名称字符串
 * @param   mode  显示模式
 * @return  模式名称字符串 (如 "fast", "balanced", "clean")
 */
const char *App_Display_ModeName(App_Display_Mode_t mode);

/**
 * @brief   获取插值方式的名称字符串
 * @param   interp  插值方式
 * @return  插值名称字符串 (如 "nearest", "bilinear")
 */
const char *App_Display_InterpName(App_Display_Interp_t interp);

/**
 * @brief   获取归一化方式的名称字符串
 * @param   norm  归一化方式
 * @return  归一化名称字符串 (如 "fast", "full")
 */
const char *App_Display_NormName(App_Display_Norm_t norm);

/** @brief 显示初始化阶段序号 (0=未开始, 正数=进行中, 0xFF=完成) */
extern volatile uint32_t g_display_init_stage;

/** @brief 显示初始化错误代码 (0=正常, 非0=失败阶段) */
extern volatile uint32_t g_display_init_error;

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
