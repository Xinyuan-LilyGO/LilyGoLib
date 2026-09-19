/**
 * @file      ui_ir_remote.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-05-15
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_IR_REMOTE)
static lv_obj_t *page_container = NULL;
static uint32_t nec_code = 0x12345678;  // LilyGo Factory ir remote test nec code
static lv_obj_t *keyboard = NULL;
static lv_obj_t *input_textarea = NULL;


static void back_event_handler(lv_event_t *e)
{
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    menu_show();
}

static void send_event_handler(lv_event_t *e)
{
    hw_feedback();
    hw_set_remote_code(nec_code);
}

static void _msg_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);
    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        const char *txt = lv_textarea_get_text(input_textarea);
        if (txt[0] == '0' && (txt[1] == 'x' || txt[1] == 'X')) {
            nec_code = (uint32_t)strtoul(&txt[2], NULL, 16);
        } else {
            nec_code = (uint32_t)strtoul(&txt[0], NULL, 16);
        }
        LILYGO_LOG_PRINTF("1. Input NEC Code: 0x%x\n", nec_code);

    } else if (code == LV_EVENT_CLICKED) {
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
        } else {
            lv_keyboard_set_textarea(keyboard, ta);
            lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            enable_keyboard();
        }
    }
}

void ui_ir_remote_enter(lv_obj_t *parent)
{
    // Select the IR sender function
    hw_ir_function_select(true);

    page_container = ui_create_app_page(parent, "IR Remote", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "NEC Code");

    input_textarea = lv_textarea_create(card);
    lv_obj_set_width(input_textarea, 140);
    lv_textarea_set_text_selection(input_textarea, false);
    lv_textarea_set_cursor_click_pos(input_textarea, false);
    lv_textarea_set_one_line(input_textarea, true);
    lv_textarea_set_accepted_chars(input_textarea, "0123456789ABCDEFabcdef");
    lv_textarea_set_max_length(input_textarea, 8);
    lv_textarea_set_placeholder_text(input_textarea, "0x12345678");
    lv_obj_set_scrollbar_mode(input_textarea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(input_textarea, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(input_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input_textarea, lv_color_white(), 0);
    lv_obj_set_style_border_color(input_textarea, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(input_textarea, 1, 0);
    lv_obj_set_style_radius(input_textarea, 6, 0);
    lv_obj_set_style_pad_all(input_textarea, 4, 0);
    lv_obj_add_event_cb(input_textarea, _msg_ta_cb, LV_EVENT_ALL, NULL);
    ui_create_card_item(card, LV_SYMBOL_WIFI, "Hex Code", input_textarea);

    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    /* Action buttons — rectangular, left=Back, right=Send (like LoRa page) */
    lv_obj_t *btn_row = lv_obj_create(page_container);
    lv_obj_set_size(btn_row, LV_PCT(100), 44);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 12, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Back button — left */
    lv_obj_t *back_btn = lv_btn_create(btn_row);
    lv_obj_set_size(back_btn, LV_PCT(40), 36);
    lv_obj_set_style_bg_color(back_btn, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, UI_COLOR_DIVIDER, 0);
    {
        lv_obj_t *l = lv_label_create(back_btn);
        lv_label_set_text(l, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_color(l, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(l);
    }
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        back_event_handler(e);
    }, LV_EVENT_CLICKED, NULL);

    /* Send button — right */
    lv_obj_t *send_btn = lv_btn_create(btn_row);
    lv_obj_set_size(send_btn, LV_PCT(40), 36);
    lv_obj_set_style_bg_color(send_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(send_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(send_btn, 8, 0);
    lv_obj_set_style_border_width(send_btn, 0, 0);
    ui_add_accent_focus_style(send_btn);
    {
        lv_obj_t *l = lv_label_create(send_btn);
        lv_label_set_text(l, LV_SYMBOL_OK " Send");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
    }
    lv_obj_add_event_cb(send_btn, send_event_handler, LV_EVENT_CLICKED, NULL);
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
