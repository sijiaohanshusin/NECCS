/**
 * @file    app_task_cfg.h
 * @brief   Shared task-level configuration macros
 */
#ifndef APP_TASK_CFG_H
#define APP_TASK_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 调试配置 (Debug Configuration)
 * ============================================================================
 *
 * 使用说明：
 *   取消注释 DEBUG_ENABLE 以开启 VOFA+ 调试数据流输出。
 *   DEBUG_MODE 选择输出内容：
 *     0 = 各通道 RMS 幅值（音量检测调试用）
 *     1 = 指定通道 FFT 频谱（频域分析用）
 *     3 = SRP-PHAT 定位结果（声源方位调试用）
 *   DEBUG_THROTTLE_FRAMES 控制输出频率，避免串口带宽饱和。
 * ============================================================================ */

/* #define DEBUG_ENABLE */              /**< 注释掉此行可关闭所有 VOFA 调试输出，节省串口带宽 */
#define DEBUG_THROTTLE_FRAMES   20u     /**< 调试节流：每处理 20 帧才输出一次，避免 UART 阻塞 */
#define DEBUG_MODE              3       /**< 调试模式选择：0=RMS  1=FFT频谱  3=SRP定位结果 */
#define DEBUG_SPECTRUM_CHANNEL  0u      /**< FFT 频谱输出时选择的麦克风通道索引（0 为第一路） */

/* ============================================================================
 * UI 刷新参数 (UI Refresh Parameters)
 * ============================================================================
 *
 * FPS 设计说明：
 *   目标帧率 [UI_FPS_MIN, UI_FPS_MAX] 由运行时配置动态调整。
 *   vTaskDelayUntil 保证帧周期稳定，不受渲染耗时抖动影响。
 *
 * CLI 缓冲说明：
 *   环形缓冲区 UI_CLI_RX_RING_SIZE = 1024 字节，足以容纳多条完整命令。
 *   每次 UI 循环最多消耗 UI_CLI_RX_DRAIN_MAX = 256 字节，防止 CLI 占用过多 CPU。
 *
 * 算法抽帧说明：
 *   AUDIO_ALGO_DECIM = N 表示每 N 帧只跑一次 SRP-PHAT，其余帧复用上次结果，
 *   用于在精度与 CPU 占用之间做权衡。
 * ============================================================================ */

#define UI_RETRY_INIT_MS         1000u  /**< 显示初始化失败后的重试间隔 (ms)，避免死循环空转 */
#define UI_DEBUG_LOG             0u     /**< UI 内部诊断日志开关：0=关闭  1=通过 printf 输出 */
#define UI_CLI_ENABLE            1u     /**< UART CLI 功能总开关：0=编译时裁掉整个 CLI 模块 */
#define UI_CLI_LINE_MAX          96u    /**< 单条 CLI 命令行最大字节数（含终止符），超出部分被截断 */
#define UI_CLI_RX_DRAIN_MAX      256u   /**< 每次 ui_cli_poll() 最多处理的字节数，防止过度占用 CPU */
#define UI_CLI_RX_RING_SIZE      1024u  /**< UART 接收环形缓冲区大小 (字节)，必须为 2 的幂次方效果最佳 */
#define UI_FPS_MIN               5u     /**< UI 帧率下限 (fps)，低于此值会强制 clamp 到 5 fps */
#define UI_FPS_MAX               30u    /**< UI 帧率上限 (fps)，高于此值会强制 clamp 到 30 fps */
#define UI_FPS_DEFAULT           20u    /**< 上电默认帧率 (fps)，20 fps = 50 ms 每帧 */
#define AUDIO_ALGO_DECIM_MIN     1u     /**< 算法抽帧比最小值 1（每帧都跑 SRP-PHAT，精度最高） */
#define AUDIO_ALGO_DECIM_MAX     8u     /**< 算法抽帧比最大值 8（8 帧才跑一次，CPU 占用最低） */
#define AUDIO_ALGO_DECIM_DEFAULT 1u     /**< 上电默认抽帧比，默认每帧都执行 SRP-PHAT 算法 */
#define PERF_RING_SAMPLES        64u    /**< 性能环形缓冲区容量（最近 64 个样本用于计算 p95 延迟） */
#define PERF_RATE_PERIOD_MS      1000u  /**< 性能速率打印周期 (ms)，每秒输出一次吞吐率统计 */

/* ============================================================================
 * 任务优先级 (Task Priorities)
 * ============================================================================
 *
 * 两个任务设置为相同优先级（4），由 FreeRTOS 时间片轮转调度。
 * 音频任务依赖队列阻塞（portMAX_DELAY），有数据时才运行，不会饿死 UI 任务。
 * UI 任务依赖 vTaskDelayUntil 定周期唤醒，空闲时主动出让 CPU。
 * 优先级不宜设置过高，以免抢占系统级任务（如 IDLE、TimerDaemon）。
 * ============================================================================ */

#define APP_AUDIO_TASK_PRIO     4u  /**< 音频处理任务优先级，与 UI 任务同级，由队列阻塞协同调度 */
#define APP_UI_TASK_PRIO        4u  /**< UI 显示任务优先级，与音频任务同级，由定时唤醒协同调度 */
#ifdef __cplusplus
}
#endif

#endif /* APP_TASK_CFG_H */
