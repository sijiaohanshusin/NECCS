#ifndef __COMP_HANZI_H
#define __COMP_HANZI_H

#include "./SYSTEM/sys/sys.h"

/*
 * 16x16 UTF-8 Chinese text helper for the existing TFTLCD driver.
 * ASCII characters use the original 8x16 LCD font; included Chinese
 * characters use the small built-in glyph table in comp_hanzi.c.
 */

/* Frequently used phrases encoded explicitly as UTF-8 bytes. */
#define COMP_TEXT_ST_EXAM       "st\xE6\xB5\x8B\xE8\xAF\x84\xE9\xA2\x98"
#define COMP_TEXT_SEND_OK       "\xE5\x8F\x91\xE9\x80\x81\xE6\x88\x90\xE5\x8A\x9F"
#define COMP_TEXT_COMPETITION   "\xE6\xAF\x94\xE8\xB5\x9B"
#define COMP_TEXT_TRACK         "\xE8\xB5\x9B\xE9\x81\x93"
#define COMP_TEXT_EMBEDDED      "\xE5\xB5\x8C\xE5\x85\xA5\xE5\xBC\x8F"
#define COMP_TEXT_CHIP          "\xE8\x8A\xAF\xE7\x89\x87"
#define COMP_TEXT_SYSTEM        "\xE7\xB3\xBB\xE7\xBB\x9F"
#define COMP_TEXT_DESIGN        "\xE8\xAE\xBE\xE8\xAE\xA1"
#define COMP_TEXT_LIGHT_UP      "\xE7\x82\xB9\xE4\xBA\xAE"
#define COMP_TEXT_ENABLE        "\xE8\xB5\x8B\xE8\x83\xBD"
#define COMP_TEXT_UNIVERSITY    "\xE5\x85\xA8\xE5\x9B\xBD\xE5\xA4\xA7\xE5\xAD\xA6\xE7\x94\x9F"
#define COMP_TEXT_DATA          "\xE6\x95\xB0\xE6\x8D\xAE"
#define COMP_TEXT_STORAGE       "\xE5\xAD\x98\xE5\x82\xA8"
#define COMP_TEXT_READ          "\xE8\xAF\xBB\xE5\x8F\x96"
#define COMP_TEXT_WRITE         "\xE5\x86\x99\xE5\x85\xA5"
#define COMP_TEXT_FAILURE       "\xE5\xA4\xB1\xE8\xB4\xA5"
#define COMP_TEXT_START         "\xE5\xBC\x80\xE5\xA7\x8B"
#define COMP_TEXT_STOP          "\xE5\x81\x9C\xE6\xAD\xA2"
#define COMP_TEXT_INCREASE      "\xE5\xA2\x9E\xE5\x8A\xA0"
#define COMP_TEXT_DECREASE      "\xE5\x87\x8F\xE5\xB0\x91"
#define COMP_TEXT_VALUE         "\xE6\x95\xB0\xE5\x80\xBC"
#define COMP_TEXT_POWER         "\xE7\x94\xB5\xE6\xBA\x90"
#define COMP_TEXT_INIT          "\xE5\x88\x9D\xE5\xA7\x8B\xE5\x8C\x96"
#define COMP_TEXT_CURRENT       "\xE5\xBD\x93\xE5\x89\x8D"
#define COMP_TEXT_STATUS        "\xE7\x8A\xB6\xE6\x80\x81"
#define COMP_TEXT_WAIT          "\xE7\xAD\x89\xE5\xBE\x85"
#define COMP_TEXT_COMPLETE      "\xE5\xAE\x8C\xE6\x88\x90"

/**
 * @brief  在 TFTLCD 上显示一行 UTF-8 文本（ASCII + 已内置的汉字）。
 * @param  x,y: 左上角坐标，单位为像素。
 * @param  text: 以 '\0' 结束的 UTF-8 字符串。
 * @param  foreground: 文字颜色，例如 BLACK、BLUE。
 * @param  background: 文字背景颜色，例如 WHITE。
 * @note   ASCII 占 8x16 像素，汉字占 16x16 像素；未收录汉字显示方框。
 */
void comp_hanzi_show_utf8(uint16_t x,
                          uint16_t y,
                          const char *text,
                          uint16_t foreground,
                          uint16_t background);

/**
 * @brief  检查一个 Unicode 汉字是否已经收录。
 * @param  codepoint: Unicode 码点，例如“测”为 0x6D4B。
 * @return 1=已收录，0=未收录。
 */
uint8_t comp_hanzi_has_glyph(uint16_t codepoint);

#endif
