#include "./BSP/COMPETITION/comp_pwm.h"

#define COMP_PWM_TIMER_CLOCK_HZ 240000000UL
#define COMP_PWM_COUNTER_HZ       1000000UL

static TIM_HandleTypeDef s_pwm_handle;
static uint32_t s_pwm_period;
static uint8_t s_pwm_running;

uint8_t comp_pwm_init(uint32_t frequency_hz, uint16_t duty_per_mille)
{
    GPIO_InitTypeDef gpio_init = {0};
    TIM_OC_InitTypeDef channel = {0};

    if ((frequency_hz < 10U) || (frequency_hz > 20000U))
    {
        return 1;
    }

    if (s_pwm_running)
    {
        comp_pwm_stop();
    }

    if (duty_per_mille > 1000U)
    {
        duty_per_mille = 1000U;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_11;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    s_pwm_period = (COMP_PWM_COUNTER_HZ / frequency_hz) - 1U;
    s_pwm_handle.Instance = TIM1;
    s_pwm_handle.Init.Prescaler = (COMP_PWM_TIMER_CLOCK_HZ / COMP_PWM_COUNTER_HZ) - 1U;
    s_pwm_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_pwm_handle.Init.Period = s_pwm_period;
    s_pwm_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_pwm_handle.Init.RepetitionCounter = 0U;
    s_pwm_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&s_pwm_handle) != HAL_OK)
    {
        return 2;
    }

    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = ((s_pwm_period + 1U) * duty_per_mille) / 1000U;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&s_pwm_handle, &channel, TIM_CHANNEL_4) != HAL_OK)
    {
        return 3;
    }

    if (HAL_TIM_PWM_Start(&s_pwm_handle, TIM_CHANNEL_4) != HAL_OK)
    {
        return 4U;
    }

    s_pwm_running = 1U;
    return 0U;
}

void comp_pwm_set_duty(uint16_t duty_per_mille)
{
    if (!s_pwm_running)
    {
        return;
    }

    if (duty_per_mille > 1000U)
    {
        duty_per_mille = 1000U;
    }

    __HAL_TIM_SET_COMPARE(&s_pwm_handle,
                          TIM_CHANNEL_4,
                          ((s_pwm_period + 1U) * duty_per_mille) / 1000U);
}

void comp_pwm_stop(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    if (s_pwm_running)
    {
        HAL_TIM_PWM_Stop(&s_pwm_handle, TIM_CHANNEL_4);
        __HAL_RCC_TIM1_CLK_DISABLE();
        s_pwm_running = 0U;
    }

    gpio_init.Pin = GPIO_PIN_11;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio_init);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET);
}
