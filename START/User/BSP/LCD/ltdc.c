/**
 ****************************************************************************************************
 * @file        ltdc.c
 * @version     V1.0
 * @brief       LTDC 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    STM32H743IIT6小系统板
 *
 ****************************************************************************************************
 */

#include "LCD/ltdc.h"
#include "LCD/lcd.h"
#include "LCD/tft_spi.h"


LTDC_HandleTypeDef  g_ltdc_handle;       /* LTDC 句柄 */
DMA2D_HandleTypeDef g_dma2d_handle;      /* DMA2D 句柄 */

static uint8_t s_ltdc_dwt_ready = 0u;

static void ltdc_dwt_init(void)
{
    if (s_ltdc_dwt_ready != 0u)
    {
        return;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0u;
    s_ltdc_dwt_ready = 1u;
}

static void ltdc_delay_us(uint32_t us)
{
    uint32_t ticks;
    uint32_t start;

    if (us == 0u)
    {
        return;
    }

    ltdc_dwt_init();
    ticks = (SystemCoreClock / 1000000u) * us;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { }
}

static void ltdc_delay_ms(uint32_t ms)
{
    /* Keep LTDC reset delay independent from RTOS tick to avoid init stalls. */
    while (ms-- > 0u)
    {
        ltdc_delay_us(1000u);
    }
}

#define delay_ms ltdc_delay_ms
#define delay_us ltdc_delay_us

uint32_t *g_ltdc_framebuf[2];              /* 帧缓冲地址 */
_ltdc_dev lcdltdc;                         /* LTDC 设备参数 */
volatile uint32_t g_ltdc_init_stage = 0u;
volatile uint32_t g_ltdc_dma2d_timeout_count = 0u;
volatile uint32_t g_ltdc_dma2d_transfer_count = 0u;
volatile uint32_t g_ltdc_dma2d_sw_fallback_count = 0u;
volatile uint16_t g_ltdc_panel_id = 0u;

#define LTDC_DMA2D_TIMEOUT_LOOP   0X1FFFFFU

static void ltdc_fill_sw_rect(uint32_t psx, uint32_t psy, uint32_t pex, uint32_t pey, uint32_t color)
{
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888
    uint32_t *base = (uint32_t *)g_ltdc_framebuf[lcdltdc.activelayer];
    for (uint32_t y = psy; y <= pey; y++)
    {
        uint32_t *row = base + y * lcdltdc.pwidth + psx;
        for (uint32_t x = psx; x <= pex; x++)
        {
            *row++ = color;
        }
    }
#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888
    uint8_t r = (uint8_t)((color >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(color & 0xFFu);
    uint8_t *base = (uint8_t *)g_ltdc_framebuf[lcdltdc.activelayer];
    for (uint32_t y = psy; y <= pey; y++)
    {
        uint8_t *row = base + (y * lcdltdc.pwidth + psx) * 3u;
        for (uint32_t x = psx; x <= pex; x++)
        {
            row[0] = b;
            row[1] = g;
            row[2] = r;
            row += 3;
        }
    }
#else
    uint16_t c565 = (uint16_t)color;
    uint16_t *base = (uint16_t *)g_ltdc_framebuf[lcdltdc.activelayer];
    for (uint32_t y = psy; y <= pey; y++)
    {
        uint16_t *row = base + y * lcdltdc.pwidth + psx;
        for (uint32_t x = psx; x <= pex; x++)
        {
            *row++ = c565;
        }
    }
#endif
}

static void ltdc_color_fill_sw_rect(uint32_t psx, uint32_t psy, uint32_t pex, uint32_t pey, const uint16_t *color)
{
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB565
    uint32_t width = pex - psx + 1u;
    uint16_t *base = (uint16_t *)g_ltdc_framebuf[lcdltdc.activelayer];

    for (uint32_t y = psy; y <= pey; y++)
    {
        uint16_t *row = base + y * lcdltdc.pwidth + psx;
        for (uint32_t x = 0u; x < width; x++)
        {
            row[x] = *color++;
        }
    }
#else
    (void)psx;
    (void)psy;
    (void)pex;
    (void)pey;
    (void)color;
#endif
}

static uint8_t ltdc_dma2d_wait_complete(void)
{
    uint32_t timeout = 0u;

    while (1)
    {
        uint32_t isr = DMA2D->ISR;

        if ((isr & DMA2D_FLAG_TC) != 0u)
        {
            DMA2D->IFCR = DMA2D_FLAG_TC;
            return 0u;
        }

        if ((isr & (DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE)) != 0u)
        {
            g_ltdc_dma2d_timeout_count++;
            DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
            return 1u;
        }

        timeout++;
        if (timeout > LTDC_DMA2D_TIMEOUT_LOOP)
        {
            g_ltdc_dma2d_timeout_count++;
            DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
            return 1u;
        }
    }
}


/**
 * @brief       LTDC 总开关
 * @param       sw         1:使能 0:关闭
 * @retval      无
 */
void ltdc_switch(uint8_t sw)
{
    if (sw)
    {
        __HAL_LTDC_ENABLE(&g_ltdc_handle);   /* 打开LTDC */
    }
    else
    {
        __HAL_LTDC_DISABLE(&g_ltdc_handle);  /* 关闭LTDC */
    }
}

/**
 * @brief       图层开关
 * @param       layerx     图层号
 * @param       sw         1:使能 0:关闭
 * @retval      无
 */
void ltdc_layer_switch(uint8_t layerx, uint8_t sw)
{
    if (sw) 
    {
        __HAL_LTDC_LAYER_ENABLE(&g_ltdc_handle, layerx);   /* 开启layerx */
    }
    else
    {
        __HAL_LTDC_LAYER_DISABLE(&g_ltdc_handle, layerx);  /* 关闭layerx */
    }

    __HAL_LTDC_RELOAD_CONFIG(&g_ltdc_handle);              /* 立即重新加载配置 */
}

/**
 * @brief       选择当前活动图层
 * @param       layerx     图层号
 * @retval      无
 */
void ltdc_select_layer(uint8_t layerx)
{
    lcdltdc.activelayer = layerx;
}

/**
 * @brief       设置显示方向
 * @param       dir        0:竖屏映射 1:横屏映射
 * @retval      无
 */
void ltdc_display_dir(uint8_t dir)
{
    lcdltdc.dir = dir;     /* 显示方向 */

    if (dir == 0)          /* 竖屏 */
    {
        lcdltdc.width = lcdltdc.pheight;
        lcdltdc.height = lcdltdc.pwidth;
    }
    else if (dir == 1)     /* 横屏 */
    {
        lcdltdc.width = lcdltdc.pwidth;
        lcdltdc.height = lcdltdc.pheight;
    }
}

/**
 * @brief       画点
 * @param       x, y       坐标
 * @param       color      颜色值
 * @retval      无
 */
void ltdc_draw_point(uint16_t x, uint16_t y, uint32_t color)
{ 
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888

    if (lcdltdc.dir)    /* 横屏 */
    {
        *(uint32_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) = color;
    }
    else                /* 竖屏 */
    {
        *(uint32_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) = color;
    }

#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888

    if (lcdltdc.dir)     /* 横屏 */
    {
        *(uint16_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) = color;
        *(uint8_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x) + 2) = color >> 16;
    }
    else                /* 竖屏 */
    {
        *(uint16_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) = color;
        *(uint8_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y) + 2) = color >> 16;
    }
    
#else

    if (lcdltdc.dir)    /* 横屏 */
    {
        *(uint16_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) = color;
    }
    else                /* 竖屏 */
    {
        *(uint16_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) = color;
    }

#endif
}

/**
 * @brief       读点
 * @param       x, y       坐标
 * @retval      像素颜色值
 */
uint32_t ltdc_read_point(uint16_t x, uint16_t y)
{ 
#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888

    if (lcdltdc.dir)    /* 横屏 */
    {
        return *(uint32_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x));
    }
    else                /* 竖屏 */
    {
        return *(uint32_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y));
    }

#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888

    if (lcdltdc.dir)    /* 横屏 */
    {
        return *(uint32_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x)) & 0XFFFFFF;
    }
    else                /* 竖屏 */
    {
        return *(uint32_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y)) & 0XFFFFFF;
    }
    
#else

    if (lcdltdc.dir)    /* 横屏 */
    {
        return *(uint16_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * y + x));
    }
    else                /* 竖屏 */
    {
        return *(uint16_t *)((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * (lcdltdc.pheight - x - 1) + y));
    }

#endif
}

/**
 * @brief       区域填充单色
 * @param       sx, sy     起始坐标
 * @param       ex, ey     结束坐标
 * @param       color      填充颜色
 * @retval      无
 */
void ltdc_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{ 
    uint32_t psx, psy, pex, pey;        /* 以LCD面板为基准的坐标系,不随横竖屏变化而变化 */
    uint16_t offline;
    uint32_t addr; 

    if ((lcdltdc.width == 0u) || (lcdltdc.height == 0u))
    {
        return;
    }
    if ((sx >= lcdltdc.width) || (sy >= lcdltdc.height))
    {
        return;
    }
    if (ex >= lcdltdc.width)
    {
        ex = lcdltdc.width - 1u;
    }
    if (ey >= lcdltdc.height)
    {
        ey = lcdltdc.height - 1u;
    }
    if ((ex < sx) || (ey < sy))
    {
        return;
    }

    /* 坐标系转换 */
    if (lcdltdc.dir)                    /* 横屏 */
    {
        psx = sx;
        psy = sy;
        pex = ex;
        pey = ey;
    }
    else                                /* 竖屏 */
    {
        if (ex >= lcdltdc.pheight)
        {
            ex = lcdltdc.pheight - 1;   /* 限制范围 */
        }

        if (sx >= lcdltdc.pheight)
        {
            sx = lcdltdc.pheight - 1;   /* 限制范围 */
        }
        
        psx = sy;
        psy = lcdltdc.pheight - ex - 1;
        pex = ey;
        pey = lcdltdc.pheight - sx - 1;
    }

#if (LTDC_ENABLE_DMA2D == 0)
    ltdc_fill_sw_rect(psx, psy, pex, pey, color);
    return;
#endif

    offline = lcdltdc.pwidth - (pex - psx + 1);   /* 行偏移:当前行最后一个像素和下一行第一个像素之间的像素数目 */
    addr = ((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx));

    __HAL_RCC_DMA2D_CLK_ENABLE();                             /* 使能DM2D时钟 */

    /* LTDC相关时序: DE/VSYNC/HSYNC/CLK */
    /* LTDC RGB数据线: R[7:0], G[7:0], B[7:0] */

    DMA2D->CR &= ~(DMA2D_CR_START);                           /* 先停止DMA2D */
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    DMA2D->CR = DMA2D_R2M;                                    /* 寄存器到存储器模式 */
    DMA2D->OPFCCR = LTDC_PIXFORMAT;                           /* 设置颜色格式 */
    DMA2D->OOR = offline;                                     /* 设置行偏移 */

    DMA2D->OMAR = addr;                                       /* 输出存储器地址 */
    DMA2D->NLR = (pey - psy + 1) | ((pex - psx + 1) << 16);   /* 设定行数寄存器 */
    DMA2D->OCOLR = color;                                     /* 设定输出颜色寄存器 */
    g_ltdc_dma2d_transfer_count++;
    DMA2D->CR |= DMA2D_CR_START;                              /* 启动DMA2D */
    if (ltdc_dma2d_wait_complete() != 0u)
    {
        /* DMA2D error/timeout: force software path so init can still paint framebuffer. */
        g_ltdc_dma2d_sw_fallback_count++;
        ltdc_fill_sw_rect(psx, psy, pex, pey, color);
    }
}
//void ltdc_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
//{
//    uint32_t timeout = 0; 
//    uint16_t offline;
//    uint32_t addr;  
//
//    if (ex >= lcdltdc.width)
//    {
//        ex = lcdltdc.width - 1;
//    }
//
//    if (ey >= lcdltdc.height)
//    {
//        ey = lcdltdc.height - 1;
//    }
//
//    {
//        psx = sx; 
//        psy = sy;
//        pex = ex;
//        pey = ey;
//    }
//    {
//        psx = sy;
//        psy = lcdltdc.pheight - ex - 1;
//        pex = ey;
//        pey = lcdltdc.pheight - sx - 1;
//    }

//    offline = lcdltdc.pwidth - (pex - psx + 1);
//    addr =((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx));
//
//    {
//        color = ((color & 0X0000F800) << 8) | ((color & 0X000007E0) << 5 ) | ((color & 0X0000001F) << 3);
//    }
//
//    g_dma2d_handle.Instance = DMA2D;
//
//
//    {
//        timeout++;
//    }
//}

/**
 * @brief       区域填充点阵颜色
 * @param       sx, sy     起始坐标
 * @param       ex, ey     结束坐标
 * @param       color      颜色数组指针
 * @retval      无
 */
void ltdc_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint32_t psx, psy, pex, pey;   /* 以LCD面板为基准的坐标系,不随横竖屏变化而变化 */
    uint16_t offline;
    uint32_t addr;

    if ((lcdltdc.width == 0u) || (lcdltdc.height == 0u) || (color == NULL))
    {
        return;
    }
    if ((sx >= lcdltdc.width) || (sy >= lcdltdc.height))
    {
        return;
    }
    if (ex >= lcdltdc.width)
    {
        ex = lcdltdc.width - 1u;
    }
    if (ey >= lcdltdc.height)
    {
        ey = lcdltdc.height - 1u;
    }
    if ((ex < sx) || (ey < sy))
    {
        return;
    }
  
    /* 坐标系转换 */
    if (lcdltdc.dir)               /* 横屏 */
    {
        psx = sx;
        psy = sy;
        pex = ex;
        pey = ey;
    }
    else                           /* 竖屏 */
    {
        psx = sy;
        psy = lcdltdc.pheight - ex - 1;
        pex = ey;
        pey = lcdltdc.pheight - sx - 1;
    }

#if (LTDC_ENABLE_DMA2D == 0)
    ltdc_color_fill_sw_rect(psx, psy, pex, pey, color);
    return;
#endif

    offline = lcdltdc.pwidth - (pex - psx + 1);   /* 行偏移:当前行最后一个像素和下一行第一个像素之间的像素数目 */
    addr = ((uint32_t)g_ltdc_framebuf[lcdltdc.activelayer] + lcdltdc.pixsize * (lcdltdc.pwidth * psy + psx));

    __HAL_RCC_DMA2D_CLK_ENABLE();                             /* 使能DM2D时钟 */

    DMA2D->CR &= ~(DMA2D_CR_START);                           /* 先停止DMA2D */
    DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE | DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
    DMA2D->CR = DMA2D_M2M;                                    /* 存储器到存储器模式 */
    DMA2D->FGPFCCR = LTDC_PIXFORMAT;                          /* 设置前景层颜色格式 */
    DMA2D->FGOR = 0;                                          /* 前景层行偏移为0 */
    DMA2D->OOR = offline;                                     /* 设置行偏移 */

    DMA2D->FGMAR = (uint32_t)color;                           /* 源地址 */
    DMA2D->OMAR = addr;                                       /* 输出存储器地址 */
    DMA2D->NLR = (pey - psy + 1) | ((pex - psx + 1) << 16);   /* 设定行数寄存器 */
    g_ltdc_dma2d_transfer_count++;
    DMA2D->CR |= DMA2D_CR_START;                              /* 启动DMA2D */
    if (ltdc_dma2d_wait_complete() != 0u)
    {
        /* DMA2D error/timeout: force software path so init can still paint framebuffer. */
        g_ltdc_dma2d_sw_fallback_count++;
        ltdc_color_fill_sw_rect(psx, psy, pex, pey, color);
    }
}  

/**
 * @brief       清屏
 * @param       color      背景色
 * @retval      无
 */
void ltdc_clear(uint32_t color)
{
    if ((lcdltdc.width == 0u) || (lcdltdc.height == 0u))
    {
        return;
    }
    ltdc_fill(0, 0, lcdltdc.width - 1, lcdltdc.height - 1, color);
}

/**
 * @brief       配置 LTDC 时钟
 * @param       pll3n      PLL3N 参数
 * @param       pll3m      PLL3M 参数
 * @param       pll3r      PLL3R 参数
 * @retval      0:成功 1:失败
 */
uint8_t ltdc_clk_set(uint32_t pll3n, uint32_t pll3m, uint32_t pll3r)
{
    RCC_PeriphCLKInitTypeDef periphclk_initure;

    /* LTDC输出像素时钟，需要根据自己所使用的LCD数据手册来配置！ */
    periphclk_initure.PeriphClockSelection = RCC_PERIPHCLK_LTDC;     /* LTDC时钟 */
    periphclk_initure.PLL3.PLL3M = pll3m;
    periphclk_initure.PLL3.PLL3N = pll3n;
    periphclk_initure.PLL3.PLL3P = 2;
    periphclk_initure.PLL3.PLL3Q = 2;
    periphclk_initure.PLL3.PLL3R = pll3r;

    if (HAL_RCCEx_PeriphCLKConfig(&periphclk_initure) == HAL_OK)     /* 配置像素时钟 */
    {
        return 0;                                                    /* 成功 */
    }
    else 
    {
        return 1;                                                    /* 失败 */
    }
}

/**
 * @brief       配置图层窗口
 * @param       layerx     图层号
 * @param       sx, sy     左上角坐标
 * @param       width      宽度
 * @param       height     高度
 * @retval      无
 */
void ltdc_layer_window_config(uint8_t layerx, uint16_t sx, uint16_t sy, uint16_t width, uint16_t height)
{
    HAL_LTDC_SetWindowPosition(&g_ltdc_handle, sx, sy, layerx);     /* 设置窗口的位置 */
    HAL_LTDC_SetWindowSize(&g_ltdc_handle, width, height, layerx);  /* 设置窗口大小 */
  
    if (lcdltdc.pheight == 1280 && layerx == 0)
    {
        LTDC_Layer1->CFBLR = (width * 4 << 16) | (width * 4 + 7);   /* 帧缓冲区行长和行间距设置(以字节为单位) */
    }  
}

/**
 * @brief       配置图层参数
 * @param       layerx     图层号
 * @param       bufaddr    帧缓冲地址
 * @param       pixformat  像素格式
 * @param       alpha      全局透明度
 * @param       alpha0     默认透明度
 * @param       bfac1      混合因子1
 * @param       bfac2      混合因子2
 * @param       bkcolor    背景色
 * @retval      无
 */
void ltdc_layer_parameter_config(uint8_t layerx, uint32_t bufaddr, uint8_t pixformat, uint8_t alpha, uint8_t alpha0, uint8_t bfac1, uint8_t bfac2, uint32_t bkcolor)
{
    LTDC_LayerCfgTypeDef playercfg;

    playercfg.WindowX0 = 0;                                            /* 窗口起始X坐标 */
    playercfg.WindowY0 = 0;                                            /* 窗口起始Y坐标 */
    playercfg.WindowX1 = lcdltdc.pwidth;                               /* 窗口终止X坐标 */
    playercfg.WindowY1 = lcdltdc.pheight;                              /* 窗口终止Y坐标 */
    playercfg.PixelFormat = pixformat;                                 /* 设置层像素格式 */
    playercfg.Alpha = alpha;                                           /* 设置层恒定Alpha值 */
    playercfg.Alpha0 = alpha0;                                         /* 设置默认颜色Alpha值 */
    playercfg.BlendingFactor1 = (uint32_t)bfac1 << 8;                  /* 设置层混合系数1 */
    playercfg.BlendingFactor2 = (uint32_t)bfac2;                       /* 设置层混合系数2 */
    playercfg.FBStartAdress = bufaddr;                                 /* 设置层颜色帧缓存起始地址 */
    playercfg.ImageWidth = lcdltdc.pwidth;                             /* 设置颜色帧缓冲区的宽度 */
    playercfg.ImageHeight = lcdltdc.pheight;                           /* 设置颜色帧缓冲区的高度 */
    playercfg.Backcolor.Red = (uint8_t)(bkcolor & 0X00FF0000) >> 16;   /* 背景颜色红色部分 */
    playercfg.Backcolor.Green = (uint8_t)(bkcolor & 0X0000FF00) >> 8;  /* 背景颜色绿色部分 */
    playercfg.Backcolor.Blue = (uint8_t)bkcolor & 0X000000FF;          /* 背景颜色蓝色部分 */
    HAL_LTDC_ConfigLayer(&g_ltdc_handle, &playercfg, layerx);          /* 设置所选中的层 */
}  

/**
 * @brief       读取 LCD 面板 ID
 * @note        通过 RGB 高位引脚状态识别面板
 * @retval      面板 ID
 */
uint16_t ltdc_panelid_read(void)
{
    uint8_t idx = 0;

    GPIO_InitTypeDef gpio_init_struct;

    __HAL_RCC_GPIOG_CLK_ENABLE();                              /* 使能GPIOG时钟 */
    __HAL_RCC_GPIOI_CLK_ENABLE();                              /* 使能GPIOI时钟 */
    
    gpio_init_struct.Pin = GPIO_PIN_6;                         /* R7引脚PG6 */
    gpio_init_struct.Mode = GPIO_MODE_INPUT;                   /* 输入 */
    gpio_init_struct.Pull = GPIO_PULLUP;                       /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_HIGH;                  /* 高速 */
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);                   /* 初始化PG6 */
    
    gpio_init_struct.Pin = GPIO_PIN_2 | GPIO_PIN_7;            /* G7,B7引脚PI2,7 */
    HAL_GPIO_Init(GPIOI, &gpio_init_struct);                   /* 初始化PI2,7 */

    delay_us(10);
    idx  = (uint8_t)HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6);       /* 读取M0 */
    idx |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2) << 1;  /* 读取M1 */
    idx |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_7) << 2;  /* 读取M2 */

    switch (idx)
    {
        case 0 :
            return 0X4342;                    /* 4.3寸屏,480*272分辨率 */
        
        case 1 :
            return 0X7084;                    /* 7寸屏,800*480分辨率 */
        
        case 2 :
            return 0X7016;                    /* 7寸屏,1024*600分辨率 */
        
        case 3 :
            return 0X5571;                    /* 5.5寸屏,720*1280分辨率 */
        
        case 4 :
            return 0X4384;                    /* 4.3寸屏,800*480分辨率 */
        
        case 5 :
            return 0X1018;                    /* 10.1寸屏,1280*800分辨率 */
        
        default :
            return 0;
    }
}

/**
 * @brief       LTDC 初始化
 * @param       无
 * @retval      无
 */
void ltdc_init(void)
{
    uint16_t lcdid = 0;
    g_ltdc_init_stage = 1u;

    /* Clear runtime geometry first, avoid stale values when re-init or ID mismatch. */
    lcdltdc.pwidth = 0U;
    lcdltdc.pheight = 0U;
    lcdltdc.width = 0U;
    lcdltdc.height = 0U;

    lcdid = ltdc_panelid_read();    /* panel id from ID pins */
    g_ltdc_init_stage = 2u;

#if RGB_80_8001280
    lcdid = 0X8081;
#endif

#if (LTDC_FORCE_PANEL_ID != 0U)
    lcdid = (uint16_t)LTDC_FORCE_PANEL_ID;
#endif

#if LTDC_ENABLE_ID_FALLBACK
    if (lcdid == 0U)
    {
        lcdid = LTDC_PANEL_FALLBACK_ID;
    }
#endif
    g_ltdc_panel_id = lcdid;

    if (lcdid == 0X5571)
    {
        tft_spi_init();
    }
  
    if (lcdid == 0X4342)
    {
        lcdltdc.pwidth = 480;       /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 272;      /* 面板高度,单位:像素 */
        lcdltdc.hsw = 1;            /* 水平同步宽度 */
        lcdltdc.hbp = 40;           /* 水平后廊 */
        lcdltdc.hfp = 5;            /* 水平前廊 */      
        lcdltdc.vsw = 1;            /* 垂直同步宽度 */
        lcdltdc.vbp = 8;            /* 垂直后廊 */      
        lcdltdc.vfp = 8;            /* 垂直前廊 */
        ltdc_clk_set(300, 25, 33);  /* 设置像素时钟 9Mhz */
    }
    else if (lcdid == 0X7084)
    {
        lcdltdc.pwidth = 800;       /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 480;      /* 面板高度,单位:像素 */
        lcdltdc.hsw = 1;            /* 水平同步宽度 */
        lcdltdc.hbp = 46;           /* 水平后廊 */
        lcdltdc.hfp = 210;          /* 水平前廊 */
        lcdltdc.vsw = 1;            /* 垂直同步宽度 */
        lcdltdc.vbp = 23;           /* 垂直后廊 */
        lcdltdc.vfp = 22;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 9);   /* 设置像素时钟 33Mhz */
    }
    else if (lcdid == 0X7016)
    {
        lcdltdc.pwidth = 1024;      /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 600;      /* 面板高度,单位:像素 */
        lcdltdc.hsw = 20;           /* 水平同步宽度 */
        lcdltdc.hbp = 140;          /* 水平后廊 */
        lcdltdc.hfp = 160;          /* 水平前廊 */
        lcdltdc.vsw = 3;            /* 垂直同步宽度 */
        lcdltdc.vbp = 20;           /* 垂直后廊 */
        lcdltdc.vfp = 12;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 6);   /* 设置像素时钟 50Mhz */
    }
    else if (lcdid == 0X5571)
    {
        lcdltdc.pwidth = 720;       /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 1280;     /* 面板高度,单位:像素 */
        lcdltdc.hsw = 10;           /* 水平同步宽度 */
        lcdltdc.hbp = 36;           /* 水平后廊 */
        lcdltdc.hfp = 46;           /* 水平前廊 */
        lcdltdc.vsw = 5;            /* 垂直同步宽度 */
        lcdltdc.vbp = 5;            /* 垂直后廊 */
        lcdltdc.vfp = 16;           /* 垂直前廊 */
        ltdc_clk_set(330, 25, 6);   /* 设置像素时钟 55Mhz */
    }
    else if (lcdid == 0X4384)
    {
        lcdltdc.pwidth = 800;       /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 480;      /* 面板高度,单位:像素 */
        lcdltdc.hsw = 48;           /* 水平同步宽度 */      
        lcdltdc.hbp = 88;           /* 水平后廊 */
        lcdltdc.hfp = 40;           /* 水平前廊 */
        lcdltdc.vsw = 3;            /* 垂直同步宽度 */
        lcdltdc.vbp = 32;           /* 垂直后廊 */
        lcdltdc.vfp = 13;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 9);   /* 设置像素时钟 33Mhz */ 
    }
    else if (lcdid == 0X8081)       /* 8寸800*1280 RGB屏 */
    {
        lcdltdc.pwidth = 800;       /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 1280;     /* 面板高度,单位:像素 */
        lcdltdc.hsw = 5;            /* 水平同步宽度 */
        lcdltdc.hbp = 20;           /* 水平后廊 */
        lcdltdc.hfp = 40;           /* 水平前廊 */
        lcdltdc.vsw = 3;            /* 垂直同步宽度 */
        lcdltdc.vbp = 20;           /* 垂直后廊 */
        lcdltdc.vfp = 30;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 5);   /* 设置像素时钟 60Mhz */
    }
    else if (lcdid == 0X1018)       /* 10.1寸1280*800 RGB屏 */
    {
        lcdltdc.pwidth = 1280;      /* 面板宽度,单位:像素 */
        lcdltdc.pheight = 800;      /* 面板高度,单位:像素 */
        lcdltdc.hsw = 10;           /* 水平同步宽度 */
        lcdltdc.hbp = 140;          /* 水平后廊 */
        lcdltdc.hfp = 10;           /* 水平前廊 */
        lcdltdc.vsw = 3;            /* 垂直同步宽度 */
        lcdltdc.vbp = 10;           /* 垂直后廊 */
        lcdltdc.vfp = 10;           /* 垂直前廊 */
        ltdc_clk_set(300, 25, 5);   /* 设置像素时钟 60Mhz */
    } 
    g_ltdc_init_stage = 3u;
    if ((lcdltdc.pwidth == 0U) || (lcdltdc.pheight == 0U))
    {
        g_ltdc_init_stage = 0xE301u;
        lcddev.id = 0U;
        lcddev.width = 0U;
        lcddev.height = 0U;
        return;
    }

    lcddev.id = lcdid;

    lcddev.width = lcdltdc.pwidth;      /* 设置lcddev的宽度参数 */
    lcddev.height = lcdltdc.pheight;    /* 设置lcddev的高度参数 */
    lcdltdc.pixformat = LTDC_PIXFORMAT; /* 颜色像素格式 */

#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888
    lcdltdc.pixsize = 4;    /* bytes per pixel */
#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB888
    lcdltdc.pixsize = 3;    /* bytes per pixel */
#else
    lcdltdc.pixsize = 2;    /* bytes per pixel */
#endif

    g_ltdc_framebuf[0] = (uint32_t *)LTDC_FRAME_BUF_ADDR;
    lcdltdc.activelayer = 0u;
    
    /* LTDC参数初始化 */
    g_ltdc_handle.Instance = LTDC;
    
    if (lcdid == 0X8081)
    {
        g_ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AH;     /* HSYNC高电平有效 */
    }
    else
    {
        g_ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AL;     /* HSYNC低电平有效 */
    }
    
    g_ltdc_handle.Init.VSPolarity = LTDC_VSPOLARITY_AL;         /* VSYNC低电平有效 */
    g_ltdc_handle.Init.DEPolarity = LTDC_DEPOLARITY_AL;         /* DE低电平有效 */
    g_ltdc_handle.State = HAL_LTDC_STATE_RESET;
    
    if (lcdid == 0X1018 || lcdid == 0X8081)
    {
        g_ltdc_handle.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;   /* 像素时钟反相 */
    }
    else 
    {
        g_ltdc_handle.Init.PCPolarity = LTDC_PCPOLARITY_IPC;    /* 像素时钟同相 */
    }

    g_ltdc_handle.Init.HorizontalSync = lcdltdc.hsw - 1;                                            /* HSYNC宽度 */
    g_ltdc_handle.Init.VerticalSync = lcdltdc.vsw - 1;                                              /* VSYNC高度 */
    g_ltdc_handle.Init.AccumulatedHBP = lcdltdc.hsw + lcdltdc.hbp - 1;                              /* 水平后沿累计 */
    g_ltdc_handle.Init.AccumulatedVBP = lcdltdc.vsw + lcdltdc.vbp - 1;                              /* 垂直后沿累计 */
    g_ltdc_handle.Init.AccumulatedActiveW = lcdltdc.hsw + lcdltdc.hbp + lcdltdc.pwidth - 1;         /* 有效宽度累计 */
    g_ltdc_handle.Init.AccumulatedActiveH = lcdltdc.vsw + lcdltdc.vbp + lcdltdc.pheight - 1;        /* 有效高度累计 */
    g_ltdc_handle.Init.TotalWidth = lcdltdc.hsw + lcdltdc.hbp + lcdltdc.pwidth + lcdltdc.hfp - 1;   /* 总宽度 */
    g_ltdc_handle.Init.TotalHeigh = lcdltdc.vsw + lcdltdc.vbp + lcdltdc.pheight + lcdltdc.vfp - 1;  /* 总高度 */
    g_ltdc_handle.Init.Backcolor.Red = 0;                                                           /* 背景颜色红色部分 */
    g_ltdc_handle.Init.Backcolor.Green = 0;                                                         /* 背景颜色绿色部分 */
    g_ltdc_handle.Init.Backcolor.Blue = 0;                                                          /* 背景颜色蓝色部分 */
    g_ltdc_init_stage = 4u;
    HAL_LTDC_Init(&g_ltdc_handle);
    g_ltdc_init_stage = 5u;

    /* LTDC层参数配置 */
    ltdc_layer_parameter_config(0, (uint32_t)g_ltdc_framebuf[0], LTDC_PIXFORMAT, 255, 0, 6, 7, 0X000000);   /* 配置layer0 */
    ltdc_layer_window_config(0, 0, 0, lcdltdc.pwidth, lcdltdc.pheight);                                     /* 配置窗口 */
    g_ltdc_init_stage = 6u;
    ltdc_select_layer(0);                  /* 选择layer0 */
    /* Force a first-frame software clear so old SDRAM image is overwritten even if DMA2D is unstable. */
    ltdc_fill_sw_rect(0u, 0u, lcdltdc.pwidth - 1u, lcdltdc.pheight - 1u, 0u);

    /* Turn on backlight early for visibility, even if panel reset sequence stalls. */
    LTDC_BL(1);
    g_ltdc_init_stage = 61u;
    if (lcdid != 0X5571)                   /* 5571无需硬复位 */
    {
        /* 执行LCD复位序列 */
        LTDC_RST(1);
        g_ltdc_init_stage = 62u;
        delay_ms(10);
        g_ltdc_init_stage = 63u;
        LTDC_RST(0);
        g_ltdc_init_stage = 64u;
        delay_ms(50);
        g_ltdc_init_stage = 65u;
        LTDC_RST(1); 
        g_ltdc_init_stage = 66u;
        delay_ms(200); 
        g_ltdc_init_stage = 67u;
    }
    
    g_ltdc_init_stage = 68u;
    LTDC_BL(1);                            /* 打开背光 */
    g_ltdc_init_stage = 69u;
    ltdc_clear(0XFFFFFFFF);                /* 白色清屏 */
    g_ltdc_init_stage = 70u;
}

/**
 * @brief       LTDC 底层 MSP 初始化
 * @param       hltdc      LTDC 句柄
 * @retval      无
 */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    GPIO_InitTypeDef gpio_init_struct;

    __HAL_RCC_LTDC_CLK_ENABLE();       /* 使能LTDC时钟 */
    __HAL_RCC_DMA2D_CLK_ENABLE();      /* 使能DM2D时钟 */

    /* LTDC RGB数据线映射说明 */
    /* LTDC_R7(PG6)...LTDC_B0(PG14) */
  
    /* 控制引脚时钟使能 */
    LTDC_BL_GPIO_CLK_ENABLE();         /* 背光 */
    LTDC_RST_GPIO_CLK_ENABLE();        /* 复位 */  
    LTDC_DE_GPIO_CLK_ENABLE();         /* DE */
    LTDC_VSYNC_GPIO_CLK_ENABLE();      /* VSYNC */
    LTDC_HSYNC_GPIO_CLK_ENABLE();      /* HSYNC */
    LTDC_CLK_GPIO_CLK_ENABLE();        /* LCD时钟 */
    
    gpio_init_struct.Pin = LTDC_BL_GPIO_PIN;                /* 背光引脚 */
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;            /* 推挽输出 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_HIGH;               /* 高速 */
    HAL_GPIO_Init(LTDC_BL_GPIO_PORT, &gpio_init_struct);    /* 初始化背光引脚 */

    gpio_init_struct.Pin = LTDC_RST_GPIO_PIN;               /* 复位引脚 */
    HAL_GPIO_Init(LTDC_RST_GPIO_PORT, &gpio_init_struct);   /* 初始化复位引脚 */
    
    gpio_init_struct.Pin = LTDC_DE_GPIO_PIN;                /* DE引脚 */     
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;                /* 复用推挽 */
    gpio_init_struct.Pull = GPIO_NOPULL;                    /* 无上下拉 */
    gpio_init_struct.Speed = GPIO_SPEED_HIGH;               /* 高速 */
    gpio_init_struct.Alternate = GPIO_AF14_LTDC;            /* LTDC复用 */
    HAL_GPIO_Init(LTDC_DE_GPIO_PORT, &gpio_init_struct);    /* 初始化DE引脚 */
    
    gpio_init_struct.Pin = LTDC_VSYNC_GPIO_PIN;             /* VSYNC引脚 */
    HAL_GPIO_Init(LTDC_VSYNC_GPIO_PORT, &gpio_init_struct); /* 初始化VSYNC引脚 */
    
    gpio_init_struct.Pin = LTDC_HSYNC_GPIO_PIN;             /* HSYNC引脚 */  
    HAL_GPIO_Init(LTDC_HSYNC_GPIO_PORT, &gpio_init_struct); /* 初始化HSYNC引脚 */
    
    gpio_init_struct.Pin = LTDC_CLK_GPIO_PIN;               /* LTDC时钟引脚 */  
    HAL_GPIO_Init(LTDC_CLK_GPIO_PORT, &gpio_init_struct);   /* 初始化LTDC时钟引脚 */

    /* 数据引脚时钟使能 */
    __HAL_RCC_GPIOA_CLK_ENABLE();                /* GPIOA时钟 */
    __HAL_RCC_GPIOD_CLK_ENABLE();                /* GPIOD时钟 */
    __HAL_RCC_GPIOE_CLK_ENABLE();                /* GPIOE时钟 */
    __HAL_RCC_GPIOG_CLK_ENABLE();                /* 使能GPIOG时钟 */
    __HAL_RCC_GPIOH_CLK_ENABLE();                /* GPIOH时钟 */
    __HAL_RCC_GPIOI_CLK_ENABLE();                /* 使能GPIOI时钟 */
    
    /* 初始化PA8 */
    gpio_init_struct.Pin = GPIO_PIN_8;
    gpio_init_struct.Alternate = GPIO_AF13_LTDC; /* LTDC复用 */
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);

    /* 初始化PA2 */
    gpio_init_struct.Pin = GPIO_PIN_2;
    gpio_init_struct.Alternate = GPIO_AF14_LTDC; /* LTDC复用 */
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);

    /* 初始化PD6 */
    gpio_init_struct.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOD, &gpio_init_struct);

    /* PE5 can be switched between LTDC_G0 and SAI1_SCK_A by LTDC_USE_PE5_G0. */
#if LTDC_USE_PE5_G0
    gpio_init_struct.Pin = GPIO_PIN_5 | GPIO_PIN_6;
#else
    gpio_init_struct.Pin = GPIO_PIN_6;
#endif
    HAL_GPIO_Init(GPIOE, &gpio_init_struct);

    /* 初始化PG6/12/13/14 */
    gpio_init_struct.Pin = GPIO_PIN_6 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14; 
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);
    
    /* 初始化PA8 */
    gpio_init_struct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | \
                     GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio_init_struct);
    
    /* 初始化PI0/1/2/4/5/6/7 */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | \
                     GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOI, &gpio_init_struct); 
}
















