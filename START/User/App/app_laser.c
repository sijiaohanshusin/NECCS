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
    GPIO_InitTypeDef gpio = {0};

    /* 使能 GPIOB 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 配置 PB0 为推挽输出 */
    gpio.Pin   = LASER_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LASER_GPIO_PORT, &gpio);

    /* 默认关闭 */
    HAL_GPIO_WritePin(LASER_GPIO_PORT, LASER_GPIO_PIN, GPIO_PIN_RESET);
    s_laser_state = APP_LASER_OFF;
}

void App_Laser_SetState(App_LaserState_t state)
{
    if (state == APP_LASER_ON)
    {
        HAL_GPIO_WritePin(LASER_GPIO_PORT, LASER_GPIO_PIN, GPIO_PIN_SET);
        s_laser_state = APP_LASER_ON;
    }
    else
    {
        HAL_GPIO_WritePin(LASER_GPIO_PORT, LASER_GPIO_PIN, GPIO_PIN_RESET);
        s_laser_state = APP_LASER_OFF;
    }
}

void App_Laser_Toggle(void)
{
    if (s_laser_state == APP_LASER_ON)
    {
        App_Laser_SetState(APP_LASER_OFF);
    }
    else
    {
        App_Laser_SetState(APP_LASER_ON);
    }
}

App_LaserState_t App_Laser_GetState(void)
{
    return s_laser_state;
}

void App_NightMode_Enable(void)
{
    App_Runtime_DisplayCfg_t cfg;

    if (s_night_mode == APP_NIGHTMODE_ON)
    {
        return;
    }

    /* 保存当前 gamma */
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    s_saved_gamma = cfg.gamma;

    /* 夜间联动：降低 gamma，提高对比度 */
    cfg.gamma = 0.6f;
    App_RuntimeConfig_SetDisplayCfg(&cfg);

    /* 开启激光 */
    App_Laser_SetState(APP_LASER_ON);

    s_night_mode = APP_NIGHTMODE_ON;
}

void App_NightMode_Disable(void)
{
    App_Runtime_DisplayCfg_t cfg;

    if (s_night_mode == APP_NIGHTMODE_OFF)
    {
        return;
    }

    /* 恢复 gamma */
    App_RuntimeConfig_GetDisplayCfg(&cfg);
    cfg.gamma = s_saved_gamma;
    App_RuntimeConfig_SetDisplayCfg(&cfg);

    /* 关闭激光 */
    App_Laser_SetState(APP_LASER_OFF);

    s_night_mode = APP_NIGHTMODE_OFF;
}

void App_NightMode_Toggle(void)
{
    if (s_night_mode == APP_NIGHTMODE_ON)
    {
        App_NightMode_Disable();
    }
    else
    {
        App_NightMode_Enable();
    }
}

App_NightModeState_t App_NightMode_GetState(void)
{
    return s_night_mode;
}
