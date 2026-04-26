/**
 * @file    app_types.h
 * @brief   应用层全局共享数据类型定义
 * @details
 * 本文件汇集了跨模块共享的核心数据结构与类型定义，包括：
 *   - DMA 双缓冲标识（PING/PONG）
 *   - ISR→任务的音频帧事件消息
 *   - 声源定位结果（单源和多源）
 *   - 频谱帧数据结构
 *
 * 设计原则：
 *   1. 只定义数据类型，不包含实现逻辑
 *   2. 类型命名采用 `模块_功能_t` 或 `App_功能_t` 格式
 *   3. 所有多字节字段均为小端序（STM32 原生）
 *
 * [注意] 本文件被多个任务（音频任务、UI 任务、CLI 任务）包含，
 *        修改此处的结构体会影响所有消费方，需全局搜索后再改动。
 *
 * 依赖关系：
 *   app_types.h → app_user_config.h（获取 FRAME_LEN 等宏）
 *                → stdint.h（标准整型）
 */
#ifndef APP_TYPES_H              /* 头文件防重复包含保护（开始） */
#define APP_TYPES_H              /* 定义本文件标识宏 */

#include "app_user_config.h"     /* 拉入 FRAME_LEN、MIC_CHANNELS 等全局配置常量 */

#include <stdint.h>              /* uint8_t、uint16_t、uint32_t 等标准整型定义 */

#ifdef __cplusplus               /* 若被 C++ 编译器包含，则启用 C 链接格式 */
extern "C" {                     /* 防止 C++ 对函数名进行名称修饰（name-mangling） */
#endif

/* ============================================================================
 * 数据结构定义 (Data Structures)
 * ============================================================================ */

/**
 * @brief   DMA 半缓冲槽位标识
 * @details 指示当前 DMA 完成事件对应的是前半缓冲（PING）还是后半缓冲（PONG）。
 *
 * PING/PONG 双缓冲机制详解：
 * ┌───────────────────────────────────────────────────────┐
 * │  物理缓冲区 Mic_Rx_Buffer[MIC_CHANNELS × FRAME_LEN × 2] │
 * ├──────────────────────────┬──────────────────────────┤
 * │     PING 区 (前半)        │     PONG 区 (后半)        │
 * │  [0 … N-1]              │  [N … 2N-1]              │
 * └──────────────────────────┴──────────────────────────┘
 *
 * 工作流：
 *   - DMA 传完前半 → 触发"半完成中断" → ISR 发送 PING 事件 → CPU 处理 PING 区
 *   - DMA 传完后半 → 触发"全完成中断" → ISR 发送 PONG 事件 → CPU 处理 PONG 区
 *   - DMA 同时在处理另一半，实现真正的乒乓、零拷贝流水线
 *
 * [注意] DMA 是循环模式（DMA_CIRCULAR），会永久在 PING/PONG 间轮换，
 *        不需要也不能软件重启 DMA。
 *
 * [改进] 若未来支持 4 缓冲区可进一步降低延迟，但会增加 RAM 占用。
 */
typedef enum {
    AUDIO_DMA_HALF_PING = 0u,   /**< PING 槽：DMA 缓冲前半区，偏移 0 */
    AUDIO_DMA_HALF_PONG = 1u    /**< PONG 槽：DMA 缓冲后半区，偏移 N */
} Audio_DmaHalf_t;

/**
 * @brief   ISR → 音频任务 通信消息（音频帧事件）
 * @details SAI DMA 中断产生此消息，经由 xAudioFrameQueue 传递给音频处理任务。
 *
 * 队列配置：
 *   - 队列深度：1（仅保留最新一帧事件）
 *   - ISR 写入：xQueueOverwriteFromISR()（覆盖旧数据，永不阻塞）
 *   - 任务读取：xQueueReceive()（portMAX_DELAY 阻塞等待）
 *
 * 为什么队列深度为 1？
 *   - 声学相机是实时系统，只需要处理最新的麦克风数据
 *   - 深度>1 会导致延迟积累（每多排一帧，延迟增加 5.3 ms @256点/48kHz）
 *   - 覆盖策略：若音频任务处理速度赶不上采集速度，自动丢弃旧帧
 *
 * [注意] seq 字段用于丢帧检测，若 task 端接收的 seq 不连续，
 *        说明期间有帧被覆盖（即丢帧），可通过 g_audio_both_flags_count 统计。
 *
 * [改进] reserved 字段目前只用于内存对齐，未来可扩展为时间戳或诊断标志。
 */
typedef struct {
    uint8_t  half_id;       /**< DMA 半缓冲标识，取值见 @ref Audio_DmaHalf_t */
    uint8_t  reserved[3];   /**< 保留/对齐字段，保证结构体 4 字节对齐，禁止直接读写 */
    uint32_t seq;           /**< ISR 侧单调递增帧序号（从 1 开始），用于丢帧检测 */
} Audio_FrameEvent_t;

/**
 * @brief   单个声源的方位定位结果
 * @details SRP-PHAT 算法输出，经队列传递给 UI 显示任务。
 *
 * 坐标系约定：
 *   - 原点：麦克风阵列中心，朝向阵列正前方（+Z）
 *   - x_angle：水平方位角，正值向右（向+X方向），负值向左（向-X方向）
 *   - y_angle：垂直仰角，正值向上（向+Y方向），负值向下（向-Y方向）
 *          ↑ y_angle(+)
 *          │
 *          ├──────→ x_angle(+)
 *     仰视  │   俯视
 *          ↓ y_angle(-)
 *
 * 能量归一化规则：
 *   - energy 范围：[0.0, 1.0]（理论值，实际输出受 SRP_VALID_MIN_ENERGY 门限约束）
 *   - 0.0：无声源或环境噪声均匀分布（无法定位）
 *   - ~0.5：中等强度定向声源
 *   - ~1.0：强指向性声源（不常见，通常 <0.8）
 *
 * [注意] 当 energy < SRP_VALID_MIN_ENERGY 时，定位结果被认为不可信，
 *        UI 层应以柔和方式显示或保持上一次有效位置。
 *
 * [改进] 当前未携带时间戳，若要做轨迹分析需在此添加 uint32_t timestamp_ms。
 */
typedef struct {
    float x_angle;   /**< 水平方位角（度），范围 [-60.0, 60.0]，正右负左 */
    float y_angle;   /**< 垂直仰/俯角（度），范围 [-60.0, 60.0]，正上负下 */
    float energy;    /**< 归一化声源能量 [0.0, 1.0]，越大越可信 */
} Sound_Pos_t;

/** @brief 同时追踪的最大声源数量
 *  @details 当前支持同时输出 3 个声源位置（按能量降序排列）。
 *           增大此值会增加 SRP 细搜索次数和 RAM 占用；减小会降低多源支持能力。
 *           [改进] 3 是经验值，若麦克风阵列孔径增大，可考虑增至 5。
 */
#define MULTI_SOURCE_MAX    3u

/**
 * @brief   多声源定位结果容器
 * @details 存放 Top-K（最多 MULTI_SOURCE_MAX）个声源位置，按能量降序排列。
 *          由音频任务写入 g_multi_source，由 UI/显示任务读取（无锁，接受偶发撕裂）。
 *
 * [注意] g_multi_source 无互斥保护，UI 任务读取时可能读到部分更新的状态。
 *        对于可视化应用，这种"撕裂"通常不影响用户体验，但若需精确值，
 *        应改用临界区或队列传递完整结构体。
 *
 * [改进] 若对时一致性有要求，可用 __attribute__((aligned(32))) 配合 LDREX/STREX 实现无锁原子更新。
 */
typedef struct {
    Sound_Pos_t sources[MULTI_SOURCE_MAX]; /**< 声源位置数组（按能量降序排列，无效槽位 energy=0） */
    uint8_t     count;                     /**< 当前有效声源数量 [0, MULTI_SOURCE_MAX] */
} Sound_MultiPos_t;

/** @brief 全局多声源定位结果（运行时共享变量）
 *  @details 由 app_audio_task.c 内 Audio_Pipeline_Task 写入，
 *           由 app_display.c、app_ui_task.c 等读取。
 *           [注意] 跨任务无锁访问，仅适用于可接受偶发撕裂的可视化场景。
 */
extern Sound_MultiPos_t g_multi_source;

/** @brief 单帧频谱的频率 bin 数量
 *  @details 等于 FFT 帧长的一半（实 FFT 输出有效 bin 数 = FRAME_LEN/2）。
 *           例如：FRAME_LEN=256 → APP_SPECTRUM_BIN_COUNT=128。
 *           [注意] 必须是 2 的幂次方，否则 CMSIS-DSP arm_cfft_f32 不支持。
 */
#define APP_SPECTRUM_BIN_COUNT    (FRAME_LEN / 2u)

/**
 * @brief   频率带宽描述：起始和结束 bin 索引
 * @details bin 索引与频率的关系：频率(Hz) = bin_index × (采样率 / FRAME_LEN)
 *          例如：bin=3, 采样率=48000, FRAME_LEN=256 → 频率=562.5 Hz
 *
 * [注意] start_bin 必须 < end_bin，且均须在 [1, APP_SPECTRUM_BIN_COUNT-1] 范围内，
 *        bin=0 是直流分量（DC offset），通常不代表音频信号。
 */
typedef struct {
    uint16_t start_bin;   /**< 起始 bin 索引（含），对应最低频率 */
    uint16_t end_bin;     /**< 结束 bin 索引（含），对应最高频率 */
} App_FreqBand_t;

/**
 * @brief   单帧完整频谱数据
 * @details 由 app_spectrum.c 从 FFT 结果填充，通过指针或队列传递给 UI 频谱面板。
 *
 * [注意] magnitude 数组大小为 APP_SPECTRUM_BIN_COUNT（=FRAME_LEN/2），
 *        对于 FRAME_LEN=256 这是 128 个 float，共 512 字节。
 *        若放在栈上会导致栈溢出风险，应声明为 static 或放在堆/静态区。
 *
 * [改进] 当前没有 phase 字段，未来若要做相位谱可视化需要扩展。
 *        delta_f_hz 是派生值（= 采样率/帧长），可以考虑只存 bin_count，运行时计算。
 */
typedef struct {
    uint32_t seq;                                 /**< 帧序号（与音频帧序号一一对应，用于同步/跳帧检测） */
    uint16_t bin_count;                           /**< 有效 bin 数量（= FRAME_LEN/2） */
    float    delta_f_hz;                          /**< 频率分辨率（Hz/bin）= 采样率 / FRAME_LEN */
    App_FreqBand_t active_band;                   /**< 当前 SRP 算法使用的频率带宽（算法关注范围） */
    App_FreqBand_t preview_band;                  /**< UI 频谱面板显示的频率带宽（可与 active_band 不同） */
    float magnitude[APP_SPECTRUM_BIN_COUNT];      /**< 各 bin 的幅度值（线性幅度，非 dB）[改进: 可以预转换为 dBFS 减少 UI 计算] */
} App_SpectrumFrame_t;

#ifdef __cplusplus               /* 结束 C 链接声明区域 */
}
#endif

#endif /* APP_TYPES_H */         /* 头文件防重复包含保护（结束） */
