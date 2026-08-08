#include "./BSP/COMPETITION/comp_max7219.h"

#define MAX7219_DATA_GPIO_PORT     GPIOB
#define MAX7219_DATA_GPIO_PIN      GPIO_PIN_12
#define MAX7219_CLOCK_GPIO_PORT    GPIOA
#define MAX7219_CLOCK_GPIO_PIN     GPIO_PIN_2
#define MAX7219_LOAD_GPIO_PORT     GPIOE
#define MAX7219_LOAD_GPIO_PIN      GPIO_PIN_0

#define MAX7219_DATA_LOW()   (MAX7219_DATA_GPIO_PORT->BSRR = ((uint32_t)MAX7219_DATA_GPIO_PIN << 16U))
#define MAX7219_DATA_HIGH()  (MAX7219_DATA_GPIO_PORT->BSRR = MAX7219_DATA_GPIO_PIN)
#define MAX7219_CLOCK_LOW()  (MAX7219_CLOCK_GPIO_PORT->BSRR = ((uint32_t)MAX7219_CLOCK_GPIO_PIN << 16U))
#define MAX7219_CLOCK_HIGH() (MAX7219_CLOCK_GPIO_PORT->BSRR = MAX7219_CLOCK_GPIO_PIN)
#define MAX7219_LOAD_LOW()   (MAX7219_LOAD_GPIO_PORT->BSRR = ((uint32_t)MAX7219_LOAD_GPIO_PIN << 16U))
#define MAX7219_LOAD_HIGH()  (MAX7219_LOAD_GPIO_PORT->BSRR = MAX7219_LOAD_GPIO_PIN)

#define MAX7219_REG_DECODE_MODE  0x09U
#define MAX7219_REG_INTENSITY    0x0AU
#define MAX7219_REG_SCAN_LIMIT   0x0BU
#define MAX7219_REG_SHUTDOWN     0x0CU
#define MAX7219_REG_DISPLAY_TEST 0x0FU

static void comp_max7219_write(uint8_t address, uint8_t data)
{
    uint16_t word = (uint16_t)(((uint16_t)address << 8U) | data);
    int8_t bit;

    MAX7219_LOAD_LOW();
    for (bit = 15; bit >= 0; bit--)
    {
        MAX7219_CLOCK_LOW();
        if ((word & ((uint16_t)1U << bit)) != 0U)
        {
            MAX7219_DATA_HIGH();
        }
        else
        {
            MAX7219_DATA_LOW();
        }
        MAX7219_CLOCK_HIGH();
    }
    MAX7219_CLOCK_LOW();
    MAX7219_LOAD_HIGH();
    MAX7219_LOAD_LOW();
}

void comp_max7219_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio_init.Pin = MAX7219_DATA_GPIO_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    gpio_init.Pin = MAX7219_CLOCK_GPIO_PIN;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = MAX7219_LOAD_GPIO_PIN;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    MAX7219_DATA_LOW();
    MAX7219_CLOCK_LOW();
    MAX7219_LOAD_LOW();

    comp_max7219_write(MAX7219_REG_DISPLAY_TEST, 0x00U);
    comp_max7219_write(MAX7219_REG_SHUTDOWN, 0x00U);
    comp_max7219_write(MAX7219_REG_DECODE_MODE, 0x0FU); /* Code-B decode on digits 0..3. */
    comp_max7219_write(MAX7219_REG_SCAN_LIMIT, 0x03U);  /* Scan exactly four digits. */
    comp_max7219_set_intensity(3U);
    comp_max7219_clear();
    comp_max7219_write(MAX7219_REG_SHUTDOWN, 0x01U);
}

void comp_max7219_clear(void)
{
    uint8_t position;

    for (position = 0U; position < 4U; position++)
    {
        comp_max7219_set_digit(position, COMP_MAX7219_BLANK);
    }
}

void comp_max7219_set_intensity(uint8_t level)
{
    if (level > 15U)
    {
        level = 15U;
    }
    comp_max7219_write(MAX7219_REG_INTENSITY, level);
}

void comp_max7219_set_digit(uint8_t position, uint8_t code_b_data)
{
    if (position < 4U)
    {
        /* Logical position 0 is leftmost; MAX7219 digit 0 is wired rightmost. */
        comp_max7219_write((uint8_t)(4U - position), code_b_data);
    }
}

void comp_max7219_show_number(int16_t value, uint8_t leading_zero)
{
    uint16_t magnitude;
    uint8_t output[4] =
    {
        COMP_MAX7219_BLANK, COMP_MAX7219_BLANK,
        COMP_MAX7219_BLANK, COMP_MAX7219_BLANK
    };
    int8_t position;
    uint8_t negative = 0U;

    if (value < -999)
    {
        value = -999;
    }
    else if (value > 9999)
    {
        value = 9999;
    }

    if (value < 0)
    {
        negative = 1U;
        magnitude = (uint16_t)(-value);
    }
    else
    {
        magnitude = (uint16_t)value;
    }

    for (position = 3; position >= 0; position--)
    {
        output[position] = (uint8_t)(magnitude % 10U);
        magnitude /= 10U;
        if ((magnitude == 0U) && (position > 0) && (leading_zero == 0U))
        {
            break;
        }
    }

    if (negative != 0U)
    {
        position--;
        if (position < 0)
        {
            position = 0;
        }
        output[position] = COMP_MAX7219_MINUS;
    }

    for (position = 0; position < 4; position++)
    {
        comp_max7219_set_digit((uint8_t)position, output[position]);
    }
}

void comp_max7219_show_fixed1(int16_t tenths)
{
    uint16_t magnitude;
    uint8_t output[4] =
    {
        COMP_MAX7219_BLANK, COMP_MAX7219_BLANK,
        COMP_MAX7219_BLANK, COMP_MAX7219_BLANK
    };
    uint8_t negative = 0U;

    if (tenths < -999)
    {
        tenths = -999;
    }
    else if (tenths > 9999)
    {
        tenths = 9999;
    }

    if (tenths < 0)
    {
        negative = 1U;
        magnitude = (uint16_t)(-tenths);
    }
    else
    {
        magnitude = (uint16_t)tenths;
    }

    output[3] = (uint8_t)(magnitude % 10U);
    magnitude /= 10U;
    output[2] = (uint8_t)((magnitude % 10U) | COMP_MAX7219_DP);
    magnitude /= 10U;

    if (magnitude != 0U)
    {
        output[1] = (uint8_t)(magnitude % 10U);
        magnitude /= 10U;
        if (magnitude != 0U)
        {
            output[0] = (uint8_t)(magnitude % 10U);
        }
    }

    if (negative != 0U)
    {
        if (output[1] == COMP_MAX7219_BLANK)
        {
            output[1] = COMP_MAX7219_MINUS;
        }
        else
        {
            output[0] = COMP_MAX7219_MINUS;
        }
    }

    comp_max7219_set_digit(0U, output[0]);
    comp_max7219_set_digit(1U, output[1]);
    comp_max7219_set_digit(2U, output[2]);
    comp_max7219_set_digit(3U, output[3]);
}
