/**
 * @file    app_user_config.h
 * @brief   工程统一可配置项入口
 * @details
 * 所有需要人工调节的编译期配置统一收口到这里，并按功能分组。
 * `app_task_cfg.h`、`app_display_cfg.h`、`ai_config.h` 继续保留，
 * 但它们现在主要作为兼容入口或派生常量头使用。
 */
#ifndef APP_USER_CONFIG_H
#define APP_USER_CONFIG_H

#define APP_CAMERA_ENABLE                 1u
#define APP_CAMERA_PREVIEW_W              320u
#define APP_CAMERA_PREVIEW_H              240u

#define APP_CAMERA_OVERLAY_MODE_VERIFY_ALPHA 1u
#define APP_CAMERA_OVERLAY_MODE           APP_CAMERA_OVERLAY_MODE_VERIFY_ALPHA
#define APP_CAMERA_OVERLAY_COLOR_565      0xFC60u
#define APP_CAMERA_OVERLAY_ALPHA_MAX      192u

/* ============================================================================
 * 调试与串口输出配置
 * ============================================================================ */

/* 取消下面这一行的注释后，开启 VOFA 调试输出。 */
/* #define DEBUG_ENABLE */

#define DEBUG_THROTTLE_FRAMES        20u      /**< 调试节流：每处理 20 帧才输出一次。 */
#define DEBUG_MODE                   3        /**< 调试模式：0=RMS，1=FFT，3=SRP 定位结果。 */
#define DEBUG_SPECTRUM_CHANNEL       0u       /**< FFT 调试输出使用的麦克风通道索引。 */
#define VOFA_UART_TX_TIMEOUT         5u       /**< VOFA 串口发送超时，单位 ms。 */

/* ============================================================================
 * 音频输入与 SRP-PHAT 基础参数
 * ============================================================================ */

#define MIC_CHANNELS                 16u      /**< 麦克风通道数。 */
#define FRAME_LEN                    256u     /**< 单帧采样点数。 */
#define SAMPLING_RATE                48000u   /**< 采样率，单位 Hz。 */

#define SPEED_OF_SOUND               343.0f   /**< 声速，单位 m/s。 */
#define SRP_FREQ_BIN_START           3u       /**< SRP 起始频率 bin。 */
#define SRP_FREQ_BIN_END             42u      /**< SRP 结束频率 bin。 */
#define SRP_PAIR_COUNT               40u      /**< 参与 GCC-PHAT 的麦克风对数量。 */

#define COARSE_GRID_SIZE             9u       /**< 粗搜索网格边长。 */
#define COARSE_ANGLE_MIN_DEG         (-60.0f) /**< 粗搜索最小角度，单位度。 */
#define COARSE_ANGLE_MAX_DEG         (60.0f) /**< 粗搜索最大角度，单位度。 */

#define FINE_TOP_K                   3u       /**< 进入细搜索的粗峰值数量。 */
#define FINE_GRID_SIZE               4u       /**< 单个细搜索网格边长。 */
#define FINE_SPAN_DEG                (10.0f) /**< 细搜索半跨度，单位度。 */

#define PHAT_EPSILON                 1.0e-10f /**< PHAT 白化除零保护常数。 */

/* ============================================================================
 * 低置信度策略与结果质量门限
 * ============================================================================ */

#define SRP_LOWCONF_REPORT_NEW       0u       /**< 低置信度时直接上报新结果。 */
#define SRP_LOWCONF_HOLD_LAST        1u       /**< 低置信度时保持上一次有效结果。 */
#define SRP_LOWCONF_MIXED            2u       /**< 低置信度时先保持，连续多帧后接受新结果。 */
#define SRP_LOWCONF_POLICY           SRP_LOWCONF_REPORT_NEW /**< 当前采用的低置信度策略。 */
#define SRP_LOWCONF_MIXED_HOLD_FRAMES 6u      /**< 混合策略下的保持帧数。 */

#define SRP_CONTRAST_MIN_RATIO       0.03f    /**< 最小对比度门限。 */
#define SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG 5.0f /**< 计算次峰值时排除的邻域角度。 */
#define SRP_TOPK_NMS_RADIUS          1u       /**< Top-K 非极大值抑制半径。 */

#define SRP_VALID_MIN_ENERGY         0.05f    /**< 判定结果有效的最小能量。 */
#define SRP_VALID_MIN_QUALITY        0.01f    /**< 判定结果有效的最小质量。 */

#define SRP_ENABLE_ENERGY_SOFTCAP    0u       /**< 是否启用低置信度能量软上限。 */
#define SRP_AMBIGUOUS_ENERGY_MAX     0.30f    /**< 模糊结果允许保留的最大能量。 */

/* ============================================================================
 * 定位结果朝向重映射
 * ============================================================================ */

/* 0=自定义，1=顺时针旋转 90 度，2=逆时针旋转 90 度，3=旋转 180 度，4=仅镜像 X，5=仅镜像 Y。 */
#define SRP_OUTPUT_REMAP_PRESET      0u

#if (SRP_OUTPUT_REMAP_PRESET == 0u)
#define SRP_OUTPUT_SWAP_XY           0u       /**< 是否交换 X/Y 角度。 */
#define SRP_OUTPUT_INVERT_X          0u       /**< 是否翻转 X 角度。 */
#define SRP_OUTPUT_INVERT_Y          0u       /**< 是否翻转 Y 角度。 */
#elif (SRP_OUTPUT_REMAP_PRESET == 1u)
#define SRP_OUTPUT_SWAP_XY           1u
#define SRP_OUTPUT_INVERT_X          0u
#define SRP_OUTPUT_INVERT_Y          1u
#elif (SRP_OUTPUT_REMAP_PRESET == 2u)
#define SRP_OUTPUT_SWAP_XY           1u
#define SRP_OUTPUT_INVERT_X          1u
#define SRP_OUTPUT_INVERT_Y          0u
#elif (SRP_OUTPUT_REMAP_PRESET == 3u)
#define SRP_OUTPUT_SWAP_XY           0u
#define SRP_OUTPUT_INVERT_X          1u
#define SRP_OUTPUT_INVERT_Y          1u
#elif (SRP_OUTPUT_REMAP_PRESET == 4u)
#define SRP_OUTPUT_SWAP_XY           0u
#define SRP_OUTPUT_INVERT_X          1u
#define SRP_OUTPUT_INVERT_Y          0u
#elif (SRP_OUTPUT_REMAP_PRESET == 5u)
#define SRP_OUTPUT_SWAP_XY           0u
#define SRP_OUTPUT_INVERT_X          0u
#define SRP_OUTPUT_INVERT_Y          1u
#else
#error "Invalid SRP_OUTPUT_REMAP_PRESET"
#endif

/* ============================================================================
 * FreeRTOS 任务、UI 刷新与性能统计
 * ============================================================================ */

#define APP_AUDIO_TASK_PRIO          4u       /**< 音频任务优先级。与 UI 保持同级，避免持续算力占用时饿死显示。 */
#define APP_UI_TASK_PRIO             4u       /**< UI 任务优先级。 */
#define APP_AUDIO_TASK_STACK_WORDS   2304u    /**< 音频任务堆栈深度，单位 word。 */
#define APP_UI_TASK_STACK_WORDS      2048u    /**< UI 任务堆栈深度，单位 word。 */

#define APP_PERF_DEFAULT_ENABLE      0u       /**< 上电默认是否开启性能统计。 */
#define PERF_RING_SAMPLES            64u      /**< 性能统计环形样本数。 */
#define PERF_RATE_PERIOD_MS          1000u    /**< 性能吞吐统计打印周期，单位 ms。 */

#define UI_RETRY_INIT_MS             1000u    /**< 显示初始化失败后的重试间隔，单位 ms。 */
#define UI_DEBUG_LOG                 0u       /**< UI 内部调试日志开关。 */
#define UI_FPS_MIN                   5u       /**< UI 帧率下限。 */
#define UI_FPS_MAX                   30u      /**< UI 帧率上限。 */
#define UI_FPS_DEFAULT               20u      /**< UI 默认帧率。 */

#define AUDIO_ALGO_DECIM_MIN         1u       /**< 算法抽帧最小值。 */
#define AUDIO_ALGO_DECIM_MAX         8u       /**< 算法抽帧最大值。 */
#define AUDIO_ALGO_DECIM_DEFAULT     1u       /**< 算法默认抽帧值。 */

/* ============================================================================
 * UART CLI 配置
 * ============================================================================ */

#define UI_CLI_ENABLE                1u       /**< UART CLI 总开关。 */
#define UI_CLI_LINE_MAX              96u      /**< 单条 CLI 命令最大长度。 */
#define UI_CLI_RX_DRAIN_MAX          256u     /**< 单次轮询最多处理的接收字节数。 */
#define UI_CLI_RX_RING_SIZE          1024u    /**< UART CLI 环形缓冲区大小。 */
#define UI_CLI_ALIVE_WINDOW_MS       2000u    /**< 判定 CLI 活跃的静默窗口，单位 ms。 */

/* ============================================================================
 * 显示默认模式与布局资源
 * ============================================================================ */

#define APP_DISPLAY_DEFAULT_MODE     0u       /**< 上电默认显示模式：0=FAST，1=BALANCED，2=CLEAN。 */

#define APP_DISPLAY_TEXT_WIDTH_PX    APP_DISPLAY_UI_PANEL_W /**< 兼容旧布局代码的 UI 宽度别名。 */
#define APP_DISPLAY_CAMERA_VIEW_W     640u     /**< 摄像头显示区域宽度。 */
#define APP_DISPLAY_CAMERA_VIEW_H     480u     /**< 摄像头显示区域高度。 */
#define APP_DISPLAY_HEAT_VIEW_W       480u     /**< 热力图叠加区域宽度。 */
#define APP_DISPLAY_HEAT_VIEW_H       480u     /**< 热力图叠加区域高度。 */
#define APP_DISPLAY_UI_PANEL_W        160u     /**< 右侧状态/UI 面板宽度。 */
#define APP_DISPLAY_MAX_LINE_PIXELS  1280u    /**< 单行临时缓冲允许的最大像素数。 */
#define APP_DISPLAY_BLIT_ROWS_MAX    8u       /**< 单次块渲染最大行数。 */

#define APP_DISPLAY_RAM_SAVE_LEVEL   0u       /**< RAM 节省等级：0=画质优先，1=折中，2=内存优先。 */

#if (APP_DISPLAY_RAM_SAVE_LEVEL == 0u)
#define APP_DISPLAY_FIELD_W          96u      /**< 中间显示场宽度。 */
#define APP_DISPLAY_FIELD_H          96u      /**< 中间显示场高度。 */
#elif (APP_DISPLAY_RAM_SAVE_LEVEL == 1u)
#define APP_DISPLAY_FIELD_W          72u
#define APP_DISPLAY_FIELD_H          72u
#else
#define APP_DISPLAY_FIELD_W          56u
#define APP_DISPLAY_FIELD_H          56u
#endif

#define APP_DISPLAY_HEAT_LUT_SIZE    256u     /**< 热力图颜色查找表长度。 */
#define APP_DISPLAY_NORM_RATIO_LUT_SIZE 1024u /**< 快速归一化比例查找表长度。 */
#define APP_DISPLAY_DMA2D_TIMEOUT    0x1FFFFFu /**< DMA2D 等待超时计数。 */

/* ============================================================================
 * 显示画质、诊断与动态范围配置
 * ============================================================================ */

#define APP_DISPLAY_SMOOTH_ENABLE          1u       /**< 是否编译高斯平滑逻辑。 */
#define APP_DISPLAY_SMOOTH_RADIUS          2u       /**< 高斯平滑半径。 */
#define APP_DISPLAY_SMOOTH_SIGMA           1.05f    /**< 高斯平滑 sigma。 */
#define APP_DISPLAY_SMOOTH_PASSES          1u       /**< 平滑迭代次数。 */

#define APP_DISPLAY_FINE_FUSION_ENABLE     1u       /**< 是否叠加细搜索结果。 */
#define APP_DISPLAY_FINE_KERNEL_RADIUS     4u       /**< 细搜索扩散核半径。 */
#define APP_DISPLAY_FINE_KERNEL_SIGMA      2.0f     /**< 细搜索扩散核 sigma。 */
#define APP_DISPLAY_FINE_GAIN              0.65f    /**< 细搜索融合增益。 */
#define APP_DISPLAY_FINE_MIN_RATIO         0.10f    /**< 细搜索融合最小能量比。 */

#define APP_DISPLAY_DYNAMIC_DB_FLOOR       (-45.0f) /**< 动态范围底噪下限，单位 dB。 */
#define APP_DISPLAY_DYNAMIC_MIN_PEAK       (1.0e-9f) /**< 动态归一化最小峰值保护。 */
#define APP_DISPLAY_EMA_ATTACK             0.65f    /**< 峰值上升时的 EMA 攻击系数。 */
#define APP_DISPLAY_EMA_DECAY              0.08f    /**< 峰值下降时的 EMA 衰减系数。 */
#define APP_DISPLAY_EMA_MIN_PEAK           (1.0e-7f) /**< EMA 峰值下限。 */
#define APP_DISPLAY_DYNAMIC_GAMMA          1.10f    /**< 热力图 gamma。 */
#define APP_DISPLAY_NOISE_GATE_RATIO       0.08f    /**< 噪声门限比例。 */
#define APP_DISPLAY_NOISE_ADAPT_GAIN       2.00f    /**< 背景噪声自适应增益。 */

#define APP_DISPLAY_DIAG_OVERLAY           1u       /**< 是否显示诊断叠加层。 */
#define APP_DISPLAY_DRAW_COARSE_GRID       0u       /**< 是否绘制粗网格辅助线。 */
#define APP_DISPLAY_DEBUG_LOG              0u       /**< 显示模块日志开关。 */
#define APP_DISPLAY_IDLE_TEST_PATTERN      1u       /**< 无有效峰值时显示测试图案，便于确认热力图链路正常刷新。 */
#define APP_DISPLAY_TEXT_REFRESH_DIV       2u       /**< 文本面板刷新分频。 */
#define APP_DISPLAY_BILINEAR_SAMPLING      1u       /**< 默认是否使用双线性插值。 */

#define APP_DISPLAY_CROSSHAIR_HALF_PX      6u       /**< 十字准星半径。 */
#define APP_DISPLAY_PEAK_MARKER_RADIUS_PX  4u       /**< 峰值框半径。 */
#define APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES 10u      /**< SAI 活跃状态保持帧数。 */

#define APP_SPECTRUM_INFO_ENABLE           0u       /**< 是否显示频谱状态/调试文字。默认关闭，仅保留主图与坐标。 */
#define APP_SPECTRUM_FREQ_SCALE_MODE       1u       /**< 频率坐标模式：0=线性，1=对数。音频频谱默认更常用对数坐标。 */
#define APP_SPECTRUM_LOW_FREQ_AT_BOTTOM    1u       /**< 频率方向：1=低频在下，高频在上；0=低频在上。 */
#define APP_SPECTRUM_DISPLAY_MIN_HZ        ((float)SAMPLING_RATE / (float)FRAME_LEN) /**< 频谱显示最低频率，默认从首个非 DC bin 开始。 */
#define APP_SPECTRUM_DB_FLOOR              (-48.0f) /**< 频谱显示底噪下限，单位 dB。 */
#define APP_SPECTRUM_BAR_ATTACK            0.58f    /**< 频谱条上升平滑系数。 */
#define APP_SPECTRUM_BAR_DECAY             0.36f    /**< 频谱条下降平滑系数。 */
#define APP_SPECTRUM_REF_ATTACK            0.85f    /**< 显示参考峰值上升系数。 */
#define APP_SPECTRUM_REF_DECAY             0.04f    /**< 显示参考峰值下降系数。 */

/* ============================================================================
 * 显示模式预设
 * ============================================================================ */

#define APP_DISPLAY_MODE_FAST_EMA_ATTACK           0.70f /**< FAST 模式的 EMA 攻击系数。 */
#define APP_DISPLAY_MODE_FAST_EMA_DECAY            0.15f /**< FAST 模式的 EMA 衰减系数。 */
#define APP_DISPLAY_MODE_FAST_DB_FLOOR             (-34.0f) /**< FAST 模式的动态范围底噪。 */
#define APP_DISPLAY_MODE_FAST_FINE_GAIN            0.25f /**< FAST 模式的细搜索融合增益。 */
#define APP_DISPLAY_MODE_FAST_GAMMA                1.00f /**< FAST 模式的 gamma。 */
#define APP_DISPLAY_MODE_FAST_NOISE_GATE_RATIO     0.12f /**< FAST 模式的噪声门限比例。 */
#define APP_DISPLAY_MODE_FAST_NOISE_ADAPT_GAIN     1.50f /**< FAST 模式的背景噪声增益。 */
#define APP_DISPLAY_MODE_FAST_SMOOTH_PASSES        0u    /**< FAST 模式的平滑迭代次数。 */
#define APP_DISPLAY_MODE_FAST_FINE_FUSION_ENABLE   0u    /**< FAST 模式是否叠加细搜索。 */
#define APP_DISPLAY_MODE_FAST_DRAW_COARSE_GRID     0u    /**< FAST 模式是否绘制粗网格。 */
#define APP_DISPLAY_MODE_FAST_INTERP_BILINEAR      0u    /**< FAST 模式插值：0=最近邻，1=双线性。 */
#define APP_DISPLAY_MODE_FAST_NORM_FULL            0u    /**< FAST 模式归一化：0=FAST，1=FULL。 */
#define APP_DISPLAY_MODE_FAST_TEXT_REFRESH_DIV     4u    /**< FAST 模式文本刷新分频。 */
#define APP_DISPLAY_MODE_FAST_BLIT_ROWS            APP_DISPLAY_BLIT_ROWS_MAX /**< FAST 模式块渲染行数。 */

#define APP_DISPLAY_MODE_BALANCED_NORM_FULL        0u    /**< BALANCED 模式归一化：0=FAST，1=FULL。 */

#define APP_DISPLAY_MODE_CLEAN_EMA_ATTACK          0.58f /**< CLEAN 模式的 EMA 攻击系数。 */
#define APP_DISPLAY_MODE_CLEAN_EMA_DECAY           0.06f /**< CLEAN 模式的 EMA 衰减系数。 */
#define APP_DISPLAY_MODE_CLEAN_DB_FLOOR            (-52.0f) /**< CLEAN 模式的动态范围底噪。 */
#define APP_DISPLAY_MODE_CLEAN_FINE_GAIN           0.50f /**< CLEAN 模式的细搜索融合增益。 */
#define APP_DISPLAY_MODE_CLEAN_GAMMA               1.35f /**< CLEAN 模式的 gamma。 */
#define APP_DISPLAY_MODE_CLEAN_NOISE_GATE_RATIO    0.10f /**< CLEAN 模式的噪声门限比例。 */
#define APP_DISPLAY_MODE_CLEAN_NOISE_ADAPT_GAIN    2.80f /**< CLEAN 模式的背景噪声增益。 */
#define APP_DISPLAY_MODE_CLEAN_SMOOTH_PASSES       2u    /**< CLEAN 模式的平滑迭代次数。 */
#define APP_DISPLAY_MODE_CLEAN_FINE_FUSION_ENABLE  1u    /**< CLEAN 模式是否叠加细搜索。 */
#define APP_DISPLAY_MODE_CLEAN_DRAW_COARSE_GRID    0u    /**< CLEAN 模式是否绘制粗网格。 */
#define APP_DISPLAY_MODE_CLEAN_INTERP_BILINEAR     1u    /**< CLEAN 模式插值：0=最近邻，1=双线性。 */
#define APP_DISPLAY_MODE_CLEAN_NORM_FULL           1u    /**< CLEAN 模式归一化：0=FAST，1=FULL。 */
#define APP_DISPLAY_MODE_CLEAN_TEXT_REFRESH_DIV    2u    /**< CLEAN 模式文本刷新分频。 */
#define APP_DISPLAY_MODE_CLEAN_BLIT_ROWS           6u    /**< CLEAN 模式块渲染行数。 */

/* ============================================================================
 * 编译期保护
 * ============================================================================ */

#if (SRP_FREQ_BIN_END < SRP_FREQ_BIN_START)
#error "SRP_FREQ_BIN_END must be >= SRP_FREQ_BIN_START"
#endif

#if (COARSE_GRID_SIZE < 2u)
#error "COARSE_GRID_SIZE must be >= 2"
#endif

#if (FINE_GRID_SIZE < 1u)
#error "FINE_GRID_SIZE must be >= 1"
#endif

#if (UI_FPS_MIN > UI_FPS_DEFAULT) || (UI_FPS_DEFAULT > UI_FPS_MAX)
#error "UI_FPS_DEFAULT must be in [UI_FPS_MIN, UI_FPS_MAX]"
#endif

#if (AUDIO_ALGO_DECIM_MIN > AUDIO_ALGO_DECIM_DEFAULT) || \
    (AUDIO_ALGO_DECIM_DEFAULT > AUDIO_ALGO_DECIM_MAX)
#error "AUDIO_ALGO_DECIM_DEFAULT must be in [AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX]"
#endif

#if (UI_CLI_LINE_MAX >= UI_CLI_RX_RING_SIZE)
#error "UI_CLI_LINE_MAX must be smaller than UI_CLI_RX_RING_SIZE"
#endif

#if (APP_DISPLAY_DEFAULT_MODE > 2u)
#error "APP_DISPLAY_DEFAULT_MODE must be 0, 1, or 2"
#endif

#if (APP_DISPLAY_FIELD_W < 16u) || (APP_DISPLAY_FIELD_H < 16u)
#error "APP_DISPLAY_FIELD_W/H too small"
#endif

#if (APP_DISPLAY_SMOOTH_RADIUS > 4u)
#error "APP_DISPLAY_SMOOTH_RADIUS too large"
#endif

#if (APP_DISPLAY_FINE_KERNEL_RADIUS > 8u)
#error "APP_DISPLAY_FINE_KERNEL_RADIUS too large"
#endif

#if (APP_DISPLAY_BLIT_ROWS_MAX < 1u) || (APP_DISPLAY_BLIT_ROWS_MAX > 16u)
#error "APP_DISPLAY_BLIT_ROWS_MAX must be in [1,16]"
#endif

#if (APP_SPECTRUM_FREQ_SCALE_MODE > 1u)
#error "APP_SPECTRUM_FREQ_SCALE_MODE must be 0 or 1"
#endif

#if (APP_SPECTRUM_INFO_ENABLE > 1u) || (APP_SPECTRUM_LOW_FREQ_AT_BOTTOM > 1u)
#error "APP_SPECTRUM_* boolean options must be 0 or 1"
#endif

#if (APP_DISPLAY_MODE_FAST_BLIT_ROWS < 1u) || (APP_DISPLAY_MODE_FAST_BLIT_ROWS > APP_DISPLAY_BLIT_ROWS_MAX)
#error "APP_DISPLAY_MODE_FAST_BLIT_ROWS out of range"
#endif

#if (APP_DISPLAY_MODE_CLEAN_BLIT_ROWS < 1u) || (APP_DISPLAY_MODE_CLEAN_BLIT_ROWS > APP_DISPLAY_BLIT_ROWS_MAX)
#error "APP_DISPLAY_MODE_CLEAN_BLIT_ROWS out of range"
#endif

#if (APP_CAMERA_PREVIEW_W < 4u) || (APP_CAMERA_PREVIEW_H < 4u) || \
    ((APP_CAMERA_PREVIEW_W % 4u) != 0u) || ((APP_CAMERA_PREVIEW_H % 4u) != 0u)
#error "APP_CAMERA_PREVIEW_W/H must be >= 4 and aligned to 4 pixels"
#endif

#if (APP_CAMERA_OVERLAY_ALPHA_MAX > 255u)
#error "APP_CAMERA_OVERLAY_ALPHA_MAX must be <= 255"
#endif

#if (APP_CAMERA_OVERLAY_MODE != APP_CAMERA_OVERLAY_MODE_VERIFY_ALPHA)
#error "Unsupported APP_CAMERA_OVERLAY_MODE"
#endif

#endif /* APP_USER_CONFIG_H */
