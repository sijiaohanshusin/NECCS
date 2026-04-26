/**
 * @file   app_spectrum.c
 * @brief  FFT 频谱分析与发布模块实现
 * @details
 * 内部机制详述：
 *
 * 1. seqlock + 双缓冲 无锁发布：
 *    音频任务（写者）和 UI 任务（读者）共享频谱数据。
 *    写者步骤：seq 奇数 -> 写 next_buf -> seq++ 偶数 -> index 切换
 *    读者步骤：读 seq1 -> 若奇数则写入中，等待 -> 读 buf（局部）-> 读 seq2 -> seq1==seq2 且为偶数=成功
 *
 * 2. 频段打包存储：
 *    App_FreqBand_t 有 start_bin + end_bin 两个 uint16_t。
 *    Pack: 将两者合并入一个 32 位整数 | start(16) | end(16) |
 *    这样可用一条 volatile uint32_t 读/写，保证同一帧内不会读到一半旧/一半新的混合数据。
 *
 * 3. 幅度谱计算：
 *    对 16 个通道的 FFT 结果进行平均：
 *      FFT 输出格式是复数对: [Re0, Im0, Re1, Im1, ...]
 *        - bin=0（DC）：Re0 = buf[0]，Im0 = 0（读 buf[0] 的绝对值即可）
 *        - bin=1..N-2：Re = buf[bin*2]，Im = buf[bin*2+1]，幅度 = sqrt(Re²+Im²)
 *        - bin=N-1（奈奎斯特）：Re = buf[1]，Im = 0
 *
 * [注意] 内部 s_publish_buffers 放在默认 AXI SRAM（可缓存区）。
 *        Mic_Freq_Buffer 在 DTCM（零等待）。两者均不需要 DMA 访问，无需缓存撤销。
 */

#include "app_spectrum.h"               /* 本模块公开接口 */

#include "ai_config.h"                  /* APP_SPECTRUM_BIN_COUNT, DELTA_F, MIC_CHANNELS, FRAME_LEN */
#include "ai_beamforming.h"             /* Mic_Freq_Buffer：FFT 输出的频域缓冲区 */
#include "app_data_stream.h"            /* AI_SRP_SetActiveFreqRange()：同步 SRP 活动频段 */
#include "app_user_config.h"            /* SRP_FREQ_BIN_START, SRP_FREQ_BIN_END */

#include <math.h>                       /* sqrtf, fabsf, logf, expf, isfinite */
#include <string.h>                     /* memset, memcpy */

/** @brief 双缓冲发布数组：[0] 和 [1] 两个频谱帧，写者和读者分别操作不同索引 */
static App_SpectrumFrame_t s_publish_buffers[2];

/** @brief 当前读者应当读取的缓冲索引（volatile：写者切换后其他核立即可见）*/
static volatile uint8_t s_publish_index = 0u;

/**
 * @brief seqlock 序列号：平数=无写入进行，奇数=写入中
 * @details 写者：seq++ -> 写数据 -> seq++
 *          读者：seq 为奇数则等待/跳过；seq 左右相等且为偶数则读取成功
 */
static volatile uint32_t s_publish_seq = 0u;

/**
 * @brief 当前活动频段（压缩打包为 32 位整数，保证 32 位读写的原子性）
 * @details 高 16 位 = end_bin，低 16 位 = start_bin
 */
static volatile uint32_t s_active_band_packed = 0u;

/**
 * @brief 当前预览频段（存储方式同上，不影响 SRP 算法）
 */
static volatile uint32_t s_preview_band_packed = 0u;

/**
 * @brief  将频段结构体压缩为一个 32 位整数存储
 * @details 目的：保证对 s_active_band_packed 的单次读/写操作是 32 位原子的，
 *          避免读到一半 start_bin 新/end_bin 旧的混合情况。
 *          Cortex-M7 上 32 位对齐访问天然原子。
 */
static uint32_t s_pack_band(App_FreqBand_t band)
{
    /* 低 16 位存放 start_bin，高 16 位存放 end_bin */
    return ((uint32_t)band.start_bin) | ((uint32_t)band.end_bin << 16);
}

/**
 * @brief  将 32 位打包值解包为频段结构体
 * @param  packed 打包值（s_pack_band 的返回值）
 * @return 解包后的频段结构体
 */
static App_FreqBand_t s_unpack_band(uint32_t packed)
{
    App_FreqBand_t band;

    band.start_bin = (uint16_t)(packed & 0xFFFFu);  /* 取低 16 位还原 start_bin */
    band.end_bin   = (uint16_t)(packed >> 16);       /* 取高 16 位还原 end_bin */

    return band;
}

/**
 * @brief  将频段的起止 bin 钳位至合法范围（防止越界访问）
 * @details 检查 start_bin 和 end_bin 不超过最大 bin，
 *          并保证 end_bin >= start_bin（防止频段反向）。
 */
static App_FreqBand_t s_clamp_band(App_FreqBand_t band)
{
    uint16_t last_bin = (uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u);  /* 最大有效 bin 索引 */

    if (band.start_bin > last_bin)     /* start_bin 超界则钳到上限 */
    {
        band.start_bin = last_bin;
    }
    if (band.end_bin > last_bin)       /* end_bin 超界则钳到上限 */
    {
        band.end_bin = last_bin;
    }
    if (band.end_bin < band.start_bin) /* end < start 则将 end 就近为 start（单点频带） */
    {
        band.end_bin = band.start_bin;
    }

    return band;
}

/**
 * @brief  从 Mic_Freq_Buffer 填充一帧频谱数据
 * @details 遍历所有通道，对每个 bin 计算幅度，求和后求平均。
 *          FFT 输出格式：[Re0, Im0, Re1, Im1, ...]
 *            - bin=0（DC）：buf[0] = 实部，取绝对值即可
 *            - bin=1..N-2：Re = buf[bin*2]，Im = buf[bin*2+1]，幅度 = sqrt(Re²+Im²)
 *            - bin=N-1（奈奎斯特）：Re = buf[1]，Im = 0（ARM CFFT 特殊格式）
 *
 * @param  frame 用于存储计算结果的帧结构体
 * @param  seq   帧序列号（传失帧检测用）
 */
static void s_fill_frame(App_SpectrumFrame_t *frame, uint32_t seq)
{
    App_FreqBand_t active_band  = App_Spectrum_GetActiveBand();   /* 读取缓存的活动频段 */
    App_FreqBand_t preview_band = App_Spectrum_GetPreviewBand();  /* 读取预览频段 */
    uint32_t bin;  /* 当前 bin 索引 */
    uint32_t ch;   /* 当前麦克风通道索引 */

    if (frame == NULL)             /* 防止空指针（内部调用不应传 NULL，已上校验） */
    {
        return;
    }

    memset(frame->magnitude, 0, sizeof(frame->magnitude));  /* 幅度数组清零（累加前需要）*/

    frame->seq = seq;                    /* 帧序列号，供读者检测是否是新帧 */
    frame->bin_count = (uint16_t)APP_SPECTRUM_BIN_COUNT;  /* 属与该帧的 bin 数量 */
    frame->delta_f_hz = DELTA_F;         /* 频率分辨率（Hz/bin）*/
    frame->active_band  = active_band;   /* 载入当前活动频段到帧里，保证帧和频段一致性 */
    frame->preview_band = preview_band;  /* 载入当前预览频段 */

    /* 第一轮循环：逐通道累加幅度到 frame->magnitude */
    for (ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        /* 每个通道的 FFT 输出在 Mic_Freq_Buffer 中的起始地址 */
        /* Mic_Freq_Buffer 布局：[ch0的FRAME_LEN个float | ch1的...] */
        const float32_t *p_freq = &Mic_Freq_Buffer[ch * FRAME_LEN];

        /* bin=0（DC）：buf[0] = 实部，取绝对值 */
        frame->magnitude[0] += fabsf(p_freq[0]);

        /* bin=1..(N-2)：幅度 = sqrt(Re² + Im²)，Re=buf[bin*2], Im=buf[bin*2+1] */
        for (bin = 1u; bin < (APP_SPECTRUM_BIN_COUNT - 1u); bin++)
        {
            float re = p_freq[bin * 2u];       /* 实部（就cos分量） */
            float im = p_freq[bin * 2u + 1u];  /* 虚部（就sin分量） */

            frame->magnitude[bin] += sqrtf((re * re) + (im * im));  /* 模长 = |X[bin]| */
        }

        /* bin=N-1（奈奎斯特）：buf[1] = 实部，取绝对值（ARM CFFT 特殊格式）*/
        frame->magnitude[APP_SPECTRUM_BIN_COUNT - 1u] += fabsf(p_freq[1]);
    }

    /* 第二轮循环：求通道平均幅度（除以通道数） */
    for (bin = 0u; bin < APP_SPECTRUM_BIN_COUNT; bin++)
    {
        float avg_mag = frame->magnitude[bin] / (float)MIC_CHANNELS;  /* 多通道算术平均 */

        /* isfinite 判断防止 NaN/Inf 写入帧数据引发显示异常 */
        frame->magnitude[bin] = isfinite(avg_mag) ? avg_mag : 0.0f;
    }
}

/**
 * @brief  初始化频谱分析模块
 * @details 清空发布缓冲并设置默认频段（活动和预览频段均初始化为 SRP 的默认频带）。
 */
void App_Spectrum_Init(void)
{
    App_FreqBand_t default_band = App_Spectrum_DefaultBand();  /* 获取默认频段（来自 ai_config.h）*/
    uint32_t i;  /* 循环变量（ARM Compiler 5 要求在块首声明）*/

    memset(s_publish_buffers, 0, sizeof(s_publish_buffers));  /* 双缓冲清零 */
    s_publish_index = 0u;                         /* 当前发布索引从 0 开始 */
    s_publish_seq = 0u;                           /* seqlock 序列号从 0 开始（0 为偶，表示无写入进行）*/
    s_active_band_packed  = s_pack_band(default_band);  /* 活动频段存储为 pack 形式 */
    s_preview_band_packed = s_pack_band(default_band);  /* 预览频段与活动频段同步 */

    /* 初始化双缓冲的元数据（尺寸和频段），避免读者第一次读到未初始化的帧 */
    for (i = 0u; i < 2u; i++)
    {
        s_publish_buffers[i].bin_count = (uint16_t)APP_SPECTRUM_BIN_COUNT;
        s_publish_buffers[i].delta_f_hz = DELTA_F;
        s_publish_buffers[i].active_band  = default_band;
        s_publish_buffers[i].preview_band = default_band;
    }
}

/**
 * @brief  从 FFT 结果发布新频谱帧（seqlock 写者端）
 * @details seqlock 写入步骤：
 *   1. seq++（居奇）— 开始写入信号
 *   2. __DMB()：内存屏障，确保 seq++ 对他核可见
 *   3. 写入 next_buf（非当前发布索引），不干扰读者
 *   4. __DMB()：确保帧数据写入完成，在切换索引之前
 *   5. s_publish_index = next_index：切换读者可用缓冲
 *   6. seq++（居偶）= 写入完成信号
 * @param  seq 当前音频帧序列号
 */
void App_Spectrum_PublishFromFft(uint32_t seq)
{
    /* 计算非当前索引（按二异或就是 0<->1 切换）*/
    uint8_t next_index = (uint8_t)(s_publish_index ^ 1u);

    __DMB();           /* 屏障1：确保之前的操作已对所有 CPU 可见 */
    s_publish_seq++;   /* seqlock 开始写入：居奇数 -> 读者将知晓操作进行中 */
    __DMB();           /* 屏障2：确保 seq++ 对其他 CPU 可见，在实际写入前 */
    s_fill_frame(&s_publish_buffers[next_index], seq);  /* 写入利用非当前缓冲，不干扰读者 */
    __DMB();           /* 屏障3：确保帧数据写入完成，在切换索引之前 */
    s_publish_index = next_index;  /* 切换读者可用索引，读者从这里读取新帧 */
    s_publish_seq++;   /* seqlock 完成写入：居偶数 -> 通知读者写入完成 */
}

/**
 * @brief  复制最新频谱帧（无 seqlock 保护，用于单读者场景）
 * @details 直接 memcpy，不重试。只在确保只有一个读者时使用。
 *          [改进] 若将来有多个读者，应统一改为 GetLatestFrame()。
 * @param  frame 接收缓冲指针
 */
void App_Spectrum_CopyFrame(App_SpectrumFrame_t *frame)
{
    if (frame == NULL)         /* 空指针保护 */
    {
        return;
    }

    memcpy(frame, &s_publish_buffers[s_publish_index], sizeof(*frame));  /* 直接副本 */
}

/**
 * @brief  以 seqlock 方式安全读取最新频谱帧
 * @details seqlock 读取步骤（最多重试 3 次）：
 *   1. 读 start_seq
 *   2. 若 start_seq 为奇数（写入中），则跳过本次尝试
 *   3. 读取 s_publish_index 后 memcpy 数据
 *   4. __DMB()：确保读取完成后才读 end_seq
 *   5. 读 end_seq；若 start_seq == end_seq 且为偶数，则读取成功
 * @param  frame 接收缓冲指针
 * @return 1 = 成功读取，0 = 失败
 */
uint8_t App_Spectrum_GetLatestFrame(App_SpectrumFrame_t *frame)
{
    uint32_t start_seq;  /* 尝试前读到的 seqlock 序列号 */
    uint32_t end_seq;    /* 尝试后读到的 seqlock 序列号 */
    uint8_t index;       /* 被读取时的缓冲索引 */
    uint32_t attempt;    /* 当前重试次数 */

    if (frame == NULL)   /* 空指针保护 */
    {
        return 0u;
    }

    for (attempt = 0u; attempt < 3u; attempt++)  /* 最多重试 3 次，防止长期占用 CPU */
    {
        start_seq = s_publish_seq;   /* 读取开始前的序列号 */
        __DMB();                     /* 内存屏障：确保 start_seq 读取先于数据读取 */
        if ((start_seq & 1u) != 0u)  /* 奇数说明写入中，本次尝试无效，直接下一次 */
        {
            continue;
        }

        index = s_publish_index;     /* 读取当前发布索引 */
        memcpy(frame, &s_publish_buffers[index], sizeof(*frame));  /* 副本到本地 */
        __DMB();                     /* 内存屏障：确保数据读取完成后才读 end_seq */
        end_seq = s_publish_seq;     /* 读取完成后的序列号 */
        if ((start_seq == end_seq) && ((end_seq & 1u) == 0u))  /* 两次序列号相等且为偶数 = 读取一致 */
        {
            return 1u;               /* 读取成功，数据一致 */
        }
        /* 若到此说明读取期间发生了写入（seq 变化），继续重试 */
    }

    return 0u;  /* 3 次重试均失败（高并发写入频率极快时极少发生）*/
}

/**
 * @brief  获取默认频段（基于 SRP 配置）
 * @return 默认频段结构体
 */
App_FreqBand_t App_Spectrum_DefaultBand(void)
{
    App_FreqBand_t band = {
        (uint16_t)SRP_FREQ_BIN_START,  /* SRP 配置的起始 bin（来自 app_user_config.h）*/
        (uint16_t)SRP_FREQ_BIN_END     /* SRP 配置的终止 bin */
    };

    return s_clamp_band(band);  /* 钳位防止配置超出合法范围 */
}

/**
 * @brief  设置当前激活频段
 * @details 同步更新 SRP 算法的活动频段（AI_SRP_SetActiveFreqRange）。
 *          [注意] 此操作跨任务写 s_active_band_packed（volatile uint32_t），
 *                 依赖 Cortex-M7 32 位对齐访问原子性，无需额外互斥。
 * @param  band 新的频段（会被自动 clamp）
 */
void App_Spectrum_SetActiveBand(App_FreqBand_t band)
{
    App_FreqBand_t clamped = s_clamp_band(band);  /* 先钳位，防止越界 */

    __DMB();  /* 内存屏障：确保 clamped 计算完成，在写入 packed 前 */
    s_active_band_packed = s_pack_band(clamped);  /* 原子写 32 位 packed 值 */

    /* 同步更新 SRP 算法的活动频段（影响 SRP-PHAT 的频率积分范围）*/
    AI_SRP_SetActiveFreqRange(clamped.start_bin, clamped.end_bin);
}

/**
 * @brief  获取当前激活频段
 * @return 当前激活频段结构体
 */
App_FreqBand_t App_Spectrum_GetActiveBand(void)
{
    uint32_t packed;

    __DMB();              /* 内存屏障：确保读到最新值，不是旧缓存 */
    packed = s_active_band_packed;
    return s_clamp_band(s_unpack_band(packed));  /* 解包并再次钳位（防御性编程）*/
}

/**
 * @brief  设置当前预览频段
 * @details 仅更新 UI 显示用的预览频段，不触发 SRP 算法更新。
 *          [注意] 同 SetActiveBand，依赖 volatile uint32_t 原子写。
 * @param  band 新的预览频段（会被自动 clamp）
 */
void App_Spectrum_SetPreviewBand(App_FreqBand_t band)
{
    App_FreqBand_t clamped = s_clamp_band(band);  /* 钳位 */

    __DMB();  /* 内存屏障 */
    s_preview_band_packed = s_pack_band(clamped);  /* 原子写 */
}

/**
 * @brief  获取当前预览频段
 * @return 当前预览频段结构体
 */
App_FreqBand_t App_Spectrum_GetPreviewBand(void)
{
    uint32_t packed;

    __DMB();              /* 内存屏障 */
    packed = s_preview_band_packed;
    return s_clamp_band(s_unpack_band(packed));  /* 解包并钳位 */
}

/**
 * @brief  将频率 bin 索引转换为频率 (Hz)
 * @details 公式：f = bin × Δf，Δf = 采样率/FFT点数 = 48000/256 = 187.5 Hz/bin
 * @param  bin  bin 索引
 * @return 对应的频率值 (Hz)
 */
float App_Spectrum_BinToHz(uint16_t bin)
{
    uint16_t last_bin = (uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u);  /* 最大有效 bin */

    if (bin > last_bin)    /* 超界则钳位（防止返回异常大的频率值）*/
    {
        bin = last_bin;
    }

    return ((float)bin * DELTA_F);  /* bin × 187.5 Hz/bin = 频率（Hz）*/
}

/**
 * @brief  将频率 (Hz) 转换为最近的 bin 索引
 * @details 公式：bin = round(hz / Δf)；四舍五入取近。
 * @param  hz  频率值 (Hz)
 * @return 最接近的 bin 索引
 */
uint16_t App_Spectrum_HzToBin(float hz)
{
    uint16_t last_bin = (uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u);
    float clamped_hz = hz;        /* 钳位后的频率值 */
    float max_hz = App_Spectrum_BinToHz(last_bin);  /* 最大可表示频率 */
    uint32_t bin;

    if (clamped_hz <= 0.0f)    /* 非正频率 -> DC bin */
    {
        return 0u;
    }
    if (clamped_hz >= max_hz)  /* 超过奈奎斯特频率 -> 返回最大 bin */
    {
        return last_bin;
    }

    /* 四舍五入：+0.5f 再取整，等效于 round() 但避免 math.h roundf 依赖 */
    bin = (uint32_t)((clamped_hz / DELTA_F) + 0.5f);
    if (bin > last_bin)        /* 防止整数溢出（理论上已钳位，防御性保护）*/
    {
        bin = last_bin;
    }

    return (uint16_t)bin;
}

/**
 * @brief  将面板坐标轴像素位置转换为频谱 bin 索引
 * @details 支持线性和对数两种坐标轴缩放，以及坐标轴反转。
 *   线性模式：hz = min_hz + (max_hz - min_hz) × norm
 *   对数模式：hz = exp(ln(min_hz) + (ln(max_hz) - ln(min_hz)) × norm)
 *   其中 norm = (axis_px - axis_px0) / (axis_length - 1) 为归一化坐标 [0,1]
 *
 * @param  axis_px     像素坐标
 * @param  axis_px0    坐标轴起始像素
 * @param  axis_length 坐标轴像素长度
 * @param  min_hz      最低频率 (Hz)
 * @param  scale_mode  0 = 线性分布，非零 = 对数分布
 * @param  invert      非零 = 反转方向
 * @return 对应的 bin 索引
 */
uint16_t App_Spectrum_PanelAxisToBin(uint16_t axis_px,
                                     uint16_t axis_px0,
                                     uint16_t axis_length,
                                     float min_hz,
                                     uint8_t scale_mode,
                                     uint8_t invert)
{
    float max_hz = App_Spectrum_BinToHz((uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u));  /* 最高可表示频率 */
    float clamped_min_hz = min_hz;  /* 钳位后的最低频率 */
    float norm;  /* 归一化坐标 [0,1]，0 = 坐标轴最左/最底，1 = 最右/最顶 */
    float hz;    /* 像素坐标对应的频率（Hz）*/

    if ((axis_length <= 1u) || (APP_SPECTRUM_BIN_COUNT <= 1u))  /* 无效轴参数保护 */
    {
        return 0u;
    }

    /* 钳位 min_hz：不得低于 DELTA_F（避免对数计算时 ln(0)）且不得超过 max_hz */
    if (clamped_min_hz < DELTA_F)
    {
        clamped_min_hz = DELTA_F;      /* 最小合法频率：1 个 bin 宽度 */
    }
    if (clamped_min_hz > max_hz)
    {
        clamped_min_hz = max_hz;
    }

    /* 计算像素坐标的归一化位置 norm ∈ [0, 1] */
    if (axis_px <= axis_px0)           /* 在坐标轴起始点及左侧 */
    {
        norm = 0.0f;
    }
    else
    {
        uint32_t axis_local = (uint32_t)axis_px - (uint32_t)axis_px0;  /* 相对坐标轴的偏移 */
        uint32_t span = (uint32_t)axis_length - 1u;                     /* 坐标轴范围（像素）*/

        if (axis_local >= span)        /* 到达或超过坐标轴末端 */
        {
            norm = 1.0f;
        }
        else
        {
            norm = (float)axis_local / (float)span;  /* 线性归一化 */
        }
    }

    if (invert != 0u)                  /* 轴反转：高频对应小像素坐标 */
    {
        norm = 1.0f - norm;
    }

    /* 将 norm 映射到频率 */
    if (scale_mode != 0u)              /* 对数刻度（人耳感知友好，适合频谱显示）*/
    {
        float log_min = logf(clamped_min_hz);  /* ln(min_hz) */
        float log_max = logf(max_hz);          /* ln(max_hz) */

        hz = expf(log_min + ((log_max - log_min) * norm));  /* 对数插值 */
    }
    else                               /* 线性刻度 */
    {
        hz = clamped_min_hz + ((max_hz - clamped_min_hz) * norm);  /* 线性插值 */
    }

    return App_Spectrum_HzToBin(hz);  /* 频率转 bin 索引 */
}

/**
 * @brief  将面板 X 轴像素位置转换为频谱 bin（线性分布，不反转）
 * @details 是 PanelAxisToBin 的简化版：线性尺度，不反转，最低频率 = DELTA_F。
 * @param  x_px       X 像素坐标
 * @param  plot_x0    绘图区域起始 X
 * @param  plot_width 绘图区域宽度
 * @return 对应的 bin 索引
 */
uint16_t App_Spectrum_PanelXToBin(uint16_t x_px, uint16_t plot_x0, uint16_t plot_width)
{
    /* 委托给通用函数，参数：线性(0)，不反转(0)，最低频率=DELTA_F */
    return App_Spectrum_PanelAxisToBin(x_px, plot_x0, plot_width, DELTA_F, 0u, 0u);
}

/**
 * @brief  将面板坐标轴像素位置转换为频谱 bin 索引
 * @param  axis_px     像素坐标
 * @param  axis_px0    坐标轴起始像素
 * @param  axis_length 坐标轴像素长度
 * @param  min_hz      最低频率 (Hz)
 * @param  scale_mode  0 = 线性分布，非零 = 对数分布
 * @param  invert      非零 = 反转方向
 * @return 对应的 bin 索引
 */
uint16_t App_Spectrum_PanelAxisToBin(uint16_t axis_px,
                                     uint16_t axis_px0,
                                     uint16_t axis_length,
                                     float min_hz,
                                     uint8_t scale_mode,
                                     uint8_t invert)
{
    float max_hz = App_Spectrum_BinToHz((uint16_t)(APP_SPECTRUM_BIN_COUNT - 1u));
    float clamped_min_hz = min_hz;
    float norm;
    float hz;

    if ((axis_length <= 1u) || (APP_SPECTRUM_BIN_COUNT <= 1u))
    {
        return 0u;
    }

    if (clamped_min_hz < DELTA_F)
    {
        clamped_min_hz = DELTA_F;
    }
    if (clamped_min_hz > max_hz)
    {
        clamped_min_hz = max_hz;
    }

    if (axis_px <= axis_px0)
    {
        norm = 0.0f;
    }
    else
    {
        uint32_t axis_local = (uint32_t)axis_px - (uint32_t)axis_px0;
        uint32_t span = (uint32_t)axis_length - 1u;

        if (axis_local >= span)
        {
            norm = 1.0f;
        }
        else
        {
            norm = (float)axis_local / (float)span;
        }
    }

    if (invert != 0u)
    {
        norm = 1.0f - norm;
    }

    if (scale_mode != 0u)
    {
        float log_min = logf(clamped_min_hz);
        float log_max = logf(max_hz);

        hz = expf(log_min + ((log_max - log_min) * norm));
    }
    else
    {
        hz = clamped_min_hz + ((max_hz - clamped_min_hz) * norm);
    }

    return App_Spectrum_HzToBin(hz);
}

/**
 * @brief  将面板 X 轴像素位置转换为频谱 bin（线性分布，不反转）
 * @param  x_px       X 像素坐标
 * @param  plot_x0    绘图区域起始 X
 * @param  plot_width 绘图区域宽度
 * @return 对应的 bin 索引
 */
uint16_t App_Spectrum_PanelXToBin(uint16_t x_px, uint16_t plot_x0, uint16_t plot_width)
{
    return App_Spectrum_PanelAxisToBin(x_px, plot_x0, plot_width, DELTA_F, 0u, 0u);
}
