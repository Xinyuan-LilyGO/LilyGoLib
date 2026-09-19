/**
 * @file      ui_nfc_emulation.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-11
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if defined(ARDUINO) && !defined(EXCLUDE_NFC_EMULATION)

static lv_obj_t *page_container = NULL;
static lv_obj_t *state_value = NULL;
static lv_obj_t *template_value = NULL;
static lv_obj_t *tech_value = NULL;
static lv_obj_t *count_value = NULL;
static lv_obj_t *payload_value = NULL;
static lv_obj_t *hint_value = NULL;
static lv_obj_t *tech_dropdown = NULL;
static lv_obj_t *url_btn = NULL;
static lv_obj_t *text_btn = NULL;
static lv_obj_t *wifi_btn = NULL;

static bool nfc_card_running = false;
static LilyGoNfcEmulationKind active_kind = LILYGO_NFC_EMULATION_URL;
static LilyGoNfcEmulationTech selected_tech = LILYGO_NFC_EMULATION_TECH_NFCF_T3T;

static const char *kDefaultUrl = "https://github.com/Xinyuan-LilyGO";
static const char *kDefaultText = "LilyGo NFC Card Emulation";
static const char *kDefaultWifiSsid = "LilyGo-NFC";
static const char *kDefaultWifiPassword = "12345678";

static bool compact_screen()
{
    return lv_display_get_horizontal_resolution(NULL) <= 320 ||
           lv_display_get_vertical_resolution(NULL) <= 240;
}

static bool short_wide_screen()
{
    return lv_display_get_vertical_resolution(NULL) <= 240;
}

static bool round_safe_screen()
{
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    return true;
#else
    return lv_display_get_horizontal_resolution(NULL) >= 480 &&
           lv_display_get_vertical_resolution(NULL) >= 360;
#endif
}

static int32_t panel_gap()
{
    return short_wide_screen() ? 4 : 8;
}

static void apply_round_safe_area(lv_obj_t *content)
{
    if (!content || !round_safe_screen()) return;

    lv_obj_t *root = (lv_obj_t *)lv_obj_get_user_data(content);
    if (!root || lv_obj_get_child_count(root) < 2) return;

    lv_obj_t *nav = lv_obj_get_child(root, 0);
    lv_obj_t *divider = lv_obj_get_child(root, 1);
    if (nav) {
        lv_obj_set_height(nav, 44);
        lv_obj_align(nav, LV_ALIGN_TOP_MID, 0, 8);

        if (lv_obj_get_child_count(nav) > 0) {
            lv_obj_t *back_btn = lv_obj_get_child(nav, 0);
            lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 22, 0);
        }
    }
    if (divider && nav) {
        lv_obj_align_to(divider, nav, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    }

    const int32_t content_y = 53;
    const int32_t bottom_bar_h = 44;
    int32_t content_h = lv_display_get_vertical_resolution(NULL) - content_y - bottom_bar_h;
    if (content_h < 120) content_h = 120;
    lv_obj_set_height(content, content_h);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, content_y);
}

static void add_to_group(lv_obj_t *obj)
{
    if (!obj) return;
    lv_group_t *group = lv_group_get_default();
    if (group && lv_obj_get_group(obj) != group) {
        lv_group_add_obj(group, obj);
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
}

static lv_obj_t *create_panel(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_width(panel, LV_PCT(100));
    if (height > 0) {
        lv_obj_set_height(panel, height);
    } else {
        lv_obj_set_height(panel, 1);
        lv_obj_set_flex_grow(panel, 1);
    }
    lv_obj_set_style_bg_color(panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, compact_screen() ? 5 : 10, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *create_plain_container(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_height(obj, 1);
    lv_obj_set_flex_grow(obj, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, panel_gap(), 0);
    lv_obj_set_style_pad_column(obj, panel_gap(), 0);
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *create_metric(lv_obj_t *parent, const char *title, const char *value)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_height(box, LV_PCT(100));
    lv_obj_set_width(box, 1);
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_row(box, 1, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(box);
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);

    lv_obj_t *value_label = lv_label_create(box);
    lv_label_set_text(value_label, value ? value : "--");
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_obj_set_style_text_color(value_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    return value_label;
}

static lv_obj_t *button_label(lv_obj_t *btn)
{
    if (!btn || lv_obj_get_child_count(btn) == 0) return NULL;
    return lv_obj_get_child(btn, 0);
}

static void set_button_text(lv_obj_t *btn, const char *text)
{
    lv_obj_t *label = button_label(btn);
    if (label) {
        lv_label_set_text(label, text ? text : "");
        lv_obj_center(label);
    }
}

static lv_obj_t *create_template_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_height(btn, 30);
    lv_obj_set_width(btn, 1);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_WARNING, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT_DIM, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, UI_COLOR_TRACK, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_PRIMARY, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_SECONDARY, LV_STATE_DISABLED);
    ui_add_accent_focus_style(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    add_to_group(btn);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);
    return btn;
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label) {
        lv_label_set_text(label, text ? text : "--");
    }
}

static const char *template_short_name(LilyGoNfcEmulationKind kind)
{
    switch (kind) {
    case LILYGO_NFC_EMULATION_URL:
        return "URL";
    case LILYGO_NFC_EMULATION_TEXT:
        return "Text";
    case LILYGO_NFC_EMULATION_WIFI:
        return "WiFi";
    default:
        return "--";
    }
}

static const char *event_state_text(LilyGoNfcEvent event, const LilyGoNfcEmulationStatus &status)
{
    if (event == LILYGO_NFC_EVENT_START_FAILED ||
            event == LILYGO_NFC_EVENT_EMULATION_ERROR) {
        return "Error";
    }
    if (event == LILYGO_NFC_EVENT_STOPPED) {
        return "Stopped";
    }
    if (event == LILYGO_NFC_EVENT_EMULATION_ACTIVATED) {
        return "Reading";
    }
    if (status.readerActive) {
        return "Reading";
    }
    if (nfc_card_running || event == LILYGO_NFC_EVENT_EMULATION_RELEASED ||
            event == LILYGO_NFC_EVENT_EMULATION_STARTED) {
        return "Waiting";
    }
    return "Idle";
}

static void update_controls()
{
    const bool url_active = nfc_card_running && active_kind == LILYGO_NFC_EMULATION_URL;
    const bool text_active = nfc_card_running && active_kind == LILYGO_NFC_EMULATION_TEXT;
    const bool wifi_active = nfc_card_running && active_kind == LILYGO_NFC_EMULATION_WIFI;

    if (tech_dropdown) {
        if (nfc_card_running) {
            lv_obj_add_state(tech_dropdown, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(tech_dropdown, LV_STATE_DISABLED);
            lv_dropdown_set_selected(tech_dropdown, selected_tech);
        }
    }

    if (url_btn) {
        set_button_text(url_btn, url_active ? "Stop" : "URL");
        if (nfc_card_running && !url_active) lv_obj_add_state(url_btn, LV_STATE_DISABLED);
        else lv_obj_clear_state(url_btn, LV_STATE_DISABLED);
        if (url_active) lv_obj_add_state(url_btn, LV_STATE_CHECKED);
        else lv_obj_clear_state(url_btn, LV_STATE_CHECKED);
    }
    if (text_btn) {
        set_button_text(text_btn, text_active ? "Stop" : "Text");
        if (nfc_card_running && !text_active) lv_obj_add_state(text_btn, LV_STATE_DISABLED);
        else lv_obj_clear_state(text_btn, LV_STATE_DISABLED);
        if (text_active) lv_obj_add_state(text_btn, LV_STATE_CHECKED);
        else lv_obj_clear_state(text_btn, LV_STATE_CHECKED);
    }
    if (wifi_btn) {
        set_button_text(wifi_btn, wifi_active ? "Stop" : "WiFi");
        if (nfc_card_running && !wifi_active) lv_obj_add_state(wifi_btn, LV_STATE_DISABLED);
        else lv_obj_clear_state(wifi_btn, LV_STATE_DISABLED);
        if (wifi_active) lv_obj_add_state(wifi_btn, LV_STATE_CHECKED);
        else lv_obj_clear_state(wifi_btn, LV_STATE_CHECKED);
    }
}

static void format_count_label(const LilyGoNfcEmulationStatus &status)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "R%lu F%lu",
             static_cast<unsigned long>(status.readCount),
             static_cast<unsigned long>(status.exchangeCount));
    set_label_text(count_value, buf);
}

static void update_emulation_view(LilyGoNfcEvent event, const LilyGoNfcEmulationStatus &status)
{
    if (!page_container) return;

    char buf[192];
    set_label_text(state_value, event_state_text(event, status));
    set_label_text(template_value, status.templateName[0] ? status.templateName : template_short_name(status.kind));

    if (status.readerActive && status.activeTechnology[0] && strcmp(status.activeTechnology, "--") != 0) {
        snprintf(buf, sizeof(buf), "%s/%s", status.activeTechnology, status.activeInterface);
    } else {
        snprintf(buf, sizeof(buf), "%s%s",
                 lilygoNfcEmulationTechName(status.tech),
                 status.experimental ? " Exp" : "");
    }
    set_label_text(tech_value, buf);

    format_count_label(status);
    set_label_text(payload_value, status.payloadPreview[0] ? status.payloadPreview : "--");

    if (event == LILYGO_NFC_EVENT_START_FAILED ||
            event == LILYGO_NFC_EVENT_EMULATION_ERROR) {
        snprintf(buf, sizeof(buf), "Error %u: %s",
                 status.lastError,
                 status.errorText[0] ? status.errorText : "NFC emulation error");
        set_label_text(hint_value, buf);
    } else if (event == LILYGO_NFC_EVENT_EMULATION_ACTIVATED) {
        set_label_text(hint_value, "Phone detected. Keep it close.");
    } else if (event == LILYGO_NFC_EVENT_EMULATION_RELEASED) {
        set_label_text(hint_value, "Read finished. Waiting for next phone.");
    } else if (event == LILYGO_NFC_EVENT_STOPPED) {
        set_label_text(hint_value, "Stopped.");
    } else if (status.tech == LILYGO_NFC_EMULATION_TECH_NFCA_T4T) {
        set_label_text(hint_value, "NFC-A T4T is experimental.");
    } else {
        set_label_text(hint_value, "Hold phone near NFC antenna.");
    }

    update_controls();
}

static void nfc_emulation_event_callback(LilyGoNfcEvent event,
        const LilyGoNfcEmulationStatus &status,
        void *userData)
{
    (void)userData;
    LILYGO_LOG_PRINTF("NFC card event: %s  state:%s  kind:%s  tech:%s  reads:%lu  exchanges:%lu  err:%u\n",
                  lilygoNfcEventName(event),
                  lilygoNfcStateName(status.state),
                  lilygoNfcEmulationKindName(status.kind),
                  lilygoNfcEmulationTechName(status.tech),
                  static_cast<unsigned long>(status.readCount),
                  static_cast<unsigned long>(status.exchangeCount),
                  status.lastError);

    if (event == LILYGO_NFC_EVENT_EMULATION_STARTED) {
        nfc_card_running = true;
    } else if (event == LILYGO_NFC_EVENT_STOPPED ||
               event == LILYGO_NFC_EVENT_START_FAILED ||
               !LilyGoNfc.isRunning()) {
        nfc_card_running = false;
    }

    if (event == LILYGO_NFC_EVENT_EMULATION_ACTIVATED) {
        hw_feedback();
    }
    update_emulation_view(event, status);
}

static bool start_template(LilyGoNfcEmulationKind kind)
{
    if (nfc_card_running) {
        if (kind == active_kind) {
            nfc_card_running = false;
            hw_stop_nfc();
            update_controls();
            set_label_text(state_value, "Stopped");
            set_label_text(hint_value, "Stopped.");
        }
        return false;
    }

    LilyGoNfcEmulationConfig config = {};
    config.kind = kind;
    config.tech = selected_tech;
    config.discoveryDurationMs = 1000U;
    config.callback = nfc_emulation_event_callback;
    config.userData = NULL;

    active_kind = kind;

    switch (kind) {
    case LILYGO_NFC_EMULATION_URL:
        config.templateName = "URL";
        config.url = kDefaultUrl;
        break;

    case LILYGO_NFC_EMULATION_TEXT:
        config.templateName = "Text";
        config.text = kDefaultText;
        break;

    case LILYGO_NFC_EMULATION_WIFI:
        config.templateName = "WiFi";
        config.wifiSsid = kDefaultWifiSsid;
        config.wifiPassword = kDefaultWifiPassword;
        config.wifiAuthenticationType = 0x0020U;
        config.wifiEncryptionType = 0x0010U;
        break;

    default:
        return false;
    }

    set_label_text(state_value, "Starting");
    set_label_text(template_value, template_short_name(kind));
    set_label_text(hint_value, "Starting...");
    update_controls();

    const bool started = hw_start_nfc_emulation(config);
    nfc_card_running = started;
    if (!started) {
        const LilyGoNfcEmulationStatus &status = LilyGoNfc.emulationStatus();
        update_emulation_view(LILYGO_NFC_EVENT_START_FAILED, status);
    } else {
        update_controls();
    }
    return started;
}

static void stop_emulation()
{
    if (nfc_card_running || LilyGoNfc.isRunning()) {
        nfc_card_running = false;
        hw_stop_nfc();
    }
    update_controls();
}

static void url_btn_cb(lv_event_t *e)
{
    (void)e;
    start_template(LILYGO_NFC_EMULATION_URL);
}

static void text_btn_cb(lv_event_t *e)
{
    (void)e;
    start_template(LILYGO_NFC_EMULATION_TEXT);
}

static void wifi_btn_cb(lv_event_t *e)
{
    (void)e;
    start_template(LILYGO_NFC_EMULATION_WIFI);
}

static void tech_dropdown_cb(lv_event_t *e)
{
    if (nfc_card_running) {
        if (tech_dropdown) {
            lv_dropdown_set_selected(tech_dropdown, selected_tech);
        }
        return;
    }
    lv_obj_t *dd = lv_event_get_target_obj(e);
    selected_tech = static_cast<LilyGoNfcEmulationTech>(lv_dropdown_get_selected(dd));
    set_label_text(tech_value, lilygoNfcEmulationTechName(selected_tech));
}

static void create_status_panel(int32_t height)
{
    lv_obj_t *status_panel = create_panel(page_container, height);
    lv_obj_set_style_pad_column(status_panel, short_wide_screen() ? 4 : 8, 0);
    lv_obj_set_flex_flow(status_panel, LV_FLEX_FLOW_ROW);
    state_value = create_metric(status_panel, "State", "Idle");
    template_value = create_metric(status_panel, "Template", "--");
    tech_value = create_metric(status_panel, "Tech", lilygoNfcEmulationTechName(selected_tech));
    count_value = create_metric(status_panel, "Count", "R0 F0");
}

static void create_short_controls()
{
    lv_obj_t *control_panel = create_panel(page_container, 38);
    lv_obj_set_style_pad_column(control_panel, 5, 0);
    lv_obj_set_flex_flow(control_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *tech_title = lv_label_create(control_panel);
    lv_label_set_text(tech_title, "Tech");
    lv_obj_set_style_text_color(tech_title, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(tech_title, &lv_font_montserrat_12, 0);

    tech_dropdown = lv_dropdown_create(control_panel);
    lv_dropdown_set_options(tech_dropdown, "NFC-F T3T\nNFC-A T4T Exp\nMixed A/F");
    lv_dropdown_set_selected(tech_dropdown, selected_tech);
    lv_obj_set_size(tech_dropdown, lv_display_get_horizontal_resolution(NULL) <= 330 ? 112 : 138, 28);
    lv_obj_add_event_cb(tech_dropdown, tech_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);
    add_to_group(tech_dropdown);

    url_btn = create_template_button(control_panel, "URL", url_btn_cb);
    text_btn = create_template_button(control_panel, "Text", text_btn_cb);
    wifi_btn = create_template_button(control_panel, "WiFi", wifi_btn_cb);
}

static void create_large_controls(lv_obj_t *parent)
{
    lv_obj_t *control_panel = create_panel(parent, 0);
    lv_obj_set_size(control_panel, 172, LV_PCT(100));
    lv_obj_set_flex_grow(control_panel, 0);
    lv_obj_set_style_pad_row(control_panel, 8, 0);
    lv_obj_set_flex_flow(control_panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *tech_title = lv_label_create(control_panel);
    lv_label_set_text(tech_title, "Technology");
    lv_obj_set_width(tech_title, LV_PCT(100));
    lv_obj_set_style_text_color(tech_title, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(tech_title, &lv_font_montserrat_12, 0);

    tech_dropdown = lv_dropdown_create(control_panel);
    lv_dropdown_set_options(tech_dropdown, "NFC-F T3T\nNFC-A T4T Exp\nMixed A/F");
    lv_dropdown_set_selected(tech_dropdown, selected_tech);
    lv_obj_set_size(tech_dropdown, LV_PCT(100), 34);
    lv_obj_add_event_cb(tech_dropdown, tech_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);
    add_to_group(tech_dropdown);

    lv_obj_t *template_title = lv_label_create(control_panel);
    lv_label_set_text(template_title, "Templates");
    lv_obj_set_width(template_title, LV_PCT(100));
    lv_obj_set_style_text_color(template_title, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(template_title, &lv_font_montserrat_12, 0);

    url_btn = create_template_button(control_panel, "URL", url_btn_cb);
    text_btn = create_template_button(control_panel, "Text", text_btn_cb);
    wifi_btn = create_template_button(control_panel, "WiFi", wifi_btn_cb);

    lv_obj_set_width(url_btn, LV_PCT(100));
    lv_obj_set_width(text_btn, LV_PCT(100));
    lv_obj_set_width(wifi_btn, LV_PCT(100));
}

static lv_obj_t *create_payload_panel(lv_obj_t *parent)
{
    lv_obj_t *payload_panel = create_panel(parent, 0);
    lv_obj_set_style_pad_row(payload_panel, short_wide_screen() ? 4 : 8, 0);
    lv_obj_set_flex_flow(payload_panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(payload_panel);
    lv_label_set_text(title, "Payload");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);

    payload_value = lv_label_create(payload_panel);
    lv_label_set_text(payload_value, "--");
    lv_obj_set_width(payload_value, LV_PCT(100));
    lv_obj_set_height(payload_value, 1);
    lv_obj_set_flex_grow(payload_value, 1);
    lv_label_set_long_mode(payload_value, short_wide_screen() ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(payload_value, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(payload_value, short_wide_screen() ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);

    hint_value = lv_label_create(payload_panel);
    lv_label_set_text(hint_value, "Choose URL, Text or WiFi to start.");
    lv_obj_set_width(hint_value, LV_PCT(100));
    lv_label_set_long_mode(hint_value, short_wide_screen() ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint_value, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint_value, &lv_font_montserrat_12, 0);

    return payload_panel;
}

static void build_short_layout()
{
    create_status_panel(42);
    create_short_controls();
    create_payload_panel(page_container);
}

static void build_large_layout()
{
    create_status_panel(68);

    lv_obj_t *body = create_plain_container(page_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 10, 0);

    create_large_controls(body);

    lv_obj_t *payload_panel = create_payload_panel(body);
    lv_obj_set_size(payload_panel, 1, LV_PCT(100));
    lv_obj_set_flex_grow(payload_panel, 1);
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    stop_emulation();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    state_value = NULL;
    template_value = NULL;
    tech_value = NULL;
    count_value = NULL;
    payload_value = NULL;
    hint_value = NULL;
    tech_dropdown = NULL;
    url_btn = NULL;
    text_btn = NULL;
    wifi_btn = NULL;
    menu_show();
}

void ui_nfc_emulation_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC Card", back_event_handler);
    apply_round_safe_area(page_container);

    lv_obj_set_style_pad_left(page_container, round_safe_screen() ? 32 : 6, 0);
    lv_obj_set_style_pad_right(page_container, round_safe_screen() ? 32 : 6, 0);
    lv_obj_set_style_pad_top(page_container, round_safe_screen() ? 14 : 4, 0);
    lv_obj_set_style_pad_bottom(page_container, round_safe_screen() ? 8 : 4, 0);
    lv_obj_set_style_pad_row(page_container, panel_gap(), 0);
    lv_obj_set_style_pad_column(page_container, panel_gap(), 0);

    if (short_wide_screen()) {
        build_short_layout();
    } else {
        build_large_layout();
    }

    update_controls();
}

void ui_nfc_emulation_exit(lv_obj_t *parent)
{
    (void)parent;
    stop_emulation();
}

app_t ui_nfc_emulation_main = {
    .setup_func_cb = ui_nfc_emulation_enter,
    .exit_func_cb  = ui_nfc_emulation_exit,
    .user_data     = nullptr,
};

#else

void ui_nfc_emulation_enter(lv_obj_t *parent)
{
    (void)parent;
}

void ui_nfc_emulation_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_nfc_emulation_main = {
    .setup_func_cb = ui_nfc_emulation_enter,
    .exit_func_cb  = ui_nfc_emulation_exit,
    .user_data     = nullptr,
};

#endif
