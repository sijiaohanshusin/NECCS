/**
 * @file   app_camera.h
 * @brief  摄像头应用层接口
 * @details 封装 OV2640 摄像头的初始化、启动/停止、帧采集与发布逻辑，
 *          提供双缓冲帧获取及冻结控制功能，同时暴露 DCMI/DMA 中断处理入口。
 */
#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 摄像头帧描述结构体
 * @details 描述一帧已发布的摄像头图像数据，包含像素指针、尺寸及有效性标志。
 */
typedef struct
{
    const uint16_t *pixels; /**< RGB565 像素数据指针 */
    uint16_t width;         /**< 图像宽度（像素） */
    uint16_t height;        /**< 图像高度（像素） */
    uint16_t stride;        /**< 行跨度（像素），含对齐填充 */
    uint32_t seq;           /**< 帧序列号 */
    uint8_t buffer_id;      /**< 双缓冲区索引（0 或 1） */
    uint8_t valid;          /**< 帧数据是否有效：1=有效，0=无效 */
} App_CameraFrame_t;

/**
 * @brief 摄像头初始化阶段枚举
 * @details 表示摄像头从空闲到就绪的各个初始化步骤。
 */
typedef enum
{
    APP_CAMERA_INIT_STAGE_IDLE        = 0u, /**< 空闲，尚未开始初始化 */
    APP_CAMERA_INIT_STAGE_SENSOR_INIT = 1u, /**< 传感器（OV2640）初始化中 */
    APP_CAMERA_INIT_STAGE_PREVIEW_CFG = 2u, /**< 预览分辨率/格式配置中 */
    APP_CAMERA_INIT_STAGE_DCMI_INIT   = 3u, /**< DCMI 外设初始化中 */
    APP_CAMERA_INIT_STAGE_READY       = 4u  /**< 初始化完成，可以启动采集 */
} App_CameraInitStage_t;

/**
 * @brief 摄像头运行状态结构体
 * @details 包含帧计数、错误码、传感器 ID、DMA/DCMI 状态等诊断信息，
 *          用于 UI 状态栏和调试输出。
 */
typedef struct
{
    uint32_t frame_seq;            /**< 当前帧序列号 */
    uint32_t published_seq;        /**< 已发布帧序列号 */
    uint32_t error_code;           /**< 最近错误码 */
    uint32_t dma_error_code;       /**< DMA 错误码 */
    uint32_t restart_count;        /**< 成功重启次数 */
    uint32_t restart_fail_count;   /**< 重启失败次数 */
    uint32_t init_attempt_count;   /**< 初始化尝试次数 */
    uint32_t publish_count;        /**< 帧发布成功计数 */
    uint32_t publish_drop_count;   /**< 帧发布丢弃计数 */
    uint32_t dma_done_count;       /**< DMA 传输完成计数 */
    uint32_t frame_event_count;    /**< 帧事件触发计数 */
    uint32_t arm_count;            /**< DMA 臂（arm）操作计数 */
    uint32_t arm_fail_count;       /**< DMA 臂操作失败计数 */
    uint32_t raw_hash;             /**< 原始缓冲区哈希值（调试用） */
    uint32_t pub_hash;             /**< 发布缓冲区哈希值（调试用） */
    uint32_t dcmi_state;           /**< DCMI 外设 HAL 状态 */
    uint32_t dma_state;            /**< DMA 通道 HAL 状态 */
    uint16_t sensor_mid;           /**< 传感器厂商 ID（MID） */
    uint16_t sensor_pid;           /**< 传感器产品 ID（PID） */
    uint16_t raw_sample0;          /**< 原始缓冲区采样像素 0 */
    uint16_t raw_sample1;          /**< 原始缓冲区采样像素 1 */
    uint16_t raw_sample2;          /**< 原始缓冲区采样像素 2 */
    uint16_t pub_sample0;          /**< 发布缓冲区采样像素 0 */
    uint16_t pub_sample1;          /**< 发布缓冲区采样像素 1 */
    uint16_t pub_sample2;          /**< 发布缓冲区采样像素 2 */
    uint8_t initialized;           /**< 是否已初始化：1=是 */
    uint8_t streaming;             /**< 是否正在采集：1=是 */
    uint8_t valid;                 /**< 状态数据是否有效：1=有效 */
    uint8_t latest_index;          /**< 最新帧缓冲区索引 */
    uint8_t published_index;       /**< 已发布帧缓冲区索引 */
    uint8_t init_stage;            /**< 当前初始化阶段，参见 App_CameraInitStage_t */
    uint8_t pending_restart;       /**< 是否有挂起的重启请求：1=是 */
    uint8_t freeze_enabled;        /**< 冻结模式是否使能：1=冻结 */
    uint8_t sensor_diag_stage;     /**< 传感器诊断阶段 */
    uint8_t sensor_last_write_status; /**< 传感器最后一次 I2C 写状态 */
    uint8_t sensor_last_read_status;  /**< 传感器最后一次 I2C 读状态 */
} App_CameraStatus_t;

/** @brief 初始化摄像头硬件及驱动（OV2640 + DCMI + DMA） */
void App_Camera_Init(void);

/** @brief 启动摄像头连续采集 */
void App_Camera_Start(void);

/**
 * @brief  重试摄像头初始化
 * @return 0=成功，非零=失败
 */
uint8_t App_Camera_Retry(void);

/** @brief 停止摄像头采集并释放 DMA 资源 */
void App_Camera_Stop(void);

/** @brief 在 RTOS 任务启动前完成摄像头任务级初始化 */
void App_Camera_TaskInit(void);

/**
 * @brief  将最新完成的帧发布到消费端
 * @return 0=成功发布，非零=无新帧可发布
 */
uint8_t App_Camera_UpdatePublishedFrame(void);

/**
 * @brief 设置帧冻结模式
 * @param enable 1=冻结（暂停帧更新），0=恢复
 */
void App_Camera_SetFreeze(uint8_t enable);

/**
 * @brief  获取帧冻结状态
 * @return 1=冻结中，0=正常
 */
uint8_t App_Camera_GetFreeze(void);

/**
 * @brief  获取最新帧并锁定缓冲区（需配合 ReleaseFrame 使用）
 * @param  frame 输出帧描述结构体指针
 * @return 1=获取成功，0=无有效帧
 */
uint8_t App_Camera_AcquireLatestFrame(App_CameraFrame_t *frame);

/**
 * @brief 释放通过 AcquireLatestFrame 锁定的帧缓冲区
 * @param frame 帧描述结构体指针
 */
void App_Camera_ReleaseFrame(const App_CameraFrame_t *frame);

/**
 * @brief 获取最新帧的副本（非锁定方式）
 * @param frame 输出帧描述结构体指针
 */
void App_Camera_GetLatestFrame(App_CameraFrame_t *frame);

/**
 * @brief 获取摄像头当前运行状态
 * @param status 输出状态结构体指针
 */
void App_Camera_GetStatus(App_CameraStatus_t *status);

/**
 * @brief  将初始化阶段编号转换为可读名称字符串
 * @param  stage 初始化阶段编号，参见 App_CameraInitStage_t
 * @return 阶段名称的常量字符串指针
 */
const char *App_Camera_InitStageName(uint8_t stage);

/** @brief DCMI 中断处理入口，需在 stm32h7xx_it.c 中调用 */
void App_Camera_DCMI_IRQHandler(void);

/** @brief DMA 中断处理入口，需在 stm32h7xx_it.c 中调用 */
void App_Camera_DMA_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */
