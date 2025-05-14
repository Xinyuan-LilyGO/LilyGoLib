/**
 * @file      ui_nrf24.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#ifdef USING_EXTERN_NRF2401

#define RADIO_FREQUENCY_LIST    "2400MHz\n""2424MHz\n""2450MHZ\n""2500MHz\n""2525MHz"
#define RADIO_TX_POWER_LIST     "-18dBm\n""-12dBm\n""-6dBm\n""0dBm"
#define RADIO_INTERVAL_LIST     "1000ms\n""2000ms\n""3000ms"
#define RADIO_MODE_LIST         "Disable\n""TX Mode\n""RX Mode"
#define RADIO_SF_LIST           "5\n""6\n""7\n""8\n""9\n""10\n""11\n""12"
#define RADIO_CR_LIST           "5\n""6\n""7\n""8"

static const float radio_freq_args_list[] = {2400.0, 2424.0, 2450, 2500.0, 2525.0};
static const float radio_power_args_list[] = {-18, -12, -6, 0};
static const uint16_t radio_interval_args_list[] = {1000, 2000, 3000};
static const uint8_t radio_dr_args_list[] = {5, 6, 7, 8};

static lv_obj_t *menu = NULL;
static lv_obj_t *radio_msg_label = NULL;
static radio_params_t radio_params_copy;
static uint8_t radio_run_mode = RADIO_DISABLE;
static lv_timer_t *timer = NULL;

static void radio_timer_task(lv_timer_t *t);
static void ui_set_msg_label(const char *msg);

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
        }
        // Disable Radio RX or TX
        radio_run_mode = RADIO_DISABLE;
        radio_params_copy.mode = RADIO_DISABLE;
        hw_set_nrf24_params(radio_params_copy);

        lv_obj_clean(menu);
        lv_obj_del(menu);

        menu_show();
    }
}

static void _ui_nrf24_obj_event(lv_event_t *e)
{
    uint16_t selected = 0;
    string opt;
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    const char *flag = ( const char *)lv_event_get_user_data(e);
    const char *prefix = "RX Mode";
    char buf[64];

    if (*flag != 'b') {
        selected =  lv_dropdown_get_selected(obj);
    }

    switch (*flag) {
    case 'f':   //*frequency
        radio_params_copy.freq = radio_freq_args_list[selected];
        printf("set freq:%.2f\n", radio_params_copy.freq);
        break;
    case 't':   //*tx power
        radio_params_copy.power = radio_power_args_list[selected];
        printf("set power:%u selected:%u\n", radio_params_copy.power, selected);
        break;
    case 'i':   //*interval
        radio_params_copy.interval = radio_interval_args_list[selected];
        printf("set interval:%u\n", radio_params_copy.interval);
        break;
    case 'm':   //*mode
        lv_dropdown_get_selected_str(obj, buf, 64);
        if (strncmp(buf, prefix, lv_strlen(prefix)) == 0) {
            // lv_obj_clear_flag(radio_msg_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            // lv_obj_add_flag(radio_msg_label, LV_OBJ_FLAG_HIDDEN);
        }
        radio_params_copy.mode = selected;
        break;
    case 'c':   //*data rate
        radio_params_copy.cr = radio_dr_args_list[selected];
        break;
    case 'b':   //*btn
        hw_set_nrf24_params(radio_params_copy);
        radio_run_mode = radio_params_copy.mode;
        switch (radio_params_copy.mode) {
        case RADIO_DISABLE:
            if (timer) {
                lv_timer_pause(timer);
            }
            break;
        case RADIO_TX:
            if (timer) {
                lv_timer_resume(timer);
                lv_timer_set_period(timer, radio_params_copy.interval);
            }
            break;
        case RADIO_RX:
            if (timer) {
                lv_timer_resume(timer);
                lv_timer_set_period(timer, 300);
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}


static lv_obj_t *create_frequency_dropdown(lv_obj_t *parent)
{
    static const char flag = 'f';
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, RADIO_FREQUENCY_LIST);
    lv_obj_add_event_cb(dd, _ui_nrf24_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_freq_args_list) {
        if (i == radio_params_copy.freq) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    return dd;
}



static lv_obj_t *create_tx_power_dropdown(lv_obj_t *parent)
{
    static const char flag = 't';
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, RADIO_TX_POWER_LIST);
    lv_obj_add_event_cb(dd, _ui_nrf24_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_power_args_list) {
        if (i == radio_params_copy.power) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    return dd;
}

static lv_obj_t *create_tx_interval_dropdown(lv_obj_t *parent)
{
    static const char flag = 'i';
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, RADIO_INTERVAL_LIST);
    lv_obj_add_event_cb(dd, _ui_nrf24_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_interval_args_list) {
        if (i == radio_params_copy.interval) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    return dd;
}

static lv_obj_t *create_mode_dropdown(lv_obj_t *parent)
{
    static const char flag = 'm';
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, RADIO_MODE_LIST);
    lv_obj_add_event_cb(dd, _ui_nrf24_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    switch (radio_params_copy.mode) {
    case RADIO_DISABLE:
        lv_dropdown_set_selected(dd, 0);
        ui_set_msg_label( "RADIO DISABLE");
        break;
    case RADIO_TX:
        lv_dropdown_set_selected(dd, 1);
        ui_set_msg_label("RADIO TX");
        break;
    case RADIO_RX:
        lv_dropdown_set_selected(dd, 2);
        ui_set_msg_label("RADIO RX");
        break;
    default:
        break;
    }
    return dd;
}

static lv_obj_t *create_dr_dropdown(lv_obj_t *parent)
{
    static const char flag = 'c';
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, RADIO_CR_LIST);
    lv_obj_add_event_cb(dd, _ui_nrf24_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_dr_args_list) {
        if (i == radio_params_copy.cr) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    return dd;
}


static lv_obj_t *create_state_textarea(lv_obj_t *parent)
{
    //Rx Receiver msg box
    radio_msg_label = lv_textarea_create(parent);
    lv_textarea_set_text_selection(radio_msg_label, false);
    lv_textarea_set_cursor_click_pos(radio_msg_label, false);
    lv_textarea_set_one_line(radio_msg_label, true);
    lv_obj_set_scrollbar_mode(radio_msg_label, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(radio_msg_label, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
        }
    }, LV_EVENT_ALL, NULL);

    return radio_msg_label;
}


static void ui_set_msg_label(const char *msg)
{
    if (radio_msg_label) {
        lv_textarea_set_text(radio_msg_label, msg);
        lv_textarea_set_cursor_pos(radio_msg_label, 0);
    }
}

static void _msg_ta_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool state =  lv_obj_has_state(ta, LV_STATE_FOCUSED);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);
    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
        if (code == LV_EVENT_CLICKED) {
            if (edited) {
                lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
                printf("disable keyboard\n");
                disable_keyboard();
                const char *text = lv_textarea_get_text(ta);
                if (text) {
                    radio_params_copy.syncWord = atoi(text);
                    printf("syncword -> %s - DEC:%d\n", text, radio_params_copy.syncWord);
                }
            }
        } else if (code == LV_EVENT_FOCUSED) {
            if (edited) {
                printf("enable input keyboard \n");
                enable_keyboard();
            }
        }
    }
}


static void radio_timer_task(lv_timer_t *t)
{
    static radio_tx_params_t tx_params;
    static radio_rx_params_t rx_params;
    static uint32_t dummy_tx_payload = 0;
    static uint32_t dummy_rx_payload = 0;
    char msg[128];
    bool rlst = false;
    static uint32_t last_sended = 0;
    int tick = lv_tick_get() / 1000;

    uint8_t tmp_buffer[5] = {0};

    switch (radio_run_mode) {
    case RADIO_DISABLE:
        break;
    case RADIO_TX:
        // Discard the first byte
        memcpy(&tmp_buffer[1], &dummy_tx_payload, sizeof(dummy_tx_payload));
        tx_params.data = tmp_buffer;
        tx_params.length = sizeof(tmp_buffer);

        rlst = hw_set_nrf24_tx(tx_params);
        if (!rlst && ((lv_tick_get() - last_sended) > radio_params_copy.interval)) {
            printf("Clear ISR flag\n");
            hw_clear_nrf24_flag();
        }
        if (tx_params.state == 0) {
            snprintf(msg, 128, "[%u]Tx PASS :%u", tick, dummy_tx_payload);
            ui_set_msg_label(msg);
            dummy_tx_payload++;
            last_sended = lv_tick_get();
        }
        break;
    case RADIO_RX:
        dummy_tx_payload = 0;
        rx_params.data = tmp_buffer;
        rx_params.length = sizeof(tmp_buffer);
        hw_get_nrf24_rx(rx_params);
        if (rx_params.state == 0) {
            memcpy(&dummy_rx_payload, &tmp_buffer[1], sizeof(dummy_rx_payload));
            snprintf(msg, 128, "[%u]Rx PASS :%u", tick, dummy_rx_payload);
            ui_set_msg_label(msg);
        }
        break;
    default:
        break;
    }
}

void ui_nrf24_enter(lv_obj_t *parent)
{
    static const char flag = 'b';

    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    if (!hw_has_nrf24()) {

        lv_obj_t *cont = lv_obj_create(main_page);
        lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
        lv_obj_center(cont);
        lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);

        LV_IMG_DECLARE(img_cry);
        lv_obj_t *img = lv_img_create(cont);
        lv_img_set_src(img, &img_cry);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, lv_pct(10));

        lv_obj_t *label = lv_label_create(cont);
        lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
        lv_label_set_text(label, "NRF2401 module not detected.");

        lv_menu_set_page(menu, main_page);
        lv_obj_align_to(label, img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

#ifdef USING_TOUCHPAD
        quit_btn  = create_floating_button([](lv_event_t*e) {
            lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
        }, NULL);
#endif

        return;
    }


    hw_get_nrf24_params(radio_params_copy);
    ui_create_option(main_page, "State:", NULL, create_state_textarea, NULL);
    ui_create_option(main_page, "Mode:", NULL, create_mode_dropdown, NULL);
    ui_create_option(main_page, "Frequency:", NULL, create_frequency_dropdown, NULL);
    ui_create_option(main_page, "TX Power:", NULL, create_tx_power_dropdown, NULL);
    ui_create_option(main_page, "Tx Interval:", NULL, create_tx_interval_dropdown, NULL);
    ui_create_option(main_page, "Data rate:", NULL, create_dr_dropdown, NULL);

    timer =  lv_timer_create(radio_timer_task, 1000, NULL);
    lv_timer_pause(timer);

    lv_obj_t *cont = lv_menu_cont_create(main_page);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, lv_pct(100), 80);

    int w =  lv_disp_get_hor_res(NULL) / 5;
    lv_obj_t *quit_btn = create_radius_button(cont, LV_SYMBOL_LEFT, [](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_remove_flag(quit_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, -w, -20);

    lv_obj_t *ok_btn = create_radius_button(cont, LV_SYMBOL_OK, _ui_nrf24_obj_event,  (void *)&flag);
    lv_obj_remove_flag(ok_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, w, -20);

    lv_menu_set_page(menu, main_page);

}


void ui_nrf24_exit(lv_obj_t *parent)
{

}


app_t ui_nrf24_main = {
    .setup_func_cb = ui_nrf24_enter,
    .exit_func_cb = ui_nrf24_exit,
    .user_data = nullptr,
};

#endif /*USING_EXTERN_NRF2401*/

