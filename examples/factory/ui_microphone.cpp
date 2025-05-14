/**
 * @file      ui_microphone.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

static lv_timer_t *timer = NULL;
static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
        }
        hw_set_mic_stop();
        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}


#ifndef ARDUINO
static void set_temp(void *bar, int32_t temp)
{
    lv_bar_set_value((lv_obj_t *)bar, temp, LV_ANIM_ON);
}
#endif

static void get_pressure_level(lv_timer_t *t)
{
    lv_obj_t *bar = static_cast<lv_obj_t *>(lv_timer_get_user_data(t));
    if (!bar) {
        lv_timer_del(t);
        return;
    }
    int16_t level = hw_get_microphone_pressure_level();
    lv_bar_set_value(bar, level, LV_ANIM_ON);
}

void ui_microphone_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    /*Create a main page*/
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    static lv_style_t style_indic;

    lv_style_init(&style_indic);
    lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&style_indic, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_bg_grad_color(&style_indic, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_grad_dir(&style_indic, LV_GRAD_DIR_VER);

    lv_obj_t *obj = lv_obj_create(main_page);
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_center(obj);

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, "Microphone Pressure level:");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, lv_pct(10));

    lv_obj_t *bar = lv_bar_create(obj);
    lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);
    lv_obj_set_size(bar, 200, 20);
    lv_obj_center(bar);
    lv_bar_set_range(bar, 0, 100);

#ifndef ARDUINO
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_temp);
    lv_anim_set_time(&a, 3000);
    lv_anim_set_playback_time(&a, 3000);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, -20, 40);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
#endif

    lv_menu_set_page(menu, main_page);

    hw_set_mic_start();

    timer =  lv_timer_create(get_pressure_level, 100, bar);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif

}


void ui_microphone_exit(lv_obj_t *parent)
{

}

app_t ui_microphone_main = {
    .setup_func_cb = ui_microphone_enter,
    .exit_func_cb = ui_microphone_exit,
    .user_data = nullptr,
};


