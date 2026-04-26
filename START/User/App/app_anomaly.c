/**
 * @file    app_anomaly.c
 * @brief   异常声音检测与历史日志实现
 * @details
 * 检测算法：
 *   每帧遍历所有 bin，计算 deviation = magnitude[i] / floor[i]（当前幅度/噪声底）。
 *   若最大 deviation >= s_threshold（默认 3.0），则记录一条异常事件。
 *   记录后启动冷却计数（10 帧），避免同一声音事件被重复记录。
 *
 * 环形日志实现：
 *   使用固定大小数组 s_log[APP_ANOMALY_LOG_SIZE] + 写指针 s_write_idx。
 *   GetEntry(0) 映射到 (s_write_idx - 1)，即最新写入的那条。
 *   当 s_total_count > APP_ANOMALY_LOG_SIZE 时，最旧记录被新记录覆盖。
 */
#include "app_anomaly.h"               /* 本模块公开接口 */

#include "FreeRTOS.h"                  /* xTaskGetTickCount()：获取当前系统时刻 */
#include "task.h"                      /* 提供 FreeRTOS 任务 API */

#include <string.h>                    /* memset：日志清零 */

/** @brief 默认异常阈值：当前幅度超过噪声底 3 倍时触发异常 */
/** 3.0 大致对应~10dB 高于噪声底，是工程实践中较常用的灵敏度 */
#define ANOMALY_DEFAULT_THRESHOLD  3.0f

/** @brief 异常事件冷却帧数：检测到异常后的该帧数内不再重新检测 */
/** 10 帧 × (256/48000s) ≈ 53ms，避免同一时间段内重复触发日志 */
#define ANOMALY_COOLDOWN_FRAMES    10u

/** @brief 环形日志缓冲，存储最近 APP_ANOMALY_LOG_SIZE 条异常事件 */
static App_AnomalyEntry_t s_log[APP_ANOMALY_LOG_SIZE];

/** @brief 写指针：下一次写入 s_log 的位置（模 APP_ANOMALY_LOG_SIZE 环形）*/
static uint32_t s_write_idx = 0u;

/** @brief 历史异常总条数（含被覆盖的旧记录，可超过 APP_ANOMALY_LOG_SIZE）*/
static uint32_t s_total_count = 0u;

/** @brief 当前异常检测阈值倍数（默认 3.0）*/
static float s_threshold = ANOMALY_DEFAULT_THRESHOLD;

/** @brief 检测使能标志：0 = 禁用（Feed 直接返回），1 = 启用 */
static uint8_t s_enabled = 0u;

/** @brief 冷却计数器：> 0 时跳过检测，每帧递减 */
static uint32_t s_cooldown = 0u;

/**
 * @brief 初始化异常检测模块
 */
void App_Anomaly_Init(void)
{
    (void)memset(s_log, 0, sizeof(s_log));        /* 清空环形日志缓冲 */
    s_write_idx   = 0u;                            /* 写指针复位到起点 */
    s_total_count = 0u;                            /* 历史计数清零 */
    s_threshold   = ANOMALY_DEFAULT_THRESHOLD;     /* 恢复默认阈值 3.0 */
    s_enabled     = 0u;                            /* 默认禁用，需要显式启用 */
    s_cooldown    = 0u;                            /* 冷却计数清零 */
}

/**
 * @brief 馈送频谱帧，检测异常声音事件
 */
uint8_t App_Anomaly_Feed(const float *magnitude, const float *floor,
                         uint16_t bin_count,
                         float x_angle, float y_angle, float energy)
{
    uint16_t i;              /* 循环变量：当前 bin 索引 */
    float max_dev = 0.0f;    /* 所有 bin 中的最大偏离倍数 */
    uint16_t max_bin = 0u;   /* 偏离最大的 bin 索引（用于记录峰值频率）*/
    float ratio;             /* 当前 bin 的偏离倍数：magnitude[i] / floor[i] */

    if (s_enabled == 0u)     /* 检测未使能，直接返回（节省 CPU）*/
    {
        return 0u;
    }

    if ((magnitude == NULL) || (floor == NULL) || (bin_count == 0u))  /* 参数合法性检查 */
    {
        return 0u;
    }

    /* 冷却期间跳过检测（避免同一事件重复记录）*/
    if (s_cooldown > 0u)
    {
        s_cooldown--;          /* 每帧递减，直到冷却结束 */
        return 0u;
    }

    /* 遍历所有 bin，找到偏离最大的那个 */
    for (i = 0u; i < bin_count; i++)
    {
        if (floor[i] > 1.0e-10f)  /* 噪声底必须是正值（防止除零，1e-10 是最小有效噪声底）*/
        {
            ratio = magnitude[i] / floor[i];  /* 当前幅度相对噪声底的倍数 */
            if (ratio > max_dev)
            {
                max_dev = ratio;   /* 更新最大偏离值 */
                max_bin = i;       /* 记录最大偏离对应的 bin 索引 */
            }
        }
    }

    /* 判定是否超过阈值（任意 bin 超过即为异常）*/
    if (max_dev >= s_threshold)
    {
        /* 写入新的异常记录到环形日志 */
        App_AnomalyEntry_t *entry = &s_log[s_write_idx];  /* 指向当前写入位置 */
        entry->tick      = (uint32_t)xTaskGetTickCount();  /* 记录当前系统时刻（ms）*/
        entry->x_angle   = x_angle;    /* 记录声源水平方向角 */
        entry->y_angle   = y_angle;    /* 记录声源垂直方向角 */
        entry->energy    = energy;     /* 记录触发异常时的声源能量 */
        entry->deviation = max_dev;    /* 记录峰值偏离倍数（触发比率）*/
        entry->peak_bin  = max_bin;    /* 记录偏离最大的 bin（可换算为频率）*/
        entry->reserved  = 0u;         /* 保留字段填 0 */

        /* 写指针前进，超出 LOG_SIZE 时环绕到 0（覆盖最旧记录）*/
        s_write_idx++;
        if (s_write_idx >= APP_ANOMALY_LOG_SIZE)
        {
            s_write_idx = 0u;   /* 环绕：最旧的记录在下一次写入中被覆盖 */
        }
        s_total_count++;        /* 历史总计数递增（不环绕，记录历史总次数）*/
        s_cooldown = ANOMALY_COOLDOWN_FRAMES;  /* 启动冷却，避免下 N 帧重复触发 */
        return 1u;              /* 返回 1 表示本帧检测到异常 */
    }

    return 0u;                  /* 本帧无异常 */
}

/**
 * @brief 设置异常检测阈值
 */
void App_Anomaly_SetThreshold(float ratio)
{
    if (ratio > 1.0f)           /* 阈值必须 > 1.0（否则所有帧都会触发异常）*/
    {
        s_threshold = ratio;    /* 更新阈值（立即对下一帧 Feed 生效）*/
    }
    /* [注意] 不满足 > 1.0 时静默忽略，不报错（容错设计）*/
}

/**
 * @brief 获取当前异常检测阈值
 */
float App_Anomaly_GetThreshold(void)
{
    return s_threshold;
}

/**
 * @brief 获取指定索引的异常日志条目
 */
uint8_t App_Anomaly_GetEntry(uint32_t index, App_AnomalyEntry_t *entry)
{
    uint32_t valid;       /* 当前有效条目数 */
    uint32_t actual_idx;  /* 环形缓冲中的实际数组索引 */

    if (entry == NULL)    /* 空指针保护 */
    {
        return 0u;
    }

    /* 有效条目数：未满 LOG_SIZE 时用实际条数，否则固定为 LOG_SIZE */
    valid = (s_total_count < APP_ANOMALY_LOG_SIZE) ? s_total_count : APP_ANOMALY_LOG_SIZE;
    if (index >= valid)   /* 请求的索引超出有效范围 */
    {
        return 0u;
    }

    /* 将 "最新优先" 的 index 映射到环形缓冲的实际数组索引 */
    /* index=0 表示最新，即 s_write_idx - 1（最后一次写入的位置）*/
    if (s_write_idx >= (index + 1u))
    {
        actual_idx = s_write_idx - 1u - index;  /* 不需要环绕 */
    }
    else
    {
        /* 需要环绕：例如 s_write_idx=2, index=3 -> 需要从末尾向前数 */
        actual_idx = APP_ANOMALY_LOG_SIZE + s_write_idx - 1u - index;
    }

    *entry = s_log[actual_idx];  /* 复制日志条目到调用方 */
    return 1u;
}

/**
 * @brief 获取日志中有效条目数
 */
uint32_t App_Anomaly_GetCount(void)
{
    if (s_total_count < APP_ANOMALY_LOG_SIZE)
    {
        return s_total_count;              /* 未满日志，返回实际条目数 */
    }
    return APP_ANOMALY_LOG_SIZE;           /* 满了，最多有 LOG_SIZE 条有效记录 */
}

/**
 * @brief 清空异常日志（不重置阈值和使能状态）
 */
void App_Anomaly_ClearLog(void)
{
    (void)memset(s_log, 0, sizeof(s_log));  /* 清零所有日志数据 */
    s_write_idx   = 0u;                     /* 写指针复位 */
    s_total_count = 0u;                     /* 历史计数清零 */
    /* 不清零 s_threshold 和 s_enabled，仅清空数据 */
}

/**
 * @brief 使能/禁用异常检测
 */
void App_Anomaly_SetEnabled(uint8_t enable)
{
    s_enabled = (enable != 0u) ? 1u : 0u;  /* 规范化为 0/1，避免非零值干扰后续比较 */
}

/**
 * @brief 查询异常检测是否已使能
 */
uint8_t App_Anomaly_GetEnabled(void)
{
    return s_enabled;
}
