# 定向录音技术研究报告

## Directional Recording Research Report

> NECCS 声学相机项目 — STM32H743 平台定向录音可行性分析与实现方案

---

## 1. 引言

### 1.1 需求背景

NECCS 声学相机已实现 16 通道 SRP-PHAT 声源定位，能够实时确定声源方向。然而，当前系统仅输出定位坐标与能量，无法提取目标方向的增强音频信号。定向录音功能将允许用户：

- **选择性收听**：增强来自特定方向的声音，抑制其他方向干扰
- **证据采集**：记录定向增强后的音频，用于故障诊断回溯
- **智能联动**：与 SRP-PHAT 定位结果联动，自动追踪主声源进行录制

### 1.2 技术目标

| 指标 | 目标值 | 约束 |
|------|--------|------|
| 输出通道 | 1ch (单声道) | SD 卡写入带宽 |
| 采样率 | 48 kHz | 与 ADC 采样率一致 |
| 位深 | 16-bit PCM | WAV 格式兼容性 |
| 数据率 | 96 KB/s | SD Class 10 带宽充足 |
| 处理延迟 | ≤ 1 帧 (5.33 ms) | 实时性要求 |
| CPU 开销 | ≤ 0.5 ms/帧 | Audio_Pipeline_Task 时间预算 |

---

## 2. 波束成形算法综述

### 2.1 延迟求和波束成形 (Delay-and-Sum, DAS)

#### 原理

DAS 是最经典的波束成形方法。核心思想：通过对各通道施加适当的时间延迟，使来自目标方向的信号同相叠加，其他方向的信号因相位不一致而相消。

#### 数学公式

对于 $N$ 个麦克风，目标方向 $(\theta, \phi)$ 的 DAS 输出为：

$$y[n] = \frac{1}{N} \sum_{i=0}^{N-1} x_i[n - \Delta_i(\theta, \phi)]$$

其中时间延迟：

$$\Delta_i(\theta, \phi) = \frac{\mathbf{d}_i \cdot \mathbf{u}(\theta, \phi)}{c} \cdot f_s$$

- $\mathbf{d}_i$: 第 $i$ 个麦克风相对于阵列中心的位置向量
- $\mathbf{u}(\theta, \phi)$: 目标方向的单位向量
- $c = 343$ m/s: 声速
- $f_s = 48000$ Hz: 采样率

#### 整数延迟近似

在 48 kHz 采样率下，阵列尺寸约 60 mm 的条件下：

$$\Delta_{max} = \frac{d_{max}}{c} \cdot f_s = \frac{0.06}{343} \times 48000 \approx 8.4 \text{ samples}$$

整数延迟足够满足精度需求（误差 < 1 样本 ≈ 20.8 μs），无需分数延迟插值。

#### 优势

- **计算简单**：仅需整数索引偏移和累加，无需乘法
- **数值稳定**：无矩阵求逆或迭代，不存在发散风险
- **实现确定性**：执行时间固定，适合实时嵌入式系统
- **与 SRP-PHAT 协同**：可直接复用已有的阵列几何参数和 LUT

#### 局限

- **空间分辨率有限**：由阵列孔径决定，低频分辨率差
- **旁瓣较高**：均匀加权导致旁瓣抑制约 -13 dB
- **无自适应能力**：不能根据噪声环境自动调整

### 2.2 最小方差无失真响应 (MVDR / Capon)

#### 原理

MVDR 通过最小化输出功率的同时保持目标方向增益不变:

$$\mathbf{w}_{MVDR} = \frac{\mathbf{R}^{-1} \mathbf{a}(\theta)}{\mathbf{a}^H(\theta) \mathbf{R}^{-1} \mathbf{a}(\theta)}$$

其中 $\mathbf{R}$ 为空间协方差矩阵，$\mathbf{a}(\theta)$ 为导向向量。

#### STM32H743 可行性分析

| 操作 | 复杂度 | STM32H743 耗时估算 |
|------|--------|-------------------|
| 协方差矩阵估计 | $O(N^2 \cdot L)$ | ~2 ms (16ch × 256 samples) |
| 矩阵求逆 (16×16) | $O(N^3)$ | ~1.5 ms (Cholesky 分解) |
| 权重向量计算 | $O(N^2)$ | ~0.1 ms |
| 滤波输出 | $O(N \cdot L)$ | ~0.3 ms |
| **总计** | | **~3.9 ms** |

**结论：MVDR 在 STM32H743 上理论可行但风险较高：**
- CPU 预算紧张（3.9 ms + SRP-PHAT 4 ms ≈ 7.9 ms，超过 5.33 ms 帧周期）
- 协方差矩阵条件数差时数值不稳定
- 需要复数矩阵库（CMSIS-DSP 仅提供实数矩阵分解）
- 实现复杂度高，调试困难

### 2.3 广义旁瓣消除器 (GSC)

GSC 是 MVDR 的等价实现，将约束优化分解为固定波束成形器 + 自适应噪声消除：

$$y[n] = \mathbf{w}_q^H \mathbf{x}[n] - \mathbf{w}_a^H \mathbf{B}^H \mathbf{x}[n]$$

- $\mathbf{w}_q$: 静态 DAS 权重
- $\mathbf{B}$: 阻塞矩阵（投影到目标方向的零空间）
- $\mathbf{w}_a$: 自适应滤波器权重 (LMS/NLMS 更新)

**评估：相比直接 MVDR 略优（避免矩阵逆），但 LMS 收敛速度和步长调优仍有工程风险。暂不推荐作为首选方案。**

### 2.4 方案选择

| 方案 | CPU 负载 | 数值稳定性 | 实现复杂度 | 音质 | 推荐度 |
|------|---------|-----------|-----------|------|--------|
| **DAS** | ★☆☆ (~0.2ms) | ★★★ | ★☆☆ | ★★☆ | **首选** |
| MVDR | ★★★ (~3.9ms) | ★☆☆ | ★★★ | ★★★ | 不推荐 |
| GSC | ★★☆ (~1.5ms) | ★★☆ | ★★★ | ★★★ | 备选 |

**决策：采用 DAS 作为首个实现版本。** 理由：
1. CPU 开销极低，不影响现有 SRP-PHAT 管线
2. 数值完全稳定，无需额外容错
3. 可在 1 天内完成实现和集成
4. 后续可作为 GSC 的固定波束成形器组件

---

## 3. DAS 实现方案

### 3.1 系统架构

```
Audio_Pipeline_Task
    ├── Deinterleave (q15 → float planar)
    ├── [Bandpass Filter] (可选)
    ├── AI_FFT_Process()
    ├── AI_SRP_PHAT_Process() → (θ_target, φ_target)
    ├── **AI_BeamSteer_Process()** ← 新增
    │     ├── 计算/查表延迟 Δ_i(θ, φ)
    │     ├── 延迟求和: y[n] = 1/N Σ x_i[n - Δ_i]
    │     └── 输出到环形缓冲
    └── xQueueOverwrite(xPositionQueue, ...)
```

### 3.2 延迟计算

复用 SRP-PHAT 已有的阵列几何定义（`ai_srp_lut.c` 中的麦克风坐标）：

```c
/* 预计算延迟表 (整数样本延迟) */
int8_t delays[MIC_CHANNELS];  /* 范围 [-8, +8] */

for (i = 0; i < MIC_CHANNELS; i++) {
    float tau = (mic_x[i] * ux + mic_y[i] * uy) / SPEED_OF_SOUND * SAMPLING_RATE;
    delays[i] = (int8_t)roundf(tau);
}
```

### 3.3 延迟求和实现

```c
/* 输入: Mic_Process_Buffer[ch][n], 16ch × 256 samples
 * 输出: beam_output[256], 单通道增强信号 */
void AI_BeamSteer_Process(float *beam_output, uint16_t frame_len)
{
    int i, n, idx;
    float sum;

    for (n = 0; n < frame_len; n++) {
        sum = 0.0f;
        for (i = 0; i < MIC_CHANNELS; i++) {
            idx = n - s_delays[i];
            if (idx >= 0 && idx < frame_len) {
                sum += Mic_Process_Buffer[i * frame_len + idx];
            }
        }
        beam_output[n] = sum / (float)MIC_CHANNELS;
    }
}
```

### 3.4 性能估算

- 操作：16 × 256 = 4096 次浮点加法 + 256 次浮点除法
- 预估耗时：~0.15 ms @ 480 MHz (Cortex-M7 双发射 + FPU)
- 占音频帧周期：0.15 / 5.33 = 2.8%

### 3.5 波束追踪模式

| 模式 | 描述 | 实现 |
|------|------|------|
| 自动追踪 | 跟踪 SRP-PHAT 主声源方向 | 每帧更新 θ,φ = current_pos |
| 手动锁定 | 用户通过 UI 指定固定方向 | θ,φ 由 UI slider 或热力图点击设定 |
| 触发锁定 | 触发时锁定方向 | TRIGGERED 时冻结 θ,φ |

---

## 4. 音频输出与 SD 卡录制

### 4.1 输出格式

- **WAV PCM**：48 kHz, 16-bit, 1 channel
- 文件名：`BEAM/BEAM_XXXXXX.wav`
- 最大时长：SD 卡容量限制（~30 min @ 96 KB/s ≈ 172 MB）

### 4.2 缓冲策略

```
Audio_Pipeline_Task                Storage_Task
     │                                  │
     ├── beam_output[256]               │
     ├── float→q15 转换                 │
     ├── 写入环形缓冲 (4KB)    ──────→  │
     │                                  ├── 检测半满
     │                                  ├── f_write(2KB)
     │                                  └── 继续等待
```

- 环形缓冲：4 KB (AXI SRAM)，足够存储 ~42 ms 音频
- 写入粒度：2 KB（SD 卡扇区对齐）
- 双缓冲：一半在填充，一半在写入

### 4.3 float → q15 转换

```c
/* 使用 CMSIS-DSP 硬件加速转换 */
arm_float_to_q15(beam_output, q15_output, FRAME_LEN);
```

---

## 5. 边界条件与风险

### 5.1 整数延迟误差

最大延迟误差 = 0.5 样本 = 10.4 μs。在 48 kHz 采样率下：
- 8 kHz 信号：相位误差 = 0.5/6 × 360° = 30°（可接受）
- 20 kHz 信号：相位误差 = 0.5/2.4 × 360° = 75°（较大，高频性能下降）

**缓解：** 对于高频应用场景，考虑分数延迟（线性插值），CPU 开销增加约 50%。

### 5.2 帧边界效应

当延迟 Δ_i > 0 时，帧头部的样本需要前一帧的数据。解决方案：
- 维护每通道 8 样本的历史缓冲（overlap buffer）
- 总额外内存：16 × 8 × 4 = 512 字节

### 5.3 方向突变

自动追踪模式下，若声源方向帧间跳变，延迟向量突变会导致输出不连续（click/pop）。缓解：
- 延迟向量使用 EMA 平滑（α = 0.3）
- 或在延迟变化时执行交叉淡入淡出（crossfade）

---

## 6. 内存布局

| 资源 | 大小 | 位置 | 说明 |
|------|------|------|------|
| 延迟表 | 16 bytes | DTCM | int8_t delays[16] |
| 历史缓冲 | 512 bytes | DTCM | float overlap[16][8] |
| 波束输出 | 1 KB | DTCM | float beam_output[256] |
| 录制环形缓冲 | 4 KB | AXI SRAM | q15_t ring_buf[2048] |
| **总计** | **~5.5 KB** | | 远低于可用容量 |

---

## 7. API 设计

```c
/* ai_beamsteer.h */

/** @brief 初始化波束控向模块 */
void AI_BeamSteer_Init(void);

/** @brief 设置波束方向 (角度制) */
void AI_BeamSteer_SetDirection(float theta_deg, float phi_deg);

/** @brief 获取当前波束方向 */
void AI_BeamSteer_GetDirection(float *theta_deg, float *phi_deg);

/** @brief 执行 DAS 波束成形 (在 Audio_Pipeline_Task 中调用)
 *  @param output  输出缓冲 (float, frame_len)
 *  @param frame_len 帧长度 (256)
 */
void AI_BeamSteer_Process(float *output, uint16_t frame_len);

/** @brief 设置追踪模式 */
typedef enum {
    BEAMSTEER_MODE_AUTO    = 0u,  /* 自动追踪 SRP 主源 */
    BEAMSTEER_MODE_MANUAL  = 1u,  /* 手动固定方向 */
    BEAMSTEER_MODE_TRIGGER = 2u   /* 触发时锁定 */
} AI_BeamSteer_Mode_t;

void AI_BeamSteer_SetMode(AI_BeamSteer_Mode_t mode);
```

---

## 8. 实现计划与完成状态

### Phase 1: 基本 DAS (整数延迟) — ✅ 已完成
1. ✅ 创建 `ai_beamsteer.h/c` — 16 通道 DAS, 整数延迟, overlap 缓冲
2. ✅ 实现延迟计算 + 延迟求和
3. ✅ 在 `Audio_Pipeline_Task` 中插入调用 (deinterleave → DAS → FFT 管线顺序)
4. ✅ 三种追踪模式: AUTO / MANUAL / TRIGGER

### Phase 2: SD 卡录制集成 — ✅ 已完成
1. ✅ 波束输出通过 `App_Recorder_Feed()` 写入 64KB 环形缓冲
2. ✅ `Storage_Task` 读取环形缓冲写入 WAV 文件
3. ✅ UI 集成: Capture 屏幕增加双模式录音 (MONO 定向 / RAW16 原始)
4. ✅ FatFS R0.14b 中间件 + BSP_SD 底层驱动

### Phase 3: 增强 — 部分完成
1. ⬜ 分数延迟插值 (线性) — 当前使用整数延迟, 满足需求
2. ✅ 帧边界 overlap 缓冲 (16ch × 10 samples)
3. ⬜ 交叉淡入淡出 — 自动模式下方向跟随足够平滑, 暂不需要

### 实际实现与报告设计的差异

| 项目 | 报告设计 | 实际实现 | 原因 |
|------|---------|---------|------|
| DAS 输出缓冲位置 | DTCM | DTCM (`__SECTION_DTCM`) | 一致 |
| 环形缓冲大小 | 4 KB | 64 KB (8 帧) | 增大以容纳 RAW16 模式 |
| 录音模式 | 仅 MONO | MONO + RAW16 | 扩展支持原始 16ch 存储 |
| DAS 调用位置 | FFT 之后 | FFT 之前 | FFT 原地修改输入, DAS 必须先执行 |
| Feed 调用条件 | 每帧调用 | 仅录音时调用 | 优化: 避免非录音时的无用开销 |
| mono_frame 参数 | 总是有效 | 可为 NULL | 当 beamsteer 禁用时传 NULL, Feed 按模式处理 |

---

## 9. 结论

DAS 延迟求和波束成形是 NECCS 声学相机平台上定向录音功能的最优首选方案。其计算开销极低 (~0.15 ms/帧，仅占 CPU 预算的 2.8%)，数值完全稳定，且实现复杂度低。通过与现有 SRP-PHAT 定位管线的紧密集成，已实现自动追踪录音和手动定向录音两种工作模式。

**实际实现验证 (2026-04-09):**
- DAS 模块 (`ai_beamsteer.c`) 已完整实现并集成到音频管线
- 录音模块 (`app_recorder.c`) 支持 MONO 定向 + RAW16 原始两种模式
- 存储任务 (`app_storage_task.c`) 实现异步 WAV 文件写入
- Capture 屏幕 UI 提供完整的录音控制界面
- 构建验证: 0 Error(s), 0 Warning(s)

主要风险点是高频信号的整数延迟误差和帧边界效应，均有明确的缓解方案。建议在基本 DAS 功能验证后，根据实际板级测试结果决定是否引入分数延迟插值。

---

*最后更新: 2026-04-09*
*作者: NECCS Team — Algorithm Engineer*
