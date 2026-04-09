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
    s_current = APP_PROFILE_GENERAL;
}

void App_Profile_Apply(App_ProfileId_t id)
{
    const App_ProfileDef_t *p;
    App_Runtime_DisplayCfg_t dcfg;
    uint16_t bin_lo;
    uint16_t bin_hi;

    if (id >= APP_PROFILE_COUNT)
    {
        return;
    }

    p = &s_profiles[id];
    s_current = id;

    /* 设置频段 */
    bin_lo = HZ_TO_BIN(p->freq_lo_hz);
    bin_hi = HZ_TO_BIN(p->freq_hi_hz);

    /* 钳位到有效范围 */
    if (bin_lo < SRP_FREQ_BIN_START)
    {
        bin_lo = SRP_FREQ_BIN_START;
    }
    if (bin_hi > SRP_FREQ_BIN_END)
    {
        bin_hi = SRP_FREQ_BIN_END;
    }
    if (bin_lo >= bin_hi)
    {
        bin_lo = SRP_FREQ_BIN_START;
        bin_hi = SRP_FREQ_BIN_END;
    }

    App_RuntimeConfig_SetFreqBand(bin_lo, bin_hi);

    /* 设置显示配置 */
    App_RuntimeConfig_GetDisplayCfg(&dcfg);
    dcfg.noise_gate_ratio = p->noise_gate_ratio;
    dcfg.gamma = p->gamma;
    dcfg.smooth_passes = p->smooth_passes;
    dcfg.fine_fusion_enable = p->fine_fusion;
    App_RuntimeConfig_SetDisplayCfg(&dcfg);

    /* 设置显示模式 */
    App_RuntimeConfig_SetDisplayMode((App_Runtime_DisplayMode_t)p->display_mode);

    /* 设置异常检测 */
    App_Anomaly_SetEnabled(p->anomaly_enable);
}

App_ProfileId_t App_Profile_GetCurrent(void)
{
    return s_current;
}

const App_ProfileDef_t *App_Profile_GetDef(App_ProfileId_t id)
{
    if (id >= APP_PROFILE_COUNT)
    {
        return (const App_ProfileDef_t *)0;
    }
    return &s_profiles[id];
}
