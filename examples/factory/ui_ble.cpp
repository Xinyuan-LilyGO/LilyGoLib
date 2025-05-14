/**
 * @file      ui_ble.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-13
 *
 */
#include "ui_define.h"


static lv_timer_t *timer = NULL;
static lv_obj_t *menu = NULL;
static lv_obj_t *msg_ta = NULL;
static lv_obj_t *ble_name_ta = NULL;
static lv_obj_t *quit_btn = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (timer) {
            lv_timer_del(timer); timer = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        ble_name_ta = NULL;
        msg_ta = NULL;
        hw_disable_ble();
        hw_deinit_ble();

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

static lv_obj_t *create_ble_name_textarea(lv_obj_t *parent)
{
    //Rx Receiver msg box
    lv_obj_t *ta_label = lv_textarea_create(parent);
    lv_textarea_set_text_selection(ta_label, false);
    lv_textarea_set_cursor_click_pos(ta_label, false);
    lv_textarea_set_one_line(ta_label, true);
    lv_textarea_set_text(ta_label, hw_get_variant_name());
    lv_obj_set_scrollbar_mode(ta_label, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(ta_label, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
        }
    }, LV_EVENT_ALL, NULL);
    ble_name_ta = ta_label;
    return ta_label;
}


static void _ui_ble_obj_event(lv_event_t *e)
{
    uint16_t selected = 0;
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    const char *prefix = "On";
    char buf[64];
    selected =  lv_dropdown_get_selected(obj);
    lv_dropdown_get_selected_str(obj, buf, 64);
    if (strncmp(buf, prefix, strlen(prefix)) == 0) {
        const char *name = lv_textarea_get_text(ble_name_ta);
        if (strlen(name) != 0) {
            hw_enable_ble(name);
        } else {
            hw_enable_ble(hw_get_variant_name());
        }
    } else {
        hw_disable_ble();
    }
}

static lv_obj_t *create_mode_dropdown(lv_obj_t *parent)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, "On\n""Off");
    lv_dropdown_set_selected(dd, 1);
    lv_obj_add_event_cb(dd, _ui_ble_obj_event, LV_EVENT_VALUE_CHANGED, NULL);
    return dd;
}


static lv_obj_t *create_ble_message_textarea(lv_obj_t *parent)
{
    //Rx Receiver msg box
    lv_obj_t *ta_label = lv_textarea_create(parent);
    lv_textarea_set_text_selection(ta_label, false);
    lv_textarea_set_cursor_click_pos(ta_label, false);
    lv_textarea_set_one_line(ta_label, true);
    lv_textarea_set_placeholder_text(ta_label, "Message receiving window");
    lv_obj_set_scrollbar_mode(ta_label, LV_SCROLLBAR_MODE_OFF);
    
    lv_obj_add_event_cb(ta_label, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
        }
    }, LV_EVENT_ALL, NULL);
    msg_ta = ta_label;
    return ta_label;
}

void ui_ble_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    ui_create_option(main_page, "Mode:", NULL, create_mode_dropdown, NULL);
    ui_create_option(main_page, "BLE Name:", NULL, create_ble_name_textarea, NULL);
    ui_create_option(main_page, "Message:", NULL, create_ble_message_textarea, NULL);

    lv_menu_set_page(menu, main_page);

    timer = lv_timer_create([](lv_timer_t *t) {
        char buffer[256];
        size_t rs = hw_get_ble_message(buffer, 256);
        if (rs > 0) {
            buffer[rs + 1] = '\0';
            lv_textarea_set_text(msg_ta, buffer);
        }
    }, 1000, NULL);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_ble_exit(lv_obj_t *parent)
{

}

app_t ui_ble_main = {
    .setup_func_cb = ui_ble_enter,
    .exit_func_cb = ui_ble_exit,
    .user_data = nullptr,
};


