/**
 * @file      ui_wireless.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-02
 * 
 */
#include "ui_define.h"

#include <algorithm>

#define WIFI_SCAN_POLL_MS   200
#define WIFI_STATUS_MS      1000
#define WIFI_MAX_AP_ROWS    14

static lv_obj_t *page_container = NULL;
static lv_timer_t *scan_timer = NULL;
static lv_timer_t *status_timer = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *ssid_label = NULL;
static lv_obj_t *ip_label = NULL;
static lv_obj_t *rssi_label = NULL;
static lv_obj_t *mac_label = NULL;
static lv_obj_t *scan_summary_label = NULL;
static lv_obj_t *ap_list_card = NULL;
static lv_obj_t *ap_list_cont = NULL;
static lv_obj_t *wifi_switch = NULL;
static lv_obj_t *connect_overlay = NULL;
static lv_obj_t *connect_panel = NULL;
static lv_obj_t *connect_password_ta = NULL;
static lv_obj_t *connect_detail_label = NULL;
static lv_obj_t *connect_show_switch = NULL;
static lv_obj_t *wifi_connecting_overlay = NULL;
static lv_obj_t *wifi_connecting_panel = NULL;
static lv_timer_t *wifi_connecting_timer = NULL;
static uint32_t wifi_connecting_deadline = 0;
static lv_group_t *connect_prev_group = NULL;
static lv_group_t *connect_modal_group = NULL;

#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;
#endif
static ui_soft_keyboard_lift_t connect_keyboard_lift = {NULL, NULL, -1};

static vector<wifi_scan_params_t> scan_results;
static char wifi_ssid[64] = {0};
static char wifi_password[128] = {0};
static int selected_index = -1;
static bool scanning = false;
static bool wifi_enabled = false;
static bool password_editing = false;

static void connect_modal_add_group_obj(lv_obj_t *obj)
{
    if (connect_modal_group && obj && lv_obj_get_group(obj) != connect_modal_group) {
        lv_group_add_obj(connect_modal_group, obj);
    }
}

static void begin_connect_modal_group(void)
{
    connect_prev_group = lv_group_get_default();
    connect_modal_group = lv_group_create();
    if (connect_modal_group) {
        set_default_group(connect_modal_group);
    }
}

static void restore_connect_modal_group(void)
{
    if (connect_modal_group) {
        if (connect_prev_group) {
            set_default_group(connect_prev_group);
        }
        lv_group_delete(connect_modal_group);
    }
    connect_modal_group = NULL;
    connect_prev_group = NULL;
}

static int rssi_to_percent(int8_t rssi)
{
    if (rssi >= -30) return 100;
    if (rssi <= -90) return 0;
    return (int)((rssi + 90) * 100 / 60);
}

static lv_color_t rssi_color(int8_t rssi)
{
    if (rssi >= -55) return lv_color_hex(0x00D4AA);
    if (rssi >= -70) return lv_color_hex(0xFFB800);
    if (rssi >= -82) return lv_color_hex(0xFF6B35);
    return lv_color_hex(0x777777);
}

static const char *auth_mode_str(uint8_t mode)
{
    switch (mode) {
    case 0: return "Open";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/WPA2";
    case 5: return "WPA2 Ent";
    case 6: return "WPA3";
    case 7: return "WPA2/WPA3";
    case 8: return "WAPI";
    default: return "Unknown";
    }
}

static bool auth_is_open(uint8_t mode)
{
    return mode == 0;
}

static const char *status_to_text(wl_status_t status)
{
    switch (status) {
    case WL_CONNECTED: return "Connected";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_CONNECT_FAILED: return "Connect failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED: return "Disconnected";
    case WL_IDLE_STATUS: return "Idle";
    default: return "Offline";
    }
}

static const char *ssid_display(const wifi_scan_params_t &ap)
{
    return ap.ssid[0] ? ap.ssid : "<hidden>";
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static bool known_password_for_ssid(const char *ssid, char *out, size_t out_len)
{
    if (!ssid || !out || out_len == 0) return false;
    out[0] = '\0';
#ifdef WIFI_SSID
    if (strcmp(ssid, WIFI_SSID) == 0) {
#ifdef WIFI_PASSWORD
        snprintf(out, out_len, "%s", WIFI_PASSWORD);
        return true;
#endif
    }
#endif
#ifdef WIFI_SSID2
    if (strcmp(ssid, WIFI_SSID2) == 0) {
#ifdef WIFI_PASSWORD2
        snprintf(out, out_len, "%s", WIFI_PASSWORD2);
        return true;
#endif
    }
#endif
    return false;
}

static void hide_keyboard(void)
{
#ifdef USING_TOUCHPAD
    ui_soft_keyboard_hide(keyboard, &connect_keyboard_lift);
#else
    ui_soft_keyboard_restore(&connect_keyboard_lift);
#endif
    disable_keyboard();
}

static void update_connection_labels(void)
{
    char ssid[WIFI_SSID_MAX_LEN];
    char ip[24];
    hw_get_wifi_ssid(ssid, sizeof(ssid));
    hw_get_ip_address(ip, sizeof(ip));

    wl_status_t wifi_status = hw_get_wifi_status();
    bool connected = wifi_status == WL_CONNECTED;

    if (status_label) {
        lv_label_set_text(status_label, status_to_text(wifi_status));
        lv_obj_set_style_text_color(status_label, connected ? UI_COLOR_ACCENT : lv_color_hex(0xFF6B35), 0);
    }
    if (ssid_label) lv_label_set_text(ssid_label, connected ? ssid : "--");
    if (ip_label) lv_label_set_text(ip_label, connected ? ip : "--");
    if (rssi_label) {
        if (connected) {
            int8_t rssi = hw_get_wifi_rssi();
            lv_label_set_text_fmt(rssi_label, "%d dBm", rssi);
            lv_obj_set_style_text_color(rssi_label, rssi_color(rssi), 0);
        } else {
            lv_label_set_text(rssi_label, "--");
            lv_obj_set_style_text_color(rssi_label, UI_COLOR_TEXT_SECONDARY, 0);
        }
    }
#ifdef ARDUINO
    if (mac_label) lv_label_set_text(mac_label, WiFi.macAddress().c_str());
#else
    if (mac_label) lv_label_set_text(mac_label, "--");
#endif
}

static void status_timer_cb(lv_timer_t *t)
{
    update_connection_labels();
}

static void clear_ap_list(void)
{
    if (ap_list_cont) lv_obj_clean(ap_list_cont);
}

static void add_empty_row(const char *text)
{
    if (!ap_list_cont) return;
    lv_obj_t *label = lv_label_create(ap_list_cont);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

static void close_connect_modal(void)
{
    if (connect_password_ta) {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(connect_password_ta);
        if (group) {
            lv_group_set_editing(group, false);
        }
    }
    hide_keyboard();
    password_editing = false;
    if (connect_overlay) {
        lv_obj_delete(connect_overlay);
        connect_overlay = NULL;
    }
    restore_connect_modal_group();
    connect_panel = NULL;
    connect_password_ta = NULL;
    connect_detail_label = NULL;
    connect_show_switch = NULL;
}

static void close_connecting_overlay(void)
{
    if (wifi_connecting_timer) {
        lv_timer_del(wifi_connecting_timer);
        wifi_connecting_timer = NULL;
    }
    if (wifi_connecting_overlay) {
        lv_obj_delete(wifi_connecting_overlay);
        wifi_connecting_overlay = NULL;
    }
    wifi_connecting_panel = NULL;
    wifi_connecting_deadline = 0;
}

static void focus_password_textarea(lv_obj_t *ta)
{
    if (!ta) return;
    lv_group_t *group = (lv_group_t *)lv_obj_get_group(ta);
    if (group) {
        if (lv_group_get_focused(group) != ta) {
            lv_group_focus_obj(ta);
        }
        lv_group_set_editing(group, true);
    }
    password_editing = true;
    enable_keyboard();
#ifdef USING_TOUCHPAD
    if (keyboard && ui_soft_keyboard_should_open()) {
        ui_soft_keyboard_show(keyboard, ta, lv_obj_get_parent(ta), &connect_keyboard_lift);
    }
#endif
}

static void leave_password_textarea(lv_obj_t *ta)
{
    lv_group_t *group = ta ? (lv_group_t *)lv_obj_get_group(ta) : NULL;
    if (group) {
        lv_group_set_editing(group, false);
    }
    password_editing = false;
    hide_keyboard();
    if (connect_panel && lv_obj_is_valid(connect_panel) &&
            lv_obj_get_parent(connect_panel) == connect_overlay) {
        lv_obj_center(connect_panel);
    }
}

static void wifi_result_close_timer_cb(lv_timer_t *t)
{
    if (wifi_connecting_timer == t) {
        wifi_connecting_timer = NULL;
    }
    close_connecting_overlay();
}

static void wifi_result_anim_exec_cb(void *var, int32_t val)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)var, val, 0);
}

static void show_connecting_result(bool success, const char *detail)
{
    if (!wifi_connecting_panel) return;

    if (wifi_connecting_timer) {
        lv_timer_del(wifi_connecting_timer);
        wifi_connecting_timer = NULL;
    }

    lv_obj_clean(wifi_connecting_panel);

    lv_color_t color = success ? UI_COLOR_ACCENT : lv_color_hex(0xFF445A);
    lv_obj_t *badge = lv_obj_create(wifi_connecting_panel);
    lv_obj_set_size(badge, 50, 50);
    lv_obj_set_style_bg_color(badge, color, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(badge, 14, 0);
    lv_obj_set_style_shadow_opa(badge, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(badge, color, 0);
    lv_obj_set_style_transform_pivot_x(badge, 25, 0);
    lv_obj_set_style_transform_pivot_y(badge, 25, 0);
    lv_obj_set_style_transform_scale(badge, 128, 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(badge);
    lv_label_set_text(icon, success ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_center(icon);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, badge);
    lv_anim_set_values(&anim, 128, 256);
    lv_anim_set_duration(&anim, 180);
    lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&anim, wifi_result_anim_exec_cb);
    lv_anim_start(&anim);

    lv_obj_t *title = lv_label_create(wifi_connecting_panel);
    lv_label_set_text(title, success ? "Connected" : "Connect failed");
    lv_obj_set_style_text_color(title, color, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    lv_obj_t *label = lv_label_create(wifi_connecting_panel);
    lv_label_set_text(label, detail && detail[0] ? detail : (success ? "WiFi is ready" : "Unknown error"));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);

    wifi_connecting_deadline = 0;
    wifi_connecting_timer = lv_timer_create(wifi_result_close_timer_cb, success ? 1100 : 2600, NULL);
    lv_timer_set_repeat_count(wifi_connecting_timer, 1);
}

static void wifi_connecting_timer_cb(lv_timer_t *t)
{
    wl_status_t status = hw_get_wifi_status();
    if (status == WL_CONNECTED) {
        set_status("Connected", UI_COLOR_ACCENT);
        update_connection_labels();
        show_connecting_result(true, wifi_ssid);
        return;
    }

    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED || status == WL_CONNECTION_LOST) {
        set_status(status_to_text(status), lv_color_hex(0xFF6B35));
        update_connection_labels();
        show_connecting_result(false, status_to_text(status));
        return;
    }

    if (wifi_connecting_deadline != 0 && lv_tick_get() > wifi_connecting_deadline) {
        set_status("Connect timeout", lv_color_hex(0xFF6B35));
        update_connection_labels();
        show_connecting_result(false, "Connect timeout");
    }
}

static void show_connecting_overlay(const char *ssid)
{
    close_connecting_overlay();

    wifi_connecting_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(wifi_connecting_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_connecting_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wifi_connecting_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(wifi_connecting_overlay, 0, 0);
    lv_obj_set_style_pad_all(wifi_connecting_overlay, 0, 0);
    lv_obj_add_flag(wifi_connecting_overlay, LV_OBJ_FLAG_CLICKABLE);

    wifi_connecting_panel = lv_obj_create(wifi_connecting_overlay);
    lv_obj_set_size(wifi_connecting_panel, LV_PCT(76), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(wifi_connecting_panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(wifi_connecting_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(wifi_connecting_panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(wifi_connecting_panel, 1, 0);
    lv_obj_set_style_radius(wifi_connecting_panel, 10, 0);
    lv_obj_set_style_pad_all(wifi_connecting_panel, 14, 0);
    lv_obj_set_style_pad_row(wifi_connecting_panel, 10, 0);
    lv_obj_set_flex_flow(wifi_connecting_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_connecting_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(wifi_connecting_panel);

    lv_obj_t *spinner = lv_spinner_create(wifi_connecting_panel);
    lv_obj_set_size(spinner, 36, 36);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    lv_obj_t *label = lv_label_create(wifi_connecting_panel);
    lv_label_set_text_fmt(label, "Connecting to\n%s", ssid && ssid[0] ? ssid : "WiFi");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);

    wifi_connecting_deadline = lv_tick_get() + 12000;
    wifi_connecting_timer = lv_timer_create(wifi_connecting_timer_cb, 300, NULL);
}

static void password_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        const char *pwd = lv_textarea_get_text(ta);
        snprintf(wifi_password, sizeof(wifi_password), "%s", pwd ? pwd : "");
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER || key == LV_KEY_ESC) {
            leave_password_textarea(ta);
            lv_event_stop_processing(e);
        }
    } else if (code == LV_EVENT_CLICKED) {
        lv_indev_t *indev = lv_event_get_indev(e);
        lv_indev_type_t indev_type = indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
        if ((indev_type == LV_INDEV_TYPE_ENCODER || indev_type == LV_INDEV_TYPE_KEYPAD) && password_editing) {
            leave_password_textarea(ta);
        } else {
            focus_password_textarea(ta);
        }
    } else if (code == LV_EVENT_FOCUSED) {
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
        if (password_editing) {
            focus_password_textarea(ta);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED) {
        leave_password_textarea(ta);
    }
}

static void show_password_cb(lv_event_t *e)
{
    if (!connect_password_ta) return;
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool show = lv_obj_has_state(sw, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(connect_password_ta, !show);
}

static void connect_selected_cb(lv_event_t *e)
{
    if (selected_index < 0 || selected_index >= (int)scan_results.size()) return;

    const wifi_scan_params_t &ap = scan_results[selected_index];
    if (ap.ssid[0] == '\0') {
        ui_msg_pop_up("WiFi", "Hidden SSID cannot be joined from scan list.");
        return;
    }

    bool open_ap = auth_is_open(ap.authmode);
    const char *pwd = "";
    if (!open_ap) {
        pwd = connect_password_ta ? lv_textarea_get_text(connect_password_ta) : "";
        snprintf(wifi_password, sizeof(wifi_password), "%s", pwd ? pwd : "");
        if (strlen(wifi_password) < 8) {
            ui_msg_pop_up("WiFi", "Password must be at least 8 characters.");
            return;
        }
        pwd = wifi_password;
    }

    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ap.ssid);
    wifi_conn_params_t params;
    snprintf(params.ssid, sizeof(params.ssid), "%s", wifi_ssid);
    snprintf(params.password, sizeof(params.password), "%s", open_ap ? "" : pwd);
    hw_set_wifi_connect(params);
    set_status("Connecting...", UI_COLOR_TEXT_SECONDARY);
    close_connect_modal();
    show_connecting_overlay(wifi_ssid);
    update_connection_labels();
    hw_feedback();
}

static void cancel_connect_cb(lv_event_t *e)
{
    close_connect_modal();
    hw_feedback();
}

static lv_obj_t *create_modal_button(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(48), 36);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    if (lv_color_eq(color, UI_COLOR_ACCENT)) {
        ui_add_accent_focus_style(btn);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void open_connect_modal(void)
{
    if (selected_index < 0 || selected_index >= (int)scan_results.size()) return;
    close_connect_modal();
    begin_connect_modal_group();

    const wifi_scan_params_t &ap = scan_results[selected_index];
    bool open_ap = auth_is_open(ap.authmode);
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ap.ssid);
    wifi_password[0] = '\0';
    if (!open_ap) known_password_for_ssid(wifi_ssid, wifi_password, sizeof(wifi_password));

    connect_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(connect_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(connect_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(connect_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(connect_overlay, 0, 0);
    lv_obj_set_style_pad_all(connect_overlay, 10, 0);
    lv_obj_add_flag(connect_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(connect_overlay);
    connect_panel = panel;
    lv_obj_set_width(panel, LV_PCT(92));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 9, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(panel);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, ssid_display(ap));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    connect_detail_label = lv_label_create(panel);
    lv_label_set_text_fmt(connect_detail_label, "CH%d  %d dBm  %s",
                          (int)ap.channel, ap.rssi, auth_mode_str(ap.authmode));
    lv_obj_set_style_text_color(connect_detail_label, rssi_color(ap.rssi), 0);
    lv_obj_set_style_text_font(connect_detail_label, &lv_font_montserrat_12, 0);

    if (!open_ap) {
        lv_obj_t *password_row = lv_obj_create(panel);
        lv_obj_set_size(password_row, LV_PCT(100), 38);
        lv_obj_set_style_bg_opa(password_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(password_row, 0, 0);
        lv_obj_set_style_pad_all(password_row, 0, 0);
        lv_obj_set_scrollbar_mode(password_row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(password_row, LV_OBJ_FLAG_SCROLLABLE);

        connect_password_ta = lv_textarea_create(password_row);
        lv_textarea_set_placeholder_text(connect_password_ta, "Password");
        lv_textarea_set_one_line(connect_password_ta, true);
        lv_textarea_set_align(connect_password_ta, LV_TEXT_ALIGN_LEFT);
        lv_textarea_set_password_mode(connect_password_ta, true);
        lv_textarea_set_max_length(connect_password_ta, sizeof(wifi_password) - 1);
        lv_textarea_set_text(connect_password_ta, wifi_password);
        lv_obj_set_size(connect_password_ta, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(connect_password_ta, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(connect_password_ta, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(connect_password_ta, lv_color_white(), 0);
        lv_obj_set_style_text_line_space(connect_password_ta, 0, 0);
        lv_obj_set_style_pad_left(connect_password_ta, 9, 0);
        lv_obj_set_style_pad_right(connect_password_ta, 9, 0);
        lv_obj_set_style_pad_top(connect_password_ta, 7, 0);
        lv_obj_set_style_pad_bottom(connect_password_ta, 5, 0);
        lv_obj_set_style_bg_color(connect_password_ta, lv_color_white(), LV_PART_CURSOR);
        lv_obj_set_style_bg_opa(connect_password_ta, LV_OPA_COVER, LV_PART_CURSOR);
        lv_obj_set_style_border_color(connect_password_ta, UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_border_color(connect_password_ta, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(connect_password_ta, 1, 0);
        lv_obj_set_style_radius(connect_password_ta, 7, 0);
        lv_obj_set_scrollbar_mode(connect_password_ta, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(connect_password_ta, LV_DIR_HOR);
        lv_obj_add_event_cb(connect_password_ta, password_ta_event_cb, LV_EVENT_ALL, NULL);
        ui_prepare_textarea_for_encoder(connect_password_ta);
        connect_modal_add_group_obj(connect_password_ta);

        lv_obj_t *show_row = lv_obj_create(panel);
        lv_obj_set_size(show_row, LV_PCT(100), 28);
        lv_obj_set_style_bg_opa(show_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(show_row, 0, 0);
        lv_obj_set_style_pad_all(show_row, 0, 0);
        lv_obj_set_style_pad_column(show_row, 8, 0);
        lv_obj_set_flex_flow(show_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(show_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *show_label = lv_label_create(show_row);
        lv_label_set_text(show_label, "Show password");
        lv_obj_set_style_text_color(show_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(show_label, &lv_font_montserrat_12, 0);

        connect_show_switch = lv_switch_create(show_row);
        lv_obj_set_size(connect_show_switch, 42, 22);
        lv_obj_add_event_cb(connect_show_switch, show_password_cb, LV_EVENT_VALUE_CHANGED, NULL);
        connect_modal_add_group_obj(connect_show_switch);
    } else {
        lv_obj_t *open_label = lv_label_create(panel);
        lv_label_set_text(open_label, "Open network");
        lv_obj_set_style_text_color(open_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(open_label, &lv_font_montserrat_12, 0);
    }

    lv_obj_t *btn_row = lv_obj_create(panel);
    lv_obj_set_size(btn_row, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel_btn = create_modal_button(btn_row, LV_SYMBOL_CLOSE " Cancel", lv_color_hex(0x333333), cancel_connect_cb);
    lv_obj_t *connect_btn = create_modal_button(btn_row, LV_SYMBOL_OK " Connect", UI_COLOR_ACCENT, connect_selected_cb);
    connect_modal_add_group_obj(cancel_btn);
    connect_modal_add_group_obj(connect_btn);

    if (connect_password_ta) {
        focus_password_textarea(connect_password_ta);
    } else if (connect_btn && connect_modal_group) {
        lv_group_focus_obj(connect_btn);
    }
}

static void ap_row_event_cb(lv_event_t *e);

static void bind_ap_click(lv_obj_t *obj, int index)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, ap_row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
}

static void rebuild_ap_list(void)
{
    clear_ap_list();
    if (!ap_list_cont) return;

    if (scan_results.empty()) {
        add_empty_row("Turn on WiFi and scan nearby networks.");
        return;
    }

    int count = scan_results.size() < WIFI_MAX_AP_ROWS ? scan_results.size() : WIFI_MAX_AP_ROWS;
    for (int i = 0; i < count; ++i) {
        const wifi_scan_params_t &ap = scan_results[i];

        lv_obj_t *row = lv_obj_create(ap_list_cont);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, i == selected_index ? lv_color_hex(0x233932) : lv_color_hex(0x151515), 0);
        lv_obj_set_style_bg_opa(row, i == selected_index ? LV_OPA_80 : LV_OPA_30, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, i == selected_index ? UI_COLOR_ACCENT : UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_pad_ver(row, 7, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        bind_ap_click(row, i);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x233932), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
        lv_group_t *group = lv_group_get_default();
        if (group && lv_obj_get_group(row) != group) {
            lv_group_add_obj(group, row);
        }

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, rssi_color(ap.rssi), 0);
        bind_ap_click(icon, i);

        lv_obj_t *text_col = lv_obj_create(row);
        lv_obj_set_size(text_col, 1, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(text_col, 1);
        lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_col, 0, 0);
        lv_obj_set_style_pad_all(text_col, 0, 0);
        lv_obj_set_style_pad_row(text_col, 2, 0);
        lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
        bind_ap_click(text_col, i);

        lv_obj_t *ssid = lv_label_create(text_col);
        lv_label_set_text(ssid, ssid_display(ap));
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid, LV_PCT(100));
        lv_obj_set_style_text_color(ssid, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(ssid, &lv_font_montserrat_14, 0);
        bind_ap_click(ssid, i);

        lv_obj_t *meta = lv_label_create(text_col);
        lv_label_set_text_fmt(meta, "CH%d  %s", (int)ap.channel, auth_mode_str(ap.authmode));
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_obj_set_width(meta, LV_PCT(100));
        lv_obj_set_style_text_color(meta, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
        bind_ap_click(meta, i);

        lv_obj_t *right = lv_obj_create(row);
        lv_obj_set_size(right, 58, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right, 0, 0);
        lv_obj_set_style_pad_all(right, 0, 0);
        lv_obj_set_style_pad_row(right, 4, 0);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        bind_ap_click(right, i);

        lv_obj_t *rssi = lv_label_create(right);
        lv_label_set_text_fmt(rssi, "%d dBm", ap.rssi);
        lv_obj_set_style_text_color(rssi, rssi_color(ap.rssi), 0);
        lv_obj_set_style_text_font(rssi, &lv_font_montserrat_12, 0);
        bind_ap_click(rssi, i);

        lv_obj_t *bar = lv_bar_create(right);
        lv_obj_set_size(bar, 52, 6);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, rssi_to_percent(ap.rssi), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, rssi_color(ap.rssi), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
        bind_ap_click(bar, i);
    }
}

static void ap_row_event_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= (int)scan_results.size()) return;

    selected_index = index;
    rebuild_ap_list();
    open_connect_modal();
    hw_feedback();
}

static void apply_scan_results(vector<wifi_scan_params_t> &results)
{
    scan_results = results;
    sort(scan_results.begin(), scan_results.end(), [](const wifi_scan_params_t &a, const wifi_scan_params_t &b) {
        if (a.rssi != b.rssi) return a.rssi > b.rssi;
        return strcmp(a.ssid, b.ssid) < 0;
    });

    selected_index = -1;
    if (scan_summary_label) {
        int open_count = 0;
        for (auto &ap : scan_results) {
            if (auth_is_open(ap.authmode)) open_count++;
        }
        lv_label_set_text_fmt(scan_summary_label, "%u AP  %d open",
                              (unsigned)scan_results.size(), open_count);
    }

    set_status(scan_results.empty() ? "Scan complete: no AP" : "Scan complete", UI_COLOR_ACCENT);
    rebuild_ap_list();
}

static void scan_poll_cb(lv_timer_t *t)
{
    if (hw_get_wifi_scanning()) return;

    scanning = false;
    if (scan_timer) {
        lv_timer_del(scan_timer);
        scan_timer = NULL;
    }

    vector<wifi_scan_params_t> results;
    hw_get_wifi_scan_result(results);
    apply_scan_results(results);
}

static void start_scan(void)
{
    if (scanning) {
        set_status("Scan already running", UI_COLOR_TEXT_SECONDARY);
        return;
    }

    wifi_enabled = true;
    if (wifi_switch) lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    if (ap_list_card) lv_obj_remove_flag(ap_list_card, LV_OBJ_FLAG_HIDDEN);

    scanning = true;
    set_status("Scanning...", UI_COLOR_TEXT_SECONDARY);
    if (scan_summary_label) lv_label_set_text(scan_summary_label, "Scanning channels");
    clear_ap_list();
    add_empty_row("Scanning...");

    int16_t rc = hw_set_wifi_scan();
    if (rc == -2) {
        scanning = false;
        set_status("Scan busy", lv_color_hex(0xFF6B35));
        return;
    }

    if (scan_timer) {
        lv_timer_del(scan_timer);
        scan_timer = NULL;
    }
    scan_timer = lv_timer_create(scan_poll_cb, WIFI_SCAN_POLL_MS, NULL);
}

static void scan_event_cb(lv_event_t *e)
{
    hw_feedback();
    start_scan();
}

static void wifi_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    wifi_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    hw_feedback();

    if (wifi_enabled) {
        if (ap_list_card) lv_obj_remove_flag(ap_list_card, LV_OBJ_FLAG_HIDDEN);
        start_scan();
    } else {
        close_connect_modal();
        if (scan_timer) {
            lv_timer_del(scan_timer);
            scan_timer = NULL;
        }
        scanning = false;
        scan_results.clear();
        selected_index = -1;
        clear_ap_list();
        add_empty_row("WiFi is off.");
        if (scan_summary_label) lv_label_set_text(scan_summary_label, "Off");
        if (ap_list_card) lv_obj_add_flag(ap_list_card, LV_OBJ_FLAG_HIDDEN);
#ifdef ARDUINO
        WiFi.disconnect(false);
#endif
        set_status("WiFi off", lv_color_hex(0xFF6B35));
        update_connection_labels();
    }
}

static void disconnect_event_cb(lv_event_t *e)
{
    hw_feedback();
    close_connecting_overlay();
#ifdef ARDUINO
    WiFi.disconnect(false);
#endif
    set_status("Disconnected", lv_color_hex(0xFF6B35));
    update_connection_labels();
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) {
        *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        if (*out_label) {
            lv_obj_set_width(*out_label, 140);
            lv_label_set_long_mode(*out_label, LV_LABEL_LONG_DOT);
        }
    }
    return row;
}

static const lv_font_t *wifi_action_font(void)
{
    uint8_t pref = ui_get_font_size_pref();
    if (pref == 1) return &lv_font_montserrat_12;
    if (pref >= 3) return &lv_font_montserrat_16;
    return &lv_font_montserrat_14;
}

static lv_obj_t *add_right_action_button(lv_obj_t *card, const char *icon, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(lv_obj_create(card));
    lv_obj_set_size(btn, 96, 32);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    ui_add_accent_focus_style(btn);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, wifi_action_font(), 0);
    lv_obj_center(label);
    return ui_create_card_item(card, icon, NULL, btn);
}

static void back_event_handler(lv_event_t *e)
{
    close_connecting_overlay();
    close_connect_modal();
#ifdef USING_TOUCHPAD
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
#endif
    if (scan_timer) {
        lv_timer_del(scan_timer);
        scan_timer = NULL;
    }
    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = NULL;
    }
    scanning = false;
    scan_results.clear();

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    status_label = NULL;
    ssid_label = NULL;
    ip_label = NULL;
    rssi_label = NULL;
    mac_label = NULL;
    scan_summary_label = NULL;
    ap_list_card = NULL;
    ap_list_cont = NULL;
    wifi_switch = NULL;
    selected_index = -1;
    wifi_enabled = false;

    hide_keyboard();
    menu_show();
}

void ui_wireless_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "WiFi", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Connection");
    lv_obj_t *row = ui_create_card_switch(card, LV_SYMBOL_WIFI, "WiFi", hw_get_wifi_connected(), wifi_switch_cb);
    wifi_switch = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    wifi_enabled = hw_get_wifi_connected();

    row = ui_create_card_info(card, LV_SYMBOL_WIFI, "State", "Offline");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    add_info_row(card, LV_SYMBOL_WIFI, "SSID", "--", &ssid_label);
    add_info_row(card, LV_SYMBOL_HOME, "IP", "--", &ip_label);
    add_info_row(card, LV_SYMBOL_WIFI, "RSSI", "--", &rssi_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "MAC", "--", &mac_label);
    add_right_action_button(card, LV_SYMBOL_CLOSE, "Forget", disconnect_event_cb);

    ap_list_card = ui_create_card(page_container, "Networks");
    lv_obj_t *scan_row = ui_create_card_info(ap_list_card, LV_SYMBOL_LIST, "Scan", "Not scanned");
    scan_summary_label = lv_obj_get_child(scan_row, lv_obj_get_child_count(scan_row) - 1);
    add_right_action_button(ap_list_card, LV_SYMBOL_REFRESH, "Scan", scan_event_cb);

    ap_list_cont = lv_obj_create(ap_list_card);
    lv_obj_set_size(ap_list_cont, LV_PCT(100), is_screen_small() ? 162 : 218);
    lv_obj_set_style_bg_opa(ap_list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ap_list_cont, 0, 0);
    lv_obj_set_style_pad_all(ap_list_cont, 0, 0);
    lv_obj_set_style_pad_row(ap_list_cont, 6, 0);
    lv_obj_set_flex_flow(ap_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(ap_list_cont, LV_SCROLLBAR_MODE_OFF);
    add_empty_row(wifi_enabled ? "Tap Scan to discover nearby networks." : "Turn on WiFi to scan.");
    if (!wifi_enabled) lv_obj_add_flag(ap_list_card, LV_OBJ_FLAG_HIDDEN);

#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif

    update_connection_labels();
    status_timer = lv_timer_create(status_timer_cb, WIFI_STATUS_MS, NULL);
}

void ui_wireless_exit(lv_obj_t *parent)
{
}

app_t ui_wireless_main = {
    .setup_func_cb = ui_wireless_enter,
    .exit_func_cb = ui_wireless_exit,
    .user_data = nullptr,
};
