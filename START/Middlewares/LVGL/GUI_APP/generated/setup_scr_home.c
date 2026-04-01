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
#include <string.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"
#include "lv_port_disp_template.h"

LV_FONT_DECLARE(Font16);
LV_FONT_DECLARE(Font18);
LV_FONT_DECLARE(Font30);
LV_FONT_DECLARE(Font60);

#define UI_TXT_START      "\xE5\xBC\x80\xE5\xA7\x8B"
#define UI_TXT_SETTINGS   "\xE8\xAE\xBE\xE7\xBD\xAE"
#define UI_TXT_SCAN_MORE  "\xE6\x89\xAB\xE6\x88\x91\xE4\xBA\x86\xE8\xA7\xA3\xE6\x9B\xB4\xE5\xA4\x9A"
#define UI_TXT_DEMO_1     "\xE5\x8A\x9F\xE8\x83\xBD""1""\xE7\xA4\xBA\xE6\x84\x8F"
#define UI_TXT_DEMO_2     "\xE5\x8A\x9F\xE8\x83\xBD""2""\xE7\xA4\xBA\xE6\x84\x8F"
#define UI_TXT_TITLE      "\xE5\xA3\xB0\xE5\xAD\xA6\xE6\x88\x90\xE5\x83\x8F\xE4\xBB\xAA"

void setup_scr_home(lv_ui *ui)
{
	//Write codes home
	ui->home = lv_obj_create(NULL);
	lv_obj_set_size(ui->home, 800, 480);
	lv_obj_clear_flag(ui->home, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(ui->home, LV_SCROLLBAR_MODE_OFF);

	//Write style for home, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_bg_color(ui->home, lv_color_hex(0x11132d), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->home, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(ui->home, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes home_start
	ui->home_start = lv_obj_create(ui->home);
	lv_obj_set_pos(ui->home_start, 0, 0);
	lv_obj_set_size(ui->home_start, 800, 480);
	lv_obj_clear_flag(ui->home_start, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(ui->home_start, LV_SCROLLBAR_MODE_OFF);

	//Write style for home_start, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_border_width(ui->home_start, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(ui->home_start, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(ui->home_start, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_start, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home_start, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_start, lv_color_hex(0x11132d), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_top(ui->home_start, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_bottom(ui->home_start, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_left(ui->home_start, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_right(ui->home_start, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->home_start, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes home_qrcode_1
	ui->home_qrcode_1 = lv_qrcode_create(ui->home_start, 100, lv_color_hex(0x2cebcf), lv_color_hex(0x000000));
	{
		const char *home_qrcode_1_data = "https://www.kdocs.cn/l/ckkhoiIDxY3q";
		lv_qrcode_update(ui->home_qrcode_1, home_qrcode_1_data, strlen(home_qrcode_1_data));
	}
	lv_obj_set_pos(ui->home_qrcode_1, 668, 349);
	lv_obj_set_size(ui->home_qrcode_1, 100, 100);

	//Write codes home_img_1
	ui->home_img_1 = lv_img_create(ui->home_start);
	lv_obj_add_flag(ui->home_img_1, LV_OBJ_FLAG_CLICKABLE);
	lv_img_set_src(ui->home_img_1, &_start2_alpha_100x100);
	lv_img_set_pivot(ui->home_img_1, 50,50);
	lv_img_set_angle(ui->home_img_1, 0);
	lv_obj_set_pos(ui->home_img_1, 127, 116);
	lv_obj_set_size(ui->home_img_1, 100, 100);
	lv_obj_set_scrollbar_mode(ui->home_img_1, LV_SCROLLBAR_MODE_OFF);

	//Write style for home_img_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_img_opa(ui->home_img_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes home_spangroup_1
	ui->home_spangroup_1 = lv_spangroup_create(ui->home_start);
	lv_spangroup_set_align(ui->home_spangroup_1, LV_TEXT_ALIGN_CENTER);
	lv_spangroup_set_overflow(ui->home_spangroup_1, LV_SPAN_OVERFLOW_CLIP);
	lv_spangroup_set_mode(ui->home_spangroup_1, LV_SPAN_MODE_BREAK);
	//create spans
	{
		lv_span_t *home_spangroup_1_span = lv_spangroup_new_span(ui->home_spangroup_1);
		lv_span_set_text(home_spangroup_1_span, UI_TXT_TITLE);
		lv_style_set_text_color(&home_spangroup_1_span->style, lv_color_hex(0x2cebcf));
		lv_style_set_text_decor(&home_spangroup_1_span->style, LV_TEXT_DECOR_NONE);
		lv_style_set_text_font(&home_spangroup_1_span->style, &Font60);
	}
	lv_obj_set_pos(ui->home_spangroup_1, 248, 118);
	lv_obj_set_size(ui->home_spangroup_1, 363, 66);
	lv_obj_set_scrollbar_mode(ui->home_spangroup_1, LV_SCROLLBAR_MODE_OFF);

	//Write style state: LV_STATE_DEFAULT for &style_home_spangroup_1_main_main_default
	static lv_style_t style_home_spangroup_1_main_main_default;
	ui_init_style(&style_home_spangroup_1_main_main_default);
	
	lv_style_set_border_width(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_radius(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_bg_opa(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_pad_top(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_pad_right(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_pad_bottom(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_pad_left(&style_home_spangroup_1_main_main_default, 0);
	lv_style_set_shadow_width(&style_home_spangroup_1_main_main_default, 0);
	lv_obj_add_style(ui->home_spangroup_1, &style_home_spangroup_1_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_spangroup_refr_mode(ui->home_spangroup_1);

	//Write codes home_spangroup_2
	ui->home_spangroup_2 = lv_spangroup_create(ui->home_start);
	lv_spangroup_set_align(ui->home_spangroup_2, LV_TEXT_ALIGN_LEFT);
	lv_spangroup_set_overflow(ui->home_spangroup_2, LV_SPAN_OVERFLOW_CLIP);
	lv_spangroup_set_mode(ui->home_spangroup_2, LV_SPAN_MODE_BREAK);
	//create spans
	{
		lv_span_t *home_spangroup_2_span = lv_spangroup_new_span(ui->home_spangroup_2);
		lv_span_set_text(home_spangroup_2_span, "ACOUSTIC IMAGING DEVICE");
		lv_style_set_text_color(&home_spangroup_2_span->style, lv_color_hex(0x2cebcf));
		lv_style_set_text_decor(&home_spangroup_2_span->style, LV_TEXT_DECOR_NONE);
		lv_style_set_text_font(&home_spangroup_2_span->style, &lv_font_montserratMedium_25);
	}
	lv_obj_set_pos(ui->home_spangroup_2, 248, 206);
	lv_obj_set_size(ui->home_spangroup_2, 363, 35);
	lv_obj_set_scrollbar_mode(ui->home_spangroup_2, LV_SCROLLBAR_MODE_OFF);

	//Write style state: LV_STATE_DEFAULT for &style_home_spangroup_2_main_main_default
	static lv_style_t style_home_spangroup_2_main_main_default;
	ui_init_style(&style_home_spangroup_2_main_main_default);
	
	lv_style_set_border_width(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_radius(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_bg_opa(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_pad_top(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_pad_right(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_pad_bottom(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_pad_left(&style_home_spangroup_2_main_main_default, 0);
	lv_style_set_shadow_width(&style_home_spangroup_2_main_main_default, 0);
	lv_obj_add_style(ui->home_spangroup_2, &style_home_spangroup_2_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_spangroup_refr_mode(ui->home_spangroup_2);

	//Write codes home_btn_1
	ui->home_btn_1 = lv_btn_create(ui->home_start);
	ui->home_btn_1_label = lv_label_create(ui->home_btn_1);
	lv_label_set_text(ui->home_btn_1_label, UI_TXT_START);
	lv_label_set_long_mode(ui->home_btn_1_label, LV_LABEL_LONG_WRAP);
	lv_obj_align(ui->home_btn_1_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_pad_all(ui->home_btn_1, 0, LV_STATE_DEFAULT);
	lv_obj_set_pos(ui->home_btn_1, 330, 286);
	lv_obj_set_size(ui->home_btn_1, 132, 82);
	lv_obj_set_scrollbar_mode(ui->home_btn_1, LV_SCROLLBAR_MODE_OFF);

	//Write style for home_btn_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_bg_opa(ui->home_btn_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_btn_1, lv_color_hex(0x009ea9), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->home_btn_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_btn_1, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->home_btn_1, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_color(ui->home_btn_1, lv_color_hex(0x0d4b3b), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_opa(ui->home_btn_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_spread(ui->home_btn_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_ofs_x(ui->home_btn_1, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_ofs_y(ui->home_btn_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(ui->home_btn_1, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(ui->home_btn_1, &Font30, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(ui->home_btn_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes home_spangroup_3
	ui->home_spangroup_3 = lv_spangroup_create(ui->home_start);
	lv_spangroup_set_align(ui->home_spangroup_3, LV_TEXT_ALIGN_CENTER);
	lv_spangroup_set_overflow(ui->home_spangroup_3, LV_SPAN_OVERFLOW_CLIP);
	lv_spangroup_set_mode(ui->home_spangroup_3, LV_SPAN_MODE_BREAK);
	//create spans
	{
		lv_span_t *home_spangroup_3_span = lv_spangroup_new_span(ui->home_spangroup_3);
		lv_span_set_text(home_spangroup_3_span, UI_TXT_SCAN_MORE);
		lv_style_set_text_color(&home_spangroup_3_span->style, lv_color_hex(0x2cebcf));
		lv_style_set_text_decor(&home_spangroup_3_span->style, LV_TEXT_DECOR_NONE);
		lv_style_set_text_font(&home_spangroup_3_span->style, &Font18);
	}
	lv_obj_set_pos(ui->home_spangroup_3, 618, 317);
	lv_obj_set_size(ui->home_spangroup_3, 200, 28);
	lv_obj_set_scrollbar_mode(ui->home_spangroup_3, LV_SCROLLBAR_MODE_OFF);

	//Write style state: LV_STATE_DEFAULT for &style_home_spangroup_3_main_main_default
	static lv_style_t style_home_spangroup_3_main_main_default;
	ui_init_style(&style_home_spangroup_3_main_main_default);
	
	lv_style_set_border_width(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_radius(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_bg_opa(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_pad_top(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_pad_right(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_pad_bottom(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_pad_left(&style_home_spangroup_3_main_main_default, 0);
	lv_style_set_shadow_width(&style_home_spangroup_3_main_main_default, 0);
	lv_obj_add_style(ui->home_spangroup_3, &style_home_spangroup_3_main_main_default, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_spangroup_refr_mode(ui->home_spangroup_3);

	//Write codes home_btn_2
	ui->home_btn_2 = lv_btn_create(ui->home_start);
	ui->home_btn_2_label = lv_label_create(ui->home_btn_2);
	lv_label_set_text(ui->home_btn_2_label, UI_TXT_SETTINGS);
	lv_label_set_long_mode(ui->home_btn_2_label, LV_LABEL_LONG_WRAP);
	lv_obj_align(ui->home_btn_2_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_pad_all(ui->home_btn_2, 0, LV_STATE_DEFAULT);
	lv_obj_set_pos(ui->home_btn_2, 30, 398);
	lv_obj_set_size(ui->home_btn_2, 58, 58);
	lv_obj_set_scrollbar_mode(ui->home_btn_2, LV_SCROLLBAR_MODE_OFF);

	//Write style for home_btn_2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_bg_opa(ui->home_btn_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_btn_2, lv_color_hex(0x009ea9), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->home_btn_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_btn_2, 29, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->home_btn_2, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_color(ui->home_btn_2, lv_color_hex(0x0d4b3b), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_opa(ui->home_btn_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_spread(ui->home_btn_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_ofs_x(ui->home_btn_2, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_ofs_y(ui->home_btn_2, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(ui->home_btn_2, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(ui->home_btn_2, &Font18, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(ui->home_btn_2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes home_cont_1
	ui->home_cont_1 = lv_obj_create(ui->home_start);
	lv_obj_set_pos(ui->home_cont_1, 88, 172);
	lv_obj_set_size(ui->home_cont_1, 335, 242);
	lv_obj_clear_flag(ui->home_cont_1, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(ui->home_cont_1, LV_SCROLLBAR_MODE_OFF);
	lv_obj_add_flag(ui->home_cont_1, LV_OBJ_FLAG_HIDDEN);

	//Write style for home_cont_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_border_width(ui->home_cont_1, 4, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(ui->home_cont_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(ui->home_cont_1, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_cont_1, 50, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home_cont_1, 213, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_cont_1, lv_color_hex(0x00e0ff), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_top(ui->home_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_bottom(ui->home_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_left(ui->home_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_pad_right(ui->home_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->home_cont_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write codes home_cb_1
	ui->home_cb_1 = lv_checkbox_create(ui->home_cont_1);
	lv_checkbox_set_text(ui->home_cb_1, UI_TXT_DEMO_1);
	lv_obj_set_pos(ui->home_cb_1, 37, 51);
	lv_obj_set_scrollbar_mode(ui->home_cb_1, LV_SCROLLBAR_MODE_OFF);

	//Write style for home_cb_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_text_color(ui->home_cb_1, lv_color_hex(0x0D3055), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(ui->home_cb_1, &Font16, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_letter_space(ui->home_cb_1, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->home_cb_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_cb_1, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home_cb_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_cb_1, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->home_cb_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write style for home_cb_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
	lv_obj_set_style_border_width(ui->home_cb_1, 2, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(ui->home_cb_1, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(ui->home_cb_1, lv_color_hex(0x2195f6), LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_cb_1, 6, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home_cb_1, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_cb_1, lv_color_hex(0xffffff), LV_PART_INDICATOR|LV_STATE_DEFAULT);

	//Write codes home_cb_2
	ui->home_cb_2 = lv_checkbox_create(ui->home_cont_1);
	lv_checkbox_set_text(ui->home_cb_2, UI_TXT_DEMO_2);
	lv_obj_set_pos(ui->home_cb_2, 37, 93);
	lv_obj_set_scrollbar_mode(ui->home_cb_2, LV_SCROLLBAR_MODE_OFF);

	//Write style for home_cb_2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
	lv_obj_set_style_text_color(ui->home_cb_2, lv_color_hex(0x0D3055), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(ui->home_cb_2, &Font16, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_text_letter_space(ui->home_cb_2, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(ui->home_cb_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_cb_2, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home_cb_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_cb_2, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(ui->home_cb_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

	//Write style for home_cb_2, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
	lv_obj_set_style_border_width(ui->home_cb_2, 2, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(ui->home_cb_2, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(ui->home_cb_2, lv_color_hex(0x2195f6), LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_radius(ui->home_cb_2, 6, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(ui->home_cb_2, 255, LV_PART_INDICATOR|LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui->home_cb_2, lv_color_hex(0xffffff), LV_PART_INDICATOR|LV_STATE_DEFAULT);

	//Update current screen layout.
	lv_obj_update_layout(ui->home);

	//Init events for screen.
	events_init_home(ui);
}
