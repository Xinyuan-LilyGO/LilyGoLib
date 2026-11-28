/**
 * @file      ui_pn532_write.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-26
 *
 * Write a UTF-8 text NDEF record to an NFC Forum Type 2 tag.
 */
#include "ui_define.h"

#if defined(ARDUINO) && \
    (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && \
    !defined(EXCLUDE_PN532)

static lv_obj_t *page_container = nullptr;
static lv_obj_t *text_input = nullptr;
static lv_obj_t *text_dropdown = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *capacity_label = nullptr;
static lv_obj_t *write_button = nullptr;
static lv_obj_t *wait_msgbox = nullptr;
static lv_timer_t *wait_timer = nullptr;
static uint8_t pending_tlv[256] = {};
static size_t pending_tlv_length = 0;
static size_t pending_ndef_length = 0;

#if !defined(ARDUINO_T_EMBED_PLUS) && \
    (defined(USING_TOUCHPAD) || defined(USING_INPUT_DEV_TOUCHPAD) || defined(HAS_TOUCHSCREEN))
#define PN532_WRITE_HAS_SOFT_KEYBOARD 1
#else
#define PN532_WRITE_HAS_SOFT_KEYBOARD 0
#endif

#if PN532_WRITE_HAS_SOFT_KEYBOARD
static lv_obj_t *keyboard = nullptr;
static ui_soft_keyboard_lift_t keyboard_lift = {nullptr, nullptr, -1};
#endif

static void set_status(const char *text, lv_color_t color = UI_COLOR_TEXT_SECONDARY)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text ? text : "--");
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void set_capacity(uint16_t capacity, bool writable)
{
    if (!capacity_label) return;
    if (capacity == 0) {
        lv_label_set_text(capacity_label, "Tag capacity: --");
        return;
    }
    lv_label_set_text_fmt(capacity_label, "Tag capacity: %u bytes (%s)",
                          (unsigned)capacity, writable ? "writable" : "read-only");
}

static void format_uid(const uint8_t *uid, uint8_t uid_length, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    size_t offset = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < uid_length && offset + 4 < out_len; ++i) {
        offset += snprintf(out + offset, out_len - offset, i ? " %02X" : "%02X", uid[i]);
    }
}

static bool build_text_ndef(const char *text, uint8_t *tlv, size_t tlv_size,
                            size_t *tlv_length, size_t *ndef_length)
{
    if (!text || !tlv || !tlv_length || !ndef_length) return false;

    const size_t text_length = strlen(text);
    // A short NDEF record is used so the Type 2 TLV remains portable.
    if (text_length == 0 || text_length > 220) return false;

    const size_t record_length = 7 + text_length;
    const size_t total_length = 2 + record_length + 1; // TLV header + NDEF + terminator
    if (record_length > 0xFF || total_length > tlv_size) return false;

    memset(tlv, 0, total_length);
    tlv[0] = 0x03; // NDEF Message TLV
    tlv[1] = (uint8_t)record_length;
    tlv[2] = 0xD1; // MB, ME, SR, TNF=well-known
    tlv[3] = 0x01; // Type length
    tlv[4] = (uint8_t)(3 + text_length); // status + language + text
    tlv[5] = 'T';
    tlv[6] = 0x02; // UTF-8, two-byte language code
    tlv[7] = 'e';
    tlv[8] = 'n';
    memcpy(tlv + 9, text, text_length);
    tlv[2 + record_length] = 0xFE; // TLV terminator

    *tlv_length = total_length;
    *ndef_length = record_length;
    return true;
}

static bool read_type2_capability(Adafruit_PN532 &nfc, uint16_t *capacity, bool *writable)
{
    uint8_t cc[4] = {};
    if (!nfc.ntag2xx_ReadPage(3, cc)) return false;

    // NFC Forum Type 2 Capability Container starts with E1/E2/E3.
    if (cc[0] != 0xE1 && cc[0] != 0xE2 && cc[0] != 0xE3) return false;
    if (cc[2] == 0) return false;

    *capacity = (uint16_t)cc[2] * 8U;
    // Low nibble 0xF is read-only; 0x0 is the normal read/write state.
    *writable = (cc[3] & 0x0F) == 0x00;
    return true;
}

static bool write_type2_text(Adafruit_PN532 &nfc, const uint8_t *tlv,
                             size_t tlv_length, uint16_t capacity)
{
    if (!tlv || tlv_length == 0 || capacity == 0 || tlv_length > capacity) return false;

    const uint8_t page_count = (uint8_t)((tlv_length + 3) / 4);
    if (page_count > 222) return false; // ntag2xx_WritePage supports pages through 225.

    // Write the payload before the TLV length so an interrupted write leaves the
    // previous NDEF message discoverable instead of advertising partial data.
    for (int16_t page = page_count - 1; page >= 1; --page) {
        uint8_t page_data[4] = {};
        const size_t offset = (size_t)page * 4;
        const size_t count = min((size_t)4, tlv_length - offset);
        memcpy(page_data, tlv + offset, count);
        if (!nfc.ntag2xx_WritePage((uint8_t)(4 + page), page_data)) return false;
    }

    uint8_t first_page[4] = {};
    memcpy(first_page, tlv, min((size_t)4, tlv_length));
    if (!nfc.ntag2xx_WritePage(4, first_page)) return false;

    uint8_t verify[4] = {};
    for (uint8_t page = 0; page < page_count; ++page) {
        if (!nfc.ntag2xx_ReadPage((uint8_t)(4 + page), verify)) return false;
        const size_t offset = (size_t)page * 4;
        const size_t count = min((size_t)4, tlv_length - offset);
        if (memcmp(verify, tlv + offset, count) != 0) return false;
    }
    return true;
}

static bool detect_card(Adafruit_PN532 &nfc, uint8_t *uid, uint8_t *uid_length,
                        uint16_t timeout = 1200)
{
    memset(uid, 0, 10);
    *uid_length = 0;
    return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uid_length, timeout);
}

static bool restart_reader(Adafruit_PN532 &nfc)
{
    // readPassiveTargetID() leaves the PN532 target selected. Reset the reader
    // before every write so a card left on the antenna can be selected again.
    if (!nfc.begin()) return false;
    return nfc.SAMConfig();
}

static const char *selected_text()
{
#if defined(ARDUINO_T_EMBED_PLUS)
    static const char *const presets[] = {"LilyGo", "LilyGoLib", nullptr};
    const uint16_t selected = text_dropdown ? lv_dropdown_get_selected(text_dropdown) : 0;
    if (selected < 2) return presets[selected];
    const char *device_name = instance.getName();
    return device_name && device_name[0] ? device_name : "LilyGo T-Embed-Plus";
#else
    return text_input ? lv_textarea_get_text(text_input) : "";
#endif
}

static void set_waiting_ui(bool waiting)
{
    if (write_button) {
        if (waiting) {
            lv_obj_add_state(write_button, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(write_button, LV_STATE_DISABLED);
        }
    }
}

static void close_wait_msgbox()
{
    if (wait_msgbox) {
        lv_obj_t *msgbox = wait_msgbox;
        wait_msgbox = nullptr;
        destroy_msgbox(msgbox);
    }
}

static void finish_write_attempt()
{
    if (wait_timer) {
        lv_timer_delete(wait_timer);
        wait_timer = nullptr;
    }
    close_wait_msgbox();
    pending_tlv_length = 0;
    pending_ndef_length = 0;
    set_waiting_ui(false);
}

static void cancel_wait_cb(lv_event_t *event)
{
    (void)event;
    finish_write_attempt();
    set_capacity(0, false);
    set_status("Write cancelled", UI_COLOR_WARNING);
}

static void show_wait_msgbox()
{
    wait_msgbox = create_msgbox(lv_scr_act(), "NFC Write",
                                "Place card near the NFC area.\n"
                                "Waiting for an NTAG or Ultralight card.",
                                nullptr, cancel_wait_cb, nullptr);
}

static void write_detected_card(Adafruit_PN532 &nfc, const uint8_t *uid,
                                uint8_t uid_length)
{
    char uid_text[32] = {};
    format_uid(uid, uid_length, uid_text, sizeof(uid_text));

    uint16_t capacity = 0;
    bool writable = false;
    if (!read_type2_capability(nfc, &capacity, &writable)) {
        set_capacity(0, false);
        char message[96];
        snprintf(message, sizeof(message), "Unsupported card (UID %s)", uid_text);
        set_status(message, UI_COLOR_WARNING);
        finish_write_attempt();
        return;
    }
    set_capacity(capacity, writable);
    if (!writable) {
        set_status("Card is read-only", UI_COLOR_WARNING);
        finish_write_attempt();
        return;
    }
    if (pending_tlv_length > capacity) {
        set_status("Text does not fit on this card", UI_COLOR_WARNING);
        finish_write_attempt();
        return;
    }

    set_status("Writing NDEF text...", UI_COLOR_ACCENT);
    if (!write_type2_text(nfc, pending_tlv, pending_tlv_length, capacity)) {
        set_status("Write or verification failed", UI_COLOR_WARNING);
    } else {
        char message[128];
        snprintf(message, sizeof(message), "Text written (%u bytes)\nUID %s",
                 (unsigned)pending_ndef_length, uid_text);
        set_status(message, UI_COLOR_ACCENT);
    }
    finish_write_attempt();
}

static void wait_for_card_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!wait_timer || pending_tlv_length == 0) return;

    uint8_t uid[10] = {};
    uint8_t uid_length = 0;
    Adafruit_PN532 &nfc = instance.getNFC();
    if (!detect_card(nfc, uid, &uid_length, 100)) return;

    lv_timer_delete(wait_timer);
    wait_timer = nullptr;
    close_wait_msgbox();
    set_status("Card detected. Checking...", UI_COLOR_ACCENT);
    write_detected_card(nfc, uid, uid_length);
}

static void write_button_cb(lv_event_t *event)
{
    (void)event;
    if (!write_button) return;

#if PN532_WRITE_HAS_SOFT_KEYBOARD
    ui_soft_keyboard_hide(keyboard, &keyboard_lift);
    disable_keyboard();
#endif

    const char *text = selected_text();
    if (!text || !text[0]) {
        set_status("Enter text before writing", UI_COLOR_WARNING);
        return;
    }

    size_t tlv_length = 0;
    size_t ndef_length = 0;
    if (!build_text_ndef(text, pending_tlv, sizeof(pending_tlv), &tlv_length, &ndef_length)) {
        set_status("Text is empty or too long", UI_COLOR_WARNING);
        return;
    }

    pending_tlv_length = tlv_length;
    pending_ndef_length = ndef_length;
    set_capacity(0, false);
    set_waiting_ui(true);
    set_status("Resetting PN532...", UI_COLOR_ACCENT);
    if (!(instance.getDeviceProbe() & HW_NFC_ONLINE) && !instance.initNFC()) {
        set_status("PN532 reader unavailable", UI_COLOR_WARNING);
        finish_write_attempt();
        return;
    }

    Adafruit_PN532 &nfc = instance.getNFC();
    if (!restart_reader(nfc)) {
        set_status("PN532 reader reset failed", UI_COLOR_WARNING);
        finish_write_attempt();
        return;
    }

    set_status("Waiting for NTAG or Ultralight card", UI_COLOR_ACCENT);
    wait_timer = lv_timer_create(wait_for_card_cb, 180, nullptr);
    if (!wait_timer) {
        set_status("Unable to wait for card", UI_COLOR_WARNING);
        finish_write_attempt();
        return;
    }
    show_wait_msgbox();
    if (!wait_msgbox) {
        set_status("Unable to show card prompt", UI_COLOR_WARNING);
        finish_write_attempt();
    }
}

#if PN532_WRITE_HAS_SOFT_KEYBOARD
static lv_indev_type_t event_indev_type(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (!indev) indev = lv_indev_active();
    return indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
}

static void text_input_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED && event_indev_type(event) == LV_INDEV_TYPE_POINTER) {
        enable_keyboard();
        ui_soft_keyboard_show(keyboard, text_input, lv_obj_get_parent(text_input), &keyboard_lift);
    } else if (code == LV_EVENT_FOCUSED) {
        lv_obj_scroll_to_view_recursive(text_input, LV_ANIM_ON);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED) {
        ui_soft_keyboard_hide(keyboard, &keyboard_lift);
        disable_keyboard();
    }
}
#endif

static void close_page()
{
    finish_write_attempt();
#if PN532_WRITE_HAS_SOFT_KEYBOARD
    ui_soft_keyboard_hide(keyboard, &keyboard_lift);
    disable_keyboard();
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = nullptr;
    }
#endif
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    text_input = nullptr;
    text_dropdown = nullptr;
    status_label = nullptr;
    capacity_label = nullptr;
    write_button = nullptr;
    wait_msgbox = nullptr;
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page();
    ui_nfc_child_back();
}

static void ui_pn532_write_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC Write", back_event_handler);
    set_low_power_mode_flag(false);

    lv_obj_t *card = ui_create_card(page_container, "Text NDEF");
#if defined(ARDUINO_T_EMBED_PLUS)
    static char preset_options[96];
    const char *device_name = instance.getName();
    snprintf(preset_options, sizeof(preset_options), "LilyGo\nLilyGoLib\n%s",
             device_name && device_name[0] ? device_name : "LilyGo T-Embed-Plus");
    lv_obj_t *preset_row = ui_create_card_dropdown(card, LV_SYMBOL_EDIT, "Text",
                                                     preset_options, 0, nullptr);
    text_dropdown = lv_obj_get_child(preset_row, lv_obj_get_child_count(preset_row) - 1);
#else
    lv_obj_t *input_parent = lv_obj_create(card);
    text_input = lv_textarea_create(input_parent);
    lv_obj_set_size(text_input, 155, 52);
    lv_textarea_set_placeholder_text(text_input, "Text to write");
    lv_textarea_set_max_length(text_input, 120);
    lv_textarea_set_text_selection(text_input, false);
    lv_textarea_set_cursor_click_pos(text_input, false);
    ui_prepare_textarea_for_encoder(text_input);
#if PN532_WRITE_HAS_SOFT_KEYBOARD
    lv_obj_add_event_cb(text_input, text_input_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(text_input, text_input_event_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(text_input, text_input_event_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(text_input, text_input_event_cb, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(text_input, text_input_event_cb, LV_EVENT_DEFOCUSED, nullptr);
#endif
    ui_create_card_item(card, LV_SYMBOL_EDIT, "Text", text_input);
#endif

    lv_obj_t *capacity_row = ui_create_card_info(card, LV_SYMBOL_SETTINGS,
                                                  "Capacity", "Tag capacity: --");
    capacity_label = lv_obj_get_child(capacity_row, lv_obj_get_child_count(capacity_row) - 1);
    lv_obj_t *status_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Ready");
    status_label = lv_obj_get_child(status_row, lv_obj_get_child_count(status_row) - 1);
    lv_obj_t *write_row = ui_create_card_button(card, LV_SYMBOL_UPLOAD, "Write", "Write card",
                                                write_button_cb);
    write_button = lv_obj_get_child(write_row, lv_obj_get_child_count(write_row) - 1);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Supports NFC Forum Type 2 tags (NTAG / Ultralight).\n"
                           "MIFARE Classic and locked cards are not modified.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);

#if PN532_WRITE_HAS_SOFT_KEYBOARD
    keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif
}

static void ui_pn532_write_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page();
}

app_t ui_pn532_write_main = {
    .setup_func_cb = ui_pn532_write_enter,
    .exit_func_cb = ui_pn532_write_exit,
    .user_data = nullptr,
};

#endif
