/**
 * @file    touch_i2c.c
 * @brief   触摸屏 I2C 软件模拟（位操作）通信接口实现
 * @details 使用 GPIO + DWT 微秒延时实现 I2C 总线协议，
 *          提供起始/停止信号、字节发送接收、ACK/NACK 处理。
 *          适用于 STM32H743 平台。
 */

#include "touch_i2c.h"

static uint8_t s_touch_i2c_ready = 0u;  /**< I2C 总线初始化完成标志 */
static uint8_t s_touch_dwt_ready = 0u;  /**< DWT 周期计数器初始化完成标志 */

/**
 * @brief   微秒级延时（基于 DWT 周期计数器）
 * @param   us 延时时间，单位微秒
 */
static void s_touch_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;

    if (us == 0u)
    {
        return;
    }

    if (s_touch_dwt_ready == 0u)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        DWT->CYCCNT = 0u;
        s_touch_dwt_ready = 1u;
    }

    ticks = (SystemCoreClock / 1000000u) * us;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks)
    {
    }
}

/**
 * @brief   设置 I2C SCL 引脚电平
 * @param   level 电平值 (0=低电平, 非0=高电平)
 */
static void s_touch_scl_write(uint8_t level)
{
    HAL_GPIO_WritePin(TOUCH_I2C_SCL_GPIO_PORT,
                      TOUCH_I2C_SCL_GPIO_PIN,
                      (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief   设置 I2C SDA 引脚电平
 * @param   level 电平值 (0=低电平, 非0=高电平)
 */
static void s_touch_sda_write(uint8_t level)
{
    HAL_GPIO_WritePin(TOUCH_I2C_SDA_GPIO_PORT,
                      TOUCH_I2C_SDA_GPIO_PIN,
                      (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief   读取 I2C SDA 引脚电平
 * @return  GPIO_PIN_SET 或 GPIO_PIN_RESET
 */
static GPIO_PinState s_touch_sda_read(void)
{
    return HAL_GPIO_ReadPin(TOUCH_I2C_SDA_GPIO_PORT, TOUCH_I2C_SDA_GPIO_PIN);
}

/**
 * @brief   初始化触摸屏 I2C 总线
 * @details 配置 SCL/SDA GPIO，拉高总线并发送停止条件，仅首次调用执行
 */
void Touch_I2C_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    if (s_touch_i2c_ready != 0u)
    {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init.Pin = TOUCH_I2C_SCL_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TOUCH_I2C_SCL_GPIO_PORT, &gpio_init);

    gpio_init.Pin = TOUCH_I2C_SDA_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TOUCH_I2C_SDA_GPIO_PORT, &gpio_init);

    s_touch_scl_write(1u);
    s_touch_sda_write(1u);
    Touch_I2C_Stop();
    s_touch_i2c_ready = 1u;
}

/**
 * @brief 产生 I2C 起始信号
 */
void Touch_I2C_Start(void)
{
    s_touch_sda_write(1u);
    s_touch_scl_write(1u);
    s_touch_delay_us(2u);
    s_touch_sda_write(0u);
    s_touch_delay_us(2u);
    s_touch_scl_write(0u);
    s_touch_delay_us(2u);
}

/**
 * @brief 产生 I2C 停止信号
 */
void Touch_I2C_Stop(void)
{
    s_touch_sda_write(0u);
    s_touch_delay_us(2u);
    s_touch_scl_write(1u);
    s_touch_delay_us(2u);
    s_touch_sda_write(1u);
    s_touch_delay_us(2u);
}

/**
 * @brief   等待从设备 ACK 应答
 * @return  0: 收到 ACK, 1: 超时未收到 ACK
 */
uint8_t Touch_I2C_WaitAck(void)
{
    uint8_t wait_time = 0u;

    s_touch_sda_write(1u);
    s_touch_delay_us(2u);
    s_touch_scl_write(1u);
    s_touch_delay_us(2u);

    while (s_touch_sda_read() != GPIO_PIN_RESET)
    {
        wait_time++;
        if (wait_time > 250u)
        {
            s_touch_scl_write(0u);
            Touch_I2C_Stop();
            return 1u;
        }
        s_touch_delay_us(2u);
    }

    s_touch_scl_write(0u);
    s_touch_delay_us(2u);
    return 0u;
}

/**
 * @brief   发送一个字节数据
 * @param   data 待发送的字节，MSB 先发
 */
void Touch_I2C_SendByte(uint8_t data)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        s_touch_sda_write((uint8_t)((data & 0x80u) != 0u));
        s_touch_delay_us(2u);
        s_touch_scl_write(1u);
        s_touch_delay_us(2u);
        s_touch_scl_write(0u);
        s_touch_delay_us(2u);
        data <<= 1;
    }

    s_touch_sda_write(1u);
}

/**
 * @brief 发送 ACK 应答信号
 */
static void s_touch_ack(void)
{
    s_touch_sda_write(0u);
    s_touch_delay_us(2u);
    s_touch_scl_write(1u);
    s_touch_delay_us(2u);
    s_touch_scl_write(0u);
    s_touch_delay_us(2u);
    s_touch_sda_write(1u);
}

/**
 * @brief 发送 NACK 非应答信号
 */
static void s_touch_nack(void)
{
    s_touch_sda_write(1u);
    s_touch_delay_us(2u);
    s_touch_scl_write(1u);
    s_touch_delay_us(2u);
    s_touch_scl_write(0u);
    s_touch_delay_us(2u);
}

/**
 * @brief   读取一个字节数据
 * @param   ack 是否发送 ACK (1=发送 ACK, 0=发送 NACK)
 * @return  读取到的字节数据
 */
uint8_t Touch_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0u;

    s_touch_sda_write(1u);
    for (i = 0u; i < 8u; i++)
    {
        data <<= 1;
        s_touch_scl_write(1u);
        s_touch_delay_us(2u);
        if (s_touch_sda_read() == GPIO_PIN_SET)
        {
            data |= 0x01u;
        }
        s_touch_scl_write(0u);
        s_touch_delay_us(2u);
    }

    if (ack != 0u)
    {
        s_touch_ack();
    }
    else
    {
        s_touch_nack();
    }

    return data;
}
