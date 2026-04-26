/**
 * @file lv_port_disp_templ.h
 *
 */

 /*Copy this file as "lv_port_disp.h" and set this value to "1" to enable content*/
#if 1

#ifndef LV_PORT_DISP_TEMPL_H
#define LV_PORT_DISP_TEMPL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>
#include "lvgl/lvgl.h"

/*********************
 *      DEFINES
 *********************/
#define LV_PORT_OVERLAY_W                  1024u
#define LV_PORT_OVERLAY_H                  600u
#define LV_PORT_OVERLAY_DRAW_BUF_ROWS      16u
#define LV_PORT_OVERLAY_MARGIN_X           0u
#define LV_PORT_OVERLAY_MARGIN_Y           0u
#define LV_PORT_OVERLAY_CHROMA_KEY_HEX     0xFF00FFu
#define LV_PORT_OVERLAY_CHROMA_KEY_RGB565  0xF81Fu

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void lv_port_disp_init(void);
void lv_port_disp_reconfigure(void);
void lv_port_disp_blit_to_display(void);
uint16_t lv_port_disp_get_origin_x(void);
uint16_t lv_port_disp_get_origin_y(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_DISP_TEMPL_H*/

#endif /*Disable/Enable content*/
