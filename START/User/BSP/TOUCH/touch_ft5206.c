/**
 * @file    touch_ft5206.c
 * @brief   FT5206 电容触摸控制器驱动实现
 * @details 通过软件模拟 I2C 与 FT5206 触摸 IC 通信，
 *          实现初始化、寄存器读写和多点触摸扫描。
 *
 *          通信模式：
 *          - 写寄存器：START -> 0x70 -> REG -> DATA... -> STOP
 *          - 读寄存器：START -> 0x70 -> REG -> RESTART -> 0x71 -> DATA... -> STOP
 *
 *          坐标读取：每个触点 4 字节，X/Y 坐标各 12 bit（高4位在首字节低半字节）。
 *          本驱动按 lcddev.dir 做横/竖屏映射，输出统一逻辑坐标给上层。
 *
 * @note    [改进] 当前为轮询读取（Touch_Scan 主动拉取）；
 *          可利用 INT 引脚做中断触发，降低空闲总线占用与扫描延迟。
 *          适用于 STM32H743 平台。
 */

#include "touch_ft5206.h"

#include "touch_i2c.h"
#include "LCD/lcd.h"

#include <stdio.h>

/**
 * @brief   设置 FT5206 复位引脚电平
 * @param   level 电平值 (0=低电平, 非0=高电平)
 */
static void s_ft_rst_write(uint8_t level)
{
    HAL_GPIO_WritePin(TOUCH_FT5206_RST_GPIO_PORT,
                      TOUCH_FT5206_RST_GPIO_PIN,
                      (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief   通过 I2C 写入 FT5206 寄存器
 * @param   reg 寄存器地址
 * @param   buf 待写入数据缓冲区指针
 * @param   len 待写入数据字节数
 * @return  0: 成功, 1: 失败
 */
static uint8_t s_ft_write_reg(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    Touch_I2C_Start();                        /* 起始信号 */
    Touch_I2C_SendByte(TOUCH_FT5206_CMD_WR); /* 发送写地址 0x70 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte(reg);                  /* 发送寄存器地址 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    for (i = 0u; i < len; i++)
    {
        Touch_I2C_SendByte(buf[i]);           /* 连续写入数据字节 */
        if (Touch_I2C_WaitAck() != 0u)
        {
            Touch_I2C_Stop();
            return 1u;
        }
    }

    Touch_I2C_Stop();
    return 0u;
}

/**
 * @brief   通过 I2C 读取 FT5206 寄存器
 * @param   reg 寄存器地址
 * @param   buf 读取数据输出缓冲区指针
 * @param   len 待读取字节数
 * @return  0: 成功, 1: 失败
 */
static uint8_t s_ft_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    /* 阶段1：写寄存器地址（随机读前导）*/
    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_FT5206_CMD_WR);
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte(reg);
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    /* 阶段2：重复起始后切换到读地址，连续读取 len 字节。 */
    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_FT5206_CMD_RD);
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    for (i = 0u; i < len; i++)
    {
        /* 非最后一字节回 ACK，最后一字节回 NACK 结束读取。 */
        buf[i] = Touch_I2C_ReadByte((i + 1u) < len);
    }

    Touch_I2C_Stop();
    return 0u;
}

/**
 * @brief   初始化 FT5206 触摸控制器
 * @details 配置 GPIO，复位芯片，写入默认参数并校验固件版本
 * @return  0: 成功, 1: 失败
 */
uint8_t Touch_FT5206_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    uint8_t temp[2] = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    Touch_I2C_Init();

    gpio_init.Pin = TOUCH_FT5206_RST_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(TOUCH_FT5206_RST_GPIO_PORT, &gpio_init);

    gpio_init.Pin = TOUCH_FT5206_INT_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(TOUCH_FT5206_INT_GPIO_PORT, &gpio_init);

    /* 硬复位时序：RST 低脉冲后拉高，等待内部状态机稳定。 */
    s_ft_rst_write(0u);
    HAL_Delay(20u);
    s_ft_rst_write(1u);
    HAL_Delay(50u);

    temp[0] = 0u;
    if (s_ft_write_reg(TOUCH_FT5206_DEVIDE_MODE, temp, 1u) != 0u)
    {
        return 1u;
    }
    if (s_ft_write_reg(TOUCH_FT5206_ID_G_MODE, temp, 1u) != 0u)
    {
        return 1u;
    }
    temp[0] = 22u; /* 触摸阈值：数值越大越不敏感，22 为经验值 */
    if (s_ft_write_reg(TOUCH_FT5206_ID_G_THGROUP, temp, 1u) != 0u)
    {
        return 1u;
    }
    temp[0] = 12u; /* Active 周期：控制扫描速率与功耗折中 */
    if (s_ft_write_reg(TOUCH_FT5206_ID_G_PERIODACTIVE, temp, 1u) != 0u)
    {
        return 1u;
    }

    if (s_ft_read_reg(TOUCH_FT5206_ID_G_LIB_VERSION, temp, 2u) != 0u)
    {
        return 1u;
    }

    /* 固件版本白名单校验，过滤异常器件或总线误读。 */
    if ((((temp[0] == 0x30u) && (temp[1] == 0x03u)) ||
         ((temp[0] == 0x00u) && (temp[1] == 0x01u)) ||
         ((temp[0] == 0x00u) && (temp[1] == 0x02u)) ||
         ((temp[0] == 0x00u) && (temp[1] == 0x00u))) == 0u)
    {
        /* [改进] 白名单策略较保守，可考虑允许更多版本并输出告警而非直接失败。 */
        return 1u;
    }

    printf("Touch FT ID:0x%02X%02X\r\n", temp[0], temp[1]);
    return 0u;
}

/**
 * @brief   扫描 FT5206 触摸数据
 * @details 读取触摸点数和各点坐标，根据屏幕方向映射坐标
 * @param   state 指向触摸状态结构体的指针，用于输出触摸结果
 * @return  1: 有触摸按下, 0: 无触摸
 */
uint8_t Touch_FT5206_Scan(Touch_State_t *state)
{
    /* 各触点寄存器首地址：P1=0x03, P2=0x09 ... 每个触点占 6 字节，
     * 本驱动只读取坐标相关前4字节（事件+XH+XL+YH/YL布局）。 */
    static const uint8_t reg_table[5] = {0x03u, 0x09u, 0x0Fu, 0x15u, 0x1Bu};
    uint8_t finger_count = 0u;
    uint8_t i;
    uint8_t valid_count = 0u;
    uint8_t landscape = (uint8_t)(lcddev.dir & 0x01u);

    if ((state == NULL) || (s_ft_read_reg(TOUCH_FT5206_REG_NUM_FINGER, &finger_count, 1u) != 0u))
    {
        return 0u;
    }

    finger_count &= 0x0Fu; /* bit[3:0] = 当前触点个数 */
    if ((finger_count == 0u) || (finger_count > 5u))
    {
        state->pressed = 0u;
        state->count = 0u;
        state->active_mask = 0u;
        return 0u;
    }

    for (i = 0u; i < finger_count; i++)
    {
        uint8_t buf[4];
        uint16_t x;
        uint16_t y;

        if (s_ft_read_reg(reg_table[i], buf, 4u) != 0u)
        {
            continue;
        }
        /* buf[0] bit[7:6] 为事件标志，0b10 表示 Contact（接触中）。 */
        if ((buf[0] & 0xF0u) != 0x80u)
        {
            continue;
        }

        if (landscape != 0u)
        {
            /* 横屏：坐标轴按当前板级定义直接映射。 */
            y = (uint16_t)(((uint16_t)(buf[0] & 0x0Fu) << 8) | buf[1]);
            x = (uint16_t)(((uint16_t)(buf[2] & 0x0Fu) << 8) | buf[3]);
        }
        else
        {
            /* 竖屏：X 做镜像翻转，Y 保持；与现有 UI 坐标系保持一致。 */
            x = (uint16_t)(lcddev.width - ((((uint16_t)(buf[0] & 0x0Fu) << 8) | buf[1])));
            y = (uint16_t)(((uint16_t)(buf[2] & 0x0Fu) << 8) | buf[3]);
        }

        if ((x >= lcddev.width) || (y >= lcddev.height))
        {
            continue;
        }

        state->x[valid_count] = x;
        state->y[valid_count] = y;
        state->active_mask |= (uint16_t)(1u << valid_count);
        valid_count++; /* 压缩存放有效点，跳过无效点后仍保持数组连续 */
    }

    state->count = valid_count;
    state->pressed = (uint8_t)(valid_count != 0u);
    if (valid_count == 0u)
    {
        state->active_mask = 0u;
    }
    return state->pressed;
}
