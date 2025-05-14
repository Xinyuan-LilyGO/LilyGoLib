/**
 * @file      ui_wireless.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"


#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;
#endif

static lv_obj_t *menu = NULL;
static char wifi_ssid[64];
static char wifi_password[128];
static lv_obj_t *wifi_dd;
static bool scanning = false;

extern void ui_show_wifi_process_bar();

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
#ifdef USING_TOUCHPAD
        if (keyboard) {
            lv_obj_del(keyboard);
            keyboard = NULL;
        }
#endif
        lv_obj_clean(menu);
        lv_obj_del(menu);
        disable_keyboard();
        menu_show();
    }
}

static void password_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool state =  lv_obj_has_state(ta, LV_STATE_FOCUSED);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);

    bool copyToBuffer = false;

#ifdef USING_TOUCHPAD
    if (code == LV_EVENT_READY) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        copyToBuffer = true;
    }
#endif

    if (code == LV_EVENT_CLICKED) {

        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            printf("disable keyboard\n");
            disable_keyboard();
            copyToBuffer = true;
        }
#ifdef USING_TOUCHPAD
        else {
            lv_keyboard_set_textarea(keyboard, ta);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
#endif

    } else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            printf("enable input keyboard \n");
            enable_keyboard();
        }
    }

#ifdef USING_TOUCHPAD
    else if (code == LV_EVENT_DEFOCUSED) {
        if (!state) {
            lv_keyboard_set_textarea(keyboard, NULL);
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            copyToBuffer = true;
        }
    }
#endif
    if (copyToBuffer) {
        const char  *password =  lv_textarea_get_text(ta);
        if (!password) {
            printf("PWD IS NULL!\n");
            return;
        }
        if (lv_strlen(password) > 0) {
            lv_strncpy(wifi_password, password, lv_strlen(password));
            printf("PWD:%s\n", wifi_password);
        } else {
            printf("PWD IS EMPTY!\n");
        }
    }
}

static lv_obj_t *password_text_crate(lv_obj_t *parent)
{
    lv_obj_t *pwd_ta = lv_textarea_create(parent);
    lv_textarea_set_placeholder_text(pwd_ta, "password");
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_obj_set_scrollbar_mode(pwd_ta, LV_SCROLLBAR_MODE_OFF);
#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_add_event_cb(pwd_ta, password_ta_event_cb, LV_EVENT_ALL, NULL);

    return pwd_ta;
}

static void wifi_connect_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    wifi_conn_params_t params;

    printf("-> Connect to SSID : %s\n", wifi_ssid);
    printf("-> Connect to PWD  : %s\n", wifi_password);
    if (lv_strlen(wifi_ssid) == 0 ) {
        ui_msg_pop_up("WiFi", "SSID is null");
    } else if (lv_strlen(wifi_password) == 0 ) {
        ui_msg_pop_up("WiFi", "Password is null");
    } else if ( lv_strlen(wifi_password) < 8) {
        ui_msg_pop_up("WiFi", "Password too short");
    } else {
        params.ssid = wifi_ssid;
        params.password = wifi_password;
        hw_set_wifi_connect(params);
        ui_show_wifi_process_bar();
    }
}

static void dropdown_event(lv_event_t *e)
{
    char buf[128];
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_dropdown_get_selected_str(dd, buf, 128);
        printf("select option = %s\n", buf);
        lv_strcpy(wifi_ssid, buf);
    }
}


static lv_obj_t *dropdown_create(lv_obj_t *parent)
{
    wifi_dd = lv_dropdown_create(parent);
    lv_dropdown_clear_options(wifi_dd);
    lv_obj_add_event_cb(wifi_dd, dropdown_event, LV_EVENT_VALUE_CHANGED, NULL);
    return wifi_dd;
}


static void set_angle(void *obj, int32_t val)
{
    char buf[128];
    lv_obj_t *bar = (lv_obj_t *)obj;
    lv_bar_set_value(bar, val, LV_ANIM_ON);

    bool running = hw_get_wifi_scanning();

    if (val == 100 || !running) {

        lv_obj_del(lv_obj_get_parent(bar));

        scanning = false;

        vector <wifi_scan_params_t> list;
        hw_get_wifi_scan_result(list);

        // wifi_dd
        lv_dropdown_clear_options(wifi_dd);
        int16_t pos = 0;
        for (auto i : list) {
            lv_dropdown_add_option(wifi_dd, i.ssid.c_str(), pos++);
        }
        lv_dropdown_set_selected(wifi_dd, 0);
        lv_dropdown_get_selected_str(wifi_dd, buf, 128);
        lv_strcpy(wifi_ssid, buf);
    }
}

static void scan_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED) {

        if (scanning) {
            printf("Scanning !...\n");
            return;
        }

        scanning = true;

        hw_set_wifi_scan();

        lv_obj_t *bar =  ui_create_process_bar(lv_scr_act(), "WiFi Scanning...");
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, set_angle);
        lv_anim_set_time(&a, 20000);
        lv_anim_set_playback_time(&a, 3000);
        lv_anim_set_var(&a, bar);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_start(&a);
    }
}

void ui_wireless_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *con2 = ui_create_option(main_page, LV_SYMBOL_WIFI" Password:", NULL, password_text_crate, NULL);
    lv_obj_t *con1 = ui_create_option(main_page, LV_SYMBOL_WIFI" SSID:", NULL, dropdown_create, NULL);




#ifdef USING_TOUCHPAD
    int w =  lv_disp_get_hor_res(NULL) / 5;
    lv_obj_t *refresh_btn = create_radius_button(main_page, LV_SYMBOL_REFRESH, scan_btn_event, NULL);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_t *ok_btn = create_radius_button(main_page, LV_SYMBOL_OK, wifi_connect_event, NULL);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, w, -30);

    lv_obj_t *quit_btn = create_radius_button(main_page, LV_SYMBOL_LEFT, [](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, -w, -30);
#else

    lv_obj_t *refresh_btn = create_radius_button(main_page, LV_SYMBOL_REFRESH, scan_btn_event, NULL);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, -40, -20);

    lv_obj_t *ok_btn = create_radius_button(main_page, LV_SYMBOL_OK, wifi_connect_event, NULL);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 40, -20);
#endif



    lv_menu_set_page(menu, main_page);

}


void ui_wireless_exit(lv_obj_t *parent)
{

}

app_t ui_wireless_main = {
    .setup_func_cb = ui_wireless_enter,
    .exit_func_cb = ui_wireless_exit,
    .user_data = nullptr,
};


