/**
 * @file      ui_sys.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

static lv_obj_t *menu = NULL;
static lv_timer_t *timer = NULL;
static lv_group_t *menu_g;
static  user_setting_params_t local_param;
static uint32_t get_ip_id = 0;
static lv_obj_t *quit_btn = NULL;

typedef struct {

    lv_obj_t *datetime_label;
    lv_obj_t *wifi_rssi_label;
    lv_obj_t *batt_voltage_label;

} sys_label_t;

static sys_label_t sys_label;


static void sys_timer_event_cb(lv_timer_t*t)
{
    string datetime;
    hw_get_date_time(datetime);
    lv_label_set_text_fmt(sys_label.datetime_label, "%s", datetime.c_str());

    if (hw_get_wifi_connected()) {
        lv_label_set_text_fmt(sys_label.wifi_rssi_label, "%d", hw_get_wifi_rssi());
    }
    lv_label_set_text_fmt(sys_label.batt_voltage_label, "%d mV", hw_get_battery_voltage());
}

static long map_r(long x, long in_min, long in_max, long out_min, long out_max)
{
    if (x < in_min) {
        return out_min;
    } else if (x > in_max) {
        return out_max;
    }
    return ((x - in_min) * (out_max - out_min)) / (in_max - in_min) + out_min;
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        hw_set_user_setting(local_param);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
        }
        menu_show();
    }
}


static void display_brightness_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);

    uint16_t min_brightness = hw_get_disp_min_brightness();
    uint16_t max_brightness = hw_get_disp_max_brightness();

    lv_label_set_text_fmt(slider_label, "   %u%%  ", map_r(val, min_brightness, max_brightness, 0, 100));
    local_param.brightness_level = val;
    hw_set_disp_backlight(val);
}

static void keyboard_brightness_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    lv_label_set_text_fmt(slider_label, "   %u%%  ", map_r(val, 0, 16, 0, 100));
    val = map_r(val, 0, 16, 0, 255);
    local_param.keyboard_bl_level = val;
    hw_set_kb_backlight(val);
}

static void disp_timeout_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);

    local_param.disp_timeout_second = val;
    lv_label_set_text_fmt(slider_label, "   %uS  ", val);
}

static void otg_output_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool turnOn = lv_obj_has_state(obj, LV_STATE_CHECKED);
        printf("State: %s\n", turnOn ? "On" : "Off");
        if (hw_set_otg(turnOn) == false) {
            lv_obj_clear_state(obj, LV_STATE_CHECKED);
        }
    }
}

static void charger_enable_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool turnOn = lv_obj_has_state(obj, LV_STATE_CHECKED);
        local_param.charger_enable = turnOn;
        printf("State: %s\n", turnOn ? "On" : "Off");
        hw_set_charger(turnOn);
    }
}

static void charger_current_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    uint16_t val =  lv_slider_get_value(obj) ;
    local_param.charger_current = hw_set_charger_current_level(val );
    lv_label_set_text_fmt(slider_label, "%04umA", local_param.charger_current);
}

static lv_obj_t *create_subpage_backlight(lv_obj_t *menu, lv_obj_t *main_page)
{
    lv_obj_t *cont = lv_menu_cont_create(main_page);
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS" Display & Backlight");
    lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *slider;
    lv_obj_t *parent ;
    lv_obj_t *slider_label;

    uint16_t min_brightness = hw_get_disp_min_brightness();
    uint16_t max_brightness = hw_get_disp_max_brightness();

    slider = create_slider(sub_page, LV_SYMBOL_SETTINGS, "Display Brightness", min_brightness, max_brightness,
                           local_param.brightness_level, display_brightness_cb, LV_EVENT_VALUE_CHANGED);
    parent = lv_obj_get_parent(slider);
    slider_label = lv_label_create(parent);

    uint8_t set_level = map_r(local_param.brightness_level, min_brightness, max_brightness, 0, 100);
    printf("get params level:%u min:%u max:%u local:%d \n", set_level, min_brightness, max_brightness, local_param.brightness_level);

    lv_label_set_text_fmt(slider_label, "   %u%%  ", set_level);
    lv_obj_set_user_data(slider, slider_label);
    lv_group_add_obj(lv_group_get_default(), (slider));

    if (hw_has_keyboard()) {
        uint8_t level =  map_r(local_param.keyboard_bl_level, 0, 255, 0, 16);
        printf("Keyboard level = %u value=%u\n", level, local_param.keyboard_bl_level);
        slider = create_slider(sub_page, LV_SYMBOL_SETTINGS, "Keyboard Brightness", 0, 16, level, keyboard_brightness_cb, LV_EVENT_VALUE_CHANGED);
        parent = lv_obj_get_parent(slider);
        slider_label = lv_label_create(parent);
        lv_label_set_text_fmt(slider_label, "   %u%%  ", map_r(local_param.keyboard_bl_level, 0, 255, 0, 100));
        lv_obj_set_user_data(slider, slider_label);
        lv_group_add_obj(lv_group_get_default(), (slider));
    }

    slider = create_slider(sub_page, LV_SYMBOL_SETTINGS, "Display Timeout", 0, 180, local_param.disp_timeout_second, disp_timeout_cb, LV_EVENT_VALUE_CHANGED);
    parent = lv_obj_get_parent(slider);
    slider_label = lv_label_create(parent);
    lv_label_set_text_fmt(slider_label, "   %uS  ", local_param.disp_timeout_second);
    lv_obj_set_user_data(slider, slider_label);
    lv_group_add_obj(lv_group_get_default(), (slider));

    lv_menu_set_load_page_event(menu, cont, sub_page);
    return cont;
}

static lv_obj_t *create_subpage_otg(lv_obj_t *menu, lv_obj_t *main_page)
{
    lv_obj_t *cont = lv_menu_cont_create(main_page);
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS" Charger");
    lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);
    bool enableOtg = hw_get_otg_enable();

    uint8_t total_charge_level = hw_get_charge_level_nums();
    uint8_t curr_charge_level = hw_get_charger_current_level();

    printf("CURRENT LEVEL:%u\n", curr_charge_level);
    if (hw_has_otg_function()) {
        create_switch(sub_page, LV_SYMBOL_POWER, "OTG Enable", enableOtg,  otg_output_cb);
    }

    create_switch(sub_page, LV_SYMBOL_POWER, "Charger Enable", local_param.charger_enable,  charger_enable_cb);
    lv_obj_t *slider = create_slider(sub_page, LV_SYMBOL_POWER,
                                     "Charger current",
                                     1, total_charge_level,
                                     curr_charge_level,
                                     charger_current_cb,
                                     LV_EVENT_VALUE_CHANGED);

    lv_obj_t *parent = lv_obj_get_parent(slider);
    lv_obj_t *slider_label = lv_label_create(parent);
    lv_label_set_text_fmt(slider_label, "%umA", local_param.charger_current);
    lv_obj_set_user_data(slider, slider_label);

    lv_menu_set_load_page_event(menu, cont, sub_page);
    return cont;
}

static lv_obj_t *create_subpage_info(lv_obj_t *menu, lv_obj_t *main_page)
{
    lv_obj_t *btn;
    lv_obj_t *cont = lv_menu_cont_create(main_page);
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS" System Info");
    lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *list1 = lv_list_create(sub_page);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_center(list1);

    char buffer[128];
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    bool has_mac = hw_get_mac(mac);
    if (has_mac) {
        snprintf(buffer, 128, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    btn = lv_list_add_btn(list1, LV_SYMBOL_SETTINGS, "MAC");
    label = lv_label_create(btn);
    lv_label_set_text(label, has_mac ?  buffer : "N.A");

    string wifi_ssid = "N.A";
    hw_get_wifi_ssid(wifi_ssid);
    btn = lv_list_add_btn(list1, LV_SYMBOL_WIFI, "WiFi SSID");
    label = lv_label_create(btn);
    lv_label_set_text(label, wifi_ssid.c_str());

    btn = lv_list_add_btn(list1, LV_SYMBOL_WIFI, "RTC Datetime");
    label = lv_label_create(btn);
    lv_label_set_text(label, "00:00:00");
    sys_label.datetime_label = label;

    static string ip_info = "N.A";
    hw_get_ip_address(ip_info);
    btn = lv_list_add_btn(list1, LV_SYMBOL_WIFI, "IP");
    label = lv_label_create(btn);
    lv_label_set_text(label, ip_info.c_str());

    btn = lv_list_add_btn(list1, LV_SYMBOL_WIFI, "RSSI");
    label = lv_label_create(btn);
    lv_label_set_text(label, "N.A");
    sys_label.wifi_rssi_label = label;

    int16_t vol = hw_get_battery_voltage();
    btn = lv_list_add_btn(list1, LV_SYMBOL_BATTERY_FULL, "Voltage");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%d", vol);
    sys_label.batt_voltage_label = label;

    float size = hw_get_sd_size();
#if defined(HAS_SD_CARD_SOCKET)
    const char *unit = "GB";
    btn = lv_list_add_btn(list1, LV_SYMBOL_SD_CARD, "SD Card");
#else
    const char *unit = "MB";
    btn = lv_list_add_btn(list1, LV_SYMBOL_SD_CARD, "Storage");
#endif
    label = lv_label_create(btn);
    if (size > 0) {
        lv_label_set_text_fmt(label, "%.2f %s", size, unit);
    } else {
        lv_label_set_text(label, "N.A");
    }

    btn = lv_list_add_btn(list1, LV_SYMBOL_EYE_OPEN, "lvgl");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "V%d.%d.%d", lv_version_major(), lv_version_minor(), lv_version_patch());


    string ver;
    hw_get_arduino_version(ver);
    btn = lv_list_add_btn(list1, LV_SYMBOL_EYE_OPEN, "arduino");
    label = lv_label_create(btn);
    lv_label_set_text(label, ver.c_str());

    lv_menu_set_load_page_event(menu, cont, sub_page);

    timer =  lv_timer_create(sys_timer_event_cb, 1000, NULL);

    return cont;
}

static lv_obj_t *create_device_probe(lv_obj_t *menu, lv_obj_t *main_page)
{
    lv_obj_t *cont = lv_menu_cont_create(main_page);
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS" Devices status");
    lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);

    /*Create a list*/
    lv_obj_t *list1 = lv_list_create(sub_page);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_center(list1);
    uint8_t devices = hw_get_devices_nums();
    uint32_t devices_mask = hw_get_device_online();
    for (int i = 0; i < devices; ++i) {
        const char *device_name = hw_get_devices_name(i);
        if (lv_strcmp(device_name, "") != 0) {
            lv_obj_t *obj =   lv_list_add_btn(list1, LV_SYMBOL_OK, device_name);
            label = lv_label_create(obj);
            if (devices_mask & 0x01) {
                lv_label_set_text(label, "Online");
                lv_obj_set_style_text_color(label, lv_color_make(0, 255, 0), LV_PART_MAIN);
            } else {
                lv_label_set_text(label, "Offline");
                lv_obj_set_style_text_color(label, lv_color_make(255, 0, 0), LV_PART_MAIN);
            }
        }
        devices_mask >>= 1;
    }
    lv_menu_set_load_page_event(menu, cont, sub_page);
    return cont;
}


void ui_sys_enter(lv_obj_t *parent)
{
    menu_g = lv_group_get_default();

    menu = lv_menu_create(parent);

    hw_get_user_setting(local_param);

    menu = create_menu(parent, back_event_handler);

    /*Create a main page*/
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont;
    // //! BACKLIGHT SETTING
    cont = create_subpage_backlight(menu, main_page);
    lv_group_add_obj(menu_g, cont);

    // //! SYSTEM INFO
    cont = create_subpage_info(menu, main_page);
    lv_group_add_obj(menu_g, cont);

    cont = create_device_probe(menu, main_page);
    lv_group_add_obj(menu_g, cont);

    cont = create_subpage_otg(menu, main_page);
    lv_group_add_obj(menu_g, cont);

    lv_menu_set_page(menu, main_page);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, lv_event_get_target_obj(e));
    }, NULL);
#endif
}


void ui_sys_exit(lv_obj_t *parent)
{

}

app_t ui_sys_main = {
    .setup_func_cb = ui_sys_enter,
    .exit_func_cb = ui_sys_exit,
    .user_data = nullptr,
};


