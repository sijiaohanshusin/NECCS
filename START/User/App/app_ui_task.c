/**
 * @file    app_ui_task.c
 * @brief   UI 显示任务 —— FreeRTOS 任务入口与渲染后端调度
 * @details 本模块实现 UI_Display_Task，负责：
 *          1. 渲染后端的动态切换（Legacy DMA2D / LVGL）
 *          2. 从 xPositionQueue 接收声源定位结果
 *          3. 以目标帧率调用 render 回调完成声学热力图显示
 *          4. 性能计数（perf）、CLI 轮询、触摸轮询
 */
#include "main.h"

#include "ai_beamforming.h"
#include "app_camera.h"
#include "app_display.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_touch.h"
#include "app_trigger.h"
#include "app_ui_cli.h"
#include "app_user_config.h"
#include "LCD/ltdc.h"

#include <stdio.h>
#include <string.h>

#if (APP_LVGL_ENABLE != 0u)
#include "app_lvgl_ui.h"
#include "app_ui_screens.h"
#include "lvgl/lvgl.h"

void lv_port_disp_init(void);
void lv_port_disp_reconfigure(void);
void lv_port_indev_init(void);
#endif

/**
 * @brief UI 渲染后端虚表结构体
 *
 * 通过函数指针实现 Legacy / LVGL 渲染后端的运行时切换：
 *  - is_ready : 查询硬件是否初始化完毕
 *  - init     : 执行硬件初始化（LCD、LTDC、LVGL 等）
 *  - render   : 完成一帧声学热力图渲染并输出到 LCD
 */
typedef struct
{
    uint8_t (*is_ready)(void);          /**< 查询就绪：1=可渲染，0=未初始化 */
    void    (*init)(void);              /**< 执行初始化 */
    void    (*render)(const Sound_Pos_t   *pos,
                      const SRP_VisFrame_t *vis_frame,
                      uint32_t             frame_seq,
                      uint8_t              sai_dma_active); /**< SAI DMA 活跃标志 */
} App_UiRendererOps_t;

/* Legacy 渲染后端实现：委托给 App_Display 模块                               */

static uint8_t s_ui_legacy_is_ready(void);   /**< 查询 App_Display 就绪状态 */
static void    s_ui_legacy_init(void);        /**< 调用 App_Display_Init() */
static void    s_ui_legacy_render(const Sound_Pos_t *pos,
                                  const SRP_VisFrame_t *vis_frame,
                                  uint32_t frame_seq,
                                  uint8_t sai_dma_active); /**< 调用 App_Display_Render() */

#if (APP_LVGL_ENABLE != 0u)
static uint8_t s_ui_lvgl_is_ready(void);
static void    s_ui_lvgl_init(void);
static void    s_ui_lvgl_render(const Sound_Pos_t *pos,
                                const SRP_VisFrame_t *vis_frame,
                                uint32_t frame_seq,
                                uint8_t sai_dma_active);
#endif

/** @brief Legacy 渲染后端虚表（const，存于 Flash） */
static const App_UiRendererOps_t s_ui_renderer_legacy_ops = {
    s_ui_legacy_is_ready,   /**< is_ready 回调 */
    s_ui_legacy_init,        /**< init 回调 */
    s_ui_legacy_render       /**< render 回调 */
};

#if (APP_LVGL_ENABLE != 0u)
static const App_UiRendererOps_t s_ui_renderer_lvgl_ops = {
    s_ui_lvgl_is_ready,
    s_ui_lvgl_init,
    s_ui_lvgl_render
};

static uint8_t s_ui_lvgl_ready = 0u;
static uint8_t s_ui_lvgl_core_inited = 0u;
static uint8_t s_ui_lvgl_ports_inited = 0u;
static TickType_t s_ui_lvgl_last_tick = 0u;

/** @brief LVGL FPS 计算状态 */
static uint32_t s_ui_lvgl_fps_frames = 0u;
static TickType_t s_ui_lvgl_fps_tick = 0u;
static uint32_t s_ui_lvgl_fps_value = 0u;
#endif

/** @brief 当前活跃的渲染后端虚表指针，默认 Legacy */
static const App_UiRendererOps_t *s_ui_renderer = &s_ui_renderer_legacy_ops;

/** @brief 当前渲染后端枚举值，volatile 供跨任务读取 */
static volatile App_UiRenderBackend_t s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;

/**
 * @brief  将无符号整数限制在 [lo, hi] 范围内
 * @param  v   待限制的值
 * @param  lo  下限
 * @param  hi  上限
 * @return 限制后的值
 */
static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/**
 * @brief   切换 UI 渲染后端（Legacy / LVGL）
 * @details 在临界区内修改渲染器虚表指针，使下一次 render() 调用走新后端。
 *          切换到 LVGL 时启用 GPU 合成层；切换到 Legacy 时使用纯 DMA2D 渲染。
 * @param   backend  目标渲染后端枚举值
 */
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend)
{
    taskENTER_CRITICAL();
    switch (backend)
    {
#if (APP_LVGL_ENABLE != 0u)
        case APP_UI_RENDER_BACKEND_LVGL:
            s_ui_renderer = &s_ui_renderer_lvgl_ops;
            s_ui_backend  = APP_UI_RENDER_BACKEND_LVGL;
            break;
#endif
        case APP_UI_RENDER_BACKEND_LEGACY:
        default:
            s_ui_renderer = &s_ui_renderer_legacy_ops;
            s_ui_backend  = APP_UI_RENDER_BACKEND_LEGACY;
            break;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief   获取当前 UI 渲染后端类型
 * @return  当前活跃的渲染后端枚举值
 */
App_UiRenderBackend_t App_UiRenderer_GetBackend(void)
{
    App_UiRenderBackend_t backend;
    taskENTER_CRITICAL();
    backend = s_ui_backend;
    taskEXIT_CRITICAL();
    return backend;
}

/**
 * @brief   Legacy 后端 is_ready 回调
 * @return  1=LCD 已初始化完毕可渲染；0=未就绪
 */
static uint8_t s_ui_legacy_is_ready(void)
{
    return App_Display_IsReady();
}

/**
 * @brief   Legacy 后端 init 回调
 * @details 调用 App_Display_Init() 完成 LCD 硬件初始化和 LTDC/DMA2D 配置。
 *          初始化前禁用 LVGL overlay，确保 Legacy 模式独占显示。
 */
static void s_ui_legacy_init(void)
{
    App_LvglUi_SetOverlayEnabled(0u);
    App_Display_Init();
}

/**
 * @brief   Legacy 后端 render 回调，封装对 App_Display_Render 的调用
 * @param   pos             声源定位结果（方位角+仰角+能量值）
 * @param   vis_frame       SRP 可视化帧数据（能量网格等）
 * @param   frame_seq       UI 帧序号
 * @param   sai_dma_active  SAI DMA 活跃标志（1=有音频流，0=静音）
 */
static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active)
{

    App_CameraFrame_t camera_frame = {0};

    (void)App_Camera_AcquireLatestFrame(&camera_frame);
    App_Display_Render(pos, vis_frame, &camera_frame, frame_seq, sai_dma_active);
    App_Camera_ReleaseFrame(&camera_frame);
}

#if (APP_LVGL_ENABLE != 0u)

/**
 * @brief   LVGL 后端 is_ready 回调
 * @return  1=LVGL 已完成初始化且显示端口就绪；0=未就绪
 */
static uint8_t s_ui_lvgl_is_ready(void)
{
    return s_ui_lvgl_ready;
}

/**
 * @brief   LVGL 后端 init 回调
 * @details 依次完成 lv_init() → 显示端口 → 输入设备 → App_LvglUi_Init()。
 *          若已初始化过则仅重新配置显示端口（lv_port_disp_reconfigure）。
 */
static void s_ui_lvgl_init(void)
{
    if (App_Display_IsReady() == 0u)
    {
        return;
    }

    if (s_ui_lvgl_core_inited == 0u)
    {
        lv_init();
        s_ui_lvgl_core_inited = 1u;
    }

    if (s_ui_lvgl_ports_inited == 0u)
    {
        lv_port_disp_init();
        lv_port_indev_init();
        s_ui_lvgl_ports_inited = 1u;
    }
    else
    {
        lv_port_disp_reconfigure();
    }

    s_ui_lvgl_last_tick = xTaskGetTickCount();
    s_ui_lvgl_ready = 1u;
    App_LvglUi_Init();
    App_LvglUi_SetOverlayEnabled(1u);
}

/**
 * @brief   LVGL 后端 render 回调
 * @details 1. 用 RTOS tick 差值驱动 lv_tick_inc()
 *          2. lv_timer_handler() 处理 LVGL 事件
 *          3. 获取相机帧并调用 App_Display_Render 完成声学热力图叠加
 * @param   pos             声源定位结果
 * @param   vis_frame       SRP 可视化帧
 * @param   frame_seq       UI 帧序号
 * @param   sai_dma_active  SAI DMA 活跃标志
 */
static void s_ui_lvgl_render(const Sound_Pos_t *pos,
                             const SRP_VisFrame_t *vis_frame,
                             uint32_t frame_seq,
                             uint8_t sai_dma_active)
{
    App_CameraFrame_t camera_frame = {0};
    TickType_t now;
    uint32_t delta_ms;

    if (s_ui_lvgl_ready == 0u)
    {
        return;
    }

    now = xTaskGetTickCount();
    delta_ms = (uint32_t)(now - s_ui_lvgl_last_tick) * (uint32_t)portTICK_PERIOD_MS;
    s_ui_lvgl_last_tick = now;

    if (delta_ms != 0u)
    {
        /* Feed LVGL directly from the RTOS tick so no extra timer is needed. */
        lv_tick_inc(delta_ms);
    }

    App_LvglUi_Process();

    /* 计算 LVGL FPS 并馈送实时数据到屏幕框架 */
    {
        s_ui_lvgl_fps_frames++;
        if ((now - s_ui_lvgl_fps_tick) >= pdMS_TO_TICKS(1000u))
        {
            s_ui_lvgl_fps_value = s_ui_lvgl_fps_frames;
            s_ui_lvgl_fps_frames = 0u;
            s_ui_lvgl_fps_tick = now;
        }
    }
    {
        App_UiLiveData_t ld;
        ld.x_angle   = pos->x_angle;
        ld.y_angle   = pos->y_angle;
        ld.energy    = pos->energy;
        ld.ui_fps    = s_ui_lvgl_fps_value;
        ld.audio_fps = 0u;
        ld.sai_active = sai_dma_active;
        App_UiScreens_SetLiveData(&ld);
    }

    (void)lv_timer_handler();

    (void)App_Camera_AcquireLatestFrame(&camera_frame);
    App_Display_Render(pos, vis_frame, &camera_frame, frame_seq, sai_dma_active);
    App_Camera_ReleaseFrame(&camera_frame);
}
#endif

/**
 * @brief   计算当前 UI 渲染周期对应的 FreeRTOS ticks
 * @details LVGL 模式使用固定周期 APP_LVGL_HANDLER_PERIOD_MS。
 *          Legacy 模式：period_ms = round(1000 / fps)，ticks = pdMS_TO_TICKS(period_ms)，最小为 1。
 * @return  渲染周期的 FreeRTOS tick 数
 */
static uint32_t s_ui_period_ticks(void)
{
#if (APP_LVGL_ENABLE != 0u)
    if (App_UiRenderer_GetBackend() == APP_UI_RENDER_BACKEND_LVGL)
    {
        TickType_t lvgl_ticks = pdMS_TO_TICKS(APP_LVGL_HANDLER_PERIOD_MS);

        if (lvgl_ticks == 0u)
        {
            lvgl_ticks = 1u;
        }
        return (uint32_t)lvgl_ticks;
    }
#endif

    uint32_t fps = s_clamp_u32(App_RuntimeConfig_GetUiTargetFps(), UI_FPS_MIN, UI_FPS_MAX);

    uint32_t period_ms = (1000u + (fps / 2u)) / fps;

    TickType_t ticks = pdMS_TO_TICKS(period_ms);

    if (ticks == 0u)
    {
        ticks = 1u;
    }
    return (uint32_t)ticks;
}

#define ui_cli_poll App_UiCli_Poll

/**
 * @brief   UI 显示主任务（FreeRTOS 任务入口）
 * @details 任务主循环：CLI 轮询 → 后端就绪检查 → 接收 SRP 定位 → 渲染 → 帧率控制。
 *
 * 关键行为：
 * - 启动时初始化渲染后端，失败则每 UI_RETRY_INIT_MS 重试
 * - 后端切换时自动触发新后端 init
 * - 从 xPositionQueue 零等待接收定位结果，队列积压时 drain 到最新帧
 * - 通过 lock-free 双缓冲获取 SRP 可视化快照
 *
 * @param   pvParameters  FreeRTOS 任务参数（未使用）
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;

    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};  /* 从队列接收的声源定位结果 */

    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};   /* 上次有效定位，队列空时沿用 */

    SRP_VisFrame_t vis_snapshot = {0};  /* SRP 可视化帧快照 */

    uint32_t ui_frame_seq = 0u;  /* UI 帧序号（单调递增） */

    uint32_t last_audio_isr_seq = 0u;  /* 上次 ISR 序号，检测 DMA 活跃 */

    uint8_t audio_idle_frames = 0xFFu;  /* 连续无音频帧计数，0xFF=启动静音 */

    TickType_t next_render_wake;  /* vTaskDelayUntil 唤醒基准 */

    TickType_t last_init_try = 0u;  /* 上次初始化重试时间戳 */

    uint32_t last_dma2d_timeout = 0u;  /* DMA2D 超时计数（去重日志） */
    App_UiRenderBackend_t current_backend = APP_UI_RENDER_BACKEND_LEGACY;
    App_UiRenderBackend_t last_backend = App_UiRenderer_GetBackend();

    /* 尝试初始化渲染后端，若 LCD 未就绪则会在主循环中重试 */
    if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
    {
        s_ui_renderer->init();
    }
    last_init_try    = xTaskGetTickCount();
    next_render_wake = last_init_try;

    /* ================================================================
     * 主循环：每轮完成一帧渲染，由 vTaskDelayUntil 控制帧率
     * ================================================================ */
    for (;;)
    {
        uint32_t t_loop;       /* 主循环计时起始 */
        uint8_t  sai_dma_active; /* SAI DMA 活跃：1=有音频，0=静音 */

        /* ---- 步骤 1: CLI / 触摸轮询 + 后端切换检测 ---- */
        ui_cli_poll();
        App_Touch_Poll();
        current_backend = App_UiRenderer_GetBackend();

        if (current_backend != last_backend)
        {
            last_backend = current_backend;
#if (APP_LVGL_ENABLE != 0u)
            if ((current_backend == APP_UI_RENDER_BACKEND_LVGL) && (s_ui_renderer != NULL))
            {
                s_ui_renderer->init();
            }
            else
#endif
            if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
            {
                s_ui_renderer->init();
            }
            last_init_try = xTaskGetTickCount();
        }

        /* ---- 步骤 2: 后端就绪检查，未就绪则定时重试 ---- */
        if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG

                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,
                       (unsigned long)g_display_init_error,
                       (unsigned long)g_lcd_init_stage,
                       (unsigned long)g_ltdc_init_stage);
#endif
                s_ui_renderer->init();
                last_init_try = now;
            }
            taskYIELD();  /* 让出 CPU */
            continue;
        }

        /* ---- 步骤 3: 性能计数 ---- */
        App_Perf_CountUiLoop();         /* UI 循环计数+1 */
        App_Perf_MaybePrintRates();     /* 周期性打印吞吐率 */
        t_loop = App_Perf_BeginCycles(); /* 开始计时 */
        sai_dma_active = 0u;

        /* ---- 步骤 4: 从定位队列接收声源位置（零等待） ---- */
        {
            /* LVGL overlays the legacy scene, so the scene data still needs to update. */
            if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;
                g_ui_queue_rx_count++;

                while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
                {
                    last_pos = draw_pos;
                    g_ui_queue_rx_count++;
                }
            }
            else
            {
                g_ui_queue_timeout_count++;
            }
        }

        ui_frame_seq++;  /* UI 帧序号递增 */

        /* ---- 步骤 4b: 触发模式检测 ---- */
        (void)App_Trigger_Feed(last_pos.energy);
        if (App_Trigger_GetState() == APP_TRIGGER_TRIGGERED)
        {
            /* 冻结显示：跳过后续渲染更新，保持当前帧 */
            App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);
            vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
            continue;
        }

        /* ---- 步骤 5: 检测 SAI DMA 活跃状态 ---- */
        {
            uint32_t audio_seq = g_audio_frame_seq_isr;  /* 读 ISR 序号（volatile） */
            if (audio_seq != last_audio_isr_seq)
            {
                last_audio_isr_seq = audio_seq;
                audio_idle_frames  = 0u;
            }
            else if (audio_idle_frames < 0xFFu)
            {
                audio_idle_frames++;
            }

            sai_dma_active = (audio_idle_frames <= APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES) ? 1u : 0u;
        }

        /* ---- 步骤 6: 获取 SRP 可视化快照 ---- */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            (void)AI_SRP_GetLatestVisualizationFrame(&vis_snapshot);
            App_Perf_EndCycles(APP_PERF_SEC_UI_SNAPSHOT, t_sec);
        }

        /* ---- 步骤7: 调用渲染后端绘制当前帧 ---- */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            s_ui_renderer->render(&last_pos,
                                  &vis_snapshot,
                                  ui_frame_seq,
                                  sai_dma_active);
            App_Perf_EndCycles(APP_PERF_SEC_UI_RENDER, t_sec);
        }
        g_ui_render_count++;  /* 渲染完成+1 */

        /* ---- 步骤 8: DMA2D 超时日志 ---- */
        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)
        {
#if UI_DEBUG_LOG

            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,
                   (unsigned int)g_ltdc_panel_id);
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;
        }

        /* ---- 步骤 9: 结束计时 + 帧率控制 ---- */
        App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);

        /* vTaskDelayUntil 用于帧率控制，确保固定周期渲染 */
        vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
    }
}
