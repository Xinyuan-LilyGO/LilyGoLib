/**
 * @file      ui_wifi_tools.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-21
 * 
 * WiFi Analyzer: RF environment overview, channel overlap graph and AP details.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_WIFI_TOOLS)

#ifdef ARDUINO
#include <WiFiClient.h>
#endif

static lv_obj_t *page_container = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *ssid_label = NULL;
static lv_obj_t *ip_label = NULL;
static lv_obj_t *gateway_label = NULL;
static lv_obj_t *dns_label = NULL;
static lv_obj_t *mac_label = NULL;
static lv_obj_t *rssi_label = NULL;
static lv_obj_t *target_ta = NULL;
static lv_obj_t *port_dd = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *keyboard = NULL;

#if defined(USING_TOUCHPAD) || defined(USING_INPUT_DEV_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
#define WIFI_TOOLS_HAS_SOFT_KEYBOARD 1
#else
#define WIFI_TOOLS_HAS_SOFT_KEYBOARD 0
#endif

static uint16_t port_from_dropdown(void)
{
    static const uint16_t ports[] = {80, 443, 1883, 8080};
    uint8_t sel = lv_dropdown_get_selected(port_dd);
    if (sel >= sizeof(ports) / sizeof(ports[0])) return 80;
    return ports[sel];
}

static void set_result(const char *text, lv_color_t color)
{
    if (!result_label) return;
    lv_label_set_text(result_label, text);
    lv_obj_set_style_text_color(result_label, color, 0);
}

static void refresh_info(void)
{
#ifdef ARDUINO
    bool connected = WiFi.isConnected();
    lv_label_set_text(status_label, connected ? "Connected" : "Disconnected");
    lv_obj_set_style_text_color(status_label, connected ? UI_COLOR_ACCENT : lv_color_hex(0xFF4444), 0);

    lv_label_set_text(ssid_label, connected ? WiFi.SSID().c_str() : "--");
    lv_label_set_text(ip_label, connected ? WiFi.localIP().toString().c_str() : "--");
    lv_label_set_text(gateway_label, connected ? WiFi.gatewayIP().toString().c_str() : "--");
    lv_label_set_text(dns_label, connected ? WiFi.dnsIP(0).toString().c_str() : "--");
    lv_label_set_text(mac_label, WiFi.macAddress().c_str());
    if (connected) {
        lv_label_set_text_fmt(rssi_label, "%d dBm", WiFi.RSSI());
    } else {
        lv_label_set_text(rssi_label, "--");
    }
#else
    lv_label_set_text(status_label, "Unavailable");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4444), 0);
#endif
}

static void refresh_cb(lv_event_t *e)
{
    refresh_info();
    set_result("Info refreshed.", UI_COLOR_TEXT_SECONDARY);
}

static const char *target_text(void)
{
    const char *target = lv_textarea_get_text(target_ta);
    return target && target[0] ? target : "example.com";
}

static void dns_cb(lv_event_t *e)
{
#ifdef ARDUINO
    if (!WiFi.isConnected()) {
        set_result("WiFi is disconnected.", lv_color_hex(0xFF4444));
        refresh_info();
        return;
    }

    IPAddress ip;
    const char *host = target_text();
    uint32_t start = millis();
    bool ok = WiFi.hostByName(host, ip);
    uint32_t elapsed = millis() - start;
    if (ok) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s -> %s (%lums)",
                 host, ip.toString().c_str(), (unsigned long)elapsed);
        set_result(buf, UI_COLOR_ACCENT);
    } else {
        set_result("DNS lookup failed.", lv_color_hex(0xFF4444));
    }
#endif
}

static void tcp_cb(lv_event_t *e)
{
#ifdef ARDUINO
    if (!WiFi.isConnected()) {
        set_result("WiFi is disconnected.", lv_color_hex(0xFF4444));
        refresh_info();
        return;
    }

    const char *host = target_text();
    uint16_t port = port_from_dropdown();
    WiFiClient client;
    client.setTimeout(1500);
    uint32_t start = millis();
    bool ok = client.connect(host, port);
    uint32_t elapsed = millis() - start;
    client.stop();

    char buf[112];
    if (ok) {
        snprintf(buf, sizeof(buf), "TCP %s:%u OK (%lums)",
                 host, port, (unsigned long)elapsed);
        set_result(buf, UI_COLOR_ACCENT);
    } else {
        snprintf(buf, sizeof(buf), "TCP %s:%u failed", host, port);
        set_result(buf, lv_color_hex(0xFF4444));
    }
#endif
}

static void show_input_keyboard(lv_obj_t *ta)
{
    if (!ta) return;

    enable_keyboard();
#if WIFI_TOOLS_HAS_SOFT_KEYBOARD
    if (keyboard) {
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
#else
    (void)keyboard;
#endif
}

static void hide_input_keyboard(lv_obj_t *ta)
{
    lv_group_t *group = ta ? (lv_group_t *)lv_obj_get_group(ta) : NULL;
    if (group) {
        lv_group_set_editing(group, false);
    }
#if WIFI_TOOLS_HAS_SOFT_KEYBOARD
    if (keyboard) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
#else
    (void)keyboard;
#endif
    disable_keyboard();
}

static lv_indev_type_t target_event_indev_type(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        indev = lv_indev_active();
    }
    return indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
}

static void target_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target_obj(e);

    if (code == LV_EVENT_CLICKED && target_event_indev_type(e) == LV_INDEV_TYPE_POINTER) {
        hide_input_keyboard(ta);
        show_input_keyboard(ta);
    } else if (code == LV_EVENT_FOCUSED) {
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED) {
        hide_input_keyboard(ta);
#if WIFI_TOOLS_HAS_SOFT_KEYBOARD
    } else if (code == LV_EVENT_DELETE && keyboard) {
        lv_keyboard_set_textarea(keyboard, NULL);
#endif
    }
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    return row;
}

static lv_obj_t *card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                               const char *options, uint8_t sel)
{
    lv_obj_t *row = ui_create_card_dropdown(card, icon, title, options, sel, NULL);
    lv_obj_t *dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    lv_obj_set_width(dd, 110);
    return dd;
}

static void back_event_handler(lv_event_t *e)
{
    hide_input_keyboard(target_ta);
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    status_label = NULL;
    ssid_label = NULL;
    ip_label = NULL;
    gateway_label = NULL;
    dns_label = NULL;
    mac_label = NULL;
    rssi_label = NULL;
    target_ta = NULL;
    port_dd = NULL;
    result_label = NULL;
    menu_show();
}

void ui_wifi_tools_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "WiFi Tools", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Link");
    add_info_row(card, LV_SYMBOL_WIFI, "Status", "--", &status_label);
    add_info_row(card, LV_SYMBOL_LIST, "SSID", "--", &ssid_label);
    add_info_row(card, LV_SYMBOL_HOME, "IP", "--", &ip_label);
    add_info_row(card, LV_SYMBOL_HOME, "Gateway", "--", &gateway_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "DNS", "--", &dns_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "MAC", "--", &mac_label);
    add_info_row(card, LV_SYMBOL_WIFI, "RSSI", "--", &rssi_label);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Update", "Refresh", refresh_cb);

    card = ui_create_card(page_container, "Diagnostics");
    target_ta = lv_textarea_create(card);
    lv_obj_set_width(target_ta, 150);
    lv_obj_set_height(target_ta, 34);
    lv_textarea_set_one_line(target_ta, true);
    lv_textarea_set_text_selection(target_ta, false);
    lv_textarea_set_cursor_click_pos(target_ta, false);
    lv_textarea_set_max_length(target_ta, 64);
    lv_textarea_set_text(target_ta, "example.com");
    lv_obj_set_scrollbar_mode(target_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(target_ta, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(target_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(target_ta, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_color(target_ta, lv_color_white(), LV_PART_CURSOR);
#if WIFI_TOOLS_HAS_SOFT_KEYBOARD
    lv_obj_set_style_bg_opa(target_ta, LV_OPA_COVER, LV_PART_CURSOR);
#else
    lv_obj_set_style_bg_opa(target_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(target_ta, LV_OPA_COVER,
                            static_cast<lv_style_selector_t>(LV_PART_CURSOR) |
                            static_cast<lv_style_selector_t>(LV_STATE_EDITED));
#endif
    lv_obj_set_style_border_color(target_ta, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_color(target_ta, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(target_ta, 1, 0);
    lv_obj_set_style_radius(target_ta, 6, 0);
    lv_obj_add_event_cb(target_ta, target_ta_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(target_ta, target_ta_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(target_ta, target_ta_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(target_ta, target_ta_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(target_ta, target_ta_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(target_ta, target_ta_cb, LV_EVENT_DELETE, NULL);
    ui_create_card_item(card, LV_SYMBOL_EDIT, "Host", target_ta);

    port_dd = card_dropdown(card, LV_SYMBOL_SETTINGS, "Port", "80\n443\n1883\n8080", 0);
    ui_create_card_button(card, LV_SYMBOL_WIFI, "DNS", "Lookup", dns_cb);
    ui_create_card_button(card, LV_SYMBOL_UPLOAD, "TCP", "Connect", tcp_cb);

    result_label = lv_label_create(card);
    lv_label_set_text(result_label, "Ready.");
    lv_obj_set_style_text_color(result_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(result_label, LV_PCT(100));

#if WIFI_TOOLS_HAS_SOFT_KEYBOARD
    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif

    refresh_info();
}

void ui_wifi_tools_exit(lv_obj_t *parent)
{
}

app_t ui_wifi_tools_main = {
    .setup_func_cb = ui_wifi_tools_enter,
    .exit_func_cb = ui_wifi_tools_exit,
    .user_data = nullptr,
};

#endif
