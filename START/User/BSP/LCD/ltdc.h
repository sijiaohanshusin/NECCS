/**
 * @file    ltdc.h
 * @brief   LTDC + DMA2D 显示驱动接口
 * @details 提供 LCD 初始化、图层控制、像素读写、区域填充与双缓冲换页接口。 */

#ifndef LTDC_H
#define LTDC_H

#include "main.h"

/* 8-inch 800x1280 panel compatibility switch.
 * 0: disabled (default), 1: enabled.
 */
#define RGB_80_8001280       0

/* If panel ID pins are floating/unwired, fall back to a fixed panel profile. */
#define LTDC_ENABLE_ID_FALLBACK  1
#define LTDC_PANEL_FALLBACK_ID   0X4384U

/* Force a known panel profile for this project. Set to 0U to enable auto-detect. */
#define LTDC_FORCE_PANEL_ID      0X4384U

/* LTDC panel timing and active layer state. */
typedef struct
{
    uint32_t pwidth;       /* Physical panel width (fixed by hardware). */
    uint32_t pheight;      /* Physical panel height (fixed by hardware). */
    uint16_t hsw;          /* Horizontal sync width. */
    uint16_t vsw;          /* Vertical sync width. */
    uint16_t hbp;          /* Horizontal back porch. */
    uint16_t vbp;          /* Vertical back porch. */
    uint16_t hfp;          /* Horizontal front porch. */
    uint16_t vfp;          /* Vertical front porch. */
    uint8_t activelayer;   /* Current LTDC layer index: 0/1. */
    uint8_t dir;           /* Display direction: 0=portrait, 1=landscape. */
    uint16_t width;        /* Logical rendering width. */
    uint16_t height;       /* Logical rendering height. */
    uint32_t pixsize;      /* Bytes per pixel for current pixel format. */
    uint8_t pixformat;     /* Pixel format, see LTDC_PIXFORMAT_* macros. */
}_ltdc_dev;

extern _ltdc_dev lcdltdc;                     /* Runtime LTDC panel state. */
extern LTDC_HandleTypeDef g_ltdc_handle;      /* HAL LTDC handle. */
extern DMA2D_HandleTypeDef g_dma2d_handle;    /* HAL DMA2D handle. */
extern volatile uint32_t g_ltdc_init_stage;
extern volatile uint32_t g_ltdc_dma2d_timeout_count;
extern volatile uint32_t g_ltdc_dma2d_transfer_count;
extern volatile uint32_t g_ltdc_dma2d_sw_fallback_count;
extern volatile uint16_t g_ltdc_panel_id;
extern volatile uint32_t g_ltdc_swap_count;
extern volatile uint32_t g_ltdc_swap_pending_count;
extern volatile uint32_t g_ltdc_swap_error_count;

#define LTDC_PIXFORMAT_ARGB8888      0X00     /* ARGB8888 format */
#define LTDC_PIXFORMAT_RGB888        0X01     /* RGB888 format */
#define LTDC_PIXFORMAT_RGB565        0X02     /* RGB565 format */
#define LTDC_PIXFORMAT_ARGB1555      0X03     /* ARGB1555 format */
#define LTDC_PIXFORMAT_ARGB4444      0X04     /* ARGB4444 format */
#define LTDC_PIXFORMAT_L8            0X05     /* L8 format */
#define LTDC_PIXFORMAT_AL44          0X06     /* AL44 format */
#define LTDC_PIXFORMAT_AL88          0X07     /* AL88 format */

/* LTDC_DE/VSYNC/HSYNC/CLK/BL/RST pin mapping.
 * RGB data pins (R0~R7/G0~G7/B0~B7) are configured in ltdc_init().
 */
#define LTDC_DE_GPIO_PORT               GPIOF
#define LTDC_DE_GPIO_PIN                GPIO_PIN_10
#define LTDC_DE_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)

#define LTDC_VSYNC_GPIO_PORT            GPIOI
#define LTDC_VSYNC_GPIO_PIN             GPIO_PIN_9
#define LTDC_VSYNC_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)

#define LTDC_HSYNC_GPIO_PORT            GPIOI
#define LTDC_HSYNC_GPIO_PIN             GPIO_PIN_10
#define LTDC_HSYNC_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)

#define LTDC_CLK_GPIO_PORT              GPIOG
#define LTDC_CLK_GPIO_PIN               GPIO_PIN_7
#define LTDC_CLK_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)

#define LTDC_BL_GPIO_PORT               GPIOD
#define LTDC_BL_GPIO_PIN                GPIO_PIN_12
#define LTDC_BL_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

#define LTDC_RST_GPIO_PORT              GPIOD
#define LTDC_RST_GPIO_PIN               GPIO_PIN_11
#define LTDC_RST_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

/* Default output pixel format (RGB565 in this project). */
#define LTDC_PIXFORMAT           LTDC_PIXFORMAT_RGB565

/* Default layer background color: ARGB8888 black. */
#define LTDC_BACKLAYERCOLOR      0X00000000

/* Framebuffer base in SDRAM. */
#define LTDC_FRAME_BUF_ADDR      0XC0000000U
#define LTDC_TARGET_WIDTH        800U
#define LTDC_TARGET_HEIGHT       480U

/* Backlight polarity: 0=active-high, 1=active-low. */
#define LTDC_BL_ACTIVE_LOW       0

/* Diagnostic switch: 1=configure PE5 as LTDC_G0, 0=leave PE5 to SAI1_SCK_A. */
#define LTDC_USE_PE5_G0          0

/* Bring-up switch: 1=use DMA2D acceleration, 0=force software fill path. */
#define LTDC_ENABLE_DMA2D        1

/* Backlight control helper. */
#define LTDC_BL(x)      do{ \
                            HAL_GPIO_WritePin(LTDC_BL_GPIO_PORT, LTDC_BL_GPIO_PIN, \
                                ((x) ? (LTDC_BL_ACTIVE_LOW ? GPIO_PIN_RESET : GPIO_PIN_SET) \
                                     : (LTDC_BL_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET))); \
                        }while(0)

/* Panel reset pin control helper. */
#define LTDC_RST(x)     do{ x ? \
                            HAL_GPIO_WritePin(LTDC_RST_GPIO_PORT, LTDC_RST_GPIO_PIN, GPIO_PIN_SET) : \
                            HAL_GPIO_WritePin(LTDC_RST_GPIO_PORT, LTDC_RST_GPIO_PIN, GPIO_PIN_RESET); \
                        }while(0)

void ltdc_switch(uint8_t sw); /* LTDC global enable/disable */
void ltdc_layer_switch(uint8_t layerx, uint8_t sw); /* Enable/disable layer */
void ltdc_select_layer(uint8_t layerx); /* Select active layer */
void ltdc_display_dir(uint8_t dir); /* Set display direction */
void ltdc_draw_point(uint16_t x, uint16_t y, uint32_t color); /* Draw one pixel */
uint32_t ltdc_read_point(uint16_t x, uint16_t y); /* Read one pixel */
void ltdc_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color); /* Solid rectangle fill (DMA2D path) */
void ltdc_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color); /* Color buffer fill (DMA2D path) */
void ltdc_clear(uint32_t color); /* Clear full frame */
uint8_t ltdc_clk_set(uint32_t pll3n, uint32_t pll3m, uint32_t pll3r); /* Configure LTDC pixel clock */
void ltdc_layer_window_config(uint8_t layerx, uint16_t sx, uint16_t sy, uint16_t width, uint16_t height); /* Configure layer window */
void ltdc_layer_parameter_config(uint8_t layerx, uint32_t bufaddr, uint8_t pixformat, uint8_t alpha, uint8_t alpha0, uint8_t bfac1, uint8_t bfac2, uint32_t bkcolor); /* Configure layer parameters */
uint16_t ltdc_panelid_read(void); /* Read panel ID */
void ltdc_init(void); /* Initialize LTDC + DMA2D + panel */

uint32_t ltdc_get_frontbuf_addr(void);
uint32_t ltdc_get_backbuf_addr(void);
void ltdc_request_swap(void);
uint8_t ltdc_is_swap_pending(void);
uint8_t ltdc_wait_for_swap_complete(uint32_t timeout_ms);

uint8_t ltdc_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);
uint8_t ltdc_color_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);
uint8_t ltdc_copy_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint16_t *src, uint16_t src_stride);
uint8_t ltdc_l8_fill_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint8_t *src_l8, uint16_t src_stride);
uint8_t ltdc_a8_blend_async(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, const uint8_t *src_a8, uint16_t src_stride, uint16_t color565);
uint8_t ltdc_draw_flush(uint32_t timeout_loop);

#endif
