#include "camera_ov2640.h"

#include "main.h"

#include <stdio.h>

#define CAMERA_OV2640_ADDR            0x60u
#define CAMERA_OV2640_MID             0x7FA2u
#define CAMERA_OV2640_PID             0x2642u
#define CAMERA_OV2640_REG_BANK_SEL    0xFFu
#define CAMERA_OV2640_REG_COM7        0x12u
#define CAMERA_OV2640_REG_MIDH        0x1Cu
#define CAMERA_OV2640_REG_MIDL        0x1Du
#define CAMERA_OV2640_REG_PIDH        0x0Au
#define CAMERA_OV2640_REG_PIDL        0x0Bu

#define CAMERA_OV2640_RST_PORT        GPIOC
#define CAMERA_OV2640_RST_PIN         GPIO_PIN_5
#define CAMERA_OV2640_PWDN_PORT       GPIOI
#define CAMERA_OV2640_PWDN_PIN        GPIO_PIN_3

#define CAMERA_OV2640_SCL_PORT        GPIOG
#define CAMERA_OV2640_SCL_PIN         GPIO_PIN_3
#define CAMERA_OV2640_SDA_PORT        GPIOB
#define CAMERA_OV2640_SDA_PIN         GPIO_PIN_10

#define CAMERA_OV2640_FLIP_VER        0u
#define CAMERA_OV2640_DIAG_NONE       0u
#define CAMERA_OV2640_DIAG_RESET_IO   1u
#define CAMERA_OV2640_DIAG_SOFT_RESET 2u
#define CAMERA_OV2640_DIAG_READ_MIDH  3u
#define CAMERA_OV2640_DIAG_READ_MIDL  4u
#define CAMERA_OV2640_DIAG_READ_PIDH  5u
#define CAMERA_OV2640_DIAG_READ_PIDL  6u
#define CAMERA_OV2640_DIAG_ID_MATCH   7u
#define CAMERA_OV2640_DIAG_READY      8u

static uint8_t s_ov2640_dwt_ready = 0u;
static volatile uint16_t s_ov2640_last_mid = 0u;
static volatile uint16_t s_ov2640_last_pid = 0u;
static volatile uint8_t s_ov2640_diag_stage = CAMERA_OV2640_DIAG_NONE;
static volatile uint8_t s_ov2640_last_write_status = 0u;
static volatile uint8_t s_ov2640_last_read_status = 0u;

static void s_delay_us(uint32_t us)
{
    uint32_t ticks;
    uint32_t start;

    if (us == 0u)
    {
        return;
    }

    if (s_ov2640_dwt_ready == 0u)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        DWT->CYCCNT = 0u;
        s_ov2640_dwt_ready = 1u;
    }

    ticks = (SystemCoreClock / 1000000u) * us;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks)
    {
    }
}

static const uint8_t s_ov2640_svga_init_reg_tbl[][2] =
{
    {0xff, 0x00},
    {0x2c, 0xff},
    {0x2e, 0xdf},
    {0xff, 0x01},
    {0x3c, 0x32},
    {0x11, 0x00},
    {0x09, 0x02},
#if (CAMERA_OV2640_FLIP_VER != 0u)
    {0x04, 0xD8},
#else
    {0x04, 0x28},
#endif
    {0x13, 0xe5},
    {0x14, 0x48},
    {0x2c, 0x0c},
    {0x33, 0x78},
    {0x3a, 0x33},
    {0x3b, 0xfb},
    {0x3e, 0x00},
    {0x43, 0x11},
    {0x16, 0x10},
    {0x39, 0x92},
    {0x35, 0xda},
    {0x22, 0x1a},
    {0x37, 0xc3},
    {0x23, 0x00},
    {0x34, 0xc0},
    {0x36, 0x1a},
    {0x06, 0x88},
    {0x07, 0xc0},
    {0x0d, 0x87},
    {0x0e, 0x41},
    {0x4c, 0x00},
    {0x48, 0x00},
    {0x5b, 0x00},
    {0x42, 0x03},
    {0x4a, 0x81},
    {0x21, 0x99},
    {0x24, 0x40},
    {0x25, 0x38},
    {0x26, 0x82},
    {0x5c, 0x00},
    {0x63, 0x00},
    {0x46, 0x22},
    {0x0c, 0x3c},
    {0x61, 0x70},
    {0x62, 0x80},
    {0x7c, 0x05},
    {0x20, 0x80},
    {0x28, 0x30},
    {0x6c, 0x00},
    {0x6d, 0x80},
    {0x6e, 0x00},
    {0x70, 0x02},
    {0x71, 0x94},
    {0x73, 0xc1},
    {0x3d, 0x34},
    {0x5a, 0x57},
    {0x12, 0x40},
    {0x17, 0x11},
    {0x18, 0x43},
    {0x19, 0x00},
    {0x1a, 0x4b},
    {0x32, 0x09},
    {0x37, 0xc0},
    {0x4f, 0xca},
    {0x50, 0xa8},
    {0x5a, 0x23},
    {0x6d, 0x00},
    {0x3d, 0x38},
    {0xff, 0x00},
    {0xe5, 0x7f},
    {0xf9, 0xc0},
    {0x41, 0x24},
    {0xe0, 0x14},
    {0x76, 0xff},
    {0x33, 0xa0},
    {0x42, 0x20},
    {0x43, 0x18},
    {0x4c, 0x00},
    {0x87, 0xd5},
    {0x88, 0x3f},
    {0xd7, 0x03},
    {0xd9, 0x10},
    {0xd3, 0x82},
    {0xc8, 0x08},
    {0xc9, 0x80},
    {0x7c, 0x00},
    {0x7d, 0x00},
    {0x7c, 0x03},
    {0x7d, 0x48},
    {0x7d, 0x48},
    {0x7c, 0x08},
    {0x7d, 0x20},
    {0x7d, 0x10},
    {0x7d, 0x0e},
    {0x90, 0x00},
    {0x91, 0x0e},
    {0x91, 0x1a},
    {0x91, 0x31},
    {0x91, 0x5a},
    {0x91, 0x69},
    {0x91, 0x75},
    {0x91, 0x7e},
    {0x91, 0x88},
    {0x91, 0x8f},
    {0x91, 0x96},
    {0x91, 0xa3},
    {0x91, 0xaf},
    {0x91, 0xc4},
    {0x91, 0xd7},
    {0x91, 0xe8},
    {0x91, 0x20},
    {0x92, 0x00},
    {0x93, 0x06},
    {0x93, 0xe3},
    {0x93, 0x05},
    {0x93, 0x05},
    {0x93, 0x00},
    {0x93, 0x04},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x93, 0x00},
    {0x96, 0x00},
    {0x97, 0x08},
    {0x97, 0x19},
    {0x97, 0x02},
    {0x97, 0x0c},
    {0x97, 0x24},
    {0x97, 0x30},
    {0x97, 0x28},
    {0x97, 0x26},
    {0x97, 0x02},
    {0x97, 0x98},
    {0x97, 0x80},
    {0x97, 0x00},
    {0x97, 0x00},
    {0xc3, 0xed},
    {0xa4, 0x00},
    {0xa8, 0x00},
    {0xc5, 0x11},
    {0xc6, 0x51},
    {0xbf, 0x80},
    {0xc7, 0x10},
    {0xb6, 0x66},
    {0xb8, 0xa5},
    {0xb7, 0x64},
    {0xb9, 0x7c},
    {0xb3, 0xaf},
    {0xb4, 0x97},
    {0xb5, 0xff},
    {0xb0, 0xc5},
    {0xb1, 0x94},
    {0xb2, 0x0f},
    {0xc4, 0x5c},
    {0xc0, 0x64},
    {0xc1, 0x4b},
    {0x8c, 0x00},
    {0x86, 0x3d},
    {0x50, 0x00},
    {0x51, 0xc8},
    {0x52, 0x96},
    {0x53, 0x00},
    {0x54, 0x00},
    {0x55, 0x00},
    {0x5a, 0xc8},
    {0x5b, 0x96},
    {0x5c, 0x00},
    {0xd3, 0x02},
    {0xc3, 0xed},
    {0x7f, 0x00},
    {0xda, 0x09},
    {0xe5, 0x1f},
    {0xe1, 0x67},
    {0xe0, 0x00},
    {0xdd, 0x7f},
    {0x05, 0x00},
};

static const uint8_t s_ov2640_rgb565_reg_tbl[][2] =
{
    {0xff, 0x00},
    {0xda, 0x09},
    {0xd7, 0x03},
    {0xdf, 0x02},
    {0x33, 0xa0},
    {0x3c, 0x00},
    {0xe1, 0x67},
    {0xff, 0x01},
    {0x11, 0x00},
    {0xe0, 0x00},
    {0xe1, 0x00},
    {0xe5, 0x00},
    {0xd7, 0x00},
    {0xda, 0x00},
    {0xe0, 0x00},
};

static void s_gpio_write(GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    HAL_GPIO_WritePin(port, pin, (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void s_sccb_delay(void)
{
    s_delay_us(5u);
}

static void s_sccb_stop(void);

static void s_sccb_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init.Pin = CAMERA_OV2640_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(CAMERA_OV2640_SCL_PORT, &gpio_init);

    gpio_init.Pin = CAMERA_OV2640_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(CAMERA_OV2640_SDA_PORT, &gpio_init);

    s_sccb_stop();
}

static void s_sccb_start(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 0u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
    s_sccb_delay();
}

static void s_sccb_stop(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 0u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_sccb_delay();
}

static void s_sccb_nack(void)
{
    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
    s_sccb_delay();
}

static uint8_t s_sccb_send_byte(uint8_t data)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        s_gpio_write(CAMERA_OV2640_SDA_PORT,
                     CAMERA_OV2640_SDA_PIN,
                     (uint8_t)((data & 0x80u) != 0u));
        s_sccb_delay();
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
        s_sccb_delay();
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        data <<= 1;
    }

    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    s_sccb_delay();
    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
    s_sccb_delay();

    if (HAL_GPIO_ReadPin(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN) != GPIO_PIN_RESET)
    {
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        return 1u;
    }

    s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
    return 0u;
}

static uint8_t s_sccb_read_byte(void)
{
    uint8_t i;
    uint8_t value = 0u;

    s_gpio_write(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN, 1u);
    for (i = 0u; i < 8u; i++)
    {
        value <<= 1;
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 1u);
        s_sccb_delay();
        if (HAL_GPIO_ReadPin(CAMERA_OV2640_SDA_PORT, CAMERA_OV2640_SDA_PIN) != GPIO_PIN_RESET)
        {
            value |= 1u;
        }
        s_gpio_write(CAMERA_OV2640_SCL_PORT, CAMERA_OV2640_SCL_PIN, 0u);
        s_sccb_delay();
    }

    return value;
}

static uint8_t s_ov2640_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t status = 0u;

    s_sccb_start();
    s_delay_us(100u);
    if (s_sccb_send_byte(CAMERA_OV2640_ADDR) != 0u)
    {
        status = 1u;
    }
    s_delay_us(100u);
    if (s_sccb_send_byte(reg) != 0u)
    {
        status = 1u;
    }
    s_delay_us(100u);
    if (s_sccb_send_byte(value) != 0u)
    {
        status = 1u;
    }
    s_delay_us(100u);
    s_sccb_stop();

    return status;
}

static uint8_t s_ov2640_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t data = 0u;

    if (value == NULL)
    {
        return 1u;
    }

    s_sccb_start();
    s_delay_us(100u);
    if (s_sccb_send_byte(CAMERA_OV2640_ADDR) != 0u)
    {
        s_sccb_stop();
        return 1u;
    }
    s_delay_us(100u);
    if (s_sccb_send_byte(reg) != 0u)
    {
        s_sccb_stop();
        return 1u;
    }
    s_sccb_stop();
    s_delay_us(100u);

    s_sccb_start();
    s_delay_us(100u);
    if (s_sccb_send_byte(CAMERA_OV2640_ADDR | 0x01u) != 0u)
    {
        s_sccb_stop();
        return 1u;
    }
    s_delay_us(100u);
    data = s_sccb_read_byte();
    s_sccb_nack();
    s_sccb_stop();

    *value = data;
    return 0u;
}

static void s_apply_table(const uint8_t table[][2], uint32_t pair_count)
{
    uint32_t i;

    for (i = 0u; i < pair_count; i++)
    {
        (void)s_ov2640_write_reg(table[i][0], table[i][1]);
    }
}

static void s_reset_io_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio_init.Pin = CAMERA_OV2640_RST_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(CAMERA_OV2640_RST_PORT, &gpio_init);

    gpio_init.Pin = CAMERA_OV2640_PWDN_PIN;
    HAL_GPIO_Init(CAMERA_OV2640_PWDN_PORT, &gpio_init);
}

static uint8_t s_ov2640_outsize_set(uint16_t width, uint16_t height)
{
    uint16_t out_h;
    uint16_t out_w;
    uint8_t temp;

    if (((width % 4u) != 0u) || ((height % 4u) != 0u))
    {
        return 1u;
    }

    out_w = (uint16_t)(width / 4u);
    out_h = (uint16_t)(height / 4u);

    (void)s_ov2640_write_reg(0xff, 0x00);
    (void)s_ov2640_write_reg(0xe0, 0x04);
    (void)s_ov2640_write_reg(0x5a, (uint8_t)(out_w & 0xFFu));
    (void)s_ov2640_write_reg(0x5b, (uint8_t)(out_h & 0xFFu));

    temp = (uint8_t)((out_w >> 8) & 0x03u);
    temp |= (uint8_t)((out_h >> 6) & 0x04u);
    (void)s_ov2640_write_reg(0x5c, temp);
    (void)s_ov2640_write_reg(0xe0, 0x00);

    return 0u;
}

static uint8_t s_ov2640_image_win_set(uint16_t off_x, uint16_t off_y, uint16_t width, uint16_t height)
{
    uint16_t hsize;
    uint16_t vsize;
    uint8_t temp;

    if (((width % 4u) != 0u) || ((height % 4u) != 0u))
    {
        return 1u;
    }

    hsize = (uint16_t)(width / 4u);
    vsize = (uint16_t)(height / 4u);

    (void)s_ov2640_write_reg(0xff, 0x00);
    (void)s_ov2640_write_reg(0xe0, 0x04);
    (void)s_ov2640_write_reg(0x51, (uint8_t)(hsize & 0xFFu));
    (void)s_ov2640_write_reg(0x52, (uint8_t)(vsize & 0xFFu));
    (void)s_ov2640_write_reg(0x53, (uint8_t)(off_x & 0xFFu));
    (void)s_ov2640_write_reg(0x54, (uint8_t)(off_y & 0xFFu));

    temp = (uint8_t)((vsize >> 1) & 0x80u);
    temp |= (uint8_t)((off_y >> 4) & 0x70u);
    temp |= (uint8_t)((hsize >> 5) & 0x08u);
    temp |= (uint8_t)((off_x >> 8) & 0x07u);
    (void)s_ov2640_write_reg(0x55, temp);
    (void)s_ov2640_write_reg(0x57, (uint8_t)((hsize >> 2) & 0x80u));
    (void)s_ov2640_write_reg(0xe0, 0x00);

    return 0u;
}

static uint8_t s_ov2640_imagesize_set(uint16_t width, uint16_t height)
{
    uint8_t temp;

    (void)s_ov2640_write_reg(0xff, 0x00);
    (void)s_ov2640_write_reg(0xe0, 0x04);
    (void)s_ov2640_write_reg(0xc0, (uint8_t)((width >> 3) & 0xFFu));
    (void)s_ov2640_write_reg(0xc1, (uint8_t)((height >> 3) & 0xFFu));

    temp = (uint8_t)((width & 0x07u) << 3);
    temp |= (uint8_t)(height & 0x07u);
    temp |= (uint8_t)((width >> 4) & 0x80u);
    (void)s_ov2640_write_reg(0x8c, temp);
    (void)s_ov2640_write_reg(0xe0, 0x00);

    return 0u;
}

uint8_t Camera_OV2640_ReadId(uint16_t *mid, uint16_t *pid)
{
    uint8_t high;
    uint8_t low;

    if ((mid == NULL) || (pid == NULL))
    {
        return 1u;
    }

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_MIDH;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_MIDH, &high);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_MIDL;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_MIDL, &low);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    *mid = (uint16_t)(((uint16_t)high << 8) | low);
    s_ov2640_last_mid = *mid;

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_PIDH;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_PIDH, &high);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READ_PIDL;
    s_ov2640_last_read_status = s_ov2640_read_reg(CAMERA_OV2640_REG_PIDL, &low);
    if (s_ov2640_last_read_status != 0u)
    {
        return 1u;
    }
    *pid = (uint16_t)(((uint16_t)high << 8) | low);
    s_ov2640_last_pid = *pid;

    return 0u;
}

uint8_t Camera_OV2640_Init(void)
{
    uint16_t mid = 0u;
    uint16_t pid = 0u;

    s_ov2640_last_mid = 0u;
    s_ov2640_last_pid = 0u;
    s_ov2640_last_write_status = 0u;
    s_ov2640_last_read_status = 0u;
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_RESET_IO;

    s_reset_io_init();

    s_gpio_write(CAMERA_OV2640_PWDN_PORT, CAMERA_OV2640_PWDN_PIN, 0u);
    HAL_Delay(10u);
    s_gpio_write(CAMERA_OV2640_RST_PORT, CAMERA_OV2640_RST_PIN, 0u);
    HAL_Delay(20u);
    s_gpio_write(CAMERA_OV2640_RST_PORT, CAMERA_OV2640_RST_PIN, 1u);
    HAL_Delay(20u);

    s_sccb_init();
    HAL_Delay(5u);

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_SOFT_RESET;
    s_ov2640_last_write_status = s_ov2640_write_reg(CAMERA_OV2640_REG_BANK_SEL, 0x01u);
    if (s_ov2640_last_write_status != 0u)
    {
        printf("CAM: OV2640 bank select write failed\r\n");
        return 1u;
    }
    s_ov2640_last_write_status = s_ov2640_write_reg(CAMERA_OV2640_REG_COM7, 0x80u);
    if (s_ov2640_last_write_status != 0u)
    {
        printf("CAM: OV2640 soft reset write failed\r\n");
        return 1u;
    }
    HAL_Delay(50u);

    if (Camera_OV2640_ReadId(&mid, &pid) != 0u)
    {
        printf("CAM: OV2640 read id failed\r\n");
        return 1u;
    }
    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_ID_MATCH;
    if ((mid != CAMERA_OV2640_MID) || (pid != CAMERA_OV2640_PID))
    {
        printf("CAM: OV2640 id mismatch mid=0x%04X pid=0x%04X\r\n", mid, pid);
        return 1u;
    }

    s_apply_table(s_ov2640_svga_init_reg_tbl,
                  (uint32_t)(sizeof(s_ov2640_svga_init_reg_tbl) / sizeof(s_ov2640_svga_init_reg_tbl[0])));

    s_ov2640_diag_stage = CAMERA_OV2640_DIAG_READY;
    return 0u;
}

uint8_t Camera_OV2640_ConfigRgb565Preview(uint16_t width, uint16_t height)
{
    s_apply_table(s_ov2640_rgb565_reg_tbl,
                  (uint32_t)(sizeof(s_ov2640_rgb565_reg_tbl) / sizeof(s_ov2640_rgb565_reg_tbl[0])));

    if (s_ov2640_imagesize_set(800u, 600u) != 0u)
    {
        return 1u;
    }
    if (s_ov2640_image_win_set(0u, 0u, 800u, 600u) != 0u)
    {
        return 1u;
    }
    if (s_ov2640_outsize_set(width, height) != 0u)
    {
        return 1u;
    }

    return 0u;
}

void Camera_OV2640_GetDiag(Camera_OV2640_Diag_t *diag)
{
    if (diag == NULL)
    {
        return;
    }

    diag->mid = s_ov2640_last_mid;
    diag->pid = s_ov2640_last_pid;
    diag->diag_stage = s_ov2640_diag_stage;
    diag->last_write_status = s_ov2640_last_write_status;
    diag->last_read_status = s_ov2640_last_read_status;
}
