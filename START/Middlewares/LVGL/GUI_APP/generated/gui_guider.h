/*
* Copyright 2026 NXP
* NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef GUI_GUIDER_H
#define GUI_GUIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef struct
{
  
	lv_obj_t *home;
	bool home_del;
	lv_obj_t *home_start;
	lv_obj_t *home_qrcode_1;
	lv_obj_t *home_img_1;
	lv_obj_t *home_spangroup_1;
	lv_obj_t *home_spangroup_2;
	lv_obj_t *home_btn_1;
	lv_obj_t *home_btn_1_label;
	lv_obj_t *home_spangroup_3;
	lv_obj_t *home_btn_2;
	lv_obj_t *home_btn_2_label;
	lv_obj_t *home_cont_1;
	lv_obj_t *home_cb_1;
	lv_obj_t *home_cb_2;
	lv_obj_t *using;
	bool using_del;
	lv_obj_t *using_btn_1;
	lv_obj_t *using_btn_1_label;
}lv_ui;

void ui_init_style(lv_style_t * style);
void init_scr_del_flag(lv_ui *ui);
void setup_ui(lv_ui *ui);
extern lv_ui guider_ui;

void setup_scr_home(lv_ui *ui);
void setup_scr_using(lv_ui *ui);
LV_IMG_DECLARE(_start2_alpha_100x100);

LV_FONT_DECLARE(lv_font_simsun_60)
LV_FONT_DECLARE(lv_font_montserratMedium_25)
LV_FONT_DECLARE(lv_font_simsun_30)
LV_FONT_DECLARE(lv_font_montserratMedium_16)
LV_FONT_DECLARE(lv_font_montserratMedium_12)
LV_FONT_DECLARE(lv_font_simsun_18)
LV_FONT_DECLARE(lv_font_simsun_16)


#ifdef __cplusplus
}
#endif
#endif
