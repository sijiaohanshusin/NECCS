/**
 * @file    app_audio_task.c
 * @brief   音频处理流水线 FreeRTOS 任务
 * @details 本模块实现从 DMA 半缓冲事件到声源定位结果输出的完整音频处理流水线。
 *
 * 处理流程:
 *   1. 等待 DMA 半缓冲完成事件 (PING/PONG 双缓冲)
 *   2. 丢帧检测 (通过 ISR 序号跳变判断)
 *   3. 解交织 + 类型转换 (q15 交织 -> float 平面)
 *   4. FFT 频域变换
 *   5. SRP-PHAT 声源定位
 *   6. 将定位结果投递到 UI 任务队列
 *
 * 抽帧机制:
 *   可通过 CLI 命令 'cfg algodecim N' 动态调整算法执行频率,
 *   非执行帧复用上次定位结果, 降低 CPU 负载的同时保证 UI 不饿死。
 *
 * @note    本任务的优先级高于 UI 任务, 确保音频数据不被丢弃。
 */
#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_runtime.h"
#include "app_spectrum.h"
#include "app_task_cfg.h"

/* ============================================================================
 * 外部变量 (External Variables)
 * ============================================================================ */

/** @brief 调试计数: 每处理一帧音频递增一次, 可通过调试器观察算法实际执行次数 */
extern int16_t found_val;

/** @brief 多声源定位结果（音频任务写入，UI/显示任务读取） */
Sound_MultiPos_t g_multi_source;

/**
 * @brief   将无符号整数限制在 [lo, hi] 范围内
 * @param   v   待限制的值
 * @param   lo  下限
 * @param   hi  上限
 * @return  限制后的值: v < lo 返回 lo, v > hi 返回 hi, 否则返回 v
 */
static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)           /* 低于下限: 直接返回下限值 */
    {
        return lo;
    }
    if (v > hi)           /* 高于上限: 直接返回上限值 */
    {
        return hi;
    }
    return v;             /* 在范围内: 原值返回 */
}

/**
 * @brief   音频处理主任务 (FreeRTOS 入口)
 * @details 处理流程: DMA 半缓冲事件 -> 解交织 -> FFT -> SRP-PHAT -> 结果投递。
 *
 * 关键点:
 * - 通过 event.seq 检测事件跳变并累计异常计数。
 * - 使用 audio_algo_decim 实现算法降采样, 非执行帧复用上次定位结果。
 * - 复用 Mic_Freq_Buffer 作为临时 q15 平面缓冲, 减少额外 RAM 占用。
 *
 * 调试输出:
 * - DEBUG_MODE=0: 输出各通道 RMS。
 * - DEBUG_MODE=1: 输出指定通道 FFT 频谱幅度。
 * - DEBUG_MODE=3: 输出 SRP 定位结果 (角度 + 能量)。
 *
 * @param   pvParameters  FreeRTOS 任务参数 (未使用)
 */
void Audio_Pipeline_Task(void *pvParameters)
{
    (void)pvParameters;  /* 任务参数未使用, 显式转换避免编译器 -Wunused-parameter 警告 */

    /* ---- 任务局部状态变量 (persistent across iterations) ---- */

    /** @brief 已执行解交织的总帧数 (用于 DEBUG 节流判断) */
    static uint32_t s_frame_cnt = 0u;

    /** @brief 上一个接收到的 ISR 帧序号, 用于检测跳变 (丢帧检测) */
    uint32_t s_last_seq = 0u;

    /** @brief 抽帧相位计数 (0 ~ decim-1 循环), phase==0 时执行算法 */
    uint32_t s_decim_phase = 0u;

    /** @brief 本轮 (含抽帧复用) 要送往 UI 的声源位置 */
    Sound_Pos_t current_pos   = {0.0f, 0.0f, 0.0f};

    /** @brief 上次 SRP-PHAT 算法计算得到的声源位置 (抽帧时复用) */
    Sound_Pos_t last_algo_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 是否已有至少一次有效算法结果 (首帧强制执行算法, 不抽帧) */
    uint8_t has_last_algo = 0u;

    /* ---- 帧处理工作变量 ---- */

    Audio_FrameEvent_t event;  /* 从队列接收的 DMA 半缓冲事件 */

    /** @brief 指向当前 DMA 半缓冲的起始地址 (PING 或 PONG 区) */
    q15_t *p_current_dma_src;

    /** @brief 解交织临时缓冲区, 复用 Mic_Freq_Buffer (节省 RAM)
     *         Mic_Freq_Buffer 存放 FFT 频域数据, 但在解交织阶段频域计算尚未开始,
     *         因此可以临时借用; FFT 阶段会用 FFT 结果覆盖此区域 */
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    /* ================================================================
     * 任务主循环 (永不退出)
     * ================================================================ */
    for (;;)
    {
        /* ---- 阶段 1: 等待 DMA 半缓冲完成事件 ---- */
        /* portMAX_DELAY = 永久阻塞直到有数据, 不占用 CPU */
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;  /* 理论上不会到达 (portMAX_DELAY 下不会超时), 保留作为防御代码 */
        }

        /* ---- 阶段 2: 丢帧检测 (通过序号跳变判断) ---- */
        /* event.seq 由 ISR 侧单调递增写入; 若本次 seq > last_seq+1, 说明有帧被覆盖 */
        if ((s_last_seq != 0u) && (event.seq > (s_last_seq + 1u)))
        {
            /* 累计跳变量 (可能跳多帧, 如队列在两次 ISR 之间未被消费) */
            g_audio_both_flags_count += (event.seq - s_last_seq - 1u);
        }
        s_last_seq = event.seq;  /* 更新上次序号基准 */

        /* ---- 阶段 3: 根据 half_id 选择正确的 DMA 缓冲区地址 ---- */
        if (event.half_id == AUDIO_DMA_HALF_PING)
        {
            /* PING 区: DMA 缓冲区前半段, 偏移 0 */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if (event.half_id == AUDIO_DMA_HALF_PONG)
        {
            /* PONG 区: DMA 缓冲区后半段, 偏移 MIC_CHANNELS * FRAME_LEN */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }
        else
        {
            /* half_id 为非法值 (理论上不应发生), 记录异常并跳过本帧 */
            g_audio_no_flag_count++;
            continue;
        }

        /* ---- 阶段 4: 算法抽帧决策 ---- */
        {
            /* 读取当前抽帧比 (运行时可通过 CLI 'cfg algodecim N' 修改) */
            uint32_t decim = s_clamp_u32(App_RuntimeConfig_GetAudioAlgoDecim(),
                                         AUDIO_ALGO_DECIM_MIN,
                                         AUDIO_ALGO_DECIM_MAX);

            /* run_algo 决策:
             *   - 若从未执行过算法 (首帧), 强制执行 (避免 UI 显示全零位置)
             *   - 否则, 仅在相位为 0 时执行 (每 decim 帧执行一次) */
            uint8_t run_algo = (has_last_algo == 0u) ? 1u
                             : ((s_decim_phase == 0u) ? 1u : 0u);

            /* 推进相位计数 (0 -> 1 -> ... -> decim-1 -> 0 循环) */
            s_decim_phase++;
            if (s_decim_phase >= decim)
            {
                s_decim_phase = 0u;  /* 回绕到 0, 下次将再次执行算法 */
            }

            if (run_algo != 0u)
            {
                /* ============================================================
                 * 执行完整算法流水线: 解交织 -> FFT -> SRP-PHAT
                 * ============================================================ */
                uint32_t t_audio = App_Perf_BeginCycles();  /* 开始整体计时 */
                uint32_t t_sec;                              /* 各子段计时起点 */

                found_val++;  /* 调试计数: 统计实际执行算法的帧数 (可通过调试器观察) */

                /* ---- 子阶段 A: 解交织 + 类型转换 ---- */
                /* 将 DMA 交织格式 (ch0_s0, ch1_s0, ..., ch0_s1, ch1_s1, ...)
                 * 转换为平面格式 (ch0_s0..ch0_sN, ch1_s0..ch1_sN, ...)
                 * 同时完成 q15 -> float 类型转换并存入 Mic_Process_Buffer */
                t_sec = App_Perf_BeginCycles();
                Deinterleave_Using_Matrix(p_current_dma_src,  /* 源: 交织 DMA 缓冲 */
                                          p_temp_planar,       /* 临时平面 q15 缓冲 */
                                          Mic_Process_Buffer,  /* 目标: float 平面缓冲 */
                                          FRAME_LEN,           /* 每通道采样点数 */
                                          MIC_CHANNELS);       /* 麦克风通道数 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_DEINT, t_sec);

                s_frame_cnt++;  /* 递增解交织帧计数 (用于 DEBUG 节流) */

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 0)
                /* DEBUG 模式 0: 每 DEBUG_THROTTLE_FRAMES 帧输出一次 RMS 数据到 VOFA+ */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_Channel_RMS();  /* 发送各通道 RMS 到串口波形工具 */
                }
#endif
#endif

                /* ---- 子阶段 B: FFT 频域变换 ---- */
                /* 对 Mic_Process_Buffer 中的各通道时域信号执行 FFT,
                 * 结果写入 Mic_Freq_Buffer (覆盖了解交织阶段的临时数据) */
                t_sec = App_Perf_BeginCycles();
                AI_FFT_Process();  /* 内部使用 CMSIS-DSP arm_cfft_q15, 加窗 + 变换 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_FFT, t_sec);
                App_Spectrum_PublishFromFft(event.seq);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 1)
                /* DEBUG 模式 1: 输出指定通道的 FFT 频谱幅度 */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
                }
#endif
#endif

                /* ---- 子阶段 C: SRP-PHAT 声源定位 ---- */
                /* 基于频域互功率谱相位变换计算声源方位角,
                 * 结果 (x_angle, y_angle, energy) 写入 current_pos */
                t_sec = App_Perf_BeginCycles();
                AI_SRP_PHAT_Process(&current_pos);  /* 核心算法, 最耗时的部分 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_SRP, t_sec);

                /* 提取多声源 Top-K 结果（供显示层使用） */
                AI_SRP_GetMultiSource(&g_multi_source);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 3)
                /* DEBUG 模式 3: 输出 SRP 定位结果 (角度 + 能量) 到 VOFA+ */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_SRP_Result(&current_pos);
                }
#endif
#endif

                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_TOTAL, t_audio); /* 结束整体计时 */

                last_algo_pos = current_pos;  /* 缓存本次结果, 供抽帧时复用 */
                has_last_algo = 1u;           /* 标记已有有效算法结果 */
                App_Perf_CountAudioProc();    /* 递增算法处理帧计数 (性能速率统计用) */
            }
            else
            {
                /* 抽帧阶段: 跳过算法, 直接复用上次结果 */
                /* 这样 UI 任务仍能收到数据 (不会饿死), 只是位置更新频率降低 */
                current_pos = last_algo_pos;
            }
        }

        /* ---- 阶段 5: 将定位结果投递到 UI 任务 ---- */
        /* xQueueOverwrite: 若队列已满 (UI 任务还未消费), 直接覆盖旧数据,
         * 保证 UI 始终拿到最新位置, 不因队列满而阻塞音频任务 */
        xQueueOverwrite(xPositionQueue, &current_pos);
        taskYIELD();

        /* 主动出让 CPU, 让同优先级的 UI 任务有机会立即运行处理刚投递的数据 */
    }
}