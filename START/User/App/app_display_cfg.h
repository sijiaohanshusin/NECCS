#ifndef APP_DISPLAY_CFG_H
#define APP_DISPLAY_CFG_H

/*
 * 编译期显示配置。
 *
 * 这个文件只放“可视化层”的静态调参项，不应该混入算法本身的搜索逻辑。
 * 调参时建议按以下顺序理解：
 * 1. 先看布局参数，决定屏幕上热力图和文字面板怎么摆放。
 * 2. 再看场分辨率和内存节省等级，决定中间缓冲大小与画质上限。
 * 3. 再看平滑、细网格融合、动态归一化，决定热力图观感。
 * 4. 最后看诊断、插值、标记大小等“表现层细节”。
 */

/* 布局参数：
 * - `MARGIN` 控制四周留白
 * - `TEXT_WIDTH` 控制右侧文本栏宽度
 * - `MAX_LINE_PIXELS` 限制单行/单块渲染允许的最大宽度
 * - `BLIT_ROWS_MAX` 限制一次块渲染的最大行数，直接影响临时缓冲大小 */
#define APP_DISPLAY_MARGIN_PX              8u
#define APP_DISPLAY_TEXT_WIDTH_PX          180u
#define APP_DISPLAY_MAX_LINE_PIXELS        1280u
#define APP_DISPLAY_BLIT_ROWS_MAX          8u

/* RAM 压力调节旋钮：
 * 0 = 画质优先（默认）
 * 1 = 折中
 * 2 = 内存优先
 *
 * 这里本质上是在缩小“显示中间场”的尺寸。尺寸越小：
 * - 内存占用越低
 * - 平滑和插值代价越小
 * - 但热力图细节越容易丢失
 *
 * 如果后续再次出现链接空间不足，优先提高这个等级。 */
 */
#define APP_DISPLAY_RAM_SAVE_LEVEL         0u

/* 低分辨率中间标量场：
 * 算法输出不会直接按 LCD 分辨率绘制，而是先映射到这个中间场上，再做
 * 平滑、融合、归一化和放大。这样可以显著降低运算量。
 *
 * 尺寸选择原则：
 * - 太小：热力图会显得模糊、块状、定位细节不足
 * - 太大：RAM 占用和逐帧处理成本上升 */
#if (APP_DISPLAY_RAM_SAVE_LEVEL == 0u)
#define APP_DISPLAY_FIELD_W                96u
#define APP_DISPLAY_FIELD_H                96u
#elif (APP_DISPLAY_RAM_SAVE_LEVEL == 1u)
#define APP_DISPLAY_FIELD_W                72u
#define APP_DISPLAY_FIELD_H                72u
#else
#define APP_DISPLAY_FIELD_W                56u
#define APP_DISPLAY_FIELD_H                56u
#endif

/* 粗场平滑参数（近似高斯、可分离卷积）：
 * - `ENABLE` 控制是否编译进平滑逻辑
 * - `RADIUS` 是一维卷积半径
 * - `SIGMA` 决定核函数扩散程度
 * - `PASSES` 是每帧重复平滑的次数
 *
 * 实际效果：
 * - 开启后热力图边缘更柔和，孤立噪点更少
 * - 但会牺牲尖锐峰值与局部细节 */
#define APP_DISPLAY_SMOOTH_ENABLE          1u
#define APP_DISPLAY_SMOOTH_RADIUS          2u
#define APP_DISPLAY_SMOOTH_SIGMA           1.05f
#define APP_DISPLAY_SMOOTH_PASSES          1u

/* 细搜索结果融合到低分辨率场：
 * 粗网格负责整体结构，细网格负责补充峰值附近更准确的能量分布。
 * 这里通过一个二维核把细网格能量“撒”回中间场中。
 *
 * 参数含义：
 * - `ENABLE` 是否允许细融合参与编译
 * - `KERNEL_RADIUS` / `SIGMA` 控制扩散核的大小和形状
 * - `FINE_GAIN` 控制融合增益
 * - `FINE_MIN_RATIO` 过滤过弱的细网格点，避免把噪声也融合进去 */
#define APP_DISPLAY_FINE_FUSION_ENABLE     1u
#define APP_DISPLAY_FINE_KERNEL_RADIUS     4u
#define APP_DISPLAY_FINE_KERNEL_SIGMA      2.0f
#define APP_DISPLAY_FINE_GAIN              0.65f
#define APP_DISPLAY_FINE_MIN_RATIO         0.10f

/* 动态归一化：
 * SRP 功率范围变化很大，直接线性映射通常不是好选择，因此这里采用：
 * - EMA 跟踪参考峰值
 * - dB 下限截断
 * - gamma 调整观感曲线
 * - 噪声门限与背景自适应估计
 *
 * 这些参数共同决定“黑场有多黑、亮点有多亮、画面是否跳动”。 */
#define APP_DISPLAY_DYNAMIC_DB_FLOOR       (-45.0f)
#define APP_DISPLAY_EMA_ATTACK             0.65f
#define APP_DISPLAY_EMA_DECAY              0.08f
#define APP_DISPLAY_EMA_MIN_PEAK           (1.0e-7f)
#define APP_DISPLAY_DYNAMIC_GAMMA          1.10f
#define APP_DISPLAY_NOISE_GATE_RATIO       0.08f
#define APP_DISPLAY_NOISE_ADAPT_GAIN       2.00f

/* 叠加层与诊断选项：
 * - `DIAG_OVERLAY` 控制是否在文本区输出更多内部计数器
 * - `DRAW_COARSE_GRID` 预留给粗网格辅助绘制
 * - `DEBUG_LOG` 预留给串口/日志调试
 * - `IDLE_TEST_PATTERN` 在没有有效峰值时输出测试图案，便于确认显示链路工作
 * - `TEXT_REFRESH_DIV` 控制文字刷新分频
 * - `BILINEAR_SAMPLING` 定义默认插值方式 */
#define APP_DISPLAY_DIAG_OVERLAY           1u
#define APP_DISPLAY_DRAW_COARSE_GRID       0u
#define APP_DISPLAY_DEBUG_LOG              0u
#define APP_DISPLAY_IDLE_TEST_PATTERN      0u
#define APP_DISPLAY_TEXT_REFRESH_DIV       2u
#define APP_DISPLAY_BILINEAR_SAMPLING      1u

/* 叠加标记尺寸：
 * - 十字准星用于显示最终输出角度
 * - 峰值方框用于显示当前热力图峰值位置 */
#define APP_DISPLAY_CROSSHAIR_HALF_PX      6u
#define APP_DISPLAY_PEAK_MARKER_RADIUS_PX  4u

/* SAI DMA 活跃状态保持窗口。
 * 用于 UI 诊断时避免状态抖动过快。 */
#define APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES 10u

/* 编译期保护：
 * 这些检查用于尽早发现明显不合理的配置，避免运行期再暴露问题。 */
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

#endif /* APP_DISPLAY_CFG_H */
