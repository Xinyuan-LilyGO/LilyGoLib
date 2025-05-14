/**
 * @file      ui_msgchat.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

static lv_timer_t *timer = NULL;
static lv_group_t *menu_g;
static lv_obj_t *rx_msg_ta = NULL;
static char recv_buf[1024];
static radio_rx_params_t rx_params;
#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;
#endif

static  float freq_plan[] = {
    433.0,
    470.0,
    868.0,
    915.0,
    920.0,
    923.0
};
const uint8_t list_size = sizeof(freq_plan) / sizeof(freq_plan[0]);
static lv_obj_t *ta_list[list_size];
static uint8_t ta_index = 0;
static uint8_t sel_ta_index = 0;
static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;

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
        if (timer) {
            lv_timer_del(timer); timer = NULL;
        }
        hw_set_radio_default();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        rx_msg_ta = NULL;
        ta_index = 0;
        sel_ta_index = 0;

        disable_keyboard();

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
#ifdef USING_TOUCHPAD
    else {
        lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    }
#endif
}

static void msg_chat_receiver_task(lv_timer_t *t)
{

    rx_params.data = (uint8_t *)recv_buf;
    rx_params.length = sizeof(recv_buf);
    hw_get_radio_rx(rx_params);
    if (rx_params.state == 0 && rx_params.length != 0) {
        recv_buf[rx_params.length + 1] = '\0';
        // printf("Recv msg : %s len:%u set ta %p\n", recv_buf, rx_params.length, ta_list[sel_ta_index]);
        lv_textarea_set_text(ta_list[sel_ta_index], recv_buf);
    }
}

static void msg_chat_send_callback(lv_event_t *e)
{
    lv_obj_t *msg_ta =  (lv_obj_t *)lv_event_get_user_data(e);
    const char *messages =  lv_textarea_get_text(msg_ta);
    uint32_t msg_len = strlen(messages);
    if (msg_len == 0) {
        return;
    }
    printf("Send msg : %s\n", messages);
    static radio_tx_params_t tx_params;
    tx_params.data = (uint8_t *)(messages);
    tx_params.length = msg_len;
    hw_set_radio_tx(tx_params, false);
    if (tx_params.state == 0) {

    } else {

    }

    hw_set_radio_listening();
}

static void _msg_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool state =  lv_obj_has_state(ta, LV_STATE_FOCUSED);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);

#ifdef USING_TOUCHPAD
    if (code == LV_EVENT_READY) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
#endif

    if (code == LV_EVENT_CLICKED) {
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            printf("disable keyboard\n");
            disable_keyboard();
        }
#ifdef USING_TOUCHPAD
        else {
            lv_obj_scroll_to(ta, 0, -150, LV_ANIM_ON);
            lv_keyboard_set_textarea(keyboard, ta);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(quit_btn, LV_ALIGN_TOP_RIGHT, -10, 0);
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
        }
    }
#endif
}


static lv_obj_t *ui_msg_chat_create_ta(lv_obj_t *parent)
{
    int offset_y = 0;
#ifdef USING_TOUCHPAD
    offset_y = -70;
#endif


    lv_obj_t *cont_obj = lv_obj_create(parent);
    lv_obj_set_size(cont_obj, lv_pct(100), lv_pct(100));

    lv_obj_set_style_border_width(cont_obj, 0, 0);
    lv_obj_t *label;

    lv_obj_t *rx_msg_ta = lv_textarea_create(cont_obj);
    lv_textarea_set_placeholder_text(rx_msg_ta, "Messages received are displayed here");
    lv_textarea_set_text_selection(rx_msg_ta, false);
    lv_textarea_set_one_line(rx_msg_ta, false);
    lv_textarea_set_cursor_click_pos(rx_msg_ta, false);
    lv_textarea_set_max_length(rx_msg_ta, 128);
    lv_obj_set_size(rx_msg_ta, lv_pct(100), lv_pct(65));
    lv_obj_align(rx_msg_ta, LV_ALIGN_TOP_LEFT, 0, -10);

    ta_list[ta_index] = rx_msg_ta;
    printf("Create ta %u : %p\n", ta_index, rx_msg_ta);
    ta_index++;

    lv_group_remove_obj(rx_msg_ta);

    lv_obj_t *tx_msg_ta = lv_textarea_create(cont_obj);
    lv_textarea_set_placeholder_text(tx_msg_ta, "Message to send Enter here");
    lv_textarea_set_text_selection(tx_msg_ta, false);
    lv_textarea_set_cursor_click_pos(tx_msg_ta, false);
    lv_textarea_set_one_line(tx_msg_ta, true);
    lv_obj_set_width(tx_msg_ta, lv_pct(85));
    lv_obj_align(tx_msg_ta, LV_ALIGN_BOTTOM_LEFT, 0, offset_y);
    lv_obj_add_event_cb(tx_msg_ta, _msg_ta_cb, LV_EVENT_ALL, NULL);


    lv_obj_t *btn = lv_btn_create(cont_obj);
    label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_OK);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, offset_y);

    lv_obj_add_event_cb(btn, msg_chat_send_callback, LV_EVENT_CLICKED, tx_msg_ta);

    return cont_obj;
}

static lv_obj_t *ui_msg_chat_create(lv_obj_t *menu, lv_obj_t *main_page, float freq)
{
    lv_obj_t *cont = lv_menu_cont_create(main_page);
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text_fmt(label, LV_SYMBOL_WIFI"  Channel %.0f MHz", freq);
    lv_obj_t *sub_page = lv_menu_page_create(menu, NULL);
    ui_msg_chat_create_ta(sub_page);
    lv_menu_set_load_page_event(menu, cont, sub_page);
    return cont;
}


static void msg_chat_event_cb(lv_event_t *e)
{
    float *freq =  (float *)lv_event_get_user_data(e);
    printf("Click event:%.2f MHZ\n", *freq);

    for (int i = 0; i < list_size; ++i) {
        if (freq_plan[i] == *freq) {
            sel_ta_index = i;
            printf("Select index = %u\n", sel_ta_index);
        }
    }
    radio_params_t params;
    params.freq = *freq;
    params.bandwidth = 125.0;
    params.cr = 5;
    params.sf  = 12;
    params.power = 22;
    params.syncWord = 0xAB;
    params.interval = 0;
    params.mode = RADIO_RX;
    hw_set_radio_params(params);
}

void ui_msgchat_enter(lv_obj_t *parent)
{
    menu_g = lv_group_get_default();

    menu = create_menu(parent, back_event_handler);

    lv_obj_t *cont;
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    for (uint16_t i = 0; i < list_size; i++) {
        cont =  ui_msg_chat_create(menu, main_page, freq_plan[i]);
        lv_obj_add_event_cb(cont, msg_chat_event_cb, LV_EVENT_CLICKED, &freq_plan[i]);
        lv_group_add_obj(menu_g, cont);
    }

#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event(keyboard, [](lv_event_t * e) {
        // lv_obj_t *kb = lv_event_get_target_obj(e);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    }, LV_EVENT_READY, NULL);
#endif

    lv_menu_set_page(menu, main_page);

    timer = lv_timer_create(msg_chat_receiver_task, 300, NULL);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
#endif

}


void ui_msgchat_exit(lv_obj_t *parent)
{

}

app_t ui_msgchat_main = {
    .setup_func_cb = ui_msgchat_enter,
    .exit_func_cb = ui_msgchat_exit,
    .user_data = nullptr,
};

