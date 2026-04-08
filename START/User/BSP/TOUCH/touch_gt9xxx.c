/**
 * @file    touch_gt9xxx.c
 * @brief   GT9XXX 系列电容触摸控制器驱动实现
 * @details 通过软件模拟 I2C 与 GT9XXX 触摸 IC 通信，
 *          实现初始化、寄存器读写、多点触摸扫描及坐标映射。
 *          支持 GT911/GT9147/GT1151/GT1158/GT9271/GT967 等型号。
 *          适用于 STM32H743 平台。
 */

#include "touch_gt9xxx.h"

#include "touch_i2c.h"
#include "LCD/lcd.h"

#include <stdio.h>
#include <string.h>

static uint8_t  s_gt_max_points     = 5u;  /**< 当前控制器支持的最大触摸点数 */
static uint8_t  s_gt_dbg_status      = 0u;  /**< 调试用: 最近一次状态寄存器值 */
static uint8_t  s_gt_dbg_point_num   = 0u;  /**< 调试用: 最近一次触摸点数 */
static uint8_t  s_gt_dbg_valid_count = 0u;  /**< 调试用: 最近一次有效触摸点数 */
static uint16_t s_gt_dbg_raw_x0      = 0u;  /**< 调试用: 第一个触摸点原始 X */
static uint16_t s_gt_dbg_raw_y0      = 0u;  /**< 调试用: 第一个触摸点原始 Y */
static uint16_t s_gt_dbg_map_x0      = 0u;  /**< 调试用: 第一个触摸点映射 X */
static uint16_t s_gt_dbg_map_y0      = 0u;  /**< 调试用: 第一个触摸点映射 Y */
static uint32_t s_gt_dbg_scan_count  = 0u;  /**< 调试用: 累计扫描次数 */

/**
 * @brief   设置 GT9XXX 复位引脚电平
 * @param   level 电平值 (0=低电平, 非0=高电平)
 */
static void s_gt_rst_write(uint8_t level)
{
    HAL_GPIO_WritePin(TOUCH_GT9XXX_RST_GPIO_PORT,
                      TOUCH_GT9XXX_RST_GPIO_PIN,
                      (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief   通过 I2C 写入 GT9XXX 寄存器
 * @param   reg 16位寄存器地址
 * @param   buf 待写入数据缓冲区指针
 * @param   len 待写入数据字节数
 * @return  0: 成功, 1: 失败
 */
static uint8_t s_gt_write_reg(uint16_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_GT9XXX_CMD_WR);
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg >> 8));
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg & 0xFFu));
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    for (i = 0u; i < len; i++)
    {
        Touch_I2C_SendByte(buf[i]);
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
 * @brief   通过 I2C 读取 GT9XXX 寄存器
 * @param   reg 16位寄存器地址
 * @param   buf 读取数据输出缓冲区指针
 * @param   len 待读取字节数
 * @return  0: 成功, 1: 失败
 */
static uint8_t s_gt_read_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_GT9XXX_CMD_WR);
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg >> 8));
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg & 0xFFu));
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_GT9XXX_CMD_RD);
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    for (i = 0u; i < len; i++)
    {
        buf[i] = Touch_I2C_ReadByte((i + 1u) < len);
    }

    Touch_I2C_Stop();
    return 0u;
}

/**
 * @brief   检查产品 ID 是否为已知的 GT 系列控制器
 * @param   id 产品 ID 字符串 (ASCII)
 * @return  1: 有效, 0: 未知 ID
 */
static uint8_t s_gt_valid_id(const char *id)
{
    return (uint8_t)((strcmp(id, "911") == 0) ||
                     (strcmp(id, "9147") == 0) ||
                     (strcmp(id, "1151") == 0) ||
                     (strcmp(id, "1158") == 0) ||
                     (strcmp(id, "9271") == 0) ||
                     (strcmp(id, "967") == 0));
}

/**
 * @brief   检查坐标是否在屏幕范围内
 * @param   x X 坐标
 * @param   y Y 坐标
 * @return  1: 在范围内, 0: 超出范围
 */
static uint8_t s_gt_coord_in_range(uint16_t x, uint16_t y)
{
    return (uint8_t)((x < lcddev.width) && (y < lcddev.height));
}

/**
 * @brief   尝试一组候选坐标映射，若在屏幕范围内则接受
 * @param   cand_x 候选 X 坐标
 * @param   cand_y 候选 Y 坐标
 * @param   x      输出映射后的 X 坐标
 * @param   y      输出映射后的 Y 坐标
 * @param   valid  输出有效标志，已为1时不再更新
 */
static void s_gt_try_map_candidate(uint16_t cand_x,
                                   uint16_t cand_y,
                                   uint16_t *x,
                                   uint16_t *y,
                                   uint8_t *valid)
{
    if ((valid == NULL) || (*valid != 0u))
    {
        return;
    }

    if (s_gt_coord_in_range(cand_x, cand_y) != 0u)
    {
        *x = cand_x;
        *y = cand_y;
        *valid = 1u;
    }
}

/**
 * @brief   将触摸 IC 报告的原始坐标映射为屏幕坐标
 * @details 根据 LCD ID 和屏幕方向尝试多种坐标变换方式
 * @param   buf      4 字节原始触摸数据 (XL, XH, YL, YH)
 * @param   x        输出映射后的 X 坐标
 * @param   y        输出映射后的 Y 坐标
 * @param   raw_x_out 输出原始 X 坐标 (可为 NULL)
 * @param   raw_y_out 输出原始 Y 坐标 (可为 NULL)
 * @return  1: 映射成功, 0: 坐标超出屏幕范围
 */
static uint8_t s_gt_map_coords(const uint8_t *buf,
                               uint16_t *x,
                               uint16_t *y,
                               uint16_t *raw_x_out,
                               uint16_t *raw_y_out)
{
    uint16_t raw_x = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);
    uint16_t raw_y = (uint16_t)(((uint16_t)buf[3] << 8) | buf[2]);
    uint8_t landscape = (uint8_t)(lcddev.dir & 0x01u);
    uint16_t map_x = 0u;
    uint16_t map_y = 0u;
    uint8_t valid = 0u;

    if (raw_x_out != NULL)
    {
        *raw_x_out = raw_x;
    }
    if (raw_y_out != NULL)
    {
        *raw_y_out = raw_y;
    }

    if ((lcddev.id == 0x5510u) ||
        (lcddev.id == 0x5310u) ||
        (lcddev.id == 0x7796u) ||
        (lcddev.id == 0x7789u) ||
        (lcddev.id == 0x9806u))
    {
        if (landscape != 0u)
        {
            s_gt_try_map_candidate((uint16_t)((raw_y < lcddev.width) ? (lcddev.width - 1u - raw_y) : lcddev.width),
                                   raw_x,
                                   &map_x,
                                   &map_y,
                                   &valid);
        }
        else
        {
            s_gt_try_map_candidate(raw_x, raw_y, &map_x, &map_y, &valid);
        }
    }
    else
    {
        if (landscape != 0u)
        {
            s_gt_try_map_candidate(raw_x, raw_y, &map_x, &map_y, &valid);
        }
        else
        {
            s_gt_try_map_candidate((uint16_t)((raw_y < lcddev.width) ? (lcddev.width - 1u - raw_y) : lcddev.width),
                                   raw_x,
                                   &map_x,
                                   &map_y,
                                   &valid);
        }
    }

    /* Fallbacks for panels that report portrait-oriented raw coordinates. */
    s_gt_try_map_candidate((uint16_t)((raw_y < lcddev.width) ? (lcddev.width - 1u - raw_y) : lcddev.width),
                           raw_x,
                           &map_x,
                           &map_y,
                           &valid);
    s_gt_try_map_candidate(raw_x, raw_y, &map_x, &map_y, &valid);
    s_gt_try_map_candidate(raw_y,
                           (uint16_t)((raw_x < lcddev.height) ? (lcddev.height - 1u - raw_x) : lcddev.height),
                           &map_x,
                           &map_y,
                           &valid);
    s_gt_try_map_candidate((uint16_t)((raw_x < lcddev.width) ? (lcddev.width - 1u - raw_x) : lcddev.width),
                           (uint16_t)((raw_y < lcddev.height) ? (lcddev.height - 1u - raw_y) : lcddev.height),
                           &map_x,
                           &map_y,
                           &valid);

    *x = map_x;
    *y = map_y;
    return valid;
}

/**
 * @brief   初始化 GT9XXX 触摸控制器
 * @details 配置 GPIO，复位芯片，读取产品 ID 并写入控制命令
 * @return  0: 成功, 1: 失败
 */
uint8_t Touch_GT9XXX_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    uint8_t id_buf[5] = {0};
    uint8_t ctrl = 0x02u;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    Touch_I2C_Init();

    gpio_init.Pin = TOUCH_GT9XXX_RST_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(TOUCH_GT9XXX_RST_GPIO_PORT, &gpio_init);

    gpio_init.Pin = TOUCH_GT9XXX_INT_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(TOUCH_GT9XXX_INT_GPIO_PORT, &gpio_init);

    s_gt_rst_write(0u);
    HAL_Delay(30u);
    s_gt_rst_write(1u);
    HAL_Delay(30u);

    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TOUCH_GT9XXX_INT_GPIO_PORT, &gpio_init);
    HAL_Delay(100u);

    if (s_gt_read_reg(TOUCH_GT9XXX_PID_REG, id_buf, 4u) != 0u)
    {
        return 1u;
    }
    id_buf[4] = 0u;
    printf("Touch GT PID raw:%02X %02X %02X %02X\r\n",
           id_buf[0],
           id_buf[1],
           id_buf[2],
           id_buf[3]);

    if (s_gt_valid_id((const char *)id_buf) == 0u)
    {
        printf("Touch GT PID unknown, continue probe\r\n");
    }

    s_gt_max_points = (uint8_t)((strcmp((const char *)id_buf, "9271") == 0) ? 10u : 5u);

    if (s_gt_write_reg(TOUCH_GT9XXX_CTRL_REG, &ctrl, 1u) != 0u)
    {
        return 1u;
    }
    HAL_Delay(10u);
    ctrl = 0x00u;
    if (s_gt_write_reg(TOUCH_GT9XXX_CTRL_REG, &ctrl, 1u) != 0u)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief   扫描 GT9XXX 触摸数据
 * @details 读取状态寄存器，遍历各触摸点寄存器并映射坐标
 * @param   state 指向触摸状态结构体的指针，用于输出触摸结果
 * @return  1: 有触摸按下, 0: 无触摸
 */
uint8_t Touch_GT9XXX_Scan(Touch_State_t *state)
{
    static const uint16_t reg_table[TOUCH_MAX_POINTS] = {
        0x8150u, 0x8158u, 0x8160u, 0x8168u, 0x8170u,
        0x8178u, 0x8180u, 0x8188u, 0x8190u, 0x8198u
    };
    static uint8_t s_gt_last_pressed = 0u;
    uint8_t status = 0u;
    uint8_t clear = 0u;
    uint8_t point_num;
    uint8_t i;
    uint8_t valid_count = 0u;
    uint16_t raw_x0 = 0u;
    uint16_t raw_y0 = 0u;
    uint16_t map_x0 = 0u;
    uint16_t map_y0 = 0u;
    uint8_t got_first = 0u;

    if ((state == NULL) || (s_gt_read_reg(TOUCH_GT9XXX_GSTID_REG, &status, 1u) != 0u))
    {
        return 0u;
    }

    s_gt_dbg_scan_count++;
    s_gt_dbg_status = status;

    point_num = (uint8_t)(status & 0x0Fu);
    s_gt_dbg_point_num = point_num;
    if (((status & 0x80u) != 0u) && (point_num <= s_gt_max_points))
    {
        (void)s_gt_write_reg(TOUCH_GT9XXX_GSTID_REG, &clear, 1u);
    }

    if (((status & 0x80u) == 0u) || (point_num == 0u) || (point_num > s_gt_max_points))
    {
        state->pressed = 0u;
        state->count = 0u;
        state->active_mask = 0u;
        s_gt_dbg_valid_count = 0u;
        return 0u;
    }

    for (i = 0u; (i < point_num) && (i < TOUCH_MAX_POINTS); i++)
    {
        uint8_t buf[4];
        uint16_t x;
        uint16_t y;
        uint16_t raw_x;
        uint16_t raw_y;

        if (s_gt_read_reg(reg_table[i], buf, 4u) != 0u)
        {
            continue;
        }

        if (s_gt_map_coords(buf, &x, &y, &raw_x, &raw_y) == 0u)
        {
            if (got_first == 0u)
            {
                raw_x0 = raw_x;
                raw_y0 = raw_y;
                map_x0 = x;
                map_y0 = y;
                got_first = 1u;
            }
            continue;
        }

        if (got_first == 0u)
        {
            raw_x0 = raw_x;
            raw_y0 = raw_y;
            map_x0 = x;
            map_y0 = y;
            got_first = 1u;
        }

        state->x[valid_count] = x;
        state->y[valid_count] = y;
        state->active_mask |= (uint16_t)(1u << valid_count);
        valid_count++;
    }

    state->count = valid_count;
    state->pressed = (uint8_t)(valid_count != 0u);
    s_gt_dbg_valid_count = valid_count;
    if (valid_count == 0u)
    {
        state->active_mask = 0u;
    }

    if ((state->pressed != 0u) && (s_gt_last_pressed == 0u) && (got_first != 0u))
    {
        s_gt_dbg_raw_x0 = raw_x0;
        s_gt_dbg_raw_y0 = raw_y0;
        s_gt_dbg_map_x0 = map_x0;
        s_gt_dbg_map_y0 = map_y0;
        printf("Touch GT press raw=(%u,%u) map=(%u,%u) n=%u\r\n",
               raw_x0,
               raw_y0,
               map_x0,
               map_y0,
               valid_count);
    }
    else if ((point_num != 0u) && (valid_count == 0u) && (got_first != 0u))
    {
        s_gt_dbg_raw_x0 = raw_x0;
        s_gt_dbg_raw_y0 = raw_y0;
        s_gt_dbg_map_x0 = map_x0;
        s_gt_dbg_map_y0 = map_y0;
        printf("Touch GT raw only raw=(%u,%u) map=(%u,%u) status=0x%02X\r\n",
               raw_x0,
               raw_y0,
               map_x0,
               map_y0,
               status);
    }

    s_gt_last_pressed = state->pressed;
    return state->pressed;
}

/**
 * @brief   获取当前控制器支持的最大触摸点数
 * @return  最大触摸点数
 */
uint8_t Touch_GT9XXX_GetMaxPoints(void)
{
    return s_gt_max_points;
}

/**
 * @brief   获取最近一次状态寄存器值 (调试用)
 * @return  GSTID 寄存器原始值
 */
uint8_t Touch_GT9XXX_DebugStatus(void)
{
    return s_gt_dbg_status;
}

/**
 * @brief   获取最近一次触摸点数 (调试用)
 * @return  触摸点数
 */
uint8_t Touch_GT9XXX_DebugPointNum(void)
{
    return s_gt_dbg_point_num;
}

/**
 * @brief   获取最近一次有效触摸点数 (调试用)
 * @return  有效触摸点数
 */
uint8_t Touch_GT9XXX_DebugValidCount(void)
{
    return s_gt_dbg_valid_count;
}

/**
 * @brief   获取最近一次按下的原始 X 坐标 (调试用)
 * @return  原始 X 值
 */
uint16_t Touch_GT9XXX_DebugRawX0(void)
{
    return s_gt_dbg_raw_x0;
}

/**
 * @brief   获取最近一次按下的原始 Y 坐标 (调试用)
 * @return  原始 Y 值
 */
uint16_t Touch_GT9XXX_DebugRawY0(void)
{
    return s_gt_dbg_raw_y0;
}

/**
 * @brief   获取最近一次按下的映射 X 坐标 (调试用)
 * @return  映射后 X 值
 */
uint16_t Touch_GT9XXX_DebugMapX0(void)
{
    return s_gt_dbg_map_x0;
}

/**
 * @brief   获取最近一次按下的映射 Y 坐标 (调试用)
 * @return  映射后 Y 值
 */
uint16_t Touch_GT9XXX_DebugMapY0(void)
{
    return s_gt_dbg_map_y0;
}

/**
 * @brief   获取累计扫描次数 (调试用)
 * @return  扫描调用总次数
 */
uint32_t Touch_GT9XXX_DebugScanCount(void)
{
    return s_gt_dbg_scan_count;
}
