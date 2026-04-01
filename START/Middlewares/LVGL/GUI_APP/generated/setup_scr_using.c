/*
* Copyright 2026 NXP
* NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"
#include "lv_port_disp_template.h"

LV_FONT_DECLARE(Font18);

#define UI_TXT_HOME  "\xE9\xA6\x96\xE9\xA1\xB5"

void setup_scr_using(lv_ui *ui)
{
	//Write codes using
	ui->using = lv_obj_create(NULL);
	lv_obj_set_size(ui->using, 800, 480);
	lv_obj_clear_flag(ui->using, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(ui->using, LV_SCROLLBAR_MODE_OFF);

	//Write style for using, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_bg_color(ui->using, lv_color_hex(LV_PORT_OVERLAY_CHROMA_KEY_HEX), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->using, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->using, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(ui->using, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes using_btn_1
	ui->using_btn_1 = lv_btn_create(ui->using);
	ui->using_btn_1_label = lv_label_create(ui->using_btn_1);
	lv_label_set_text(ui->using_btn_1_label, UI_TXT_HOME);
	lv_label_set_long_mode(ui->using_btn_1_label, LV_LABEL_LONG_WRAP);
	lv_obj_align(ui->using_btn_1_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_pad_all(ui->using_btn_1, 0, LV_STATE_DEFAULT);
	lv_obj_set_pos(ui->using_btn_1, 8, 414);
	lv_obj_set_size(ui->using_btn_1, 56, 56);
	lv_obj_set_scrollbar_mode(ui->using_btn_1, LV_SCROLLBAR_MODE_OFF);

	//Write style for using_btn_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_bg_opa(ui->using_btn_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->using_btn_1, lv_color_hex(0x009ea9), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->using_btn_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->using_btn_1, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->using_btn_1, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_color(ui->using_btn_1, lv_color_hex(0x0d4b3b), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_opa(ui->using_btn_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_spread(ui->using_btn_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_ofs_x(ui->using_btn_1, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_ofs_y(ui->using_btn_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(ui->using_btn_1, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(ui->using_btn_1, &Font18, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(ui->using_btn_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Update current screen layout.
	lv_obj_update_layout(ui->using);

	//Init events for screen.
	events_init_using(ui);
}
