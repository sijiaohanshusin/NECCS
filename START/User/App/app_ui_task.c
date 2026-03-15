/**
 * @file    app_ui_task.c
 * @brief   UI display task implementation
 */
#include "main.h"

#include "ai_beamforming.h"
#include "app_camera.h"
#include "app_display.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_task_cfg.h"
#include "app_ui_cli.h"
#include "LCD/ltdc.h"

#include <stdio.h>

/**
 * @brief UI 渲染后端操作表（虚函数表）
 *
 * 包含三个函数指针，分别对应：
 *  - is_ready : 查询显示层是否已完成初始化
 *  - init     : 执行显示层初始化（幂等，可重复调用）
 *  - render   : 执行一帧渲染并提交到 LCD
 *
 * 通过函数指针而非直接调用，未来可无缝切换到不同渲染实现（如 GPU 加速后端）。
 */
typedef struct
{
    uint8_t (*is_ready)(void);          /**< 返回 1 表示显示硬件已就绪，可以开始渲染 */
    void    (*init)(void);              /**< 初始化显示层：LCD 时序、LTDC 配置、帧缓冲清零 */
    void    (*render)(const Sound_Pos_t   *pos,        /**< 声源位置（方位角+能量） */
                      const SRP_VisFrame_t *vis_frame, /**< SRP 热力图可视化快照 */
                      uint32_t             frame_seq,  /**< UI 帧序号，用于文字刷新分频 */
                      uint8_t              sai_dma_active); /**< SAI DMA 活跃标志 */
} App_UiRendererOps_t;

/* -------------------------------------------------------------------------- */
/* 前向声明：Legacy 渲染后端的三个实现函数（定义在下方）                         */
/* -------------------------------------------------------------------------- */
static uint8_t s_ui_legacy_is_ready(void);   /**< 查询 App_Display 是否就绪 */
static void    s_ui_legacy_init(void);        /**< 调用 App_Display_Init() */
static void    s_ui_legacy_render(const Sound_Pos_t *pos,
                                  const SRP_VisFrame_t *vis_frame,
                                  uint32_t frame_seq,
                                  uint8_t sai_dma_active); /**< 调用 App_Display_Render() */

/** @brief Legacy 渲染后端操作表（const，编译期确定，存入 Flash） */
static const App_UiRendererOps_t s_ui_renderer_legacy_ops = {
    s_ui_legacy_is_ready,   /**< is_ready 函数指针 */
    s_ui_legacy_init,        /**< init 函数指针 */
    s_ui_legacy_render       /**< render 函数指针 */
};

/** @brief 当前激活的渲染后端指针，默认指向 Legacy 后端 */
static const App_UiRendererOps_t *s_ui_renderer = &s_ui_renderer_legacy_ops;

/** @brief 当前后端枚举值，用于外部查询（App_UiRenderer_GetBackend） */
static volatile App_UiRenderBackend_t s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;

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
 * @brief   切换 UI 渲染后端（线程安全）
 * @details 在临界区内切换函数指针表，UI 任务下一次调用 render() 时生效。
 *          当前仅支持 LEGACY 后端，预留未来扩展（如 GPU 加速后端）。
 * @param   backend  目标后端枚举值
 */
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend)
{
    taskENTER_CRITICAL();  /* 临界区：保证指针切换原子性，防止 UI 任务读到半切换状态 */
    switch (backend)
    {
        case APP_UI_RENDER_BACKEND_LEGACY:  /* 切换到 Legacy 软件渲染后端 */
        default:                            /* 未知后端也回退到 Legacy（安全保底） */
            s_ui_renderer = &s_ui_renderer_legacy_ops;   /* 更新操作表指针 */
            s_ui_backend  = APP_UI_RENDER_BACKEND_LEGACY; /* 更新枚举值 */
            break;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief   读取当前 UI 渲染后端枚举值（线程安全）
 * @return  当前后端枚举值
 */
App_UiRenderBackend_t App_UiRenderer_GetBackend(void)
{
    App_UiRenderBackend_t backend;
    taskENTER_CRITICAL();
    backend = s_ui_backend;
    taskEXIT_CRITICAL();
    return backend;
}

/* -------------------------------------------------------------------------- */
/* Legacy 渲染后端实现（直接透传到 App_Display 模块）                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief   Legacy 后端 is_ready 实现
 * @return  App_Display_IsReady() 的返回值（1=已初始化完成，0=未就绪）
 */
static uint8_t s_ui_legacy_is_ready(void)
{
    return App_Display_IsReady();  /* 直接查询 App_Display 初始化状态 */
}

/**
 * @brief   Legacy 后端 init 实现
 * @details 调用 App_Display_Init() 初始化 LCD 时序、LTDC、帧缓冲区。
 *          若 LCD 已就绪则为空操作（App_Display_Init 内部幂等处理）。
 */
static void s_ui_legacy_init(void)
{
    App_Display_Init();  /* 初始化显示层，包括 LCD 驱动、LTDC 配置、帧缓冲清零 */
}

/**
 * @brief   Legacy 后端 render 实现（透传参数到 App_Display_Render）
 * @param   pos           声源位置（方位角+能量），用于绘制十字光标
 * @param   vis_frame     SRP 热力图可视化快照，用于渲染背景热力图
 * @param   frame_seq     UI 帧序号，供文字刷新分频逻辑使用
 * @param   sai_dma_active  SAI DMA 是否活跃，用于显示 "无音频信号" 提示
 */
static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active)
{
    /* 直接转发所有参数到 App_Display 渲染模块，无额外处理 */
    App_CameraFrame_t camera_frame;

    App_Camera_GetLatestFrame(&camera_frame);
    App_Display_Render(pos, vis_frame, &camera_frame, frame_seq, sai_dma_active);
}

/**
 * @brief   计算 UI 任务的渲染周期（FreeRTOS ticks）
 * @details 将目标帧率换算为 vTaskDelayUntil 的周期 ticks：
 *            period_ms = round(1000 / fps)  （四舍五入：加 fps/2 再除以 fps）
 *            ticks = pdMS_TO_TICKS(period_ms)
 *
 *          示例：fps=20 -> period_ms=50ms -> 50 ticks (1ms/tick)
 *                fps=30 -> period_ms=33ms -> 33 ticks
 *                fps=5  -> period_ms=200ms -> 200 ticks
 *
 *          保证 ticks >= 1，防止 vTaskDelayUntil(0) 导致任务不让出 CPU。
 *
 * @return  渲染周期，单位：FreeRTOS ticks
 */
static uint32_t s_ui_period_ticks(void)
{
    /* 读取目标帧率并钳位，防止运行时配置被设置到范围外 */
    uint32_t fps = s_clamp_u32(App_RuntimeConfig_GetUiTargetFps(), UI_FPS_MIN, UI_FPS_MAX);

    /* 四舍五入换算：加 fps/2 再整除，相当于对 1000/fps 做四舍五入 */
    uint32_t period_ms = (1000u + (fps / 2u)) / fps;

    TickType_t ticks = pdMS_TO_TICKS(period_ms);  /* 毫秒转 FreeRTOS ticks */

    if (ticks == 0u)    /* 极端情况保护（portTICK_PERIOD_MS > period_ms 时可能为 0） */
    {
        ticks = 1u;     /* 至少延迟 1 个 tick，保证任务出让 CPU */
    }
    return (uint32_t)ticks;
}

#define ui_cli_poll App_UiCli_Poll

/**
 * @brief   UI 显示任务
 * @details 轮询位置队列并刷新显示：取最新位置 -> 快照 SRP 可视化数据 -> 渲染输出。
 *
 * 关键点：
 * - 显示未就绪时按 `UI_RETRY_INIT_MS` 周期重试初始化。
 * - 每帧仅使用队列中的最后一条位置数据，避免 UI 堆积。
 * - 在临界区内复制 SRP 可视化快照，避免读写竞争。
 *
 * @param   pvParameters  FreeRTOS 任务参数（未使用）
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;  /* 任务参数未使用，消除警告 */

    /* ---- 任务局部状态变量 ---- */

    /** @brief 当次从队列取到的声源位置（可能被多次读取覆盖，取最新值） */
    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 最后一次成功从队列读到的声源位置（队列为空时复用此值继续渲染） */
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};

    /** @brief SRP 热力图可视化快照（从音频任务临界区拷贝，避免读写竞争） */
    SRP_VisFrame_t vis_snapshot;

    /** @brief UI 帧序号，每渲染一帧递增，供文字刷新分频逻辑使用 */
    uint32_t ui_frame_seq = 0u;

    /** @brief 上次读取 g_audio_frame_seq_isr 时的值，用于检测 SAI DMA 是否仍在运行 */
    uint32_t last_audio_isr_seq = 0u;

    /** @brief 连续多帧 SAI DMA 序号未变化的帧数（达到阈值后认为无音频输入） */
    uint8_t audio_idle_frames = 0xFFu;  /* 初始化为最大值，触发首帧后立即归零 */

    /** @brief vTaskDelayUntil 的绝对唤醒时刻（保证帧周期稳定，不受渲染耗时影响） */
    TickType_t next_render_wake;

    /** @brief 上次尝试初始化显示层的 tick 值（限制重试频率） */
    TickType_t last_init_try = 0u;

    /** @brief 上次记录的 DMA2D 超时计数，用于变化检测（仅在 UI_DEBUG_LOG 模式打印） */
    uint32_t last_dma2d_timeout = 0u;

    /* ---- 任务启动时首次尝试初始化显示层 ---- */
    /* 若 LCD 尚未就绪（如时序初始化未完成），先尝试初始化 */
    if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
    {
        s_ui_renderer->init();  /* 初始化 LCD 驱动、LTDC 配置、清零帧缓冲 */
    }
    last_init_try    = xTaskGetTickCount();  /* 记录初始化尝试时刻 */
    next_render_wake = last_init_try;        /* 第一帧立即渲染 */

    /* ================================================================
     * 任务主循环（永不退出）
     * ================================================================ */
    for (;;)
    {
        uint32_t t_loop;       /* 本帧循环整体耗时计时起点 */
        uint8_t  sai_dma_active; /* SAI DMA 活跃标志（传给渲染器显示音频状态） */

        /* ---- 步骤 1：处理 CLI 输入（UART 命令行） ---- */
        /* 每帧都轮询 CLI，保证串口命令响应延迟 <= 1 帧周期（≈50ms@20fps） */
        ui_cli_poll();

        /* ---- 步骤 2：显示初始化重试逻辑 ---- */
        /* 若显示层未就绪（如 LCD 初始化失败），每隔 UI_RETRY_INIT_MS(1000ms) 重试一次 */
        if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                /* 调试模式下打印初始化失败时各模块的阶段值，辅助定位硬件问题 */
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,   /* App_Display 初始化阶段 */
                       (unsigned long)g_display_init_error,   /* App_Display 错误码 */
                       (unsigned long)g_lcd_init_stage,       /* LCD 驱动初始化阶段 */
                       (unsigned long)g_ltdc_init_stage);     /* LTDC 控制器初始化阶段 */
#endif
                s_ui_renderer->init();   /* 重试初始化 */
                last_init_try = now;     /* 更新重试时刻 */
            }
            taskYIELD();  /* 出让 CPU，不要空转等待，让音频任务有机会运行 */
            continue;     /* 回到循环顶部重新判断 */
        }

        /* ---- 步骤 3：性能统计递增与速率打印 ---- */
        App_Perf_CountUiLoop();         /* UI 循环计数 +1（用于 perf rate 速率计算） */
        App_Perf_MaybePrintRates();     /* 若达到打印周期（1s），输出速率统计 */
        t_loop = App_Perf_BeginCycles(); /* 记录本帧循环开始时刻 */

        /* ---- 步骤 4：消耗位置队列，取最新声源位置 ---- */
        /* 使用 timeout=0（非阻塞），若无新数据则复用 last_pos 继续渲染（保持帧率稳定） */
        if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
        {
            last_pos = draw_pos;           /* 保存最新位置 */
            g_ui_queue_rx_count++;         /* 统计成功接收次数 */

            /* 若队列中还有更新的位置（极少情况），继续消耗直到清空，取最后一个 */
            while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;       /* 持续覆盖，保证取到最新的 */
                g_ui_queue_rx_count++;
            }
        }
        else
        {
            /* 队列为空（音频任务尚未产生新结果），复用 last_pos 不更新 */
            g_ui_queue_timeout_count++;  /* 统计无新数据帧数（正常现象：当 UI FPS > 音频帧率 / decim 时） */
        }

        ui_frame_seq++;  /* 递增 UI 帧序号（传给渲染器用于分频刷新文字等元素） */

        /* ---- 步骤 5：检测 SAI DMA 活跃性 ---- */
        /* 通过监测 g_audio_frame_seq_isr 是否有变化来判断 SAI DMA 是否在运行 */
        {
            uint32_t audio_seq = g_audio_frame_seq_isr;  /* 读取 ISR 帧序号（volatile） */
            if (audio_seq != last_audio_isr_seq)          /* 序号有变化：DMA 仍在工作 */
            {
                last_audio_isr_seq = audio_seq;  /* 更新基准值 */
                audio_idle_frames  = 0u;          /* 重置空闲计数（表示 DMA 活跃） */
            }
            else if (audio_idle_frames < 0xFFu)   /* 序号未变：DMA 可能停止，累积空闲帧数 */
            {
                audio_idle_frames++;  /* 饱和计数，不溢出 */
            }
            /* 若连续空闲帧数 <= 阈值，认为 SAI DMA 活跃；超过阈值认为无音频输入 */
            sai_dma_active = (audio_idle_frames <= APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES) ? 1u : 0u;
        }

        /* ---- 步骤 6：在临界区内复制 SRP 可视化快照 ---- */
        /* SRP 可视化数据（热力图数据）由音频任务写入，UI 任务读取，存在竞争。
         * 使用临界区保护复制操作，防止读到中间状态（半拷贝的数据）。
         * 复制后快照与音频任务解耦，渲染期间不再需要持有临界区。 */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            taskENTER_CRITICAL();                         /* 禁止任务切换和中断 */
            AI_SRP_CopyVisualizationFrame(&vis_snapshot); /* 快速内存拷贝 */
            taskEXIT_CRITICAL();                          /* 恢复调度 */
            App_Perf_EndCycles(APP_PERF_SEC_UI_SNAPSHOT, t_sec);
        }

        /* ---- 步骤 7：调用渲染后端执行一帧渲染 ---- */
        /* 渲染流程（在 App_Display_Render 内部）：
         *   归一化热力图 -> colormap 映射 -> 双线性/最近邻插值 -> 平滑 ->
         *   绘制十字光标 -> 绘制坐标轴 -> 覆盖文字 -> DMA2D blit 到 LCD */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            s_ui_renderer->render(&last_pos,       /* 声源位置 */
                                  &vis_snapshot,   /* SRP 热力图快照 */
                                  ui_frame_seq,    /* 帧序号（文字分频用） */
                                  sai_dma_active); /* SAI DMA 状态 */
            App_Perf_EndCycles(APP_PERF_SEC_UI_RENDER, t_sec);
        }
        g_ui_render_count++;  /* 渲染帧计数 +1（可通过调试器观察 UI 实际运行帧率） */

        /* ---- 步骤 8：DMA2D 超时变化检测（可选调试日志） ---- */
        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)  /* 超时计数有增加 */
        {
#if UI_DEBUG_LOG
            /* 打印超时信息，帮助诊断 DMA2D 硬件或时序问题 */
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,  /* 最新超时计数 */
                   (unsigned int)g_ltdc_panel_id);              /* LCD 面板 ID */
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;  /* 更新基准值 */
        }

        /* ---- 步骤 9：结束本帧计时并等待下一帧唤醒时刻 ---- */
        App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);  /* 记录本帧总耗时 */

        /* vTaskDelayUntil：绝对时间延迟，自动补偿渲染耗时，保证帧周期恒定。
         * 若渲染时间超过一个帧周期，next_render_wake 会被推进以避免负延迟。
         * 相比 vTaskDelay（相对延迟），可有效防止帧率随负载波动而漂移。 */
        vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
    }
}
