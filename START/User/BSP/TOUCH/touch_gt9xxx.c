/**
 * @file    touch_gt9xxx.c
 * @brief   GT9XXX 系列电容触摸控制器驱动实现
 * @details
 *   ## 支持芯片型号
 *   GT911（5点）、GT9147（5点）、GT1151（5点）、GT1158（5点）、
 *   GT9271（10点）、GT967（5点）。
 *
 *   ## 通信协议
 *   标准 I²C，7位地址 0x14（写0x28, 读0x29）。
 *   使用 Touch_I2C 软件模拟 I²C 总线（touch_i2c.c）。
 *   GT9XXX 寄存器为16位地址，通信格式：
 *     写：START + 0x28 + 寄存器高字节 + 寄存器低字节 + 数据字节... + STOP
 *     读：先写地址（同写操作前3字节）+ STOP，再 START + 0x29 + 读数据... + STOP
 *
 *   ## 关键寄存器
 *   - 0x8047 (GT9XXX_CTRL_REG)  : 控制寄存器（0x02=软件复位, 0x00=开始上报）
 *   - 0x8140 (GT9XXX_CFGS_REG)  : 配置版本寄存器
 *   - 0x8144 (GT9XXX_CHECKSUM)  : 配置校验和
 *   - 0x8145 (GT9XXX_FRESH_REG) : 配置刷新标志
 *   - 0x814E (GT9XXX_GSTID_REG) : 状态寄存器（bit[7]=Buffer状态, bit[3:0]=点数）
 *   - 0x8150~0x8198             : 10个触摸点数据，每点8字节（前4字节：XL/XH/YL/YH）
 *   - 0x8140 (GT9XXX_PID_REG)   : 产品 ID（4字节 ASCII 字符串，如"911\0"）
 *
 *   ## 坐标映射
 *   硬件报告的原始坐标与屏幕坐标系可能不一致（取决于面板安装方向），
 *   s_gt_map_coords 通过尝试多种变换（旋转/镜像）查找第一个落在屏幕范围内的坐标。
 *   [注意] 坐标映射策略依赖 lcddev.id 和 lcddev.dir，若屏幕方向改变应重新校验。
 *
 *   ## 资源占用
 *   - GPIO: RST(PH11 输出PP), INT(PB5 输入上拉)
 *   - 无 DMA，无中断（轮询扫描）
 *   [改进] 可通过 INT 引脚中断触发扫描代替轮询，降低 CPU 占用。
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
 * @param   reg 16位寄存器地址（GT9XXX 使用16位寄存器地址）
 * @param   buf 待写入数据缓冲区指针
 * @param   len 待写入数据字节数
 * @return  0: 成功, 1: 失败（I2C无应答）
 * @details
 *   I2C 时序：START → 器件写地址(0x28) → 寄存器高字节 → 寄存器低字节
 *              → 数据[0..len-1] → STOP
 *   任意阶段 WaitAck 失败则立即发 STOP 并返回 1。
 */
static uint8_t s_gt_write_reg(uint16_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_GT9XXX_CMD_WR);   /* 发送写地址 0x28 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;   /* 器件无应答，可能未初始化或总线错误 */
    }
    Touch_I2C_SendByte((uint8_t)(reg >> 8));    /* 寄存器地址高字节 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg & 0xFFu)); /* 寄存器地址低字节 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    for (i = 0u; i < len; i++)   /* 依次写入数据（支持自动地址递增）*/
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
 * @return  0: 成功, 1: 失败（I2C无应答）
 * @details
 *   I2C 读操作分两阶段：
 *   阶段1：START → 写地址(0x28) → 寄存器高字节 → 寄存器低字节 → STOP
 *   阶段2：START → 读地址(0x29) → 读len字节（最后一字节发NACK）→ STOP
 *   [注意] GT9XXX 不支持 Repeated START（Sr），必须发完整 STOP 后再重新 START。
 */
static uint8_t s_gt_read_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    /* 阶段1：写入目标寄存器地址 */
    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_GT9XXX_CMD_WR);   /* 器件写地址 0x28 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg >> 8));    /* 寄存器高字节 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }
    Touch_I2C_SendByte((uint8_t)(reg & 0xFFu)); /* 寄存器低字节 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    /* 阶段2：重新发 START + 读地址，读取数据 */
    Touch_I2C_Start();
    Touch_I2C_SendByte(TOUCH_GT9XXX_CMD_RD);   /* 器件读地址 0x29 */
    if (Touch_I2C_WaitAck() != 0u)
    {
        Touch_I2C_Stop();
        return 1u;
    }

    /* (i+1u) < len 时发 ACK（继续读）；最后一字节发 NACK（通知停止）*/
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
 * @details
 *   GT9XXX 上报的原始坐标以触摸 IC 安装方向为基准（通常为竖屏），
 *   需根据当前 LCD 面板 ID 和显示方向进行旋转/镜像变换。
 *
 *   变换策略（根据 lcddev.id 和 lcddev.dir）：
 *   - 特定面板（5510/5310/7796/7789/9806）+ 横屏：X' = width-1-rawY, Y' = rawX
 *   - 特定面板（5510/5310/7796/7789/9806）+ 竖屏：X' = rawX, Y' = rawY（直通）
 *   - 其他面板 + 横屏：X' = rawX, Y' = rawY（直通）
 *   - 其他面板 + 竖屏：X' = width-1-rawY, Y' = rawX
 *   之后有3个 fallback 尝试（依次用另外3种变换），确保即使首选变换超出范围
 *   也能找到有效坐标。
 *
 *   [注意] 逻辑基于 lcddev 全局结构中存储的面板 ID 和显示方向，
 *          若面板安装方向或 ID 映射有误，触摸坐标将偏移。
 *   [改进] 坐标映射逻辑嵌入多次 fallback 尝试，代码难以直观理解；
 *          建议改为矩阵旋转变换（2×2 旋转矩阵）。
 * @param   buf      4字节原始数据：[0]=XL, [1]=XH, [2]=YL, [3]=YH
 * @param   x        输出映射后的 X 坐标
 * @param   y        输出映射后的 Y 坐标
 * @param   raw_x_out 输出原始 X（可为 NULL）
 * @param   raw_y_out 输出原始 Y（可为 NULL）
 * @return  1: 映射成功（在屏幕范围内），0: 所有变换均超出屏幕范围
 */
static uint8_t s_gt_map_coords(const uint8_t *buf,
                               uint16_t *x,
                               uint16_t *y,
                               uint16_t *raw_x_out,
                               uint16_t *raw_y_out)
{
    /* 从4字节数据组合16位原始坐标（小端字节序）*/
    uint16_t raw_x = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);  /* XH:XL */
    uint16_t raw_y = (uint16_t)(((uint16_t)buf[3] << 8) | buf[2]);  /* YH:YL */
    uint8_t landscape = (uint8_t)(lcddev.dir & 0x01u);   /* 0=竖屏, 1=横屏 */
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

    /* Fallbacks: 尝试其余3种变换（按优先级），直到找到屏幕范围内的坐标 */
    s_gt_try_map_candidate((uint16_t)((raw_y < lcddev.width) ? (lcddev.width - 1u - raw_y) : lcddev.width),
                           raw_x,
                           &map_x,
                           &map_y,
                           &valid);                          /* 变换2: X'=W-1-rawY, Y'=rawX */
    s_gt_try_map_candidate(raw_x, raw_y, &map_x, &map_y, &valid); /* 变换3: 直通 */
    s_gt_try_map_candidate(raw_y,
                           (uint16_t)((raw_x < lcddev.height) ? (lcddev.height - 1u - raw_x) : lcddev.height),
                           &map_x,
                           &map_y,
                           &valid);                          /* 变换4: X'=rawY, Y'=H-1-rawX */
    s_gt_try_map_candidate((uint16_t)((raw_x < lcddev.width) ? (lcddev.width - 1u - raw_x) : lcddev.width),
                           (uint16_t)((raw_y < lcddev.height) ? (lcddev.height - 1u - raw_y) : lcddev.height),
                           &map_x,
                           &map_y,
                           &valid);                          /* 变换5: X'=W-1-rawX, Y'=H-1-rawY（180°翻转）*/

    *x = map_x;
    *y = map_y;
    return valid;
}

/**
 * @brief   初始化 GT9XXX 触摸控制器
 * @details
 *   初始化步骤：
 *   1. 使能 GPIOB/GPIOH 时钟，初始化 Touch_I2C 总线
 *   2. 配置 RST（PH11，PP输出上拉）和 INT（PB5，输入上拉）GPIO
 *   3. 硬件复位：RST拉低30ms → RST拉高30ms（内部 POR 稳定）
 *   4. INT 重配为无上拉浮空输入（若保持上拉可能影响触摸原点检测）
 *   5. 等待100ms（GT9XXX 启动主程序）
 *   6. 读取 PID 寄存器（4字节 ASCII），校验是否已知型号
 *   7. 根据 PID 确定最大触摸点数（9271 = 10点，其余 = 5点）
 *   8. 写 CTRL_REG = 0x02（软件复位），10ms后写 0x00（开始上报）
 * @return  0: 成功, 1: I2C通信失败
 */
uint8_t Touch_GT9XXX_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    uint8_t id_buf[5] = {0};     /* 5字节：4字节 PID ASCII + null 终结符 */
    uint8_t ctrl = 0x02u;        /* 控制命令：0x02 = 软件复位 */

    __HAL_RCC_GPIOB_CLK_ENABLE();   /* INT 引脚所在端口 */
    __HAL_RCC_GPIOH_CLK_ENABLE();   /* RST 引脚所在端口 */
    Touch_I2C_Init();               /* 初始化软件 I2C（GPIO + 发一次 STOP）*/

    /* 配置 RST 引脚：推挽输出，上拉 */
    gpio_init.Pin = TOUCH_GT9XXX_RST_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(TOUCH_GT9XXX_RST_GPIO_PORT, &gpio_init);

    /* 配置 INT 引脚：输入上拉（初始化期间避免误触发）*/
    gpio_init.Pin = TOUCH_GT9XXX_INT_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(TOUCH_GT9XXX_INT_GPIO_PORT, &gpio_init);

    /* 硬件复位序列：RST低→RST高，等待 POR 完成 */
    s_gt_rst_write(0u);
    HAL_Delay(30u);    /* 保持低电平复位 ≥10ms（GT9XXX datasheet 要求）*/
    s_gt_rst_write(1u);
    HAL_Delay(30u);    /* 等待内部寄存器恢复默认值 */

    /* 释放 INT 引脚上拉，改为浮空输入（避免影响后续触摸检测）*/
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TOUCH_GT9XXX_INT_GPIO_PORT, &gpio_init);
    HAL_Delay(100u);   /* 等待 GT9XXX 主程序启动完成（内部约需 50ms）*/

    if (s_gt_read_reg(TOUCH_GT9XXX_PID_REG, id_buf, 4u) != 0u)
    {
        return 1u;   /* PID 读取失败：I2C 总线异常或芯片未响应 */
    }
    id_buf[4] = 0u;   /* 补充 null 终结符，确保 strcmp 安全 */
    printf("Touch GT PID raw:%02X %02X %02X %02X\r\n",
           id_buf[0],
           id_buf[1],
           id_buf[2],
           id_buf[3]);

    if (s_gt_valid_id((const char *)id_buf) == 0u)
    {
        printf("Touch GT PID unknown, continue probe\r\n");
        /* [改进] 未知 ID 时应返回错误，此处继续是为了兼容新版本硬件 */
    }

    /* GT9271 支持10点触摸，其他型号最多5点 */
    s_gt_max_points = (uint8_t)((strcmp((const char *)id_buf, "9271") == 0) ? 10u : 5u);

    /* 配置控制寄存器：0x02=进入软件复位状态（停止上报）*/
    if (s_gt_write_reg(TOUCH_GT9XXX_CTRL_REG, &ctrl, 1u) != 0u)
    {
        return 1u;
    }
    HAL_Delay(10u);    /* 给 IC 时间完成软件复位 */
    ctrl = 0x00u;      /* 0x00 = 退出复位，开始正常上报触摸数据 */
    if (s_gt_write_reg(TOUCH_GT9XXX_CTRL_REG, &ctrl, 1u) != 0u)
    {
        return 1u;
    }

    return 0u;
}

/**
 * @brief   扫描 GT9XXX 触摸数据（轮询模式）
 * @details
 *   扫描流程：
 *   1. 读 GSTID 寄存器（0x814E）获取触摸状态
 *      - bit[7]=1 表示 Buffer 已准备好（有新数据）
 *      - bit[3:0] 为有效触摸点数（0~10）
 *   2. 若 Buffer Ready 且点数有效，清零 GSTID（写0，通知 IC 可更新下一帧）
 *   3. 依次读取每个触摸点寄存器（8字节/点，前4字节为坐标），调用 s_gt_map_coords 映射
 *   4. 将有效坐标填入 state->x[]/y[]，更新 active_mask 和 count
 *   [注意] 函数内 buf[4] 是 VLA 行为的局部数组，
 *          ARM Compiler 5 不支持 C99 VLA，此处 4 为常量故无问题。
 *   [改进] 当前为轮询，建议改为 INT 引脚中断驱动，降低 CPU 占用。
 * @param   state 指向 Touch_State_t 结构体，不可为 NULL
 * @return  1: 有触摸按下（state->pressed=1）, 0: 无触摸
 */
uint8_t Touch_GT9XXX_Scan(Touch_State_t *state)
{
    /* 每个触摸点寄存器起始地址（每个点占8字节，间隔8）*/
    static const uint16_t reg_table[TOUCH_MAX_POINTS] = {
        0x8150u, 0x8158u, 0x8160u, 0x8168u, 0x8170u,  /* 点0~4 */
        0x8178u, 0x8180u, 0x8188u, 0x8190u, 0x8198u    /* 点5~9 */
    };
    static uint8_t s_gt_last_pressed = 0u;   /* 上一次扫描的按下状态（检测 press 事件）*/
    uint8_t status = 0u;    /* GSTID 寄存器原始值 */
    uint8_t clear = 0u;     /* 清零 GSTID 用的0值 */
    uint8_t point_num;      /* 原始点数（bit[3:0]）*/
    uint8_t i;
    uint8_t valid_count = 0u;   /* 成功映射到屏幕范围内的触摸点数 */
    uint16_t raw_x0 = 0u;
    uint16_t raw_y0 = 0u;
    uint16_t map_x0 = 0u;
    uint16_t map_y0 = 0u;
    uint8_t got_first = 0u;  /* 是否已捕获到第一个点的坐标（用于调试打印）*/

    if ((state == NULL) || (s_gt_read_reg(TOUCH_GT9XXX_GSTID_REG, &status, 1u) != 0u))
    {
        return 0u;   /* 参数为空或 I2C 读取失败 */
    }

    s_gt_dbg_scan_count++;       /* 累计扫描计数（调试用）*/
    s_gt_dbg_status = status;    /* 记录原始状态寄存器（调试用）*/

    point_num = (uint8_t)(status & 0x0Fu);  /* 低4位=触摸点数 */
    s_gt_dbg_point_num = point_num;
    if (((status & 0x80u) != 0u) && (point_num <= s_gt_max_points))
    {
        /* Buffer Ready位有效 + 点数合法 → 清零 GSTID，通知 IC 可更新缓冲区 */
        (void)s_gt_write_reg(TOUCH_GT9XXX_GSTID_REG, &clear, 1u);
    }

    if (((status & 0x80u) == 0u) || (point_num == 0u) || (point_num > s_gt_max_points))
    {
        /* Buffer未就绪 or 无触摸 or 点数异常 → 返回无触摸状态 */
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
