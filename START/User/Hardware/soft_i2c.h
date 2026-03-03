/**
 * @file    soft_i2c.h
 * @brief   软件 I2C 驱动头文件
 * @details GPIO 模拟 I2C 通信协议
 *
 * 硬件配置：
 * - SCL: PE2 (时钟线)
 * - SDA: PE3 (数据线)
 * - 速度：约 100kHz (标准模式)
 *
 * 为什么使用软件 I2C？
 * - 硬件 I2C 可能被其他外设占用
 * - GPIO 模拟更灵活，可任意选择引脚
 * - 便于调试和故障排查
 *
 * 使用方法：
 * 1. 调用 Soft_I2C_Init() 初始化
 * 2. 使用 PCMD_WriteReg/PCMD_ReadReg 读写寄存器
 * 3. 使用 I2C_Scan() 扫描总线设备
 *
 * 注意事项：
 * - 必须在 FreeRTOS 启动后调用 Soft_I2C_Init()
 * - 内部使用互斥量保护，支持多任务访问
 * - 不支持时钟拉伸 (Clock Stretching)
 */

#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "main.h"

/* ============================================================================
 * 硬件引脚定义 (Hardware Pin Definitions)
 * ============================================================================ */

/** @brief I2C GPIO 端口 */
#define I2C_PORT        GPIOE

/** @brief I2C 时钟线引脚 (SCL) */
#define I2C_SCL_PIN     GPIO_PIN_2

/** @brief I2C 数据线引脚 (SDA) */
#define I2C_SDA_PIN     GPIO_PIN_3

/* ============================================================================
 * 函数声明 (Function Declarations)
 * ============================================================================ */

/**
 * @brief   软件 I2C 初始化
 * @details 初始化 GPIO 和互斥量
 *
 * 初始化内容：
 * 1. 配置 SCL/SDA 为开漏输出
 * 2. 设置上拉电阻
 * 3. 创建 FreeRTOS 互斥量 (保护总线访问)
 * 4. 释放总线 (SCL=1, SDA=1)
 *
 * GPIO 配置：
 * - 模式：开漏输出 (Open-Drain)
 * - 速度：高速 (High Speed)
 * - 上拉：使能 (Pull-Up)
 *
 * 为什么使用开漏输出？
 * - I2C 总线需要线与逻辑
 * - 多个设备可共享总线
 * - 支持总线仲裁
 *
 * @note    必须在 FreeRTOS 启动后调用 (需要创建互斥量)
 * @note    在 PCMD3180InitTask 中调用
 */
void Soft_I2C_Init(void);

/**
 * @brief   PCMD3180 写寄存器
 * @details 向 PCMD3180 芯片写入单个寄存器
 *
 * I2C 时序：
 * 1. START 条件
 * 2. 发送设备地址 + 写位 (devAddr << 1 | 0)
 * 3. 等待 ACK
 * 4. 发送寄存器地址 (regAddr)
 * 5. 等待 ACK
 * 6. 发送数据 (data)
 * 7. 等待 ACK
 * 8. STOP 条件
 *
 * 互斥保护：
 * - 使用 FreeRTOS 互斥量保护
 * - 支持多任务并发访问
 * - 超时时间：100ms
 *
 * @param   devAddr   设备地址 (7-bit, 例如 0x4C)
 * @param   regAddr   寄存器地址 (8-bit)
 * @param   data      写入数据 (8-bit)
 * @return  0: 成功, 1: 失败 (无 ACK 或超时)
 *
 * @note    devAddr 是 7-bit 地址，函数内部会自动左移 1 位
 * @note    写入失败时会自动重试 STOP 条件
 */
uint8_t PCMD_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data);

/**
 * @brief   PCMD3180 读寄存器
 * @details 从 PCMD3180 芯片读取单个寄存器
 *
 * I2C 时序：
 * 1. START 条件
 * 2. 发送设备地址 + 写位 (devAddr << 1 | 0)
 * 3. 等待 ACK
 * 4. 发送寄存器地址 (regAddr)
 * 5. 等待 ACK
 * 6. RESTART 条件
 * 7. 发送设备地址 + 读位 (devAddr << 1 | 1)
 * 8. 等待 ACK
 * 9. 读取数据
 * 10. 发送 NACK (最后一个字节)
 * 11. STOP 条件
 *
 * 互斥保护：
 * - 使用 FreeRTOS 互斥量保护
 * - 支持多任务并发访问
 * - 超时时间：100ms
 *
 * @param   devAddr   设备地址 (7-bit, 例如 0x4C)
 * @param   regAddr   寄存器地址 (8-bit)
 * @param   pData     读取数据缓冲区指针 (8-bit)
 * @return  0: 成功, 1: 失败 (无 ACK 或超时)
 *
 * @note    devAddr 是 7-bit 地址，函数内部会自动左移 1 位
 * @note    读取失败时会自动重试 STOP 条件
 */
uint8_t PCMD_ReadReg(uint8_t devAddr, uint8_t regAddr, uint8_t *pData);

/**
 * @brief   I2C 总线扫描
 * @details 扫描 I2C 总线上的所有设备
 *
 * 扫描范围：
 * - 地址范围：0x08 - 0x77 (7-bit)
 * - 跳过保留地址：0x00-0x07, 0x78-0x7F
 *
 * 扫描方法：
 * - 发送 START + 设备地址 + 写位
 * - 等待 ACK
 * - 发送 STOP
 * - 有 ACK 则设备存在
 *
 * 输出格式：
 * - 串口输出扫描结果
 * - 格式：地址 (十六进制)
 * - 例如：Found device at 0x4C
 *
 * @note    用于调试和设备检测
 * @note    扫描时间约 1 秒 (取决于总线速度)
 */
void I2C_Scan(void);

#endif /* __SOFT_I2C_H */
