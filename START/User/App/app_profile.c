/**
 * @file    app_profile.c
 * @brief   应用场景快速预设系统实现
 */
#include "app_profile.h"

#include "app_runtime.h"
#include "app_anomaly.h"
#include "app_user_config.h"

/** @brief 频率 Hz → FFT bin 索引 */
#define HZ_TO_BIN(hz)  ((uint16_t)((uint32_t)(hz) * FRAME_LEN / SAMPLING_RATE))

/** @brief 预设定义表 */
static const App_ProfileDef_t s_profiles[APP_PROFILE_COUNT] = {
    /* GENERAL: 通用, 0.5-8 kHz */
    {
        "General",          /* name */
        500u,               /* freq_lo_hz */
        8000u,              /* freq_hi_hz */
        0.15f,              /* noise_gate_ratio */
        1.8f,               /* gamma */
        2u,                 /* smooth_passes */
        1u,                 /* fine_fusion */
        1u,                 /* display_mode: BALANCED */
        0u                  /* anomaly_enable */
    },
    /* GAS_LEAK: 高频气体泄漏, 约 20 kHz 为上限 (24 kHz Nyquist) */
    {
        "Gas Leak",         /* name */
        15000u,             /* freq_lo_hz (实际受 Nyquist 限制) */
        23000u,             /* freq_hi_hz */
        0.05f,              /* noise_gate_ratio: 低, 高灵敏度 */
        1.2f,               /* gamma: 低, 增强弱信号 */
        1u,                 /* smooth_passes: 少, 快响应 */
        0u,                 /* fine_fusion: 关 */
        0u,                 /* display_mode: FAST */
        1u                  /* anomaly_enable: 开 */
    },
    /* BEARING: 轴承故障诊断, 5-20 kHz */
    {
        "Bearing",          /* name */
        5000u,              /* freq_lo_hz */
        20000u,             /* freq_hi_hz */
        0.10f,              /* noise_gate_ratio */
        1.5f,               /* gamma */
        2u,                 /* smooth_passes */
        1u,                 /* fine_fusion */
        1u,                 /* display_mode: BALANCED */
        1u                  /* anomaly_enable: 开 */
    },
    /* ELECTRICAL: 局部放电, 8-15 kHz */
    {
        "Electrical",       /* name */
        8000u,              /* freq_lo_hz */
        15000u,             /* freq_hi_hz */
        0.08f,              /* noise_gate_ratio */
        1.4f,               /* gamma */
        3u,                 /* smooth_passes */
        1u,                 /* fine_fusion: 开 */
        2u,                 /* display_mode: CLEAN */
        1u                  /* anomaly_enable: 开 */
    }
};

/** @brief 当前激活的预设 ID */
static App_ProfileId_t s_current = APP_PROFILE_GENERAL;

void App_Profile_Init(void)
{
    s_current = APP_PROFILE_GENERAL;  /* 系统启动时默认使用通用预设 */
}

void App_Profile_Apply(App_ProfileId_t id)
{
    const App_ProfileDef_t *p;      /* 指向目标预设定义的指针 */
    App_Runtime_DisplayCfg_t dcfg;  /* 临时显示配置结构体（读取→修改→写回） */
    uint16_t bin_lo;                /* 频段低端 FFT bin 索引 */
    uint16_t bin_hi;                /* 频段高端 FFT bin 索引 */

    if (id >= APP_PROFILE_COUNT)
    {
        return;  /* 无效的预设 ID（越界保护） */
    }

    p = &s_profiles[id];  /* 取指定预设的定义指针 */
    s_current = id;       /* 记录当前激活的预设 ID（供 GetCurrent 返回） */

    /* ---- 设置 SRP-PHAT 工作频段（Hz → bin 索引） ---- */
    /* HZ_TO_BIN: bin = hz × FRAME_LEN / SAMPLING_RATE = hz × 256 / 48000 */
    bin_lo = HZ_TO_BIN(p->freq_lo_hz);  /* 低截止频带对应的 FFT bin */
    bin_hi = HZ_TO_BIN(p->freq_hi_hz);  /* 高截止频带对应的 FFT bin */

    /* 钳位到系统有效频段范围（SRP_FREQ_BIN_START ~ SRP_FREQ_BIN_END） */
    if (bin_lo < SRP_FREQ_BIN_START)
    {
        bin_lo = SRP_FREQ_BIN_START;  /* 低端不能低于 SRP 算法配置的最小 bin */
    }
    if (bin_hi > SRP_FREQ_BIN_END)
    {
        bin_hi = SRP_FREQ_BIN_END;    /* 高端不能超过 SRP 算法配置的最大 bin */
    }
    if (bin_lo >= bin_hi)
    {
        /* 频段无效（交叉或相等），回退到默认全频段 */
        bin_lo = SRP_FREQ_BIN_START;
        bin_hi = SRP_FREQ_BIN_END;
    }

    App_RuntimeConfig_SetFreqBand(bin_lo, bin_hi);  /* 写入 SRP 算法工作频段 */

    /* ---- 设置显示配置（从当前配置读取，只更改预设相关字段） ---- */
    App_RuntimeConfig_GetDisplayCfg(&dcfg);          /* 读取当前全量显示配置 */
    dcfg.noise_gate_ratio  = p->noise_gate_ratio;    /* 噪声门限比例（抑制弱信号假峰） */
    dcfg.gamma             = p->gamma;                /* 伽马校正系数（视觉对比度调整） */
    dcfg.smooth_passes     = p->smooth_passes;        /* 空间平滑迭代次数 */
    dcfg.fine_fusion_enable = p->fine_fusion;         /* 是否叠加精细 SRP 网格 */
    App_RuntimeConfig_SetDisplayCfg(&dcfg);           /* 写回修改后的配置 */

    /* ---- 设置显示渲染模式 ---- */
    App_RuntimeConfig_SetDisplayMode((App_Runtime_DisplayMode_t)p->display_mode);

    /* ---- 设置异常能量检测开关 ---- */
    App_Anomaly_SetEnabled(p->anomaly_enable);  /* 工业诊断场景开启，通用场景关闭 */
}

App_ProfileId_t App_Profile_GetCurrent(void)
{
    return s_current;  /* 返回当前激活的预设 ID（默认 GENERAL = 0） */
}

const App_ProfileDef_t *App_Profile_GetDef(App_ProfileId_t id)
{
    if (id >= APP_PROFILE_COUNT)
    {
        return (const App_ProfileDef_t *)0;  /* 无效 ID 返回 NULL，调用方须检查 */
    }
    return &s_profiles[id];  /* 返回指定预设的定义指针（只读，不可修改预设表） */
}
