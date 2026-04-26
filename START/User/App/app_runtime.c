/**
 * @file    app_runtime.c
 * @brief   Runtime configuration implementation
 */
#include "app_runtime.h"

#include "app_display.h"
#include "app_laser.h"
#include "app_perf.h"
#include "app_spectrum.h"
#include "app_task_cfg.h"

#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 运行时配置全局单例（静态存储，临界区保护）
 *
 * 字段初始值均从编译期宏（app_display_cfg.h）读取，保证参数一致性。
 * 所有字段修改必须通过 App_RuntimeConfig_Set* API，禁止直接写入。
 */
static App_Runtime_Config_t s_runtime_cfg = {
    UI_FPS_DEFAULT,             /**< ui_target_fps：目标帧率，默认 20 fps */
    AUDIO_ALGO_DECIM_DEFAULT,   /**< audio_algo_decim：算法抽帧比，默认 1（每帧都跑） */
    0u,                         /**< perf_enabled：性能统计默认关闭 */
    {0u, 0u, 0u},               /**< reserved[3]：对齐填充，保留备用 */
    APP_RUNTIME_DISP_MODE_BALANCED, /**< display_mode：默认均衡模式（兼顾清晰度与 CPU） */
    APP_MODE_MAIN,                  /**< operating_mode：默认主模式 */
    {
        APP_DISPLAY_EMA_ATTACK,         /**< ema_attack：EMA 攻击系数（能量上升速率） */
        APP_DISPLAY_EMA_DECAY,          /**< ema_decay：EMA 衰减系数（能量下降速率） */
        APP_DISPLAY_DYNAMIC_DB_FLOOR,   /**< db_floor：动态范围底限 (dB)，低于此值视为噪声 */
        APP_DISPLAY_FINE_GAIN,          /**< fine_gain：精细网格叠加增益 */
        APP_DISPLAY_DYNAMIC_GAMMA,      /**< gamma：伽马校正系数，调整热力图视觉对比度 */
        APP_DISPLAY_NOISE_GATE_RATIO,   /**< noise_gate_ratio：噪声门限比例，抑制弱信号假峰 */
        APP_DISPLAY_NOISE_ADAPT_GAIN,   /**< noise_adapt_gain：自适应噪声估计增益 */
        0.85f,                          /**< heatmap_opacity：热力图叠加透明度 (0.0-1.0) */
        APP_DISPLAY_SMOOTH_PASSES,      /**< smooth_passes：空间平滑迭代次数（0=不平滑） */
        APP_DISPLAY_FINE_FUSION_ENABLE, /**< fine_fusion_enable：是否叠加精细 SRP 网格 */
        APP_DISPLAY_DRAW_COARSE_GRID,   /**< draw_coarse_grid：是否绘制粗网格参考线 */
        /* 插值模式：从编译期 bilinear 宏映射到运行时枚举 */
        (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_RUNTIME_DISP_INTERP_BILINEAR
                                               : APP_RUNTIME_DISP_INTERP_NEAREST,
        APP_RUNTIME_DISP_NORM_FAST,     /**< norm_mode：归一化策略，默认快速模式（仅用峰值） */
        APP_DISPLAY_TEXT_REFRESH_DIV,   /**< text_refresh_div：文字刷新分频（每 N 帧刷新一次） */
        APP_DISPLAY_BLIT_ROWS_MAX,     /**< blit_rows：每次 blit 传输的最大行数（DMA2D 分块） */
        (uint16_t)SRP_FREQ_BIN_START,  /**< freq_bin_start：默认频段起始 bin */
        (uint16_t)SRP_FREQ_BIN_END     /**< freq_bin_end：默认频段结束 bin */
    }
};

static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)           /* 低于下限：直接返回下限值 */
    {
        return lo;
    }
    if (v > hi)           /* 高于上限：直接返回上限值 */
    {
        return hi;
    }
    return v;             /* 在范围内：原值返回 */
}

/**
 * @brief   App_Display 模式枚举 -> 运行时配置模式枚举（内部转换）
 * @details App_Display 模块使用自己的枚举，运行时配置模块使用独立枚举。
 *          两套枚举在语义上一一对应，此函数负责从 Display 侧转换到 Runtime 侧，
 *          确保两个模块可以独立演进而不产生耦合。
 *
 * @param   mode  App_Display 模块的模式值
 * @return  对应的运行时配置模式值
 */
static App_Runtime_DisplayMode_t s_runtime_mode_from_display(App_Display_Mode_t mode)
{
    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:     /* 快速模式：最低延迟，牺牲画质 */
            return APP_RUNTIME_DISP_MODE_FAST;
        case APP_DISPLAY_MODE_CLEAN:    /* 清晰模式：最佳画质，CPU 占用较高 */
            return APP_RUNTIME_DISP_MODE_CLEAN;
        case APP_DISPLAY_MODE_BALANCED: /* 均衡模式：默认，兼顾性能与画质 */
        default:                        /* 未知值也映射到均衡模式，保证安全 */
            return APP_RUNTIME_DISP_MODE_BALANCED;
    }
}

/**
 * @brief   运行时配置模式枚举 -> App_Display 模式枚举（内部转换）
 * @details 与 s_runtime_mode_from_display 互为反向映射。
 *          在将运行时配置写入 App_Display 模块时调用。
 *
 * @param   mode  运行时配置的模式值
 * @return  对应的 App_Display 模式值
 */
static App_Display_Mode_t s_runtime_mode_to_display(App_Runtime_DisplayMode_t mode)
{
    switch (mode)
    {
        case APP_RUNTIME_DISP_MODE_FAST:     /* 快速模式 -> Display FAST */
            return APP_DISPLAY_MODE_FAST;
        case APP_RUNTIME_DISP_MODE_CLEAN:    /* 清晰模式 -> Display CLEAN */
            return APP_DISPLAY_MODE_CLEAN;
        case APP_RUNTIME_DISP_MODE_BALANCED: /* 均衡模式 -> Display BALANCED */
        default:                             /* 未知值安全回退到均衡模式 */
            return APP_DISPLAY_MODE_BALANCED;
    }
}

/**
 * @brief   App_Display 配置结构体 -> 运行时配置结构体（字段逐一复制 + 枚举转换）
 * @details 负责将 App_Display 内部使用的配置格式转换为运行时配置格式。
 *          两个结构体字段语义完全相同，差异仅在于插值/归一化枚举的不同定义。
 *
 * @param   src  源：App_Display 模块的运行时配置（只读）
 * @param   dst  目标：写入运行时配置结构体
 */
static void s_runtime_displaycfg_from_display(const App_Display_RuntimeCfg_t *src,
                                              App_Runtime_DisplayCfg_t *dst)
{
    if ((src == NULL) || (dst == NULL)) /* 空指针保护，防止野指针访问 */
    {
        return;
    }

    dst->ema_attack        = src->ema_attack;        /* EMA 攻击系数直接复制 */
    dst->ema_decay         = src->ema_decay;         /* EMA 衰减系数直接复制 */
    dst->db_floor          = src->db_floor;          /* 动态范围底限直接复制 */
    dst->fine_gain         = src->fine_gain;         /* 精细增益直接复制 */
    dst->gamma             = src->gamma;             /* 伽马系数直接复制 */
    dst->noise_gate_ratio  = src->noise_gate_ratio;  /* 噪声门限比例直接复制 */
    dst->noise_adapt_gain  = src->noise_adapt_gain;  /* 自适应噪声增益直接复制 */
    dst->heatmap_opacity   = src->heatmap_opacity;   /* 热力图透明度直接复制 */
    dst->smooth_passes     = src->smooth_passes;     /* 平滑迭代次数直接复制 */
    dst->fine_fusion_enable = src->fine_fusion_enable; /* 精细融合开关直接复制 */
    dst->draw_coarse_grid  = src->draw_coarse_grid;  /* 粗网格绘制开关直接复制 */
    /* 插值模式：Display 枚举 -> Runtime 枚举（不同模块定义相同语义的不同枚举值） */
    dst->interp_mode = (src->interp_mode == APP_DISPLAY_INTERP_BILINEAR)
                           ? APP_RUNTIME_DISP_INTERP_BILINEAR
                           : APP_RUNTIME_DISP_INTERP_NEAREST;
    /* 归一化模式：Display 枚举 -> Runtime 枚举 */
    dst->norm_mode = (src->norm_mode == APP_DISPLAY_NORM_FULL)
                         ? APP_RUNTIME_DISP_NORM_FULL
                         : APP_RUNTIME_DISP_NORM_FAST;
    dst->text_refresh_div  = src->text_refresh_div;  /* 文字刷新分频直接复制 */
    dst->blit_rows         = src->blit_rows;         /* DMA2D 分块行数直接复制 */
}

/**
 * @brief   运行时配置结构体 -> App_Display 配置结构体（与上方函数互为逆操作）
 * @details 在将运行时配置下发到 App_Display 模块时调用。
 *          枚举转换方向与 s_runtime_displaycfg_from_display 相反。
 *
 * @param   src  源：运行时配置结构体（只读）
 * @param   dst  目标：写入 App_Display 运行时配置结构体
 */
static void s_runtime_displaycfg_to_display(const App_Runtime_DisplayCfg_t *src,
                                            App_Display_RuntimeCfg_t *dst)
{
    if ((src == NULL) || (dst == NULL)) /* 空指针保护 */
    {
        return;
    }

    dst->ema_attack        = src->ema_attack;        /* EMA 攻击系数 */
    dst->ema_decay         = src->ema_decay;         /* EMA 衰减系数 */
    dst->db_floor          = src->db_floor;          /* 动态范围底限 */
    dst->fine_gain         = src->fine_gain;         /* 精细增益 */
    dst->gamma             = src->gamma;             /* 伽马系数 */
    dst->noise_gate_ratio  = src->noise_gate_ratio;  /* 噪声门限比例 */
    dst->noise_adapt_gain  = src->noise_adapt_gain;  /* 自适应噪声增益 */
    dst->heatmap_opacity   = src->heatmap_opacity;   /* 热力图透明度 */
    dst->smooth_passes     = src->smooth_passes;     /* 平滑迭代次数 */
    dst->fine_fusion_enable = src->fine_fusion_enable; /* 精细融合开关 */
    dst->draw_coarse_grid  = src->draw_coarse_grid;  /* 粗网格绘制开关 */
    /* 插值模式：Runtime 枚举 -> Display 枚举（反向映射） */
    dst->interp_mode = (src->interp_mode == APP_RUNTIME_DISP_INTERP_BILINEAR)
                           ? APP_DISPLAY_INTERP_BILINEAR
                           : APP_DISPLAY_INTERP_NEAREST;
    /* 归一化模式：Runtime 枚举 -> Display 枚举（反向映射） */
    dst->norm_mode = (src->norm_mode == APP_RUNTIME_DISP_NORM_FULL)
                         ? APP_DISPLAY_NORM_FULL
                         : APP_DISPLAY_NORM_FAST;
    dst->text_refresh_div  = src->text_refresh_div;  /* 文字刷新分频 */
    dst->blit_rows         = src->blit_rows;         /* DMA2D 分块行数 */
}

/**
 * @brief   从 App_Display 模块回读配置并同步到运行时配置单例
 * @details 在 SetDisplayMode / SetDisplayCfg 调用之后执行，
 *          确保 s_runtime_cfg 与 App_Display 内部状态保持一致。
 *          使用临界区保护写操作，防止 UI 任务读到中间状态。
 */
static void s_runtime_sync_from_display(void)
{
    App_Display_RuntimeCfg_t display_cfg;   /* 临时存放从 App_Display 读回的配置 */
    App_Display_Mode_t       display_mode;  /* 临时存放从 App_Display 读回的模式 */

    App_Display_GetConfig(&display_cfg);    /* 读取 App_Display 当前运行时配置 */
    display_mode = App_Display_GetMode();   /* 读取 App_Display 当前渲染模式 */

    taskENTER_CRITICAL();                   /* 进入临界区：禁止任务切换，保证原子写 */
    s_runtime_cfg.display_mode = s_runtime_mode_from_display(display_mode);           /* 同步模式 */
    s_runtime_displaycfg_from_display(&display_cfg, &s_runtime_cfg.display_cfg);     /* 同步配置 */
    taskEXIT_CRITICAL();                    /* 退出临界区：恢复调度 */
}

/**
 * @brief   读取完整运行时配置快照（线程安全）
 * @details 在临界区内完整复制 s_runtime_cfg，保证调用者拿到的是一致的快照，
 *          而非各字段分开读取时可能出现的撕裂状态。
 *
 * @param   cfg  输出参数，写入当前运行时配置副本
 */
void App_RuntimeConfig_Get(App_Runtime_Config_t *cfg)
{
    if (cfg == NULL)            /* 空指针保护 */
    {
        return;
    }

    taskENTER_CRITICAL();       /* 临界区：保证多字节结构体读取的原子性 */
    *cfg = s_runtime_cfg;       /* 整体结构体赋值复制 */
    taskEXIT_CRITICAL();        /* 恢复调度 */
}

/**
 * @brief   设置 UI 目标帧率（线程安全，自动 clamp）
 * @param   fps  期望帧率，将被 clamp 到 [UI_FPS_MIN, UI_FPS_MAX]
 */
void App_RuntimeConfig_SetUiTargetFps(uint32_t fps)
{
    fps = s_clamp_u32(fps, UI_FPS_MIN, UI_FPS_MAX); /* 边界钳位，防止非法值 */
    taskENTER_CRITICAL();
    s_runtime_cfg.ui_target_fps = fps;               /* 写入配置单例 */
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取 UI 目标帧率（线程安全）
 * @return  当前目标帧率 (fps)
 */
uint32_t App_RuntimeConfig_GetUiTargetFps(void)
{
    uint32_t fps;
    taskENTER_CRITICAL();
    fps = s_runtime_cfg.ui_target_fps;  /* 临界区内读取，防止写端同时修改 */
    taskEXIT_CRITICAL();
    return fps;
}

/**
 * @brief   设置音频算法抽帧比（线程安全，自动 clamp）
 * @details decim=1 每帧都跑 SRP-PHAT，decim=N 每 N 帧才跑一次。
 *          增大 decim 可降低 CPU 占用，但会降低声源位置更新频率。
 * @param   decim  抽帧比，clamp 到 [AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX]
 */
void App_RuntimeConfig_SetAudioAlgoDecim(uint32_t decim)
{
    decim = s_clamp_u32(decim, AUDIO_ALGO_DECIM_MIN, AUDIO_ALGO_DECIM_MAX); /* clamp */
    taskENTER_CRITICAL();
    s_runtime_cfg.audio_algo_decim = decim;  /* 写入配置，音频任务下一帧生效 */
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取音频算法抽帧比（线程安全）
 * @return  当前抽帧比
 */
uint32_t App_RuntimeConfig_GetAudioAlgoDecim(void)
{
    uint32_t decim;
    taskENTER_CRITICAL();
    decim = s_runtime_cfg.audio_algo_decim;
    taskEXIT_CRITICAL();
    return decim;
}

/**
 * @brief   设置性能统计开关（线程安全）
 * @details 内部调用 App_Perf_SetEnabled()，若 DWT 不可用则开启失败，
 *          同步将实际状态写回 s_runtime_cfg.perf_enabled。
 * @param   enable  1=开启，0=关闭
 */
void App_RuntimeConfig_SetPerfEnabled(uint8_t enable)
{
    App_Perf_SetEnabled(enable);                           /* 尝试开启 DWT 并设置标志 */
    taskENTER_CRITICAL();
    /* 读取 Perf 模块实际状态同步回配置（若 DWT 不可用，开启会失败，此处反映真实状态） */
    s_runtime_cfg.perf_enabled = (App_Perf_IsEnabled() != 0u) ? 1u : 0u;
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取性能统计是否已启用（线程安全）
 * @return  1=已启用，0=未启用
 */
uint8_t App_RuntimeConfig_GetPerfEnabled(void)
{
    uint8_t enabled;
    taskENTER_CRITICAL();
    enabled = s_runtime_cfg.perf_enabled;
    taskEXIT_CRITICAL();
    return enabled;
}

/**
 * @brief   设置显示渲染模式（线程安全）
 * @details 先将运行时枚举转换为 App_Display 枚举，调用 App_Display_SetMode()，
 *          再将 App_Display 的实际状态回读同步到 s_runtime_cfg，保证一致性。
 * @param   mode  新的显示模式（FAST / BALANCED / CLEAN）
 */
void App_RuntimeConfig_SetDisplayMode(App_Runtime_DisplayMode_t mode)
{
    App_Display_Mode_t display_mode = s_runtime_mode_to_display(mode); /* 枚举转换 */
    App_Display_SetMode(display_mode);   /* 写入 App_Display 模块 */
    s_runtime_sync_from_display();       /* 回读同步，保证 s_runtime_cfg 与实际一致 */
}

/**
 * @brief   读取当前显示渲染模式（线程安全）
 * @return  当前模式枚举值
 */
App_Runtime_DisplayMode_t App_RuntimeConfig_GetDisplayMode(void)
{
    App_Runtime_DisplayMode_t mode;
    taskENTER_CRITICAL();
    mode = s_runtime_cfg.display_mode;
    taskEXIT_CRITICAL();
    return mode;
}

/**
 * @brief   批量设置显示参数（线程安全）
 * @details 将运行时格式的配置转换后写入 App_Display，然后回读同步。
 *          调用后所有显示参数（EMA/伽马/噪声门限等）立即对下一帧生效。
 * @param   cfg  新的显示参数（若为 NULL 则直接返回）
 */
void App_RuntimeConfig_SetDisplayCfg(const App_Runtime_DisplayCfg_t *cfg)
{
    App_Display_RuntimeCfg_t display_cfg;  /* 转换后的中间结构体 */

    if (cfg == NULL)   /* 空指针保护 */
    {
        return;
    }

    s_runtime_displaycfg_to_display(cfg, &display_cfg); /* 格式转换：Runtime -> Display */
    App_Display_SetConfig(&display_cfg);                 /* 写入 App_Display 模块 */
    s_runtime_sync_from_display();                       /* 回读同步 */
}

/**
 * @brief   读取当前显示参数（线程安全）
 * @param   cfg  输出参数，写入当前显示参数副本
 */
void App_RuntimeConfig_GetDisplayCfg(App_Runtime_DisplayCfg_t *cfg)
{
    if (cfg == NULL)   /* 空指针保护 */
    {
        return;
    }

    taskENTER_CRITICAL();
    *cfg = s_runtime_cfg.display_cfg;  /* 整体结构体复制，保证一致性 */
    taskEXIT_CRITICAL();
}

/* -------------------------------------------------------------------- */
/*  操作模式 (Operating Mode)                                            */
/* -------------------------------------------------------------------- */

/**
 * @brief 设置设备操作模式 (含硬件侧效应)
 * @param mode 目标操作模式
 */
void App_RuntimeConfig_SetOperatingMode(App_OperatingMode_t mode)
{
    App_OperatingMode_t old_mode;

    if ((uint32_t)mode >= (uint32_t)APP_MODE_COUNT)
    {
        return;
    }

    taskENTER_CRITICAL();
    old_mode = s_runtime_cfg.operating_mode;
    s_runtime_cfg.operating_mode = mode;
    taskEXIT_CRITICAL();

    /* 硬件侧效应: 切离旧模式 */
    if (old_mode == APP_MODE_NIGHT && mode != APP_MODE_NIGHT)
    {
        App_NightMode_Disable();
    }

    /* 硬件侧效应: 切入新模式 */
    if (mode == APP_MODE_NIGHT && old_mode != APP_MODE_NIGHT)
    {
        App_NightMode_Enable();
    }
}

/**
 * @brief 获取当前操作模式
 * @return 当前操作模式
 */
App_OperatingMode_t App_RuntimeConfig_GetOperatingMode(void)
{
    App_OperatingMode_t mode;
    taskENTER_CRITICAL();
    mode = s_runtime_cfg.operating_mode;
    taskEXIT_CRITICAL();
    return mode;
}

/**
 * @brief   Initialize the runtime configuration singleton
 * @details Syncs perf state and current display defaults before tasks start.
 */
void App_RuntimeConfig_Init(void)
{
    taskENTER_CRITICAL();
    s_runtime_cfg.perf_enabled = (App_Perf_IsEnabled() != 0u) ? 1u : 0u;
    taskEXIT_CRITICAL();
    s_runtime_sync_from_display();
}

void App_RuntimeConfig_SetFreqBand(uint16_t bin_start, uint16_t bin_end)
{
    App_FreqBand_t band;

    if (bin_end > (uint16_t)SRP_FREQ_BIN_END)
    {
        bin_end = (uint16_t)SRP_FREQ_BIN_END;
    }
    if (bin_start > bin_end)
    {
        bin_start = bin_end;
    }

    taskENTER_CRITICAL();
    s_runtime_cfg.display_cfg.freq_bin_start = bin_start;
    s_runtime_cfg.display_cfg.freq_bin_end   = bin_end;
    taskEXIT_CRITICAL();

    band.start_bin = bin_start;
    band.end_bin   = bin_end;
    App_Spectrum_SetActiveBand(band);
}

void App_RuntimeConfig_GetFreqBand(uint16_t *bin_start, uint16_t *bin_end)
{
    taskENTER_CRITICAL();
    if (bin_start != NULL)
    {
        *bin_start = s_runtime_cfg.display_cfg.freq_bin_start;
    }
    if (bin_end != NULL)
    {
        *bin_end = s_runtime_cfg.display_cfg.freq_bin_end;
    }
    taskEXIT_CRITICAL();
}
