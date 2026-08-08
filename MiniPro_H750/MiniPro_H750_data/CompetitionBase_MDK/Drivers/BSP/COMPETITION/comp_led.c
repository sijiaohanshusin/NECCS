#include "./BSP/COMPETITION/comp_led.h"

static GPIO_TypeDef *s_led_ports[COMP_LED_COUNT] =
{
    GPIOB,
    GPIOB,
    GPIOA,
    GPIOC
};

static const uint16_t s_led_pins[COMP_LED_COUNT] =
{
    GPIO_PIN_14,
    GPIO_PIN_15,
    GPIO_PIN_3,
    GPIO_PIN_3
};

void comp_led_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    gpio_init.Pin = GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    HAL_GPIO_WritePin(GPIOB, gpio_init.Pin, GPIO_PIN_RESET);

    gpio_init.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio_init);
    HAL_GPIO_WritePin(GPIOA, gpio_init.Pin, GPIO_PIN_RESET);

    gpio_init.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    HAL_GPIO_WritePin(GPIOC, gpio_init.Pin, GPIO_PIN_RESET);
}

void comp_led_set(comp_led_t led, uint8_t on)
{
    if (led >= COMP_LED_COUNT)
    {
        return;
    }

    HAL_GPIO_WritePin(s_led_ports[led], s_led_pins[led], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void comp_led_toggle(comp_led_t led)
{
    if (led < COMP_LED_COUNT)
    {
        HAL_GPIO_TogglePin(s_led_ports[led], s_led_pins[led]);
    }
}

void comp_led_set_mask(uint8_t mask)
{
    uint8_t i;

    for (i = 0; i < COMP_LED_COUNT; i++)
    {
        comp_led_set((comp_led_t)i, (mask >> i) & 0x01U);
    }
}
