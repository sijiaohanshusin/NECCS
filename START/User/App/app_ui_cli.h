/**
 * @file    app_ui_cli.h
 * @brief   UI UART CLI interfaces
 */
#ifndef APP_UI_CLI_H
#define APP_UI_CLI_H

#include "stm32h7xx_hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

void App_UiCli_Poll(void);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_CLI_H */
