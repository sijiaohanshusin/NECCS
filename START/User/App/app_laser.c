/**
 * @file    app_laser.c
 * @brief   激光瞄准器与夜间模式控制实现
 */
#include "app_laser.h"

#include "main.h"
#include "app_runtime.h"

/** @brief 激光 GPIO 端口 */
#define LASER_GPIO_PORT   GPIOB
/** @brief 激光 GPIO 引脚 */
#define LASER_GPIO_PIN    GPIO_PIN_0

/** @brief 当前激光状态 */
static App_LaserState_t s_laser_state = APP_LASER_OFF;

/** @brief 当前夜间模式状态 */
static App_NightModeState_t s_night_mode = APP_NIGHTMODE_OFF;

/** @brief 夜间模式前保存的 gamma 值 */
static float s_saved_gamma = 1.0f;

void App_Laser_Init(void)
{
    GPIO_InitTypeDef gpio = {0};  /* 清零结构体，避免使用未初始化字段 */

    /* 使能 GPIOB 时钟（PB0 控制激光器，必须先开时钟） */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 配置 PB0 为推挽输出（激光器是数字开关，不需要 OD），低速足够 */
    gpio.Pin   = LASER_GPIO_PIN;                  /* 目标引脚：GPIO_PIN_0 (PB0) */
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;             /* 推挽输出模式 */
    gpio.Pull  = GPIO_NOPULL;                     /* 无上/下拉（硬件外部控制） */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;             /* 低速 (输出频率 << 1MHz，无需高速） */
    HAL_GPIO_Init(LASER_GPIO_PORT, &gpio);        /* 写入 GPIO 寄存器 */

    /* 默认关闭激光器（上电安全状态，防止无意激光照射） */
    HAL_GPIO_WritePin(LASER_GPIO_PORT, LASER_GPIO_PIN, GPIO_PIN_RESET);
    s_laser_state = APP_LASER_OFF;  /* 同步状态变量 */
}

void App_Laser_SetState(App_LaserState_t state)
{
    if (state == APP_LASER_ON)
    {
        /* 输出高电平 → 激光器控制电路导通，激光发射 */
        HAL_GPIO_WritePin(LASER_GPIO_PORT, LASER_GPIO_PIN, GPIO_PIN_SET);
        s_laser_state = APP_LASER_ON;   /* 同步内部状态标志 */
    }
    else
    {
        /* 输出低电平 → 激光器控制电路断开，激光关闭 */
        HAL_GPIO_WritePin(LASER_GPIO_PORT, LASER_GPIO_PIN, GPIO_PIN_RESET);
        s_laser_state = APP_LASER_OFF;  /* 同步内部状态标志 */
    }
}

void App_Laser_Toggle(void)
{
    /* 根据当前状态取反（ON→OFF 或 OFF→ON） */
    if (s_laser_state == APP_LASER_ON)
    {
        App_Laser_SetState(APP_LASER_OFF);  /* 当前开启 → 切换为关闭 */
    }
    else
    {
        App_Laser_SetState(APP_LASER_ON);   /* 当前关闭 → 切换为开启 */
    }
}

App_LaserState_t App_Laser_GetState(void)
{
    return s_laser_state;  /* 返回当前激光状态（OFF/ON），供 UI 显示和 CLI 查询 */
}

void App_NightMode_Enable(void)
{
    App_Runtime_DisplayCfg_t cfg;  /* 临时配置结构体，用于读取/修改显示参数 */

    if (s_night_mode == APP_NIGHTMODE_ON)
    {
        return;  /* 已在夜间模式，防止重复调用导致 gamma 被多次覆盖 */
    }

    /* 读取当前显示配置，保存 gamma 值（退出夜间模式时恢复） */
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    s_saved_gamma = cfg.gamma;  /* 缓存原始 gamma，App_NightMode_Disable 时还原 */

    /* 夜间模式联动：降低 gamma 系数提高对比度，方便暗环境识别声源位置 */
    cfg.gamma = 0.6f;  /* 降低伽马值（<1.0 使暗部更明显，增强对比度） */
    App_RuntimeConfig_SetDisplayCfg(&cfg);  /* 推送新配置，下一帧渲染立即生效 */

    /* 开启激光瞄准器（夜间对准声源方向辅助） */
    App_Laser_SetState(APP_LASER_ON);

    s_night_mode = APP_NIGHTMODE_ON;  /* 标记夜间模式已激活 */
}

void App_NightMode_Disable(void)
{
    App_Runtime_DisplayCfg_t cfg;  /* 临时配置结构体 */

    if (s_night_mode == APP_NIGHTMODE_OFF)
    {
        return;  /* 未在夜间模式，防止误恢复 gamma */
    }

    /* 恢复之前保存的 gamma 值（退出夜间模式，还原正常渲染对比度） */
    App_RuntimeConfig_GetDisplayCfg(&cfg);         /* 读取当前配置（获取其他字段保持不变） */
    cfg.gamma = s_saved_gamma;                     /* 将 gamma 还原为进入夜间模式前的值 */
    App_RuntimeConfig_SetDisplayCfg(&cfg);         /* 推送修改后的配置 */

    /* 关闭激光瞄准器 */
    App_Laser_SetState(APP_LASER_OFF);

    s_night_mode = APP_NIGHTMODE_OFF;  /* 更新夜间模式状态标志 */
}

void App_NightMode_Toggle(void)
{
    /* 根据当前夜间模式状态切换（ON→Disable 或 OFF→Enable） */
    if (s_night_mode == APP_NIGHTMODE_ON)
    {
        App_NightMode_Disable();  /* 当前为夜间模式 → 退出 */
    }
    else
    {
        App_NightMode_Enable();   /* 当前非夜间模式 → 进入 */
    }
}

App_NightModeState_t App_NightMode_GetState(void)
{
    return s_night_mode;  /* 返回当前夜间模式状态（OFF 或 ON） */
}
