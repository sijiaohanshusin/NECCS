/*
* Copyright 2026 NXP
* NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <stdio.h>
#include "lvgl.h"


static void home_btn_1_event_handler (lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	switch (code) {
	case LV_EVENT_CLICKED:
	{
		//Write the load screen code.
	    lv_obj_t * act_scr = lv_scr_act();
	    lv_disp_t * d = lv_obj_get_disp(act_scr);
	    if (d->prev_scr == NULL && (d->scr_to_load == NULL || d->scr_to_load == act_scr)) {
	        if (guider_ui.using_del == true) {
	          setup_scr_using(&guider_ui);
	        }
	        lv_scr_load_anim(guider_ui.using, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
	        guider_ui.using_del = true;
	    }
		break;
	}
	default:
		break;
	}
}
static void home_btn_2_event_handler (lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	switch (code) {
	case LV_EVENT_CLICKED:
	{
		if (lv_obj_has_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN)) {
			lv_obj_clear_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);
		}
		else {
			lv_obj_add_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);
		}
		break;
	}
	default:
		break;
	}
}
static void home_cont_1_event_handler (lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *target = lv_event_get_target(e);
	lv_obj_t *current_target = lv_event_get_current_target(e);

	switch (code) {
	case LV_EVENT_CLICKED:
	{
		if (target == current_target) {
			lv_obj_add_flag(guider_ui.home_cont_1, LV_OBJ_FLAG_HIDDEN);
		}
		break;
	}
	default:
		break;
	}
}
void events_init_home(lv_ui *ui)
{
	lv_obj_add_event_cb(ui->home_btn_1, home_btn_1_event_handler, LV_EVENT_ALL, NULL);
	lv_obj_add_event_cb(ui->home_btn_2, home_btn_2_event_handler, LV_EVENT_ALL, NULL);
	lv_obj_add_event_cb(ui->home_cont_1, home_cont_1_event_handler, LV_EVENT_ALL, NULL);
}
static void using_btn_1_event_handler (lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	switch (code) {
	case LV_EVENT_CLICKED:
	{
		//Write the load screen code.
	    lv_obj_t * act_scr = lv_scr_act();
	    lv_disp_t * d = lv_obj_get_disp(act_scr);
	    if (d->prev_scr == NULL && (d->scr_to_load == NULL || d->scr_to_load == act_scr)) {
	        if (guider_ui.home_del == true) {
	          setup_scr_home(&guider_ui);
	        }
	        lv_scr_load_anim(guider_ui.home, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
	        guider_ui.home_del = true;
	    }
		break;
	}
	default:
		break;
	}
}
void events_init_using(lv_ui *ui)
{
	lv_obj_add_event_cb(ui->using_btn_1, using_btn_1_event_handler, LV_EVENT_ALL, NULL);
}

void events_init(lv_ui *ui)
{

}
