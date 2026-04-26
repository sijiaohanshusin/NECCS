/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp_template.h"
#include "../../lvgl.h"
#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "app_user_config.h"
#include "mpu.h"

#include <stdint.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static void s_panel_clear_to_chroma_key(void);
static void s_clean_dcache_by_addr(const void *addr, uint32_t size);

/**********************
 *  STATIC VARIABLES
 **********************/
#define LV_PORT_PANEL_FB_ADDR   0xC0800000u
#define LV_PORT_PANEL_FB_BYTES  (LV_PORT_OVERLAY_W * LV_PORT_OVERLAY_H * 2u)
#define LV_PORT_PANEL_FB_LIMIT  0xC0940000u

#if ((LV_PORT_PANEL_FB_ADDR + LV_PORT_PANEL_FB_BYTES) > LV_PORT_PANEL_FB_LIMIT)
#error "LVGL panel framebuffer must stay inside the reserved SDRAM window"
#endif

static uint16_t * const s_panel_fb = (uint16_t *)LV_PORT_PANEL_FB_ADDR;
__SECTION_D2_SRAM static lv_color_t s_draw_buf_mem[LV_PORT_OVERLAY_W * LV_PORT_OVERLAY_DRAW_BUF_ROWS];

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_disp_t *s_disp = NULL;
static uint8_t s_disp_registered = 0u;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

uint16_t lv_port_disp_get_origin_x(void)
{
    return (lcddev.width > (LV_PORT_OVERLAY_W + LV_PORT_OVERLAY_MARGIN_X))
         ? (uint16_t)(lcddev.width - LV_PORT_OVERLAY_W - LV_PORT_OVERLAY_MARGIN_X)
         : 0u;
}

uint16_t lv_port_disp_get_origin_y(void)
{
    return (lcddev.height > (LV_PORT_OVERLAY_H + LV_PORT_OVERLAY_MARGIN_Y))
         ? (uint16_t)(lcddev.height - LV_PORT_OVERLAY_H - LV_PORT_OVERLAY_MARGIN_Y)
         : 0u;
}

void lv_port_disp_reconfigure(void)
{
    s_panel_clear_to_chroma_key();
}

void lv_port_disp_init(void)
{
    disp_init();
    s_panel_clear_to_chroma_key();

    if (s_disp_registered != 0u)
    {
        s_disp_drv.hor_res = LV_PORT_OVERLAY_W;
        s_disp_drv.ver_res = LV_PORT_OVERLAY_H;
        if (s_disp != NULL)
        {
            lv_disp_drv_update(s_disp, &s_disp_drv);
        }
        return;
    }

    lv_disp_draw_buf_init(&s_draw_buf,
                          s_draw_buf_mem,
                          NULL,
                          LV_PORT_OVERLAY_W * LV_PORT_OVERLAY_DRAW_BUF_ROWS);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LV_PORT_OVERLAY_W;
    s_disp_drv.ver_res = LV_PORT_OVERLAY_H;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf;

    s_disp = lv_disp_drv_register(&s_disp_drv);
    s_disp_registered = (s_disp != NULL) ? 1u : 0u;
}

void lv_port_disp_blit_to_display(void)
{
    uint16_t origin_x;
    uint16_t origin_y;
    uint16_t *dst_base;
    uint16_t y;

    if ((lcddev.width < LV_PORT_OVERLAY_W) || (lcddev.height < LV_PORT_OVERLAY_H))
    {
        return;
    }

    /* Let queued DMA2D work finish before the CPU overlays the LVGL pixels. */
    (void)ltdc_draw_flush(APP_DISPLAY_DMA2D_TIMEOUT);

    origin_x = lv_port_disp_get_origin_x();
    origin_y = lv_port_disp_get_origin_y();
    dst_base = (uint16_t *)ltdc_get_backbuf_addr();
    if (dst_base == NULL)
    {
        return;
    }

    for (y = 0u; y < LV_PORT_OVERLAY_H; y++)
    {
        uint16_t *dst_row = dst_base + ((uint32_t)(origin_y + y) * (uint32_t)lcddev.width) + origin_x;
        const uint16_t *src_row = &s_panel_fb[(uint32_t)y * LV_PORT_OVERLAY_W];
        uint16_t x;

        for (x = 0u; x < LV_PORT_OVERLAY_W; x++)
        {
            if (src_row[x] != LV_PORT_OVERLAY_CHROMA_KEY_RGB565)
            {
                dst_row[x] = src_row[x];
            }
        }
    }

    s_clean_dcache_by_addr(dst_base + ((uint32_t)origin_y * (uint32_t)lcddev.width) + origin_x,
                           ((((uint32_t)LV_PORT_OVERLAY_H - 1u) * (uint32_t)lcddev.width) +
                            (uint32_t)LV_PORT_OVERLAY_W) * sizeof(uint16_t));
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void disp_init(void)
{
    /* The legacy display pipeline already initialized LTDC/LCD before LVGL starts. */
}

static void s_panel_clear_to_chroma_key(void)
{
    uint32_t i;

    for (i = 0u; i < (uint32_t)(LV_PORT_OVERLAY_W * LV_PORT_OVERLAY_H); i++)
    {
        s_panel_fb[i] = LV_PORT_OVERLAY_CHROMA_KEY_RGB565;
    }
}

static void s_clean_dcache_by_addr(const void *addr, uint32_t size)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start_addr;
    uintptr_t end_addr;
    uintptr_t aligned_addr;
    uint32_t aligned_size;

    if ((addr == NULL) || (size == 0u))
    {
        return;
    }

    start_addr = (uintptr_t)addr;
    end_addr = start_addr + (uintptr_t)size;
    aligned_addr = start_addr & ~(uintptr_t)31u;
    aligned_size = (uint32_t)(((end_addr + 31u) & ~(uintptr_t)31u) - aligned_addr);
    SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
#else
    LV_UNUSED(addr);
    LV_UNUSED(size);
#endif
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t clip_x1;
    int32_t clip_y1;
    int32_t clip_x2;
    int32_t clip_y2;
    int32_t src_stride;
    int32_t y;

    if ((area == NULL) || (color_p == NULL))
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    if ((area->x2 < 0) || (area->y2 < 0) ||
        (area->x1 >= (lv_coord_t)LV_PORT_OVERLAY_W) ||
        (area->y1 >= (lv_coord_t)LV_PORT_OVERLAY_H))
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    clip_x1 = (area->x1 < 0) ? 0 : area->x1;
    clip_y1 = (area->y1 < 0) ? 0 : area->y1;
    clip_x2 = (area->x2 >= (lv_coord_t)LV_PORT_OVERLAY_W) ? ((int32_t)LV_PORT_OVERLAY_W - 1) : area->x2;
    clip_y2 = (area->y2 >= (lv_coord_t)LV_PORT_OVERLAY_H) ? ((int32_t)LV_PORT_OVERLAY_H - 1) : area->y2;
    src_stride = (int32_t)(area->x2 - area->x1 + 1);

    for (y = clip_y1; y <= clip_y2; y++)
    {
        uint16_t *dst_row = &s_panel_fb[(uint32_t)y * LV_PORT_OVERLAY_W + (uint32_t)clip_x1];
        lv_color_t *src_row = &color_p[(uint32_t)(y - area->y1) * (uint32_t)src_stride + (uint32_t)(clip_x1 - area->x1)];
        int32_t x;

        for (x = clip_x1; x <= clip_x2; x++)
        {
            dst_row[x - clip_x1] = src_row[x - clip_x1].full;
        }
    }

    lv_disp_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
