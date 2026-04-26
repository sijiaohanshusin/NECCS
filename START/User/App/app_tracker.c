/**
 * @file    app_tracker.c
 * @brief   多声源帧间跟踪器实现
 * @details
 * 使用贪心最近邻匹配算法：
 *   - 复杂度：O(N_det × N_trk)，当 MULTI_SOURCE_MAX 较小（≤8）时可接受
 *   - 匹配策略：对每个新检测，找已有目标中角度距离最近的（<阈值）进行匹配
 *   - EMA 平滑：匹配成功后以 α=0.4 平滑更新位置，降低角度抖动
 *   - 目标删除：连续超过 TRACKER_MAX_MISSED 帧未匹配则删除（后向填充法）
 *   - 目标创建：未匹配的新检测创建新目标，从全局 ID 计数器分配唯一 ID
 *
 * [注意] s_next_id 使用 uint8_t，范围 1-255，长时间运行后会 ID 复用（回绕）。
 *        在目标生命周期短的场景下不影响使用，但若需要长时间唯一 ID 可改为 uint32_t。
 */
#include "app_tracker.h"               /* 本模块公开接口 */

#include <string.h>                    /* memset, memcpy */
#include <math.h>                      /* sqrtf（角度欧氏距离计算）*/

/** @brief 默认匹配角度阈值（度）：10° 内认为是同一声源 */
#define TRACKER_DEFAULT_MATCH_DEG  10.0f

/** @brief 最大允许未匹配帧数：超过则认为声源已消失，删除此目标 */
/** 5 帧 × (256/48000s) ≈ 26ms，声源短暂消失超过此时间则删除 */
#define TRACKER_MAX_MISSED         5u

/** @brief 位置平滑 EMA 系数（α=0.4）*/
/** α 大（接近1）：响应快但噪声大；α 小（接近0）：平滑但有延迟 */
#define TRACKER_EMA_ALPHA          0.4f

/** @brief 下一个分配的跟踪 ID（从 1 开始，0 保留为无效 ID）*/
static uint8_t s_next_id = 1u;

/** @brief 当前所有活跃跟踪目标数组 */
static App_TrackerTarget_t s_targets[MULTI_SOURCE_MAX];

/** @brief 当前活跃目标数（0..MULTI_SOURCE_MAX）*/
static uint8_t s_count = 0u;

/** @brief 角度匹配阈值（度）*/
static float s_match_threshold = TRACKER_DEFAULT_MATCH_DEG;

/**
 * @brief  计算两个角度坐标之间的欧氏距离（单位：度）
 * @details 距离公式：sqrt((x1-x2)² + (y1-y2)²)，基于球面小角近似，
 *          在角度范围较小时（<60°）误差可忽略。
 * @return 欧氏角度距离（度）
 */
static float s_angle_dist(float x1, float y1, float x2, float y2)
{
    float dx = x1 - x2;  /* 水平角差 */
    float dy = y1 - y2;  /* 垂直角差 */
    return sqrtf(dx * dx + dy * dy);  /* 欧氏距离 */
}

/**
 * @brief 初始化跟踪器
 */
void App_Tracker_Init(void)
{
    (void)memset(s_targets, 0, sizeof(s_targets));   /* 清空目标数组 */
    s_count = 0u;                                     /* 活跃目标数清零 */
    s_next_id = 1u;                                   /* ID 从 1 开始分配（0 保留为无效）*/
    s_match_threshold = TRACKER_DEFAULT_MATCH_DEG;    /* 恢复默认匹配阈值 */
}

/**
 * @brief 更新跟踪器（每帧调用一次）
 * @details 步骤：
 *   1. 贪心匹配新检测与已有目标
 *   2. 未匹配已有目标：missed++
 *   3. 删除超时目标（missed > MAX_MISSED）
 *   4. 未匹配新检测：创建新目标
 */
void App_Tracker_Update(const Sound_MultiPos_t *multi)
{
    uint8_t i;              /* 新检测索引 */
    uint8_t j;              /* 已有目标索引 */
    float dist;             /* 当前检测与当前目标的角度距离 */
    float best_dist;        /* 本检测找到的最近距离 */
    int8_t best_match;      /* 最近目标在 s_targets 中的索引（-1 = 无匹配）*/
    uint8_t det_matched[MULTI_SOURCE_MAX];  /* 每个新检测是否已匹配到目标 */
    uint8_t trk_matched[MULTI_SOURCE_MAX];  /* 每个已有目标是否已被匹配 */

    if (multi == NULL)      /* 空指针保护 */
    {
        return;
    }

    (void)memset(det_matched, 0, sizeof(det_matched));  /* 初始化：所有检测未匹配 */
    (void)memset(trk_matched, 0, sizeof(trk_matched));  /* 初始化：所有目标未匹配 */

    /* ========== 阶段1：贪心匹配 ==========
     * 对每个新检测，在未匹配的已有目标中找最近邻 */
    for (i = 0u; i < multi->count; i++)
    {
        best_dist  = s_match_threshold;  /* 门限：距离超过此值不匹配 */
        best_match = -1;                 /* 初始无匹配 */

        for (j = 0u; j < s_count; j++)
        {
            if (trk_matched[j] != 0u)   /* 已被匹配的目标跳过（保证一对一）*/
            {
                continue;
            }
            dist = s_angle_dist(multi->sources[i].x_angle,
                                multi->sources[i].y_angle,
                                s_targets[j].x_angle,
                                s_targets[j].y_angle);  /* 计算检测与目标的角度距离 */
            if (dist < best_dist)        /* 找到更近的目标 */
            {
                best_dist  = dist;
                best_match = (int8_t)j;
            }
        }

        if (best_match >= 0)            /* 找到了匹配目标 */
        {
            /* 更新匹配目标位置（EMA 平滑，防止位置突跳）*/
            App_TrackerTarget_t *t = &s_targets[best_match];
            t->x_angle = t->x_angle * (1.0f - TRACKER_EMA_ALPHA)
                       + multi->sources[i].x_angle * TRACKER_EMA_ALPHA;  /* EMA: 新=(旧×(1-α) + 新det×α) */
            t->y_angle = t->y_angle * (1.0f - TRACKER_EMA_ALPHA)
                       + multi->sources[i].y_angle * TRACKER_EMA_ALPHA;  /* 同上，垂直角 */
            t->energy = multi->sources[i].energy;   /* 能量不平滑，直接更新为最新值 */
            t->missed = 0u;                          /* 成功匹配，清零连续未匹配计数 */
            if (t->age < 255u)                       /* 存活帧数递增（防 uint8_t 溢出）*/
            {
                t->age++;
            }
            trk_matched[best_match] = 1u;  /* 标记此目标已被匹配 */
            det_matched[i] = 1u;           /* 标记此检测已被匹配 */
        }
    }

    /* ========== 阶段2：未匹配的已有目标，missed++ ==========
     * 如果某目标本帧没有任何检测与之匹配，说明可能声源短暂消失 */
    for (j = 0u; j < s_count; j++)
    {
        if (trk_matched[j] == 0u)          /* 本帧未匹配 */
        {
            s_targets[j].missed++;          /* 连续未匹配计数+1 */
        }
    }

    /* ========== 阶段3：删除超时目标（从后往前遍历，避免索引移位问题）==========
     * 删除方法：用最后一个有效目标覆盖当前位置，然后 s_count-- */
    for (j = s_count; j > 0u; j--)
    {
        if (s_targets[j - 1u].missed > TRACKER_MAX_MISSED)  /* 超过最大未匹配帧数 */
        {
            /* 用最后一个目标覆盖当前被删除的位置（O(1) 删除不保序）*/
            if ((j - 1u) < (s_count - 1u))
            {
                s_targets[j - 1u] = s_targets[s_count - 1u];  /* 后向填充法 */
            }
            s_count--;               /* 活跃目标数递减 */
        }
    }

    /* ========== 阶段4：为未匹配的新检测创建新目标 ==========
     * 成功创建后分配新的唯一 ID */
    for (i = 0u; i < multi->count; i++)
    {
        if ((det_matched[i] == 0u) && (s_count < MULTI_SOURCE_MAX))  /* 未匹配且有空位 */
        {
            App_TrackerTarget_t *t = &s_targets[s_count];  /* 在末尾添加新目标 */
            t->id = s_next_id;               /* 分配新 ID */
            s_next_id++;                     /* ID 计数器递增 */
            if (s_next_id == 0u)             /* uint8_t 溢出回绕到 0，跳过 0（为无效值）*/
            {
                s_next_id = 1u;              /* 直接跳到 1，继续分配（ID 复用）*/
            }
            t->age = 1u;                     /* 新目标存活 1 帧 */
            t->missed = 0u;                  /* 新目标未连续未匹配 */
            t->reserved = 0u;               /* 对齐字段 */
            t->x_angle = multi->sources[i].x_angle;   /* 初始位置直接使用检测值 */
            t->y_angle = multi->sources[i].y_angle;
            t->energy = multi->sources[i].energy;
            s_count++;                       /* 活跃目标数+1 */
        }
        /* [注意] 若目标数量已满（s_count == MULTI_SOURCE_MAX），新检测被丢弃 */
        /* [改进] 可改为替换最老的目标，或增大 MULTI_SOURCE_MAX */
    }
}

/**
 * @brief 获取当前跟踪结果
 */
void App_Tracker_GetResult(App_TrackerResult_t *result)
{
    if (result == NULL)  /* 空指针保护 */
    {
        return;
    }
    /* 复制活跃目标数组的有效部分到输出结构体 */
    (void)memcpy(result->targets, s_targets,
                 sizeof(App_TrackerTarget_t) * s_count);
    result->count = s_count;  /* 传递活跃目标数量 */
}

/**
 * @brief 设置角度匹配阈值
 */
void App_Tracker_SetMatchThreshold(float deg)
{
    if (deg > 0.0f)      /* 阈值必须为正值 */
    {
        s_match_threshold = deg;
    }
}

/**
 * @brief 获取角度匹配阈值
 */
float App_Tracker_GetMatchThreshold(void)
{
    return s_match_threshold;
}

/**
 * @brief 重置跟踪器（清除所有目标）
 */
void App_Tracker_Reset(void)
{
    (void)memset(s_targets, 0, sizeof(s_targets));  /* 清空目标数组 */
    s_count = 0u;                                    /* 活跃目标数清零 */
    /* [注意] 不重置 s_next_id（保留 ID 连续性，避免复用最近使用的 ID）*/
    /* [注意] 不重置 s_match_threshold（保留用户设置）*/
}

/**
 * @brief 计算两个角度坐标之间的欧氏距离
 */
static float s_angle_dist(float x1, float y1, float x2, float y2)
{
    float dx = x1 - x2;
    float dy = y1 - y2;
    return sqrtf(dx * dx + dy * dy);
}

void App_Tracker_Init(void)
{
    (void)memset(s_targets, 0, sizeof(s_targets));
    s_count = 0u;
    s_next_id = 1u;
    s_match_threshold = TRACKER_DEFAULT_MATCH_DEG;
}

void App_Tracker_Update(const Sound_MultiPos_t *multi)
{
    uint8_t i;
    uint8_t j;
    float dist;
    float best_dist;
    int8_t best_match;
    uint8_t det_matched[MULTI_SOURCE_MAX];
    uint8_t trk_matched[MULTI_SOURCE_MAX];

    if (multi == NULL)
    {
        return;
    }

    (void)memset(det_matched, 0, sizeof(det_matched));
    (void)memset(trk_matched, 0, sizeof(trk_matched));

    /* 贪心匹配：对每个检测，找最近的已有目标 */
    for (i = 0u; i < multi->count; i++)
    {
        best_dist = s_match_threshold;
        best_match = -1;

        for (j = 0u; j < s_count; j++)
        {
            if (trk_matched[j] != 0u)
            {
                continue;
            }
            dist = s_angle_dist(multi->sources[i].x_angle,
                                multi->sources[i].y_angle,
                                s_targets[j].x_angle,
                                s_targets[j].y_angle);
            if (dist < best_dist)
            {
                best_dist = dist;
                best_match = (int8_t)j;
            }
        }

        if (best_match >= 0)
        {
            /* 匹配成功：更新位置 (EMA 平滑) */
            App_TrackerTarget_t *t = &s_targets[best_match];
            t->x_angle = t->x_angle * (1.0f - TRACKER_EMA_ALPHA)
                       + multi->sources[i].x_angle * TRACKER_EMA_ALPHA;
            t->y_angle = t->y_angle * (1.0f - TRACKER_EMA_ALPHA)
                       + multi->sources[i].y_angle * TRACKER_EMA_ALPHA;
            t->energy = multi->sources[i].energy;
            t->missed = 0u;
            if (t->age < 255u)
            {
                t->age++;
            }
            trk_matched[best_match] = 1u;
            det_matched[i] = 1u;
        }
    }

    /* 未匹配的已有目标：增加 missed 计数 */
    for (j = 0u; j < s_count; j++)
    {
        if (trk_matched[j] == 0u)
        {
            s_targets[j].missed++;
        }
    }

    /* 删除超时目标（从后往前遍历避免索引问题） */
    for (j = s_count; j > 0u; j--)
    {
        if (s_targets[j - 1u].missed > TRACKER_MAX_MISSED)
        {
            /* 用最后一个覆盖当前位置 */
            if ((j - 1u) < (s_count - 1u))
            {
                s_targets[j - 1u] = s_targets[s_count - 1u];
            }
            s_count--;
        }
    }

    /* 未匹配的新检测：创建新目标 */
    for (i = 0u; i < multi->count; i++)
    {
        if ((det_matched[i] == 0u) && (s_count < MULTI_SOURCE_MAX))
        {
            App_TrackerTarget_t *t = &s_targets[s_count];
            t->id = s_next_id;
            s_next_id++;
            if (s_next_id == 0u)
            {
                s_next_id = 1u; /* 跳过 0 (无效 ID) */
            }
            t->age = 1u;
            t->missed = 0u;
            t->reserved = 0u;
            t->x_angle = multi->sources[i].x_angle;
            t->y_angle = multi->sources[i].y_angle;
            t->energy = multi->sources[i].energy;
            s_count++;
        }
    }
}

void App_Tracker_GetResult(App_TrackerResult_t *result)
{
    if (result == NULL)
    {
        return;
    }
    (void)memcpy(result->targets, s_targets,
                 sizeof(App_TrackerTarget_t) * s_count);
    result->count = s_count;
}

void App_Tracker_SetMatchThreshold(float deg)
{
    if (deg > 0.0f)
    {
        s_match_threshold = deg;
    }
}

float App_Tracker_GetMatchThreshold(void)
{
    return s_match_threshold;
}

void App_Tracker_Reset(void)
{
    (void)memset(s_targets, 0, sizeof(s_targets));
    s_count = 0u;
}
