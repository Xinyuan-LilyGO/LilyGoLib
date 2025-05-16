/**
 * @file      ui_ir_remote.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-05-15
 *
 */
#include "ui_define.h"

#if defined(USING_IR_REMOTE)
static lv_obj_t *menu = NULL;
static uint32_t nec_code = 0x12345678;  // LilyGo Factory ir remote test nec code

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu_show();
    }
}

static void send_event_handler(lv_event_t *e)
{
    hw_set_remote_code(nec_code);
}

void ui_ir_remote_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *cont = lv_obj_create(menu);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, "NEC Code(Dec Format):");
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, -40);


    lv_obj_t *ta = lv_textarea_create(cont);
    lv_obj_set_width(ta, lv_pct(95));
    lv_textarea_set_text_selection(ta, false);
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_accepted_chars(ta, "012345678");
    lv_textarea_set_max_length(ta, 8);
    lv_textarea_set_placeholder_text(ta, "0x12345678");
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align_to(ta, label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

    int w =  lv_disp_get_hor_res(NULL) / 5;
    lv_obj_t *quit_btn = create_radius_button(cont, LV_SYMBOL_LEFT, [](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_remove_flag(quit_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, -w, -20);

    lv_obj_t *ok_btn = create_radius_button(cont, LV_SYMBOL_OK, send_event_handler,  NULL);
    lv_obj_remove_flag(ok_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, w, -20);
}

void ui_ir_remote_exit(lv_obj_t *parent)
{

}

app_t ui_ir_remote_main = {
    .setup_func_cb = ui_ir_remote_enter,
    .exit_func_cb = ui_ir_remote_exit,
    .user_data = nullptr,
};

#endif
