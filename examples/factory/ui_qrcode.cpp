/**
 * @file      ui_qrcode.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-10
 * 
 */
#include "ui_define.h"

#if !defined(EXCLUDE_QRCODE)

enum {
    QR_TEMPLATE_DEVICE = 0,
    QR_TEMPLATE_WIFI,
    QR_TEMPLATE_GNSS,
    QR_TEMPLATE_CUSTOM,
};

static lv_obj_t *page_container = NULL;
static lv_obj_t *qr_box = NULL;
static lv_obj_t *qr_obj = NULL;
static lv_obj_t *type_dd = NULL;
static lv_obj_t *content_ta = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *keyboard = NULL;

static char qr_content[256];

static void add_to_default_group(lv_obj_t *obj)
{
    lv_group_t *group = lv_group_get_default();
    if (group && obj && lv_obj_get_group(obj) != group) {
        lv_group_add_obj(group, obj);
    }
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void append_qr_escaped(char *out, size_t out_size, const char *text)
{
    if (!out || !text) return;
    while (*text) {
        size_t len = strlen(out);
        if (len + 2 >= out_size) return;
        if (*text == '\\' || *text == ';' || *text == ',' || *text == ':') {
            out[len++] = '\\';
            out[len] = 0;
        }
        len = strlen(out);
        if (len + 1 >= out_size) return;
        out[len++] = *text++;
        out[len] = 0;
    }
}

static void build_device_qr(void)
{
    uint8_t mac[6] = {0};
    hw_get_mac(mac);
    snprintf(qr_content, sizeof(qr_content),
             "Device:%s\nMAC:%02X:%02X:%02X:%02X:%02X:%02X",
             hw_get_variant_name(),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        size_t len = strlen(qr_content);
        snprintf(qr_content + len, sizeof(qr_content) - len,
                 "\nSSID:%s\nIP:%s",
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }
#endif
}

static void build_wifi_qr(void)
{
    qr_content[0] = 0;
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        snprintf(qr_content, sizeof(qr_content), "WIFI:T:nopass;S:");
        append_qr_escaped(qr_content, sizeof(qr_content), WiFi.SSID().c_str());
        size_t len = strlen(qr_content);
        if (len + 2 < sizeof(qr_content)) {
            snprintf(qr_content + len, sizeof(qr_content) - len, ";;");
        }
    } else
#endif
    {
        snprintf(qr_content, sizeof(qr_content), "WiFi is not connected");
    }
}

static void build_gnss_qr(void)
{
#if !defined(EXCLUDE_GPS)
    gps_params_t gps = {};
    hw_get_gps_info(gps);
    if (gps.location_valid) {
        snprintf(qr_content, sizeof(qr_content), "geo:%.6f,%.6f", gps.lat, gps.lng);
    } else {
        snprintf(qr_content, sizeof(qr_content), "GNSS fix is not available");
    }
#else
    snprintf(qr_content, sizeof(qr_content), "GNSS is disabled");
#endif
}

static void update_qr_from_text(void)
{
    if (!qr_obj || !content_ta) return;
    const char *text = lv_textarea_get_text(content_ta);
    if (!text || !text[0]) {
        set_status("Content is empty.", lv_color_hex(0xFF4444));
        return;
    }
    lv_result_t result = lv_qrcode_update(qr_obj, text, strlen(text));
    if (result == LV_RESULT_OK) {
        set_status("QR updated.", UI_COLOR_ACCENT);
    } else {
        set_status("QR data is too long.", lv_color_hex(0xFF4444));
    }
}

static void load_template(uint8_t type)
{
    switch (type) {
    case QR_TEMPLATE_DEVICE:
        build_device_qr();
        break;
    case QR_TEMPLATE_WIFI:
        build_wifi_qr();
        break;
    case QR_TEMPLATE_GNSS:
        build_gnss_qr();
        break;
    default:
        if (!qr_content[0]) {
            snprintf(qr_content, sizeof(qr_content), "https://www.lilygo.cc");
        }
        break;
    }

    if (content_ta) {
        lv_textarea_set_text(content_ta, qr_content);
    }
    update_qr_from_text();
}

static void type_change_cb(lv_event_t *e)
{
    uint8_t type = lv_dropdown_get_selected(type_dd);
    load_template(type);
}

static void generate_cb(lv_event_t *e)
{
    const char *text = lv_textarea_get_text(content_ta);
    snprintf(qr_content, sizeof(qr_content), "%s", text ? text : "");
    update_qr_from_text();
}

static void content_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool edited = lv_obj_has_state(ta, LV_STATE_EDITED);

    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        if (keyboard) {
            lv_keyboard_set_textarea(keyboard, NULL);
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        disable_keyboard();
        generate_cb(e);
    } else if (code == LV_EVENT_CLICKED) {
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
        } else {
            if (keyboard) {
                lv_keyboard_set_textarea(keyboard, ta);
                lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            enable_keyboard();
        }
    }
}

static void qr_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        lv_obj_t *obj = lv_event_get_target_obj(e);
        if (obj) {
            lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
        }
    }
}

static lv_obj_t *card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                               const char *options, uint8_t sel, lv_event_cb_t cb)
{
    lv_obj_t *row = ui_create_card_dropdown(card, icon, title, options, sel, cb);
    lv_obj_t *dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    lv_obj_set_width(dd, 128);
    return dd;
}

static void back_event_handler(lv_event_t *e)
{
    disable_keyboard();
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    qr_box = NULL;
    qr_obj = NULL;
    type_dd = NULL;
    content_ta = NULL;
    status_label = NULL;
    menu_show();
}

void ui_qrcode_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "QRCode", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Code");
    lv_coord_t qr_size = is_screen_small() ? 128 : 156;

    qr_box = lv_obj_create(card);
    lv_obj_set_size(qr_box, LV_PCT(100), qr_size + 16);
    lv_obj_set_style_bg_opa(qr_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qr_box, 0, 0);
    lv_obj_set_style_pad_all(qr_box, 0, 0);
    lv_obj_set_flex_flow(qr_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(qr_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(qr_box, LV_OBJ_FLAG_SCROLLABLE);

    qr_obj = lv_qrcode_create(qr_box);
    lv_qrcode_set_size(qr_obj, qr_size);
    lv_qrcode_set_dark_color(qr_obj, lv_color_black());
    lv_qrcode_set_light_color(qr_obj, lv_color_white());
    lv_qrcode_set_quiet_zone(qr_obj, true);
    lv_obj_remove_flag(qr_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(qr_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(qr_obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_outline_width(qr_obj, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(qr_obj, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(qr_obj, LV_OPA_80, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(qr_obj, 4, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(qr_obj, qr_event_cb, LV_EVENT_ALL, NULL);
    add_to_default_group(qr_obj);

    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Ready.");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    card = ui_create_card(page_container, "Content");
    type_dd = card_dropdown(card, LV_SYMBOL_LIST, "Template",
                            "Device\nWiFi\nGNSS\nCustom", QR_TEMPLATE_DEVICE, type_change_cb);

    content_ta = lv_textarea_create(card);
    lv_obj_set_width(content_ta, LV_PCT(100));
    lv_obj_set_height(content_ta, 72);
    lv_textarea_set_max_length(content_ta, sizeof(qr_content) - 1);
    lv_textarea_set_placeholder_text(content_ta, "QR content");
    lv_obj_set_scrollbar_mode(content_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(content_ta, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(content_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(content_ta, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(content_ta, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(content_ta, 1, 0);
    lv_obj_set_style_radius(content_ta, 6, 0);
    lv_obj_add_event_cb(content_ta, content_ta_cb, LV_EVENT_ALL, NULL);
    ui_create_card_item(card, LV_SYMBOL_EDIT, "Text", content_ta);

    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Generate", "Update", generate_cb);

    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    qr_content[0] = 0;
    load_template(QR_TEMPLATE_DEVICE);
}

void ui_qrcode_exit(lv_obj_t *parent)
{
}

app_t ui_qrcode_main = {
    .setup_func_cb = ui_qrcode_enter,
    .exit_func_cb = ui_qrcode_exit,
    .user_data = nullptr,
};

#endif
