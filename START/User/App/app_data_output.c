/**
 * @file    app_data_output.c
 * @brief   调试数据输出实现 (VOFA+ JustFloat 协议)
 * @details 通过 UART1 输出音频处理结果，用于 PC 端实时监控
 *
 * 支持的输出模式：
 * - 通道 RMS (16 通道有效值)
 * - FFT 幅度谱 (单通道，128 点)
 * - SRP 定位结果 (角度 + 能量 + 81 点粗搜功率图 + 诊断信息)
 *
 * VOFA+ JustFloat 协议：
 * - 数据格式：[float32 × N] + [0x00, 0x00, 0x80, 0x7F]
 * - 波特率：921600 (UART1)
 * - 超时：5ms (非阻塞发送)
 *
 * 异常处理：
 * - 粗搜功率图异常时，使用上一帧有效数据 (hold)
 * - 非有限值 (NaN/Inf) 自动替换为 0.0
 * - UART 忙时丢弃数据，计数器递增
 */

#include "app_data_output.h"

#include "ai_beamforming.h"
#include "ai_config.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "app_user_config.h"
#include "usart.h"

#include <math.h>
#include <string.h>

#define VOFA_MAX_FLOATS        (FRAME_LEN / 2u)
#define VOFA_DIAG_FLOATS       8u
#define VOFA_SRP_FLOATS        (3u + COARSE_TOTAL + VOFA_DIAG_FLOATS)
#define VOFA_FRAME_MAX_BYTES   ((VOFA_MAX_FLOATS * sizeof(float32_t)) + 4u)

static const uint8_t s_vofa_tail[4] = {0x00, 0x00, 0x80, 0x7F};

static uint8_t s_tx_buf[VOFA_FRAME_MAX_BYTES];
static volatile uint32_t s_vofa_tx_drop_count = 0u;
static uint32_t s_vofa_frame_seq = 0u;

static float32_t s_last_coarse_power[COARSE_TOTAL];
static uint8_t s_has_last_coarse = 0u;
static uint8_t s_coarse_bad_streak = 0u;
static volatile uint32_t s_coarse_hold_count = 0u;

/**
 * @brief   VOFA+ JustFloat 协议发送函数
 * @param   data   浮点数据数组
 * @param   count  数据个数
 * @details 数据格式：[float32 × count] + [0x00, 0x00, 0x80, 0x7F]
 * @note    如果 UART 忙，丢弃数据并递增计数器
 */
static void VOFA_JustFloat_Send(const float32_t *data, uint16_t count)
{
    /* 参数有效性检查：空指针、零数量、或超出最大允许浮点数都直接返回 */
    if ((data == NULL) || (count == 0u) || (count > VOFA_MAX_FLOATS))
    {
        return;
    }

    /* 检查 UART1 是否处于就绪状态；若正在发送则丢弃本帧，递增丢帧计数 */
    /* [注意] 此处使用轮询发送，若 UART 持续忙则会大量丢帧，可改为 DMA 发送 */
    if (huart1.gState != HAL_UART_STATE_READY)
    {
        s_vofa_tx_drop_count++;   /* 记录丢帧次数，供 VOFA+ 诊断通道显示 */
        return;
    }

    /* 计算有效载荷字节数：每个 float32 占 4 字节 */
    uint16_t payload_bytes = (uint16_t)(count * sizeof(float32_t));
    /* 将浮点数据拷贝到发送缓冲区前半部分 */
    memcpy(&s_tx_buf[0], data, payload_bytes);
    /* 追加 VOFA+ JustFloat 帧尾标记：0x00 0x00 0x80 0x7F（IEEE754 +Inf 小端） */
    /* VOFA+ 软件通过此 4 字节序列识别帧边界并切割帧 */
    memcpy(&s_tx_buf[payload_bytes], s_vofa_tail, sizeof(s_vofa_tail));

    /* 以阻塞方式发送完整帧（payload + 4 字节帧尾），超时 = VOFA_UART_TX_TIMEOUT */
    /* [注意] 阻塞发送会占用 CPU 时间；921600bps 下 100 字节约耗时 ~1.1ms */
    /* [改进] 可改为 HAL_UART_Transmit_DMA 以释放 CPU，但需要增加缓冲区管理 */
    if (HAL_UART_Transmit(&huart1,
                          s_tx_buf,
                          (uint16_t)(payload_bytes + sizeof(s_vofa_tail)),
                          VOFA_UART_TX_TIMEOUT) != HAL_OK)
    {
        s_vofa_tx_drop_count++;   /* 发送失败（超时）也计入丢帧 */
    }
}

/**
 * @brief   发送 16 通道 RMS 值
 * @details 计算每个通道的有效值 (标准差)，用于监控信号强度
 * @note    使用 arm_std_f32 计算标准差 (AC RMS)
 */
void VOFA_Send_Channel_RMS(void)
{
    float32_t ac_rms_buf[MIC_CHANNELS];   /* 存放各通道的交流 RMS 值 */

    for (uint32_t ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        /* arm_std_f32 计算标准差，等价于零均值信号的 AC RMS（不含 DC 分量） */
        /* 输入：第 ch 通道的时域浮点帧，长度 FRAME_LEN (256 点) */
        /* [改进] 若需要真 RMS（含 DC），应改用 arm_rms_f32 */
        arm_std_f32(&Mic_Process_Buffer[ch * FRAME_LEN], FRAME_LEN, &ac_rms_buf[ch]);
    }

    /* 将 16 个通道 RMS 值打包发送，VOFA+ 可绘制 16 条实时曲线 */
    VOFA_JustFloat_Send(ac_rms_buf, MIC_CHANNELS);
}

/**
 * @brief   发送单通道 FFT 幅度谱
 * @param   channel  通道号 (0-15)
 * @details 输出 128 点幅度谱 (0-24kHz, 187.5Hz 分辨率)
 * @note    RFFT 输出格式：[DC, Nyquist, Re1, Im1, Re2, Im2, ...]
 *          转换为：[|DC|, |bin1|, |bin2|, ..., |Nyquist|]
 */
void VOFA_Send_FFT_Magnitude(uint8_t channel)
{
    /* 越界检查：通道号超出 MIC_CHANNELS 直接返回 */
    if (channel >= MIC_CHANNELS)
    {
        return;
    }

    /* 指向频域缓冲区中该通道的起始位置（每通道 FRAME_LEN 个复数 float） */
    const float32_t *p_freq = &Mic_Freq_Buffer[(uint32_t)channel * FRAME_LEN];
    float32_t mag_buf[FRAME_LEN / 2u];   /* 幅度谱缓冲区，128 点 */

    /* RFFT 输出布局：[DC(实), Nyquist(实), Re1, Im1, Re2, Im2, ..., Re127, Im127] */
    /* CMSIS-DSP arm_rfft_fast_f32 将 DC 和奈奎斯特分量压缩到 [0] 和 [1] 位置 */

    /* DC 分量：p_freq[0] 是纯实数，直接取绝对值 */
    mag_buf[0] = fabsf(p_freq[0]);
    /* bin 1 ~ 126：复数幅度 sqrt(Re² + Im²)，由 arm_cmplx_mag_f32 批量计算 */
    /* 输入从 p_freq[2] 开始（跳过 DC/奈奎斯特），计算 (FRAME_LEN/2 - 2) = 126 个幅度值 */
    arm_cmplx_mag_f32(&p_freq[2], &mag_buf[1], (FRAME_LEN / 2u) - 2u);
    /* 奈奎斯特分量（24kHz）：p_freq[1] 是纯实数，放到 mag_buf 最后一位 */
    mag_buf[(FRAME_LEN / 2u) - 1u] = fabsf(p_freq[1]);

    /* 发送 128 点幅度谱到 VOFA+，频率分辨率 = 187.5 Hz/bin */
    VOFA_JustFloat_Send(mag_buf, FRAME_LEN / 2u);
}

/**
 * @brief   发送 SRP 定位结果 + 诊断信息
 * @param   pos  SRP 定位结果 (角度 + 能量)
 * @details 发送数据包含：
 *          - [0-2]: x_angle, y_angle, energy
 *          - [3 ... 3+COARSE_TOTAL-1]: 9×9 粗搜功率图 (81 点)
 *          - [base ... base+7]: 诊断信息 (计数器 + 质量指标)
 * @note    异常处理：
 *          - 粗搜功率图异常时，使用上一帧有效数据 (hold)
 *          - 非有限值 (NaN/Inf) 自动替换为 0.0
 */
void VOFA_Send_SRP_Result(const Sound_Pos_t *pos)
{
    /* 空指针保护 */
    if (pos == NULL)
    {
        return;
    }

    float32_t send_buf[VOFA_SRP_FLOATS];   /* 完整发送帧缓冲，含定位结果+粗搜图+诊断 */
    float32_t coarse_abs_sum = 0.0f;       /* 粗搜功率图的绝对值累加，用于判断是否全零 */
    uint32_t coarse_nonfinite_count = 0u;  /* 粗搜图中 NaN/Inf 的数量 */

    /* 前 3 个浮点：x 轴角度（°）、y 轴角度（°）、SRP 峰值能量 */
    send_buf[0] = pos->x_angle;
    send_buf[1] = pos->y_angle;
    send_buf[2] = pos->energy;

    /* 将本帧粗搜功率图（9×9 = 81 点）复制到发送缓冲区的 [3 ... 83] 位置 */
    memcpy(&send_buf[3], SRP_Power, COARSE_TOTAL * sizeof(float32_t));

    /* 扫描粗搜图：统计非有限值数量和绝对值总和，用于判断本帧是否有效 */
    for (uint32_t i = 0u; i < COARSE_TOTAL; i++)
    {
        float32_t v = send_buf[3u + i];
        if (!isfinite(v))
        {
            coarse_nonfinite_count++;   /* 发现 NaN 或 Inf，跳过累加 */
            continue;
        }
        coarse_abs_sum += fabsf(v);    /* 正常值累加绝对值 */
    }

    /* 异常帧处理：含 NaN/Inf 或功率图全零则使用上一帧备份数据（hold 机制） */
    if ((coarse_nonfinite_count > 0u) || (coarse_abs_sum < 1.0e-6f))
    {
        /* 若已有上一帧有效数据，且连续坏帧数为 0（严格只替换一次），则用备份替换本帧 */
        /* [注意] s_coarse_bad_streak == 0 条件意味着只有第一次坏帧才会 hold，之后放弃 */
        /* [改进] 可放宽为允许连续多帧 hold：改为 s_coarse_bad_streak < MAX_HOLD_FRAMES */
        if ((s_has_last_coarse == 1u) && (s_coarse_bad_streak == 0u))
        {
            memcpy(&send_buf[3], s_last_coarse_power, COARSE_TOTAL * sizeof(float32_t));
            s_coarse_hold_count++;   /* 记录 hold 次数，供诊断通道显示 */
        }

        /* 递增连续坏帧计数器，防止溢出 */
        if (s_coarse_bad_streak < 255u)
        {
            s_coarse_bad_streak++;
        }
    }
    else
    {
        /* 本帧数据有效：更新备份缓存，清空坏帧计数 */
        memcpy(s_last_coarse_power, &send_buf[3], COARSE_TOTAL * sizeof(float32_t));
        s_has_last_coarse = 1u;      /* 标记已有至少一帧有效历史数据 */
        s_coarse_bad_streak = 0u;    /* 清零连续坏帧计数 */
    }

    /* 在粗搜图之后追加 8 个诊断浮点（VOFA_DIAG_FLOATS = 8） */
    uint32_t base = 3u + COARSE_TOTAL;    /* 诊断数据起始偏移 */
    send_buf[base + 0u] = (float32_t)g_audio_both_flags_count;  /* 音频双缓冲同时就绪次数（理想情况为 0）*/
    send_buf[base + 1u] = (float32_t)s_vofa_tx_drop_count;      /* UART 发送丢帧次数 */
    send_buf[base + 2u] = (float32_t)g_srp_invalid_count;       /* SRP 无效帧计数（峰值不可信） */
    send_buf[base + 3u] = (float32_t)s_coarse_hold_count;       /* 粗搜图 hold 次数 */
    send_buf[base + 4u] = (float32_t)g_srp_low_contrast_count;  /* SRP 低对比度（峰不突出）计数 */
    send_buf[base + 5u] = g_srp_last_contrast;   /* 上一帧的对比度值（峰值/均值之比） */
    send_buf[base + 6u] = g_srp_last_quality;    /* 上一帧的综合质量评分 */
    send_buf[base + 7u] = (float32_t)(++s_vofa_frame_seq);  /* 帧序号，自增，用于检测丢帧 */

    /* 最终非有限值清零保护：防止 VOFA+ 解析失败 */
    for (uint32_t i = 0u; i < VOFA_SRP_FLOATS; i++)
    {
        if (!isfinite(send_buf[i]))
        {
            send_buf[i] = 0.0f;   /* 将任何残留 NaN/Inf 替换为 0 */
        }
    }

    /* 发送完整的 3+81+8 = 92 个浮点值到 VOFA+ */
    VOFA_JustFloat_Send(send_buf, VOFA_SRP_FLOATS);
}
