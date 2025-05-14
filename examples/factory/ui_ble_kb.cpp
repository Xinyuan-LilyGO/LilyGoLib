/**
 * @file      ui_ble_kb.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-13
 *
 */
#include "ui_define.h"

static lv_timer_t *timer = NULL;

typedef struct {
    lv_obj_t *bar;
    lv_obj_t *label;
    lv_obj_t *state;
} ble_kb_state_t;

static ble_kb_state_t ble_kb_state;
static lv_obj_t *menu = NULL;
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
        hw_set_keyboard_read_callback(NULL);
        hw_set_ble_kb_disable();
        disable_keyboard();

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}


void ui_ble_kb_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *label;
    lv_obj_t *page = lv_obj_create(main_page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *bar = lv_bar_create(page);
    lv_obj_set_size(bar, lv_pct(50), lv_pct(80));
    lv_bar_set_value(bar, 90, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    ble_kb_state.bar = bar;

    label = lv_label_create(bar);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text_fmt(label, "%u%%", 99);
    lv_obj_center(label);
    ble_kb_state.label = label;


    lv_obj_t *cont2 = lv_obj_create(page);
    lv_obj_set_size(cont2, lv_pct(50), lv_pct(80));
    lv_obj_align(cont2, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(cont2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_opa(cont2, LV_OPA_TRANSP, LV_PART_MAIN);

    label = lv_label_create(cont2);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "Mode:BLE Keyboard Mode");

    label = lv_label_create(cont2);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "DeviceName: T-Pager-Lora");

    label = lv_label_create(cont2);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "STATE: Disconnected");
    ble_kb_state.state = label;

    lv_menu_set_page(menu, main_page);

    timer = lv_timer_create([](lv_timer_t *t) {
        monitor_params_t params;
        hw_get_monitor_params(params);
        lv_bar_set_value(ble_kb_state.bar, params.battery_percent, LV_ANIM_ON);
        lv_label_set_text_fmt(ble_kb_state.label, "%u%%", params.battery_percent);
        lv_label_set_text_fmt(ble_kb_state.state, "STATE: %s", hw_get_ble_kb_connected() ? "Connected" : "Disconnected");
    }, 3000, NULL);

    hw_set_keyboard_read_callback([](int state, char &c) {
        printf("state:%d char:%c\n", state, c);
        if (state == 1) {
            hw_set_ble_kb_char(&c);
        }
    });

    enable_keyboard();

    hw_set_ble_kb_enable();


#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_RIGHT, -50, -50);
#endif

}

void ui_ble_kb_exit(lv_obj_t *parent)
{

}

app_t ui_ble_kb_main = {
    .setup_func_cb = ui_ble_kb_enter,
    .exit_func_cb = ui_ble_kb_exit,
    .user_data = nullptr,
};


