#include "stdio.h"
#include "string.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/COMPETITION/comp_led.h"
#include "./BSP/COMPETITION/comp_keys.h"
#include "./BSP/COMPETITION/comp_buzzer.h"
#include "./BSP/COMPETITION/comp_pwm.h"
#include "./BSP/COMPETITION/comp_nvm.h"
#include "./BSP/COMPETITION/comp_max7219.h"
#include "./BSP/COMPETITION/comp_hanzi.h"
#include "exam_2025.h"

#define EXAM2025_LED_ALL_MASK       0x0FU
#define EXAM2025_LED_1_AND_3_MASK   0x05U
#define EXAM2025_KEY1_MASK          0x01U
#define EXAM2025_AA_MIN_TENTHS      (-999)
#define EXAM2025_AA_MAX_TENTHS      9999

#define EXAM2025_BREATH_PERIOD_MS   2000UL
#define EXAM2025_SOFT_PWM_PERIOD_MS   10UL

#define EXAM2025_NVM_RECORD_SIZE    8U
#define EXAM2025_NVM_MAGIC_0        0x41U
#define EXAM2025_NVM_MAGIC_1        0x41U
#define EXAM2025_NVM_VERSION        0x25U

typedef enum
{
    EXAM_LED_IDLE = 0,
    EXAM_LED_STARTUP_WAIT,
    EXAM_LED_SEQUENCE
} exam_led_mode_t;

static exam_led_mode_t s_led_mode;
static uint32_t s_led_changed_at;
static uint32_t s_led_step_time_ms;
static uint8_t s_led_sequence_step;
static uint8_t s_key1_wait_release;
static uint8_t s_nvm_available;
static int16_t s_aa_tenths;

/*
 * PA11 carries the real hardware PWM. LED1 remains on its existing PB14
 * wiring and is gated with the same duty ratio in software for a visible
 * breathing effect. No GPIO assignment is changed.
 */
static void exam2025_process_breathing_led(void)
{
    uint32_t now;
    uint32_t phase;
    uint16_t duty_per_mille;
    uint16_t soft_pwm_threshold;

    if (s_led_mode != EXAM_LED_IDLE)
    {
        return;
    }

    now = HAL_GetTick();
    phase = now % EXAM2025_BREATH_PERIOD_MS;
    if (phase <= (EXAM2025_BREATH_PERIOD_MS / 2UL))
    {
        duty_per_mille = (uint16_t)phase;
    }
    else
    {
        duty_per_mille = (uint16_t)(EXAM2025_BREATH_PERIOD_MS - phase);
    }

    comp_pwm_set_duty(duty_per_mille);

    soft_pwm_threshold = (uint16_t)((now % EXAM2025_SOFT_PWM_PERIOD_MS) *
                                    (1000UL / EXAM2025_SOFT_PWM_PERIOD_MS));
    comp_led_set(COMP_LED1,
                 (soft_pwm_threshold < duty_per_mille) ? 1U : 0U);
}

static uint8_t exam2025_nvm_checksum(const uint8_t *record)
{
    uint8_t i;
    uint8_t checksum = 0xA5U;

    for (i = 0U; i < 6U; i++)
    {
        checksum ^= record[i];
    }
    return checksum;
}

static uint8_t exam2025_load_aa(int16_t *aa_tenths)
{
    uint8_t record[EXAM2025_NVM_RECORD_SIZE];
    uint8_t checksum;

    if ((s_nvm_available == 0U) || (aa_tenths == NULL))
    {
        return 1U;
    }
    if (comp_nvm_read(0U, record, sizeof(record)) != 0U)
    {
        return 1U;
    }

    checksum = exam2025_nvm_checksum(record);
    if ((record[0] != EXAM2025_NVM_MAGIC_0) ||
        (record[1] != EXAM2025_NVM_MAGIC_1) ||
        (record[2] != EXAM2025_NVM_VERSION) ||
        (record[3] != EXAM2025_NVM_RECORD_SIZE) ||
        (record[6] != checksum) ||
        (record[7] != (uint8_t)(~checksum)))
    {
        return 1U;
    }

    *aa_tenths = (int16_t)((uint16_t)record[4] |
                           ((uint16_t)record[5] << 8));
    if ((*aa_tenths < EXAM2025_AA_MIN_TENTHS) ||
        (*aa_tenths > EXAM2025_AA_MAX_TENTHS))
    {
        return 1U;
    }
    return 0U;
}

static uint8_t exam2025_save_aa(int16_t aa_tenths)
{
    uint8_t record[EXAM2025_NVM_RECORD_SIZE];
    uint8_t checksum;

    if (s_nvm_available == 0U)
    {
        return 1U;
    }

    record[0] = EXAM2025_NVM_MAGIC_0;
    record[1] = EXAM2025_NVM_MAGIC_1;
    record[2] = EXAM2025_NVM_VERSION;
    record[3] = EXAM2025_NVM_RECORD_SIZE;
    record[4] = (uint8_t)((uint16_t)aa_tenths & 0xFFU);
    record[5] = (uint8_t)(((uint16_t)aa_tenths >> 8) & 0xFFU);
    checksum = exam2025_nvm_checksum(record);
    record[6] = checksum;
    record[7] = (uint8_t)(~checksum);

    return comp_nvm_write(0U, record, sizeof(record));
}

static void exam2025_format_aa(char *buffer, uint16_t size, int16_t aa_tenths)
{
    int32_t value = aa_tenths;
    uint32_t magnitude;

    if ((buffer == NULL) || (size == 0U))
    {
        return;
    }

    magnitude = (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
    if (value < 0)
    {
        snprintf(buffer, size, "-%lu.%lu",
                 (unsigned long)(magnitude / 10U),
                 (unsigned long)(magnitude % 10U));
    }
    else
    {
        snprintf(buffer, size, "%lu.%lu",
                 (unsigned long)(magnitude / 10U),
                 (unsigned long)(magnitude % 10U));
    }
}

static void exam2025_show_aa(void)
{
    char value[24];
    char line[32];

    exam2025_format_aa(value, sizeof(value), s_aa_tenths);
    snprintf(line, sizeof(line), "aa = %s", value);
    lcd_fill(20U, 120U, 300U, 151U, WHITE);
    lcd_show_string(20U, 120U, 280U, 32U, 32U, line, BLUE);
}

static void exam2025_show_uart_status(const char *text, uint16_t color)
{
    lcd_fill(20U, 180U, 430U, 199U, WHITE);
    lcd_show_string(20U, 180U, 410U, 16U, 16U, (char *)text, color);
}

static void exam2025_show_send_success(void)
{
    lcd_fill(20U, 155U, 430U, 175U, WHITE);
    comp_hanzi_show_utf8(20U, 155U, COMP_TEXT_SEND_OK, GREEN, WHITE);
}

void exam2025_uart_send_line(const char *text)
{
    if (text == NULL)
    {
        return;
    }
    printf("%s\r\n", text);
}

void exam2025_set_aa_tenths(int16_t aa_tenths,
                            uint8_t save_to_nvm,
                            uint8_t send_uart)
{
    char value[24];
    char line[32];

    if (aa_tenths < EXAM2025_AA_MIN_TENTHS)
    {
        aa_tenths = EXAM2025_AA_MIN_TENTHS;
    }
    if (aa_tenths > EXAM2025_AA_MAX_TENTHS)
    {
        aa_tenths = EXAM2025_AA_MAX_TENTHS;
    }

    s_aa_tenths = aa_tenths;
    exam2025_show_aa();

    /*
     * ===== 现场改题区：变量改变后触发的条件 =====
     * 当前要求：aa==0 蜂鸣器响，并把 aa 显示到四位数码管。
     * 若新题条件不同，就在这里改 if 条件或替换对应动作。
     */
    comp_buzzer_set((s_aa_tenths == 0) ? 1U : 0U);
    comp_max7219_show_fixed1(s_aa_tenths);

    if ((save_to_nvm != 0U) && (exam2025_save_aa(s_aa_tenths) != 0U))
    {
        exam2025_show_uart_status("NVM write failed", RED);
    }

    if (send_uart != 0U)
    {
        exam2025_format_aa(value, sizeof(value), s_aa_tenths);
        snprintf(line, sizeof(line), "aa=%s", value);
        exam2025_uart_send_line(line);
    }
}

int16_t exam2025_get_aa_tenths(void)
{
    return s_aa_tenths;
}

void exam2025_start_led_sequence(uint32_t step_time_ms)
{
    if (step_time_ms == 0U)
    {
        step_time_ms = 1U;
    }

    comp_led_set_mask(0U);
    s_led_mode = EXAM_LED_SEQUENCE;
    s_led_sequence_step = 0U;
    s_led_step_time_ms = step_time_ms;
    s_led_changed_at = HAL_GetTick();
}

static void exam2025_process_leds(void)
{
    uint32_t now = HAL_GetTick();

    if (s_led_mode == EXAM_LED_STARTUP_WAIT)
    {
        if ((uint32_t)(now - s_led_changed_at) >=
            EXAM2025_STARTUP_LED_TIME_MS)
        {
            comp_led_set_mask(EXAM2025_LED_ALL_MASK);
            s_led_mode = EXAM_LED_IDLE;
        }
    }
    else if (s_led_mode == EXAM_LED_SEQUENCE)
    {
        if ((uint32_t)(now - s_led_changed_at) >= s_led_step_time_ms)
        {
            s_led_changed_at = now;
            if (s_led_sequence_step < 4U)
            {
                comp_led_set_mask((uint8_t)((1U <<
                                  (s_led_sequence_step + 1U)) - 1U));
                s_led_sequence_step++;
            }
            if (s_led_sequence_step >= 4U)
            {
                s_led_mode = EXAM_LED_IDLE;
            }
        }
    }
}

static void exam2025_process_uart_line(const uint8_t *data, uint16_t length)
{
    /*
     * ===== 现场改题区：收到上位机字符串后做什么 =====
     * data 是收到的字节，length 是有效字节数。
     * 当前判断收到两个字符 st，然后回复 st is ok。
     */
    if ((length == 2U) && (data[0] == (uint8_t)'s') &&
        (data[1] == (uint8_t)'t'))
    {
        exam2025_uart_send_line("st is ok");
        exam2025_show_uart_status("RX: st / TX: st is ok", GREEN);
    }
}

static void exam2025_process_uart(void)
{
    uint16_t state;
    uint16_t length = 0U;
    uint8_t local[USART_REC_LEN];
    uint8_t ready = 0U;

    /*
     * The original USART driver normally finishes a line on CRLF. For the
     * exact two-byte command "st", this application also accepts no CRLF.
     */
    __disable_irq();
    state = g_usart_rx_sta;
    if ((state & 0x8000U) != 0U)
    {
        length = (uint16_t)(state & 0x3FFFU);
        if (length > USART_REC_LEN)
        {
            length = USART_REC_LEN;
        }
        memcpy(local, g_usart_rx_buf, length);
        g_usart_rx_sta = 0U;
        ready = 1U;
    }
    else if (((state & 0x4000U) == 0U) &&
             ((state & 0x3FFFU) == 2U) &&
             (g_usart_rx_buf[0] == (uint8_t)'s') &&
             (g_usart_rx_buf[1] == (uint8_t)'t'))
    {
        local[0] = (uint8_t)'s';
        local[1] = (uint8_t)'t';
        length = 2U;
        g_usart_rx_sta = 0U;
        ready = 1U;
    }
    __enable_irq();

    if (ready != 0U)
    {
        exam2025_process_uart_line(local, length);
    }
}

static void exam2025_process_key(comp_key_t key)
{
    /*
     * ===== 现场改题区：四个按键分别做什么 =====
     * BOARD_0/BOARD_1 是板载按键1/2；EXT_0/EXT_1 是外接按键3/4。
     * 每个 case 中直接替换为新题要求的函数即可，最后保留 break。
     */
    switch (key)
    {
        case COMP_KEY_BOARD_0:
            /* Q2 starts only after button 1 has been released. */
            s_key1_wait_release = 1U;
            break;

        case COMP_KEY_BOARD_1:
            /* Q4: button 2 sends key2 and reports success on the LCD. */
            exam2025_uart_send_line("key2");
            exam2025_show_send_success();
            break;

        case COMP_KEY_EXT_0:
            /* Q6: button 3 increments aa by 0.5, saves and transmits it. */
            exam2025_set_aa_tenths(
                (int16_t)(s_aa_tenths + EXAM2025_AA_STEP_TENTHS), 1U, 1U);
            break;

        case COMP_KEY_EXT_1:
            /* Q6: button 4 decrements aa by 0.5, saves and transmits it. */
            exam2025_set_aa_tenths(
                (int16_t)(s_aa_tenths - EXAM2025_AA_STEP_TENTHS), 1U, 1U);
            break;

        default:
            break;
    }
}

void exam2025_init(uint8_t nvm_available)
{
    int16_t saved_aa = 0;

    s_nvm_available = (nvm_available != 0U) ? 1U : 0U;
    s_key1_wait_release = 0U;

    /* ===== 现场改题区：上电屏幕内容和初始状态 ===== */
    lcd_clear(WHITE);       /* 先清屏，再显示文字。 */
    comp_hanzi_show_utf8(20U, 20U, COMP_TEXT_ST_EXAM, DARKBLUE, WHITE);
    lcd_show_string(20U, 55U, 430U, 16U, 16U,
                    "KEY1: LED sequence   KEY2: send key2", BLACK);
    lcd_show_string(20U, 80U, 430U, 16U, 16U,
                    "KEY3: aa+0.5         KEY4: aa-0.5", BLACK);

    if (exam2025_load_aa(&saved_aa) != 0U)
    {
        saved_aa = 0;
        exam2025_show_uart_status(
            s_nvm_available ? "NVM: first use, aa=0.0" : "NVM unavailable", RED);
    }
    else
    {
        exam2025_show_uart_status("NVM: restored aa", GREEN);
    }
    exam2025_set_aa_tenths(saved_aa, 0U, 0U);

    /* Q1: LED1 and LED3 are on immediately; all four after 30 seconds. */
    comp_led_set_mask(EXAM2025_LED_1_AND_3_MASK);
    s_led_mode = EXAM_LED_STARTUP_WAIT;
    s_led_changed_at = HAL_GetTick();
    s_led_step_time_ms = EXAM2025_LED_STEP_TIME_MS;
    s_led_sequence_step = 0U;

    printf("\r\n2025 exam reference started\r\n");
    printf("UART1 115200 8N1; send st or st+CRLF\r\n");
}

void exam2025_process(void)
{
    comp_key_t key = comp_keys_poll_event();

    if (key != COMP_KEY_NONE)
    {
        exam2025_process_key(key);
    }

    if ((s_key1_wait_release != 0U) &&
        ((comp_keys_read_mask() & EXAM2025_KEY1_MASK) == 0U))
    {
        s_key1_wait_release = 0U;
        exam2025_start_led_sequence(EXAM2025_LED_STEP_TIME_MS);
    }

    exam2025_process_leds();
    exam2025_process_breathing_led();
    exam2025_process_uart();
}
