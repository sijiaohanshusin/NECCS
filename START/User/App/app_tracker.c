/**
 * @file    app_tracker.c
 * @brief   多声源帧间跟踪器实现
 */
#include "app_tracker.h"

#include <string.h>
#include <math.h>

/** @brief 默认匹配角度阈值（度） */
#define TRACKER_DEFAULT_MATCH_DEG  10.0f

/** @brief 最大允许未匹配帧数，超过则删除目标 */
#define TRACKER_MAX_MISSED         5u

/** @brief 位置平滑 EMA 系数 */
#define TRACKER_EMA_ALPHA          0.4f

/** @brief 下一个分配的跟踪 ID */
static uint8_t s_next_id = 1u;

/** @brief 当前跟踪目标 */
static App_TrackerTarget_t s_targets[MULTI_SOURCE_MAX];

/** @brief 活跃目标数 */
static uint8_t s_count = 0u;

/** @brief 匹配阈值 */
static float s_match_threshold = TRACKER_DEFAULT_MATCH_DEG;

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
