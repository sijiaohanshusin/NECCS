/**
 * @file    tft_spi.c
 * @brief   RGB 面板初始化 SPI（GPIO Bit-Bang）实现
 * @details
 *   ## 用途
 *   仅用于初始化阶段向面板驱动 IC（NV3052CGRB）写入控制寄存器，
 *   不参与 LTDC 帧数据传输（帧数据由 LTDC + RGB 并行接口传输）。
 *
 *   ## 面板信息
 *   - 驱动 IC：NV3052CGRB（紫光微电子 RGB/SPI 双接口 IC）
 *   - 模组：HSD055BHW5-C（5.5英寸 RGB 面板，1080×1920，此处作 720×1280 使用）
 *   - [注意] 本工程实际使用面板为 7寸 1024×600 WKS7，具体时序在 ltdc.c 中配置，
 *            此 SPI 初始化序列仅用于驱动 IC 唤醒，与面板分辨率无关。
 *
 *   ## SPI 协议（9-bit 模式）
 *   NV3052CGRB 使用 4线 SPI，通信格式为 9bit：
 *   - bit[8]（先发）: D/C 位，0=命令，1=数据
 *   - bit[7:0]: 命令/数据内容
 *   tft_spi_write_cmd → D/C=0 + 8-bit cmd
 *   tft_spi_write_data → D/C=1 + 8-bit data
 *
 *   ## 寄存器页切换
 *   NV3052C 寄存器分页，通过连续写 {0xFF,0x30},{0xFF,0x52},{0xFF,XX} 切换：
 *   - XX=0x00: 标准寄存器页（MIPI/RGB 接口控制）
 *   - XX=0x01: 面板功率/时序控制页
 *   - XX=0x02: Gamma 曲线配置页
 *   - XX=0x03: GOA 信号路由页（Gate Driver On Array）
 *
 *   ## 延时使用
 *   初始化路径调用 HAL_Delay（ms 级），SPI 时序使用忙等 __NOP()（µs 级）。
 *   [改进] tft_delay_us 精度受编译优化影响，建议改用 DWT_Timer_DelayUs。
 */
 
#include "LCD/tft_spi.h"

static void tft_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static void tft_delay_us(uint32_t us)
{
    /* 粗粒度忙等延时，用于初始化序列时序，精度需求不高。 */
    uint32_t loops = (SystemCoreClock / 1000000u / 5u) * us;
    while (loops-- > 0u)
    {
        __NOP();
    }
}

#define delay_ms tft_delay_ms
#define delay_us tft_delay_us


/**
 * @brief       SPI写数据字节（位操作，MSB先发）
 * @note        NV3052CGRB SPI 4线模式，时钟极性 CPOL=0（空闲低），相位 CPHA=0（上升沿采样）。
 *              此函数仅发送8位有效数据；第9位 D/C 由 tft_spi_write_cmd/data 在 CS 下降沿后单独发送。
 * @param       buf: 要写入的数据字节
 * @retval      无
 */
void tft_spi_write_byte(uint8_t buf)
{    
    uint8_t count = 0;

    for (count = 0 ; count < 8 ; count++)
    {        
        if (buf & 0x80)       
        {  
            TFT_SPI_SDA(1);   /* 发送 bit=1（MSB 先）*/
        }
        else                  
        {
            TFT_SPI_SDA(0);   /* 发送 bit=0 */
        }
         
        buf <<= 1;            /* 下一位移至最高位 */
        TFT_SPI_SCL(0);       /* SCL 下降沿：数据建立 */
        delay_us(1);          /* 数据建立时间（≥1 bit 周期）*/
        TFT_SPI_SCL(1);       /* SCL 上升沿：从机采样 */
        delay_us(1);
    }
}

/**
 * @brief       向LCD驱动IC发送命令（9-bit SPI，D/C=0）
 * @details     NV3052C 9-bit SPI 协议：第9位 D/C=0 表示命令，之后8位为命令字节。
 *              CS 拉低后先发 D/C bit，再调用 tft_spi_write_byte 发送8位命令。
 * @param       cmd: 要发送的命令字节
 * @retval      无
 */
void tft_spi_write_cmd(uint8_t cmd)
{
    TFT_SPI_CS(0);       /* CS低：选中从机，开始传输 */
    TFT_SPI_SDA(0);      /* D/C=0：命令模式（第9 bit）*/
    TFT_SPI_SCL(0);      /* SCL低：产生下降沿 */
    TFT_SPI_SCL(1);      /* SCL高：D/C bit 上升沿采样 */
    delay_us(2);
    tft_spi_write_byte(cmd);  /* 发送8位命令内容 */
    TFT_SPI_CS(1);       /* CS高：结束传输 */
}  

/**
 * @brief       向LCD驱动IC发送数据（9-bit SPI，D/C=1）
 * @details     NV3052C 9-bit SPI 协议：第9位 D/C=1 表示数据参数，之后8位为参数字节。
 *              通常在 tft_spi_write_cmd 之后调用，传递该命令的参数。
 * @param       data: 要发送的数据字节
 * @retval      无
 */
void tft_spi_write_data(uint8_t data)
{
    TFT_SPI_CS(0);       /* CS低：选中从机 */
    TFT_SPI_SDA(1);      /* D/C=1：数据模式（第9 bit）*/
    TFT_SPI_SCL(0);
    TFT_SPI_SCL(1);      /* D/C bit 上升沿采样 */
    delay_us(2);
    tft_spi_write_byte(data);  /* 发送8位数据参数 */
    TFT_SPI_CS(1);       /* CS高：结束传输 */
} 

/**
 * @brief       向LCD驱动IC写寄存器（命令+1字节参数）
 * @details     封装 tft_spi_write_cmd + tft_spi_write_data，实现"寄存器编号→参数值"的标准写操作。
 *              InitTable 中的每一行 {reg, data} 均通过此函数写入。
 * @param       reg : 寄存器编号（命令字节，D/C=0）
 * @param       data: 寄存器参数值（数据字节，D/C=1）
 * @retval      无
 */
void tft_spi_write_reg(uint8_t reg, uint8_t data)
{ 
    tft_spi_write_cmd(reg);       /* 发送寄存器地址/命令，D/C=0 */
    tft_spi_write_data(data);     /* 发送寄存器参数值，D/C=1 */
}

/**
 * @brief       TFTLCD SPI 接口初始化（NV3052CGRB IC + HSD055BHW5-C 面板）
 * @details     执行步骤：
 *              1. 使能4个 GPIO 时钟，配置 CS/SCL/SDA/RST 为 PP 推挽高速输出。
 *              2. 硬件复位：RST 高→低→高，保证 IC 处于已知初始状态。
 *              3. 按顺序写入4个寄存器页的配置参数：
 *                 - Page 1 (0xFF=0x01)：功率/时序控制
 *                 - Page 2 (0xFF=0x02)：Gamma 曲线
 *                 - Page 3 (0xFF=0x03)：GOA 信号路由
 *                 - Page 0 (0xFF=0x00)：标准接口控制
 *              4. Sleep Out (0x11) + 200 ms 延时，再 Display On (0x29) + 100 ms 延时。
 * @note        [注意] 切换页需固定前导序列 {0xFF,0x30},{0xFF,0x52},{0xFF,PGx}，
 *              缺少任何一步将导致后续寄存器写入无效。
 * @note        [注意] 初始化仅在上电阶段执行一次；不应在运行时重复调用。
 * @note        [改进] GPIO_SPEED_FREQ_VERY_HIGH 对软件 bit-bang 有意义，
 *              但实际速率受循环指令数限制（约数 MHz），可考虑用 HAL_SPI 硬件替代以提升速率。
 * @param       无
 * @retval      无
 */
void tft_spi_init(void)
{    
    GPIO_InitTypeDef gpio_init_struct;

    /* ---------- 步骤1：GPIO 时钟使能 + 引脚初始化 ---------- */
    TFT_SPI_CS_GPIO_CLK_ENABLE();      /* CS 引脚所在端口时钟使能 */
    TFT_SPI_SCL_GPIO_CLK_ENABLE();     /* SCL 引脚所在端口时钟使能 */
    TFT_SPI_SDA_GPIO_CLK_ENABLE();     /* SDA 引脚所在端口时钟使能 */
    TFT_SPI_RST_GPIO_CLK_ENABLE();     /* RST 引脚所在端口时钟使能 */

    /* 公共 GPIO 参数：推挽输出、上拉、极高速度 */
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;                 /* 推挽输出：驱动能力强 */
    gpio_init_struct.Pull = GPIO_PULLUP;                         /* 上拉：总线空闲保持确定态 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;          /* 极高速：降低 bit-bang 上升时间 */

    gpio_init_struct.Pin = TFT_SPI_CS_GPIO_PIN;
    HAL_GPIO_Init(TFT_SPI_CS_GPIO_PORT, &gpio_init_struct);      /* CS 初始化 */

    gpio_init_struct.Pin = TFT_SPI_SCL_GPIO_PIN;
    HAL_GPIO_Init(TFT_SPI_SCL_GPIO_PORT, &gpio_init_struct);     /* SCL 初始化 */
  
    gpio_init_struct.Pin = TFT_SPI_SDA_GPIO_PIN;
    HAL_GPIO_Init(TFT_SPI_SDA_GPIO_PORT, &gpio_init_struct);     /* SDA 初始化 */
    
    gpio_init_struct.Pin = TFT_SPI_RST_GPIO_PIN;
    HAL_GPIO_Init(TFT_SPI_RST_GPIO_PORT, &gpio_init_struct);     /* RST 初始化 */
        
	  delay_ms(50);      /* 等待 GPIO 稳定，NV3052C 上电 VCC 建立时间 ≥ 10 ms */
     
    /* ---------- 步骤2：硬件复位序列 ----------
     * NV3052C 复位时序要求：RST 低电平宽度 ≥ 10 µs；低->高后等待 ≥ 5 ms 才可发指令。
     * 此处保守取 RST=1(10ms) → RST=0(50ms 低脉冲) → RST=1(200ms 等初始化完成)。
     */
	  TFT_SPI_RST(1);    /* RST 高：初始高电平（上电默认） */
	  delay_ms(10);      /* 稳定 10 ms */
	  TFT_SPI_RST(0);    /* RST 拉低：开始硬件复位 */
	  delay_ms(50);      /* 低电平保持 50 ms（远超 IC 要求，确保复位有效） */
	  TFT_SPI_RST(1);    /* RST 释放：IC 开始内部初始化 */
		delay_ms(200);   /* 等待 IC 内部复位流程完成（最少 5 ms，取 200 ms 保守值） */

    /* ================================================================
     * NV3052CGRB 寄存器初始化序列
     * 切换寄存器页固定前导：{0xFF,0x30} + {0xFF,0x52} + {0xFF,页号}
     * ================================================================ */

    /* ---- Page 1：功率/时序控制寄存器 ---- */
    tft_spi_write_reg(0xFF, 0x30);   /* 页切换前导 #1（固定值） */
    tft_spi_write_reg(0xFF, 0x52);   /* 页切换前导 #2（固定值） */
    tft_spi_write_reg(0xFF, 0x01);   /* 切换到 Page 1：电源/时序控制 */
    tft_spi_write_reg(0xE3, 0x00);
    tft_spi_write_reg(0xF6, 0xC0);
    tft_spi_write_reg(0xF0, 0x00);
    tft_spi_write_reg(0x0a, 0x00);  
    
    tft_spi_write_reg(0x23, 0xA2);  /* [注意] 原注释 "//A0"，实际值 0xA2，如与 DC/DC 相关需确认 */
    
    tft_spi_write_reg(0x24, 0x10);
    tft_spi_write_reg(0x25, 0x0a);
    tft_spi_write_reg(0x26, 0x2E);
    tft_spi_write_reg(0x27, 0x2E);
    tft_spi_write_reg(0x29, 0x04);
    tft_spi_write_reg(0x2A, 0xFF);
    tft_spi_write_reg(0x38, 0x9C);
    tft_spi_write_reg(0x39, 0xA7);
    tft_spi_write_reg(0x3A, 0x5E);  /* VCOM 电压调节（[改进] 值可根据实际面板调整以优化对比度） */
    tft_spi_write_reg(0x49, 0x3C);
    tft_spi_write_reg(0x91, 0x67);
    tft_spi_write_reg(0x92, 0x67);
    tft_spi_write_reg(0xA0, 0x55);
    tft_spi_write_reg(0xA1, 0x50);
    tft_spi_write_reg(0xA4, 0x9C);
    tft_spi_write_reg(0xA7, 0x02);
    tft_spi_write_reg(0xA8, 0x01);
    tft_spi_write_reg(0xA9, 0x01);
    tft_spi_write_reg(0xAA, 0xFC);
    tft_spi_write_reg(0xAB, 0x28);
    tft_spi_write_reg(0xAC, 0x06);
    tft_spi_write_reg(0xAD, 0x06);
    tft_spi_write_reg(0xAE, 0x06);
    tft_spi_write_reg(0xAF, 0x03);
    tft_spi_write_reg(0xB0, 0x08);
    tft_spi_write_reg(0xB1, 0x26);
    tft_spi_write_reg(0xB2, 0x28);
    tft_spi_write_reg(0xB3, 0x28);
    tft_spi_write_reg(0xB4, 0x03);
    tft_spi_write_reg(0xB5, 0x08);
    tft_spi_write_reg(0xB6, 0x26);
    tft_spi_write_reg(0xB7, 0x08);
    tft_spi_write_reg(0xB8, 0x26);

    /* ---- Page 2：Gamma 曲线校正寄存器 ---- */
    tft_spi_write_reg(0xFF, 0x30);   /* 页切换前导 #1 */
    tft_spi_write_reg(0xFF, 0x52);   /* 页切换前导 #2 */
    tft_spi_write_reg(0xFF, 0x02);   /* 切换到 Page 2：正/负 Gamma 曲线拐点 */
    /* Bx 为正极性 Gamma，Dx 为负极性 Gamma，成对调整 */
    tft_spi_write_reg(0xB1, 0x11);    
    tft_spi_write_reg(0xD1, 0x15);    
    tft_spi_write_reg(0xB4, 0x2F);    
    tft_spi_write_reg(0xD4, 0x31);    
    tft_spi_write_reg(0xB2, 0x13);    
    tft_spi_write_reg(0xD2, 0x15);    
    tft_spi_write_reg(0xB3, 0x2D);    
    tft_spi_write_reg(0xD3, 0x33);       
    tft_spi_write_reg(0xB6, 0x22);    
    tft_spi_write_reg(0xD6, 0x24);    
    tft_spi_write_reg(0xB7, 0x3D);    
    tft_spi_write_reg(0xD7, 0x41);    
    tft_spi_write_reg(0xC1, 0x08);    
    tft_spi_write_reg(0xE1, 0x08);    
    tft_spi_write_reg(0xB8, 0x0D);    
    tft_spi_write_reg(0xD8, 0x0D);    
    tft_spi_write_reg(0xB9, 0x04);    
    tft_spi_write_reg(0xD9, 0x04);    
    tft_spi_write_reg(0xBD, 0x15);    
    tft_spi_write_reg(0xDD, 0x15);    
    tft_spi_write_reg(0xBC, 0x13);    
    tft_spi_write_reg(0xDC, 0x13);    
    tft_spi_write_reg(0xBB, 0x11);    
    tft_spi_write_reg(0xDB, 0x11);    
    tft_spi_write_reg(0xBA, 0x11);    
    tft_spi_write_reg(0xDA, 0x11);    
    tft_spi_write_reg(0xBE, 0x17);    
    tft_spi_write_reg(0xDE, 0x19);    
    tft_spi_write_reg(0xBF, 0x0F);    
    tft_spi_write_reg(0xDF, 0x11);    
    tft_spi_write_reg(0xC0, 0x16);    
    tft_spi_write_reg(0xE0, 0x18);    
    tft_spi_write_reg(0xB5, 0x37);    
    tft_spi_write_reg(0xD5, 0x34);    
    tft_spi_write_reg(0xB0, 0x02);    
    tft_spi_write_reg(0xD0, 0x05);    

    /* ---- Page 3：GOA 信号路由（Gate Output Array）---- */
    tft_spi_write_reg(0xFF, 0x30);   /* 页切换前导 #1 */
    tft_spi_write_reg(0xFF, 0x52);   /* 页切换前导 #2 */
    tft_spi_write_reg(0xFF, 0x03);   /* 切换到 Page 3：GOA 时序/信号路由，与面板驱动电路强绑定 */
    /* [注意] Page 3 寄存器值与面板 HSD055BHW5-C 的 GOA 走线方案强相关，
     *        更换面板时必须同步替换此段参数 */
    tft_spi_write_reg(0x05, 0x00);   
    tft_spi_write_reg(0x06, 0x00);   
    tft_spi_write_reg(0x08, 0x06);   
    tft_spi_write_reg(0x09, 0x07);   
    tft_spi_write_reg(0x25, 0x32);   
    tft_spi_write_reg(0x2A, 0x0a);   
    tft_spi_write_reg(0x2B, 0x0b);   
    tft_spi_write_reg(0x70, 0x0f);   
    tft_spi_write_reg(0x71, 0xc0);   
    tft_spi_write_reg(0x30, 0x2A);   
    tft_spi_write_reg(0x31, 0x2A);   
    tft_spi_write_reg(0x32, 0x2A);   
    tft_spi_write_reg(0x33, 0x2A);   
    tft_spi_write_reg(0x34, 0xb1);   
    tft_spi_write_reg(0x35, 0x76);   
    tft_spi_write_reg(0x36, 0x08);   
    tft_spi_write_reg(0x40, 0x07);   
    tft_spi_write_reg(0x41, 0x08);   
    tft_spi_write_reg(0x42, 0x09);   
    tft_spi_write_reg(0x43, 0x0a);   
    tft_spi_write_reg(0x45, 0x04);   
    tft_spi_write_reg(0x46, 0x05);   
    tft_spi_write_reg(0x48, 0x06);   
    tft_spi_write_reg(0x49, 0x07);   
    tft_spi_write_reg(0x50, 0x0b);   
    tft_spi_write_reg(0x51, 0x0c);   
    tft_spi_write_reg(0x52, 0x0d);   
    tft_spi_write_reg(0x53, 0x0e);   
    tft_spi_write_reg(0x55, 0x08);   
    tft_spi_write_reg(0x56, 0x09);   
    tft_spi_write_reg(0x58, 0x0a);   
    tft_spi_write_reg(0x59, 0x0b);   
    tft_spi_write_reg(0x80, 0x1f);   
    tft_spi_write_reg(0x81, 0x00);   
    tft_spi_write_reg(0x82, 0x16);   
    tft_spi_write_reg(0x83, 0x15);   
    tft_spi_write_reg(0x84, 0x0a);   
    tft_spi_write_reg(0x85, 0x0c);   
    tft_spi_write_reg(0x86, 0x0e);   
    tft_spi_write_reg(0x87, 0x10);   
    tft_spi_write_reg(0x88, 0x02);   
    tft_spi_write_reg(0x8F, 0x06);   
    tft_spi_write_reg(0x96, 0x1f);   
    tft_spi_write_reg(0x97, 0x00);   
    tft_spi_write_reg(0x98, 0x18);   
    tft_spi_write_reg(0x99, 0x17);   
    tft_spi_write_reg(0x9A, 0x09);   
    tft_spi_write_reg(0x9B, 0x0b);   
    tft_spi_write_reg(0x9C, 0x0d);   
    tft_spi_write_reg(0x9D, 0x0f);   
    tft_spi_write_reg(0x9E, 0x01);   
    tft_spi_write_reg(0xA5, 0x05);   
    tft_spi_write_reg(0xB0, 0x00);   
    tft_spi_write_reg(0xB1, 0x1f);   
    tft_spi_write_reg(0xB2, 0x16);   
    tft_spi_write_reg(0xB3, 0x15);   
    tft_spi_write_reg(0xB4, 0x0f);   
    tft_spi_write_reg(0xB5, 0x0d);   
    tft_spi_write_reg(0xB6, 0x0b);   
    tft_spi_write_reg(0xB7, 0x09);   
    tft_spi_write_reg(0xB8, 0x05);   
    tft_spi_write_reg(0xBF, 0x01);   
    tft_spi_write_reg(0xC6, 0x00);   
    tft_spi_write_reg(0xC7, 0x1f);   
    tft_spi_write_reg(0xC8, 0x18);   
    tft_spi_write_reg(0xC9, 0x17);   
    tft_spi_write_reg(0xCA, 0x10);   
    tft_spi_write_reg(0xCB, 0x0e);   
    tft_spi_write_reg(0xCC, 0x0c);   
    tft_spi_write_reg(0xCD, 0x0a);   
    tft_spi_write_reg(0xCE, 0x06);   
    tft_spi_write_reg(0xD5, 0x02);   

    /* ---- Page 0：标准接口控制寄存器 ---- */
    tft_spi_write_reg(0xFF, 0x30);   /* 页切换前导 #1 */
    tft_spi_write_reg(0xFF, 0x52);   /* 页切换前导 #2 */
    tft_spi_write_reg(0xFF, 0x00);   /* 切换到 Page 0：MIPI/标准接口通用命令集 */
    tft_spi_write_reg(0x36, 0x0a);   /* MADCTL：内存访问控制，0x0A = BGR+列翻转（适配本板排布） */
    tft_spi_write_reg(0x11, 0x00);   /* Sleep Out (0x11)：退出 Sleep 模式，参数固定 0x00 */
    delay_ms(200);                   /* 等待 Sleep Out 完成；NV3052C 规格要求 ≥ 120 ms，取 200 ms */
    tft_spi_write_reg(0x29, 0x00);   /* Display On (0x29)：打开显示输出，参数固定 0x00 */
    delay_ms(100);                   /* 等待显示电路稳定、背光同步 */
}




