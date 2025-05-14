/**
 * @file      ui_gps.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

typedef struct {
    lv_obj_t *lat;
    lv_obj_t *lng;
    lv_obj_t *datetime;
    lv_obj_t *speed;
    lv_obj_t *rx_size;
    lv_obj_t *satellite;
    lv_obj_t *use_second;
    lv_obj_t *pps;
    uint32_t startTime;
    bool updateTime;
} gps_label_t;

static gps_label_t label_gps;
static lv_obj_t *menu = NULL;
static lv_timer_t *timer = NULL;
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

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        hw_gps_detach_pps();

        menu_show();
    }
}



void ui_gps_enter(lv_obj_t *parent)
{

    label_gps.startTime = lv_tick_get();
    label_gps.updateTime = false;

    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    /*Create a list*/
    lv_obj_t *list1 = lv_list_create(main_page);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_center(list1);

    lv_obj_t *btn, *label ;

    hw_gps_attach_pps();

    gps_params_t param;
    hw_get_gps_info(param);

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "Model");
    label = lv_label_create(btn);
    lv_label_set_text(label, param.model.c_str());

#ifdef GPS_PPS
    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "PPS Single");
    label = lv_led_create(btn);
    lv_led_set_color(label, lv_color_make(0, 255, 0));
    lv_led_off(label);
    label_gps.pps = label;
#endif

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "Use Second");
    label = lv_label_create(btn);
    lv_label_set_text(label, "N.A");
    label_gps.use_second = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "Visible satellite");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%u", param.satellite);
    label_gps.satellite = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "lat");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%.06f", param.lat);
    label_gps.lat = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "lng");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%.06f", param.lng);
    label_gps.lng = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "datetime");
    label = lv_label_create(btn);
    char buffer[128];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &param.datetime);
    lv_label_set_text(label, buffer);
    label_gps.datetime = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "Speed");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%.02f Km/h", param.speed);
    label_gps.speed = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_GPS, "RX Char");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%u", param.rx_size);
    label_gps.rx_size = label;

    lv_menu_set_page(menu, main_page);

    timer = lv_timer_create([](lv_timer_t *t) {
        char buffer[128];
        static gps_params_t param;
        bool rlst = hw_get_gps_info(param);
#ifdef GPS_PPS
        param.pps ? lv_led_on(label_gps.pps) :  lv_led_off(label_gps.pps);
#endif

        if (rlst && !label_gps.updateTime) {
            label_gps.updateTime = true;
            uint32_t sec = (lv_tick_get() - label_gps.startTime) / 1000;
            printf("Update postion time is :%u\n", sec);
            lv_label_set_text_fmt(label_gps.use_second, "%u", sec);
        }

        static uint32_t interval;
        if (lv_tick_get() < interval) {
            return;
        }
        interval = lv_tick_get() + 1000;
        lv_label_set_text_fmt(label_gps.satellite, "%u", param.satellite);
        lv_label_set_text_fmt(label_gps.lat, "%.06f", param.lat);
        lv_label_set_text_fmt(label_gps.lng, "%.06f", param.lng);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &param.datetime);
        lv_label_set_text(label_gps.datetime, buffer);
        lv_label_set_text_fmt(label_gps.speed, "%.02f Km/h", param.speed);
        lv_label_set_text_fmt(label_gps.rx_size, "%u", param.rx_size);
    }, 80, label);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif

}


void ui_gps_exit(lv_obj_t *parent)
{

}

app_t ui_gps_main = {
    .setup_func_cb = ui_gps_enter,
    .exit_func_cb = ui_gps_exit,
    .user_data = nullptr,
};


