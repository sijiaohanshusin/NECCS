/**
 * @file    app_user_config.h
 * @brief   工程统一可配置项入口 — 所有编译期可调参数的集中管理文件
 * @details
 * 设计目标：
 *   将所有"需要人工根据硬件/场景调整"的编译期参数统一收录在此文件，
 *   避免工程参数散落在各个模块头文件中，导致调整时遗漏或冲突。
 *
 * 文件组织结构（按功能分组）：
 *   1. 摄像头（OV2640）相关配置
 *   2. 触摸屏（GT9xxx/FT5206）相关配置
 *   3. LVGL UI 框架配置
 *   4. 调试与串口输出（VOFA+）
 *   5. 音频输入与 SRP-PHAT 基础参数（麦克风数、帧长、声速等）
 *   6. 低置信度定位结果处理策略
 *   7. 定位结果朝向重映射（适配不同安装方向）
 *   8. FreeRTOS 任务配置（优先级、堆栈）
 *   9. UART CLI 配置
 *   10. 显示布局与分辨率
 *   11. 显示画质参数（EMA、gamma、噪声门限等）
 *   12. 显示模式预设（FAST/BALANCED/CLEAN）
 *   13. 编译期一致性保护（#error 静态断言）
 *
 * 与其他配置头文件的关系：
 *   - app_task_cfg.h：历史遗留兼容入口，转发到本文件
 *   - app_display_cfg.h：若存在，作为显示子系统的派生宏入口
 *   - ai_config.h：作为算法层的独立配置，与本文件参数可能重叠（音频参数以本文件为准）
 *
 * 修改注意事项：
 *   [注意] 修改 MIC_CHANNELS / FRAME_LEN / SAMPLING_RATE 后必须重新生成 ai_srp_lut.c，
 *          否则 LUT 中的相位延迟数据与硬件不匹配，定位结果会错误。
 *   [注意] 修改 COARSE_GRID_SIZE / FINE_GRID_SIZE 会改变定位精度和 CPU 负载的折衷，
 *          改动前请用工具脚本验证角度分辨率是否满足需求。
 *   [改进] 未来可以将运行时可调参数（如 fps、decim）迁移到 Flash 存储的结构体，
 *          实现掉电保存，避免每次需要重新编译。
 */
#ifndef APP_USER_CONFIG_H        /* 头文件防重复包含保护（开始） */
#define APP_USER_CONFIG_H        /* 定义本文件宏，防止多次包含 */

/* ============================================================================
 * 摄像头（OV2640）配置
 * ============================================================================
 * OV2640 是 200 万像素 CMOS 图像传感器，通过 DCMI 接口连接到 STM32H743。
 * 摄像头提供声学相机的视觉背景图像，叠加热力图后形成"声学+视觉"融合输出。
 * ============================================================================ */

#define APP_CAMERA_ENABLE                 1u    /**< 摄像头功能总开关：1=启用，0=禁用（禁用后热力图无视觉背景） */
#define APP_CAMERA_PREVIEW_W              320u  /**< 摄像头预览分辨率宽度（像素）。OV2640 推荐输出分辨率：320×240（QVGA） */
#define APP_CAMERA_PREVIEW_H              240u  /**< 摄像头预览分辨率高度（像素）。[注意] 必须是 4 的倍数（DMA 对齐要求） */

/* 叠加模式枚举值定义（目前只支持 Alpha 验证模式） */
#define APP_CAMERA_OVERLAY_MODE_VERIFY_ALPHA 1u  /**< 叠加模式：颜色键控 + Alpha 透明度。将热力图以半透明方式叠加在摄像头图像上 */
#define APP_CAMERA_OVERLAY_MODE           APP_CAMERA_OVERLAY_MODE_VERIFY_ALPHA  /**< 当前使用的叠加模式（当前只支持 VERIFY_ALPHA）[改进] 可增加 OPAQUE/BLEND 等模式 */
#define APP_CAMERA_OVERLAY_COLOR_565      0xFC60u  /**< 热力图颜色键（RGB565 格式）：0xFC60 ≈ 纯绿色，用于色度键控（chroma-key）抠图 */
#define APP_CAMERA_OVERLAY_ALPHA_MAX      192u  /**< 热力图叠加最大不透明度 [0, 255]，192 ≈ 75% 不透明（既显示热力图，又能透见背景）[改进] 此值应可通过 CLI 动态调整 */

/* ============================================================================
 * 触摸屏配置（GT9xxx / FT5206 双驱动，自动识别）
 * ============================================================================
 * 触摸屏通过 I2C 与 STM32H743 通信，支持 GT9xxx 和 FT5206 两种控制器。
 * 系统上电时自动探测控制器类型，无需手动配置（由 touch.c 中的探测逻辑决定）。
 * ============================================================================ */

#define APP_TOUCH_ENABLE                  1u    /**< 触摸屏功能总开关：1=启用，0=禁用 */
#define APP_TOUCH_RETRY_MS                1000u /**< 触摸初始化失败后的重试间隔（毫秒）。用于上电时 I2C 总线未就绪的情况 */
#define APP_TOUCH_TEST_ENABLE             1u    /**< 触摸测试界面开关：1=启用 app_touch_test 模块（可通过 CLI 进入触摸测试模式） */

/* ============================================================================
 * LVGL UI 框架配置
 * ============================================================================
 * LVGL（Light and Versatile Graphics Library）v8 是本工程的主 UI 框架。
 * UI 任务（UI_Display_Task）独占 LVGL，禁止其他任务直接调用 LVGL API。
 * [注意] LVGL 不是线程安全的，所有 lv_xxx() 调用必须在 UI_Display_Task 内。
 * ============================================================================ */

/* LVGL 调试带入说明：
 * 在 LVGL 调试期间，系统默认以 LVGL 后端启动，使测试 UI 在复位后立即显示。
 * 若要恢复到旧版（Legacy）显示路径上电，将 APP_LVGL_BOOT_AS_DEFAULT 设置为 0。
 */
#define APP_LVGL_ENABLE                   1u    /**< LVGL 框架总开关：1=启用 LVGL 渲染后端，0=仅使用 Legacy 帧缓冲渲染 */
#define APP_LVGL_BOOT_AS_DEFAULT          1u    /**< 上电默认后端：1=LVGL 后端，0=Legacy 后端。[改进] 未来可从 Flash 读取上次选择 */
#define APP_LVGL_HANDLER_PERIOD_MS        20u   /**< LVGL 事件处理周期（毫秒）= 50 Hz。影响触摸响应速度，值越小响应越快但 CPU 占用越高 */
#define APP_LVGL_TEST_UI_ENABLE           1u    /**< LVGL 测试 UI 开关：1=显示测试/诊断界面组件，0=仅显示主功能界面 */

/* ============================================================================
 * 调试与串口输出配置（VOFA+ 波形工具）
 * ============================================================================
 * 调试输出通过 UART1（波特率 921600）发送到 PC 端 VOFA+ 上位机工具。
 * 为避免 UART 传输影响实时性，采用节流机制（每 DEBUG_THROTTLE_FRAMES 输出一次）。
 *
 * [注意] 调试输出会占用 UART 带宽和 CPU 时间，生产固件务必注释掉 #define DEBUG_ENABLE。
 * [注意] DEBUG_ENABLE 被注释时，编译器会完全优化掉所有 #ifdef DEBUG_ENABLE 块，零运行时开销。
 * ============================================================================ */

/* 取消下面这一行的注释后，开启 VOFA 调试输出（生产固件必须注释掉）。 */
/* #define DEBUG_ENABLE */              /* [注意] 调试宏：开启后 UART 输出大量数据，会干扰实时性 */

#define DEBUG_THROTTLE_FRAMES        20u      /**< 调试节流：每处理 20 帧才输出一次（避免 UART 带宽饱和）。@48kHz/256点 → 每 20×5.3ms=106ms 输出一次 */
#define DEBUG_MODE                   3        /**< 调试模式选择：0=各通道 RMS 功率，1=单通道 FFT 幅度谱，3=SRP 定位结果（角度+能量）*/
#define DEBUG_SPECTRUM_CHANNEL       0u       /**< DEBUG_MODE=1 时使用的麦克风通道索引（0-based），0=第一个麦克风 */
#define VOFA_UART_TX_TIMEOUT         5u       /**< VOFA 串口发送超时（毫秒）。超时后跳过本帧数据，保证音频任务不因串口阻塞卡死 */

/* ============================================================================
 * 音频输入与 SRP-PHAT 基础参数
 * ============================================================================
 * 这些参数与硬件直接绑定，通常不允许在运行时修改。
 * 修改 MIC_CHANNELS/FRAME_LEN/SAMPLING_RATE 后必须重新生成 LUT（ai_srp_lut.c）。
 *
 * SRP-PHAT 频率范围建议：
 *   - 人声频率：85~3400 Hz（对应 bin ≈ 0.45~18 @256点/48kHz）
 *   - 工业噪声：500~4000 Hz（对应 bin ≈ 2.7~21 @256点/48kHz）
 *   - 当前设置：SRP_FREQ_BIN_START=3 → 562.5 Hz, SRP_FREQ_BIN_END=42 → 7875 Hz
 * ============================================================================ */

#define MIC_CHANNELS                 16u      /**< 麦克风通道数，当前为 16 路 TDM 阵列。[注意] 修改后需同步更新 SAI TDM slot 配置和 LUT */
#define FRAME_LEN                    256u     /**< 单帧采样点数（FFT 窗口大小）。帧周期 = 256/48000 ≈ 5.33ms。[注意] 必须是 2 的幂次方（CMSIS-DSP 要求） */
#define SAMPLING_RATE                48000u   /**< 采样率（Hz）。与 SAI 的 MCLK 分频比直接关联，修改需同步配置 SAI 时钟树 */

#define SPEED_OF_SOUND               343.0f   /**< 声速（m/s），标准大气压、20°C 下的值。[改进] 可根据温度传感器动态修正（每+1°C 声速增加约 0.6 m/s） */
#define SRP_FREQ_BIN_START           3u       /**< SRP 算法起始频率 bin（含）。对应频率 = 3 × (48000/256) = 562.5 Hz（滤除低频噪声） */
#define SRP_FREQ_BIN_END             42u      /**< SRP 算法结束频率 bin（含）。对应频率 = 42 × (48000/256) = 7875 Hz（滤除高频混叠） */
#define SRP_PAIR_COUNT               40u      /**< 参与 GCC-PHAT 计算的麦克风对数量。16 路阵列理论最大对数 = 16×15/2=120对；选 40 对可覆盖阵列主要分布，CPU 开销较低。[改进] 可根据 tools/generate_srp_lut.py 中的 pair 选择算法优化 */

/* 粗搜索网格配置：在整个视角范围内均匀采样 COARSE_GRID_SIZE×COARSE_GRID_SIZE 个方向 */
#define COARSE_GRID_SIZE             9u       /**< 粗搜索网格边长（每轴方向数量）。总网格点数 = 9×9=81 点，角度分辨率 = 120°/8=15°。[改进] 增大到 13 可提高粗搜索精度，但计算量增加约 2.1× */
#define COARSE_ANGLE_MIN_DEG         (-60.0f) /**< 粗搜索 X/Y 轴最小角度（度）：-60° 对应左/下极限视角 */
#define COARSE_ANGLE_MAX_DEG         (60.0f)  /**< 粗搜索 X/Y 轴最大角度（度）：+60° 对应右/上极限视角。总视场角 = 120° */

/* 细搜索配置：在粗峰值附近细化定位，提高角度精度 */
#define FINE_TOP_K                   3u       /**< 从粗搜索结果中选取能量最高的 K 个点进入细搜索（同时限制多声源输出数量） */
#define FINE_GRID_SIZE               4u       /**< 细搜索子网格每轴点数：4×4=16 点，分布在粗点附近的 ±FINE_SPAN_DEG 范围内 */
#define FINE_SPAN_DEG                (10.0f)  /**< 细搜索半跨度（度）：粗点±10° 范围内细化。总细化范围 = 20°。[注意] 必须 > 粗搜索网格间距(15°)的一半(7.5°)，否则相邻粗点的细化区域有空隙 */

#define PHAT_EPSILON                 1.0e-10f /**< PHAT 白化分母下限（防止除零）。当互功率谱幅度趋近 0（静音）时保护数值稳定。[注意] 过大会削弱高频白化效果，过小可能在低 SNR 时产生数值爆炸 */

/* ============================================================================
 * 低置信度策略与结果质量门限
 * ============================================================================
 * "置信度"由以下两个量联合判断：
 *   1. energy：SRP 功率谱峰值的归一化能量 [0,1]，低→无明显声源
 *   2. quality：主峰与次峰的对比度 ratio，低→多个方向能量相近（模糊定位）
 *
 * 当以上任一量低于门限时，认为本帧定位结果"低置信度"，
 * 根据 SRP_LOWCONF_POLICY 选择不同的上报策略。
 * ============================================================================ */

/* 策略枚举值定义 */
#define SRP_LOWCONF_REPORT_NEW       0u       /**< 策略 0：低置信度时直接上报新结果（可能抖动但响应快） */
#define SRP_LOWCONF_HOLD_LAST        1u       /**< 策略 1：低置信度时保持上一次有效结果（稳定但反应滞后） */
#define SRP_LOWCONF_MIXED            2u       /**< 策略 2：先保持旧结果，连续 SRP_LOWCONF_MIXED_HOLD_FRAMES 帧后接受新结果（折衷方案） */
#define SRP_LOWCONF_POLICY           SRP_LOWCONF_REPORT_NEW /**< 当前策略：REPORT_NEW（直接上报）。[改进] 实际应用中 HOLD_LAST 通常效果更好，可以切换试验 */
#define SRP_LOWCONF_MIXED_HOLD_FRAMES 6u      /**< MIXED 策略下，低置信度帧连续多少帧后接受更新（6帧×5.3ms≈32ms 滞后）*/

/* 质量门限参数 */
#define SRP_CONTRAST_MIN_RATIO       0.03f    /**< 最小对比度门限：主峰能量 / 次峰能量 的最小比值。低于此值认为定位模糊（多点等能） */
#define SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG 5.0f /**< 计算次峰值时的邻域排除半径（度）：主峰 ±5° 内不作为次峰候选（避免主峰旁瓣被误识为次峰） */
#define SRP_TOPK_NMS_RADIUS          1u       /**< Top-K 结果的非极大值抑制半径（网格单位）。相邻 1 格内只保留最高能量的峰值 */

#define SRP_VALID_MIN_ENERGY         0.05f    /**< 认为定位结果有效的最小能量门限。低于 0.05 时认为环境过于均匀/静音，跳过 UI 更新 */
#define SRP_VALID_MIN_QUALITY        0.01f    /**< 认为定位结果有效的最小质量门限（= 对比度比值）。低于 0.01 时认为无法确定方向 */

#define SRP_ENABLE_ENERGY_SOFTCAP    0u       /**< 是否启用低置信度时的能量软上限：0=禁用，1=启用（将模糊结果的能量限制在 SRP_AMBIGUOUS_ENERGY_MAX 以下）*/
#define SRP_AMBIGUOUS_ENERGY_MAX     0.30f    /**< 启用软上限时，模糊结果允许保留的最大能量值（限制热力图亮度，避免低质量定位导致虚假亮点）*/

/* ============================================================================
 * 定位结果朝向重映射（适配不同安装方向）
 * ============================================================================
 * 背景：麦克风阵列安装方向可能与显示屏方向不一致（例如阵列竖装但屏幕横显示），
 * 此时需要对定位结果的 X/Y 角度进行重映射（旋转/镜像）以匹配视觉。
 *
 * 可用预设：
 *   0 = 自定义（直接设置 SWAP_XY / INVERT_X / INVERT_Y）
 *   1 = 顺时针旋转 90°（SWAP_XY=1, INVERT_Y=1）
 *   2 = 逆时针旋转 90°（SWAP_XY=1, INVERT_X=1）
 *   3 = 旋转 180°（INVERT_X=1, INVERT_Y=1）
 *   4 = 仅水平镜像（INVERT_X=1）
 *   5 = 仅垂直镜像（INVERT_Y=1）
 *
 * [改进] 当前仅支持编译期预设，未来可以通过 CLI 命令运行时修改映射参数，
 *        并保存到 Flash 以在断电后保持设置。
 * ============================================================================ */

/* 选择预设模式（0-5），或使用 0 自定义三个分量 */
#define SRP_OUTPUT_REMAP_PRESET      0u       /**< 朝向重映射预设索引：0=自定义，1=CW90°，2=CCW90°，3=180°，4=镜像X，5=镜像Y */

#if (SRP_OUTPUT_REMAP_PRESET == 0u)          /* 预设 0：自定义模式，直接设置各分量 */
#define SRP_OUTPUT_SWAP_XY           0u       /**< 是否交换 X 轴与 Y 轴角度：0=不交换，1=交换（用于将水平/垂直坐标互换） */
#define SRP_OUTPUT_INVERT_X          0u       /**< 是否翻转 X 轴角度：0=不翻转，1=取反（用于水平镜像修正） */
#define SRP_OUTPUT_INVERT_Y          0u       /**< 是否翻转 Y 轴角度：0=不翻转，1=取反（用于垂直镜像修正） */
#elif (SRP_OUTPUT_REMAP_PRESET == 1u)        /* 预设 1：顺时针旋转 90° 等效变换 */
#define SRP_OUTPUT_SWAP_XY           1u       /**< CW90°：交换 XY（旋转的第一步） */
#define SRP_OUTPUT_INVERT_X          0u       /**< CW90°：X 不翻转 */
#define SRP_OUTPUT_INVERT_Y          1u       /**< CW90°：翻转新 Y（旋转的第二步，完成 90° 变换） */
#elif (SRP_OUTPUT_REMAP_PRESET == 2u)        /* 预设 2：逆时针旋转 90° 等效变换 */
#define SRP_OUTPUT_SWAP_XY           1u       /**< CCW90°：交换 XY */
#define SRP_OUTPUT_INVERT_X          1u       /**< CCW90°：翻转新 X */
#define SRP_OUTPUT_INVERT_Y          0u       /**< CCW90°：Y 不翻转 */
#elif (SRP_OUTPUT_REMAP_PRESET == 3u)        /* 预设 3：旋转 180°（双轴翻转） */
#define SRP_OUTPUT_SWAP_XY           0u       /**< 180°：不交换 XY */
#define SRP_OUTPUT_INVERT_X          1u       /**< 180°：翻转 X */
#define SRP_OUTPUT_INVERT_Y          1u       /**< 180°：翻转 Y */
#elif (SRP_OUTPUT_REMAP_PRESET == 4u)        /* 预设 4：仅水平镜像 */
#define SRP_OUTPUT_SWAP_XY           0u       /**< 镜像X：不交换 XY */
#define SRP_OUTPUT_INVERT_X          1u       /**< 镜像X：翻转 X 轴 */
#define SRP_OUTPUT_INVERT_Y          0u       /**< 镜像X：Y 不翻转 */
#elif (SRP_OUTPUT_REMAP_PRESET == 5u)        /* 预设 5：仅垂直镜像 */
#define SRP_OUTPUT_SWAP_XY           0u       /**< 镜像Y：不交换 XY */
#define SRP_OUTPUT_INVERT_X          0u       /**< 镜像Y：X 不翻转 */
#define SRP_OUTPUT_INVERT_Y          1u       /**< 镜像Y：翻转 Y 轴 */
#else                                        /* 非法预设值 → 编译期报错，及早发现配置错误 */
#error "Invalid SRP_OUTPUT_REMAP_PRESET"     /* [注意] 必须在 0-5 范围内，超出此范围表示配置错误 */
#endif                                       /* 结束 SRP_OUTPUT_REMAP_PRESET 条件编译 */

/* ============================================================================
 * FreeRTOS 任务优先级、堆栈与性能统计配置
 * ============================================================================
 * STM32H743 + FreeRTOS 的优先级约定（值越大优先级越高）：
 *   - 优先级 1（osPriorityLow）：存储任务（Storage_Task）
 *   - 优先级 2（osPriorityBelowNormal）：存储任务实际使用
 *   - 优先级 4（osPriorityNormal）：音频任务 + UI 任务（同级竞争）
 *
 * [注意] 音频任务和 UI 任务同级（均为 4），FreeRTOS 时间片轮转调度（configUSE_TIME_SLICING=1）。
 *        若音频任务 CPU 占用过高，UI 任务可能得不到足够的时间片，导致界面卡顿。
 *        建议音频任务 CPU 占用 < 50%（通过 App_Perf 模块监测）。
 * ============================================================================ */

#define APP_AUDIO_TASK_PRIO          4u       /**< 音频处理任务优先级（= osPriorityNormal）。与 UI 同级，时间片轮转。[改进] 若 SRP 算法耗时过长，可临时降为 3，让 UI 先得到时间片 */
#define APP_UI_TASK_PRIO             4u       /**< UI 显示任务优先级（= osPriorityNormal）。与音频任务同级 */
#define APP_AUDIO_TASK_STACK_WORDS   2304u    /**< 音频任务堆栈深度（字=4字节）：2304×4=9216 字节。含 FFT 临时变量、SRP 局部栈等。[注意] 减小此值可能导致堆栈溢出，请用 uxTaskGetStackHighWaterMark 验证最小余量 */
#define APP_UI_TASK_STACK_WORDS      2048u    /**< UI 显示任务堆栈深度（字）：2048×4=8192 字节。含 LVGL 渲染调用栈 */

#define APP_PERF_DEFAULT_ENABLE      0u       /**< 上电时性能统计默认状态：0=关闭（节省 CPU），1=默认开启（调试时使用）*/
#define PERF_RING_SAMPLES            64u      /**< 性能统计环形缓冲样本数：保存最近 64 帧的各阶段 CPU 周期数，用于 CLI 输出 */
#define PERF_RATE_PERIOD_MS          1000u    /**< 性能速率统计打印周期（毫秒）：每 1 秒通过 CLI 输出一次帧率和 CPU 利用率 */

#define UI_RETRY_INIT_MS             1000u    /**< 显示子系统初始化失败后的重试间隔（毫秒）。用于 SDRAM 初始化不稳定等情况 */
#define UI_DEBUG_LOG                 0u       /**< UI 模块内部调试日志：1=开启（通过 UART 输出 UI 状态变化），0=关闭 */
#define UI_FPS_MIN                   5u       /**< UI 帧率下限（FPS）：低于此值认为系统过载，可通过 CLI 诊断 */
#define UI_FPS_MAX                   30u      /**< UI 帧率上限（FPS）：限制最高刷新率，避免无意义的 CPU 占用 */
#define UI_FPS_DEFAULT               20u      /**< UI 默认帧率（FPS）：20 FPS = 50ms/帧，满足声学相机的视觉流畅度要求 */

#define AUDIO_ALGO_DECIM_MIN         1u       /**< 算法抽帧比最小值：1 = 每帧都执行算法（无抽帧），最高精度但 CPU 占用最大 */
#define AUDIO_ALGO_DECIM_MAX         8u       /**< 算法抽帧比最大值：8 = 每 8 帧执行一次，CPU 占用最低但更新率仅约 23 Hz */
#define AUDIO_ALGO_DECIM_DEFAULT     1u       /**< 算法抽帧默认值：1（不抽帧）。[改进] 若 STM32H743 性能足够，保持 1；若 CPU 占用 >80%，可通过 CLI 设为 2 */

/* ============================================================================
 * UART CLI（命令行接口）配置
 * ============================================================================
 * CLI 通过 UART1（PA9/PA10，波特率 921600）提供实时调试能力。
 * 支持命令：cfg fps/algodecim/mode、perf、status、help 等。
 * [注意] CLI 使用环形缓冲区接收，由 UI 任务轮询处理（非 DMA），延迟约 20ms（1帧）。
 * ============================================================================ */

#define UI_CLI_ENABLE                1u       /**< UART CLI 总开关：1=启用（默认），0=禁用（节省 ~1KB RAM）*/
#define UI_CLI_LINE_MAX              96u      /**< 单条 CLI 命令最大字节长度（不含换行符）。[注意] 必须 < UI_CLI_RX_RING_SIZE，否则编译期 #error */
#define UI_CLI_RX_DRAIN_MAX          256u     /**< 单次轮询最多处理的接收字节数，防止 CLI 处理占用 UI 任务过多时间片 */
#define UI_CLI_RX_RING_SIZE          1024u    /**< UART 接收环形缓冲区大小（字节）。[注意] 若 CLI 命令序列较长，可适当增大，但会占用 D1 SRAM */
#define UI_CLI_ALIVE_WINDOW_MS       2000u    /**< 判定 CLI"活跃"状态的静默窗口（毫秒）：2 秒内接收过有效字节则 g_ui_cli_rx_alive=1，否则为 0（用于 status 命令的诊断输出）*/

/* ============================================================================
 * 显示布局与分辨率配置
 * ============================================================================
 * 屏幕布局（横向，7 寸 1024×600）：
 * ┌────────────────────────────┬──────────┐
 * │  摄像头+热力图主视图（768×544）  │ UI 面板  │
 * │  APP_DISPLAY_CAMERA_VIEW_W │ 256×600  │
 * │  × APP_DISPLAY_CAMERA_VIEW_H│         │
 * └────────────────────────────┴──────────┘
 *              ← APP_DISPLAY_TARGET_SCREEN_W=1024 →
 *
 * 内部处理场（Field）用于 SRP 功率谱的中间缓冲，尺寸越大画质越好但 RAM 占用越多。
 * ============================================================================ */

#define APP_DISPLAY_DEFAULT_MODE     0u       /**< 上电默认显示模式：0=FAST（响应快），1=BALANCED（折中），2=CLEAN（画质优先）*/

#define APP_DISPLAY_TARGET_SCREEN_W   1024u   /**< 目标屏幕分辨率宽度（像素）：对应 7 寸 1024×600 RGB 屏 */
#define APP_DISPLAY_TARGET_SCREEN_H   600u    /**< 目标屏幕分辨率高度（像素）：600 行。[注意] 修改后需同步调整 LTDC 参数和 BMP 截图尺寸 */
#define APP_DISPLAY_TEXT_WIDTH_PX    APP_DISPLAY_UI_PANEL_W  /**< 右侧 UI 面板宽度的别名（兼容旧版布局代码，避免硬编码 256） */
#define APP_DISPLAY_CAMERA_VIEW_W     (APP_DISPLAY_TARGET_SCREEN_W - APP_DISPLAY_UI_PANEL_W)  /**< 摄像头+热力图主视图宽度 = 1024 - 256 = 768 像素 */
#define APP_DISPLAY_CAMERA_VIEW_H     544u    /**< 主视图高度（像素）：600 - 56（预留给顶部/底部状态栏）= 544。[改进] 此值应由常量计算而非硬编码 */
#define APP_DISPLAY_HEAT_VIEW_W       APP_DISPLAY_CAMERA_VIEW_W   /**< 热力图叠加区域宽度（= 主视图宽度，完整覆盖摄像头图像）*/
#define APP_DISPLAY_HEAT_VIEW_H       APP_DISPLAY_CAMERA_VIEW_H   /**< 热力图叠加区域高度（= 主视图高度）*/
#define APP_DISPLAY_UI_PANEL_W        256u    /**< 右侧 UI 状态面板宽度（像素）：256 像素，用于显示定位角度、FPS、状态等文本信息 */
#define APP_DISPLAY_MAX_LINE_PIXELS  1280u    /**< 单行渲染临时缓冲区允许的最大像素数。[改进] 1280 > 屏幕宽度 1024，有 256 像素余量，但过大浪费栈空间 */
#define APP_DISPLAY_BLIT_ROWS_MAX    8u       /**< 单次 DMA2D 块渲染最大行数：增大可提高渲染效率但需要更大临时缓冲，减小可降低延迟 */

/* 内部处理场（Field）尺寸 vs RAM 折衷 */
#define APP_DISPLAY_RAM_SAVE_LEVEL   0u       /**< RAM 节省等级控制内部处理场尺寸：0=画质优先（96×96），1=折中（72×72），2=内存优先（56×56）*/

#if (APP_DISPLAY_RAM_SAVE_LEVEL == 0u)        /* 等级 0：画质最佳，内部处理场 96×96 */
#define APP_DISPLAY_FIELD_W          96u      /**< 内部 SRP 功率谱处理场宽度（像素）：96×96 = 9216 个 float = 36 KB。占用 AXI SRAM */
#define APP_DISPLAY_FIELD_H          96u      /**< 内部处理场高度，与宽度相同（正方形网格） */
#elif (APP_DISPLAY_RAM_SAVE_LEVEL == 1u)      /* 等级 1：折中，内部处理场 72×72 */
#define APP_DISPLAY_FIELD_W          72u      /**< 折中模式下的处理场宽度（72×72 = 5184 float ≈ 20 KB）*/
#define APP_DISPLAY_FIELD_H          72u      /**< 折中模式下的处理场高度 */
#else                                         /* 等级 2：内存优先，内部处理场 56×56 */
#define APP_DISPLAY_FIELD_W          56u      /**< 内存优先模式下的处理场宽度（56×56 = 3136 float ≈ 12 KB，图像较粗糙）*/
#define APP_DISPLAY_FIELD_H          56u      /**< 内存优先模式下的处理场高度 */
#endif                                        /* 结束 RAM_SAVE_LEVEL 条件编译 */

#define APP_DISPLAY_HEAT_LUT_SIZE    256u     /**< 热力图颜色查找表（LUT）条目数：256 个 RGB565 颜色，覆盖 0~255 的归一化能量等级 */
#define APP_DISPLAY_NORM_RATIO_LUT_SIZE 1024u /**< 快速归一化比例查找表条目数：预计算 1024 个比例值（避免运行时除法），用于热力图归一化加速。[改进] 1024 意味着归一化精度约 0.1%，对视觉足够，但若改为 512 可减半 RAM */
#define APP_DISPLAY_DMA2D_TIMEOUT    0x1FFFFFu /**< DMA2D 操作等待超时计数（轮询次数上限，约 200ms @480MHz）。超时表示 DMA2D 卡死，此时重置并以软件后备路径继续渲染 */

/* ============================================================================
 * 显示画质、诊断与动态范围配置
 * ============================================================================ */

#define APP_DISPLAY_SMOOTH_ENABLE          1u       /**< 是否在编译时包含高斯平滑代码路径：0=完全移除平滑代码（节省 Flash），1=保留（运行时由模式控制是否启用）*/
#define APP_DISPLAY_SMOOTH_RADIUS          2u       /**< 高斯平滑核半径（像素）：值越大平滑越强但边缘越模糊，2=半径 2 像素、核大小 5×5 */
#define APP_DISPLAY_SMOOTH_SIGMA           1.05f    /**< 高斯平滑 sigma（标准差）：控制高斯核的宽度，值越小越锐利 [改进] 建议 sigma ≈ radius/2 */
#define APP_DISPLAY_SMOOTH_PASSES          1u       /**< 高斯平滑迭代次数：1 次轻微平滑，2+ 次进一步模糊（多次叠加等效更大 sigma） */

#define APP_DISPLAY_FINE_FUSION_ENABLE     1u       /**< 是否将细搜索结果叠加到粗搜索热力图：1=启用（提升分辨率），0=禁用（仅粗搜索，性能更高）*/
#define APP_DISPLAY_FINE_KERNEL_RADIUS     4u       /**< 细搜索结果在热力图上的扩散核半径（像素）：值越大扩散越宽，声源标注更醒目 */
#define APP_DISPLAY_FINE_KERNEL_SIGMA      2.0f     /**< 细搜索扩散核 sigma：控制扩散的衰减速率，应与 FINE_KERNEL_RADIUS 匹配 */
#define APP_DISPLAY_FINE_GAIN              0.65f    /**< 细搜索叠加增益 [0,1]：控制细搜索相对于粗搜索的贡献比例，0.65=细搜索贡献约 2/3 */
#define APP_DISPLAY_FINE_MIN_RATIO         0.10f    /**< 细搜索结果的最小能量比门限：低于粗峰值 10% 的细结果不参与叠加（过滤噪声峰值）*/

/* 动态范围与 EMA 平滑配置 */
#define APP_DISPLAY_DYNAMIC_DB_FLOOR       (-45.0f) /**< 热力图动态范围底噪 dB 值：低于 -45dB 的归一化能量映射到颜色最暗端（=蓝色/无色）*/
#define APP_DISPLAY_DYNAMIC_MIN_PEAK       (1.0e-9f) /**< 动态归一化最小峰值保护：防止峰值为 0 时除零，设为接近 0 的极小值 */
#define APP_DISPLAY_EMA_ATTACK             0.65f    /**< 峰值 EMA 攻击系数 [0,1]：值越接近 1，峰值上升越快（响应快）。EMA: peak = alpha*new + (1-alpha)*old */
#define APP_DISPLAY_EMA_DECAY              0.08f    /**< 峰值 EMA 衰减系数 [0,1]：值越小，峰值下降越慢（显示有"余辉"效果）[改进] 可按 CLI 模式动态切换 */
#define APP_DISPLAY_EMA_MIN_PEAK           (1.0e-7f) /**< EMA 峰值保持下限：峰值不低于此值，避免长时间静音后第一帧出现闪烁 */
#define APP_DISPLAY_DYNAMIC_GAMMA          1.10f    /**< 热力图 gamma 校正值：>1.0 压暗暗区（减少背景噪点），<1.0 提亮暗区。1.10 轻微压暗 */
#define APP_DISPLAY_NOISE_GATE_RATIO       0.08f    /**< 噪声门限比例：归一化能量低于 max×0.08 的区域强制置零（消除背景噪声格点）*/
#define APP_DISPLAY_NOISE_ADAPT_GAIN       2.00f    /**< 自适应背景噪声增益：从噪声基底估计模块读取底噪后乘以此增益作为动态门限 */

/* UI 诊断与渲染辅助开关 */
#define APP_DISPLAY_DIAG_OVERLAY           1u       /**< 是否在屏幕右下角显示诊断信息叠加层（FPS、帧序号、SAI 状态等）：1=显示，0=隐藏 */
#define APP_DISPLAY_DRAW_COARSE_GRID       0u       /**< 是否绘制粗搜索网格辅助线（调试用，辅助验证网格方向和角度映射）：1=绘制，0=不绘制 */
#define APP_DISPLAY_DEBUG_LOG              0u       /**< 显示模块内部调试日志开关（通过 UART 输出渲染统计）：生产固件设为 0 */
#define APP_DISPLAY_IDLE_TEST_PATTERN      1u       /**< 无有效峰值时是否显示彩虹测试图案：1=显示（便于确认热力图渲染链路正常），0=显示纯蓝（最暗）*/
#define APP_DISPLAY_TEXT_REFRESH_DIV       2u       /**< 右侧文本面板刷新分频：设为 2 则每 2 帧刷新一次文字（降低文字渲染开销）*/
#define APP_DISPLAY_BILINEAR_SAMPLING      1u       /**< 默认是否使用双线性插值（热力图缩放）：1=双线性（平滑），0=最近邻（块状但更快）*/

/* 十字准星和峰值标记尺寸 */
#define APP_DISPLAY_CROSSHAIR_HALF_PX      6u       /**< 十字准星半径（像素）：实际十字线宽度 = 2×6+1 = 13 像素 */
#define APP_DISPLAY_PEAK_MARKER_RADIUS_PX  4u       /**< 峰值矩形标注框半边长（像素）：实际框大小 = 9×9 像素 */
#define APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES 10u      /**< SAI"活跃"指示灯保持帧数：连续 10 帧接收到音频数据才熄灭活跃指示 */

/* 频谱面板配置 */
#define APP_SPECTRUM_INFO_ENABLE           0u       /**< 是否在频谱面板显示频带范围文字标注：0=只显示图形（默认），1=显示坐标文字 */
#define APP_SPECTRUM_FREQ_SCALE_MODE       1u       /**< 频谱 X 轴刻度模式：0=线性（均匀）频率轴，1=对数频率轴（与人耳感知一致，低频更醒目）*/
#define APP_SPECTRUM_LOW_FREQ_AT_BOTTOM    1u       /**< 频谱显示方向：1=低频在下/左（符合传统频谱仪习惯），0=低频在上/右 */
#define APP_SPECTRUM_DISPLAY_MIN_HZ        ((float)SAMPLING_RATE / (float)FRAME_LEN)  /**< 频谱显示起始频率（Hz）= 采样率/帧长 = 48000/256 = 187.5 Hz（跳过 DC bin=0）*/
#define APP_SPECTRUM_DB_FLOOR              (-48.0f) /**< 频谱条形图底噪 dB 值：低于 -48 dBFS 的 bin 显示为 0 高度 */
#define APP_SPECTRUM_BAR_ATTACK            0.58f    /**< 频谱条上升 EMA 系数：控制条形上升速度，0.58 为中等响应速度 */
#define APP_SPECTRUM_BAR_DECAY             0.36f    /**< 频谱条下降 EMA 系数：控制条形下降速度，0.36 产生较快下落（无拖影）*/
#define APP_SPECTRUM_REF_ATTACK            0.85f    /**< "参考峰值"线上升 EMA 系数：0.85 极快上升，确保峰值线立即跟随最高点 */
#define APP_SPECTRUM_REF_DECAY             0.04f    /**< "参考峰值"线下降 EMA 系数：0.04 极慢下降，形成保持峰值线（类似音量表峰值保持）*/

/* ============================================================================
 * 显示模式预设参数
 * ============================================================================
 * 三种预设对应不同的使用场景：
 *   FAST    → 工厂快速诊断，响应快，无复杂处理
 *   BALANCED→ 日常演示，性能与画质折中
 *   CLEAN   → 展示/录制，画质优先，CPU 占用较高
 *
 * 每种模式各自定义 EMA 系数、底噪、gamma、平滑等参数，
 * 通过 CLI `cfg mode <N>` 在运行时切换，无需重新编译。
 * ============================================================================ */

/* FAST 模式参数：最低延迟，最简处理，适合快速诊断 */
#define APP_DISPLAY_MODE_FAST_EMA_ATTACK           0.70f /**< [FAST] 峰值 EMA 攻击系数（快速响应）：0.70 比默认 0.65 更快上升 */
#define APP_DISPLAY_MODE_FAST_EMA_DECAY            0.15f /**< [FAST] 峰值 EMA 衰减系数（快速下落）：0.15 > 默认 0.08，热力图消退更快 */
#define APP_DISPLAY_MODE_FAST_DB_FLOOR             (-34.0f) /**< [FAST] 动态底噪 dB：-34dB（浅，只显示强声源，弱化背景细节）*/
#define APP_DISPLAY_MODE_FAST_FINE_GAIN            0.25f /**< [FAST] 细搜索叠加增益：0.25（低，细搜索贡献小，运算结果影响小）*/
#define APP_DISPLAY_MODE_FAST_GAMMA                1.00f /**< [FAST] Gamma：1.0（线性，无额外伽马校正）*/
#define APP_DISPLAY_MODE_FAST_NOISE_GATE_RATIO     0.12f /**< [FAST] 噪声门限比：0.12（较高，更激进地清除背景噪点）*/
#define APP_DISPLAY_MODE_FAST_NOISE_ADAPT_GAIN     1.50f /**< [FAST] 自适应噪声增益：1.50（低于默认 2.0，噪声门限较低）*/
#define APP_DISPLAY_MODE_FAST_SMOOTH_PASSES        0u    /**< [FAST] 高斯平滑次数：0（跳过平滑，节省 CPU）*/
#define APP_DISPLAY_MODE_FAST_FINE_FUSION_ENABLE   0u    /**< [FAST] 禁用细搜索叠加（节省渲染时间）*/
#define APP_DISPLAY_MODE_FAST_DRAW_COARSE_GRID     0u    /**< [FAST] 不绘制粗搜索网格辅助线 */
#define APP_DISPLAY_MODE_FAST_INTERP_BILINEAR      0u    /**< [FAST] 使用最近邻插值（比双线性快约 3×）*/
#define APP_DISPLAY_MODE_FAST_NORM_FULL            0u    /**< [FAST] 使用快速归一化（单遍扫描，精度略低）*/
#define APP_DISPLAY_MODE_FAST_TEXT_REFRESH_DIV     4u    /**< [FAST] 文本刷新分频 4（每 4 帧刷一次文字，降低文本渲染开销）*/
#define APP_DISPLAY_MODE_FAST_BLIT_ROWS            APP_DISPLAY_BLIT_ROWS_MAX /**< [FAST] 每次渲染最大行数（使用全局上限 8 行）*/

#define APP_DISPLAY_MODE_BALANCED_NORM_FULL        0u    /**< [BALANCED] 归一化模式：0=快速（与 FAST 相同），[改进] 可改为 1 提升准确性 */

#define APP_DISPLAY_MODE_CLEAN_EMA_ATTACK          0.58f /**< CLEAN 模式的 EMA 攻击系数。 */
#define APP_DISPLAY_MODE_CLEAN_EMA_DECAY           0.06f /**< CLEAN 模式的 EMA 衰减系数。 */
#define APP_DISPLAY_MODE_CLEAN_DB_FLOOR            (-52.0f) /**< CLEAN 模式的动态范围底噪。 */
#define APP_DISPLAY_MODE_CLEAN_FINE_GAIN           0.50f /**< CLEAN 模式的细搜索融合增益。 */
#define APP_DISPLAY_MODE_CLEAN_GAMMA               1.35f /**< CLEAN 模式的 gamma。 */
#define APP_DISPLAY_MODE_CLEAN_NOISE_GATE_RATIO    0.10f /**< CLEAN 模式的噪声门限比例。 */
#define APP_DISPLAY_MODE_CLEAN_NOISE_ADAPT_GAIN    2.80f /**< [CLEAN] 自适应噪声增益：2.80（更强的背景减除，有效清除漫反射噪声）*/
#define APP_DISPLAY_MODE_CLEAN_SMOOTH_PASSES       2u    /**< [CLEAN] 高斯平滑次数：2（比 FAST 的 0 多，热力图轮廓更圆润）*/
#define APP_DISPLAY_MODE_CLEAN_FINE_FUSION_ENABLE  1u    /**< [CLEAN] 启用细搜索叠加（提升局部角度分辨率）*/
#define APP_DISPLAY_MODE_CLEAN_DRAW_COARSE_GRID    0u    /**< [CLEAN] 不绘制粗网格辅助线（展示时无辅助线干扰）*/
#define APP_DISPLAY_MODE_CLEAN_INTERP_BILINEAR     1u    /**< [CLEAN] 使用双线性插值（热力图放大更平滑，画质更好）*/
#define APP_DISPLAY_MODE_CLEAN_NORM_FULL           1u    /**< [CLEAN] 使用全精度归一化（两遍扫描找全局最大值，精度最高）*/
#define APP_DISPLAY_MODE_CLEAN_TEXT_REFRESH_DIV    2u    /**< [CLEAN] 文本刷新分频：2（每 2 帧刷新文字，状态信息及时更新）*/
#define APP_DISPLAY_MODE_CLEAN_BLIT_ROWS           6u    /**< [CLEAN] 每次块渲染行数：6（适合双线性插值的临时缓冲，低于最大 8）*/

/* ============================================================================
 * 编译期一致性保护（静态断言 —— 优于运行时检查）
 * ============================================================================
 * 以下 #if/#error 块在预处理阶段完成参数合理性验证，
 * 若参数配置违反约束，编译器立即报错，避免逻辑错误被带入运行时。
 *
 * [注意] 所有 #error 信息均为英文，原因是 armcc 5 编译器在 #error 诊断
 *        中不保证正确渲染 UTF-8 中文字符，保持英文以确保输出可读。
 * ============================================================================ */

/* 检查 SRP 频率范围的合法性（结束 bin 必须不小于起始 bin） */
#if (SRP_FREQ_BIN_END < SRP_FREQ_BIN_START)
#error "SRP_FREQ_BIN_END must be >= SRP_FREQ_BIN_START"  /* 频率范围倒置会导致 SRP 不处理任何频率 bin */
#endif

/* 粗搜索至少需要 2×2=4 个格点方能确定方向，1 个格点无意义 */
#if (COARSE_GRID_SIZE < 2u)
#error "COARSE_GRID_SIZE must be >= 2"                   /* 1 个格点无法构成方位搜索空间 */
#endif

/* 细搜索至少需要 1×1 个格点（等价于无细化，但不触发越界） */
#if (FINE_GRID_SIZE < 1u)
#error "FINE_GRID_SIZE must be >= 1"                     /* 0 个格点会导致细搜索循环除零或空循环 */
#endif

/* UI 默认帧率必须在 [MIN, MAX] 范围内 */
#if (UI_FPS_MIN > UI_FPS_DEFAULT) || (UI_FPS_DEFAULT > UI_FPS_MAX)
#error "UI_FPS_DEFAULT must be in [UI_FPS_MIN, UI_FPS_MAX]"  /* 防止初始帧率超出范围导致 vTaskDelay 参数异常 */
#endif

/* 算法抽帧默认值必须在 [MIN, MAX] 范围内 */
#if (AUDIO_ALGO_DECIM_MIN > AUDIO_ALGO_DECIM_DEFAULT) || \          /* 检查下边界：DECIM_DEFAULT >= MIN */
    (AUDIO_ALGO_DECIM_DEFAULT > AUDIO_ALGO_DECIM_MAX)               /* 检查上边界：DECIM_DEFAULT <= MAX */
#error "AUDIO_ALGO_DECIM_DEFAULT must be in [AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX]"
#endif

/* CLI 行缓冲必须小于环形缓冲区，否则单行命令就会填满环形缓冲区 */
#if (UI_CLI_LINE_MAX >= UI_CLI_RX_RING_SIZE)
#error "UI_CLI_LINE_MAX must be smaller than UI_CLI_RX_RING_SIZE"
#endif

/* 显示模式只有 0/1/2 三个合法值 */
#if (APP_DISPLAY_DEFAULT_MODE > 2u)
#error "APP_DISPLAY_DEFAULT_MODE must be 0, 1, or 2"
#endif

/* 内部处理场不能过小，否则插值计算无意义（< 16 像素边长 = 太粗糙）*/
#if (APP_DISPLAY_FIELD_W < 16u) || (APP_DISPLAY_FIELD_H < 16u)
#error "APP_DISPLAY_FIELD_W/H too small"
#endif

/* 高斯平滑半径超过 4 像素时，编译期生成的核系数数组会超出预分配大小 */
#if (APP_DISPLAY_SMOOTH_RADIUS > 4u)
#error "APP_DISPLAY_SMOOTH_RADIUS too large"             /* 核大小 = (2*radius+1)^2，radius=4 → 9×9=81 float */
#endif

/* 细搜索扩散核半径超过 8 像素时，扩散数组会越界 */
#if (APP_DISPLAY_FINE_KERNEL_RADIUS > 8u)
#error "APP_DISPLAY_FINE_KERNEL_RADIUS too large"
#endif

/* 单次块渲染行数必须在 [1, 16] 内（超出 16 会超出临时行缓冲区）*/
#if (APP_DISPLAY_BLIT_ROWS_MAX < 1u) || (APP_DISPLAY_BLIT_ROWS_MAX > 16u)
#error "APP_DISPLAY_BLIT_ROWS_MAX must be in [1,16]"
#endif

/* 频谱刻度模式只有 0 和 1 两个合法值（0=线性，1=对数）*/
#if (APP_SPECTRUM_FREQ_SCALE_MODE > 1u)
#error "APP_SPECTRUM_FREQ_SCALE_MODE must be 0 or 1"
#endif

/* 频谱布尔选项只允许 0 或 1 */
#if (APP_SPECTRUM_INFO_ENABLE > 1u) || (APP_SPECTRUM_LOW_FREQ_AT_BOTTOM > 1u)
#error "APP_SPECTRUM_* boolean options must be 0 or 1"
#endif

/* FAST 模式块渲染行数必须在全局上限以内 */
#if (APP_DISPLAY_MODE_FAST_BLIT_ROWS < 1u) || (APP_DISPLAY_MODE_FAST_BLIT_ROWS > APP_DISPLAY_BLIT_ROWS_MAX)
#error "APP_DISPLAY_MODE_FAST_BLIT_ROWS out of range"
#endif

/* CLEAN 模式块渲染行数必须在全局上限以内 */
#if (APP_DISPLAY_MODE_CLEAN_BLIT_ROWS < 1u) || (APP_DISPLAY_MODE_CLEAN_BLIT_ROWS > APP_DISPLAY_BLIT_ROWS_MAX)
#error "APP_DISPLAY_MODE_CLEAN_BLIT_ROWS out of range"
#endif

/* 摄像头预览分辨率需 >= 4 像素且为 4 的倍数（STM32 DCMI/DMA 对齐要求）*/
#if (APP_CAMERA_PREVIEW_W < 4u) || (APP_CAMERA_PREVIEW_H < 4u) || \     /* 最小尺寸检查 */
    ((APP_CAMERA_PREVIEW_W % 4u) != 0u) || ((APP_CAMERA_PREVIEW_H % 4u) != 0u)  /* 4 字节对齐检查 */
#error "APP_CAMERA_PREVIEW_W/H must be >= 4 and aligned to 4 pixels"
#endif

/* Alpha 最大值不能超过 255（uint8_t 范围），否则运行时截断会产生意外透明度 */
#if (APP_CAMERA_OVERLAY_ALPHA_MAX > 255u)
#error "APP_CAMERA_OVERLAY_ALPHA_MAX must be <= 255"
#endif

/* 叠加模式必须是已实现的模式之一（当前仅支持 VERIFY_ALPHA）*/
#if (APP_CAMERA_OVERLAY_MODE != APP_CAMERA_OVERLAY_MODE_VERIFY_ALPHA)
#error "Unsupported APP_CAMERA_OVERLAY_MODE"             /* [改进] 扩展此检查以支持更多叠加模式 */
#endif

#endif /* APP_USER_CONFIG_H */  /* 头文件防重复包含保护（结束）*/
