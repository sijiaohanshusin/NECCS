/**
 * @file    sdram.h
 * @brief   W9825G6KH SDRAM 驱动头文件
 * @details 32MB SDRAM 控制器驱动 (适配自官方例程)
 *
 * 硬件配置：
 * - 芯片型号：W9825G6KH (Winbond)
 * - 容量：32MB (16M × 16-bit)
 * - 接口：FMC (Flexible Memory Controller)
 * - 时钟：120MHz (SDCLK)
 * - CAS 延迟：2 周期
 *
 * 内存映射：
 * - 起始地址：0xC0000000 (Bank 5)
 * - 结束地址：0xC1FFFFFF
 * - 地址范围：32MB
 *
 * 使用方法：
 * 1. 调用 sdram_init() 初始化
 * 2. 直接访问 0xC0000000 地址
 * 3. 或使用 fmc_sdram_write_buffer/read_buffer 函数
 */

#ifndef SDRAM_H
#define SDRAM_H

#include "main.h"

/* ============================================================================
 * 全局变量 (Global Variables)
 * ============================================================================ */

/** @brief SDRAM 句柄 (HAL 库使用) */
extern SDRAM_HandleTypeDef g_sdram_handle;

/* ============================================================================
 * 地址定义 (Address Definitions)
 * ============================================================================ */

/** @brief SDRAM 起始地址 (Bank 5, 0xC0000000) */
#define BANK5_SDRAM_ADDR    ((uint32_t)(0xC0000000))

/* ============================================================================
 * SDRAM 模式寄存器配置位 (Mode Register Configuration)
 * ============================================================================ */

/** @brief 突发长度：1 */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)

/** @brief 突发长度：2 */
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)

/** @brief 突发长度：4 */
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)

/** @brief 突发长度：8 */
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)

/** @brief 突发类型：顺序 */
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)

/** @brief 突发类型：交织 */
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)

/** @brief CAS 延迟：2 周期 */
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)

/** @brief CAS 延迟：3 周期 */
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)

/** @brief 操作模式：标准 */
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)

/** @brief 写突发模式：编程 */
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)

/** @brief 写突发模式：单次 */
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

/* ============================================================================
 * 函数声明 (Function Declarations)
 * ============================================================================ */

/**
 * @brief   SDRAM 初始化
 * @details 初始化 FMC 和 SDRAM 控制器
 *
 * 初始化流程：
 * 1. 配置 FMC 时序参数
 * 2. 发送 SDRAM 初始化序列
 * 3. 配置刷新率
 *
 * 时序参数：
 * - SDCLK: 120MHz (FMC_CLK / 2)
 * - CAS 延迟：2 周期
 * - 刷新周期：64ms / 4096 行 = 15.625μs
 *
 * @note    在 main() 中调用，HAL_Init() 之后
 * @note    初始化后可直接访问 0xC0000000 地址
 */
void sdram_init(void);

/**
 * @brief   发送 SDRAM 命令
 * @details 向 SDRAM 发送控制命令
 *
 * 支持的命令：
 * - FMC_SDRAM_CMD_NORMAL_MODE: 正常模式
 * - FMC_SDRAM_CMD_CLK_ENABLE: 时钟使能
 * - FMC_SDRAM_CMD_PALL: 预充电所有 Bank
 * - FMC_SDRAM_CMD_AUTOREFRESH_MODE: 自动刷新
 * - FMC_SDRAM_CMD_LOAD_MODE: 加载模式寄存器
 * - FMC_SDRAM_CMD_SELFREFRESH_MODE: 自刷新模式
 * - FMC_SDRAM_CMD_POWERDOWN_MODE: 掉电模式
 *
 * @param   bankx    Bank 选择 (1 或 2)
 * @param   cmd      命令类型
 * @param   refresh  自动刷新次数
 * @param   regval   模式寄存器值
 * @return  0: 成功, 1: 失败
 *
 * @note    内部函数，通常由 sdram_initialization_sequence() 调用
 */
uint8_t sdram_send_cmd(uint8_t bankx, uint8_t cmd, uint8_t refresh, uint16_t regval);

/**
 * @brief   SDRAM 初始化序列
 * @details 执行 SDRAM 上电初始化序列
 *
 * 初始化序列 (按 SDRAM 规范)：
 * 1. 时钟使能 (CLK_ENABLE)
 * 2. 延迟 100μs (等待时钟稳定)
 * 3. 预充电所有 Bank (PALL)
 * 4. 自动刷新 2 次 (AUTOREFRESH)
 * 5. 加载模式寄存器 (LOAD_MODE)
 * 6. 配置刷新率
 *
 * 模式寄存器配置：
 * - 突发长度：1
 * - 突发类型：顺序
 * - CAS 延迟：2
 * - 写突发模式：单次
 *
 * @note    内部函数，由 sdram_init() 调用
 */
void sdram_initialization_sequence(void);

/**
 * @brief   SDRAM 写缓冲区
 * @details 向 SDRAM 写入数据
 *
 * 写入方式：
 * - 直接内存访问 (不使用 DMA)
 * - 按字节写入
 *
 * @param   pbuf       源数据缓冲区指针
 * @param   writeaddr  写入地址 (相对于 SDRAM 起始地址的偏移)
 * @param   n          写入字节数
 *
 * @note    writeaddr 是偏移地址，实际地址 = BANK5_SDRAM_ADDR + writeaddr
 * @note    可用于测试或小数据量传输
 */
void fmc_sdram_write_buffer(uint8_t *pbuf, uint32_t writeaddr, uint32_t n);

/**
 * @brief   SDRAM 读缓冲区
 * @details 从 SDRAM 读取数据
 *
 * 读取方式：
 * - 直接内存访问 (不使用 DMA)
 * - 按字节读取
 *
 * @param   pbuf      目标数据缓冲区指针
 * @param   readaddr  读取地址 (相对于 SDRAM 起始地址的偏移)
 * @param   n         读取字节数
 *
 * @note    readaddr 是偏移地址，实际地址 = BANK5_SDRAM_ADDR + readaddr
 * @note    可用于测试或小数据量传输
 */
void fmc_sdram_read_buffer(uint8_t *pbuf, uint32_t readaddr, uint32_t n);

#endif /* SDRAM_H */
