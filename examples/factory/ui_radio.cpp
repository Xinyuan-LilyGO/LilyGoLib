/**
 * @file      ui_radio.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#ifndef EXCLUDE_LORA

#define RADIO_INTERVAL_LIST     "100ms\n""200ms\n""500ms\n""1000ms\n""2000ms\n""3000ms"
#define RADIO_MODE_LIST         "Disable\n""TX Mode\n""RX Mode\n""TxContinuousWave"
#define RADIO_SF_LIST           "5\n""6\n""7\n""8\n""9\n""10\n""11\n""12"
#define RADIO_CR_LIST           "5\n""6\n""7\n""8"

static const uint16_t radio_interval_args_list[] = {100, 200, 500, 1000, 2000, 3000};
static const uint8_t radio_sf_args_list[] = {5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t radio_cr_args_list[] = {5, 6, 7, 8};

static bool _high_freq = false;
static lv_obj_t *bandwidth_dd = nullptr;
static lv_obj_t *frequency_dd = nullptr;
static lv_obj_t *power_level_dd = nullptr;
static lv_obj_t *page_container = NULL;
static lv_obj_t *radio_msg_label = NULL;
radio_params_t radio_params_copy;
static uint8_t radio_run_mode = RADIO_DISABLE;
static lv_timer_t *timer = NULL;
static uint32_t dummy_tx_payload = 0;
static uint32_t dummy_rx_payload = 0;

static void ui_set_msg_label(const char *msg);

static void radio_timer_task(lv_timer_t *t);

static void _do_cleanup(void)
{
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
        radio_run_mode = RADIO_DISABLE;
        radio_params_copy.mode = RADIO_DISABLE;
        hw_set_radio_params(radio_params_copy);
    }

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    _high_freq = false;
    menu_show();
    dummy_tx_payload = 0;
    dummy_rx_payload = 0;
}

static void back_event_handler(lv_event_t *e)
{
    _do_cleanup();
}

static void _ui_radio_obj_event(lv_event_t *e)
{
    uint16_t selected = 0;
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    const char *flag = ( const char *)lv_event_get_user_data(e);
    const char *prefix = "RX Mode";
    char buf[64];
    int index = 0;
    if (*flag != 'b') {
        selected =  lv_dropdown_get_selected(obj);
    }

    hw_feedback();

    switch (*flag) {
    case 'f':   //*frequency
        radio_params_copy.freq = radio_get_freq_from_index(selected);
        LILYGO_LOG_PRINTF("set freq:%.2f\n", radio_params_copy.freq);
        if (radio_params_copy.freq > 960.0) {
            if (!_high_freq) {
                lv_dropdown_set_options(power_level_dd, radio_get_tx_power_list(true));
                lv_dropdown_set_options(bandwidth_dd, radio_get_bandwidth_list(true));

                radio_params_copy.bandwidth = radio_get_bandwidth_from_index(255); // 255 get default bandwidth
                radio_params_copy.power = radio_get_tx_power_from_index(255);   //255 Get Max Power level
                _high_freq = true;
            }

        } else {
            if (_high_freq) {
                lv_dropdown_set_options(power_level_dd, radio_get_tx_power_list(false));
                lv_dropdown_set_options(bandwidth_dd, radio_get_bandwidth_list(false));
                radio_params_copy.bandwidth = radio_get_bandwidth_from_index(255); // 255 get default bandwidth
                radio_params_copy.power = radio_get_tx_power_from_index(255);  //255 Get Max Power level
                _high_freq = false;
            }
        }

        for (int i = 0; i < radio_get_bandwidth_length(); ++i) {
            if (radio_get_bandwidth_from_index(i) == radio_params_copy.bandwidth) {
                lv_dropdown_set_selected(bandwidth_dd, index);
                break;
            }
            index++;
        }
        index = 0;
        for (int i = 0; i < radio_get_tx_power_length(); ++i) {
            if (radio_get_tx_power_from_index(i) == radio_params_copy.power) {
                lv_dropdown_set_selected(power_level_dd, index);
                break;
            }
            index++;
        }

        break;
    case 'w':   //*bandwidth
        radio_params_copy.bandwidth = radio_get_bandwidth_from_index(selected);
        LILYGO_LOG_PRINTF("set bandwidth:%.2f\n", radio_params_copy.bandwidth);
        break;
    case 't':   //*tx power
        radio_params_copy.power = radio_get_tx_power_from_index(selected);
        LILYGO_LOG_PRINTF("set power:%u selected:%u\n", radio_params_copy.power, selected);
        break;
    case 'i':   //*interval
        radio_params_copy.interval = radio_interval_args_list[selected];
        LILYGO_LOG_PRINTF("set interval:%u\n", radio_params_copy.interval);
        break;
    case 'm':   //*mode
        lv_dropdown_get_selected_str(obj, buf, 64);
        radio_params_copy.mode = selected;
        break;
    case 'c':   //*coding rate
        radio_params_copy.cr = radio_cr_args_list[selected];
        LILYGO_LOG_PRINTF("set cr:%u\n", radio_params_copy.cr);
        break;
    case 's':   //*spreading factor
        radio_params_copy.sf = radio_sf_args_list[selected];
        LILYGO_LOG_PRINTF("set sf:%u\n", radio_params_copy.sf);
        break;
    case 'b':   //*btn
        hw_set_radio_params(radio_params_copy);

        radio_run_mode = radio_params_copy.mode;
        switch (radio_params_copy.mode) {
        case RADIO_DISABLE:
            if (timer) {
                lv_timer_pause(timer);
            }
            ui_set_msg_label("DISABLE");
            break;
        case RADIO_TX:
            if (timer) {
                lv_timer_resume(timer);
                lv_timer_set_period(timer, radio_params_copy.interval);
            }
            ui_set_msg_label("Waiting to send");
            break;
        case RADIO_RX:
            if (timer) {
                lv_timer_resume(timer);
                lv_timer_set_period(timer, 300);
            }
            ui_set_msg_label("Listening");
            break;
        case RADIO_CW:
            ui_set_msg_label("Continuous wave");
            break;
        default:
            break;
        }
        break;
#ifdef HAS_USB_RF_SWITCH
    case 'u':   //*usb/rf switch
        if (selected == 0) {
            hw_set_usb_rf_switch(false);
        } else {
            hw_set_usb_rf_switch(true);
        }
        break;
#endif
    default:
        break;
    }
}


lv_obj_t *create_frequency_dropdown(lv_obj_t *card)
{
    static const char flag = 'f';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    const char *freq_list = radio_get_freq_list();
    lv_dropdown_set_options(dd, freq_list);
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (int i = 0; i < radio_get_freq_length(); ++i) {
        if (radio_get_freq_from_index(i) == radio_params_copy.freq) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }

    ui_create_card_item(card, LV_SYMBOL_WIFI, "Frequency", dd);
    return dd;
}

lv_obj_t *create_bandwidth_dropdown(lv_obj_t *card)
{
    static const char flag = 'w';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, radio_get_bandwidth_list());
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (int i = 0; i < radio_get_bandwidth_length(); ++i) {
        if (radio_get_bandwidth_from_index(i) == radio_params_copy.bandwidth) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    bandwidth_dd = dd;
    ui_create_card_item(card, LV_SYMBOL_WIFI, "Bandwidth", dd);
    return dd;
}

lv_obj_t *create_tx_power_dropdown(lv_obj_t *card)
{
    static const char flag = 't';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, radio_get_tx_power_list());
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (int i = 0; i < radio_get_tx_power_length(); ++i) {
        if (radio_get_tx_power_from_index(i) == radio_params_copy.power) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    power_level_dd = dd;
    ui_create_card_item(card, LV_SYMBOL_WIFI, "TX Power", dd);
    return dd;
}



static lv_obj_t *create_tx_interval_dropdown(lv_obj_t *card)
{
    static const char flag = 'i';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, RADIO_INTERVAL_LIST);
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_interval_args_list) {
        if (i == radio_params_copy.interval) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    ui_create_card_item(card, LV_SYMBOL_LOOP, "Tx Interval", dd);
    return dd;
}

static lv_obj_t *create_mode_dropdown(lv_obj_t *card)
{
    static const char flag = 'm';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, RADIO_MODE_LIST);
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    switch (radio_params_copy.mode) {
    case RADIO_DISABLE:
        lv_dropdown_set_selected(dd, 0);
        ui_set_msg_label("DISABLE");
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
    ui_create_card_item(card, LV_SYMBOL_SETTINGS, "Mode", dd);
    return dd;
}

lv_obj_t *create_cr_dropdown(lv_obj_t *card)
{
    static const char flag = 'c';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, RADIO_CR_LIST);
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_cr_args_list) {
        if (i == radio_params_copy.cr) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    ui_create_card_item(card, LV_SYMBOL_SETTINGS, "Coding Rate", dd);
    return dd;
}


lv_obj_t *create_sf_dropdown(lv_obj_t *card)
{
    static const char flag = 's';
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, RADIO_SF_LIST);
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);

    int index = 0;
    for (auto i : radio_sf_args_list) {
        if (i == radio_params_copy.sf) {
            lv_dropdown_set_selected(dd, index);
            break;
        }
        index++;
    }
    ui_create_card_item(card, LV_SYMBOL_SETTINGS, "Spread Factor", dd);
    return dd;
}


static void ui_set_msg_label(const char *msg)
{
    if (radio_msg_label) {
        if (strcmp(lv_label_get_text(radio_msg_label), msg) != 0) {
            lv_label_set_text(radio_msg_label, msg);
        }
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
                LILYGO_LOG_PRINTF("disable keyboard\n");
                disable_keyboard();
                const char *text = lv_textarea_get_text(ta);
                if (text) {
                    radio_params_copy.syncWord = atoi(text);
                    LILYGO_LOG_PRINTF("syncword -> %s - DEC:%d\n", text, radio_params_copy.syncWord);
                }
            }
        } else if (code == LV_EVENT_FOCUSED) {
            if (edited) {
                LILYGO_LOG_PRINTF("enable input keyboard \n");
                enable_keyboard();
            }
        }
    }
}

lv_obj_t *create_syncword_textarea(lv_obj_t *card)
{
    lv_obj_t *ta = lv_textarea_create(card);
    lv_textarea_set_text_selection(ta, false);
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_textarea_set_max_length(ta, 3);
    lv_textarea_set_placeholder_text(ta, "Dec (0-255)");
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_width(ta, 80);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_obj_set_style_pad_all(ta, 4, 0);
    if (is_screen_small()) {
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
    }

    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "%d", radio_params_copy.syncWord);
    lv_textarea_set_text(ta, buf);

#ifdef USING_TOUCHPAD
    /* Show soft keyboard on touch devices when textarea is clicked */
    static lv_obj_t *sync_kb = NULL;
    lv_obj_add_event_cb(ta, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
        if (code == LV_EVENT_CLICKED) {
            if (!sync_kb) {
                sync_kb = lv_keyboard_create(lv_screen_active());
                lv_obj_set_style_bg_color(sync_kb, UI_COLOR_CARD_BG, 0);
                lv_obj_set_style_bg_opa(sync_kb, LV_OPA_COVER, 0);
            }
            lv_keyboard_set_textarea(sync_kb, ta);
            lv_obj_remove_flag(sync_kb, LV_OBJ_FLAG_HIDDEN);
        } else if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
            if (sync_kb) {
                lv_keyboard_set_textarea(sync_kb, NULL);
                lv_obj_add_flag(sync_kb, LV_OBJ_FLAG_HIDDEN);
            }
            const char *text = lv_textarea_get_text(ta);
            if (text[0] != '\0') {
                radio_params_copy.syncWord = atoi(text);
            }
        }
    }, LV_EVENT_ALL, NULL);
#else
    lv_obj_add_event_cb(ta, _msg_ta_cb, LV_EVENT_ALL, NULL);
#endif

    ui_create_card_item(card, LV_SYMBOL_EDIT, "Sync Word", ta);
    return ta;
}

static void radio_timer_task(lv_timer_t *t)
{
    static radio_tx_params_t tx_params;
    static radio_rx_params_t rx_params;

    char msg[128];

    int tick = lv_tick_get() / 1000;
    switch (radio_run_mode) {
    case RADIO_DISABLE:

        break;
    case RADIO_TX:
        tx_params.data = reinterpret_cast<uint8_t *>(&dummy_tx_payload);
        tx_params.length = sizeof(dummy_tx_payload);
        hw_set_radio_tx(tx_params);
        if (tx_params.state == 0) {
            snprintf(msg, 128, "[%u]Tx PASS :%u", tick, dummy_tx_payload);
            ui_set_msg_label(msg);
            dummy_tx_payload++;
        }
        break;
    case RADIO_RX:
        dummy_tx_payload = 0;
        rx_params.data = reinterpret_cast<uint8_t *>(&dummy_rx_payload);
        rx_params.length = sizeof(dummy_rx_payload);
        hw_get_radio_rx(rx_params);
        if (rx_params.state == 0) {
            snprintf(msg, 128, "[%u]Rx PASS :%u/%d", tick, dummy_rx_payload, rx_params.rssi);
            ui_set_msg_label(msg);
        }
        break;
    case RADIO_CW:

        break;
    default:
        break;
    }
}

#ifdef HAS_USB_RF_SWITCH
static lv_obj_t *create_usb_rf_dropdown(lv_obj_t *card)
{
    static const char flag = 'u';
    lv_obj_t *dd = lv_dropdown_create(card);
    lv_dropdown_set_options(dd, "Built-in\nUSB-If");
    lv_obj_add_event_cb(dd, _ui_radio_obj_event, LV_EVENT_VALUE_CHANGED, (void *)&flag);
    lv_dropdown_set_selected(dd, 0);
    ui_create_card_item(card, LV_SYMBOL_USB, "RF Switch", dd);
    return dd;
}
#endif

void ui_radio_enter(lv_obj_t *parent)
{
    static const char flag = 'b';

    page_container = ui_create_app_page(parent, "Radio", back_event_handler);

    if (!(HW_RADIO_ONLINE & hw_get_device_online())) {
        lv_obj_t *card = ui_create_card(page_container, NULL);
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, "Radio module not detected!");
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_width(label, lv_pct(90));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        return;
    }

    hw_get_radio_params(radio_params_copy);

    /* ── State card ── */
    lv_obj_t *card = ui_create_card(page_container, "State");
    lv_obj_t *state_row = ui_create_card_info(card, LV_SYMBOL_WIFI, "Status", "DISABLE");
    radio_msg_label = lv_obj_get_child(state_row, lv_obj_get_child_count(state_row) - 1);

    /* ── Mode card ── */
    card = ui_create_card(page_container, "Mode");
    create_mode_dropdown(card);
#ifdef HAS_USB_RF_SWITCH
    create_usb_rf_dropdown(card);
#endif

    /* ── Parameters card ── */
    card = ui_create_card(page_container, "Parameters");
    create_frequency_dropdown(card);
    create_bandwidth_dropdown(card);
    create_tx_power_dropdown(card);
    create_tx_interval_dropdown(card);
    create_cr_dropdown(card);
    create_sf_dropdown(card);
    create_syncword_textarea(card);

    timer =  lv_timer_create(radio_timer_task, 1000, NULL);
    lv_timer_pause(timer);

    /* ── Action buttons — rectangular, left=Back, right=OK ── */
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
        hw_feedback();
        _do_cleanup();
    }, LV_EVENT_CLICKED, NULL);

    /* OK button — right */
    lv_obj_t *ok_btn = lv_btn_create(btn_row);
    lv_obj_set_size(ok_btn, LV_PCT(40), 36);
    lv_obj_set_style_bg_color(ok_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(ok_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ok_btn, 8, 0);
    lv_obj_set_style_border_width(ok_btn, 0, 0);
    ui_add_accent_focus_style(ok_btn);
    {
        lv_obj_t *l = lv_label_create(ok_btn);
        lv_label_set_text(l, LV_SYMBOL_OK " Start");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
    }
    lv_obj_add_event_cb(ok_btn, _ui_radio_obj_event, LV_EVENT_CLICKED, (void *)&flag);
}


void ui_radio_exit(lv_obj_t *parent)
{

}


app_t ui_radio_main = {
    .setup_func_cb = ui_radio_enter,
    .exit_func_cb = ui_radio_exit,
    .user_data = nullptr,
};

#endif
