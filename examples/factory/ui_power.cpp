/**
 * @file      ui_power.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#if defined(USING_PPM_MANAGE)
const char *tips = "Are you sure you want to turn off the device? Wake up by pressing the PWR button or plugging in a charger.";
#else
const char *tips = "Are you sure you want to shut down the device? Wake up by pressing the BOOT button or the PWR button.";
#endif

static lv_obj_t *msgbox = NULL;

static void event_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);

#if LVGL_VERSION_MAJOR == 9
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *text = lv_label_get_text(label);
    printf("Button %s clicked\n", text);
#else
    const char *text = lv_msgbox_get_active_btn_text(obj);
    printf("Button %s clicked", text );
#endif
    if (strcmp(text, "Shutdown") == 0) {
        LV_FONT_DECLARE(logo_font_80);
        LV_FONT_DECLARE(logo_font1_32);
        lv_obj_clean(lv_scr_act());
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
        lv_obj_t *logo = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_font(logo, &logo_font_80, LV_PART_MAIN);
        lv_obj_set_style_text_color(logo, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(logo, "LilyGo");
        lv_obj_center(logo);

        lv_obj_t *label1 = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_font(label1, &logo_font1_32, LV_PART_MAIN);
        lv_obj_set_style_text_color(label1, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(label1, "LoRa Pager");
        lv_obj_align_to(label1, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

        hw_shutdown();

    } else if (strcmp(text, "Sleep") == 0) {

        hw_sleep();

    } else {
        printf("msgbox close \n");
#if LVGL_VERSION_MAJOR == 9
        lv_msgbox_close(msgbox);
#else
        // hide the btnmatrix
        lv_obj_t *msg_btns = lv_msgbox_get_btns(msgbox);
        lv_group_focus_obj(msg_btns);
        lv_btnmatrix_set_btn_ctrl_all(msg_btns, LV_BTNMATRIX_CTRL_HIDDEN);
        lv_msgbox_close_async(msgbox);
#endif
        menu_show();
    }
}

void ui_power_enter(lv_obj_t *parent)
{
    static const char *btns[] = {"Shutdown", "Sleep", "Close", ""};
#if LVGL_VERSION_MAJOR == 9
    msgbox = lv_msgbox_create(lv_scr_act());
    lv_obj_set_size(msgbox, lv_pct(90), lv_pct(80));
    lv_msgbox_add_text(msgbox, tips);
    lv_msgbox_add_title(msgbox, "Tips");
    for (auto str : btns) {
        if (lv_strcmp(str, "") == 0) {
            break;
        }
        lv_obj_t *btn;
        btn = lv_msgbox_add_footer_button(msgbox, str);
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }
    return;
#else
    msgbox = lv_msgbox_create(parent, "Tips", tips, btns, false);
    lv_obj_set_size(msgbox, lv_pct(95), lv_pct(80));
    lv_obj_add_event_cb(msgbox, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
#endif
    lv_obj_center(msgbox);

}

void ui_power_exit(lv_obj_t *parent)
{

}

app_t ui_power_main = {
    .setup_func_cb = ui_power_enter,
    .exit_func_cb = ui_power_exit,
    .user_data = nullptr,
};


