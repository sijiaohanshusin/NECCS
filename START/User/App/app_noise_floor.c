/**
 * @file    app_noise_floor.c
 * @brief   自适应背景噪声底估计与频域减噪实现
 * @details 使用非对称指数移动平均（EMA）跟踪背景噪声底：
 *          - 幅度下降时：立即跟随（噪声减少，直接采纳）
 *          - 幅度上升时：缓慢跟随（EMA 平滑）
 *          这种非对称策略确保噪声底始终贴紧实际背景幅度，
 *          而不会因突发声音（语音、碰击）使噪声底被拉高。
 */
#include "app_noise_floor.h"           /* 本模块公开接口（Init/Update/Get/Calibrate 等）*/

#include "app_user_config.h"           /* FRAME_LEN（用于计算最大 bin 数）*/

#include <string.h>                    /* memset/memcpy（初始化清零、复制噪声底数组）*/

/** @brief 最大支持的 bin 数 = FRAME_LEN / 2（对应实数 FFT 的正频率部分）*/
/** 默认：256/2 = 128 个 bin（FFT 输出的 DC 到奈奎斯特频率之间）*/
#define NOISE_FLOOR_MAX_BINS  (FRAME_LEN / 2u)

/** @brief 默认 EMA 平滑系数（0.02 = 约 50 帧收敛，50 × 5.3ms ≈ 265ms 达到新水平）*/
#define NOISE_FLOOR_DEFAULT_ALPHA  0.02f

/** @brief 噪声底估计数组（每个 bin 存储对应频率的长期背景幅度估计值）*/
/** 存储在 AXI SRAM（默认全局区域），128 × 4 = 512 字节 */
static float s_floor[NOISE_FLOOR_MAX_BINS];

/** @brief EMA 平滑系数（0.001~0.1），由 SetAlpha 动态调整 */
static float s_alpha = NOISE_FLOOR_DEFAULT_ALPHA;

/** @brief 初始化完成标志（0=未初始化/第一帧直接赋值，1=已有有效噪声底）*/
/** [注意] 此标志确保第一帧不经过 EMA（从某随机值收敛），而是直接采用当前幅度 */
static uint8_t s_initialized = 0u;

/** @brief 噪声减除使能标志（1=启用，0=禁用）*/
/** 默认禁用，防止首次启动时误将有声音的环境当作"背景" */
static uint8_t s_enabled = 0u;

/**
 * @brief 初始化噪声底模块（清零状态，应用默认参数）
 */
void App_NoiseFloor_Init(void)
{
    (void)memset(s_floor, 0, sizeof(s_floor));  /* 将所有 bin 的噪声底清零（设为 0.0f，等待第一帧数据）*/
    s_alpha = NOISE_FLOOR_DEFAULT_ALPHA;         /* 恢复默认 EMA 系数 0.02 */
    s_initialized = 0u;                          /* 标记未初始化，等待第一帧直接赋值 */
    s_enabled = 0u;                              /* 默认禁用噪声减除（需用户显式启用）*/
}

/**
 * @brief 更新噪声底估计（非对称 EMA）
 * @details 实现策略：
 *          - 当 magnitude[k] < s_floor[k]：噪声底快速下降（立即=直接赋值），
 *            原因：环境变安静时，应快速适应以避免过度减噪
 *          - 当 magnitude[k] >= s_floor[k]：噪声底缓慢上升（EMA），
 *            原因：可能是有效声音信号导致幅度升高，不应立即抬高噪声底
 */
void App_NoiseFloor_Update(const float *magnitude, uint16_t bin_count)
{
    uint16_t i;    /* 循环变量：当前处理的 bin 索引 */
    uint16_t n;    /* 实际处理的 bin 数量（防止超出内部数组范围）*/

    if (magnitude == NULL)                    /* 空指针检查，防止非法访问 */
    {
        return;
    }

    /* 限制处理范围，防止超出 s_floor 数组边界 */
    n = (bin_count > NOISE_FLOOR_MAX_BINS) ? NOISE_FLOOR_MAX_BINS : bin_count;

    if (s_initialized == 0u)                   /* 第一次调用：直接赋值，快速获得合理初始值 */
    {
        /* 不经 EMA 直接赋值：避免从 0 收敛（0 的噪声底会导致所有信号都被"减掉"）*/
        for (i = 0u; i < n; i++)
        {
            s_floor[i] = magnitude[i];         /* 第一帧幅度直接作为初始噪声底 */
        }
        s_initialized = 1u;                    /* 标记已初始化，后续帧使用 EMA 更新 */
        return;
    }

    /* 非对称 EMA 更新（非第一帧）*/
    for (i = 0u; i < n; i++)
    {
        if (magnitude[i] < s_floor[i])         /* 当前幅度低于噪声底：环境变安静 */
        {
            /* 快速下降：立即跟随（噪声底直接下降到当前幅度）*/
            /* 原因：若 EMA 慢慢下降，噪声底过高会导致后续帧的有效信号被过度抑制 */
            s_floor[i] = magnitude[i];
        }
        else                                   /* 当前幅度高于噪声底：可能是信号或噪声升高 */
        {
            /* 缓慢上升：EMA 平滑（防止有效声音片段拉高噪声底）*/
            /* 公式：floor = floor × (1 - alpha) + magnitude × alpha */
            s_floor[i] = s_floor[i] * (1.0f - s_alpha) + magnitude[i] * s_alpha;
        }
    }
}

/**
 * @brief 获取当前噪声底数组（复制到调用方提供的缓冲区）
 */
void App_NoiseFloor_Get(float *out_floor, uint16_t bin_count)
{
    uint16_t n;    /* 实际复制的 bin 数量 */

    if (out_floor == NULL)                     /* 空指针保护 */
    {
        return;
    }

    /* 限制复制范围，防止超出 s_floor 数组 */
    n = (bin_count > NOISE_FLOOR_MAX_BINS) ? NOISE_FLOOR_MAX_BINS : bin_count;

    /* 批量复制（memcpy 比逐元素复制快）*/
    (void)memcpy(out_floor, s_floor, (size_t)n * sizeof(float));
}

/**
 * @brief 一键校零：将当前幅度快照直接赋值为噪声底（覆盖 EMA 历史）
 * @details 适用于用户按下"校零"按键时，快速将当前静音环境设定为噪声底基准。
 */
void App_NoiseFloor_Calibrate(const float *magnitude, uint16_t bin_count)
{
    uint16_t i;    /* 循环变量 */
    uint16_t n;    /* 实际处理的 bin 数量 */

    if (magnitude == NULL)                     /* 空指针保护 */
    {
        return;
    }

    n = (bin_count > NOISE_FLOOR_MAX_BINS) ? NOISE_FLOOR_MAX_BINS : bin_count;

    /* 直接覆盖（不经 EMA），使噪声底立即跳至当前幅度水平 */
    for (i = 0u; i < n; i++)
    {
        s_floor[i] = magnitude[i];             /* 快照赋值，相当于"硬校零" */
    }

    s_initialized = 1u;                        /* 确保初始化标志为已设置状态 */
    /* [注意] Calibrate 后 EMA 继续运行，噪声底从新基准开始缓慢跟踪 */
}

/**
 * @brief 设置 EMA 平滑系数
 */
void App_NoiseFloor_SetAlpha(float alpha)
{
    /* 只接受 (0, 1) 范围内的有效值，防止除零或 alpha=0/1 时 EMA 退化 */
    if ((alpha > 0.0f) && (alpha < 1.0f))
    {
        s_alpha = alpha;                        /* 更新 EMA 系数（立即生效）*/
    }
    /* [注意] 无效的 alpha 值被静默忽略（不报错），调用方可通过 GetAlpha 确认是否生效 */
}

/**
 * @brief 获取当前 EMA 平滑系数
 */
float App_NoiseFloor_GetAlpha(void)
{
    return s_alpha;                             /* 直接返回当前 alpha（用于 CLI 查询显示）*/
}

/**
 * @brief 设置噪声减除使能状态
 */
void App_NoiseFloor_SetEnabled(uint8_t enable)
{
    /* 将任意非零值统一转换为 1（规范化 enable 标志）*/
    s_enabled = (enable != 0u) ? 1u : 0u;
    /* [注意] 禁用后 Update 仍继续跟踪噪声底，只是不在 SRP 管线中减去它 */
}

/**
 * @brief 查询噪声减除使能状态
 */
uint8_t App_NoiseFloor_GetEnabled(void)
{
    return s_enabled;                           /* 1=已启用频域减噪，0=已禁用 */
}
