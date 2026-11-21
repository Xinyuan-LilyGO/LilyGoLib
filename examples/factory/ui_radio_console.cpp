/**
 * @file      ui_radio_console.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-18
 *
 * Responsive packet consoles for the CC1101 and nRF24 Factory applications.
 */
#include "ui_define.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#if !defined(EXCLUDE_CC1101_TOOL) || !defined(EXCLUDE_NRF24)

namespace {

enum radio_console_kind_t : uint8_t {
    RADIO_CONSOLE_CC1101,
    RADIO_CONSOLE_NRF24,
};

enum radio_setting_t : uint8_t {
    SETTING_FREQUENCY,
    SETTING_BANDWIDTH,
    SETTING_POWER,
    SETTING_BIT_RATE,
    SETTING_MODULATION,
    SETTING_ADDRESS,
    SETTING_SEND_MODE,
    SETTING_SEND_INTERVAL,
};

static constexpr size_t CC1101_MAX_PAYLOAD = 255;
static constexpr size_t NRF24_MAX_PAYLOAD = 32;

static const uint16_t cc1101_bit_rates[] = {1, 5, 10, 38, 100};
static const uint8_t cc1101_powers[] = {0, 5, 7, 10};
static const float nrf24_frequencies[] = {2400.0f, 2424.0f, 2450.0f, 2500.0f, 2525.0f};
static const uint8_t nrf24_powers[] = {0, 1, 2, 3};
static const uint16_t nrf24_bit_rates[] = {1000, 2000, 250};
static const uint32_t send_intervals[] = {500, 1000, 2000, 3000, 5000};
static const uint8_t nrf24_addresses[][5] = {
    {0x01, 0x23, 0x45, 0x67, 0x89},
    {0xE7, 0xE7, 0xE7, 0xE7, 0xE7},
    {0xC2, 0xC2, 0xC2, 0xC2, 0xC2},
    {0xA5, 0xA5, 0xA5, 0xA5, 0xA5},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};

static lv_obj_t *page_container;
static lv_obj_t *data_view;
static lv_obj_t *settings_view;
static lv_obj_t *status_dot;
static lv_obj_t *status_label;
static lv_obj_t *summary_label;
static lv_obj_t *packet_title_label;
static lv_obj_t *packet_data_label;
static lv_obj_t *packet_meta_label;
static lv_obj_t *listen_button;
static lv_obj_t *listen_button_label;
static lv_obj_t *send_button;
static lv_obj_t *send_button_label;
static lv_obj_t *setup_button;
static lv_obj_t *settings_first_widget;
static lv_obj_t *setting_widgets[SETTING_SEND_INTERVAL + 1];
static lv_timer_t *radio_timer;
static lv_timer_t *pixel_timer;

static radio_console_kind_t console_kind;
static radio_params_t active_params;
static radio_params_t edit_params;
static uint8_t active_nrf24_address;
static uint8_t edit_nrf24_address;
static uint8_t packet_data[CC1101_MAX_PAYLOAD];
static size_t packet_length;
static bool listening;
static bool settings_open;
static bool nrf24_tx_pending;
static bool continuous_send;
static bool edit_continuous_send;
static bool periodic_sending;
static bool received_since_listen;
static uint32_t send_interval_ms;
static uint32_t edit_send_interval_ms;
static uint32_t last_send_ms;
static uint8_t configured_mode;
static uint32_t rx_count;
static uint32_t tx_count;
static int16_t last_rssi;

static void show_data_view();
static void stop_radio();
static void update_control_state();

static bool is_short_landscape()
{
    const int32_t width = lv_display_get_horizontal_resolution(nullptr);
    const int32_t height = lv_display_get_vertical_resolution(nullptr);
    return width > height && height <= 240;
}

static bool is_compact_display()
{
    return lv_display_get_horizontal_resolution(nullptr) <= 240 ||
           lv_display_get_vertical_resolution(nullptr) <= 180;
}

static uint8_t find_float_index(const float *values, size_t count, float value, float tolerance)
{
    for (size_t i = 0; i < count; ++i) {
        if (fabsf(values[i] - value) < tolerance) {
            return static_cast<uint8_t>(i);
        }
    }
    return 0;
}

template <typename T>
static uint8_t find_value_index(const T *values, size_t count, T value, uint8_t fallback = 0)
{
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) {
            return static_cast<uint8_t>(i);
        }
    }
    return fallback;
}

static uint8_t find_cc1101_frequency_index(float value)
{
    for (uint16_t i = 0; i < radio_get_freq_length(); ++i) {
        if (fabsf(radio_get_freq_from_index(i) - value) < 0.01f) {
            return static_cast<uint8_t>(i);
        }
    }
    return 0;
}

static uint8_t find_cc1101_bandwidth_index(float value)
{
    for (uint16_t i = 0; i < radio_get_bandwidth_length(); ++i) {
        if (fabsf(radio_get_bandwidth_from_index(i) - value) < 0.1f) {
            return static_cast<uint8_t>(i);
        }
    }
    return 3;
}

static void set_status(const char *text, lv_color_t color)
{
    if (status_label) {
        lv_label_set_text(status_label, text);
    }
    if (status_dot) {
        lv_obj_set_style_bg_color(status_dot, color, 0);
    }
}

static void pixel_off_timer_cb(lv_timer_t *)
{
#if defined(WS2812_NUM_LEDS)
    ui_t_embed_ws2812_clear_status_color();
#endif
    pixel_timer = nullptr;
}

static void flash_status_pixel(uint8_t red, uint8_t green, uint8_t blue)
{
#if defined(WS2812_NUM_LEDS)
    ui_t_embed_ws2812_set_status_color(red, green, blue);
    if (pixel_timer) {
        lv_timer_reset(pixel_timer);
        lv_timer_set_repeat_count(pixel_timer, 1);
    } else {
        pixel_timer = lv_timer_create(pixel_off_timer_cb, 140, nullptr);
        lv_timer_set_repeat_count(pixel_timer, 1);
    }
#else
    (void)red;
    (void)green;
    (void)blue;
#endif
}

static void set_button_disabled(lv_obj_t *button, bool disabled)
{
    if (!button) {
        return;
    }
    if (disabled) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
}

static void update_control_state()
{
    if (!listen_button || !send_button || !setup_button) {
        return;
    }

    const bool transmit_active = periodic_sending || nrf24_tx_pending;
    set_button_disabled(listen_button, transmit_active);
    set_button_disabled(send_button, listening || (nrf24_tx_pending && !periodic_sending));
    set_button_disabled(setup_button, transmit_active);

    lv_label_set_text(listen_button_label,
                      listening ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_DOWNLOAD " Listen");
    lv_obj_set_style_bg_color(listen_button,
                              listening ? lv_color_hex(0xC2413B) : UI_COLOR_CARD_BG, 0);

    lv_label_set_text(send_button_label,
                      periodic_sending ? LV_SYMBOL_STOP " Stop TX" :
                      (continuous_send ? LV_SYMBOL_UPLOAD " Start TX" : LV_SYMBOL_UPLOAD " Send"));
    lv_obj_set_style_bg_color(send_button,
                              periodic_sending ? lv_color_hex(0xC2413B) : UI_COLOR_CARD_BG, 0);
}

static void update_summary()
{
    if (!summary_label) {
        return;
    }

    if (console_kind == RADIO_CONSOLE_CC1101) {
        const float bit_rate = active_params.cr == 38 ? 38.4f :
                               (active_params.cr == 1 ? 1.2f : active_params.cr);
        lv_label_set_text_fmt(summary_label, "%.2f MHz  |  %s  |  %.1f kbps",
                              active_params.freq,
                              active_params.sf ? "OOK" : "2-FSK",
                              bit_rate);
    } else {
        const uint8_t *address = nrf24_addresses[active_nrf24_address];
        lv_label_set_text_fmt(summary_label, "%.0f MHz  |  %g Mbps  |  %02X %02X %02X %02X %02X",
                              active_params.freq,
                              active_params.cr / 1000.0f,
                              address[0], address[1], address[2], address[3], address[4]);
    }
}

static void update_packet_meta()
{
    if (!packet_meta_label) {
        return;
    }
    if (console_kind == RADIO_CONSOLE_CC1101 && rx_count > 0) {
        lv_label_set_text_fmt(packet_meta_label, "%u bytes  |  RX %lu  TX %lu  |  RSSI %d dBm",
                              static_cast<unsigned>(packet_length),
                              static_cast<unsigned long>(rx_count),
                              static_cast<unsigned long>(tx_count), last_rssi);
    } else {
        lv_label_set_text_fmt(packet_meta_label, "%u bytes  |  RX %lu  TX %lu",
                              static_cast<unsigned>(packet_length),
                              static_cast<unsigned long>(rx_count),
                              static_cast<unsigned long>(tx_count));
    }
}

static void show_packet(const char *title)
{
    static char display[CC1101_MAX_PAYLOAD * 5 + 64];
    size_t position = 0;

    if (packet_title_label) {
        lv_label_set_text(packet_title_label, title);
    }
    if (!packet_data_label) {
        return;
    }

    position += snprintf(display + position, sizeof(display) - position, "ASCII  ");
    for (size_t i = 0; i < packet_length && position + 2 < sizeof(display); ++i) {
        display[position++] = isprint(packet_data[i]) ? static_cast<char>(packet_data[i]) : '.';
    }
    position += snprintf(display + position, sizeof(display) - position, "\nHEX    ");
    for (size_t i = 0; i < packet_length && position + 4 < sizeof(display); ++i) {
        position += snprintf(display + position, sizeof(display) - position,
                             i ? " %02X" : "%02X", packet_data[i]);
    }
    display[sizeof(display) - 1] = '\0';
    lv_label_set_text(packet_data_label, display);
    update_packet_meta();
}

static void clear_packet_view()
{
    packet_length = 0;
    lv_label_set_text(packet_title_label, "Waiting for packet...");
    lv_label_set_text(packet_data_label, "");
    update_packet_meta();
}

static void seed_test_packet()
{
    const char *text = console_kind == RADIO_CONSOLE_CC1101
                       ? "LILYGO-CC1101-TEST"
                       : "LILYGO-NRF24-TEST";
    packet_length = strlen(text);
    memcpy(packet_data, text, packet_length);
    show_packet("TEST DATA - ready to send");
}

static int16_t configure_radio_mode(uint8_t mode, bool force = false)
{
    if (!force && configured_mode == mode) {
        return 0;
    }

    active_params.mode = mode;
    int16_t state = 0;
    if (console_kind == RADIO_CONSOLE_CC1101) {
#if !defined(EXCLUDE_CC1101_TOOL)
        state = hw_set_radio_params(active_params);
#endif
    } else {
#if !defined(EXCLUDE_NRF24)
        state = hw_set_nrf24_params(active_params);
#endif
    }
    if (state == 0) {
        configured_mode = mode;
    }
    return state;
}

static void restore_radio_mode()
{
    const uint8_t target_mode = listening ? RADIO_RX :
                                (periodic_sending ? RADIO_TX : RADIO_DISABLE);
    configure_radio_mode(target_mode);
    if (periodic_sending) {
        if (send_interval_ms % 1000 == 0) {
            lv_label_set_text_fmt(status_label, "TX every %lu s",
                                  static_cast<unsigned long>(send_interval_ms / 1000));
        } else {
            lv_label_set_text_fmt(status_label, "TX every %lu ms",
                                  static_cast<unsigned long>(send_interval_ms));
        }
        lv_obj_set_style_bg_color(status_dot, UI_COLOR_ACCENT, 0);
    } else {
        set_status(listening ? "Listening" : "Idle",
                   listening ? lv_color_hex(0x2FB344) : lv_color_hex(0x777777));
    }
    update_control_state();
}

static void show_send_error(int16_t error_code)
{
    char status[40];
    snprintf(status, sizeof(status), "Send failed (%d)", error_code);
    set_status(status, lv_color_hex(0xC2413B));
    flash_status_pixel(160, 0, 0);
    if (packet_title_label) {
        lv_label_set_text_fmt(packet_title_label, "TX failed - error %d", error_code);
    }
}

static void finish_nrf24_tx_if_ready()
{
#if !defined(EXCLUDE_NRF24)
    if (!nrf24_tx_pending) {
        return;
    }
    int16_t state = 0;
    if (!hw_get_nrf24_tx_done(state)) {
        return;
    }
    nrf24_tx_pending = false;
    if (state == 0) {
        ++tx_count;
        show_packet("TX DATA - sent");
        lv_label_set_text_fmt(packet_title_label, "TX #%lu - sent",
                              static_cast<unsigned long>(tx_count));
        flash_status_pixel(0, 0, 160);
    }
    restore_radio_mode();
    if (state != 0) {
        show_send_error(state);
    }
#endif
}

static void send_packet()
{
    if (!packet_length || nrf24_tx_pending) {
        return;
    }

    set_status("Sending", UI_COLOR_ACCENT);
    const int16_t config_state = configure_radio_mode(RADIO_TX);

    if (console_kind == RADIO_CONSOLE_CC1101) {
#if !defined(EXCLUDE_CC1101_TOOL)
        const bool sent = config_state == 0 && radio_transmit(packet_data, packet_length);
        if (sent) {
            ++tx_count;
            show_packet("TX DATA - sent");
            lv_label_set_text_fmt(packet_title_label, "TX #%lu - sent",
                                  static_cast<unsigned long>(tx_count));
            flash_status_pixel(0, 0, 160);
        }
        restore_radio_mode();
        if (!sent) {
            const int16_t error_code = config_state != 0
                                       ? config_state : radio_get_last_transmit_state();
            show_send_error(error_code);
        }
#endif
        return;
    }

#if !defined(EXCLUDE_NRF24)
    radio_tx_params_t request = {};
    request.data = packet_data;
    request.length = packet_length;
    const bool started = config_state == 0 && hw_set_nrf24_tx(request, false);
    if (!started) {
        restore_radio_mode();
        show_send_error(config_state != 0 ? config_state : request.state);
        return;
    }
    nrf24_tx_pending = true;
    update_control_state();
    finish_nrf24_tx_if_ready();
#endif
}

static void toggle_listening()
{
    if (nrf24_tx_pending || periodic_sending) {
        return;
    }
    listening = !listening;
    if (listening) {
        received_since_listen = false;
        clear_packet_view();
    } else if (!received_since_listen) {
        seed_test_packet();
    }
    restore_radio_mode();
}

static void receive_packet()
{
    uint8_t incoming[CC1101_MAX_PAYLOAD] = {};
    radio_rx_params_t request = {};
    request.data = incoming;
    request.length = console_kind == RADIO_CONSOLE_CC1101
                     ? CC1101_MAX_PAYLOAD : NRF24_MAX_PAYLOAD;

    if (console_kind == RADIO_CONSOLE_CC1101) {
#if !defined(EXCLUDE_CC1101_TOOL)
        hw_get_radio_rx(request);
#endif
    } else {
#if !defined(EXCLUDE_NRF24)
        hw_get_nrf24_rx(request);
#endif
    }

    if (request.state != 0 || request.length == 0) {
        return;
    }
    packet_length = request.length > sizeof(packet_data) ? sizeof(packet_data) : request.length;
    memcpy(packet_data, incoming, packet_length);
    last_rssi = request.rssi;
    ++rx_count;
    received_since_listen = true;
    show_packet("RX DATA - Send will replay it");
    lv_label_set_text_fmt(packet_title_label, "RX #%lu - Send will replay it",
                          static_cast<unsigned long>(rx_count));
    flash_status_pixel(0, 120, 24);
}

static void radio_timer_task(lv_timer_t *)
{
    if (nrf24_tx_pending) {
        finish_nrf24_tx_if_ready();
    } else if (listening) {
        receive_packet();
    } else if (periodic_sending && lv_tick_elaps(last_send_ms) >= send_interval_ms) {
        last_send_ms = lv_tick_get();
        send_packet();
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

static lv_obj_t *make_action_button(lv_obj_t *parent, const char *text,
                                    lv_event_cb_t callback, bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(button, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_color(button, primary ? UI_COLOR_ACCENT : UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_style(button, &ui_styles.focus_glow, LV_STATE_FOCUSED);
    lv_obj_add_style(button, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, is_compact_display()
                               ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    return button;
}

static void listen_event(lv_event_t *)
{
    toggle_listening();
}

static void send_event(lv_event_t *)
{
    if (listening) {
        return;
    }
    if (continuous_send) {
        if (periodic_sending) {
            periodic_sending = false;
            if (nrf24_tx_pending) {
                set_status("Stopping TX", UI_COLOR_WARNING);
                update_control_state();
            } else {
                restore_radio_mode();
            }
            return;
        }
        if (!packet_length) {
            seed_test_packet();
        }
        periodic_sending = true;
        last_send_ms = lv_tick_get();
        update_control_state();
        send_packet();
        return;
    }
    send_packet();
}

static void settings_event(lv_event_t *)
{
    if (periodic_sending || nrf24_tx_pending) {
        return;
    }
    edit_params = active_params;
    edit_nrf24_address = active_nrf24_address;
    edit_continuous_send = continuous_send;
    edit_send_interval_ms = send_interval_ms;
    lv_dropdown_set_selected(setting_widgets[SETTING_SEND_MODE], continuous_send ? 0 : 1);
    lv_dropdown_set_selected(setting_widgets[SETTING_SEND_INTERVAL],
                             find_value_index(send_intervals,
                                              sizeof(send_intervals) / sizeof(send_intervals[0]),
                                              send_interval_ms, 2));
    if (console_kind == RADIO_CONSOLE_CC1101) {
        lv_dropdown_set_selected(setting_widgets[SETTING_FREQUENCY],
                                 find_cc1101_frequency_index(active_params.freq));
        lv_dropdown_set_selected(setting_widgets[SETTING_MODULATION], active_params.sf ? 0 : 1);
        lv_dropdown_set_selected(setting_widgets[SETTING_BIT_RATE],
                                 find_value_index(cc1101_bit_rates,
                                                  sizeof(cc1101_bit_rates) / sizeof(cc1101_bit_rates[0]),
                                                  active_params.cr, 3));
        lv_dropdown_set_selected(setting_widgets[SETTING_BANDWIDTH],
                                 find_cc1101_bandwidth_index(active_params.bandwidth));
        lv_dropdown_set_selected(setting_widgets[SETTING_POWER],
                                 find_value_index(cc1101_powers,
                                                  sizeof(cc1101_powers) / sizeof(cc1101_powers[0]),
                                                  active_params.power, 3));
    } else {
        lv_dropdown_set_selected(setting_widgets[SETTING_FREQUENCY],
                                 find_float_index(nrf24_frequencies,
                                                  sizeof(nrf24_frequencies) / sizeof(nrf24_frequencies[0]),
                                                  active_params.freq, 0.1f));
        lv_dropdown_set_selected(setting_widgets[SETTING_BIT_RATE],
                                 find_value_index(nrf24_bit_rates,
                                                  sizeof(nrf24_bit_rates) / sizeof(nrf24_bit_rates[0]),
                                                  active_params.cr));
        lv_dropdown_set_selected(setting_widgets[SETTING_POWER],
                                 find_value_index(nrf24_powers,
                                                  sizeof(nrf24_powers) / sizeof(nrf24_powers[0]),
                                                  active_params.power, 3));
        lv_dropdown_set_selected(setting_widgets[SETTING_ADDRESS], active_nrf24_address);
    }
    settings_open = true;
    lv_obj_add_flag(data_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settings_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(settings_view, 0, LV_ANIM_OFF);
    if (settings_first_widget) {
        lv_group_focus_obj(settings_first_widget);
    }
}

static void setting_changed_event(lv_event_t *event)
{
    lv_obj_t *dropdown = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const radio_setting_t setting = static_cast<radio_setting_t>(
                                        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    const uint16_t selected = lv_dropdown_get_selected(dropdown);

    if (setting == SETTING_SEND_MODE) {
        edit_continuous_send = selected == 0;
        return;
    }
    if (setting == SETTING_SEND_INTERVAL) {
        edit_send_interval_ms = send_intervals[selected];
        return;
    }

    if (console_kind == RADIO_CONSOLE_CC1101) {
        switch (setting) {
        case SETTING_FREQUENCY:
            edit_params.freq = radio_get_freq_from_index(selected);
            break;
        case SETTING_BANDWIDTH:
            edit_params.bandwidth = radio_get_bandwidth_from_index(selected);
            break;
        case SETTING_POWER:
            edit_params.power = cc1101_powers[selected];
            break;
        case SETTING_BIT_RATE:
            edit_params.cr = cc1101_bit_rates[selected];
            break;
        case SETTING_MODULATION:
            edit_params.sf = selected == 0 ? 1 : 0;
            break;
        default:
            break;
        }
    } else {
        switch (setting) {
        case SETTING_FREQUENCY:
            edit_params.freq = nrf24_frequencies[selected];
            break;
        case SETTING_POWER:
            edit_params.power = nrf24_powers[selected];
            break;
        case SETTING_BIT_RATE:
            edit_params.cr = nrf24_bit_rates[selected];
            break;
        case SETTING_ADDRESS:
            edit_nrf24_address = selected;
            break;
        default:
            break;
        }
    }
}

static lv_obj_t *add_setting_dropdown(lv_obj_t *card, const char *icon, const char *title,
                                      const char *options, uint8_t selected,
                                      radio_setting_t setting)
{
    lv_obj_t *temporary_parent = lv_obj_create(card);
    lv_obj_t *dropdown = lv_dropdown_create(temporary_parent);
    lv_dropdown_set_options(dropdown, options);
    lv_dropdown_set_selected(dropdown, selected);
    lv_obj_add_event_cb(dropdown, setting_changed_event, LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(setting)));
    ui_create_card_item(card, icon, title, dropdown);
    setting_widgets[setting] = dropdown;
    if (!settings_first_widget) {
        settings_first_widget = dropdown;
    }
    return dropdown;
}

static void apply_settings()
{
    active_params = edit_params;
    continuous_send = edit_continuous_send;
    send_interval_ms = edit_send_interval_ms;
    const uint8_t target_mode = listening ? RADIO_RX : RADIO_DISABLE;

    if (console_kind == RADIO_CONSOLE_NRF24) {
#if !defined(EXCLUDE_NRF24)
        active_nrf24_address = edit_nrf24_address;
        hw_set_nrf24_address(nrf24_addresses[active_nrf24_address], 5);
#endif
    }
    const int16_t state = configure_radio_mode(target_mode, true);
    show_data_view();
    update_summary();
    update_control_state();
    set_status(state == 0 ? (listening ? "Listening" : "Settings applied") : "Settings failed",
               state == 0 ? (listening ? lv_color_hex(0x2FB344) : UI_COLOR_ACCENT)
               : lv_color_hex(0xC2413B));
}

static void show_data_view()
{
    settings_open = false;
    lv_obj_add_flag(settings_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(data_view, LV_OBJ_FLAG_HIDDEN);
    if (listen_button) {
        lv_group_focus_obj(listen_button);
    }
}

static void build_data_view()
{
    const bool short_landscape = is_short_landscape();
    const bool compact = is_compact_display();

    data_view = make_panel(page_container);
    lv_obj_set_width(data_view, LV_PCT(100));
    lv_obj_set_height(data_view, 0);
    lv_obj_set_flex_grow(data_view, 1);
    lv_obj_set_flex_flow(data_view, short_landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(data_view, compact ? 4 : 8, 0);
    lv_obj_remove_flag(data_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *information = make_panel(data_view);
    lv_obj_set_width(information, short_landscape ? 0 : LV_PCT(100));
    lv_obj_set_height(information, short_landscape ? LV_PCT(100) : 0);
    lv_obj_set_flex_grow(information, 1);
    lv_obj_set_flex_flow(information, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(information, compact ? 2 : 5, 0);
    lv_obj_remove_flag(information, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_row = make_panel(information);
    lv_obj_set_size(status_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 6, 0);
    lv_obj_remove_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);

    status_dot = lv_obj_create(status_row);
    lv_obj_set_size(status_dot, compact ? 8 : 10, compact ? 8 : 10);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_remove_flag(status_dot, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(status_row);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(status_label,
                               compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);

    summary_label = lv_label_create(status_row);
    lv_obj_set_width(summary_label, 0);
    lv_obj_set_flex_grow(summary_label, 1);
    lv_label_set_long_mode(summary_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(summary_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(summary_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(summary_label, &lv_font_montserrat_10, 0);

    lv_obj_t *packet_panel = lv_obj_create(information);
    lv_obj_set_width(packet_panel, LV_PCT(100));
    lv_obj_set_height(packet_panel, 0);
    lv_obj_set_flex_grow(packet_panel, 1);
    lv_obj_set_style_bg_color(packet_panel, lv_color_hex(0x101314), 0);
    lv_obj_set_style_bg_opa(packet_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(packet_panel, 1, 0);
    lv_obj_set_style_border_color(packet_panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(packet_panel, 5, 0);
    lv_obj_set_style_pad_all(packet_panel, compact ? 5 : 8, 0);
    lv_obj_set_style_pad_row(packet_panel, compact ? 3 : 6, 0);
    lv_obj_set_flex_flow(packet_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(packet_panel, LV_DIR_VER);

    packet_title_label = lv_label_create(packet_panel);
    lv_obj_set_width(packet_title_label, LV_PCT(100));
    lv_label_set_long_mode(packet_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(packet_title_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(packet_title_label, &lv_font_montserrat_12, 0);

    packet_data_label = lv_label_create(packet_panel);
    lv_obj_set_width(packet_data_label, LV_PCT(100));
    lv_label_set_long_mode(packet_data_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(packet_data_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(packet_data_label,
                               compact ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);

    packet_meta_label = lv_label_create(information);
    lv_obj_set_width(packet_meta_label, LV_PCT(100));
    lv_label_set_long_mode(packet_meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(packet_meta_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(packet_meta_label, &lv_font_montserrat_10, 0);

    lv_obj_t *controls = make_panel(data_view);
    if (short_landscape) {
        lv_obj_set_size(controls, compact ? 86 : 106, LV_PCT(100));
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_size(controls, LV_PCT(100), compact ? 38 : 46);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    }
    lv_obj_set_style_pad_gap(controls, compact ? 4 : 8, 0);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    listen_button = make_action_button(controls, LV_SYMBOL_DOWNLOAD " Listen", listen_event, false);
    listen_button_label = lv_obj_get_child(listen_button, 0);
    send_button = make_action_button(controls, LV_SYMBOL_UPLOAD " Start TX", send_event, false);
    send_button_label = lv_obj_get_child(send_button, 0);
    setup_button = make_action_button(controls, LV_SYMBOL_SETTINGS " Setup", settings_event, false);

    lv_obj_t *buttons[] = {listen_button, send_button, setup_button};
    for (lv_obj_t *button : buttons) {
        if (short_landscape) {
            lv_obj_set_size(button, LV_PCT(100), 0);
        } else {
            lv_obj_set_size(button, 0, LV_PCT(100));
        }
        lv_obj_set_flex_grow(button, 1);
    }
}

static void build_cc1101_settings(lv_obj_t *card)
{
    add_setting_dropdown(card, LV_SYMBOL_WIFI, "Frequency", radio_get_freq_list(),
                         find_cc1101_frequency_index(active_params.freq), SETTING_FREQUENCY);
    add_setting_dropdown(card, LV_SYMBOL_SETTINGS, "Modulation", "OOK\n2-FSK",
                         active_params.sf ? 0 : 1, SETTING_MODULATION);
    add_setting_dropdown(card, LV_SYMBOL_SETTINGS, "Bit rate",
                         "1.2 kbps\n5 kbps\n10 kbps\n38.4 kbps\n100 kbps",
                         find_value_index(cc1101_bit_rates, sizeof(cc1101_bit_rates) / sizeof(cc1101_bit_rates[0]),
                                          active_params.cr, 3),
                         SETTING_BIT_RATE);
    add_setting_dropdown(card, LV_SYMBOL_SETTINGS, "RX bandwidth", radio_get_bandwidth_list(false),
                         find_cc1101_bandwidth_index(active_params.bandwidth), SETTING_BANDWIDTH);
    add_setting_dropdown(card, LV_SYMBOL_WIFI, "TX power", "0 dBm\n5 dBm\n7 dBm\n10 dBm",
                         find_value_index(cc1101_powers, sizeof(cc1101_powers) / sizeof(cc1101_powers[0]),
                                          active_params.power, 3),
                         SETTING_POWER);
}

static void build_nrf24_settings(lv_obj_t *card)
{
    add_setting_dropdown(card, LV_SYMBOL_WIFI, "Frequency",
                         "2400 MHz\n2424 MHz\n2450 MHz\n2500 MHz\n2525 MHz",
                         find_float_index(nrf24_frequencies,
                                          sizeof(nrf24_frequencies) / sizeof(nrf24_frequencies[0]),
                                          active_params.freq, 0.1f),
                         SETTING_FREQUENCY);
    add_setting_dropdown(card, LV_SYMBOL_SETTINGS, "Data rate",
                         "1 Mbps\n2 Mbps\n250 kbps",
                         find_value_index(nrf24_bit_rates,
                                          sizeof(nrf24_bit_rates) / sizeof(nrf24_bit_rates[0]),
                                          active_params.cr),
                         SETTING_BIT_RATE);
    add_setting_dropdown(card, LV_SYMBOL_WIFI, "TX power",
                         "-18 dBm\n-12 dBm\n-6 dBm\n0 dBm",
                         find_value_index(nrf24_powers,
                                          sizeof(nrf24_powers) / sizeof(nrf24_powers[0]),
                                          active_params.power, 3),
                         SETTING_POWER);
    add_setting_dropdown(card, LV_SYMBOL_LIST, "Pipe address",
                         "01 23 45 67 89\nE7 E7 E7 E7 E7\nC2 C2 C2 C2 C2\nA5 A5 A5 A5 A5\nFF FF FF FF FF",
                         active_nrf24_address, SETTING_ADDRESS);
}

static void build_settings_view()
{
    settings_first_widget = nullptr;
    memset(setting_widgets, 0, sizeof(setting_widgets));
    settings_view = make_panel(page_container);
    lv_obj_set_width(settings_view, LV_PCT(100));
    lv_obj_set_height(settings_view, 0);
    lv_obj_set_flex_grow(settings_view, 1);
    lv_obj_set_flex_flow(settings_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_view, 8, 0);
    lv_obj_set_scroll_dir(settings_view, LV_DIR_VER);
    lv_obj_add_flag(settings_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = ui_create_card(settings_view, "Transmit");
    add_setting_dropdown(card, LV_SYMBOL_UPLOAD, "Send mode",
                         "Continuous\nManual", continuous_send ? 0 : 1,
                         SETTING_SEND_MODE);
    add_setting_dropdown(card, LV_SYMBOL_LOOP, "Interval",
                         "500 ms\n1 second\n2 seconds\n3 seconds\n5 seconds",
                         find_value_index(send_intervals,
                                          sizeof(send_intervals) / sizeof(send_intervals[0]),
                                          send_interval_ms, 2),
                         SETTING_SEND_INTERVAL);

    card = ui_create_card(settings_view,
                                    console_kind == RADIO_CONSOLE_CC1101
                                    ? "CC1101 Radio Settings" : "NRF24 Radio Settings");
    if (console_kind == RADIO_CONSOLE_CC1101) {
        build_cc1101_settings(card);
    } else {
        build_nrf24_settings(card);
    }

}

static void stop_radio()
{
    listening = false;
    periodic_sending = false;
    nrf24_tx_pending = false;
    configure_radio_mode(RADIO_DISABLE);
}

static void back_event(lv_event_t *)
{
    if (settings_open) {
        apply_settings();
        return;
    }
    if (radio_timer) {
        lv_timer_delete(radio_timer);
        radio_timer = nullptr;
    }
    if (pixel_timer) {
        lv_timer_delete(pixel_timer);
        pixel_timer = nullptr;
    }
#if defined(WS2812_NUM_LEDS)
    ui_t_embed_ws2812_clear_status_color();
#endif
    stop_radio();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    data_view = nullptr;
    settings_view = nullptr;
    menu_show();
}

static void load_nrf24_address()
{
#if !defined(EXCLUDE_NRF24)
    uint8_t current[5] = {};
    hw_get_nrf24_address(current, sizeof(current));
    active_nrf24_address = 0;
    for (size_t i = 0; i < sizeof(nrf24_addresses) / sizeof(nrf24_addresses[0]); ++i) {
        if (memcmp(current, nrf24_addresses[i], sizeof(current)) == 0) {
            active_nrf24_address = static_cast<uint8_t>(i);
            return;
        }
    }
    hw_set_nrf24_address(nrf24_addresses[0], sizeof(nrf24_addresses[0]));
#endif
}

static void enter_console(lv_obj_t *parent, radio_console_kind_t kind)
{
    console_kind = kind;
    listening = false;
    settings_open = false;
    nrf24_tx_pending = false;
    continuous_send = true;
    periodic_sending = false;
    received_since_listen = false;
    send_interval_ms = 2000;
    last_send_ms = 0;
    configured_mode = 0xFF;
    pixel_timer = nullptr;
    rx_count = 0;
    tx_count = 0;
    last_rssi = 0;

    if (console_kind == RADIO_CONSOLE_CC1101) {
#if !defined(EXCLUDE_CC1101_TOOL)
        hw_get_radio_params(active_params);
        if (!active_params.cr) {
            active_params.cr = 38;
        }
#endif
    } else {
#if !defined(EXCLUDE_NRF24)
        hw_get_nrf24_params(active_params);
        load_nrf24_address();
#endif
    }
    active_params.mode = RADIO_DISABLE;

    page_container = ui_create_app_page(parent,
                                        console_kind == RADIO_CONSOLE_CC1101
                                        ? "CC1101 Lab" : "NRF24 Lab",
                                        back_event);
    lv_obj_set_style_pad_all(page_container, is_compact_display() ? 4 : 8, 0);
    lv_obj_set_style_pad_gap(page_container, is_compact_display() ? 4 : 8, 0);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);

    build_data_view();
    build_settings_view();
    update_summary();
    seed_test_packet();
    restore_radio_mode();

    radio_timer = lv_timer_create(radio_timer_task, 80, nullptr);
    if (listen_button) {
        lv_group_focus_obj(listen_button);
    }
}

} // namespace

#if !defined(EXCLUDE_CC1101_TOOL)
static void ui_cc1101_console_enter(lv_obj_t *parent)
{
    enter_console(parent, RADIO_CONSOLE_CC1101);
}

static void ui_cc1101_console_exit(lv_obj_t *)
{
}

app_t ui_cc1101_console_main = {
    .setup_func_cb = ui_cc1101_console_enter,
    .exit_func_cb = ui_cc1101_console_exit,
    .user_data = nullptr,
};
#endif

#if !defined(EXCLUDE_NRF24)
static void ui_nrf24_console_enter(lv_obj_t *parent)
{
    if (!hw_has_nrf24()) {
        menu_show();
        return;
    }
    enter_console(parent, RADIO_CONSOLE_NRF24);
}

static void ui_nrf24_console_exit(lv_obj_t *)
{
}

app_t ui_nrf24_console_main = {
    .setup_func_cb = ui_nrf24_console_enter,
    .exit_func_cb = ui_nrf24_console_exit,
    .user_data = nullptr,
};
#endif

#endif
