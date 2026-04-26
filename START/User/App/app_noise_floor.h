/**
 * @file    app_noise_floor.h
 * @brief   自适应背景噪声底估计与频域能量减噪模块接口
 * @details
 * 算法原理：
 *   维护每个频率 bin 的长期背景幅度估计值（噪声底），
 *   使用指数移动平均（EMA）实现慢速自适应跟踪：
 *
 *     noise_floor[k] = alpha * magnitude[k] + (1 - alpha) * noise_floor[k]
 *
 *   其中：
 *   - magnitude[k]    = 当前帧第 k 个频率 bin 的幅度
 *   - noise_floor[k]  = 第 k 个 bin 的长期背景幅度估计
 *   - alpha           = EMA 平滑系数（典型值 0.001~0.05，越小越慢越稳定）
 *
 * 在算法管线中的位置：
 *   GCC-PHAT（互功率谱相位变换）计算完成后，
 *   在 SRP 角度累加之前，将当前频域幅度减去噪声底，
 *   使得空间谱对稳定背景噪声源（风扇、空调）不敏感。
 *
 * 核心数据：
 *   - 内部维护 NOISE_FLOOR_BINS（=128）个 float 值的噪声底数组
 *   - 数组放置在 AXI SRAM（可缓存区域），非时间关键路径
 *
 * 调用时机约束：
 *   [注意] App_NoiseFloor_Update() 必须在每个算法帧调用（即使 enabled=false），
 *          否则噪声底估计会停止更新，打开时会出现"跃变"。
 *          only Update 总在调用，Apply（减噪）由 enabled 标志控制。
 *
 * 一键校零功能（Calibrate）：
 *   当外部环境声音被认为是纯背景噪声时（如静音状态），
 *   可调用 App_NoiseFloor_Calibrate() 将当前幅度快照作为噪声底基准，
 *   此后算法将立即从当前帧的幅度水平开始减噪，无需等待 EMA 收敛。
 *
 * [改进] 目前使用对称 EMA（上升和下降用同一 alpha），
 *        可考虑非对称 EMA：对于幅度上升用较大 alpha（快速跟随突发噪声），
 *        对于幅度下降用较小 alpha（慢速退出），实现更鲁棒的噪声底追踪。
 *
 * [改进] 噪声底数组大小固定为 128 bins，与 FFT 点数耦合；
 *        若配置修改了 FFT 大小，此处需同步调整。
 */
#ifndef __APP_NOISE_FLOOR_H             /* 头文件防重复包含保护（开始）*/
#define __APP_NOISE_FLOOR_H             /* 定义本文件标识宏 */

#include <stdint.h>                     /* uint8_t、uint16_t、float 等标准整数类型 */

#ifdef __cplusplus                      /* C++ 兼容声明 */
extern "C" {                            /* 开始 C 链接区域 */
#endif

/**
 * @brief   初始化噪声底模块
 * @details 将所有 bin 的噪声底估计值清零，设置默认 alpha 值，
 *          并将 enabled 标志设置为默认状态（由 app_user_config.h 中的编译期配置决定）。
 * @note    在 FreeRTOS 调度器启动之前（app_main_task.c 初始化序列中）调用。
 *          多次调用安全（幂等）。
 */
void App_NoiseFloor_Init(void);

/**
 * @brief   更新噪声底估计（EMA 平滑）
 * @details 对内部 noise_floor 数组中每个 bin 执行：
 *            noise_floor[k] = alpha * magnitude[k] + (1-alpha) * noise_floor[k]
 *          [注意] 无论 enabled 是否为真，都应在每帧调用此函数，
 *                 以确保噪声底持续跟踪背景声场变化。
 * @param   magnitude  当前帧各 bin 的幅度数组（频域模长，非功率谱），
 *                     元素个数必须 >= bin_count。
 *                     [注意] 数组值应为非负数；若传入负值，EMA 跟踪会向错误方向移动。
 * @param   bin_count  要更新的 bin 数量（通常等于 FFT_LEN/2 = 128）。
 *                     若 bin_count > NOISE_FLOOR_BINS，多余的 bin 被忽略。
 */
void App_NoiseFloor_Update(const float *magnitude, uint16_t bin_count);

/**
 * @brief   获取当前噪声底估计值（用于减噪）
 * @details 将内部 noise_floor 数组的前 bin_count 个值复制到 out_floor。
 *          调用方负责分配足够大小的 out_floor 缓冲区。
 * @param   out_floor  输出缓冲区，接收噪声底估计值（调用方分配，长度须 >= bin_count）。
 *                     [注意] 此函数为轻量级复制操作，可在算法帧内安全调用。
 * @param   bin_count  要获取的 bin 数量（通常等于 FFT_LEN/2 = 128）。
 */
void App_NoiseFloor_Get(float *out_floor, uint16_t bin_count);

/**
 * @brief   "一键校零"：将当前帧幅度快照覆盖为噪声底基准
 * @details 将 noise_floor[k] = magnitude[k] 直接赋值（非 EMA 更新），
 *          使噪声底立即跳至当前幅度水平，无需等待 EMA 收敛时间
 *          （EMA 完全收敛需约 1/alpha 帧 = 20-1000 帧 = 0.1~5 秒）。
 *          典型使用场景：用户按下"噪声标定"按键后调用。
 * @param   magnitude  当前帧各 bin 的幅度数组（将用作新噪声底基准）。
 * @param   bin_count  要校零的 bin 数量。
 * @note    调用此函数后 EMA 继续运行，noise_floor 会从新基准开始跟踪。
 */
void App_NoiseFloor_Calibrate(const float *magnitude, uint16_t bin_count);

/**
 * @brief   设置 EMA 平滑系数 alpha
 * @details alpha 控制噪声底跟踪速度：
 *            - alpha 越大（如 0.1）：噪声底响应越快，但对短暂噪声敏感
 *            - alpha 越小（如 0.001）：噪声底变化越缓慢，但适应环境需要更长时间
 *          收敛时间约估计：T_ms ≈ (1/alpha) × 帧周期_ms
 *          例：alpha=0.01, 帧周期=5.33ms → T ≈ 100帧 × 5.33ms ≈ 533ms
 * @param   alpha  EMA 系数，有效范围 [0.001, 0.1]。
 *                 超出范围的值将被 Clamp（实现层保护）。
 *                 [注意] 若 alpha = 1.0，噪声底等于上一帧幅度，减噪效果不稳定。
 */
void App_NoiseFloor_SetAlpha(float alpha);

/**
 * @brief   获取当前 EMA 平滑系数
 * @return  当前 alpha 值（范围 [0.001, 0.1]）。
 *          用于 UI/CLI 参数查询显示。
 */
float App_NoiseFloor_GetAlpha(void);

/**
 * @brief   使能或禁用噪声减除功能
 * @details 使能时：SRP 管线在频域累加前减去噪声底（noise_floor[k]）。
 *          禁用时：噪声底估计继续更新（Update 仍被调用），
 *                  但频域幅度不减任何背景值，SRP 使用原始功率谱。
 * @param   enable  1 = 启用噪声减除；0 = 禁用。
 * @note    可通过 CLI 命令 "cfg noise on/off" 动态切换，无需重启。
 */
void App_NoiseFloor_SetEnabled(uint8_t enable);

/**
 * @brief   查询噪声减除是否已启用
 * @return  1 = 当前已启用；0 = 当前已禁用。
 *          用于 CLI 状态查询和 UI 开关状态同步。
 */
uint8_t App_NoiseFloor_GetEnabled(void);

#ifdef __cplusplus                      /* 结束 C 链接区域 */
}
#endif

#endif /* __APP_NOISE_FLOOR_H */        /* 头文件防重复包含保护（结束）*/
