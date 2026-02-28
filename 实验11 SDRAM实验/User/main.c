/**
 ****************************************************************************************************
 * @file        main.c
 * @version     V1.0
 * @brief       SDRAM 实验
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    STM32H743IIT6小系统板
 *
 ****************************************************************************************************
 */
 
#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/KEY/key.h"
#include "./BSP/MPU/mpu.h"
#include "./BSP/SDRAM/sdram.h"


/* 测试用数组, 起始地址为: BANK5_SDRAM_ADDR */
#if !(__ARMCC_VERSION >= 6010050)                             /* 不是AC6编译器，即使用AC5编译器时 */

uint32_t testsdram[250000] __attribute__((at(0XC0000000)));   

#else      /* 使用AC6编译器时 */

uint32_t testsdram[250000] __attribute__((section(".bss.ARM.__at_0xC0000000")));

#endif

/**
 * @brief       SDRAM内存测试
 * @param       无
 * @retval      无
 */
void sdram_test(void)
{
    uint32_t i = 0;
    uint32_t temp = 0;
    uint32_t sval = 0;   /* 在地址0读到的数据 */

    /* 每隔16K字节,写入一个数据,总共写入2048个数据,刚好是32M字节 */
    for (i = 0; i < 32 * 1024 * 1024; i += 16 * 1024)
    {
        *(volatile uint32_t *)(BANK5_SDRAM_ADDR + i) = temp;
        temp++;
    }

    /* 依次读出之前写入的数据,进行校验 */
    for (i = 0; i < 32 * 1024 * 1024; i += 16 * 1024)
    {   
        temp = *(volatile uint32_t *)(BANK5_SDRAM_ADDR + i);
         
        if (i == 0)
        {
            sval = temp;
        }
        else if (temp <= sval)
        {
            break;     /* 后面读出的数据一定要比第一次读到的数据大 */
        }
        
        printf("SDRAM Capacity:%dKB\r\n", (temp - sval + 1) * 16);      /* 打印SDRAM容量 */
    }
}

int main(void)
{
    uint8_t key;
    uint8_t i = 0;
    uint32_t ts = 0;
  
    sys_cache_enable();                     /* 使能L1-Cache */
    HAL_Init();                             /* 初始化HAL库 */
    sys_stm32_clock_init(192, 5, 2, 4);     /* 设置时钟, 480Mhz */
    delay_init(480);                        /* 延时初始化 */
    usart_init(115200);                     /* 初始化USART */  
    led_init();                             /* 初始化LED */
    mpu_memory_protection();                /* 保护相关存储区域 */
    key_init();                             /* 初始化按键 */
    sdram_init();                           /* 初始化SDRAM */

    delay_ms(1000);
    
    for (ts = 0; ts < 250000; ts++)
    {
        testsdram[ts] = ts;                 /* 预存测试数据 */
    }

    printf("\r\n\r\nSDRAM TEST\r\n");     
  
    while (1)
    {
        key = key_scan(0);                  /* 不支持连按 */

        if (key == KEY0_PRES)
        {
            sdram_test();                   /* 测试SDRAM容量 */
        }
        else if (key == WKUP_PRES)          
        {
            for (ts = 0; ts < 250000; ts++)
            {   
                printf("testsdram[%d]:%d\r\n", ts, testsdram[ts]);     /* 打印预存测试数据 */
            }
        }
        else
        {
            delay_ms(10);
        }

        i++;

        if (i == 20)
        {
            i = 0;
            LED0_TOGGLE();                  /* LED0闪烁 */     
        }
    }
}




