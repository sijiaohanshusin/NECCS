/**
 * @file    ai_beamforming.c
 * @brief   声学波束成形与 SRP-PHAT 声源定位算法实现
 * @details 实现基于相位变换的可控响应功率 (SRP-PHAT) 声源定位算法
 *
 * 算法原理：
 * 1. GCC-PHAT (广义互相关相位变换)
 *    - 计算麦克风对之间的互相关
 *    - PHAT 白化：除以幅度，保留相位信息
 *    - 优点：抑制混响，增强直达声
 *
 * 2. SRP (可控响应功率)
 *    - 在空间网格上搜索能量最大的方向
 *    - 累加所有麦克风对的 GCC-PHAT 响应
 *    - 最大值对应声源方向
 *
 * 3. 粗搜 + 精搜策略
 *    - 粗搜：9x9 网格 (+-60 deg, 步长 15 deg)，快速定位
 *    - 精搜：Top-3 峰值周围 4x4 网格 (+-10 deg, 步长 5 deg)，精确定位
 *    - 总扫描点：81 (粗搜) + 48 (精搜) = 129 点
 *
 * 性能优化：
 * - 相位旋转优化：每对麦克风仅 4 次三角函数调用
 * - LUT 加速：粗搜使用预计算 TDOA 表
 * - SIMD 加速：CMSIS-DSP 复数运算
 */

#include "ai_beamforming.h"

#include "ai_config.h"
#include "ai_srp_lut.h"
#include "app_data_stream.h"

#include <math.h>
#include <string.h>

/* ============================================================================
 * 全局诊断变量 (Global Diagnostic Variables)
 * ============================================================================ */

/** @brief 无效结果计数器 (NaN 或索引越界) */
volatile uint32_t g_srp_invalid_count = 0u;

/** @brief 低对比度结果计数器 (质量低于门限) */
volatile uint32_t g_srp_low_contrast_count = 0u;

/** @brief 上次计算的对比度 (用于调试) */
volatile float32_t g_srp_last_contrast = 0.0f;

/** @brief 上次计算的质量指标 (用于调试) */
volatile float32_t g_srp_last_quality = 0.0f;

/** @brief 可视化帧发布计数 */
volatile uint32_t g_srp_vis_publish_count = 0u;

/** @brief 可视化快照重试计数 */
volatile uint32_t g_srp_vis_snapshot_retry_count = 0u;

/** @brief 上次峰值索引 (内部缓存) */
static volatile uint32_t s_srp_last_peak_idx = 0u;

/** @brief 上次峰值功率 (内部缓存) */
static volatile float32_t s_srp_last_peak_value = 0.0f;

/* ============================================================================
 * 静态变量 (Static Variables)
 * ============================================================================ */

/** @brief 是否有上次有效结果 (0=无, 1=有) */
static uint8_t s_has_last_valid = 0u;

/** @brief 上次有效的声源位置 (用于低置信度时保持) */
static Sound_Pos_t s_last_valid = {0.0f, 0.0f, 0.0f};

/** @brief 连续低置信度帧计数 (用于混合策略) */
static uint32_t s_low_conf_streak = 0u;

/** @brief 精搜角度表 (水平角，动态生成) */
static float32_t s_fine_theta_table[FINE_TOTAL];

/** @brief 精搜角度表 (垂直角，动态生成) */
static float32_t s_fine_phi_table[FINE_TOTAL];

/** @brief 可视化双缓冲 (无锁发布) */
static SRP_VisFrame_t s_vis_publish_buffers[2];

/** @brief 当前可视化发布索引 (0 或 1) */
static volatile uint8_t s_vis_publish_index = 0u;

/** @brief 可视化发布序列号 (奇数=写入中, 偶数=稳定) */
static volatile uint32_t s_vis_publish_seq = 0u;

/* ---- 动态频段过滤 ---- */
/** @brief 活动频率 bin 起始（FFT 空间，默认 = 编译期值） */
static uint16_t s_active_bin_start = (uint16_t)SRP_FREQ_BIN_START;
/** @brief 活动频率 bin 结束（FFT 空间，默认 = 编译期值） */
static uint16_t s_active_bin_end   = (uint16_t)SRP_FREQ_BIN_END;

/**
 * @brief   填充可视化帧数据
 * @details 将当前 SRP 功率网格和角度信息复制到可视化帧结构体
 *
 * @param   frame  输出可视化帧指针
 */
static void s_fill_visualization_frame(SRP_VisFrame_t *frame)
{
    uint32_t i;

    if (frame == NULL)
    {
        return;
    }

    /* 复制功率数据 */
    memcpy(frame->power, SRP_Power, sizeof(float32_t) * SRP_GRID_TOTAL);

    /* 填充粗搜角度 */
    for (i = 0u; i < COARSE_TOTAL; i++)
    {
        uint32_t ti = i / COARSE_GRID_SIZE;
        uint32_t pi = i % COARSE_GRID_SIZE;
        frame->theta_deg[i] = coarse_theta_deg[ti];
        frame->phi_deg[i] = coarse_phi_deg[pi];
    }

    /* 填充精搜角度 */
    for (i = 0u; i < FINE_TOTAL; i++)
    {
        frame->theta_deg[COARSE_TOTAL + i] = s_fine_theta_table[i];
        frame->phi_deg[COARSE_TOTAL + i] = s_fine_phi_table[i];
    }

    /* 设置峰值信息 */
    frame->peak_idx = (uint32_t)s_srp_last_peak_idx;
    frame->peak_value = (float32_t)s_srp_last_peak_value;

    /* 安全回退：如果缓存的峰值无效，重新搜索 */
    if ((!isfinite(frame->peak_value)) || (frame->peak_idx >= SRP_GRID_TOTAL))
    {
        arm_max_f32(frame->power, SRP_GRID_TOTAL, &frame->peak_value, &frame->peak_idx);
    }
}

/**
 * @brief   发布可视化帧 (无锁双缓冲)
 * @details 将当前 SRP 数据写入非活跃缓冲区，然后原子切换索引
 *
 * 无锁协议：
 * 1. 序列号加 1 (变为奇数，表示正在写入)
 * 2. 填充非活跃缓冲区
 * 3. 切换活跃索引
 * 4. 序列号再加 1 (变为偶数，表示写入完成)
 * - 读取端：检查序列号是否前后一致且为偶数
 */
static void s_publish_visualization_frame(void)
{
    uint8_t next_index = (uint8_t)(s_vis_publish_index ^ 1u);

    __DMB();
    s_vis_publish_seq++;
    __DMB();
    s_fill_visualization_frame(&s_vis_publish_buffers[next_index]);
    __DMB();
    s_vis_publish_index = next_index;
    s_vis_publish_seq++;
    g_srp_vis_publish_count++;
}

/* ============================================================================
 * 编译时检查 (Compile-Time Checks)
 * ============================================================================ */

/** @brief 检查低置信度策略配置是否有效 */
#if (SRP_LOWCONF_POLICY != SRP_LOWCONF_REPORT_NEW) && \
    (SRP_LOWCONF_POLICY != SRP_LOWCONF_HOLD_LAST) && \
    (SRP_LOWCONF_POLICY != SRP_LOWCONF_MIXED)
#error "Invalid SRP_LOWCONF_POLICY"
#endif

/* ============================================================================
 * 辅助函数 (Helper Functions)
 * ============================================================================ */

/**
 * @brief   判断两个粗搜索点是否为邻居
 * @details 用于 NMS (非极大值抑制) 算法
 *
 * 邻居定义：
 * - 在 9x9 网格中，曼哈顿距离 <= SRP_TOPK_NMS_RADIUS
 * - 例如：(3,4) 和 (3,5) 是邻居 (距离=1)
 * - 例如：(3,4) 和 (5,6) 不是邻居 (距离=3)
 *
 * @param   a  粗搜索点索引 A (0-80)
 * @param   b  粗搜索点索引 B (0-80)
 * @return  1: 是邻居, 0: 不是邻居
 */
static uint8_t coarse_idx_is_neighbor(uint32_t a, uint32_t b)
{
    /* 将一维索引转换为二维坐标 (行, 列) */
    uint32_t ai = a / COARSE_GRID_SIZE;  /* A 的行索引 (theta) */
    uint32_t ap = a % COARSE_GRID_SIZE;  /* A 的列索引 (phi) */
    uint32_t bi = b / COARSE_GRID_SIZE;  /* B 的行索引 (theta) */
    uint32_t bp = b % COARSE_GRID_SIZE;  /* B 的列索引 (phi) */

    /* 计算曼哈顿距离 */
    uint32_t dti = (ai > bi) ? (ai - bi) : (bi - ai);  /* 行距离 */
    uint32_t dpi = (ap > bp) ? (ap - bp) : (bp - ap);  /* 列距离 */

    /* 判断是否在邻域半径内 */
    return (uint8_t)((dti <= SRP_TOPK_NMS_RADIUS) && (dpi <= SRP_TOPK_NMS_RADIUS));
}

/**
 * @brief   带 NMS 的 Top-K 选择算法
 * @details 从粗搜索结果中选择 Top-K 个峰值，同时抑制相邻峰值
 *
 * NMS (Non-Maximum Suppression) 原理：
 * - 选择最大值后，排除其邻域内的点
 * - 避免选择相邻的峰值 (可能是同一个声源的旁瓣)
 * - 确保 Top-K 峰值分散在不同区域
 *
 * 算法流程：
 * 1. 找到未使用点中的最大值
 * 2. 检查该点是否与已选点相邻
 * 3. 如果不相邻，选择该点；否则跳过
 * 4. 重复直到选够 K 个点
 * 5. 如果无法选够 K 个点，放宽限制继续选择
 *
 * @param   arr      粗搜索功率数组 (COARSE_TOTAL 个点)
 * @param   top_idx  输出 Top-K 索引数组 (长度 k)
 * @param   k        需要选择的峰值数量 (通常为 3)
 */
static void find_top_k_indices_nms(const float32_t *arr, uint32_t *top_idx, uint32_t k)
{
    uint8_t used[COARSE_TOTAL] = {0};  /* 标记已使用的点 */
    uint32_t chosen = 0u;              /* 已选择的点数 */

    /* 选择 K 个峰值 */
    for (uint32_t s = 0u; s < k; s++)
    {
        float32_t best_val = -1.0e30f;  /* 当前最大值 */
        uint32_t best_idx = 0u;         /* 当前最大值索引 */
        uint8_t found = 0u;             /* 是否找到有效点 */

        /* 第一轮：在非邻域点中找最大值 */
        for (uint32_t n = 0u; n < COARSE_TOTAL; n++)
        {
            /* 跳过已使用的点 */
            if (used[n] != 0u)
            {
                continue;
            }

            /* 检查是否与已选点相邻 */
            uint8_t allow = 1u;
            for (uint32_t c = 0u; c < chosen; c++)
            {
                if (coarse_idx_is_neighbor(n, top_idx[c]) != 0u)
                {
                    allow = 0u;  /* 相邻，不允许选择 */
                    break;
                }
            }
            if ((allow == 0u) || (arr[n] <= best_val))
            {
                continue;
            }

            /* 更新最大值 */
            best_val = arr[n];
            best_idx = n;
            found = 1u;
        }

        /* 第二轮：如果第一轮没找到，放宽限制 (允许邻域点) */
        if (found == 0u)
        {
            for (uint32_t n = 0u; n < COARSE_TOTAL; n++)
            {
                if ((used[n] == 0u) && (arr[n] > best_val))
                {
                    best_val = arr[n];
                    best_idx = n;
                    found = 1u;
                }
            }
        }

        /* 记录选择的点 */
        top_idx[s] = best_idx;
        if (found != 0u)
        {
            used[best_idx] = 1u;
            chosen++;
        }
    }
}

/**
 * @brief   获取网格点的角度
 * @details 根据索引查询粗搜或精搜的角度
 *
 * 索引范围：
 * - [0, COARSE_TOTAL): 粗搜网格，查表 coarse_theta_deg/coarse_phi_deg
 * - [COARSE_TOTAL, COARSE_TOTAL+FINE_TOTAL): 精搜网格，查表 s_fine_theta_table/s_fine_phi_table
 *
 * @param   idx        网格点索引 (0-128)
 * @param   theta_deg  输出水平角 (度)
 * @param   phi_deg    输出垂直角 (度)
 */
static void get_grid_angle(uint32_t idx, float32_t *theta_deg, float32_t *phi_deg)
{
    /* 粗搜网格 */
    if (idx < COARSE_TOTAL)
    {
        uint32_t ti = idx / COARSE_GRID_SIZE;  /* 行索引 (theta) */
        uint32_t pi = idx % COARSE_GRID_SIZE;  /* 列索引 (phi) */
        *theta_deg = coarse_theta_deg[ti];
        *phi_deg = coarse_phi_deg[pi];
        return;
    }

    /* 精搜网格 */
    idx -= COARSE_TOTAL;
    if (idx < FINE_TOTAL)
    {
        *theta_deg = s_fine_theta_table[idx];
        *phi_deg = s_fine_phi_table[idx];
        return;
    }

    /* 越界保护 */
    *theta_deg = 0.0f;
    *phi_deg = 0.0f;
}

/**
 * @brief   计算全局次大值 (不排除邻域)
 * @details 用于计算原始对比度
 *
 * @param   max_idx  最大值索引
 * @return  次大值
 */
static float32_t compute_second_max_all(uint32_t max_idx)
{
    float32_t second_max = -1.0e30f;

    /* 遍历所有点，找次大值 */
    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if ((i != max_idx) && (SRP_Power[i] > second_max))
        {
            second_max = SRP_Power[i];
        }
    }

    return second_max;
}

/**
 * @brief   计算排除邻域的次大值
 * @details 用于计算质量指标，排除最大值周围的旁瓣
 *
 * 邻域定义：
 * - 角度距离 <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG (默认 5 deg)
 * - 排除旁瓣干扰，更准确评估峰值质量
 *
 * @param   max_idx  最大值索引
 * @return  排除邻域后的次大值
 */
static float32_t compute_second_max_excluding_neighbor(uint32_t max_idx)
{
    float32_t max_theta, max_phi;
    float32_t second_max = -1.0e30f;
    uint8_t found = 0u;

    /* 获取最大值的角度 */
    get_grid_angle(max_idx, &max_theta, &max_phi);

    /* 遍历所有点，找排除邻域后的次大值 */
    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if (i == max_idx)
        {
            continue;
        }

        float32_t theta, phi;
        get_grid_angle(i, &theta, &phi);

        /* 排除邻域内的点 */
        if ((fabsf(theta - max_theta) <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG) &&
            (fabsf(phi - max_phi) <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG))
        {
            continue;
        }

        /* 更新次大值 */
        if (SRP_Power[i] > second_max)
        {
            second_max = SRP_Power[i];
            found = 1u;
        }
    }

    /* 如果没找到 (所有点都在邻域内)，回退到全局次大值 */
    if (found == 0u)
    {
        return compute_second_max_all(max_idx);
    }

    return second_max;
}

/**
 * @brief   应用低置信度能量软上限
 * @details 限制低置信度结果的能量值，避免误导
 *
 * @param   energy  原始能量
 * @return  处理后的能量
 */
static float32_t apply_lowconf_energy(float32_t energy)
{
#if (SRP_ENABLE_ENERGY_SOFTCAP != 0u)
    /* 如果能量超过上限，截断到上限 */
    if (energy > SRP_AMBIGUOUS_ENERGY_MAX)
    {
        return SRP_AMBIGUOUS_ENERGY_MAX;
    }
#endif
    return energy;
}

/**
 * @brief   输出角度重映射
 * @details 根据安装方向调整输出角度
 *
 * 支持的变换：
 * - 交换 X/Y 轴 (旋转 90 deg)
 * - 反转 X 轴 (镜像翻转)
 * - 反转 Y 轴 (镜像翻转)
 *
 * @param   x_angle  输入/输出水平角 (度)
 * @param   y_angle  输入/输出垂直角 (度)
 */
static void remap_output_angles(float32_t *x_angle, float32_t *y_angle)
{
#if (SRP_OUTPUT_SWAP_XY != 0u)
    /* 交换 X/Y 轴 */
    float32_t tmp = *x_angle;
    *x_angle = *y_angle;
    *y_angle = tmp;
#endif

#if (SRP_OUTPUT_INVERT_X != 0u)
    /* 反转 X 轴 */
    *x_angle = -*x_angle;
#endif

#if (SRP_OUTPUT_INVERT_Y != 0u)
    /* 反转 Y 轴 */
    *y_angle = -*y_angle;
#endif
}

/* ============================================================================
 * 核心算法函数实现 (Core Algorithm Implementation)
 * ============================================================================ */

/**
 * @brief   FFT 频域变换处理
 * @details 对 16 路麦克风信号进行频域变换，为 SRP-PHAT 准备数据
 *
 * 处理流程：
 * 1. 去直流：计算均值并减去 (arm_mean_f32 + arm_offset_f32)
 *    - 原因：直流分量会干扰低频 bin，影响定位精度
 *    - 方法：计算帧均值，逐点减去
 *
 * 2. 加窗：乘以汉宁窗，减少频谱泄漏 (arm_mult_f32)
 *    - 原因：矩形窗会产生严重的频谱泄漏
 *    - 方法：逐点乘以预计算的汉宁窗函数
 *    - 效果：主瓣变宽，旁瓣降低 40dB
 *
 * 3. RFFT：实数快速傅里叶变换 (arm_rfft_fast_f32)
 *    - 算法：Cooley-Tukey FFT (基 2 分治)
 *    - 复杂度：O(N log N) = O(256 x 8) = 2048 次运算
 *    - 优化：SIMD 指令并行处理 4 个点
 *
 * 输入数据：Mic_Process_Buffer (16ch x 256 点 float32, DTCM)
 * 输出数据：Mic_Freq_Buffer (16ch x 256 点复数, DTCM)
 *
 * @note    耗时：约 0.8ms @ 480MHz (16ch x 256 点)
 *          - 去直流：约 0.1ms
 *          - 加窗：约 0.1ms
 *          - RFFT：约 0.6ms
 */
void AI_FFT_Process(void)
{
    float32_t mean_val;  /* 帧均值 (直流分量) */

    /* 对每个通道进行 FFT 处理 */
    for (uint32_t ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        /* 指向当前通道的时域数据 */
        float32_t *p_time = &Mic_Process_Buffer[ch * FRAME_LEN];

        /* 指向当前通道的频域数据 */
        float32_t *p_freq = &Mic_Freq_Buffer[ch * FRAME_LEN];

        /* 1. 去直流：计算均值 */
        /* arm_mean_f32: 累加所有点，除以点数 */
        arm_mean_f32(p_time, FRAME_LEN, &mean_val);

        /* 2. 去直流：减去均值 */
        /* arm_offset_f32: 逐点减去 mean_val */
        arm_offset_f32(p_time, -mean_val, p_time, FRAME_LEN);

        /* 3. 加窗：逐点乘以汉宁窗 */
        /* arm_mult_f32: p_time[i] *= Hanning_Window[i] */
        arm_mult_f32(p_time, Hanning_Window, p_time, FRAME_LEN);

        /* 4. RFFT：时域 -> 频域 */
        /* arm_rfft_fast_f32: 256 点实数 FFT */
        /* ifftFlag=0: 正向 FFT (时域 -> 频域) */
        arm_rfft_fast_f32(&S_Rfft, p_time, p_freq, 0);
    }
}

/**
 * @brief   GCC-PHAT 计算
 * @details 计算所有麦克风对的广义互相关相位变换
 *
 * GCC-PHAT 原理：
 * - GCC (Generalized Cross-Correlation): 广义互相关
 *   R_ij(f) = X_i(f) * conj(X_j(f))
 *
 * - PHAT (Phase Transform): 相位变换 (白化)
 *   R_ij_PHAT(f) = R_ij(f) / |R_ij(f)|
 *
 * - 作用：
 *   1. 保留相位信息 (时延差)
 *   2. 抑制幅度信息 (混响、噪声)
 *   3. 增强直达声，提升定位精度
 *
 * 算法流程：
 * 1. 计算互相关：X_i(f) * conj(X_j(f))
 * 2. 计算幅度：|R_ij(f)|
 * 3. 白化：R_ij(f) / (|R_ij(f)| + epsilon)
 *
 * 输入数据：Mic_Freq_Buffer (16ch x 256 点复数, DTCM)
 * 输出数据：GCC_PHAT_Buffer (40 对 x 40 bins x 2, AXI SRAM)
 *
 * @note    耗时：约 1.5ms @ 480MHz (40 对 x 40 bins)
 */
static void SRP_GCC_PHAT_Compute(void)
{
    /* 临时缓冲区 (栈上分配，快速访问) */
    float32_t conj_buf[SRP_FREQ_BINS * 2u];   /* 共轭复数 */
    float32_t cross_buf[SRP_FREQ_BINS * 2u];  /* 互相关 */
    float32_t mag_buf[SRP_FREQ_BINS];         /* 幅度 */

    /* 对每对麦克风计算 GCC-PHAT */
    for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
    {
        /* 获取麦克风对的索引 */
        uint8_t mi = srp_pair_idx[p][0];  /* 麦克风 i */
        uint8_t mj = srp_pair_idx[p][1];  /* 麦克风 j */

        /* 指向麦克风 i 的频域数据 (跳过 DC 和低频 bin) */
        const float32_t *xi = &Mic_Freq_Buffer[(uint32_t)mi * FRAME_LEN + (SRP_FREQ_BIN_START * 2u)];

        /* 指向麦克风 j 的频域数据 */
        const float32_t *xj = &Mic_Freq_Buffer[(uint32_t)mj * FRAME_LEN + (SRP_FREQ_BIN_START * 2u)];

        /* 1. 计算共轭：conj(X_j) = [Re, -Im] */
        /* arm_cmplx_conj_f32: 实部不变，虚部取反 */
        arm_cmplx_conj_f32((float32_t *)xj, conj_buf, SRP_FREQ_BINS);

        /* 2. 计算互相关：X_i * conj(X_j) */
        /* arm_cmplx_mult_cmplx_f32: 复数乘法 */
        /* (a+bi) * (c-di) = (ac+bd) + (bc-ad)i */
        arm_cmplx_mult_cmplx_f32((float32_t *)xi, conj_buf, cross_buf, SRP_FREQ_BINS);

        /* 3. 计算幅度：|R_ij| = sqrt(Re^2 + Im^2) */
        /* arm_cmplx_mag_f32: 复数模 */
        arm_cmplx_mag_f32(cross_buf, mag_buf, SRP_FREQ_BINS);

        /* 4. PHAT 白化：R_ij / (|R_ij| + epsilon) */
        float32_t *p_out = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2u];
        for (uint32_t k = 0u; k < SRP_FREQ_BINS; k++)
        {
            /* 计算倒数 (加 epsilon 防止除零) */
            float32_t inv_mag = 1.0f / (mag_buf[k] + PHAT_EPSILON);

            /* 白化：实部和虚部都除以幅度 */
            p_out[2u * k] = cross_buf[2u * k] * inv_mag;       /* 实部 */
            p_out[2u * k + 1u] = cross_buf[2u * k + 1u] * inv_mag;  /* 虚部 */
        }
    }
}

/**
 * @brief   SRP 累加计算
 * @details 对给定方向累加所有麦克风对的 GCC-PHAT 响应
 *
 * SRP 原理：
 * - 假设声源在方向 (theta, phi)
 * - 计算每对麦克风的理论时延差 tau
 * - 将 GCC-PHAT 响应旋转 tau 对应的相位
 * - 累加所有对的响应，得到该方向的功率
 *
 * 相位旋转公式：
 * - 频率 f_k 的相位旋转：exp(j * 2*pi * f_k * tau)
 * - 递推计算：exp(j * 2*pi * f_k * tau) = exp(j * 2*pi * f_0 * tau) * [exp(j * 2*pi * df * tau)]^k
 * - 优化：每对麦克风仅 4 次三角函数调用 (sin_d, cos_d, sin_phi, cos_phi)
 *
 * 算法流程：
 * 1. 计算相位增量：d_phi = 360 deg * df * tau
 * 2. 计算初始相位：phi_0 = d_phi * f_start
 * 3. 递推计算：phi_k = phi_{k-1} + d_phi
 * 4. 累加响应：power += GCC[k] * exp(j * phi_k)
 *
 * @param   tau  时延差数组 (SRP_PAIR_COUNT 个元素，单位：秒)
 * @return  该方向的 SRP 功率
 *
 * @note    耗时：约 0.03ms @ 480MHz (40 对 x 40 bins)
 */
static float32_t SRP_Accumulate_Point(const float32_t *tau)
{
    float32_t power = 0.0f;  /* 累加功率 */

    /* 活动频段在 GCC-PHAT 缓冲区中的偏移与长度 */
    uint32_t local_start = (uint32_t)s_active_bin_start - (uint32_t)SRP_FREQ_BIN_START;
    uint32_t local_end   = (uint32_t)s_active_bin_end   - (uint32_t)SRP_FREQ_BIN_START;
    uint32_t active_bins = local_end - local_start + 1u;

    /* 对每对麦克风累加响应 */
    for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
    {
        /* 指向当前麦克风对的 GCC-PHAT 数据 */
        const float32_t *gcc = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2u];

        /* 计算相位增量 (度) */
        /* d_phi = 360 deg * df * tau */
        float32_t d_phi_deg = 360.0f * DELTA_F * tau[p];

        /* 计算相位增量的 sin/cos (用于递推) */
        float32_t sin_d, cos_d;
        arm_sin_cos_f32(d_phi_deg, &sin_d, &cos_d);

        /* 计算初始相位 (活动频段起始 bin 的相位) */
        float32_t sin_phi, cos_phi;
        arm_sin_cos_f32(d_phi_deg * (float32_t)s_active_bin_start, &sin_phi, &cos_phi);

        /* 累加活动频率 bin 的响应 */
        float32_t pair_sum = 0.0f;
        for (uint32_t k = 0u; k < active_bins; k++)
        {
            uint32_t idx = local_start + k;

            /* 计算内积：GCC[k] * exp(j * phi_k) */
            pair_sum += gcc[2u * idx] * cos_phi + gcc[2u * idx + 1u] * sin_phi;

            /* 递推计算下一个 bin 的相位 */
            float32_t c_new = cos_phi * cos_d - sin_phi * sin_d;
            float32_t s_new = sin_phi * cos_d + cos_phi * sin_d;
            cos_phi = c_new;
            sin_phi = s_new;
        }

        /* 累加当前麦克风对的响应 */
        power += pair_sum;
    }

    return power;
}

/**
 * @brief   SRP-PHAT 算法初始化
 * @details 初始化统计计数器和历史状态
 *
 * 初始化内容：
 * - 清零诊断计数器
 * - 清零可视化状态
 * - 重置上次有效结果缓存
 * - 重置低置信度连续帧计数
 *
 * @note    在系统启动时调用一次即可 (App_Stream_Init 中)
 */
void AI_SRP_PHAT_Init(void)
{
    /* 清零诊断计数器 */
    g_srp_invalid_count = 0u;
    g_srp_low_contrast_count = 0u;
    g_srp_last_contrast = 0.0f;
    g_srp_last_quality = 0.0f;
    s_srp_last_peak_idx = 0u;
    s_srp_last_peak_value = 0.0f;
    g_srp_vis_publish_count = 0u;
    g_srp_vis_snapshot_retry_count = 0u;
    s_vis_publish_index = 0u;
    s_vis_publish_seq = 0u;
    memset(s_vis_publish_buffers, 0, sizeof(s_vis_publish_buffers));

    /* 重置历史状态 */
    s_has_last_valid = 0u;
    s_last_valid.x_angle = 0.0f;
    s_last_valid.y_angle = 0.0f;
    s_last_valid.energy = 0.0f;
    s_low_conf_streak = 0u;
}

/**
 * @brief   动态设置 GCC-PHAT 活动频率范围
 * @details 限制 SRP-PHAT 只累加指定频率 bin 范围内的响应，可突出特定声源频段。
 *          例如 500Hz-4kHz 对应语音定位，200Hz-1kHz 对应低频机器噪声。
 *
 *          换算关系：频率(Hz) = bin_index × (fs / N) = bin_index × (48000 / 256) ≈ 187.5 × bin_index
 *
 *          输入值会被钳位到编译期宏 [SRP_FREQ_BIN_START, SRP_FREQ_BIN_END] 范围内，
 *          超出范围的值会被静默钳位而不会报错。
 *
 * @param   bin_start  活动起始频率 bin（含，FFT 空间）
 * @param   bin_end    活动结束频率 bin（含，FFT 空间，bin_end >= bin_start）
 */
void AI_SRP_SetActiveFreqRange(uint16_t bin_start, uint16_t bin_end)
{
    /* 钳位到编译期允许范围 */
    if (bin_start < (uint16_t)SRP_FREQ_BIN_START)
    {
        bin_start = (uint16_t)SRP_FREQ_BIN_START;
    }
    if (bin_end > (uint16_t)SRP_FREQ_BIN_END)
    {
        bin_end = (uint16_t)SRP_FREQ_BIN_END;
    }
    if (bin_start > bin_end)
    {
        bin_start = bin_end;
    }
    s_active_bin_start = bin_start;
    s_active_bin_end   = bin_end;
}

/**
 * @brief   获取当前活动频率 bin 范围
 * @param   bin_start  输出起始 bin（若非 NULL）
 * @param   bin_end    输出结束 bin（若非 NULL）
 */
void AI_SRP_GetActiveFreqRange(uint16_t *bin_start, uint16_t *bin_end)
{
    if (bin_start != NULL)
    {
        *bin_start = s_active_bin_start;
    }
    if (bin_end != NULL)
    {
        *bin_end = s_active_bin_end;
    }
}

/**
 * @brief   SRP-PHAT 声源定位处理
 * @details 基于频域数据计算声源方位角和能量
 *
 * 算法流程：
 * 1. GCC-PHAT 计算：对 40 对麦克风计算广义互相关
 * 2. 粗搜：在 9x9 网格 (+-60 deg, 步长 15 deg) 上计算 SRP 功率
 * 3. Top-K 选择：选择前 3 个峰值 (带 NMS 抑制相邻峰值)
 * 4. 精搜：在每个峰值周围 4x4 网格 (+-10 deg, 步长 5 deg) 细化
 * 5. 质量评估：计算对比度和质量指标
 * 6. 结果输出：根据置信度策略输出最终结果
 *
 * 粗搜策略：
 * - 使用预计算 TDOA 查找表 (tdoa_coarse_lut)
 * - 快速扫描 81 个方向，定位大致区域
 * - 耗时：约 2.5ms (81 点 x 0.03ms/点)
 *
 * 精搜策略：
 * - 实时计算 TDOA (因为角度不固定)
 * - 在 Top-3 峰值周围细化，提升精度
 * - 耗时：约 1.5ms (48 点 x 0.03ms/点)
 *
 * 质量评估：
 * - 对比度 (contrast): (最大值 - 次大值) / 最大值
 * - 质量 (quality): (最大值 - 远处次大值) / 最大值
 * - 低置信度：quality < SRP_CONTRAST_MIN_RATIO
 *
 * 低置信度处理策略：
 * - REPORT_NEW: 报告新结果 (实时响应)
 * - HOLD_LAST: 保持上次有效结果 (平滑输出)
 * - MIXED: 短期保持，长期更新 (折中方案)
 *
 * @param   result  输出声源位置结构体指针
 *                  - x_angle: 水平角度 (度)
 *                  - y_angle: 垂直角度 (度)
 *                  - energy: 归一化能量 [0, 1]
 *
 * @note    耗时：约 4ms @ 480MHz (1.5ms GCC-PHAT + 2.5ms 粗搜 + 1.5ms 精搜)
 * @note    必须在 AI_FFT_Process() 完成后调用
 */
void AI_SRP_PHAT_Process(Sound_Pos_t *result)
{
    /* 参数检查 */
    if (result == NULL)
    {
        return;
    }

    /* 临时变量 */
    float32_t tau_buf[SRP_PAIR_COUNT];  /* TDOA 缓冲区 */
    const float32_t fine_step = (2.0f * FINE_SPAN_DEG) / (float32_t)FINE_GRID_SIZE;
    const float32_t inv_c = 1.0f / SPEED_OF_SOUND;  /* 声速倒数 (预计算) */

    /* ========== 步骤 1: GCC-PHAT 计算 ========== */
    /* 计算所有麦克风对的广义互相关相位变换 */
    SRP_GCC_PHAT_Compute();

    /* ========== 步骤 2: 粗搜 ========== */
    /* 在 9x9 网格上扫描，使用预计算 TDOA 表 */
    for (uint32_t g = 0u; g < COARSE_TOTAL; g++)
    {
        /* 查表获取 TDOA，累加 SRP 功率 */
        SRP_Power[g] = SRP_Accumulate_Point(tdoa_coarse_lut[g]);
    }

    /* ========== 步骤 3: Top-K 选择 ========== */
    /* 选择前 3 个峰值，带 NMS 抑制相邻峰值 */
    uint32_t top_idx[FINE_TOP_K];
    find_top_k_indices_nms(SRP_Power, top_idx, FINE_TOP_K);

    /* ========== 步骤 4: 精搜 ========== */
    /* 在 Top-3 峰值周围进行 4x4 精细扫描 */
    for (uint32_t t = 0u; t < FINE_TOP_K; t++)
    {
        /* 获取粗搜峰值的索引和角度 */
        uint32_t coarse_g = top_idx[t];
        uint32_t ti = coarse_g / COARSE_GRID_SIZE;  /* 行索引 (theta) */
        uint32_t pi = coarse_g % COARSE_GRID_SIZE;  /* 列索引 (phi) */

        float32_t center_theta = coarse_theta_deg[ti];  /* 中心水平角 */
        float32_t center_phi = coarse_phi_deg[pi];      /* 中心垂直角 */

        /* 在中心周围 4x4 网格扫描 */
        for (uint32_t fi = 0u; fi < FINE_GRID_SIZE; fi++)
        {
            /* 计算精搜水平角 */
            float32_t theta_h = center_theta + (-FINE_SPAN_DEG + ((float32_t)fi + 0.5f) * fine_step);
            float32_t sin_th, cos_th_unused;
            arm_sin_cos_f32(theta_h, &sin_th, &cos_th_unused);

            for (uint32_t fj = 0u; fj < FINE_GRID_SIZE; fj++)
            {
                /* 计算全局索引 */
                uint32_t local_idx = fi * FINE_GRID_SIZE + fj;
                uint32_t global_idx = t * FINE_TOTAL_PER_TOP + local_idx;

                /* 计算精搜垂直角 */
                float32_t theta_v = center_phi + (-FINE_SPAN_DEG + ((float32_t)fj + 0.5f) * fine_step);
                float32_t sin_tv, cos_tv;
                arm_sin_cos_f32(theta_v, &sin_tv, &cos_tv);

                /* 记录精搜角度 (用于后续查询) */
                s_fine_theta_table[global_idx] = theta_h;
                s_fine_phi_table[global_idx] = theta_v;

                /* 实时计算 TDOA */
                /* tau = (dx * sin(theta) * cos(phi) + dy * sin(phi)) / c */
                float32_t sin_th_cos_tv = sin_th * cos_tv;
                for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
                {
                    tau_buf[p] = (srp_pair_dx[p] * sin_th_cos_tv + srp_pair_dy[p] * sin_tv) * inv_c;
                }

                /* 累加 SRP 功率 */
                SRP_Power[COARSE_TOTAL + global_idx] = SRP_Accumulate_Point(tau_buf);
            }
        }
    }

    /* ========== 步骤 5: 全局最大值搜索 ========== */
    /* 在粗搜 + 精搜结果中找最大值 */
    float32_t max_val;
    uint32_t max_idx;
    arm_max_f32(SRP_Power, SRP_GRID_TOTAL, &max_val, &max_idx);
    s_srp_last_peak_idx = max_idx;
    s_srp_last_peak_value = max_val;

    /* ========== 步骤 6: 异常处理 ========== */
    /* 检查结果是否有效 (NaN 或索引越界) */
    if ((!isfinite(max_val)) || (max_idx >= SRP_GRID_TOTAL))
    {
        /* 记录无效结果 */
        g_srp_invalid_count++;
        g_srp_last_contrast = 0.0f;
        g_srp_last_quality = 0.0f;
        s_srp_last_peak_idx = 0u;
        s_srp_last_peak_value = 0.0f;

        /* 如果有上次有效结果，衰减输出；否则输出零 */
        if (s_has_last_valid != 0u)
        {
            *result = s_last_valid;
            result->energy *= 0.90f;  /* 衰减 10% */
            remap_output_angles(&result->x_angle, &result->y_angle);
        }
        else
        {
            result->x_angle = 0.0f;
            result->y_angle = 0.0f;
            result->energy = 0.0f;
        }
        s_publish_visualization_frame();
        return;
    }

    /* ========== 步骤 7: 提取候选角度 ========== */
    float32_t cand_x;  /* 候选水平角 */
    float32_t cand_y;  /* 候选垂直角 */

    /* 根据索引判断是粗搜还是精搜结果 */
    if (max_idx < COARSE_TOTAL)
    {
        /* 粗搜结果 */
        uint32_t ti = max_idx / COARSE_GRID_SIZE;
        uint32_t pi = max_idx % COARSE_GRID_SIZE;
        cand_x = coarse_theta_deg[ti];
        cand_y = coarse_phi_deg[pi];
    }
    else
    {
        /* 精搜结果 */
        uint32_t fine_idx = max_idx - COARSE_TOTAL;
        cand_x = s_fine_theta_table[fine_idx];
        cand_y = s_fine_phi_table[fine_idx];
    }

    /* ========== 步骤 8: 能量归一化 ========== */
    /* 归一化到 [0, 1] 范围 */
    float32_t norm = max_val / (float32_t)(SRP_PAIR_COUNT * SRP_FREQ_BINS);
    if (norm > 1.0f)
    {
        norm = 1.0f;  /* 上限截断 */
    }
    if (norm < 0.0f)
    {
        norm = 0.0f;  /* 下限截断 */
    }

    /* ========== 步骤 9: 质量评估 ========== */
    /* 计算对比度和质量指标 */
    float32_t second_max_raw = compute_second_max_all(max_idx);  /* 全局次大值 */
    float32_t second_max_quality = compute_second_max_excluding_neighbor(max_idx);  /* 排除邻域次大值 */

    /* 对比度：(最大值 - 次大值) / 最大值 */
    float32_t contrast_raw = (max_val - second_max_raw) / (fabsf(max_val) + 1.0e-6f);

    /* 质量：(最大值 - 远处次大值) / 最大值 */
    float32_t quality = (max_val - second_max_quality) / (fabsf(max_val) + 1.0e-6f);

    /* 记录诊断信息 */
    g_srp_last_contrast = contrast_raw;
    g_srp_last_quality = quality;

    /* ========== 步骤 10: 置信度判断 ========== */
    /* 判断是否为低置信度结果 */
    uint8_t low_conf = (uint8_t)(quality < SRP_CONTRAST_MIN_RATIO);
    if (low_conf != 0u)
    {
        /* 低置信度：累加计数器 */
        g_srp_low_contrast_count++;
        s_low_conf_streak++;
    }
    else
    {
        /* 高置信度：重置连续计数 */
        s_low_conf_streak = 0u;
    }

    /* ========== 步骤 11: 低置信度策略 ========== */
    /* 根据配置的策略决定是否保持上次结果 */
    uint8_t hold_last = 0u;
#if (SRP_LOWCONF_POLICY == SRP_LOWCONF_HOLD_LAST)
    /* 策略 1: 始终保持上次有效结果 */
    hold_last = (uint8_t)(s_has_last_valid != 0u);
#elif (SRP_LOWCONF_POLICY == SRP_LOWCONF_MIXED)
    /* 策略 2: 短期保持，长期更新 */
    hold_last = (uint8_t)((s_has_last_valid != 0u) && (s_low_conf_streak <= SRP_LOWCONF_MIXED_HOLD_FRAMES));
#endif

    /* ========== 步骤 12: 输出结果 ========== */
    /* 根据置信度和策略选择输出角度 */
    float32_t base_x = ((low_conf != 0u) && (hold_last != 0u)) ? s_last_valid.x_angle : cand_x;
    float32_t base_y = ((low_conf != 0u) && (hold_last != 0u)) ? s_last_valid.y_angle : cand_y;

    /* 应用输出角度重映射 (适配安装方向) */
    remap_output_angles(&base_x, &base_y);

    /* 填充输出结构体 */
    result->x_angle = base_x;
    result->y_angle = base_y;
    result->energy = (low_conf != 0u) ? apply_lowconf_energy(norm) : norm;

    /* ========== 步骤 13: 缓存有效结果 ========== */
    /* 如果是高置信度且满足门限，缓存为上次有效结果 */
    if ((low_conf == 0u) &&
        (result->energy >= SRP_VALID_MIN_ENERGY) &&
        (quality >= SRP_VALID_MIN_QUALITY))
    {
        /* 缓存原始角度 (未重映射) */
        s_last_valid.x_angle = cand_x;
        s_last_valid.y_angle = cand_y;
        s_last_valid.energy = result->energy;
        s_has_last_valid = 1u;
    }

    s_publish_visualization_frame();
}

/**
 * @brief   复制当前可视化帧 (同步调用)
 * @param   frame  输出帧指针
 * @note    直接从当前数据复制，无并发保护，适用于同一任务内调用
 */
void AI_SRP_CopyVisualizationFrame(SRP_VisFrame_t *frame)
{
    s_fill_visualization_frame(frame);
}

/**
 * @brief   获取最新可视化帧 (跨任务安全)
 * @details 使用无锁序列号协议从双缓冲中读取最新帧
 *
 * 读取协议：
 * 1. 读取序列号 start_seq
 * 2. 如果 start_seq 为奇数，说明正在写入，重试
 * 3. 复制缓冲区数据
 * 4. 再次读取序列号 end_seq
 * 5. 如果 start_seq == end_seq 且为偶数，读取成功
 * 6. 最多重试 3 次
 *
 * @param   frame  输出帧指针
 * @return  1: 成功, 0: 失败 (正在写入或参数无效)
 */
uint8_t AI_SRP_GetLatestVisualizationFrame(SRP_VisFrame_t *frame)
{
    uint32_t start_seq;
    uint32_t end_seq;
    uint8_t index;
    uint32_t attempt;

    if (frame == NULL)
    {
        return 0u;
    }

    for (attempt = 0u; attempt < 3u; attempt++)
    {
        start_seq = s_vis_publish_seq;
        __DMB();
        if ((start_seq & 1u) != 0u)
        {
            g_srp_vis_snapshot_retry_count++;
            continue;
        }

        index = s_vis_publish_index;
        memcpy(frame, &s_vis_publish_buffers[index], sizeof(*frame));
        __DMB();
        end_seq = s_vis_publish_seq;
        if ((start_seq == end_seq) && ((end_seq & 1u) == 0u))
        {
            return 1u;
        }

        g_srp_vis_snapshot_retry_count++;
    }

    return 0u;
}

/**
 * @brief   提取 Top-K 多声源定位结果
 * @details 在 AI_SRP_PHAT_Process() 完成后调用，从全局 SRP_Power
 *          数组中提取 Top-K 峰值（带 NMS 去重），填充到 Sound_MultiPos_t。
 *          必须在同一任务（Audio_Pipeline_Task）中、
 *          AI_SRP_PHAT_Process() 返回后立即调用。
 *
 * @param   out  输出多声源结构体指针
 */
void AI_SRP_GetMultiSource(Sound_MultiPos_t *out)
{
    uint8_t  used[SRP_GRID_TOTAL];
    uint32_t k;
    uint8_t  count;

    if (out == NULL)
    {
        return;
    }

    out->count = 0u;
    memset(used, 0, sizeof(used));

    count = 0u;

    for (k = 0u; k < MULTI_SOURCE_MAX; k++)
    {
        float32_t best_val = -1.0e30f;
        uint32_t  best_idx = 0u;
        uint8_t   found = 0u;
        uint32_t  n;

        /* 在未使用点中找最大值（跳过与已选点角度过近的点） */
        for (n = 0u; n < SRP_GRID_TOTAL; n++)
        {
            float32_t theta_n, phi_n;
            uint8_t   too_close;
            uint8_t   c;

            if (used[n] != 0u)
            {
                continue;
            }
            if (SRP_Power[n] <= best_val)
            {
                continue;
            }

            /* 检查是否与已选源距离太近（<15度） */
            too_close = 0u;
            get_grid_angle(n, &theta_n, &phi_n);
            for (c = 0u; c < count; c++)
            {
                float32_t dt = theta_n - out->sources[c].x_angle;
                float32_t dp = phi_n   - out->sources[c].y_angle;
                if ((dt * dt + dp * dp) < (15.0f * 15.0f))
                {
                    too_close = 1u;
                    break;
                }
            }
            if (too_close != 0u)
            {
                continue;
            }

            best_val = SRP_Power[n];
            best_idx = n;
            found = 1u;
        }

        if (found == 0u)
        {
            break;
        }

        /* 记录此声源 */
        {
            float32_t theta, phi, norm;
            get_grid_angle(best_idx, &theta, &phi);
            remap_output_angles(&theta, &phi);

            norm = best_val / (float32_t)(SRP_PAIR_COUNT * SRP_FREQ_BINS);
            if (norm > 1.0f) { norm = 1.0f; }
            if (norm < 0.0f) { norm = 0.0f; }

            out->sources[count].x_angle = theta;
            out->sources[count].y_angle = phi;
            out->sources[count].energy  = norm;
        }

        used[best_idx] = 1u;
        count++;
    }

    out->count = count;
}