#include "./BSP/COMPETITION/comp_keys.h"

#define COMP_KEY_COUNT       4U
#define COMP_KEY_DEBOUNCE_MS 20U

static GPIO_TypeDef *s_key_ports[COMP_KEY_COUNT] =
{
    GPIOA,
    GPIOA,
    GPIOC,
    GPIOA
};

static const uint16_t s_key_pins[COMP_KEY_COUNT] =
{
    GPIO_PIN_1,
    GPIO_PIN_15,
    GPIO_PIN_0,
    GPIO_PIN_12
};

static uint8_t s_raw_mask;
static uint8_t s_stable_mask;
static uint8_t s_event_mask;
static uint32_t s_changed_at[COMP_KEY_COUNT];

static uint8_t comp_keys_sample(void)
{
    uint8_t i;
    uint8_t mask = 0;

    for (i = 0; i < COMP_KEY_COUNT; i++)
    {
        if (HAL_GPIO_ReadPin(s_key_ports[i], s_key_pins[i]) == GPIO_PIN_RESET)
        {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}

void comp_keys_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    uint8_t i;
    uint32_t now;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    gpio_init.Pin = GPIO_PIN_1 | GPIO_PIN_12 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &gpio_init);
    gpio_init.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    s_raw_mask = comp_keys_sample();
    s_stable_mask = s_raw_mask;
    s_event_mask = 0;
    now = HAL_GetTick();

    for (i = 0; i < COMP_KEY_COUNT; i++)
    {
        s_changed_at[i] = now;
    }
}

uint8_t comp_keys_read_mask(void)
{
    return s_stable_mask;
}

comp_key_t comp_keys_poll_event(void)
{
    uint8_t i;
    uint8_t sampled;
    uint8_t bit;
    uint32_t now = HAL_GetTick();

    sampled = comp_keys_sample();

    for (i = 0; i < COMP_KEY_COUNT; i++)
    {
        bit = (uint8_t)(1U << i);

        if ((sampled & bit) != (s_raw_mask & bit))
        {
            s_raw_mask ^= bit;
            s_changed_at[i] = now;
        }
        else if (((s_stable_mask & bit) != (s_raw_mask & bit)) &&
                 ((uint32_t)(now - s_changed_at[i]) >= COMP_KEY_DEBOUNCE_MS))
        {
            s_stable_mask ^= bit;
            if ((s_stable_mask & bit) != 0U)
            {
                s_event_mask |= bit;
            }
        }
    }

    for (i = 0; i < COMP_KEY_COUNT; i++)
    {
        bit = (uint8_t)(1U << i);
        if ((s_event_mask & bit) != 0U)
        {
            s_event_mask &= (uint8_t)(~bit);
            return (comp_key_t)(i + 1U);
        }
    }

    return COMP_KEY_NONE;
}
