#include "stdio.h"
#include "./BSP/COMPETITION/comp_ui.h"
#include "./BSP/LCD/lcd.h"

static void comp_ui_value(uint16_t y, const char *text)
{
    lcd_fill(150, y, 310, y + 20, WHITE);
    lcd_show_string(150, y, 160, 16, 16, (char *)text, BLUE);
}

void comp_ui_init(uint16_t lcd_id, uint16_t flash_id, uint8_t flash_ok)
{
    char text[32];

    lcd_clear(WHITE);
    lcd_show_string(20, 20, 360, 32, 32, "NECCS H750 BASE", DARKBLUE);
    lcd_show_string(20, 70, 360, 16, 16, "LCD ID:", BLACK);
    sprintf(text, "0x%04X", lcd_id);
    comp_ui_value(70, text);

    lcd_show_string(20, 100, 360, 16, 16, "QSPI ID:", BLACK);
    sprintf(text, "0x%04X %s", flash_id, flash_ok ? "OK" : "FAIL");
    comp_ui_value(100, text);

    lcd_show_string(20, 130, 360, 16, 16, "Light:", BLACK);
    lcd_show_string(20, 160, 360, 16, 16, "Last key:", BLACK);
    lcd_show_string(20, 190, 360, 16, 16, "LED mask:", BLACK);
    lcd_show_string(20, 230, 400, 16, 16, "KEY0: LEDs   KEY1: Beep", RED);
    lcd_show_string(20, 255, 430, 16, 16, "EXT0: PWM    EXT1: PWM Stop", RED);
}

void comp_ui_show_light(uint8_t percent)
{
    char text[20];
    sprintf(text, "%3u %%", percent);
    comp_ui_value(130, text);
}

void comp_ui_show_key(uint8_t key_code)
{
    char text[20];
    sprintf(text, "%u", key_code);
    comp_ui_value(160, text);
}

void comp_ui_show_led_mask(uint8_t mask)
{
    char text[20];
    sprintf(text, "0x%X", mask & 0x0FU);
    comp_ui_value(190, text);
}
