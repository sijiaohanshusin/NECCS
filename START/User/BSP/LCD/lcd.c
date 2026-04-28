/**
 ****************************************************************************************************
 * @file        lcd.c
 * @version     V1.0
 * @brief       LCD 显示应用函数——LTDC RGB 接口抽象层
 * @details
 *   本文件是 NECCS 项目的 LCD 高层抽象层，统一封装了：
 *     - 基础绘图 API：画点、画线、矩形、圆（实心/空心）；
 *     - 文字显示 API：ASCII 字符/数字/字符串（支持 12/16/24/32 号字体）；
 *     - 填充 API：单色填充、颜色数组填充；
 *     - 初始化与方向控制：lcd_init / lcd_display_dir。
 *
 *   底层对接关系：
 *     - LTDC RGB 接口（NECCS 硬件实际使用）→ 所有操作最终调用 ltdc_*.c 系列函数；
 *     - 旧 FSMC/SPI 接口屏（遗留兼容）→ 相关函数在本文件中为空桩（stub），
 *       通过 `lcdltdc.pwidth != 0` 条件判断分流。
 *
 *   [注意] lcd.c 中的字符/数字显示函数使用 CPU 逐像素绘制（lcd_draw_point），
 *          效率较低；NECCS 项目主 UI 由 LVGL 驱动，这些函数仅供调试输出使用。
 *   [改进] lcd_draw_line / lcd_draw_circle / lcd_fill_circle 均为 CPU 软件实现，
 *          可替换为 DMA2D 加速版本以提升大面积绘制性能。
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    STM32H743IIT6小系统板
 *
 ****************************************************************************************************
 */
 
#include <stdio.h>
#include <stdlib.h>
#include "LCD/lcd.h"
#include "LCD/lcdfont.h"


/* LCD的画笔颜色和背景色（全局变量，供所有 lcd_show_* 函数引用）*/
uint32_t g_point_color = RED;            /* 画笔颜色（默认红色），可被 lcd_show_* 函数内部覆盖 */
uint32_t g_back_color  = 0XFFFFFFFF;     /* 背景色（默认白色），非叠加模式下填充字符背景 */

/* 管理LCD重要参数（全局设备描述符，字段定义见 lcd.h 的 _lcd_dev 结构体）*/
_lcd_dev lcddev;                         /* lcddev.width/height/dir：逻辑分辨率和方向 */
volatile uint32_t g_lcd_init_stage = 0u; /* 初始化阶段标志（0=未初始化，5=完成），供调试用 */


/**
 * @brief       LCD延时函数,仅用于部分在mdk -O1时间优化时需要设置的地方
 * @param       i:延时的数值
 * @retval      无
 */
void lcd_opt_delay(uint32_t i)
{
    while (i--);           /* 使用AC6时空循环可能被优化,可使用while(1) __asm volatile(""); */
}

/**
 * @brief       准备写GRAM
 * @param       无
 * @retval      无
 */
void lcd_write_ram_prepare(void)
{
}

/**
 * @brief       颜色转换：RGB565 → RGB888（去除 Alpha 通道）
 * @note        位域提取说明：
 *              RGB565 格式：[15:11]=R5，[10:5]=G6，[4:0]=B5
 *              R8 = R5 << 3（低3位补0，保留高5位）→ `(c & 0xF800) >> 8`
 *              G8 = G6 << 2（低2位补0）→ `(c & 0x07E0) >> 3`
 *              B8 = B5 << 3（低3位补0）→ `(c & 0x001F) << 3`
 *              [注意] 此方法低位补0，颜色精度略低于插值法（如 r8 = r5*255/31），
 *                     但速度更快，用于 LTDC 帧缓冲格式转换场景。
 * @param       rgb565 : RGB565 颜色值（16位）
 * @retval      RGB888 颜色值（24位，高8位为0，无 Alpha 分量）
 */
uint32_t lcd_rgb565torgb888(uint16_t rgb565)
{
    uint16_t r, g, b;
    uint32_t rgb888;

    r = (rgb565 & 0XF800) >> 8;     /* 提取 R5 并左移至 R8 位域（bit23:16），低3位=0 */
    g = (rgb565 & 0X07E0) >> 3;     /* 提取 G6 并左移至 G8 位域（bit15:8），低2位=0 */
    b = (rgb565 & 0X001F) << 3;     /* 提取 B5 并左移至 B8 位域（bit7:0），低3位=0 */

    rgb888 = (r << 16) | (g << 8) | b;   /* 拼接为 0x00RRGGBB 格式 */

    return rgb888;
}

/**
 * @brief       读取某个点的颜色值
 * @param       x,y:坐标
 * @retval      此点的颜色
 */
uint32_t lcd_read_point(uint16_t x, uint16_t y)
{
    if (x >= lcddev.width || y >= lcddev.height)return 0;   /* 超过了范围,直接返回 */

    return ltdc_read_point(x, y);
}

/**
 * @brief       LCD开启显示
 * @param       无
 * @retval      无
 */
void lcd_display_on(void)
{
    if (lcdltdc.pwidth != 0)
    {
        ltdc_switch(1);         /* 开启LTDC */
    }
}

/**
 * @brief       LCD关闭显示
 * @param       无
 * @retval      无
 */
void lcd_display_off(void)
{
    if (lcdltdc.pwidth != 0)
    {
        ltdc_switch(0);         /* 关闭LTDC */
    }
}

/**
 * @brief       设置光标位置(对RGB屏无效)
 * @param       x,y: 坐标
 * @retval      无
 */
void lcd_set_cursor(uint16_t x, uint16_t y)
{
}

/**
 * @brief       设置LCD的自动扫描方向(对RGB屏无效)
 * @note
 *              注意:其他函数可能会受到此函数设置的影响,
 *              所以,一般设置为L2R_U2D即可,如果设置为其他扫描方式,可能导致显示不正常.
 *
 * @param       dir:0~7,代表8个方向(具体定义见lcd.h)
 * @retval      无
 */
void lcd_scan_dir(uint8_t dir)
{
}

/**
 * @brief       画点
 * @param       x,y: 坐标
 * @param       color: 点的颜色
 * @retval      无
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color)
{    
    if (lcdltdc.pwidth != 0)     /* 如果是RGB屏 */
    {
        ltdc_draw_point(x, y, color);
    }
}

/**
 * @brief       设置LCD显示方向
 * @param       dir:0,竖屏; 1,横屏
 * @retval      无
 */
void lcd_display_dir(uint8_t dir)
{
    lcddev.dir = dir;            /* 竖屏/横屏 */
    
    if (lcdltdc.pwidth != 0)     /* 如果是RGB屏 */
    {
        ltdc_display_dir(dir);
        lcddev.width = lcdltdc.width;
        lcddev.height = lcdltdc.height;
        return;
    }
}

/**
 * @brief       设置窗口(对RGB屏无效),并自动设置画点坐标到窗口左上角(sx,sy).
 * @param       sx,sy:窗口起始坐标(左上角)
 * @param       width,height:窗口宽度和高度,必须大于0!!
 * @note        窗体大小:width*height.
 *
 * @retval      无
 */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height)
{
    if (lcdltdc.pwidth != 0)     /* 如果是RGB屏 */
    {
        return;                  /* RGB屏不支持该函数 */
    }
}

/**
 * @brief       初始化LCD
 * @note        该初始化函数可以初始化各种型号的LCD(详见本.c文件最前面的描述)
 *              初始化阶段（g_lcd_init_stage 用于调试跟踪）：
 *                1 = 已调用 ltdc_init（正在初始化 LTDC）
 *                2 = ltdc_init 返回，检查 RGB 屏
 *                3 = lcd_display_dir 设置横屏完成
 *                4 = LCD_BL 背光已点亮
 *                5 = lcd_clear 清屏完成，初始化结束
 * @param       无
 * @retval      无
 */
void lcd_init(void)
{
    g_lcd_init_stage = 1u;
    ltdc_init();                        /* 初始化LTDC（内部包含面板ID回退）*/
    g_lcd_init_stage = 2u;

    /* 特别注意, 如果在main函数里面屏蔽串口1初始化, 则会卡死在printf
     * 里面(卡死在f_putc函数), 所以, 必须初始化串口1, 或者屏蔽掉下面
     * 这行 printf 语句 !!!!!!!
     */
    /* Avoid per-byte UART blocking here; App_Display_Init prints a compact probe log. */

    if (lcdltdc.pwidth != 0)            /* 如果是RGB屏：lcdltdc.pwidth > 0 表示 LTDC 初始化成功 */
    {
        lcd_display_dir(1);             /* 默认为橫屏（dir=1：逻辑宽度 = 物理宽度，高度类推）*/
    }
    g_lcd_init_stage = 3u;

    LCD_BL(1);                          /* 点亮背光（与 ltdc_init 内部打开背光幂等）*/
    g_lcd_init_stage = 4u;
    lcd_clear(WHITE);                   /* 清屏为白色（调用 ltdc_clear，CPU 写帧缓冲）*/
    g_lcd_init_stage = 5u;
}

/**
 * @brief       清屏函数
 * @param       color: 要清屏的颜色
 * @retval      无
 */
void lcd_clear(uint32_t color)
{
    if (lcdltdc.pwidth != 0)            /* 如果是RGB屏 */
    {
        ltdc_clear(color);
    }
}

/**
 * @brief       在指定区域内填充单个颜色
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 * @param       color: 要填充的颜色
 * @retval      无
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    if (lcdltdc.pwidth != 0)            /* 如果是RGB屏 */
    {
        ltdc_fill(sx, sy, ex, ey, color);
    }
}

/**
 * @brief       在指定区域内填充指定颜色块
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 * @param       color: 要填充的颜色数组首地址
 * @retval      无
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    if (lcdltdc.pwidth != 0)            /* 如果是RGB屏 */
    {
        ltdc_color_fill(sx, sy, ex, ey, color);
    }
}

/**
 * @brief       画任意方向直线（Bresenham/DDA 近似算法）
 * @param       x1,y1: 起点坐标
 * @param       x2,y2: 终点坐标
 * @param       color: 线的颜色
 * @details
 *   使用误差累积法（类 Bresenham）绘制直线：
 *   - 选取 |Δx| 和 |Δy| 中较大者作为主轴 distance；
 *   - 每步主轴方向前进1像素，副轴方向按 err/distance 判断是否前进；
 *   - 相比浮点 DDA 算法全程使用整数运算，适合嵌入式场景。
 *   [注意] 该算法为逐点调用 lcd_draw_point，性能与线长成线性关系；
 *          对于长直线，建议使用 ltdc_fill_async（仅水平/垂直方向）或 DMA2D 加速。
 * @retval      无
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;
    delta_x = x2 - x1;                 /* X 方向增量（正/负/零）*/
    delta_y = y2 - y1;                 /* Y 方向增量 */
    row = x1;                           /* 当前绘制点 X 坐标 */
    col = y1;                           /* 当前绘制点 Y 坐标 */

    if (delta_x > 0) incx = 1;         /* X 正方向（向右）*/
    else if (delta_x == 0) incx = 0;   /* 垂直线，X 不变 */
    else
    {
        incx = -1;
        delta_x = -delta_x;            /* 取绝对值用于比较 */
    }

    if (delta_y > 0) incy = 1;         /* Y 正方向（向下）*/
    else if (delta_y == 0) incy = 0;   /* 水平线，Y 不变 */
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    if ( delta_x > delta_y) distance = delta_x; /* 选取较大分量为主轴（步进次数）*/
    else distance = delta_y;

    for (t = 0; t <= distance + 1; t++ )         /* 沿主轴步进 distance+1 次 */
    {
        lcd_draw_point(row, col, color);          /* 画当前点 */
        xerr += delta_x;                          /* 累积 X 误差 */
        yerr += delta_y;                          /* 累积 Y 误差 */

        if (xerr > distance)                      /* X 误差超过主轴步长：X 方向前进1步 */
        {
            xerr -= distance;
            row += incx;
        }

        if (yerr > distance)                      /* Y 误差超过主轴步长：Y 方向前进1步 */
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * @brief       画水平线
 * @param       x,y  : 起点坐标
 * @param       len  : 线长度
 * @param       color: 矩形的颜色
 * @retval      无
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint32_t color)
{
    if ((len == 0) || (x > lcddev.width) || (y > lcddev.height))return;

    lcd_fill(x, y, x + len - 1, y, color);
}

/**
 * @brief       画矩形
 * @param       x1,y1: 起点坐标
 * @param       x2,y2: 终点坐标
 * @param       color: 矩形的颜色
 * @retval      无
 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    lcd_draw_line(x1, y1, x2, y1, color);
    lcd_draw_line(x1, y1, x1, y2, color);
    lcd_draw_line(x1, y2, x2, y2, color);
    lcd_draw_line(x2, y1, x2, y2, color);
}

/**
 * @brief       画空心圆（Bresenham 中点圆算法）
 * @param       x0,y0: 圆中心坐标
 * @param       r    : 半径（uint8_t，最大255像素）
 * @param       color: 圆的颜色
 * @details
 *   使用 Bresenham 中点圆算法实现：
 *   - 初始判别值 di = 3 - 2r；
 *   - 利用圆的八对称性，每次只计算 1/8 圆弧，然后对称输出8个点；
 *   - di < 0 时 y 不变，di ≥ 0 时 y 减1（向内收缩）；
 *   - 全程使用整数加法，无乘除法，效率高于三角函数方法。
 *   [注意] 圆上8个对称点：(x0±a, y0±b) 和 (x0±b, y0±a)，对应圆的8个象限。
 * @retval      无
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint32_t color)
{
    int a, b;
    int di;
    a = 0;
    b = r;
    di = 3 - (r << 1);       /* 初始判别值：di = 3 - 2r（中点圆算法标准初始化）*/

    while (a <= b)
    {
        /* 利用八对称性，一次循环绘制圆上8个点 */
        lcd_draw_point(x0 + a, y0 - b, color);  /* 第1象限上半 */
        lcd_draw_point(x0 + b, y0 - a, color);  /* 第1象限右半 */
        lcd_draw_point(x0 + b, y0 + a, color);  /* 第4象限右半 */
        lcd_draw_point(x0 + a, y0 + b, color);  /* 第4象限下半 */
        lcd_draw_point(x0 - a, y0 + b, color);  /* 第3象限下半 */
        lcd_draw_point(x0 - b, y0 + a, color);  /* 第3象限左半 */
        lcd_draw_point(x0 - a, y0 - b, color);  /* 第2象限上半 */
        lcd_draw_point(x0 - b, y0 - a, color);  /* 第2象限左半 */
        a++;                                     /* 主轴方向前进一步 */

        /* 使用Bresenham算法更新判别值 */
        if (di < 0)
        {
            di += 4 * a + 6;        /* 中点在圆内：只更新 a，判别值增量 = 4a+6 */
        }
        else
        {
            di += 10 + 4 * (a - b); /* 中点在圆外：更新 a 且 b--，判别值增量 = 4(a-b)+10 */
            b--;
        }
    }
}

/**
 * @brief       填充实心圆（扫描线填充法）
 * @param       x,y  : 圆中心坐标
 * @param       r    : 半径（uint16_t）
 * @param       color: 圆的颜色
 * @details
 *   采用水平扫描线方法：
 *   - imax ≈ r×cos(45°) = r×0.707（使用整数近似：r×707/1000+1）；
 *   - sqmax = r² + r/2（判断点是否在圆内的近似阈值）；
 *   - 对每条扫描线 i（从1到imax），找到圆弧交点 xr，然后画水平线；
 *   - 同时从对角线处（i > imax 部分）用 x=i 的垂直补充来填满圆的四角区域。
 *   [注意] 该方法调用 lcd_draw_hline（最终调用 ltdc_fill），比逐点绘制快得多，
 *          但仍为 CPU 路径。大圆建议使用 DMA2D 填充橢圆近似。
 * @retval      无
 */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint32_t color)
{
    uint32_t i;
    uint32_t imax = ((uint32_t)r * 707) / 1000 + 1;  /* 45°处的分量（约 r/√2）*/
    uint32_t sqmax = (uint32_t)r * (uint32_t)r + (uint32_t)r / 2;
    /* sqmax：圆面积判别上限（略大于 r²，用于容纳扫描线边界误差）*/
    uint32_t xr = r;                                  /* 当前扫描行的圆弧 x 坐标（从 r 递减）*/

    lcd_draw_hline(x - r, y, 2 * r, color);           /* 绘制通过圆心的水平中心线 */

    for (i = 1; i <= imax; i++)
    {
        if ((i * i + xr * xr) > sqmax)
        {
            /* 当前点已超出圆边界：补充对称未覆盖的竖向区域 */
            if (xr > imax)
            {
                lcd_draw_hline (x - i + 1, y + xr, 2 * (i - 1), color);  /* 下方补充 */
                lcd_draw_hline (x - i + 1, y - xr, 2 * (i - 1), color);  /* 上方补充 */
            }
            xr--;   /* 圆弧 x 坐标向内收缩 */
        }

        /* 绘制 y+i 和 y-i 两条扫描线（对称于 y 轴）*/
        lcd_draw_hline(x - xr, y + i, 2 * xr, color);  /* 圆心下方第 i 行 */
        lcd_draw_hline(x - xr, y - i, 2 * xr, color);  /* 圆心上方第 i 行 */
    }
}

/**
 * @brief       在指定位置显示一个 ASCII 字符（点阵字体）
 * @param       x,y   : 左上角坐标（像素）
 * @param       chr   : 要显示的字符，有效范围 ' '（0x20）～'~'（0x7E）
 * @param       size  : 字体大小（像素高度）：12 / 16 / 24 / 32
 * @param       mode  : 叠加方式
 *              0 = 非叠加：0位（背景像素）绘制 g_back_color，完全覆盖底层内容
 *              1 = 叠加：0位跳过不绘制，字符背景透明（保留底层内容）
 * @param       color : 字符前景色
 * @details
 *   字体点阵存储方式（以 12 号字体为例，字模尺寸 6×12）：
 *   - 每一列（y方向）共 12 像素 = 12 bit，分为 2 字节（高位优先，即 MSB 先出）
 *   - 每一行（x方向）共 6 列 → 6×2 = 12 字节/字符
 *   - csize = (size/8 + (size%8?1:0)) × (size/2)
 *            = 向上取整(高/8) × (宽=高/2)
 *     例：12×6→(12/8+1)×6 = 2×6 = 12 字节 ✓
 *         16×8→2×8 = 16 字节，24×12→3×12 = 36 字节，32×16→4×16 = 64 字节
 *   - 遍历顺序：外循环字节 t（按列展开），内循环位 t1 bit7→bit0（y从上到下）
 *   [改进] 逐点绘制效率低；较大字符（SIZE=32）共 64 字节/64 次 lcd_draw_point 调用；
 *           后续可改为行缓存+ltdc_fill 批量提交以提升速度。
 * @retval      无
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint32_t color)
{
    uint8_t temp, t1, t;
    uint16_t y0 = y;                                /* 记录列起始 y 坐标，每列绘制完后复位 */
    uint8_t csize = 0;
    uint8_t *pfont = 0;                             /* 指向目标字符点阵数据首地址 */

    /* csize = 每字节能覆盖的行数（向上取整）× 字符宽度（= 字体高度/2） */
    csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);
    chr = chr - ' ';    /* 转换为字库索引（字库从空格' '=0x20开始；chr-' '=0对应空格，1对应'!'…）*/

    switch (size)
    {
        case 12:
            pfont = (uint8_t *)asc2_1206[chr];  /* 6×12 点阵字体（宽6×高12） */
            break;

        case 16:
            pfont = (uint8_t *)asc2_1608[chr];  /* 8×16 点阵字体 */
            break;

        case 24:
            pfont = (uint8_t *)asc2_2412[chr];  /* 12×24 点阵字体 */
            break;

        case 32:
            pfont = (uint8_t *)asc2_3216[chr];  /* 16×32 点阵字体 */
            break;

        default:
            return ;   /* 不支持的字体大小，直接返回 */
    }

    for (t = 0; t < csize; t++)                     /* 遍历该字符的每个字节（每字节代表 8 个纵向像素）*/
    {
        temp = pfont[t];                            /* 取第 t 个字节的点阵数据 */

        for (t1 = 0; t1 < 8; t1++)                  /* 逐 bit 检查：bit7 对应最上方像素 */
        {
            if (temp & 0x80)                        /* 最高位为 1 → 有效像素，绘制前景色 */
            {
                lcd_draw_point(x, y, color);        /* 绘制字符像素点 */
            }
            else if (mode == 0)                     /* bit=0 且非叠加模式 → 绘制背景色（清除底层）*/
            {
                lcd_draw_point(x, y, g_back_color); /* 绘制背景色（g_back_color 由调用方提前设置）*/
            }
            /* mode==1 时：bit=0 跳过绘制，保持底层像素不变（透明效果）*/

            temp <<= 1;                             /* 左移1位，下一次检查次高位 */
            y++;                                    /* 向下移动到下一个像素行 */

            if (y >= lcddev.height)return;          /* 超区域了 */

            if ((y - y0) == size)                   /* 显示完一列了? */
            {
                y = y0;                             /* y坐标复位 */
                x++;                                /* x坐标递增 */

                if (x >= lcddev.width)return;       /* x坐标超区域了 */

                break;
            }
        }
    }
}

/**
 * @brief       整数幂次运算：计算 m^n
 * @param       m : 底数（uint8_t，典型值为 10，用于十进制位分解）
 * @param       n : 指数（uint8_t，为显示位数减1）
 * @retval      m 的 n 次方（uint32_t）
 * @details
 *   lcd_show_num/lcd_show_xnum 用此函数将整数各位分解：
 *     第 t 位数字 = (num / lcd_pow(10, len-t-1)) % 10
 *   例：num=1234, len=4:
 *     t=0: (1234/1000)%10=1, t=1: (1234/100)%10=2, ...
 *   [注意] 采用循环累乘，对 n≤9 的小指数完全足够；
 *          若 n 很大（如 n>9），result 会溢出 uint32_t（可显示范围上限 2^32≈4.3×10^9）。
 *   [改进] 可改为静态查找表 pow10[]={1,10,100,...,1000000000} 消除运行时乘法。
 */
static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;   /* 初始值为 m^0=1 */

    while (n--)            /* 循环 n 次，每次乘以底数 m */
    {
        result *= m;
    }

    return result;         /* 返回 m^n */
}

/**
 * @brief       显示 len 位十进制整数（高位前导零不显示，用空格占位）
 * @param       x,y   : 起始坐标（左上角，数字从左到右排列）
 * @param       num   : 要显示的数值（0 ~ 2^32-1）
 * @param       len   : 显示总位数（必须 ≥ num 实际位数，否则高位丢失）
 * @param       size  : 字体大小（像素高度）12/16/24/32
 * @param       color : 数字颜色
 * @details
 *   每个字符宽度 = size/2（等宽字体）；字符 x 坐标 = x + (size/2)*t。
 *   前导零抑制逻辑（enshow 标志）：
 *     - 从最高位（t=0）向最低位（t=len-1）逐位检查
 *     - 若某位为 0 且 enshow==0（尚未遇到非零位），显示空格占位
 *     - 一旦遇到非零位（enshow=1），之后所有位（包括0）正常显示
 *     - 末位（t==len-1）无论如何都显示，确保 num=0 时显示 '0'
 *   [改进] 无越界检查，若 x + (size/2)*(len-1) + size/2 > lcddev.width，
 *          字符会绘制到屏幕外（ltdc_draw_point 内部有越界保护，但会静默丢弃）。
 * @retval      无
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint32_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;   /* 前导零抑制标志：0=尚未显示非零位，1=已激活显示 */

    for (t = 0; t < len; t++)                                              /* 从最高位到最低位依次处理 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                      /* 提取第 t 位十进制数字 */

        if (enshow == 0 && t < (len - 1))                                  /* 前导零抑制：非末位且尚未激活 */
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color); /* 空格占位（不污染背景）*/
                continue;                                                  /* 跳过当前位，继续下一位 */
            }
            else
            {
                enshow = 1;                                                /* 遇到第一个非零位，后续全部显示 */
            }
        }

        /* 将数字 0~9 转换为 ASCII 字符 '0'~'9' 后显示（非叠加模式）*/
        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);
    }
}

/**
 * @brief       扩展数字显示：支持高位填零/空格，以及叠加/非叠加背景
 * @param       x,y   : 起始坐标
 * @param       num   : 要显示的数值（0 ~ 2^32-1）
 * @param       len   : 显示总位数
 * @param       size  : 字体大小 12/16/24/32
 * @param       mode  : 显示模式标志字节（位域）
 *              [7] = 1：前导零位显示 '0'（对齐补零，如 "0123"），
 *                  = 0：前导零位显示 ' '（空格占位，如 " 123"）
 *              [6:1]：保留（应为0）
 *              [0] = 1：叠加绘制（mode=1 传给 lcd_show_char，背景透明），
 *                  = 0：非叠加（用 g_back_color 清背景）
 * @param       color : 数字颜色
 * @details
 *   与 lcd_show_num 区别：mode[7] 控制前导位是否显示 '0'，常用于固定宽度显示场景
 *   （如传感器读数面板："0042" vs "  42"）。
 *   [改进] mode 参数语义不直观，位域定义建议改为具名枚举或 #define 常量。
 * @retval      无
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint32_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;   /* 前导零抑制标志（同 lcd_show_num）*/

    for (t = 0; t < len; t++)                                                            /* 从最高位到最低位 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                                    /* 提取第 t 位数字 0~9 */

        if (enshow == 0 && t < (len - 1))                                                /* 前导零抑制处理（末位不受控）*/
        {
            if (temp == 0)
            {
                if (mode & 0X80)                                                         /* mode[7]=1：用 '0' 填充前导零 */
                {
                    lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0X01, color); /* '0' 占位，叠加模式取 mode[0] */
                }
                else                                                                     /* mode[7]=0：用空格替代前导零 */
                {
                    lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0X01, color); /* 空格占位 */
                }

                continue;   /* 继续下一位，不进入后续显示逻辑 */
            }
            else
            {
                enshow = 1;                                                              /* 遇到非零位，激活后续所有位的显示 */
            }
        }

        /* 显示当前位数字（叠加模式由 mode[0] 控制）*/
        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0X01, color);
    }
}

/**
 * @brief       在指定矩形区域内显示 ASCII 字符串（自动换行，超出高度截断）
 * @param       x,y         : 起始坐标（区域左上角）
 * @param       width       : 显示区域宽度（像素），超出此宽度自动换行
 * @param       height      : 显示区域高度（像素），超出此高度停止显示
 * @param       size        : 字体高度 12/16/24/32（字符宽度 = size/2）
 * @param       p           : C 字符串指针（必须以 '\0' 结尾）
 * @param       color       : 字符颜色
 * @details
 *   - 有效字符范围：ASCII 可打印字符 0x20（' '）~ 0x7E（'~'）
 *     遇到 '\0' 或任何 < 0x20 或 > 0x7E 的字节（包括中文 UTF-8 多字节）立即停止。
 *   - 自动换行：当 x + size/2 >= x0+width 时，x 重置，y 向下移动 size 像素。
 *   - 区域截断：y >= y0+height 时退出循环，防止绘制超出区域边界。
 *   [注意] 不支持中文（CJK Unicode 需 LVGL lv_label + 矢量字体引擎）。
 *   [注意] p 为非 const 指针，但函数内只读不写，const 修饰更安全。
 *   [改进] 没有处理 ASCII 控制字符（如 '\n' 换行、'\t' 制表），全部当作终止符。
 *   [改进] 非叠加（mode=0）固定传入，无法在已有图像上透明叠加文字。
 * @retval      无
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint32_t color)
{
    uint8_t x0 = x;       /* 记录列起始 x，用于换行时复位 */

    width  += x;           /* 转换为绝对 x 边界（右边界）*/
    height += y;           /* 转换为绝对 y 边界（下边界）*/

    while ((*p <= '~') && (*p >= ' '))   /* 判断是否为可打印 ASCII（0x20~0x7E）—— 超范围或 '\0' 退出 */
    {
        if (x >= width)    /* 当前字符将超出右边界 → 换行 */
        {
            x = x0;        /* x 复位到列起始位置 */
            y += size;     /* y 向下移动一行（行高 = 字体高度）*/
        }

        if (y >= height)   /* 超出区域下边界 → 停止显示 */
        {
            break;
        }

        lcd_show_char(x, y, *p, size, 0, color);  /* 绘制单个字符（非叠加模式，固定覆盖背景）*/
        x += size / 2;     /* x 前进一个字符宽度（等宽字体：宽度 = 高度/2）*/
        p++;               /* 指针移向下一个字符 */
    }
}








