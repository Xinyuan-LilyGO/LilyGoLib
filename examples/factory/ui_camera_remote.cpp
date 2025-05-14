/**
 * @file      ui_camera_remote.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"


static lv_obj_t *menu = NULL;

#define SHOOT_KEY       MEDIA_VOLUME_UP


static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        hw_set_ble_kb_disable();
        menu_show();
    }
}

static void shoot_event_handler(lv_event_t *e)
{
    hw_set_ble_key(SHOOT_KEY);
}

void ui_camera_remote_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *cont = lv_obj_create(menu);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));

    lv_obj_t *shoot_button = create_radius_button(cont, LV_SYMBOL_BLUETOOTH, shoot_event_handler, NULL);
    lv_obj_remove_flag(shoot_button, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(shoot_button, lv_pct(60), lv_pct(60));
    lv_obj_center(shoot_button);


    lv_obj_t *quit_btn = create_radius_button(cont, LV_SYMBOL_LEFT, [](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    },  NULL);
    lv_obj_remove_flag(quit_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align_to(quit_btn, shoot_button, LV_ALIGN_BOTTOM_MID, 0, 120);

    hw_set_ble_kb_enable();
}

void ui_camera_remote_exit(lv_obj_t *parent)
{

}

app_t ui_camera_remote_main = {
    .setup_func_cb = ui_camera_remote_enter,
    .exit_func_cb = ui_camera_remote_exit,
    .user_data = nullptr,
};


