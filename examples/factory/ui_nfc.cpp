/**
 * @file      ui_nfc.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if defined(ARDUINO) && !defined(EXCLUDE_NFC)

static lv_obj_t *msgbox = NULL;
static lv_obj_t *page_container = NULL;
static lv_obj_t *state_value = NULL;
static lv_obj_t *uid_value = NULL;
static lv_obj_t *tech_value = NULL;
static lv_obj_t *ndef_value = NULL;
static lv_obj_t *record_body = NULL;
static lv_obj_t *raw_value = NULL;
static lv_obj_t *hint_value = NULL;
static bool nfc_page_running = false;

static lv_obj_t *create_value_label(lv_obj_t *card, const char *icon, const char *title, const char *value)
{
    lv_obj_t *label = lv_label_create(lv_obj_create(card));
    lv_label_set_text(label, value ? value : "--");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_width(label, 1);
    ui_create_card_item(card, icon, title, label);
    return label;
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label) {
        lv_label_set_text(label, text ? text : "--");
    }
}

static bool is_mifare_classic_like(const LilyGoNfcReaderResult &result)
{
    return result.mifareClassic ||
           strcmp(result.tagType, "MIFARE") == 0 ||
           strstr(result.cardType, "MIFARE Classic") != NULL;
}

static bool is_uid_only_result(const LilyGoNfcReaderResult &result)
{
    return strcmp(result.ndefStateText, "UID only") == 0;
}

static const char *card_detected_message(const LilyGoNfcReaderResult &result)
{
    if (is_mifare_classic_like(result)) {
        return "MIFARE Classic card detected.\nID has been read.";
    }
    if (strstr(result.cardType, "Type 4") != NULL) {
        return "NFC Type 4 card/device detected.\nID has been read.";
    }
    if (strstr(result.cardType, "Type 3") != NULL || strstr(result.cardType, "FeliCa") != NULL) {
        return "FeliCa / NFC Type 3 card detected.\nID has been read.";
    }
    if (strstr(result.cardType, "Type 5") != NULL || strstr(result.cardType, "NFC-V") != NULL) {
        return "ISO15693 / NFC Type 5 card detected.\nID has been read.";
    }
    if (strstr(result.cardType, "P2P") != NULL) {
        return "NFC peer-to-peer device detected.\nID has been read.";
    }
    if (strstr(result.cardType, "ST25TB") != NULL) {
        return "ST25TB tag detected.\nID has been read.";
    }
    return "NFC card detected.\nID has been read.";
}

static const char *card_hint_message(const LilyGoNfcReaderResult &result)
{
    if (is_mifare_classic_like(result)) {
        return "MIFARE Classic ID is public. Sector data needs keys/auth.";
    }
    if (strstr(result.cardType, "Type 4") != NULL) {
        return "Type 4 / ISO-DEP card detected. Standard NDEF data was not found.";
    }
    if (strstr(result.cardType, "P2P") != NULL) {
        return "P2P device detected. This reader only shows the detected NFC ID.";
    }
    return "Card type and ID are shown above. Standard NDEF data is optional.";
}

static const char *record_icon(LilyGoNfcRecordKind kind)
{
    switch (kind) {
    case LILYGO_NFC_RECORD_TEXT:
        return LV_SYMBOL_EDIT;
    case LILYGO_NFC_RECORD_URI:
        return LV_SYMBOL_WIFI;
    case LILYGO_NFC_RECORD_WIFI:
        return LV_SYMBOL_WIFI;
    case LILYGO_NFC_RECORD_VCARD:
        return LV_SYMBOL_LIST;
    case LILYGO_NFC_RECORD_DEVICE_INFO:
        return LV_SYMBOL_SETTINGS;
    case LILYGO_NFC_RECORD_AAR:
        return LV_SYMBOL_LIST;
    case LILYGO_NFC_RECORD_EMPTY:
        return LV_SYMBOL_WARNING;
    case LILYGO_NFC_RECORD_MEDIA:
    case LILYGO_NFC_RECORD_UNKNOWN:
    default:
        return LV_SYMBOL_LIST;
    }
}

static void add_record_row(const LilyGoNfcRecord &record)
{
    if (!record_body) return;

    lv_obj_t *row = lv_obj_create(record_body);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_row(row, 3, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    char title[48];
    snprintf(title, sizeof(title), "%u  %s  %s", record.index, record_icon(record.kind), record.title);
    lv_obj_t *title_label = lv_label_create(row);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, record.value[0] ? record.value : "--");
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_obj_set_style_text_color(value_label, UI_COLOR_TEXT_PRIMARY, 0);

    if (record.detail[0]) {
        lv_obj_t *detail_label = lv_label_create(row);
        lv_label_set_text(detail_label, record.detail);
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(detail_label, LV_PCT(100));
        lv_obj_set_style_text_color(detail_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_12, 0);
    }
}

static void clear_records(const char *emptyText)
{
    if (!record_body) return;
    lv_obj_clean(record_body);

    lv_obj_t *label = lv_label_create(record_body);
    lv_label_set_text(label, emptyText ? emptyText : "No records");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(label, 6, 0);
    lv_obj_set_style_pad_bottom(label, 6, 0);
}

static void update_record_view(const LilyGoNfcReaderResult &result)
{
    if (!record_body) return;
    lv_obj_clean(record_body);

    if (result.recordCount == 0) {
        clear_records(result.hasNdef ? "NDEF message has no records" : "No NDEF records");
        return;
    }

    for (uint8_t i = 0; i < result.recordCount; ++i) {
        add_record_row(result.records[i]);
    }
}

static void update_result_view(LilyGoNfcEvent event, const LilyGoNfcReaderResult &result)
{
    if (!page_container) return;

    char buf[192];
    snprintf(buf, sizeof(buf), "%s", lilygoNfcEventName(event));
    set_label_text(state_value, buf);

    set_label_text(uid_value, result.uidText);

    if (result.cardInfo[0] && strcmp(result.cardInfo, "--") != 0) {
        snprintf(buf, sizeof(buf), "%s\n%s / %s / %s\n%s",
                 result.cardType[0] ? result.cardType : result.technology,
                 result.technology, result.interfaceName, result.tagType,
                 result.cardInfo);
    } else {
        snprintf(buf, sizeof(buf), "%s / %s / %s",
                 result.technology, result.interfaceName, result.tagType);
    }
    set_label_text(tech_value, buf);

    if (result.lastError != ST_ERR_NONE &&
            event == LILYGO_NFC_EVENT_START_FAILED) {
        snprintf(buf, sizeof(buf), "Error %u  %s", result.lastError,
                 result.errorText[0] ? result.errorText : "NFC error");
        set_label_text(ndef_value, buf);
        clear_records(buf);
    } else if (event == LILYGO_NFC_EVENT_NDEF_UNSUPPORTED) {
        set_label_text(ndef_value, "No NDEF");
        clear_records(card_detected_message(result));
        set_label_text(hint_value, card_hint_message(result));
    } else if (result.lastError != ST_ERR_NONE &&
               event == LILYGO_NFC_EVENT_NDEF_ERROR) {
        snprintf(buf, sizeof(buf), "Read error %u  %s", result.lastError,
                 result.errorText[0] ? result.errorText : "NFC error");
        set_label_text(ndef_value, buf);
        clear_records(buf);
        set_label_text(hint_value, NFC_TIPS_STRING);
    } else if (event == LILYGO_NFC_EVENT_CARD_RELEASED) {
        set_label_text(state_value, "Polling");
        set_label_text(ndef_value, "Ready");
        set_label_text(hint_value, NFC_TIPS_STRING);
    } else if (event == LILYGO_NFC_EVENT_CARD_DETECTED && is_uid_only_result(result)) {
        set_label_text(ndef_value, "ID only");
        clear_records(card_detected_message(result));
        set_label_text(hint_value, card_hint_message(result));
    } else if (result.hasNdef) {
        snprintf(buf, sizeof(buf), "%s  v%u.%u  %luB",
                 result.ndefStateText, result.ndefMajorVersion,
                 result.ndefMinorVersion,
                 static_cast<unsigned long>(result.ndefMessageLen));
        set_label_text(ndef_value, buf);
        set_label_text(hint_value, NFC_TIPS_STRING);
    } else {
        set_label_text(ndef_value, result.ndefStateText[0] ? result.ndefStateText : "--");
    }

    if (event == LILYGO_NFC_EVENT_NDEF_READ) {
        update_record_view(result);
        set_label_text(raw_value, result.rawHexPreview[0] ? result.rawHexPreview : "--");
    } else if (event == LILYGO_NFC_EVENT_NDEF_UNSUPPORTED ||
               event == LILYGO_NFC_EVENT_NDEF_ERROR) {
        set_label_text(raw_value, "--");
    } else if (event == LILYGO_NFC_EVENT_CARD_DETECTED) {
        if (!is_uid_only_result(result)) {
            clear_records("Reading NDEF...");
        }
        set_label_text(raw_value, "--");
    } else if (event == LILYGO_NFC_EVENT_STARTED || event == LILYGO_NFC_EVENT_CARD_RELEASED) {
        if (event == LILYGO_NFC_EVENT_STARTED) {
            clear_records("Waiting for card");
            set_label_text(hint_value, NFC_TIPS_STRING);
        }
        set_label_text(raw_value, "--");
    }
}

static void goto_connect_wifi_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *text = lv_label_get_text(label);
    LILYGO_LOG_PRINTF("Button %s clicked\n", text);

    if (strcmp(text, "Close") == 0) {
        if (msgbox) { destroy_msgbox(msgbox); msgbox = NULL; }
    } else {
        wifi_conn_params_t *params = (wifi_conn_params_t *)lv_event_get_user_data(e);
        if (params) hw_set_wifi_connect(*params);
        if (msgbox) { destroy_msgbox(msgbox); msgbox = NULL; }
        ui_show_wifi_process_bar();
    }
}

void ui_nfc_pop_up(wifi_conn_params_t &params)
{
    set_low_power_mode_flag(false);
    if (msgbox) return;

    static const char *btns[] = {"Connect", "Close", ""};
    char msg_txt[128];
    snprintf(msg_txt, sizeof(msg_txt), "Connect to \"%s\"?", params.ssid);
    msgbox = create_msgbox(lv_scr_act(), "NFC WiFi", msg_txt, btns,
                           goto_connect_wifi_event_cb, &params);
}

#if defined(ARDUINO) && defined(USING_ST25R3916)
void ui_nfc_on_reader_event(LilyGoNfcEvent event, const LilyGoNfcReaderResult &result)
{
    update_result_view(event, result);
}
#endif

static void stop_nfc_page()
{
    if (nfc_page_running) {
        hw_stop_nfc_discovery();
        nfc_page_running = false;
    }
}

static void back_event_handler(lv_event_t *e)
{
    stop_nfc_page();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    state_value = NULL;
    uid_value = NULL;
    tech_value = NULL;
    ndef_value = NULL;
    record_body = NULL;
    raw_value = NULL;
    hint_value = NULL;
    menu_show();
}

void ui_nfc_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC Reader", back_event_handler);

    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    state_value = create_value_label(status_card, LV_SYMBOL_REFRESH, "State", "Starting");
    uid_value = create_value_label(status_card, LV_SYMBOL_LIST, "ID", "--");
    tech_value = create_value_label(status_card, LV_SYMBOL_SETTINGS, "Card", "--");
    ndef_value = create_value_label(status_card, LV_SYMBOL_OK, "NDEF", "--");

    lv_obj_t *record_card = ui_create_card(page_container, "Records");
    record_body = lv_obj_create(record_card);
    lv_obj_set_width(record_body, LV_PCT(100));
    lv_obj_set_height(record_body, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(record_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(record_body, 0, 0);
    lv_obj_set_style_shadow_width(record_body, 0, 0);
    lv_obj_set_style_pad_all(record_body, 0, 0);
    lv_obj_set_style_pad_row(record_body, 4, 0);
    lv_obj_set_flex_flow(record_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(record_body, LV_OBJ_FLAG_SCROLLABLE);
    clear_records("Waiting for card");

    lv_obj_t *raw_card = ui_create_card(page_container, "Raw NDEF");
    raw_value = create_value_label(raw_card, LV_SYMBOL_LIST, "Hex", "--");

    lv_obj_t *hint_card = ui_create_card(page_container, "Hint");
    hint_value = create_value_label(hint_card, LV_SYMBOL_WIFI, "Place", NFC_TIPS_STRING);

    bool nfc_started = hw_start_nfc_discovery();
    nfc_page_running = nfc_started;
    if (!nfc_started) {
        set_label_text(state_value, "Start failed");
        set_label_text(ndef_value, "NFC init failed");
        set_label_text(hint_value, "NFC init failed. Check SD/SPI sharing, then re-enter.");
        clear_records("NFC init failed");
    }
}

void ui_nfc_exit(lv_obj_t *parent)
{
    (void)parent;
    stop_nfc_page();
}

app_t ui_nfc_main = {
    .setup_func_cb = ui_nfc_enter,
    .exit_func_cb  = ui_nfc_exit,
    .user_data     = nullptr,
};

#else

static lv_obj_t *page_container = NULL;

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    menu_show();
}

void ui_nfc_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC", back_event_handler);
    lv_obj_t *card = ui_create_card(page_container, NULL);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    LV_IMG_DECLARE(img_nfc_bg);
    lv_obj_t *img_obj = lv_image_create(card);
    lv_image_set_src(img_obj, &img_nfc_bg);
    lv_obj_set_style_image_recolor(img_obj, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_image_recolor_opa(img_obj, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(card);
    lv_obj_set_width(label, lv_pct(90));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_label_set_text(label, NFC_TIPS_STRING);
}

void ui_nfc_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_nfc_main = {
    .setup_func_cb = ui_nfc_enter,
    .exit_func_cb  = ui_nfc_exit,
    .user_data     = nullptr,
};

#endif
