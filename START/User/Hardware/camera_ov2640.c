/**
 * @file    camera_ov2640.c
 * @brief   OV2640 200万像素图像传感器驱动实现
 * @details
 *   ## 硬件接口
 *   - SCCB（Serial Camera Control Bus，OmniVision 私有协议，与 I²C 兼容但有差异）
 *     采用 GPIO 软件模拟时序，速率约 100kbps（s_sccb_delay = 5µs 半周期）。
 *   - SCL: PG3（推挽输出），SDA: PB10（开漏输出，符合 SCCB 规范）。
 *   - RST: PC5（低电平复位），PWDN: PI3（高电平掉电）。
 *
 *   ## SCCB 与 I²C 区别
 *   - OV2640 SCCB 写操作：START + 从地址(0x60) + 寄存器地址 + 数据 + STOP（3相）。
 *   - OV2640 SCCB 读操作：先写寄存器地址（2相），再重新 START 发读地址（0x61）读取（2相）。
 *   - 第9 bit 为"Don't Care"（从机可不拉低 ACK），因此 ACK 检测失败时驱动按警告处理。
 *
 *   ## 寄存器 Bank 说明
 *   OV2640 寄存器分两 Bank（DSP Bank / Sensor Bank），通过 0xFF 寄存器切换：
 *   - 0xFF=0x00: DSP Bank（图像处理、输出格式、分辨率缩放等）
 *   - 0xFF=0x01: Sensor Bank（曝光、增益、传感器物理参数等）
 *
 *   ## 图像管道
 *   传感器物理采集 → SVGA 800×600 捕获窗口（imagesize_set）
 *     → DSP 处理窗口（image_win_set）→ 缩放输出（outsize_set）→ RGB565 像素流
 *
 *   ## 延时使用
 *   初始化路径调用 HAL_Delay（ms 级），SCCB 时序使用 DWT_Timer_DelayUs（µs 级）。
 *   [改进] 初始化路径在 RTOS 中调用 HAL_Delay 会阻塞调用任务，
 *          建议改为 vTaskDelay 或在初始化任务中运行。
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

/** @brief OV2640 SVGA模式初始化寄存器配置表（地址-值对）
 * @details
 *   包含 ~130 组寄存器写入命令，依序配置以下子系统：
 *   1. {0xFF,0x00} → 切换到 DSP Bank
 *   2. {0xFF,0x01} → 切换到 Sensor Bank
 *   3. {0x12,0x40} → COM7: SVGA 分辨率模式
 *   4. {0x17/0x18} → HSTART/HSTOP: 水平扫描起止
 *   5. {0x19/0x1A} → VSTART/VSTOP: 垂直扫描起止
 *   6. {0x24/0x25/0x26} → AEW/AEB/VV: 自动曝光控制范围
 *   7. {0xC0/0xC1} → HSIZE/VSIZE: 传感器采集窗口
 *   8. {0xDA}     → IMAGE_MODE: 输出格式（后续由 RGB565 表覆盖）
 *   [注意] 该表为厂商推荐值，不建议修改；如需调整曝光/白平衡等，
 *          应在初始化完成后额外写入对应寄存器。
 */
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

/**
 * @brief OV2640 RGB565输出模式寄存器配置表（地址-值对）
 * @details
 *   切换 DSP 输出为 RGB565 格式，共 15 组写入：
 *   - {0xFF,0x00} → 切换到 DSP Bank
 *   - {0xDA,0x09} → IMAGE_MODE: bit[3]=1 选 RGB565, bit[0]=1 字节序 big-endian
 *   - {0xD7,0x03} → 使能 RGB bypass
 *   - {0xE1,0x67} → FORMAT 控制寄存器
 *   - {0xFF,0x01} → 切换到 Sensor Bank，清除传感器端相关格式位
 *   - 后续若干 {0xE0/E1/E5/D7/DA,0x00} → 清除 DSP/Sensor 临时状态标志
 *   [注意] 写完此表后仍需调用 imagesize_set + image_win_set + outsize_set
 *          才能完成完整的分辨率配置。
 */
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
 * @brief SCCB总线起始条件（START condition）
 * @details
 *   时序：SDA先置高，SCL置高（总线空闲状态），等待稳定后 SDA 拉低（触发 START），
 *   再将 SCL 拉低（准备传输第一个数据位）。
 *   时序图：  SDA: ‾‾‾\___
 *             SCL: ‾‾‾‾\__
 *   [注意] SCCB START 与标准 I²C START 时序完全相同。
 */
static void s_sccb_start(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u); /* SDA=1：总线空闲 */
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u); /* SCL=1：总线空闲 */
    s_sccb_delay();                                                    /* 等待稳定（≥5µs）*/
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 0u); /* SDA=0：SCL高期间下降沿=START */
    s_sccb_delay();                                                    /* 保持 START 建立时间 */
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u); /* SCL=0：拉低准备发数据 */
    s_sccb_delay();                                                    /* 等待 SCL 稳定 */
}

/**
 * @brief SCCB总线停止条件（STOP condition）
 * @details
 *   时序：确保 SDA 为低（数据传输结束状态），拉高 SCL，再拉高 SDA。
 *   SCL 高期间 SDA 出现上升沿即为 STOP。
 *   时序图：  SDA: ___/‾‾‾
 *             SCL: ___/‾‾‾
 */
static void s_sccb_stop(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 0u); /* SDA=0：确保低电平起点 */
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u); /* SCL=1：时钟先高 */
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u); /* SDA=1：SCL高期间上升沿=STOP */
    s_sccb_delay();                                                    /* 等待总线释放 */
}

/**
 * @brief SCCB总线发送 NA（No Acknowledge）信号
 * @details
 *   SCCB 读操作的最后一个字节之后，主机必须发 NA（非应答）而非 ACK，
 *   以通知从机停止传输（随后发 STOP）。
 *   时序：SDA 保持高，SCL 产生一个时钟脉冲（高→低）。
 *   [注意] SCCB 规范将此称为 NA bit，在标准 I²C 中等价于 NACK。
 */
static void s_sccb_nack(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u); /* SDA=1：NA = 非应答 */
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u); /* SCL=1：时钟高电平 */
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u); /* SCL=0：时钟下降沿，完成 NA bit */
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

    /* 发送8位数据，MSB优先（bit7 → bit0）*/
    for (i = 0u; i < 8u; i++)
    {
        /* 将当前最高位输出到 SDA（不等于0则高电平）*/
        s_gpio_write(CAMERA_OV2640_SDA_PORT,
                     CAMERA_OV2640_SDA_PIN,
                     (uint8_t)((data & 0x80u) != 0u));
        s_sccb_delay();                                                    /* SDA 建立时间 */
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u); /* SCL=1：采样沿 */
        s_sccb_delay();
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u); /* SCL=0：移到下一位 */
        data <<= 1;                                                        /* 下一位移至 bit7 */
    }

    /* 第9 bit：释放 SDA（置高），等待从机应答（ACK/Don't-Care）*/
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u); /* 主机释放 SDA */
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u); /* SCL=1：采样第9 bit */
    s_sccb_delay();

    /* 读取 SDA 电平：低=ACK（从机拉低），高=NACK/Don't-Care */
    if (HAL_GPIO_ReadPin(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN) != GPIO_PIN_RESET)
    {
        /* SCCB 第9位为 Don't Care，从机可不拉低，此处视为警告但继续 */
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        return 1u;  /* 返回1：未收到 ACK（调用方可选择忽略）*/
    }

    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u); /* SCL=0：完成第9 bit */
    return 0u;  /* 返回0：收到 ACK */
}

/**
 * @brief   从SCCB总线读取一个字节
 * @return  读取到的字节数据
 */
static uint8_t s_sccb_read_byte(void)
{
    uint8_t i;
    uint8_t value = 0u;

    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u); /* 释放 SDA（开漏转输入模式）*/
    /* 逐位读取，MSB优先（SCL上升沿采样 SDA）*/
    for (i = 0u; i < 8u; i++)
    {
        value <<= 1;                                                        /* 移位腾出低位 */
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);  /* SCL=1：上升沿，从机输出有效 */
        s_sccb_delay();                                                     /* 等待数据稳定 */
        if (HAL_GPIO_ReadPin(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN) != GPIO_PIN_RESET)
        {
            value |= 1u;   /* SDA=1 → 当前位为1 */
        }
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);  /* SCL=0：下降沿，从机准备下一位 */
        s_sccb_delay();
    }
    /* 调用方负责在读取完成后发送 NA（NACK）+ STOP */
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
/**
 * @brief   设置OV2640 DSP 缩放输出尺寸（ZMOW / ZMOH / ZMHH）
 * @param   width   [in] 输出宽度（像素，必须为4的倍数）
 * @param   height  [in] 输出高度（像素，必须为4的倍数）
 * @return  0: 成功; 1: 尺寸不满足4的倍数约束
 * @details
 *   OV2640 DSP 缩放寄存器以步进4为基本单位，因此传入值需除以4后写入寄存器：
 *   - 0x5A (ZMOW)    = out_w[7:0]  （输出宽度低8位，单位：4像素）
 *   - 0x5B (ZMOH)    = out_h[7:0]  （输出高度低8位，单位：4像素）
 *   - 0x5C (ZMHH)    = {out_h[8], out_w[9:8]}（高位合包）
 *     bit[1:0] = out_w[9:8]，bit[2] = out_h[8]
 *   - 0xE0           = 0x04：使能 DSP 缩放配置模式（必须先写）
 *   - 0xE0           = 0x00：退出配置模式，使设置生效
 *   [注意] 应在 s_ov2640_image_win_set 之后调用，确保源窗口已配置。
 */
static uint8_t s_ov2640_outsize_set(uint16_t width, uint16_t height)
{
    uint16_t out_h;
    uint16_t out_w;
    uint8_t temp;

    if (((width % 4u) != 0u) || ((height % 4u) != 0u))
    {
        return 1u;   /* 尺寸不满足4像素对齐约束 */
    }

    out_w = (uint16_t)(width  / 4u);   /* 转换为寄存器单位（步进4）*/
    out_h = (uint16_t)(height / 4u);   /* 转换为寄存器单位（步进4）*/

    (void)s_ov2640_write_reg(0xff, 0x00);                         /* 切换到 DSP Bank */
    (void)s_ov2640_write_reg(0xe0, 0x04);                         /* 使能 DSP 配置模式 */
    (void)s_ov2640_write_reg(0x5a, (uint8_t)(out_w & 0xFFu));    /* ZMOW[7:0] */
    (void)s_ov2640_write_reg(0x5b, (uint8_t)(out_h & 0xFFu));    /* ZMOH[7:0] */

    /* ZMHH: bit[1:0]=out_w[9:8], bit[2]=out_h[8] */
    temp = (uint8_t)((out_w >> 8) & 0x03u);        /* out_w 高2位 → bit[1:0] */
    temp |= (uint8_t)((out_h >> 6) & 0x04u);       /* out_h 第8位 → bit[2] */
    (void)s_ov2640_write_reg(0x5c, temp);           /* ZMHH */
    (void)s_ov2640_write_reg(0xe0, 0x00);           /* 退出配置模式，设置生效 */

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
/**
 * @brief   设置OV2640 DSP 图像处理窗口（HSIZE/VSIZE/HOFFSET/VOFFSET）
 * @param   off_x   [in] 水平方向偏移（像素）
 * @param   off_y   [in] 垂直方向偏移（像素）
 * @param   width   [in] 窗口宽度（像素，必须为4的倍数）
 * @param   height  [in] 窗口高度（像素，必须为4的倍数）
 * @return  0: 成功; 1: 尺寸不满足4的倍数约束
 * @details
 *   配置 DSP Bank 的图像窗口寄存器（以步进4为单位）：
 *   - 0x51 (HSIZE)    = hsize[7:0]    （水平窗口大小低8位）
 *   - 0x52 (VSIZE)    = vsize[7:0]    （垂直窗口大小低8位）
 *   - 0x53 (XOFFL)   = off_x[7:0]    （水平偏移低8位）
 *   - 0x54 (YOFFL)   = off_y[7:0]    （垂直偏移低8位）
 *   - 0x55 (VHYX)    = 各高位合包
 *     bit[7]   = vsize[8]
 *     bit[6:4] = off_y[11:8]（取 (off_y>>4)&0x70）
 *     bit[3]   = hsize[8]   （取 (hsize>>5)&0x08）
 *     bit[2:0] = off_x[10:8]（取 off_x>>8）
 *   - 0x57 (TEST)    bit[7] = hsize[9]（hsize最高位）
 *   [注意] 通常配置为全 SVGA 窗口（0,0,800,600），由 s_ov2640_outsize_set 负责缩放。
 */
static uint8_t s_ov2640_image_win_set(uint16_t off_x, uint16_t off_y, uint16_t width, uint16_t height)
{
    uint16_t hsize;
    uint16_t vsize;
    uint8_t temp;

    if (((width % 4u) != 0u) || ((height % 4u) != 0u))
    {
        return 1u;   /* 尺寸不满足4像素对齐约束 */
    }

    hsize = (uint16_t)(width  / 4u);   /* 横向步进单位 */
    vsize = (uint16_t)(height / 4u);   /* 纵向步进单位 */

    (void)s_ov2640_write_reg(0xff, 0x00);                         /* 切换到 DSP Bank */
    (void)s_ov2640_write_reg(0xe0, 0x04);                         /* 使能 DSP 配置模式 */
    (void)s_ov2640_write_reg(0x51, (uint8_t)(hsize & 0xFFu));    /* HSIZE[7:0] */
    (void)s_ov2640_write_reg(0x52, (uint8_t)(vsize & 0xFFu));    /* VSIZE[7:0] */
    (void)s_ov2640_write_reg(0x53, (uint8_t)(off_x & 0xFFu));    /* XOFFL[7:0] */
    (void)s_ov2640_write_reg(0x54, (uint8_t)(off_y & 0xFFu));    /* YOFFL[7:0] */

    /* VHYX: 将各高位打包到一个字节 */
    temp  = (uint8_t)((vsize >> 1) & 0x80u);  /* vsize[8]   → bit[7] */
    temp |= (uint8_t)((off_y >> 4) & 0x70u);  /* off_y[6:4] → bit[6:4] */
    temp |= (uint8_t)((hsize >> 5) & 0x08u);  /* hsize[8]   → bit[3] */
    temp |= (uint8_t)((off_x >> 8) & 0x07u);  /* off_x[2:0] → bit[2:0] */
    (void)s_ov2640_write_reg(0x55, temp);      /* VHYX 合包寄存器 */

    /* TEST 寄存器 bit[7] = hsize[9]（最高位单独在此）*/
    (void)s_ov2640_write_reg(0x57, (uint8_t)((hsize >> 2) & 0x80u));
    (void)s_ov2640_write_reg(0xe0, 0x00);      /* 退出配置模式，设置生效 */

    return 0u;
}

/**
 * @brief   设置OV2640传感器输出图像分辨率
 * @param   width   [in] 图像宽度（像素）
 * @param   height  [in] 图像高度（像素）
 * @return  0: 设置成功
 */
/**
 * @brief   设置OV2640传感器采集分辨率（HSIZE8 / MHSIZE / VSIZE8 / MVFACT）
 * @param   width   [in] 采集宽度（像素，SVGA典型值=800）
 * @param   height  [in] 采集高度（像素，SVGA典型值=600）
 * @return  0: 成功（当前无约束校验）
 * @details
 *   配置 Sensor/DSP 采集源头窗口大小（以步进8为单位存储）：
 *   - 0xC0 (HSIZE8) = width[10:3]   （宽度高8位，步进8）
 *   - 0xC1 (VSIZE8) = height[9:3]   （高度高7位，步进8）
 *   - 0x8C (SIZEL)  = 低位合包：
 *     bit[6:4] = width[2:0]  （宽度低3位）
 *     bit[2:0] = height[2:0] （高度低3位）
 *     bit[7]   = width[11]   （宽度第11位，>2048时使用）
 *   [注意] 该函数配置的是传感器采集窗口（图像管道起点），
 *          不是最终输出分辨率；最终分辨率由 s_ov2640_outsize_set 决定。
 */
static uint8_t s_ov2640_imagesize_set(uint16_t width, uint16_t height)
{
    uint8_t temp;

    (void)s_ov2640_write_reg(0xff, 0x00);                              /* 切换到 DSP Bank */
    (void)s_ov2640_write_reg(0xe0, 0x04);                              /* 使能 DSP 配置模式 */
    (void)s_ov2640_write_reg(0xc0, (uint8_t)((width  >> 3) & 0xFFu)); /* HSIZE8 = width[10:3] */
    (void)s_ov2640_write_reg(0xc1, (uint8_t)((height >> 3) & 0xFFu)); /* VSIZE8 = height[9:3] */

    /* SIZEL: 将低3位合包到一个字节 */
    temp  = (uint8_t)((width  & 0x07u) << 3);  /* width[2:0]  → bit[6:4] */
    temp |= (uint8_t)(height  & 0x07u);         /* height[2:0] → bit[2:0] */
    temp |= (uint8_t)((width  >> 4) & 0x80u);  /* width[11]   → bit[7] */
    (void)s_ov2640_write_reg(0x8c, temp);       /* SIZEL 合包寄存器 */
    (void)s_ov2640_write_reg(0xe0, 0x00);       /* 退出配置模式 */

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

    /* 硬件上电序列：先解除掉电（PWDN=低），再复位（RST: 低→高）*/
    s_gpio_write(CAMERA_OV2640_PWDN_PORT, CAMERA_OV2640_PWDN_PIN, 0u); /* PWDN=0：退出掉电模式 */
    HAL_Delay(10u);                                                      /* 等待电源稳定 */
    s_gpio_write(CAMERA_OV2640_RST_PORT, CAMERA_OV2640_RST_PIN, 0u);   /* RST=0：拉低复位 */
    HAL_Delay(20u);                                                      /* 保持复位状态（≥1ms即可，取20ms保守值）*/
    s_gpio_write(CAMERA_OV2640_RST_PORT, CAMERA_OV2640_RST_PIN, 1u);   /* RST=1：释放复位 */
    HAL_Delay(20u);                                                      /* 等待内部 PLL 锁定稳定 */

    s_sccb_init();      /* 初始化 SCCB GPIO + 发送一次 STOP 确保总线空闲 */
    HAL_Delay(5u);      /* SCCB 时序稳定等待 */

    /* 软件复位：写 Sensor Bank 的 COM7 寄存器 bit[7]=1 */
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_SOFT_RESET;
    s_ov2640_last_write_status = s_ov2640_write_reg(CAMERA_OV2640_REG_BANK_SEL, 0x01u); /* 先切换到 Sensor Bank */
    if (s_ov2640_last_write_status != 0u)
    {
        printf("CAM: OV2640 bank select write failed\r\n");
        return 1u;
    }
    s_ov2640_last_write_status = s_ov2640_write_reg(CAMERA_OV2640_REG_COM7, 0x80u); /* COM7[7]=1：软件复位 */
    if (s_ov2640_last_write_status != 0u)
    {
        printf("CAM: OV2640 soft reset write failed\r\n");
        return 1u;
    }
    HAL_Delay(50u);   /* 等待软件复位完成（OV2640 spec 要求 ≥5ms，保守取 50ms）*/

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
