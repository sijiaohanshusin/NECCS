#!/usr/bin/env python3
"""
Fix garbled Chinese comments in app_display.c.
Phase 1: Strip all garbled comment content while preserving code exactly.
Phase 2: Inject clean Chinese comments at known function locations.
"""
import re

INPUT  = "app_display.c.encoding-bak"
OUTPUT = "app_display.c"

with open(INPUT, "r", encoding="utf-8") as f:
    content = f.read()

GARBLED = set("閺閸鐏缂娑濞閻瀵鐎婢閼娴韫妫濮閵閳楠瀹鐟鐞鐠")

def has_garble(s):
    return any(c in GARBLED for c in s)

# ---- Phase 1: Process block comments ----
def process_comments(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        if i < n-1 and text[i] == '/' and text[i+1] == '*':
            # Find end of block comment
            end = text.find('*/', i + 2)
            if end == -1:
                out.append(text[i:])
                break
            block = text[i:end+2]
            if has_garble(block):
                # Extract meaningful ASCII from the comment
                inner = block[2:-2]  # strip /* */
                # Keep Doxygen tags and ASCII content
                kept_parts = []
                for line in inner.split('\n'):
                    stripped = line.strip()
                    if stripped.startswith('* @file') or stripped.startswith('@file'):
                        kept_parts.append(line)
                    elif stripped.startswith('* @brief') or stripped.startswith('@brief'):
                        # Keep tag, strip garbled desc 
                        kept_parts.append(line.split('@brief')[0] + '@brief')
                    elif stripped.startswith('* @param') or stripped.startswith('@param'):
                        kept_parts.append(line)
                    elif stripped.startswith('* @return') or stripped.startswith('@return'):
                        kept_parts.append(line)
                    elif stripped.startswith('* @details') or stripped.startswith('@details'):
                        kept_parts.append(line.split('@details')[0] + '@details')
                    elif stripped.startswith('* @note') or stripped.startswith('@note'):
                        kept_parts.append(line)
                    elif stripped.startswith('*') and not has_garble(line):
                        kept_parts.append(line)
                    elif stripped == '' or stripped == '*':
                        kept_parts.append(line)
                    # Skip lines that are purely garbled
                
                if kept_parts:
                    result = '/*' + '\n'.join(kept_parts) + '\n */'
                    # Clean up empty Doxygen blocks
                    if result.strip() in ('/**\n */', '/*\n */', '/* */'):
                        result = ''
                    out.append(result)
                else:
                    pass  # Remove entirely empty garbled blocks
            else:
                out.append(block)
            i = end + 2
        elif i < n-1 and text[i] == '/' and text[i+1] == '/':
            # Line comment
            eol = text.find('\n', i)
            if eol == -1:
                line_comment = text[i:]
                if has_garble(line_comment):
                    pass  # Remove garbled line comment
                else:
                    out.append(line_comment)
                break
            line_comment = text[i:eol]
            if has_garble(line_comment):
                pass  # Remove garbled line comment
            else:
                out.append(line_comment)
            i = eol
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)

content = process_comments(content)

# ---- Phase 2: Inject clean function comments ----
# Map: unique part of function signature -> clean comment to insert before it
FUNC_DOC = {
    "static float s_clamp_f32(float v, float lo, float hi)": 
        "/** @brief 将 float 值限制在 [lo, hi] 范围内 */",
    "static uint8_t s_clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)":
        "/** @brief 将 uint8_t 值限制在 [lo, hi] 范围内 */",
    "static uint16_t s_clamp_u16(int32_t v, uint16_t lo, uint16_t hi)":
        "/** @brief 将 int32_t 值限制到 [lo, hi] 并转为 uint16_t */",
    "static uint16_t s_rgb565(uint8_t r, uint8_t g, uint8_t b)":
        "/** @brief 将 8bit R/G/B 分量打包为 RGB565 像素值 */",
    "static uint16_t s_heat_color(float t)":
        "/** @brief 将归一化值 (0.0~1.0) 映射为 5 段热力图 RGB565 颜色 */",
    "static void s_build_heat_lut(void)":
        "/** @brief 构建 256 级热力图 colormap LUT (s_heat_lut[]) */",
    "static void s_build_kernels(void)":
        "/** @brief 构建高斯模糊核 (s_blur_kernel) 和精细融合核 (s_fine_kernel) */",
    "const char *App_Display_ModeName(App_Display_Mode_t mode)":
        "/** @brief 返回显示模式的可读名称 (\"FAST\"/\"BAL\"/\"CLEAN\") */",
    "const char *App_Display_InterpName(App_Display_Interp_t interp)":
        "/** @brief 返回插值模式的可读名称 (\"BIL\"/\"NEAR\") */",
    "const char *App_Display_NormName(App_Display_Norm_t norm)":
        "/** @brief 返回归一化模式的可读名称 (\"FULL\"/\"FAST\") */",
    "static void s_load_mode_defaults(App_Display_Mode_t mode, App_Display_RuntimeCfg_t *cfg)":
        "/** @brief 根据显示模式枚举加载默认配置参数 */",
    "void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg)":
        "/**\n * @brief 应用运行时显示配置\n * @details 对各参数进行范围限制后写入 s_cfg, 同时使 LUT 失效以触发重建。\n */",
    "void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg)":
        "/** @brief 读取当前运行时显示配置的副本 */",
    "void App_Display_SetCameraView(App_Display_CameraView_t view_mode)":
        "/** @brief 设置摄像头显示模式 (叠加/独立/冻结/纯热力图) */",
    "void App_Display_SetMode(App_Display_Mode_t mode)":
        "/** @brief 切换显示模式并加载对应默认配置 */",
    "App_Display_Mode_t App_Display_GetMode(void)":
        "/** @brief 获取当前显示模式 */",
    "uint8_t App_Display_IsReady(void)":
        "/** @brief 查询显示模块是否已完成初始化 */",
    "static void s_fill_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)":
        "/** @brief 异步矩形填充 (优先 DMA2D, 失败回退 CPU) */",
    "static void s_draw_hline_async(uint16_t x0, uint16_t y, uint16_t x1, uint32_t color)":
        "/** @brief 异步绘制水平线 */",
    "static void s_draw_vline_async(uint16_t x, uint16_t y0, uint16_t y1, uint32_t color)":
        "/** @brief 异步绘制垂直线 */",
    "static void s_draw_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)":
        "/** @brief 异步绘制空心矩形边框 */",
    "static uint8_t s_backbuf_slot(void)":
        "/** @brief 查询 LTDC 后缓冲区是 A(0) 还是 B(1), 0xFF=未知 */",
    "static uint32_t s_display_frame_budget_ms(void)":
        "/** @brief 计算单帧渲染时间预算 (ms), 基于目标帧率 */",
    "static void s_commit_frame(void)":
        "/**\n * @brief 等待 DMA2D 完成并请求 LTDC 前后缓冲区交换\n * @details 先 flush 所有待处理的 DMA2D 操作, 然后请求 LTDC 在下次 VSYNC 时交换 front/back 缓冲。\n */",
    "void App_Display_Init(void)":
        "/**\n * @brief 初始化显示模块\n * @details 构建卷积核/LUT、初始化 LCD、计算热力图/摄像头/UI 布局、清屏。\n *          通过 g_display_init_stage 报告进度, g_display_init_error 报告错误。\n */",
    "static float s_power_mag(float v)":
        "/** @brief 安全取功率值: 过滤 NaN/Inf/负值, 返回 0.0 或正值 */",
    "static void s_apply_output_remap(float *x_angle, float *y_angle)":
        "/** @brief 根据编译时配置对输出角度进行 XY 交换/反转 */",
    "static void s_inverse_output_remap(float *x_angle, float *y_angle)":
        "/** @brief s_apply_output_remap 的逆变换 (用于从显示坐标回算原始角度) */",
    "static void s_resample_coarse_to_field(const SRP_VisFrame_t *vis_frame)":
        "/**\n * @brief 将粗搜索网格功率值双线性插值重采样到 s_field_a[]\n * @details 遍历显示场每个像素, 计算对应的粗网格坐标, 进行双线性插值。\n *          支持 output remap (XY交换/反转)。\n */",
    "static void s_apply_fine_fusion(const SRP_VisFrame_t *vis_frame)":
        "/**\n * @brief 将精细搜索点以高斯核加权叠加到 s_field_a[]\n * @details 遍历所有精细网格点, 对功率超过阈值的点以 s_fine_kernel 高斯核\n *          加权叠加到对应场像素位置, 增强热点细节分辨率。\n */",
    "static void s_apply_blur_once(void)":
        "/**\n * @brief 对 s_field_a[] 执行一次可分离高斯模糊\n * @details 水平方向卷积到 s_field_b[], 垂直方向卷积回 s_field_a[]。\n *          使用 s_blur_kernel[] 高斯核, 边界使用 clamp 填充。\n */",
    "static float s_prepare_field(const SRP_VisFrame_t *vis_frame)":
        "/**\n * @brief 完整场处理流水线: 重采样 -> 精细融合 -> 高斯模糊\n * @return 场中的峰值功率, 用于后续归一化\n */",
    "static uint8_t s_compute_norm_full(float ratio)":
        "/**\n * @brief 完整归一化: ratio -> dB -> 截断 db_floor -> gamma 校正 -> 0..255\n * @param ratio  功率与参考峰值的比值 (0.0~1.0)\n * @return 归一化后的 8bit 值\n */",
    "static void s_refresh_norm_lut(void)":
        "/** @brief 刷新快速归一化 LUT (当 gamma 或 db_floor 变化时重建) */",
    "static uint8_t s_norm_fast_lookup(float ratio)":
        "/** @brief 快速归一化: 用预计算 LUT 代替 log10f/powf */",
    "void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h)":
        "/**\n * @brief 刷新 LCD 像素坐标 -> 场坐标的插值缓存表\n * @details 预计算每个显示像素对应的场网格坐标和权重,\n *          避免每帧渲染时重复计算, 支持最近邻和双线性两种模式。\n */",
    "static void s_refresh_camera_scale_cache(uint16_t map_w, uint16_t map_h, uint16_t src_w, uint16_t src_h)":
        "/** @brief 刷新摄像头到显示区域的最近邻缩放缓存 */",
    "static void s_update_norm_field(float field_peak, uint32_t frame_seq)":
        "/**\n * @brief 自适应归一化: EMA 峰值跟踪 + 噪声门 -> 8bit 映射\n * @details 1. EMA 跟踪 s_peak_ema 作为归一化参考峰值\n *          2. 计算自适应噪底 (背景功率均值 * noise_adapt_gain)\n *          3. 减去噪底后按 ratio 查 LUT 或完整计算归一化到 0..255\n *          4. 特殊处理: 信号过弱时显示棋盘格测试图案或全黑\n */",
    "static uint16_t s_angle_to_x(float angle)":
        "/** @brief 将水平角度映射为热力图区域内的 X 像素坐标 */",
    "static uint16_t s_angle_to_y(float angle)":
        "/** @brief 将垂直角度映射为热力图区域内的 Y 像素坐标 (Y轴翻转) */",
    "static void s_render_field_rows(void)":
        "/**\n * @brief 将 8bit 归一化场逐行渲染为 RGB565 并提交到帧缓冲\n * @details 支持双线性/最近邻插值。优先使用 L8+CLUT DMA2D 模式,\n *          失败时回退到 CPU 查表 s_heat_lut[] 转 RGB565。\n */",
    "static App_FreqBand_t s_spectrum_clamp_band(App_FreqBand_t band, uint16_t bin_count)":
        "/** @brief 将频带范围限制到有效 bin 区间内 */",
    "void App_Display_Render(const Sound_Pos_t *pos,":
        "/**\n * @brief 主渲染函数 (每帧由 UI 任务调用)\n * @details 完整渲染流程:\n *          1. 等待 LTDC swap 完成 (帧同步)\n *          2. s_prepare_field: 重采样+融合+模糊\n *          3. EMA 峰值更新\n *          4. s_update_norm_field: 归一化到 8bit\n *          5. 按 camera_view_mode 渲染: 纯热力图/摄像头叠加/冻结帧\n *          6. 绘制准星和峰值标记\n *          7. 频谱图 + UI 文字覆盖层\n *          8. LVGL overlay 合成\n *          9. 提交帧缓冲 (swap)\n */",
}

# Apply function doc comments
lines = content.split('\n')
new_lines = []
for i, line in enumerate(lines):
    stripped = line.strip()
    for sig, doc in FUNC_DOC.items():
        if stripped.startswith(sig) or (sig in stripped and '(' in stripped):
            # Check if line above is empty or already has a doc comment
            if i > 0:
                prev = new_lines[-1].strip() if new_lines else ''
                if prev == '' or prev == '*/':
                    new_lines.append(doc)
                    break
                elif prev.startswith('/*') and prev.endswith('*/') and len(prev) < 10:
                    # Replace tiny empty comment with proper doc
                    new_lines[-1] = doc
                    break
            else:
                new_lines.append(doc)
                break
    new_lines.append(line)

content = '\n'.join(new_lines)

# ---- Phase 3: Fix file header ----
# Replace the stripped file header with a clean one
old_header_pattern = re.compile(
    r'/\*\*\s*\n\s*\*\s*@file\s+app_display\.c\s*\n\s*\*\s*@brief.*?\*/',
    re.DOTALL
)
clean_header = """/**
 * @file    app_display.c
 * @brief   热力图渲染与显示后端 (Heatmap Rendering Engine)
 * @details
 * 本模块负责将 SRP-PHAT 声源定位功率谱渲染为 LCD 上的实时热力图。
 *
 * 渲染流水线:
 *   SRP_VisFrame_t -> 双线性重采样 -> 精细融合 -> 高斯模糊
 *   -> EMA峰值跟踪+噪声门+归一化 -> Colormap LUT -> RGB565
 *   -> DMA2D/LTDC 输出 -> 摄像头叠加(可选) -> UI覆盖层
 *
 * 关键函数:
 * - App_Display_Init: 初始化LCD、构建colormap LUT和卷积核
 * - s_prepare_field: 重采样 + 精细融合 + 高斯平滑
 * - s_update_norm_field: EMA峰值 + 噪声门 + 归一化到8bit
 * - s_render_field_rows: 8bit映射为RGB565并输出到帧缓冲
 * - App_Display_Render: 主渲染入口, UI任务每帧调用
 */"""
content = old_header_pattern.sub(clean_header, content, count=1)

# ---- Phase 4: Clean up ----
# Remove lines that are just " *" (empty comment body) in isolation
content = re.sub(r'\n\s*\n\s*\n\s*\n', '\n\n', content)
# Remove any remaining tiny empty comments like /*  */
content = re.sub(r'/\*\s*\*/', '', content)
# Clean up resulting double blank lines
content = re.sub(r'\n{3,}', '\n\n', content)

with open(OUTPUT, "w", encoding="utf-8", newline='\n') as f:
    f.write(content)

garble_count = sum(1 for c in content if c in GARBLED)
print(f"Output: {len(content)} bytes, {content.count(chr(10))+1} lines")
print(f"Remaining garbled chars: {garble_count}")
