/**
 * @file    error_code.h
 * @brief   统一错误码定义
 * @details 为整个项目提供统一的函数返回值约定，替代各模块不一致的返回值
 *
 * 背景问题：
 * - soft_i2c.c  返回 0=成功, 1=失败
 * - camera_ov2640.c 返回 1=成功, 0=失败 (反义!)
 * - pcmd3180.c  返回 PCMD3180_OK(0), PCMD3180_ERR(-1)
 *
 * 统一约定：
 * - 0 (ERR_OK) 永远表示成功
 * - 非零值表示具体的错误类型
 *
 * 使用方式：
 * @code
 *   Err_t ret = SomeModule_Init();
 *   if (ret != ERR_OK) {
 *       printf("Init failed: %d\n", ret);
 *   }
 * @endcode
 *
 * @note    本头文件不改变已有模块的返回值约定 (保持兼容)，
 *          仅供新代码和逐步迁移使用。
 */

#ifndef ERROR_CODE_H
#define ERROR_CODE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   统一错误码枚举
 * @details 所有新模块的返回值应使用此枚举
 */
typedef enum
{
    ERR_OK           =  0,   /**< 操作成功 */
    ERR_INVALID_ARG  = -1,   /**< 参数无效 (NULL 指针、越界等) */
    ERR_IO_FAILED    = -2,   /**< I/O 操作失败 (I2C NACK、DMA 超时等) */
    ERR_TIMEOUT      = -3,   /**< 操作超时 (互斥量获取、应答等待等) */
    ERR_NOT_INIT     = -4,   /**< 模块未初始化 */
    ERR_BUSY         = -5,   /**< 资源忙 (总线占用、缓冲区满等) */
    ERR_NOT_FOUND    = -6,   /**< 设备/资源未找到 (I2C 扫描无应答等) */
} Err_t;

#ifdef __cplusplus
}
#endif

#endif /* ERROR_CODE_H */
