/**
 * @file      ui_monitor.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"


typedef struct {
    lv_obj_t *charge_state;
    lv_obj_t *battery_voltage;
    lv_obj_t *sys_voltage;
    lv_obj_t *battery_percent;
    lv_obj_t *usb_voltage;
    lv_obj_t *temperature;
    lv_obj_t *instantaneousCurrent;
    lv_obj_t *remainingCapacity;
    lv_obj_t *fullChargeCapacity;
    lv_obj_t *designCapacity;
    lv_obj_t *averagePower;
    lv_obj_t *maxLoadCurrent;
    lv_obj_t *timeToEmpty;
    lv_obj_t *timeToFull;
    lv_obj_t *standbyCurrent;
    lv_obj_t *ntc_state;
} monitor_label_t;

static monitor_label_t label_monitor;
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

        menu_show();
    }
}

void ui_monitor_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    /*Create a main page*/
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    monitor_params_t param;
    hw_get_monitor_params(param);

    lv_obj_t *btn, *label ;
    /*Create a list*/
    lv_obj_t *list1 = lv_list_create(main_page);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_center(list1);

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "State:");
    label = lv_label_create(btn);
    lv_label_set_text(label, param.charge_state.c_str());
    label_monitor.charge_state = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "NTC State:");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%s", param.ntc_state.c_str());
    label_monitor.ntc_state = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Power adapter:");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%u%%", param.usb_voltage);
    label_monitor.usb_voltage = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Battery Voltage:");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%u", param.battery_voltage);
    label_monitor.battery_voltage = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "System Voltage:");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%u", param.sys_voltage);
    label_monitor.sys_voltage = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Battery Percent:");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%u%%", param.battery_percent);
    label_monitor.battery_percent = label;

    btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Temperature:");
    label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%.02f °C", param.temperature);
    label_monitor.temperature = label;

    if (param.type == MONITOR_PPM) {

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Instantaneous Current:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d mA", param.instantaneousCurrent);
        label_monitor.instantaneousCurrent = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Average Power:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d mW", param.averagePower);
        label_monitor.averagePower = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Time To Empty:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%u Min", param.timeToEmpty);
        label_monitor.timeToEmpty = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Time To Full:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%u Min", param.timeToFull);
        label_monitor.timeToFull = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Standby Current:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d mA", param.standbyCurrent);
        label_monitor.standbyCurrent = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Max Load Current:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d mA", param.maxLoadCurrent);
        label_monitor.maxLoadCurrent = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Remaining Capacity:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%u mAh", param.temperature);
        label_monitor.remainingCapacity = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Full Charge Capacity:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%u mAh", param.fullChargeCapacity);
        label_monitor.fullChargeCapacity = label;

        btn = lv_list_add_btn(list1, LV_SYMBOL_POWER, "Design Capacity:");
        label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%u mAh", param.designCapacity);
        label_monitor.designCapacity = label;
    }

    lv_menu_set_page(menu, main_page);


    timer = lv_timer_create([](lv_timer_t *t) {
        monitor_params_t param;
        hw_get_monitor_params(param);
        lv_label_set_text_fmt(label_monitor.charge_state, "%s", param.charge_state.c_str());
        lv_label_set_text_fmt(label_monitor.battery_voltage, "%u mV", param.battery_voltage);
        lv_label_set_text_fmt(label_monitor.usb_voltage, "%u mV", param.usb_voltage);
        lv_label_set_text_fmt(label_monitor.sys_voltage, "%u mV", param.sys_voltage);
        lv_label_set_text_fmt(label_monitor.battery_percent, "%d %%", param.battery_percent);
        lv_label_set_text_fmt(label_monitor.temperature, "%.02f °C", param.temperature);
        lv_label_set_text_fmt(label_monitor.ntc_state, "%s", param.ntc_state.c_str());

        if (param.type == MONITOR_PPM) {
            lv_label_set_text_fmt(label_monitor.instantaneousCurrent, "%d mA", param.instantaneousCurrent);
            lv_label_set_text_fmt(label_monitor.remainingCapacity, "%u mAh", param.remainingCapacity);
            lv_label_set_text_fmt(label_monitor.fullChargeCapacity, "%u mAh", param.fullChargeCapacity);
            lv_label_set_text_fmt(label_monitor.designCapacity, "%u mAh", param.designCapacity);
            lv_label_set_text_fmt(label_monitor.averagePower, "%d mW", param.averagePower);
            lv_label_set_text_fmt(label_monitor.standbyCurrent, "%d mA", param.standbyCurrent);
            lv_label_set_text_fmt(label_monitor.maxLoadCurrent, "%d mA", param.maxLoadCurrent);
            lv_label_set_text_fmt(label_monitor.timeToEmpty, "%u Min", param.timeToEmpty);
            lv_label_set_text_fmt(label_monitor.timeToFull, "%u Min", param.timeToFull);
        }

    }, 1000, label);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, lv_event_get_target_obj(e));
    }, NULL);
#endif

}



void ui_monitor_exit(lv_obj_t *parent)
{

}

app_t ui_monitor_main = {
    .setup_func_cb = ui_monitor_enter,
    .exit_func_cb = ui_monitor_exit,
    .user_data = nullptr,
};


