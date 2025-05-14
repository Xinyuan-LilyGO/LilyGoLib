/**
 * @file      ui_nfc.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#define USING_GMSGBOX

static lv_obj_t *msgbox = NULL;

static void goto_connect_wifi_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    uint16_t id = 0;
#if LVGL_VERSION_MAJOR == 9
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *text = lv_label_get_text(label);
    printf("Button %s clicked\n", text);
    if (strcmp(text, "Close") == 0) {
        id = 1;
    }
#else
    uint16_t id =  lv_msgbox_get_active_btn(obj);
    printf("id=%d\n", id);
#endif

    if (id == 0) {
        wifi_conn_params_t *params = static_cast<wifi_conn_params_t *>(lv_event_get_user_data(e));
        if (params) {
            hw_set_wifi_connect(*params);
        }
        if (msgbox) {
            destroy_msgbox(msgbox);
            msgbox = NULL;
        }
        ui_show_wifi_process_bar();

    } else {
        
        printf("User cancel\n");
        if (msgbox) {
            destroy_msgbox(msgbox);
            msgbox = NULL;
        }
        menu_show();

        set_low_power_mode_flag(true);
    }
}

void ui_nfc_pop_up(wifi_conn_params_t &params)
{

    if (!isinMenu()) {
        return;
    }

    set_low_power_mode_flag(false);

    if (msgbox) {
        return;
    }
    printf("Do you want to connect to %s , pwd:%s\n", params.ssid.c_str(), params.password.c_str());
    static const char *btns[] = {"Connect", "Close", ""};
    char msg_txt[128];
    snprintf(msg_txt, 128, "Do you want to connect to \"%s\"", params.ssid.c_str());
    msgbox = create_msgbox(lv_scr_act(), "NFC WiFi",
                           msg_txt, btns,
                           goto_connect_wifi_event_cb, &params);
}
