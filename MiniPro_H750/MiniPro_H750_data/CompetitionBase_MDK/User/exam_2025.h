#ifndef __EXAM_2025_H
#define __EXAM_2025_H

#include "./SYSTEM/sys/sys.h"

/* 去年题目的现场可改参数。单位全部写在宏名中。 */
#define EXAM2025_STARTUP_LED_TIME_MS  30000UL
#define EXAM2025_LED_STEP_TIME_MS       500UL
#define EXAM2025_AA_STEP_TENTHS           5

/**
 * @brief  初始化去年综测题应用。
 * @param  nvm_available: comp_nvm_init() 成功时传 1，否则传 0。
 * @note   调用前必须完成 LCD、USART1、comp_led、comp_keys 和 comp_nvm 初始化。
 */
void exam2025_init(uint8_t nvm_available);

/**
 * @brief  去年综测题循环服务函数。
 * @note   必须在 while(1) 内反复调用；函数不使用 30 秒阻塞延时。
 */
void exam2025_process(void);

/**
 * @brief  启动“全灭，然后 LED1~LED4 依次点亮”的非阻塞序列。
 * @param  step_time_ms: 每增加点亮一个 LED 的间隔，单位毫秒。
 */
void exam2025_start_led_sequence(uint32_t step_time_ms);

/**
 * @brief  通过现有 USART1 向上位机发送一行文本。
 * @param  text: 要发送的 ASCII 字符串，函数自动追加 CRLF。
 */
void exam2025_uart_send_line(const char *text);

/**
 * @brief  设置变量 aa 并刷新屏幕，可选择保存和发送。
 * @param  aa_tenths: aa 的 10 倍；例如 1.5 传 15，-0.5 传 -5。
 * @param  save_to_nvm: 1=立即写入 QSPI 非易失存储，0=不保存。
 * @param  send_uart: 1=立即向上位机发送 aa=x.x，0=不发送。
 */
void exam2025_set_aa_tenths(int16_t aa_tenths,
                            uint8_t save_to_nvm,
                            uint8_t send_uart);

/**
 * @brief  读取当前 aa 的 10 倍整数值。
 * @return 例如返回 15 表示 aa=1.5。
 */
int16_t exam2025_get_aa_tenths(void);

#endif
