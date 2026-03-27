#include "app_lvgl_ui.h"

#include "app_display.h"
#include "app_user_config.h"
#include "lv_port_disp_template.h"

#if (APP_LVGL_ENABLE != 0u)
#include "lvgl/lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_lvgl_ui_created = 0u;
static uint8_t s_overlay_enabled = 0u;
static uint8_t s_menu_visible = 0u;
static uint8_t s_switch_syncing = 0u;
static uint8_t s_demo_switch_state = 0u;
static uint32_t s_switch_last_toggle_tick = 0u;

static lv_obj_t *s_main_screen = NULL;
static lv_obj_t *s_menu_button = NULL;
static lv_obj_t *s_menu_button_label = NULL;
static lv_obj_t *s_menu_panel = NULL;
static lv_obj_t *s_mode_label = NULL;
static lv_obj_t *s_switch_label = NULL;
static lv_obj_t *s_demo_switch = NULL;

static void s_set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *current_text;

    if ((label == NULL) || (text == NULL))
    {
        return;
    }

    current_text = lv_label_get_text(label);
    if ((current_text == NULL) || (strcmp(current_text, text) != 0))
    {
        lv_label_set_text(label, text);
    }
}

static void s_set_menu_visible(uint8_t visible)
{
    if ((s_menu_panel == NULL) || (s_menu_button == NULL))
    {
        return;
    }

    s_menu_visible = (visible != 0u) ? 1u : 0u;
    if (s_menu_visible != 0u)
    {
        lv_obj_clear_flag(s_menu_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_menu_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);
        s_set_label_text_if_changed(s_menu_button_label, "MODE");
    }
}

static void s_update_mode_label(void)
{
    char text[48];

    if (s_mode_label == NULL)
    {
        return;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "Mode: %s",
                   App_Display_ModeName(App_Display_GetMode()));
    s_set_label_text_if_changed(s_mode_label, text);
}

static void s_update_switch_label(void)
{
    if (s_switch_label == NULL)
    {
        return;
    }

    s_set_label_text_if_changed(s_switch_label,
                                (s_demo_switch_state != 0u) ? "Switch: ON" : "Switch: OFF");
}

static void s_menu_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        s_set_menu_visible((s_menu_visible == 0u) ? 1u : 0u);
    }
}

static void s_mode_button_event_cb(lv_event_t *e)
{
    App_Display_Mode_t mode;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    mode = (App_Display_Mode_t)(uintptr_t)lv_event_get_user_data(e);
    App_Display_SetMode(mode);
    s_update_mode_label();
    s_set_menu_visible(0u);
}

static void s_close_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        s_set_menu_visible(0u);
    }
}

static void s_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw;
    uint8_t new_state;
    uint32_t now;

    if ((lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) || (s_switch_syncing != 0u))
    {
        return;
    }

    sw = lv_event_get_target(e);
    if (sw == NULL)
    {
        return;
    }

    new_state = (lv_obj_has_state(sw, LV_STATE_CHECKED) != false) ? 1u : 0u;
    now = lv_tick_get();

    /* Guard against noisy long-press jitter from the touch panel. */
    if ((s_switch_last_toggle_tick != 0u) &&
        ((now - s_switch_last_toggle_tick) < 250u) &&
        (new_state != s_demo_switch_state))
    {
        s_switch_syncing = 1u;
        if (s_demo_switch_state != 0u)
        {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
        s_switch_syncing = 0u;
        return;
    }

    s_demo_switch_state = new_state;
    s_switch_last_toggle_tick = now;
    s_update_switch_label();
}

static lv_obj_t *s_create_mode_button(lv_obj_t *parent,
                                      const char *text,
                                      lv_coord_t x,
                                      App_Display_Mode_t mode)
{
    lv_obj_t *btn;
    lv_obj_t *label;

    btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 54, 28);
    lv_obj_set_pos(btn, x, 34);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1D4ED8), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, s_mode_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);

    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(label);
    return btn;
}

void App_LvglUi_Init(void)
{
#if (APP_LVGL_TEST_UI_ENABLE != 0u)
    lv_obj_t *close_button;
    lv_obj_t *close_label;
    lv_obj_t *title;
    lv_obj_t *switch_caption;

    if (s_lvgl_ui_created != 0u)
    {
        if (s_main_screen != NULL)
        {
            lv_scr_load(s_main_screen);
        }
        return;
    }

    s_main_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_main_screen, LV_OBJ_FLAG_SCROLLABLE);
    /* Keep the root as a chroma-key canvas so only LVGL widgets cover the heat map. */
    lv_obj_set_style_bg_color(s_main_screen, lv_color_hex(LV_PORT_OVERLAY_CHROMA_KEY_HEX), 0);
    lv_obj_set_style_bg_opa(s_main_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_main_screen, 0, 0);
    lv_obj_set_style_outline_width(s_main_screen, 0, 0);
    lv_obj_set_style_radius(s_main_screen, 0, 0);
    lv_obj_set_style_pad_all(s_main_screen, 0, 0);

    s_menu_panel = lv_obj_create(s_main_screen);
    lv_obj_set_size(s_menu_panel, 206, 172);
    lv_obj_align(s_menu_panel, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_clear_flag(s_menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_menu_panel, 12, 0);
    lv_obj_set_style_bg_color(s_menu_panel, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(s_menu_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_menu_panel, 2, 0);
    lv_obj_set_style_border_color(s_menu_panel, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_pad_all(s_menu_panel, 10, 0);
    lv_obj_add_flag(s_menu_panel, LV_OBJ_FLAG_HIDDEN);

    title = lv_label_create(s_menu_panel);
    lv_label_set_text(title, "Display Mode");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_pos(title, 0, 4);

    close_button = lv_btn_create(s_menu_panel);
    lv_obj_set_size(close_button, 28, 28);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, -2, -2);
    lv_obj_set_style_radius(close_button, 14, 0);
    lv_obj_set_style_bg_color(close_button, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_opa(close_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close_button, 0, 0);
    lv_obj_add_event_cb(close_button, s_close_button_event_cb, LV_EVENT_CLICKED, NULL);

    close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(close_label);

    (void)s_create_mode_button(s_menu_panel, "FAST", 0, APP_DISPLAY_MODE_FAST);
    (void)s_create_mode_button(s_menu_panel, "BAL", 68, APP_DISPLAY_MODE_BALANCED);
    (void)s_create_mode_button(s_menu_panel, "CLEAN", 136, APP_DISPLAY_MODE_CLEAN);

    s_mode_label = lv_label_create(s_menu_panel);
    lv_obj_set_style_text_color(s_mode_label, lv_color_hex(0xBFDBFE), 0);
    lv_obj_set_pos(s_mode_label, 0, 80);

    switch_caption = lv_label_create(s_menu_panel);
    lv_label_set_text(switch_caption, "Demo");
    lv_obj_set_style_text_color(switch_caption, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_pos(switch_caption, 0, 116);

    s_demo_switch = lv_switch_create(s_menu_panel);
    lv_obj_set_size(s_demo_switch, 44, 22);
    lv_obj_set_pos(s_demo_switch, 60, 112);
    lv_obj_add_event_cb(s_demo_switch, s_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_switch_label = lv_label_create(s_menu_panel);
    lv_obj_set_style_text_color(s_switch_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_pos(s_switch_label, 0, 146);

    s_menu_button = lv_btn_create(s_main_screen);
    lv_obj_set_size(s_menu_button, 78, 36);
    lv_obj_align(s_menu_button, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_set_style_radius(s_menu_button, 18, 0);
    lv_obj_set_style_bg_color(s_menu_button, lv_color_hex(0x0891B2), 0);
    lv_obj_set_style_bg_opa(s_menu_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_menu_button, 0, 0);
    lv_obj_add_event_cb(s_menu_button, s_menu_button_event_cb, LV_EVENT_CLICKED, NULL);

    s_menu_button_label = lv_label_create(s_menu_button);
    lv_label_set_text(s_menu_button_label, "MODE");
    lv_obj_set_style_text_color(s_menu_button_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(s_menu_button_label);

    s_update_mode_label();
    s_update_switch_label();
    s_set_menu_visible(0u);

    lv_scr_load(s_main_screen);
    s_lvgl_ui_created = 1u;
#endif
}

void App_LvglUi_Process(void)
{
#if (APP_LVGL_TEST_UI_ENABLE != 0u)
    if (s_lvgl_ui_created == 0u)
    {
        return;
    }

    s_update_mode_label();
#endif
}

void App_LvglUi_SetOverlayEnabled(uint8_t enabled)
{
    s_overlay_enabled = (enabled != 0u) ? 1u : 0u;
}

void App_LvglUi_BlitToDisplay(void)
{
    if ((s_overlay_enabled == 0u) || (s_lvgl_ui_created == 0u))
    {
        return;
    }

    lv_port_disp_blit_to_display();
}

#else

void App_LvglUi_Init(void)
{
}

void App_LvglUi_Process(void)
{
}

void App_LvglUi_SetOverlayEnabled(uint8_t enabled)
{
    (void)enabled;
}

void App_LvglUi_BlitToDisplay(void)
{
}

#endif
