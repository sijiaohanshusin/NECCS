/**
 * @file    app_trigger.c
 * @brief   瞬态捕捉触发模式 —— 状态机实现
 */
#include "app_trigger.h"

#include <math.h>
#include "FreeRTOS.h"
#include "task.h"

/** @brief 默认触发阈值（能量变化 15%） */
#define TRIGGER_DEFAULT_THRESHOLD  0.15f

/** @brief EMA 平滑系数（用于计算基线能量） */
#define TRIGGER_EMA_ALPHA          0.05f

/** @brief 当前状态 */
static App_TriggerState_t s_state = APP_TRIGGER_IDLE;

/** @brief 触发阈值 */
static float s_threshold = TRIGGER_DEFAULT_THRESHOLD;

/** @brief EMA 基线能量 */
static float s_baseline = 0.0f;

/** @brief 基线是否已稳定 */
static uint8_t s_baseline_valid = 0u;

/** @brief 稳定计数 */
static uint32_t s_warmup_count = 0u;

/** @brief 稳定帧数要求 */
#define TRIGGER_WARMUP_FRAMES 20u

/** @brief 触发统计 */
static App_TriggerStats_t s_stats;

/** @brief 触发回调 */
static App_TriggerCallback_t s_callback = NULL;

void App_Trigger_Init(void)
{
    s_state = APP_TRIGGER_IDLE;           /* 初始状态为空闲（触发模式未启用） */
    s_threshold = TRIGGER_DEFAULT_THRESHOLD; /* 恢复默认触发阈值（15%能量变化） */
    s_baseline = 0.0f;                    /* 基线能量清零 */
    s_baseline_valid = 0u;                /* 标记基线需要重新预热建立 */
    s_warmup_count = 0u;                  /* 预热帧计数归零 */
    s_stats.trigger_count = 0u;          /* 清零触发次数统计 */
    s_stats.last_trigger_tick = 0u;      /* 清零最近触发时刻 */
    s_stats.last_trigger_energy = 0.0f;  /* 清零最近触发能量 */
    s_stats.last_trigger_x = 0.0f;      /* 清零最近触发 x 角度 */
    s_stats.last_trigger_y = 0.0f;      /* 清零最近触发 y 角度 */
    s_callback = NULL;                    /* 清除触发回调注册 */
}

void App_Trigger_Arm(void)
{
    s_state = APP_TRIGGER_ARMED;  /* 切换到已武装状态，开始监测能量 */
    s_baseline = 0.0f;            /* 重置 EMA 基线为零，确保重新从当前环境能量建立基线 */
    s_baseline_valid = 0u;        /* 标记基线无效，需要经过预热帧才能触发 */
    s_warmup_count = 0u;          /* 预热计数从头开始 */
    /* [注意] 每次 Arm 都会重新建立基线，若环境能量已发生变化，
     * 预热期间的帧不会触发，最多等待 TRIGGER_WARMUP_FRAMES 帧后才会工作 */
}

void App_Trigger_Disarm(void)
{
    s_state = APP_TRIGGER_IDLE;  /* 回到空闲状态，Feed() 将立即返回 0 */
    /* [注意] 不清除统计数据，调用方仍可通过 GetStats() 读取历史触发记录 */
}

void App_Trigger_Rearm(void)
{
    if (s_state == APP_TRIGGER_TRIGGERED)  /* 仅允许从 TRIGGERED 状态重新武装 */
    {
        App_Trigger_Arm();  /* 重置基线并切换到 ARMED 状态，开始新一轮监测 */
    }
    /* IDLE 和 ARMED 状态不做任何处理（IDLE=尚未启用，ARMED=已在监测中） */
}

uint8_t App_Trigger_Feed(float energy)
{
    float delta;  /* 当前帧能量与基线的差值（突变量） */

    if (s_state != APP_TRIGGER_ARMED)
    {
        return 0u;  /* 非武装状态（IDLE 或 TRIGGERED）直接返回，不检测 */
    }

    /* ---- 预热阶段：建立 EMA 基线 ---- */
    /* 在触发判断开始前，先积累足够帧数以稳定基线能量估计，
     * 避免 Arm 时刻的瞬时能量误触发 */
    if (s_baseline_valid == 0u)
    {
        if (s_warmup_count == 0u)
        {
            /* 第一帧：直接以当前能量作为初始基线（冷启动） */
            s_baseline = energy;
        }
        else
        {
            /* 后续帧：EMA 低通滤波跟踪环境能量
             * 公式: baseline = baseline × (1 - α) + energy × α
             * α = TRIGGER_EMA_ALPHA = 0.05（每帧约 5% 更新），慢速跟踪 */
            s_baseline = s_baseline * (1.0f - TRIGGER_EMA_ALPHA)
                       + energy * TRIGGER_EMA_ALPHA;
        }
        s_warmup_count++;           /* 预热帧计数递增 */
        if (s_warmup_count >= TRIGGER_WARMUP_FRAMES)
        {
            s_baseline_valid = 1u;  /* 积累足够帧数，基线已稳定，可开始检测 */
        }
        return 0u;  /* 预热期间不触发 */
    }

    /* ---- 正常工作阶段：计算能量变化并检测突变 ---- */

    /* 计算能量变化量：正值=能量上升（声音增强），负值=能量下降（通常不触发） */
    delta = energy - s_baseline;

    /* 持续以 EMA 缓慢跟踪环境基线（即使未触发），以适应环境噪声缓慢变化
     * [改进] 仅上升方向跟踪（asymmetric EMA）可提高触发灵敏度 */
    s_baseline = s_baseline * (1.0f - TRIGGER_EMA_ALPHA)
               + energy * TRIGGER_EMA_ALPHA;

    /* 检测正向能量突变 (上升大于阈值 s_threshold) */
    if (delta > s_threshold)
    {
        s_state = APP_TRIGGER_TRIGGERED;          /* 切换到已触发状态（画面冻结） */
        s_stats.trigger_count++;                  /* 触发次数统计递增 */
        s_stats.last_trigger_tick = (uint32_t)xTaskGetTickCount(); /* 记录触发时刻 tick */
        s_stats.last_trigger_energy = energy;     /* 记录触发瞬间能量值 */
        /* [注意] x/y 角度由调用方在触发后调用 App_Trigger_SetTriggeredPos() 回填 */
        if (s_callback != NULL)
        {
            s_callback(energy);  /* 调用已注册的触发回调（如 SD 卡截图） */
        }
        return 1u;  /* 返回 1 表示本帧发生了状态跳变（ARMED → TRIGGERED） */
    }

    return 0u;  /* 未超过阈值，未触发 */
}

App_TriggerState_t App_Trigger_GetState(void)
{
    return s_state;  /* 直接返回当前状态，读取单字节原子（无需临界区） */
}

void App_Trigger_SetThreshold(float threshold)
{
    if (threshold > 0.0f)    /* 仅接受正数阈值（零或负值无意义，拒绝写入） */
    {
        s_threshold = threshold; /* 更新触发阈值，立即对下次 Feed() 生效 */
    }
    /* [改进] 应同时设置上限（如 1.0f），防止阈值过大导致永远不触发 */
}

float App_Trigger_GetThreshold(void)
{
    return s_threshold;  /* 返回当前阈值，供 CLI 查询或 UI 显示 */
}

void App_Trigger_SetTriggeredPos(float x_angle, float y_angle)
{
    /* 在触发发生后（TRIGGERED 状态）由调用方填入触发瞬间的声源角度，
     * 供后续统计分析和 UI 显示使用 */
    s_stats.last_trigger_x = x_angle;  /* 触发瞬间水平方向角（度） */
    s_stats.last_trigger_y = y_angle;  /* 触发瞬间俯仰方向角（度） */
}

void App_Trigger_GetStats(App_TriggerStats_t *stats)
{
    if (stats != NULL)       /* 空指针保护，防止非法写入 */
    {
        *stats = s_stats;    /* 结构体整体拷贝，返回统计快照（调用方可读取历史数据） */
    }
}

void App_Trigger_RegisterCallback(App_TriggerCallback_t cb)
{
    s_callback = cb;  /* 注册触发回调，传入 NULL 可取消注册 */
    /* [注意] 回调在 Feed() 内（UI 任务或音频任务上下文）被调用，
     * 不能在回调中执行阻塞操作（如等待 SD 写完成） */
}
