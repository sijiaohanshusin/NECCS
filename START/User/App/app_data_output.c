/**
 * @file    app_data_output.c
 * @brief   VOFA+ JustFloat 调试数据输出实现
 *
 * ====================== 带宽预算 (重要！) ======================
 *  USART1 必须配置为 921600 baud，否则会严重拖慢算法任务！
 *
 *  模式1 (RMS AC):   16 × 4 + 4 =   68 字节 → 在 921600 下约  0.7ms
 *  模式2 (Raw TDM):  16 × 4 + 4 =   68 字节 → 在 921600 下约  0.7ms
 *  模式3 (Spectrum): 128 × 4 + 4 =  516 字节 → 在 921600 下约  5.6ms
 *
 *  本文件的函数每 DEBUG_THROTTLE_FRAMES 帧调用一次（见 app_main_task.c），
 *  留出足够间隔，不影响算法主流程。
 * ===============================================================
 */

#include "app_data_output.h"
#include "app_data_stream.h"
#include "ai_config.h"
#include "usart.h"
#include <math.h>   /* fabsf() */
#include <string.h> /* memcpy() */

/* =========================================================================
 * Section 1: JustFloat 协议底层
 * =========================================================================
 *
 * IEEE 754 复习:
 *   一个 32-bit float 的结构: [sign(1)] [exponent(8)] [mantissa(23)]
 *
 *   +Infinity 的编码: sign=0, exponent=0xFF(全1), mantissa=0
 *   二进制: 0 11111111 00000000000000000000000
 *   十六进制: 0x7F800000
 *   在内存中小端存储 (低地址存低字节):
 *     addr+0: 0x00
 *     addr+1: 0x00
 *     addr+2: 0x80
 *     addr+3: 0x7F
 *
 *   VOFA+ 用 +Infinity 作为帧尾，因为任何正常的物理信号都不可能是无穷大，
 *   所以它是天然的、不会误判的帧同步标记。
 */
static const uint8_t s_vofa_tail[4] = {0x00, 0x00, 0x80, 0x7F};

/**
 * @brief 底层 JustFloat 发送：数据体 + 帧尾
 *
 * 使用 HAL_UART_Transmit 阻塞发送。在调试阶段这是最简单可靠的方式。
 * 若后期需要不阻塞，可改为 HAL_UART_Transmit_DMA + 静态 TX 缓冲区。
 *
 * @param data  指向 float32_t 数组的指针
 * @param count 数组中 float 的个数
 */
static void VOFA_JustFloat_Send(const float32_t *data, uint16_t count)
{
    /* 发送数据体：count 个 float32_t，每个 4 字节 */
    HAL_UART_Transmit(&huart1,
                      (uint8_t *)data,
                      count * sizeof(float32_t),
                      HAL_MAX_DELAY);

    /* 发送帧尾：固定 4 字节 {0x00, 0x00, 0x80, 0x7F}
     * VOFA+ 收到帧尾后，才会把本帧的所有数据刷新到波形图上 */
    HAL_UART_Transmit(&huart1,
                      (uint8_t *)s_vofa_tail,
                      sizeof(s_vofa_tail),
                      HAL_MAX_DELAY);
}


/* =========================================================================
 * Section 2: 模式1 - 16路通道交流有效值（AC-RMS）
 * =========================================================================
 *
 * ---- 为什么不用 arm_rms_f32，而用 arm_std_f32 ----
 *
 * arm_rms_f32 计算的是"含DC的总 RMS"：
 *   总 RMS² = mean² + AC_RMS²
 *
 * 当 DC 偏置 (mean) 远大于声音信号 (AC_RMS) 时，
 * 总 RMS ≈ mean（一个常数），完全掩盖了响度的起伏变化。
 * 不同 MIC 的 DC 偏置因芯片/焊接工艺各异，导致跨通道比较无意义。
 *
 * arm_std_f32 计算的是"标准差"：
 *   std_dev = sqrt( (1/(N-1)) * sum( (x[n] - mean)² ) )
 *           ≈ AC_RMS   (N=256 时与 1/N 相差 < 0.4%，可忽略)
 *
 * 标准差 = 零均值化后的 RMS = 纯交流信号有效值
 * DC 偏置对它毫无影响，它只反映真正的声音响度。
 *
 * VOFA+ 显示结果:
 *   16 条随声音起伏的曲线，横轴是时间。
 *   始终贴近 0 的通道 = 虚焊/损坏的麦克风。
 */
void VOFA_Send_Channel_RMS(void)
{
    float32_t ac_rms_buf[MIC_CHANNELS];

    for (int ch = 0; ch < MIC_CHANNELS; ch++)
    {
        /* arm_std_f32: 内部自动求均值再做均方差，等价于纯交流有效值
         * 此处访问的是 Mic_Process_Buffer，即 AI_FFT_Process() 调用前的原始时域数据 */
        arm_std_f32(&Mic_Process_Buffer[ch * FRAME_LEN],
                    FRAME_LEN,
                    &ac_rms_buf[ch]);
    }

    /* 发送 16 个 float + 帧尾 = 68 字节 */
    VOFA_JustFloat_Send(ac_rms_buf, MIC_CHANNELS);
}


/* =========================================================================
 * Section 3: 模式2 - 单通道 FFT 幅度谱
 * =========================================================================
 *
 * ---- arm_rfft_fast_f32 输出格式详解 (N = FRAME_LEN = 256) ----
 *
 * 对于实数输入的 FFT，输出只有 N/2+1 = 129 个独立复数（共轭对称性）。
 * 但 CMSIS-DSP 为了节省内存，把输出压缩到刚好 N 个 float：
 *
 *  p_freq 内存布局:
 *  ┌──────┬──────┬──────┬──────┬──────┬──────┬─ ... ─┬──────┬──────┐
 *  │ [0]  │ [1]  │ [2]  │ [3]  │ [4]  │ [5]  │       │[254] │[255] │
 *  │DC实部│奈实部│B1实部│B1虚部│B2实部│B2虚部│  ...  │B127实│B127虚│
 *  └──────┴──────┴──────┴──────┴──────┴──────┴─ ... ─┴──────┴──────┘
 *    ↑                    ↑─────────────────────────────────────────↑
 *  DC和奈奎斯特的虚部恒为0              标准复数交织格式
 *  被省略掉，各自只用1个float           [Re, Im, Re, Im, ...]
 *
 *  频率对应关系: bin k 对应的物理频率 = k × Fs / N = k × 187.5 Hz
 *    bin 0  = 0 Hz (直流)
 *    bin 1  = 187.5 Hz
 *    bin 10 = 1875 Hz  (人声基频范围)
 *    bin 64 = 12000 Hz
 *    bin 127= 23812.5 Hz (接近奈奎斯特 24000 Hz)
 *
 * 因此求幅度需要分三段处理：
 *   1. bin 0  (DC):       mag = |p_freq[0]|
 *   2. bin 1..126:        mag = sqrt(p_freq[2k]^2 + p_freq[2k+1]^2)
 *                              → 用 arm_cmplx_mag_f32 批量计算
 *   3. bin 127 (奈奎斯特): mag = |p_freq[1]|
 */
/* =========================================================================
 * Section 3: 模式2 - 原始 TDM 槽位诊断（核心排查工具）
 * =========================================================================
 *
 * ---- 工作原理 ----
 *
 * DMA 接收到的 Mic_Rx_Buffer 是"交织"(Interleaved)格式：
 *   内存布局 [帧0_槽0, 帧0_槽1, ..., 帧0_槽15,
 *             帧1_槽0, 帧1_槽1, ..., 帧1_槽15, ...]
 *
 * 本函数直接按槽位（而非按通道）遍历原始 DMA 数据，
 * 计算每个 TDM 槽位的 AC-RMS（标准差）并发送。
 *
 * 这完全绕过了 arm_mat_trans_q15 解交织步骤，
 * 是判断问题来源的"黄金诊断"手段。
 *
 * ---- 如何解读 VOFA+ 中的结果 ----
 *
 *   情况 A：槽位 4 和 5 的 AC-RMS 不同  →  说明 PCMD3180 输出正常，
 *            问题在软件解交织代码
 *
 *   情况 B：槽位 4 和 5 的 AC-RMS 相同  →  PCMD3180 输出本身就一样，
 *            继续做硬件排查（MSEL 引脚、焊点、走线短路）
 *
 * @note 直接读 Non-Cacheable 的 Mic_Rx_Buffer，无需 Cache 维护
 * @note 存在极小的数据竞争风险（DMA 正在写的同时读），
 *       但对 256 帧累积 RMS 影响可忽略，debug 阶段完全可接受
 */
void VOFA_Send_Raw_TDM_Slot_RMS(void)
{
    float32_t sum_sq[MIC_CHANNELS] = {0.0f};
    float32_t sum[MIC_CHANNELS]    = {0.0f};

    /* 遍历 Ping 缓冲区的前 FRAME_LEN 帧
     * p_raw[frame * MIC_CHANNELS + slot] 就是第 frame 帧、第 slot 槽的原始 int16_t 样本 */
    const int16_t *p_raw = &Mic_Rx_Buffer[0];

    for (int frame = 0; frame < FRAME_LEN; frame++)
    {
        for (int slot = 0; slot < MIC_CHANNELS; slot++)
        {
            float32_t s = (float32_t)p_raw[frame * MIC_CHANNELS + slot];
            sum[slot]    += s;
            sum_sq[slot] += s * s;
        }
    }

    float32_t slot_ac_rms[MIC_CHANNELS];
    for (int slot = 0; slot < MIC_CHANNELS; slot++)
    {
        float32_t mean = sum[slot] / (float32_t)FRAME_LEN;
        /* var = E[x^2] - mean^2 = AC 能量（与 arm_std_f32 等价）*/
        float32_t var  = sum_sq[slot] / (float32_t)FRAME_LEN - mean * mean;
        slot_ac_rms[slot] = (var > 0.0f) ? sqrtf(var) : 0.0f;
    }

    /* 发送 16 个 float + 帧尾 = 68 字节 */
    VOFA_JustFloat_Send(slot_ac_rms, MIC_CHANNELS);
}


/* =========================================================================
 * Section 4: 模式3 - 单通道 FFT 幅度谱
 * ========================================================================= */
void VOFA_Send_FFT_Magnitude(uint8_t channel)
{
    /* 频域缓冲区指针：定位到指定通道的起始位置 */
    const float32_t *p_freq = &Mic_Freq_Buffer[channel * FRAME_LEN];

    /* 幅度谱缓冲区：128 个 bin，在栈上分配 (512字节，可接受)
     * 为什么是 128？因为 256点实数 FFT 只有 N/2 = 128 个独立频率分量 */
    float32_t mag_buf[FRAME_LEN / 2];   /* = mag_buf[128] */

    /* ---- Step 1: bin 0 (DC分量) ---- */
    /* DC 的虚部恒为 0，实部存在 p_freq[0]，直接取绝对值 */
    mag_buf[0] = fabsf(p_freq[0]);

    /* ---- Step 2: bin 1 ~ 126 ---- */
    /* arm_cmplx_mag_f32: 批量计算复数幅度 sqrt(Re^2 + Im^2)
     *   输入: &p_freq[2]  → 从 index=2 开始，正好是 bin1 的实部
     *   输出: &mag_buf[1] → 从 mag_buf[1] 开始存放结果
     *   个数: (FRAME_LEN/2 - 2) = 126 个复数对
     *
     * 底层实现: Cortex-M7 上会自动展开为 VMUL/VADD/VSQRT 指令（FPU），
     * 比手写 for 循环快约 4~8 倍。 */
    arm_cmplx_mag_f32(&p_freq[2],
                      &mag_buf[1],
                      (FRAME_LEN / 2) - 2);  /* 126 个复数 */

    /* ---- Step 3: bin 127 (奈奎斯特分量) ---- */
    /* 奈奎斯特的虚部恒为 0，实部存在 p_freq[1]，直接取绝对值 */
    mag_buf[FRAME_LEN / 2 - 1] = fabsf(p_freq[1]);

    /* 发送 128 个 float + 帧尾 = 516 字节
     * 在 VOFA+ 中你会看到一条"频谱曲线"：
     *   - 横轴 = 通道序号 (0..127)，对应频率 (0..23812.5 Hz)
     *   - 纵轴 = 幅度 (未归一化，单位取决于输入信号幅值) */
    VOFA_JustFloat_Send(mag_buf, FRAME_LEN / 2);
}


/* =========================================================================
 * Section 5: 模式4 - SRP-PHAT 定位结果 + 粗搜功率图
 * =========================================================================
 *
 * 数据布局 (52 个 float):
 *   [0]      = x_angle  (水平方位角, 度)
 *   [1]      = y_angle  (垂直俯仰角, 度)
 *   [2]      = energy   (归一化能量, 0~1)
 *   [3..51]  = SRP_Power[0..48]  (粗搜 49 点功率图)
 *
 * VOFA+ 中:
 *   - 前 3 条线: 方位角跟踪 + 能量包络
 *   - 后 49 条线: 功率热力图 (敲阵列前方应看到明显峰值)
 *
 * 带宽: 52 × 4 + 4 = 212 字节 → 在 921600 下约 2.3ms
 */
void VOFA_Send_SRP_Result(const Sound_Pos_t *pos)
{
    float32_t send_buf[3 + COARSE_TOTAL]; /* 3 + 49 = 52 floats, 208B 栈 */

    send_buf[0] = pos->x_angle;
    send_buf[1] = pos->y_angle;
    send_buf[2] = pos->energy;

    /* 复制粗搜功率图 (SRP_Power 的前 49 个元素) */
    memcpy(&send_buf[3], SRP_Power, COARSE_TOTAL * sizeof(float32_t));

    /* 数据完整性检查：扫描所有 52 个 float，将 NAN/Inf 替换为 0
     * 关键：如果 SRP_Power 中某个值恰好是 +Infinity (0x7F800000)，
     * VOFA+ 会误认为是帧尾，导致帧解析错位 → 出现 NAN 交替现象 */
    for (int i = 0; i < 3 + COARSE_TOTAL; i++)
    {
        if (!isfinite(send_buf[i]))
        {
            send_buf[i] = 0.0f;
        }
    }

    VOFA_JustFloat_Send(send_buf, 3 + COARSE_TOTAL);
}
