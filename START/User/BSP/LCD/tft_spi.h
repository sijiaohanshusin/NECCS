/**
 ****************************************************************************************************
 * @file        tft_spi.h
 * @version     V1.0
 * @brief       TFTLCD(RGB屏) SPI接口 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    STM32H743IIT6小系统板
 *
 ****************************************************************************************************
 */

#ifndef __TFT_SPI_H
#define __TFT_SPI_H

#include "main.h"


/******************************************************************************************/ 
/* TFT_SPI 相关引脚 定义 */
/* 引脚对应关系: 
 *         TFT_SPI_CS  --> TP_MISO,    TFT_SPI_SCL --> LTDC_VSYNC
 *         TFT_SPI_SDA --> LTDC_HSYNC, TFT_SPI_RST --> LCD_RST 
 */

#define TFT_SPI_CS_GPIO_PORT                GPIOB
#define TFT_SPI_CS_GPIO_PIN                 GPIO_PIN_15
#define TFT_SPI_CS_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB口时钟使能 */

#define TFT_SPI_SCL_GPIO_PORT               GPIOI
#define TFT_SPI_SCL_GPIO_PIN                GPIO_PIN_9
#define TFT_SPI_SCL_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)   /* PI口时钟使能 */

#define TFT_SPI_SDA_GPIO_PORT               GPIOI
#define TFT_SPI_SDA_GPIO_PIN                GPIO_PIN_10
#define TFT_SPI_SDA_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)   /* PI口时钟使能 */

#define TFT_SPI_RST_GPIO_PORT               GPIOD
#define TFT_SPI_RST_GPIO_PIN                GPIO_PIN_11
#define TFT_SPI_RST_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)   /* PD口时钟使能 */

/******************************************************************************************/

/* TFTLCD SPI接口引脚IO操作函数 定义 */
#define TFT_SPI_CS(x)        do{ x ? \
                                 HAL_GPIO_WritePin(TFT_SPI_CS_GPIO_PORT, TFT_SPI_CS_GPIO_PIN, GPIO_PIN_SET) : \
                                 HAL_GPIO_WritePin(TFT_SPI_CS_GPIO_PORT, TFT_SPI_CS_GPIO_PIN, GPIO_PIN_RESET); \
                             }while(0)      /* 片选引脚CS */

#define TFT_SPI_SCL(x)        do{ x ? \
                                 HAL_GPIO_WritePin(TFT_SPI_SCL_GPIO_PORT, TFT_SPI_SCL_GPIO_PIN, GPIO_PIN_SET) : \
                                 HAL_GPIO_WritePin(TFT_SPI_SCL_GPIO_PORT, TFT_SPI_SCL_GPIO_PIN, GPIO_PIN_RESET); \
                             }while(0)      /* 时钟信号引脚SCL */
                             
#define TFT_SPI_SDA(x)        do{ x ? \
                                 HAL_GPIO_WritePin(TFT_SPI_SDA_GPIO_PORT, TFT_SPI_SDA_GPIO_PIN, GPIO_PIN_SET) : \
                                 HAL_GPIO_WritePin(TFT_SPI_SDA_GPIO_PORT, TFT_SPI_SDA_GPIO_PIN, GPIO_PIN_RESET); \
                             }while(0)      /* 数据输出引脚SDA */

/* LCD复位引脚 */
#define TFT_SPI_RST(x)       do{ x ? \
                                 HAL_GPIO_WritePin(TFT_SPI_RST_GPIO_PORT, TFT_SPI_RST_GPIO_PIN, GPIO_PIN_SET) : \
                                 HAL_GPIO_WritePin(TFT_SPI_RST_GPIO_PORT, TFT_SPI_RST_GPIO_PIN, GPIO_PIN_RESET); \
                             }while(0)

/******************************************************************************************/
/* 函数声明 */

void tft_spi_init(void);                             /* TFTLCD SPI接口初始化 */
                             
void tft_spi_write_byte(uint8_t buf);                /* SPI写入1字节数据 */
void tft_spi_write_cmd(uint8_t cmd);                 /* 向LCD驱动IC写命令 */
void tft_spi_write_data(uint8_t data);               /* 向LCD驱动IC写数据 */
void tft_spi_write_reg(uint8_t reg, uint8_t data);   /* 向LCD驱动IC写寄存器的值 */
                             
                             
#endif





