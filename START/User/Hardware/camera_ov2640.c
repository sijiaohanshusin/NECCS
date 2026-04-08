/**
 * @file    camera_ov2640.c
 * @brief   OV2640图像传感器驱动实现
 * @details 实现OV2640摄像头的SCCB(串行摄像头控制总线)通信、硬件复位、
 *          寄存器配置、RGB565输出模式设置及诊断信息采集。
 *          使用GPIO软件模拟SCCB时序，通过DWT周期计数器实现微秒级精确延时。
 */

#include "camera_ov2640.h"

#include "main.h"
#include "dwt_timer.h"

#include <stdio.h>

/** @defgroup OV2640_Private_Defines OV2640私有宏定义
 * @{
 */
#define CAMERA_OV2640_ADDR            0x60u   /**< OV2640 SCCB从机地址(写地址) */
#define CAMERA_OV2640_MID             0x7FA2u /**< OV2640期望的厂商ID */
#define CAMERA_OV2640_PID             0x2642u /**< OV2640期望的产品ID */
#define CAMERA_OV2640_REG_BANK_SEL    0xFFu   /**< 寄存器页选择寄存器 */
#define CAMERA_OV2640_REG_COM7        0x12u   /**< 通用控制寄存器7(软复位) */
#define CAMERA_OV2640_REG_MIDH        0x1Cu   /**< 厂商ID高字节寄存器 */
#define CAMERA_OV2640_REG_MIDL        0x1Du   /**< 厂商ID低字节寄存器 */
#define CAMERA_OV2640_REG_PIDH        0x0Au   /**< 产品ID高字节寄存器 */
#define CAMERA_OV2640_REG_PIDL        0x0Bu   /**< 产品ID低字节寄存器 */

#define CAMERA_OV2640_RST_PORT        GPIOC       /**< 硬件复位引脚端口 */
#define CAMERA_OV2640_RST_PIN         GPIO_PIN_5  /**< 硬件复位引脚号 */
#define CAMERA_OV2640_PWDN_PORT       GPIOI       /**< 掉电控制引脚端口 */
#define CAMERA_OV2640_PWDN_PIN        GPIO_PIN_3  /**< 掉电控制引脚号 */

#define CAMERA_OV2640_SCL_PORT        GPIOG       /**< SCCB时钟线端口 */
#define CAMERA_OV2640_SCL_PIN         GPIO_PIN_3  /**< SCCB时钟线引脚号 */
#define CAMERA_OV2640_SDA_PORT        GPIOB       /**< SCCB数据线端口 */
#define CAMERA_OV2640_SDA_PIN         GPIO_PIN_10 /**< SCCB数据线引脚号 */

#define CAMERA_OV2640_FLIP_VER        0u  /**< 垂直翻转使能 (0=关闭, 非0=开启) */
#define CAMERA_OV2640_DIAG_NONE       0u  /**< 诊断阶段: 未开始 */
#define CAMERA_OV2640_DIAG_RESET_IO   1u  /**< 诊断阶段: GPIO复位初始化 */
#define CAMERA_OV2640_DIAG_SOFT_RESET 2u  /**< 诊断阶段: 软件复位 */
#define CAMERA_OV2640_DIAG_READ_MIDH  3u  /**< 诊断阶段: 读取厂商ID高字节 */
#define CAMERA_OV2640_DIAG_READ_MIDL  4u  /**< 诊断阶段: 读取厂商ID低字节 */
#define CAMERA_OV2640_DIAG_READ_PIDH  5u  /**< 诊断阶段: 读取产品ID高字节 */
#define CAMERA_OV2640_DIAG_READ_PIDL  6u  /**< 诊断阶段: 读取产品ID低字节 */
#define CAMERA_OV2640_DIAG_ID_MATCH   7u  /**< 诊断阶段: ID校验 */
#define CAMERA_OV2640_DIAG_READY      8u  /**< 诊断阶段: 就绪 */
/** @} */

/** @defgroup OV2640_Private_Variables OV2640私有变量
 * @{
 */
static volatile uint16_t s_ov2640_last_mid = 0u;                             /**< 最近读取到的厂商ID */
static volatile uint16_t s_ov2640_last_pid = 0u;                             /**< 最近读取到的产品ID */
static volatile uint8_t s_ov2640_diag_stage = CAMERA_OV2640_DIAG_NONE;       /**< 当前诊断阶段 */
static volatile uint8_t s_ov2640_last_write_status = 0u;                     /**< 最近SCCB写状态 */
static volatile uint8_t s_ov2640_last_read_status = 0u;                      /**< 最近SCCB读状态 */
/** @} */

/**
 * @brief   微秒级延时（基于DWT周期计数器）
 * @param   us  [in] 延时时间，单位微秒
 */
static void s_delay_us(uint32_t us)
{
    DWT_Timer_DelayUs(us);
}

/** @brief OV2640 SVGA模式初始化寄存器配置表 (地址-值对) */
static const uint8_t s_ov2640_svga_init_reg_tbl[][2] =
{
    {0xff, 0x00},
    {0x2c, 0xff},
    {0x2e, 0xdf},
    {0xff, 0x01},
    {0x3c, 0x32},
    {0x11, 0x00},
    {0x09, 0x02},
#if (CAMERA_OV2640_FLIP_VER != 0u)
    {0x04, 0xD8},
#else
    {0x04, 0x28},
#endif
    {0x13, 0xe5},
    {0x14, 0x48},
    {0x2c, 0x0c},
    {0x33, 0x78},
    {0x3a, 0x33},
    {0x3b, 0xfb},
    {0x3e, 0x00},
    {0x43, 0x11},
    {0x16, 0x10},
    {0x39, 0x92},
    {0x35, 0xda},
    {0x22, 0x1a},
    {0x37, 0xc3},
    {0x23, 0x00},
    {0x34, 0xc0},
    {0x36, 0x1a},
    {0x06, 0x88},
    {0x07, 0xc0},
    {0x0d, 0x87},
    {0x0e, 0x41},
    {0x4c, 0x00},
    {0x48, 0x00},
    {0x5b, 0x00},
    {0x42, 0x03},
    {0x4a, 0x81},
    {0x21, 0x99},
    {0x24, 0x40},
    {0x25, 0x38},
    {0x26, 0x82},
    {0x5c, 0x00},
    {0x63, 0x00},
    {0x46, 0x22},
    {0x0c, 0x3c},
    {0x61, 0x70},
    {0x62, 0x80},
    {0x7c, 0x05},
    {0x20, 0x80},
    {0x28, 0x30},
    {0x6c, 0x00},
    {0x6d, 0x80},
    {0x6e, 0x00},
    {0x70, 0x02},
    {0x71, 0x94},
    {0x73, 0xc1},
    {0x3d, 0x34},
    {0x5a, 0x57},
    {0x12, 0x40},
    {0x17, 0x11},
    {0x18, 0x43},
    {0x19, 0x00},
    {0x1a, 0x4b},
    {0x32, 0x09},
    {0x37, 0xc0},
    {0x4f, 0xca},
    {0x50, 0xa8},
    {0x5a, 0x23},
    {0x6d, 0x00},
    {0x3d, 0x38},
    {0xff, 0x00},
    {0xe5, 0x7f},
    {0xf9, 0xc0},
    {0x41, 0x24},
    {0xe0, 0x14},
    {0x76, 0xff},
    {0x33, 0xa0},
    {0x42, 0x20},
    {0x43, 0x18},
    {0x4c, 0x00},
    {0x87, 0xd5},
    {0x88, 0x3f},
    {0xd7, 0x03},
    {0xd9, 0x10},
    {0xd3, 0x82},
    {0xc8, 0x08},
    {0xc9, 0x80},
    {0x7c, 0x00},
    {0x7d, 0x00},
    {0x7c, 0x03},
    {0x7d, 0x48},
    {0x7d, 0x48},
    {0x7c, 0x08},
    {0x7d, 0x20},
    {0x7d, 0x10},
    {0x7d, 0x0e},
    {0x90, 0x00},
    {0x91, 0x0e},
    {0x91, 0x1a},
    {0x91, 0x31},
    {0x91, 0x5a},
    {0x91, 0x69},
    {0x91, 0x75},
    {0x91, 0x7e},
    {0x91, 0x88},
    {0x91, 0x8f},
    {0x91, 0x96},
    {0x91, 0xa3},
    {0x91, 0xaf},
    {0x91, 0xc4},
    {0x91, 0xd7},
    {0x91, 0xe8},
    {0x91, 0x20},
    {0x92, 0x00},
    {0x93, 0x06},
    {0x93, 0xe3},
    {0x93, 0x05},
    {0x93, 0x05},
    {0x93, 0x00},
    {0x93, 0x04},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x96, 0x00},
    {0x97, 0x08},
    {0x97, 0x19},
    {0x97, 0x02},
    {0x97, 0x0c},
    {0x97, 0x24},
    {0x97, 0x30},
    {0x97, 0x28},
    {0x97, 0x26},
    {0x97, 0x02},
    {0x97, 0x98},
    {0x97, 0x80},
    {0x97, 0x00},
    {0x97, 0x00},
    {0xc3, 0xed},
    {0xa4, 0x00},
    {0xa8, 0x00},
    {0xc5, 0x11},
    {0xc6, 0x51},
    {0xbf, 0x80},
    {0xc7, 0x10},
    {0xb6, 0x66},
    {0xb8, 0xa5},
    {0xb7, 0x64},
    {0xb9, 0x7c},
    {0xb3, 0xaf},
    {0xb4, 0x97},
    {0xb5, 0xff},
    {0xb0, 0xc5},
    {0xb1, 0x94},
    {0xb2, 0x0f},
    {0xc4, 0x5c},
    {0xc0, 0x64},
    {0xc1, 0x4b},
    {0x8c, 0x00},
    {0x86, 0x3d},
    {0x50, 0x00},
    {0x51, 0xc8},
    {0x52, 0x96},
    {0x53, 0x00},
    {0x54, 0x00},
    {0x55, 0x00},
    {0x5a, 0xc8},
    {0x5b, 0x96},
    {0x5c, 0x00},
    {0xd3, 0x02},
    {0xc3, 0xed},
    {0x7f, 0x00},
    {0xda, 0x09},
    {0xe5, 0x1f},
    {0xe1, 0x67},
    {0xe0, 0x00},
    {0xdd, 0x7f},
    {0x05, 0x00},
};

/** @brief OV2640 RGB565输出模式寄存器配置表 (地址-值对) */
static const uint8_t s_ov2640_rgb565_reg_tbl[][2] =
{
    {0xff, 0x00},
    {0xda, 0x09},
    {0xd7, 0x03},
    {0xdf, 0x02},
    {0x33, 0xa0},
    {0x3c, 0x00},
    {0xe1, 0x67},
    {0xff, 0x01},
    {0x11, 0x00},
    {0xe0, 0x00},
    {0xe1, 0x00},
    {0xe5, 0x00},
    {0xd7, 0x00},
    {0xda, 0x00},
    {0xe0, 0x00},
};

/**
 * @brief   GPIO引脚电平写入
 * @param   port   [in] GPIO端口指针
 * @param   pin    [in] GPIO引脚号
 * @param   level  [in] 输出电平 (0=低电平, 非0=高电平)
 */
static void s_gpio_write(GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    HAL_GPIO_WritePin(port, pin, (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief SCCB总线时序延时（约5微秒）
 */
static void s_sccb_delay(void)
{
    s_delay_us(5u);
}

static void s_sccb_stop(void);

/**
 * @brief   初始化SCCB总线GPIO引脚
 * @details 配置SCL为推挽输出，SDA为开漏输出，并发送一次停止条件。
 */
static void s_sccb_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init.Pin = CAMERA_OV2640_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(CAMERA_OV2640_SCL_PORT, &gpio_init);

    gpio_init.Pin = CAMERA_OV2640_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(CAMERA_OV2640_SDA_PORT, &gpio_init);

    s_sccb_stop();
}

/**
 * @brief SCCB总线起始条件：SCL高电平期间SDA由高变低
 */
static void s_sccb_start(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 0u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
    s_sccb_delay();
}

/**
 * @brief SCCB总线停止条件：SCL高电平期间SDA由低变高
 */
static void s_sccb_stop(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 0u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_sccb_delay();
}

/**
 * @brief SCCB总线发送NACK（非应答）信号
 */
static void s_sccb_nack(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
    s_sccb_delay();
}

/**
 * @brief   通过SCCB总线发送一个字节
 * @param   data  [in] 待发送的字节数据
 * @return  0: 收到ACK应答; 1: 未收到ACK(NACK)
 */
static uint8_t s_sccb_send_byte(uint8_t data)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        s_gpio_write(CAMERA_OV2640_SDA_PORT,
                     CAMERA_OV2640_SDA_PIN,
                     (uint8_t)((data & 0x80u) != 0u));
        s_sccb_delay();
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
        s_sccb_delay();
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        data <<= 1;
    }

    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();

    if (HAL_GPIO_ReadPin(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN) != GPIO_PIN_RESET)
    {
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        return 1u;
    }

    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
    return 0u;
}

/**
 * @brief   从SCCB总线读取一个字节
 * @return  读取到的字节数据
 */
static uint8_t s_sccb_read_byte(void)
{
    uint8_t i;
    uint8_t value = 0u;

    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    for (i = 0u; i < 8u; i++)
    {
        value <<= 1;
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
        s_sccb_delay();
        if (HAL_GPIO_ReadPin(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN) != GPIO_PIN_RESET)
        {
            value |= 1u;
        }
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        s_sccb_delay();
    }

    return value;
}

/**
 * @brief   向OV2640写入一个寄存器
 * @param   reg    [in] 寄存器地址
 * @param   value  [in] 寄存器值
 * @return  0: 写入成功; 1: 写入失败(SCCB无应答)
 */
static uint8_t s_ov2640_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t status = 0u;

    s_sccb_start();
    s_delay_us(100u);
    if (s_sccb_send_byte(CAMERA_OV2640_ADDR) != 0u)
    {
        status = 1u;
    }
    s_delay_us(100u);
    if (s_sccb_send_byte(reg) != 0u)
    {
        status = 1u;
    }
    s_delay_us(100u);
    if (s_sccb_send_byte(value) != 0u)
    {
        status = 1u;
    }
    s_delay_us(100u);
    s_sccb_stop();

    return status;
}

/**
 * @brief   从OV2640读取一个寄存器
 * @param   reg    [in]  寄存器地址
 * @param   value  [out] 指向用于存储读取值的变量
 * @return  0: 读取成功; 1: 读取失败或参数为NULL
 */
static uint8_t s_ov2640_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t data = 0u;

    if (value == NULL)
    {
        return 1u;
    }

    s_sccb_start();
    s_delay_us(100u);
    if (s_sccb_send_byte(CAMERA_OV2640_ADDR) != 0u)
    {
        s_sccb_stop();
        return 1u;
    }
    s_delay_us(100u);
    if (s_sccb_send_byte(reg) != 0u)
    {
        s_sccb_stop();
        return 1u;
    }
    s_sccb_stop();
    s_delay_us(100u);

    s_sccb_start();
    s_delay_us(100u);
    if (s_sccb_send_byte(CAMERA_OV2640_ADDR | 0x01u) != 0u)
    {
        s_sccb_stop();
        return 1u;
    }
    s_delay_us(100u);
    data = s_sccb_read_byte();
    s_sccb_nack();
    s_sccb_stop();

    *value = data;
    return 0u;
}

/**
 * @brief   批量写入寄存器配置表
 * @param   table       [in] 寄存器配置表(地址-值对数组)
 * @param   pair_count  [in] 配置对数量
 */
static void s_apply_table(const uint8_t table[][2], uint32_t pair_count)
{
    uint32_t i;

    for (i = 0u; i < pair_count; i++)
    {
        (void)s_ov2640_write_reg(table[i][0], table[i][1]);
    }
}

/**
 * @brief   初始化OV2640复位(RST)和掉电(PWDN)控制引脚
 */
static void s_reset_io_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio_init.Pin = CAMERA_OV2640_RST_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(CAMERA_OV2640_RST_PORT, &gpio_init);

    gpio_init.Pin = CAMERA_OV2640_PWDN_PIN;
    HAL_GPIO_Init(CAMERA_OV2640_PWDN_PORT, &gpio_init);
}

/**
 * @brief   设置OV2640输出图像尺寸
 * @param   width   [in] 输出宽度（像素，需为4的倍数）
 * @param   height  [in] 输出高度（像素，需为4的倍数）
 * @return  0: 设置成功; 1: 尺寸非4的倍数
 */
static uint8_t s_ov2640_outsize_set(uint16_t width, uint16_t height)
{
    uint16_t out_h;
    uint16_t out_w;
    uint8_t temp;

    if (((width % 4u) != 0u) || ((height % 4u) != 0u))
    {
        return 1u;
    }

    out_w = (uint16_t)(width / 4u);
    out_h = (uint16_t)(height / 4u);

    (void)s_ov2640_write_reg(0xff, 0x00);
    (void)s_ov2640_write_reg(0xe0, 0x04);
    (void)s_ov2640_write_reg(0x5a, (uint8_t)(out_w & 0xFFu));
    (void)s_ov2640_write_reg(0x5b, (uint8_t)(out_h & 0xFFu));

    temp = (uint8_t)((out_w >> 8) & 0x03u);
    temp |= (uint8_t)((out_h >> 6) & 0x04u);
    (void)s_ov2640_write_reg(0x5c, temp);
    (void)s_ov2640_write_reg(0xe0, 0x00);

    return 0u;
}

/**
 * @brief   设置OV2640图像窗口偏移和尺寸
 * @param   off_x   [in] 水平偏移（像素）
 * @param   off_y   [in] 垂直偏移（像素）
 * @param   width   [in] 窗口宽度（像素，需为4的倍数）
 * @param   height  [in] 窗口高度（像素，需为4的倍数）
 * @return  0: 设置成功; 1: 尺寸非4的倍数
 */
static uint8_t s_ov2640_image_win_set(uint16_t off_x, uint16_t off_y, uint16_t width, uint16_t height)
{
    uint16_t hsize;
    uint16_t vsize;
    uint8_t temp;

    if (((width % 4u) != 0u) || ((height % 4u) != 0u))
    {
        return 1u;
    }

    hsize = (uint16_t)(width / 4u);
    vsize = (uint16_t)(height / 4u);

    (void)s_ov2640_write_reg(0xff, 0x00);
    (void)s_ov2640_write_reg(0xe0, 0x04);
    (void)s_ov2640_write_reg(0x51, (uint8_t)(hsize & 0xFFu));
    (void)s_ov2640_write_reg(0x52, (uint8_t)(vsize & 0xFFu));
    (void)s_ov2640_write_reg(0x53, (uint8_t)(off_x & 0xFFu));
    (void)s_ov2640_write_reg(0x54, (uint8_t)(off_y & 0xFFu));

    temp = (uint8_t)((vsize >> 1) & 0x80u);
    temp |= (uint8_t)((off_y >> 4) & 0x70u);
    temp |= (uint8_t)((hsize >> 5) & 0x08u);
    temp |= (uint8_t)((off_x >> 8) & 0x07u);
    (void)s_ov2640_write_reg(0x55, temp);
    (void)s_ov2640_write_reg(0x57, (uint8_t)((hsize >> 2) & 0x80u));
    (void)s_ov2640_write_reg(0xe0, 0x00);

    return 0u;
}

/**
 * @brief   设置OV2640传感器输出图像分辨率
 * @param   width   [in] 图像宽度（像素）
 * @param   height  [in] 图像高度（像素）
 * @return  0: 设置成功
 */
static uint8_t s_ov2640_imagesize_set(uint16_t width, uint16_t height)
{
    uint8_t temp;

    (void)s_ov2640_write_reg(0xff, 0x00);
    (void)s_ov2640_write_reg(0xe0, 0x04);
    (void)s_ov2640_write_reg(0xc0, (uint8_t)((width >> 3) & 0xFFu));
    (void)s_ov2640_write_reg(0xc1, (uint8_t)((height >> 3) & 0xFFu));

    temp = (uint8_t)((width & 0x07u) << 3);
    temp |= (uint8_t)(height & 0x07u);
    temp |= (uint8_t)((width >> 4) & 0x80u);
    (void)s_ov2640_write_reg(0x8c, temp);
    (void)s_ov2640_write_reg(0xe0, 0x00);

    return 0u;
}

/**
 * @brief   读取OV2640的厂商ID和产品ID
 * @param   mid  [out] 指向用于存储厂商ID的变量
 * @param   pid  [out] 指向用于存储产品ID的变量
 * @return  0: 读取成功; 1: 读取失败或参数为NULL
 */
uint8_t Camera_OV2640_ReadId(uint16_t *mid, uint16_t *pid)
{
    uint8_t high;
    uint8_t low;

    if ((mid == NULL) || (pid == NULL))
    {
        return 1u;
    }

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_MIDH;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_MIDH, &high);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_MIDL;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_MIDL, &low);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    *mid = (uint16_t)(((uint16_t)high << 8) | low);
    s_ov2640_last_mid = *mid;

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_PIDH;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_PIDH, &high);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_PIDL;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_PIDL, &low);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    *pid = (uint16_t)(((uint16_t)high << 8) | low);
    s_ov2640_last_pid = *pid;

    return 0u;
}

/**
 * @brief   初始化OV2640摄像头模块
 * @details 执行流程：GPIO复位引脚初始化 -> 硬件复位序列(PWDN/RST) -> SCCB总线初始化
 *          -> 软件复位 -> 读取并校验ID -> 写入SVGA初始化寄存器表。
 * @return  0: 初始化成功; 1: 初始化失败
 */
uint8_t Camera_OV2640_Init(void)
{
    uint16_t mid = 0u;
    uint16_t pid = 0u;

    s_ov2640_last_mid = 0u;
    s_ov2640_last_pid = 0u;
    s_ov2640_last_write_status = 0u;
    s_ov2640_last_read_status = 0u;
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_RESET_IO;

    s_reset_io_init();

    s_gpio_write(CAMERA_OV2640_PWDN_PORT, CAMERA_OV2640_PWDN_PIN, 0u);
    HAL_Delay(10u);
    s_gpio_write(CAMERA_OV2640_RST_PORT, CAMERA_OV2640_RST_PIN, 0u);
    HAL_Delay(20u);
    s_gpio_write(CAMERA_OV2640_RST_PORT, CAMERA_OV2640_RST_PIN, 1u);
    HAL_Delay(20u);

    s_sccb_init();
    HAL_Delay(5u);

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_SOFT_RESET;
    s_ov2640_last_write_status = s_ov2640_write_reg(CAMERA_OV2640_REG_BANK_SEL, 0x01u);
    if (s_ov2640_last_write_status != 0u)
    {
        printf("CAM: OV2640 bank select write failed\r\n");
        return 1u;
    }
    s_ov2640_last_write_status = s_ov2640_write_reg(CAMERA_OV2640_REG_COM7, 0x80u);
    if (s_ov2640_last_write_status != 0u)
    {
        printf("CAM: OV2640 soft reset write failed\r\n");
        return 1u;
    }
    HAL_Delay(50u);

    if (Camera_OV2640_ReadId(&mid, &pid) != 0u)
    {
        printf("CAM: OV2640 read id failed\r\n");
        return 1u;
    }
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_ID_MATCH;
    if ((mid != CAMERA_OV2640_MID) || (pid != CAMERA_OV2640_PID))
    {
        printf("CAM: OV2640 id mismatch mid=0x%04X pid=0x%04X\r\n", mid, pid);
        return 1u;
    }

    s_apply_table(s_ov2640_svga_init_reg_tbl,
                  (uint32_t)(sizeof(s_ov2640_svga_init_reg_tbl) / sizeof(s_ov2640_svga_init_reg_tbl[0])));

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READY;
    return 0u;
}

/**
 * @brief   配置OV2640输出RGB565格式预览图像
 * @details 写入RGB565寄存器表，设置SVGA(800x600)源尺寸并缩放至目标分辨率。
 * @param   width   [in] 输出图像宽度（像素，需为4的倍数）
 * @param   height  [in] 输出图像高度（像素，需为4的倍数）
 * @return  0: 配置成功; 1: 配置失败
 */
uint8_t Camera_OV2640_ConfigRgb565Preview(uint16_t width, uint16_t height)
{
    s_apply_table(s_ov2640_rgb565_reg_tbl,
                  (uint32_t)(sizeof(s_ov2640_rgb565_reg_tbl) / sizeof(s_ov2640_rgb565_reg_tbl[0])));

    if (s_ov2640_imagesize_set(800u, 600u) != 0u)
    {
        return 1u;
    }
    if (s_ov2640_image_win_set(0u, 0u, 800u, 600u) != 0u)
    {
        return 1u;
    }
    if (s_ov2640_outsize_set(width, height) != 0u)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief   获取OV2640诊断信息
 * @param   diag  [out] 指向诊断信息结构体的指针，不可为NULL
 */
void Camera_OV2640_GetDiag(Camera_OV2640_Diag_t *diag)
{
    if (diag == NULL)
    {
        return;
    }

    diag->mid = s_ov2640_last_mid;
    diag->pid = s_ov2640_last_pid;
    diag->diag_stage = s_ov2640_diag_stage;
    diag->last_write_status = s_ov2640_last_write_status;
    diag->last_read_status = s_ov2640_last_read_status;
}
