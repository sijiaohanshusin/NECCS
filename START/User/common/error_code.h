/**
 * @file    error_code.h
 * @brief   工程统一错误码定义 — 所有模块函数返回值的集中规范
 * @details
 * 历史背景（为什么需要此文件）：
 *   在引入此文件之前，各模块采用不兼容的返回值约定：
 *   - soft_i2c.c      返回 0=成功,  1=失败（POSIX 风格）
 *   - camera_ov2640.c 返回 1=成功,  0=失败（与直觉相反！容易产生 bug）
 *   - pcmd3180.c      返回 PCMD3180_OK(0), PCMD3180_ERR(-1)
 *   - HAL 层          返回 HAL_OK(0), HAL_ERROR(1)...
 *
 *   这种不一致导致调用方必须记住每个模块的特殊约定，容易出错。
 *   引入本文件后，所有新代码统一使用 Err_t，0=成功，负值=错误类型。
 *
 * 统一约定：
 *   - **0（ERR_OK）永远且只表示操作成功**
 *   - **负值**表示具体的错误类型（枚举值）
 *   - 正值保留（不使用，避免与旧代码混淆）
 *
 * 向下兼容策略：
 *   - 旧模块（soft_i2c、camera 等）暂不修改返回值约定
 *   - 新代码和重构代码统一使用 Err_t
 *   - 新代码在调用旧模块时，在交接处转换返回值
 *
 * 使用示例：
 * @code
 *   Err_t ret = SomeModule_Init();          // 调用遵循新约定的模块
 *   if (ret != ERR_OK) {                    // 检查是否成功
 *       // 处理错误：打印或传播 ret 值
 *   }
 * @endcode
 *
 * [改进] 未来可以添加 ERR_OVERFLOW(-7)、ERR_UNDERFLOW(-8)、ERR_NO_MEM(-9) 等，
 *        覆盖更多嵌入式系统常见错误场景。
 *
 * [注意] 枚举值不要轻易修改数值，否则会破坏已存储在 Flash/日志中的错误码含义。
 */

#ifndef ERROR_CODE_H             /* 头文件防重复包含保护（开始）*/
#define ERROR_CODE_H             /* 定义本文件标识宏 */

#ifdef __cplusplus               /* C++ 兼容：使用 C 链接格式（避免名称修饰）*/
extern "C" {                     /* 开始 C 链接声明区域 */
#endif

/**
 * @brief   统一错误码枚举类型
 * @details 所有新模块的函数返回值类型应声明为 Err_t。
 *          错误代表：ERR_OK = 成功，其他负值 = 特定错误原因。
 *
 * 如何选择正确的错误码：
 *   - 参数校验失败（NULL、越界）     → ERR_INVALID_ARG
 *   - 硬件通信失败（NACK、超时）      → ERR_IO_FAILED
 *   - 等待超时（互斥量、信号量）      → ERR_TIMEOUT
 *   - 模块未调用 Init               → ERR_NOT_INIT
 *   - 资源被占用（DMA 忙、缓冲满）    → ERR_BUSY
 *   - 设备/文件/资源不存在           → ERR_NOT_FOUND
 */
typedef enum
{
    ERR_OK          =  0,    /**< 操作成功（零值，可直接用 `!ret` 判断）*/
    ERR_INVALID_ARG = -1,    /**< 参数无效（NULL 指针、数值越界、枚举非法等）*/
    ERR_IO_FAILED   = -2,    /**< I/O 操作失败（I2C NACK、SPI 超时、FatFS 写入错误等）*/
    ERR_TIMEOUT     = -3,    /**< 操作超时（FreeRTOS 互斥量超时、DMA 等待超时等）*/
    ERR_NOT_INIT    = -4,    /**< 模块未初始化（在调用 Init() 之前调用了功能函数）*/
    ERR_BUSY        = -5,    /**< 资源忙（I2C 总线仲裁失败、录音正在进行、缓冲区满等）*/
    ERR_NOT_FOUND   = -6,    /**< 设备/资源未找到（I2C 地址无应答、文件不存在等）*/
} Err_t;
/* [改进] 可以扩展：ERR_OVERFLOW=-7（缓冲溢出）、ERR_NO_MEM=-8（内存分配失败）等 */

#ifdef __cplusplus               /* 结束 C 链接声明区域 */
}
#endif

#endif /* ERROR_CODE_H */        /* 头文件防重复包含保护（结束）*/
