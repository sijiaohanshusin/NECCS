/**
 * @file   app_boot_diag.h
 * @brief  启动诊断模块接口
 * @details 跟踪系统启动过程中各阶段的执行状态，提供阶段名称查询及
 *          栈高水位等诊断信息，用于启动失败定位和 UI 状态显示。
 */
#ifndef APP_BOOT_DIAG_H
#define APP_BOOT_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动诊断阶段枚举
 * @details 定义从空闲到完成的各启动步骤，用于追踪启动进度和故障点。
 */
typedef enum
{
    APP_BOOT_DIAG_STAGE_IDLE         = 0u,  /**< 空闲，尚未开始启动 */
    APP_BOOT_DIAG_STAGE_SOFT_I2C    = 1u,  /**< 软件 I2C 总线初始化 */
    APP_BOOT_DIAG_STAGE_APP_STREAM  = 2u,  /**< 音频流初始化 */
    APP_BOOT_DIAG_STAGE_APP_TASK    = 3u,  /**< 应用任务创建 */
    APP_BOOT_DIAG_STAGE_SAI_DMA     = 4u,  /**< SAI + DMA 音频采集初始化 */
    APP_BOOT_DIAG_STAGE_CLOCK_WAIT  = 5u,  /**< 等待外部时钟锁定 */
    APP_BOOT_DIAG_STAGE_PCMD0       = 6u,  /**< PDM 麦克风 0 配置 */
    APP_BOOT_DIAG_STAGE_PCMD1       = 7u,  /**< PDM 麦克风 1 配置 */
    APP_BOOT_DIAG_STAGE_CAMERA_INIT = 8u,  /**< 摄像头初始化 */
    APP_BOOT_DIAG_STAGE_CAMERA_START = 9u, /**< 摄像头启动采集 */
    APP_BOOT_DIAG_STAGE_DONE        = 10u  /**< 启动流程全部完成 */
} App_BootDiag_Stage_t;

/**
 * @brief 启动诊断状态结构体
 * @details 保存当前启动阶段、任务栈高水位及是否完成标志。
 */
typedef struct
{
    uint32_t stage;                  /**< 当前启动阶段，参见 App_BootDiag_Stage_t */
    uint32_t stack_high_water_words; /**< 任务栈高水位（单位：字），用于检测栈溢出风险 */
    uint8_t completed;              /**< 启动是否完成：1=已完成，0=进行中 */
} App_BootDiag_Status_t;

/**
 * @brief 获取当前启动诊断状态
 * @param status 输出状态结构体指针
 */
void App_BootDiag_GetStatus(App_BootDiag_Status_t *status);

/**
 * @brief  将启动阶段编号转换为可读名称字符串
 * @param  stage 阶段编号，参见 App_BootDiag_Stage_t
 * @return 阶段名称的常量字符串指针
 */
const char *App_BootDiag_StageName(uint32_t stage);

#ifdef __cplusplus
}
#endif

#endif /* APP_BOOT_DIAG_H */
