/**
 * @file    app_main_task.c
 * @brief   应用层任务引导程序 — FreeRTOS 任务创建与模块初始化集中管理
 * @details
 * 本文件完成三件事：
 *   1. 定义运行时全局诊断计数器（跨任务可观测的变量）
 *   2. 实现 App_Task_Init()：在调度器启动前初始化所有模块并创建任务
 *   3. 导出任务句柄和队列句柄（供 ISR、其他模块使用）
 *
 * FreeRTOS 任务拓扑（上电后运行顺序）：
 *   freertos.c: MX_FREERTOS_Init() → App_Task_Init()
 *                                    ↓
 *       ┌──────── xAudioFrameQueue ────────┐
 *       ↑                                  ↓
 *   [SAI DMA ISR]        [Audio_Pipeline_Task, prio=4]
 *                                  ↓ xPositionQueue
 *                         [UI_Display_Task, prio=4]
 *
 * 初始化顺序（重要，有依赖关系）：
 *   1. 选择 UI 渲染后端（LVGL 或 Legacy）
 *   2. App_Perf_Init()     ← 初始化 DWT 计时器（需最先）
 *   3. App_RuntimeConfig_Init() ← 同步运行时配置
 *   4. 各功能模块初始化（Trigger、Laser、NoiseFloor、Anomaly 等）
 *   5. SD 卡初始化（允许失败，不阻塞启动）
 *   6. 创建队列和任务
 *
 * [注意] 本文件中的所有 for(;;){__NOP();} 死循环是致命错误处理点，
 *        若在此卡死，请检查 configTOTAL_HEAP_SIZE 是否足够。
 *
 * 依赖关系：
 *   app_main_task.c → FreeRTOS（xTaskCreate, xQueueCreate）
 *                   → ai_beamsteer.h（AI_BeamSteer_Init）
 *                   → app_noise_floor.h（App_NoiseFloor_Init）
 *                   → app_runtime.h（App_RuntimeConfig_Init）
 *                   → app_perf.h（App_Perf_Init）
 *                   → ...（多个功能模块）
 */
#include "app_main_task.h"      /* 本模块头文件：包含任务句柄声明、队列声明、函数原型 */

/* ---- 算法层头文件 ---- */
#include "ai_beamsteer.h"       /* DAS 波束成形控向初始化（AI_BeamSteer_Init） */

/* ---- App 功能模块头文件（按字母顺序，方便维护）---- */
#include "app_anomaly.h"        /* 声音异常检测模块（App_Anomaly_Init） */
#include "app_camera.h"         /* OV2640 摄像头任务初始化（App_Camera_TaskInit） */
#include "app_laser.h"          /* 激光笔控制模块（App_Laser_Init） */
#include "app_noise_floor.h"    /* 背景噪声基底估计模块（App_NoiseFloor_Init） */
#include "app_perf.h"           /* DWT 性能分析器（App_Perf_Init）*/
#include "app_profile.h"        /* 任务 CPU 占用分析（App_Profile_Init）*/
#include "app_runtime.h"        /* 运行时配置同步（App_RuntimeConfig_Init）*/
#include "app_sound_level.h"    /* 声级计（dBSPL）模块（App_SLM_Init）*/
#include "app_task_cfg.h"       /* 兼容入口，转发到 app_user_config.h（任务栈/优先级等）*/
#include "app_tracker.h"        /* 多声源帧间追踪器（App_Tracker_Init）*/
#include "app_trigger.h"        /* 声触发逻辑（App_Trigger_Init）*/
#include "app_sd.h"             /* SD 卡挂载（App_SD_Init，允许失败）*/
#include "app_capture.h"        /* 截图功能（App_Capture_Init）*/
#include "app_recorder.h"       /* WAV 录音功能（App_Recorder_Init）*/
#include "app_storage_task.h"   /* 异步存储任务（App_Storage_Init，创建 Storage_Task）*/
#include "app_user_config.h"    /* 编译期配置（APP_LVGL_ENABLE 等开关）*/

/* ============================================================================
 * FreeRTOS 句柄定义（全局，供其他模块和 ISR 访问）
 * 在 app_main_task.h 中以 extern 声明，这里是唯一的"拥有者"定义。
 * ============================================================================ */

/** @brief 音频处理流水线任务句柄
 *  @details 可用于：vTaskSuspend/Resume（调试）、uxTaskGetStackHighWaterMark（栈余量检查）
 *           [改进] 可通过此句柄实现 CLI 中的任务挂起/恢复命令 */
TaskHandle_t xAudioPipelineTaskHandle = NULL;  /* 初始化为 NULL，App_Task_Init 后指向实际任务 */

/** @brief UI 显示任务句柄
 *  @details 可通过 xTaskNotify 从音频任务向 UI 任务发送通知，替代部分队列开销 */
TaskHandle_t xUITaskHandle = NULL;             /* 初始化为 NULL，App_Task_Init 后指向实际任务 */

/** @brief 音频帧事件队列句柄（深度=1，ISR 写入，音频任务读取）
 *  @details xQueueOverwriteFromISR 写入，xQueueReceive 读取（portMAX_DELAY）*/
QueueHandle_t xAudioFrameQueue = NULL;         /* 初始化为 NULL，防止 App_Task_Init 前误用 */

/** @brief 声源位置队列句柄（深度=1，音频任务写入，UI 任务读取）
 *  @details xQueueOverwrite 写入（不阻塞），xQueueReceive 读取（33ms 超时）*/
QueueHandle_t xPositionQueue = NULL;           /* 初始化为 NULL */

/* ============================================================================
 * 运行时诊断计数器定义
 * 这些计数器供 CLI `status` 命令和调试器观察，所有任务均可访问（无锁，接受撕裂）。
 * ============================================================================ */

/** @brief 丢帧计数器：音频任务检测到 ISR 序号跳变时累加跳变量
 *  @details 非零表示发生了丢帧（音频任务处理速度落后 DMA 产生速度）
 *           正常工作时应维持 0，若持续增长说明 SRP 算法耗时过长 */
volatile uint32_t g_audio_both_flags_count = 0u;  /* 初始值 0（无丢帧）*/

/** @brief 音频任务收到非法 half_id 的次数（正常运行时应恒为 0）
 *  @details 非零表示 ISR 或队列存在 bug，建议立即排查 */
volatile uint32_t g_audio_no_flag_count = 0u;     /* 初始值 0 */

/** @brief UI 渲染帧计数：UI_Display_Task 每完成一次渲染递增一次
 *  @details 可作为 UI 活跃性指标，约 20 FPS → 每秒增加 20 */
volatile uint32_t g_ui_render_count = 0u;          /* 初始值 0 */

/** @brief UI 成功从位置队列接收数据的次数（正常路径计数）*/
volatile uint32_t g_ui_queue_rx_count = 0u;        /* 初始值 0 */

/** @brief UI 从位置队列接收超时的次数（表示音频任务无输出的帧数）*/
volatile uint32_t g_ui_queue_timeout_count = 0u;   /* 初始值 0 */

/** @brief UART CLI 成功接收字节的总次数（由 UART RX ISR 累加）
 *  @details 用于判断串口是否正常工作，单调递增，长时间为 0 说明串口或上位机有问题 */
volatile uint32_t g_ui_cli_rx_ok_count = 0u;       /* 初始值 0 */

/** @brief UART CLI 接收发生错误的总次数（溢出/帧错误/重启失败等均计入）
 *  @details 持续增长表示 UART 噪声干扰或波特率不匹配（应为 921600）*/
volatile uint32_t g_ui_cli_rx_err_count = 0u;      /* 初始值 0 */

/** @brief CLI 活跃标志：2 秒内收到过有效 UART 字节则置 1，否则为 0
 *  @details 由 app_ui_task.c 中的 CLI 超时逻辑维护，供 `status` 命令显示 */
volatile uint8_t  g_ui_cli_rx_alive = 0u;          /* 初始值 0（未激活）*/

/**
 * @brief   应用层任务与队列初始化入口（在 FreeRTOS 调度器启动前调用）
 * @details 此函数由 freertos.c 的 MX_FREERTOS_Init() 调用，在 vTaskStartScheduler() 之前执行。
 *          执行后所有 FreeRTOS 对象已创建，等调度器启动后各任务自动开始运行。
 *
 * 初始化步骤（有严格顺序依赖，请勿随意调整）：
 *   Step 1: 选择 UI 后端 → 决定 LVGL 还是 Legacy 渲染路径
 *   Step 2: App_Perf_Init() → 使能 DWT（性能计时器必须最先初始化）
 *   Step 3: App_RuntimeConfig_Init() → 同步显示/采集参数
 *   Step 3b: 各功能模块初始化→触发、激光、噪声基底、异常检测、追踪、声级计、分析器
 *   Step 4: SD 卡初始化（允许失败）→ 存储功能
 *   Step 5: 创建队列（depth=1 的覆盖队列）
 *   Step 6: 创建 FreeRTOS 任务
 *
 * @note    若卡在死循环 for(;;){__NOP();}，通常是 FreeRTOS heap 不够用。
 *          check: configTOTAL_HEAP_SIZE in FreeRTOSConfig.h
 */
void App_Task_Init(void)
{
    BaseType_t task_ok;  /* xTaskCreate 返回值：pdPASS=成功，pdFAIL=失败（通常因 heap 不足）*/

    /* ──────────────────────────────────────────────────────────────────────
     * Step 1: 选择 UI 渲染后端
     * 根据编译期配置决定：使用新的 LVGL 后端（触摸友好、控件丰富）
     * 还是旧版 Legacy 后端（直接帧缓冲操作，无 LVGL 层开销）。
     * ────────────────────────────────────────────────────────────────────── */
#if (APP_LVGL_ENABLE != 0u) && (APP_LVGL_BOOT_AS_DEFAULT != 0u)  /* 条件：LVGL 已启用 且 默认使用 LVGL 后端 */
    App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LVGL);         /* 设置 UI 渲染后端为 LVGL（lv_xxx API 激活）*/
#else                                                              /* 否则：使用传统帧缓冲渲染后端 */
    App_UiRenderer_SetBackend(APP_UI_RENDER_BACKEND_LEGACY);       /* 设置 UI 渲染后端为 Legacy（直接操作 LTDC 帧缓冲）*/
#endif                                                             /* 条件编译结束 */

    /* ──────────────────────────────────────────────────────────────────────
     * Step 2: 初始化性能分析器
     * 必须最早初始化，因为后续模块可能调用 App_Perf_BeginCycles()。
     * 内部使能 DWT（Data Watchpoint and Trace）计数器，用于精确 CPU 周期计时。
     * ────────────────────────────────────────────────────────────────────── */
    App_Perf_Init();  /* 清零统计缓冲，使能 DWT_CYCCNT（必须在 CoreDebug_DEMCR 中置 TRCENA 位）*/

    /* ──────────────────────────────────────────────────────────────────────
     * Step 3: 同步运行时配置
     * 从 app_display 等模块读取默认值，填充 App_RuntimeConfig 结构体，
     * 保证 CLI `status` 命令在任务启动后能读到正确的初始状态。
     * ────────────────────────────────────────────────────────────────────── */
    App_RuntimeConfig_Init();  /* 从显示/算法模块回读默认参数并缓存到运行时配置结构体 */

    /* ──────────────────────────────────────────────────────────────────────
     * Step 3b: 各功能模块初始化（顺序可调整，但需在任务创建之前完成）
     * 这些模块均只做内部状态初始化，不创建 FreeRTOS 对象。
     * ────────────────────────────────────────────────────────────────────── */
    App_Trigger_Init();         /* 声触发模块：初始化触发阈值、消抖计数器；初始状态=未触发 */
    App_Laser_Init();           /* 激光笔模块：初始化 GPIO 状态（激光默认关闭）*/
    App_NoiseFloor_Init();      /* 噪声基底估计：初始化历史帧缓冲，用于自适应背景减除 */
    App_Anomaly_Init();         /* 异常检测模块：初始化阈值参数和状态机（用于检测突发噪声）*/
    App_Tracker_Init();         /* 多声源追踪器：初始化 Top-K 追踪状态结构 */
    App_SLM_Init();             /* 声级计：初始化 dBSPL 计算历史缓冲（指数加权均值）*/
    App_Profile_Init();         /* CPU 占用分析：初始化各任务的 CPU 时间统计结构 */
    AI_BeamSteer_Init();        /* DAS 波束成形：初始化steering向量缓冲，默认指向正前方 */
    App_SD_Init();              /* SD 卡：尝试挂载 FatFS 文件系统（允许失败—SD 不是必须的）*/
    App_Storage_Init();         /* 异步存储任务：创建命令队列（depth=8）并创建 Storage_Task */
    App_Capture_Init();         /* 截图功能：初始化截图状态机（IDLE）*/
    App_Recorder_Init();        /* WAV 录音：初始化录音状态机（IDLE）、清零头部缓冲 */

    /* ──────────────────────────────────────────────────────────────────────
     * Step 5: 创建队列（深度=1 的覆盖队列）
     *
     * 为什么选择深度=1？
     *   - 实时系统：只需要最新的数据，旧数据无价值
     *   - xQueueOverwrite 语义：队列满时自动覆盖，ISR 永不阻塞
     *   - 避免数据积压：若任务处理变慢，自动丢弃旧帧
     *
     * [注意] xQueueCreate 分配 FreeRTOS heap，若失败则 heap 已耗尽。
     *        每个队列约占 sizeof(QueueDefinition_t) + element_size bytes。
     * ────────────────────────────────────────────────────────────────────── */
    /* 音频帧事件队列：ISR → Audio_Pipeline_Task，传递 DMA 半缓冲事件 */
    xAudioFrameQueue = xQueueCreate(1,                         /* 队列深度=1（覆盖模式语义）*/
                                    sizeof(Audio_FrameEvent_t)); /* 每个元素大小（8字节：half_id+reserved+seq）*/
    configASSERT(xAudioFrameQueue != NULL);  /* Debug 构建时断言：NULL 表示 heap 不足 */
    if (xAudioFrameQueue == NULL)            /* Release 构建时的显式检查（configASSERT 被优化掉）*/
    {
        /* 致命错误：堆内存不足，无法创建音频帧队列 */
        /* [注意] 卡在此处=heap 耗尽，增大 configTOTAL_HEAP_SIZE 或减少其他分配 */
        for (;;) { __NOP(); }  /* 永久死循环，配合 JTAG 调试器观察卡死位置 */
    }

    /* 声源位置队列：Audio_Pipeline_Task → UI_Display_Task，传递 Sound_Pos_t 定位结果 */
    xPositionQueue = xQueueCreate(1,                      /* 队列深度=1（UI 始终获取最新位置）*/
                                  sizeof(Sound_Pos_t));   /* 每个元素大小（12字节：x_angle+y_angle+energy）*/
    configASSERT(xPositionQueue != NULL);  /* Debug 断言 */
    if (xPositionQueue == NULL)            /* Release 显式检查 */
    {
        /* 致命错误：堆内存不足，无法创建位置队列 */
        for (;;) { __NOP(); }  /* 在此卡死=heap 耗尽，排查方法同上 */
    }

    /* ──────────────────────────────────────────────────────────────────────
     * Step 6a: 创建音频处理流水线任务
     * ────────────────────────────────────────────────────────────────────── */
    task_ok = xTaskCreate(
        Audio_Pipeline_Task,          /* 任务入口函数（定义在 app_audio_task.c）*/
        "Audio_Pipe",                 /* 任务名称字符串（调试时显示，最大 configMAX_TASK_NAME_LEN 个字符）*/
        APP_AUDIO_TASK_STACK_WORDS,   /* 堆栈深度（单位：word=4字节）= 2304 words = 9216 字节 */
        NULL,                         /* 任务参数（pvParameters），此任务不需要参数 */
        APP_AUDIO_TASK_PRIO,          /* 任务优先级（= 4 = osPriorityNormal）*/
        &xAudioPipelineTaskHandle);   /* 输出：任务句柄，供后续 suspend/notify 使用 */
    configASSERT(task_ok == pdPASS);  /* Debug 断言：pdFAIL 表示 heap 不足 */
    if (task_ok != pdPASS)            /* Release 检查 */
    {
        /* 致命错误：无法创建音频处理任务（通常是 heap 不足）*/
        for (;;) { __NOP(); }  /* 在此卡死 → 检查 heap 大小或减少其他任务栈 */
    }

    /* ──────────────────────────────────────────────────────────────────────
     * Step 6b: 创建 UI 显示任务
     * ────────────────────────────────────────────────────────────────────── */
    task_ok = xTaskCreate(
        UI_Display_Task,              /* 任务入口函数（定义在 app_ui_task.c）*/
        "UI_Disp",                    /* 任务名称（调试用，最大 16 字符）*/
        APP_UI_TASK_STACK_WORDS,      /* 堆栈深度（= 2048 words = 8192 字节，含 LVGL 渲染调用栈）*/
        NULL,                         /* 任务参数（未使用）*/
        APP_UI_TASK_PRIO,             /* 任务优先级（= 4，与音频任务同级，时间片轮转）*/
        &xUITaskHandle);              /* 输出：UI 任务句柄 */
    configASSERT(task_ok == pdPASS);  /* Debug 断言 */
    if (task_ok != pdPASS)            /* Release 检查 */
    {
        /* 致命错误：无法创建 UI 显示任务 */
        for (;;) { __NOP(); }  /* 在此卡死 → 检查 heap 大小 */
    }

    /* 初始化摄像头任务（OV2640，内部创建独立 FreeRTOS 任务）*/
    App_Camera_TaskInit();   /* 创建摄像头采集任务（若 APP_CAMERA_ENABLE=0 则此函数为空操作）*/

    /* 初始化完成，等待 vTaskStartScheduler() 启动调度器后各任务自动开始运行 */
    /* [注意] 此函数返回后，freertos.c 将立即调用 vTaskStartScheduler()，不得在此之后再修改任务状态 */
}
