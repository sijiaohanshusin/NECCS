/**
 * @file    camera_ov2640.h
 * @brief   OV2640图像传感器驱动头文件
 * @details 提供OV2640摄像头模块的初始化、ID读取、RGB565预览配置及诊断信息获取接口。
 *          通过软件模拟SCCB/I2C总线与OV2640进行寄存器级通信，适用于STM32H743平台。
 */

#ifndef CAMERA_OV2640_H
#define CAMERA_OV2640_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OV2640诊断信息结构体
 * @details 用于保存摄像头最近一次操作的状态和标识信息，便于故障排查。
 */
typedef struct
{
    uint16_t mid;               /**< 厂商ID (Manufacturer ID) */
    uint16_t pid;               /**< 产品ID (Product ID) */
    uint8_t diag_stage;         /**< 当前诊断阶段编号 */
    uint8_t last_write_status;  /**< 最近一次SCCB写操作状态 (0=成功, 1=失败) */
    uint8_t last_read_status;   /**< 最近一次SCCB读操作状态 (0=成功, 1=失败) */
} Camera_OV2640_Diag_t;

/**
 * @brief   初始化OV2640摄像头模块
 * @details 完成GPIO复位引脚初始化、SCCB总线初始化、硬件/软件复位、ID校验及SVGA寄存器表写入。
 * @return  0: 初始化成功; 1: 初始化失败
 */
uint8_t Camera_OV2640_Init(void);

/**
 * @brief   读取OV2640的厂商ID和产品ID
 * @param   mid  [out] 指向用于存储厂商ID的变量
 * @param   pid  [out] 指向用于存储产品ID的变量
 * @return  0: 读取成功; 1: 读取失败或参数为NULL
 */
uint8_t Camera_OV2640_ReadId(uint16_t *mid, uint16_t *pid);

/**
 * @brief   配置OV2640输出RGB565格式预览图像
 * @details 设置RGB565输出模式，并以SVGA(800x600)为源图像尺寸缩放至指定分辨率。
 * @param   width   [in] 输出图像宽度（像素，需为4的倍数）
 * @param   height  [in] 输出图像高度（像素，需为4的倍数）
 * @return  0: 配置成功; 1: 配置失败
 */
uint8_t Camera_OV2640_ConfigRgb565Preview(uint16_t width, uint16_t height);

/**
 * @brief   获取OV2640诊断信息
 * @param   diag  [out] 指向诊断信息结构体的指针，不可为NULL
 */
void Camera_OV2640_GetDiag(Camera_OV2640_Diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_OV2640_H */
