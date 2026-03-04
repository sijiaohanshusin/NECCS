/**
 ****************************************************************************************************
 * @file        ltdc.h
 * @version     V1.0
 * @brief       LTDC 椹卞姩浠ｇ爜
 ****************************************************************************************************
 * @attention   Waiken-Smart 鎱у嫟鏅鸿繙
 *
 * 瀹為獙骞冲彴:    STM32H743IIT6灏忕郴缁熸澘
 *
 ****************************************************************************************************
 */

#ifndef _LTDC_H
#define _LTDC_H

#include "main.h"


/* 閫夋嫨鏄惁浣跨敤8瀵?00X1280RGB灞?
 * 0: 娌℃湁浣跨敤8瀵窻GB灞?
 * 1: 浣跨敤8瀵窻GB灞?
 */
#define RGB_80_8001280       0     /* 榛樿涓嶄娇鐢?瀵?00X1280RGB灞?*/


/* If panel ID pins are floating/unwired, fall back to a fixed panel profile. */
#define LTDC_ENABLE_ID_FALLBACK  1
#define LTDC_PANEL_FALLBACK_ID   0X4384U

/* Force a known panel profile for this project. Set to 0U to enable auto-detect. */
#define LTDC_FORCE_PANEL_ID      0X4384U

/* LCD LTDC閲嶈鍙傛暟闆?*/
typedef struct  
{
    uint32_t pwidth;       /* LTDC闈㈡澘鐨勫搴?鍥哄畾鍙傛暟,涓嶉殢鏄剧ず鏂瑰悜鏀瑰彉,濡傛灉涓?,璇存槑娌℃湁浠讳綍RGB灞忔帴鍏?*/
    uint32_t pheight;      /* LTDC闈㈡澘鐨勯珮搴?鍥哄畾鍙傛暟,涓嶉殢鏄剧ず鏂瑰悜鏀瑰彉 */
    uint16_t hsw;          /* 姘村钩鍚屾瀹藉害 */
    uint16_t vsw;          /* 鍨傜洿鍚屾瀹藉害 */
    uint16_t hbp;          /* 姘村钩鍚庡粖 */
    uint16_t vbp;          /* 鍨傜洿鍚庡粖 */
    uint16_t hfp;          /* 姘村钩鍓嶅粖 */
    uint16_t vfp;          /* 鍨傜洿鍓嶅粖  */
    uint8_t activelayer;   /* 褰撳墠灞傜紪鍙?0/1 */
    uint8_t dir;           /* 0,绔栧睆;1,妯睆; */
    uint16_t width;        /* LTDC瀹藉害 */
    uint16_t height;       /* LTDC楂樺害 */
    uint32_t pixsize;      /* 姣忎釜鍍忕礌鎵€鍗犲瓧鑺傛暟 */
    uint8_t pixformat;     /* 棰滆壊鍍忕礌鏍煎紡 */
}_ltdc_dev; 

extern _ltdc_dev lcdltdc;                     /* 绠＄悊LCD LTDC鍙傛暟 */
extern LTDC_HandleTypeDef g_ltdc_handle;      /* LTDC鍙ユ焺 */
extern DMA2D_HandleTypeDef g_dma2d_handle;    /* DMA2D鍙ユ焺 */
extern volatile uint32_t g_ltdc_init_stage;
extern volatile uint32_t g_ltdc_dma2d_timeout_count;
extern volatile uint32_t g_ltdc_dma2d_transfer_count;
extern volatile uint32_t g_ltdc_dma2d_sw_fallback_count;
extern volatile uint16_t g_ltdc_panel_id;
extern volatile uint32_t g_ltdc_swap_count;
extern volatile uint32_t g_ltdc_swap_pending_count;
extern volatile uint32_t g_ltdc_swap_error_count;


#define LTDC_PIXFORMAT_ARGB8888      0X00     /* ARGB8888鏍煎紡 */
#define LTDC_PIXFORMAT_RGB888        0X01     /* RGB888鏍煎紡 */
#define LTDC_PIXFORMAT_RGB565        0X02     /* RGB565鏍煎紡 */
#define LTDC_PIXFORMAT_ARGB1555      0X03     /* ARGB1555鏍煎紡 */
#define LTDC_PIXFORMAT_ARGB4444      0X04     /* ARGB4444鏍煎紡 */
#define LTDC_PIXFORMAT_L8            0X05     /* L8鏍煎紡 */
#define LTDC_PIXFORMAT_AL44          0X06     /* AL44鏍煎紡 */
#define LTDC_PIXFORMAT_AL88          0X07     /* AL88鏍煎紡 */

/******************************************************************************************/
/* LTDC_DE/VSYNC/HSYNC/CLK/BL/RST 寮曡剼 瀹氫箟 
 * LTDC_R0~R7, G0~G7, B0~B7,鐢变簬寮曡剼澶,灏变笉鍦ㄨ繖閲屽畾涔変簡,鐩存帴鍦╨tcd_init閲岄潰淇敼.鎵€浠ュ湪绉绘鐨勬椂鍊?
 * 闄や簡鏀硅繖6涓狪O鍙? 杩樺緱鏀筶tcd_init閲岄潰鐨凴0~R7, G0~G7, B0~B7鎵€鍦ㄧ殑IO鍙?
 */

#define LTDC_DE_GPIO_PORT               GPIOF
#define LTDC_DE_GPIO_PIN                GPIO_PIN_10
#define LTDC_DE_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)    /* 鎵€鍦↖O鍙ｆ椂閽熶娇鑳?*/

#define LTDC_VSYNC_GPIO_PORT            GPIOI
#define LTDC_VSYNC_GPIO_PIN             GPIO_PIN_9
#define LTDC_VSYNC_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)    /* 鎵€鍦↖O鍙ｆ椂閽熶娇鑳?*/

#define LTDC_HSYNC_GPIO_PORT            GPIOI
#define LTDC_HSYNC_GPIO_PIN             GPIO_PIN_10
#define LTDC_HSYNC_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)    /* 鎵€鍦↖O鍙ｆ椂閽熶娇鑳?*/

#define LTDC_CLK_GPIO_PORT              GPIOG
#define LTDC_CLK_GPIO_PIN               GPIO_PIN_7
#define LTDC_CLK_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)    /* 鎵€鍦↖O鍙ｆ椂閽熶娇鑳?*/

#define LTDC_BL_GPIO_PORT               GPIOD
#define LTDC_BL_GPIO_PIN                GPIO_PIN_12
#define LTDC_BL_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)    /* 鎵€鍦↖O鍙ｆ椂閽熶娇鑳?*/

#define LTDC_RST_GPIO_PORT              GPIOD
#define LTDC_RST_GPIO_PIN               GPIO_PIN_11
#define LTDC_RST_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)    /* 鎵€鍦↖O鍙ｆ椂閽熶娇鑳?*/


/* 瀹氫箟棰滆壊鍍忕礌鏍煎紡,涓€鑸敤RGB565 */
#define LTDC_PIXFORMAT           LTDC_PIXFORMAT_RGB565

/* 瀹氫箟榛樿鑳屾櫙灞傞鑹?*/
#define LTDC_BACKLAYERCOLOR      0X00000000

/* LTDC甯х紦鍐插尯棣栧湴鍧€,杩欓噷瀹氫箟鍦⊿DRAM閲岄潰. */
#define LTDC_FRAME_BUF_ADDR      0XC0000000U
#define LTDC_TARGET_WIDTH        800U
#define LTDC_TARGET_HEIGHT       480U

/* Backlight polarity: 0=active-high, 1=active-low. */
#define LTDC_BL_ACTIVE_LOW       0

/* Diagnostic switch: 1=configure PE5 as LTDC_G0, 0=leave PE5 to SAI1_SCK_A. */
#define LTDC_USE_PE5_G0          0

/* Bring-up switch: 1=use DMA2D acceleration, 0=force software fill path. */
#define LTDC_ENABLE_DMA2D        1

/* LTDC鑳屽厜鎺у埗 */
#define LTDC_BL(x)      do{ \
                            HAL_GPIO_WritePin(LTDC_BL_GPIO_PORT, LTDC_BL_GPIO_PIN, \
                                ((x) ? (LTDC_BL_ACTIVE_LOW ? GPIO_PIN_RESET : GPIO_PIN_SET) \
                                     : (LTDC_BL_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET))); \
                        }while(0)

/* LTDC澶嶄綅寮曡剼 */
#define LTDC_RST(x)     do{ x ? \
                            HAL_GPIO_WritePin(LTDC_RST_GPIO_PORT, LTDC_RST_GPIO_PIN, GPIO_PIN_SET) : \
                            HAL_GPIO_WritePin(LTDC_RST_GPIO_PORT, LTDC_RST_GPIO_PIN, GPIO_PIN_RESET); \
                        }while(0)

/******************************************************************************************/

void ltdc_switch(uint8_t sw);                                                                                   /* LTDC寮€鍏?*/
void ltdc_layer_switch(uint8_t layerx, uint8_t sw);                                                             /* 灞傚紑鍏?*/
void ltdc_select_layer(uint8_t layerx);                                                                         /* 灞傞€夋嫨 */
void ltdc_display_dir(uint8_t dir);                                                                             /* LTDC鏄剧ず鏂瑰悜璁剧疆 */
void ltdc_draw_point(uint16_t x, uint16_t y, uint32_t color);                                                   /* LTDC鐢荤偣鍑芥暟 */
uint32_t ltdc_read_point(uint16_t x, uint16_t y);                                                               /* LTDC璇荤偣鍑芥暟 */
void ltdc_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);                             /* 鐭╁舰鍗曡壊濉厖, DMA2D濉厖 */
void ltdc_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);                      /* 鐭╁舰褰╄壊濉厖, DMA2D濉厖 */
void ltdc_clear(uint32_t color);                                                                                /* LTDC娓呭睆鍑芥暟 */
uint8_t ltdc_clk_set(uint32_t pll3n, uint32_t pll3m, uint32_t pll3r);                                           /* LTDC鏃堕挓閰嶇疆 */
void ltdc_layer_window_config(uint8_t layerx, uint16_t sx, uint16_t sy, uint16_t width, uint16_t height);       /* LTDC灞傜獥鍙ｈ缃?*/
void ltdc_layer_parameter_config(uint8_t layerx, uint32_t bufaddr, uint8_t pixformat, uint8_t alpha, uint8_t alpha0, uint8_t bfac1, uint8_t bfac2, uint32_t bkcolor); /* LTDC灞傚熀鏈弬鏁拌缃?*/
uint16_t ltdc_panelid_read(void);                                                                               /* LTDC 闈㈡澘ID璇诲彇鍑芥暟 */
void ltdc_init(void);                                                                                           /* LTDC鍒濆鍖栧嚱鏁?*/

uint32_t ltdc_get_frontbuf_addr(void);
uint32_t ltdc_get_backbuf_addr(void);
void ltdc_request_swap(void);
uint8_t ltdc_is_swap_pending(void);

uint8_t ltdc_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);
uint8_t ltdc_color_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);
uint8_t ltdc_l8_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint8_t *src_l8, uint16_t src_stride);
uint8_t ltdc_a8_blend_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint8_t *src_a8, uint16_t src_stride, uint16_t color565);
uint8_t ltdc_draw_flush(uint32_t timeout_loop);

#endif 

                        
                        
                    


