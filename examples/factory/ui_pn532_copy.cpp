/**
 * @file      ui_pn532_copy.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-26
 *
 * Copy NDEF user data between NFC Forum Type 2 tags.
 */
#include "ui_define.h"

#if defined(ARDUINO) && \
    (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && \
    !defined(EXCLUDE_PN532)

static lv_obj_t *page_container = nullptr;
static lv_obj_t *source_label = nullptr;
static lv_obj_t *target_label = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *copy_button = nullptr;
static lv_obj_t *wait_msgbox = nullptr;
static lv_timer_t *copy_timer = nullptr;

#define PN532_COPY_BUFFER_SIZE 1024
#define PN532_COPY_CARD_REMOVED_READS 3

static uint8_t source_ndef[PN532_COPY_BUFFER_SIZE] = {};
static size_t source_ndef_length = 0;
static uint8_t source_uid[10] = {};
static uint8_t source_uid_length = 0;
static uint8_t removed_reads = 0;

enum copy_state_t {
    COPY_IDLE,
    COPY_WAIT_SOURCE,
    COPY_WAIT_SOURCE_REMOVAL,
    COPY_WAIT_TARGET,
};

static copy_state_t copy_state = COPY_IDLE;

static void set_status(const char *text, lv_color_t color = UI_COLOR_TEXT_SECONDARY)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text ? text : "--");
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void set_source_info(const char *text)
{
    if (source_label) lv_label_set_text(source_label, text ? text : "Source: --");
}

static void set_target_info(const char *text)
{
    if (target_label) lv_label_set_text(target_label, text ? text : "Target: --");
}

static void format_uid(const uint8_t *uid, uint8_t uid_length, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';
    size_t offset = 0;
    for (uint8_t i = 0; i < uid_length && offset + 4 < out_len; ++i) {
        offset += snprintf(out + offset, out_len - offset, i ? " %02X" : "%02X", uid[i]);
    }
}

static bool read_type2_capability(Adafruit_PN532 &nfc, uint16_t *capacity, bool *writable)
{
    if (!capacity || !writable) return false;

    uint8_t cc[4] = {};
    if (!nfc.ntag2xx_ReadPage(3, cc)) return false;
    if (cc[0] != 0xE1 && cc[0] != 0xE2 && cc[0] != 0xE3) return false;
    if (cc[2] == 0) return false;

    *capacity = (uint16_t)cc[2] * 8U;
    *writable = (cc[3] & 0x0F) == 0x00;
    return true;
}

static bool find_ndef_tlv(const uint8_t *data, size_t data_length,
                          size_t *ndef_offset, size_t *ndef_end)
{
    if (!data || !ndef_offset || !ndef_end) return false;

    size_t offset = 0;
    while (offset < data_length) {
        const size_t tlv_offset = offset;
        const uint8_t type = data[offset++];
        if (type == 0x00) continue;
        if (type == 0xFE || offset >= data_length) return false;

        size_t length = data[offset++];
        if (length == 0xFF) {
            if (offset + 2 > data_length) return false;
            length = ((size_t)data[offset] << 8) | data[offset + 1];
            offset += 2;
        }
        if (length > data_length - offset) return false;

        if (type == 0x03) {
            *ndef_offset = tlv_offset;
            *ndef_end = offset + length;
            if (*ndef_end < data_length && data[*ndef_end] == 0xFE) ++(*ndef_end);
            return true;
        }
        offset += length;
    }
    return false;
}

static bool read_type2_ndef(Adafruit_PN532 &nfc, uint16_t capacity,
                            uint8_t *out, size_t out_size, size_t *out_length)
{
    if (!out || !out_length || capacity == 0 || capacity > PN532_COPY_BUFFER_SIZE) {
        return false;
    }

    const uint16_t page_count = (capacity + 3U) / 4U;
    if (page_count > 222) return false;

    uint8_t data[PN532_COPY_BUFFER_SIZE] = {};
    size_t loaded = 0;
    for (uint16_t page = 0; page < page_count; ++page) {
        uint8_t page_data[4] = {};
        if (!nfc.ntag2xx_ReadPage((uint8_t)(4 + page), page_data)) return false;
        const size_t count = min((size_t)4, (size_t)capacity - loaded);
        memcpy(data + loaded, page_data, count);
        loaded += count;
    }

    size_t ndef_offset = 0;
    size_t ndef_end = 0;
    if (!find_ndef_tlv(data, loaded, &ndef_offset, &ndef_end)) return false;
    const size_t length = ndef_end - ndef_offset;
    if (length == 0 || length > out_size) return false;

    memcpy(out, data + ndef_offset, length);
    *out_length = length;
    return true;
}

static bool write_type2_ndef(Adafruit_PN532 &nfc, const uint8_t *data,
                             size_t data_length, uint16_t capacity)
{
    if (!data || data_length == 0 || capacity == 0 || data_length > capacity) return false;

    const uint16_t page_count = (data_length + 3U) / 4U;
    if (page_count > 222) return false;

    // Write the payload first and publish page 4 last, so partial writes do not
    // advertise a new NDEF message before its contents are present.
    for (int16_t page = (int16_t)page_count - 1; page >= 1; --page) {
        uint8_t page_data[4] = {};
        const size_t offset = (size_t)page * 4;
        const size_t count = min((size_t)4, data_length - offset);
        memcpy(page_data, data + offset, count);
        if (!nfc.ntag2xx_WritePage((uint8_t)(4 + page), page_data)) return false;
    }

    uint8_t first_page[4] = {};
    memcpy(first_page, data, min((size_t)4, data_length));
    if (!nfc.ntag2xx_WritePage(4, first_page)) return false;

    for (uint16_t page = 0; page < page_count; ++page) {
        uint8_t actual[4] = {};
        if (!nfc.ntag2xx_ReadPage((uint8_t)(4 + page), actual)) return false;
        const size_t offset = (size_t)page * 4;
        const size_t count = min((size_t)4, data_length - offset);
        if (memcmp(actual, data + offset, count) != 0) return false;
    }
    return true;
}

static bool detect_card(Adafruit_PN532 &nfc, uint8_t *uid, uint8_t *uid_length,
                        uint16_t timeout = 1200)
{
    if (!uid || !uid_length) return false;
    memset(uid, 0, 10);
    *uid_length = 0;
    return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uid_length, timeout);
}

static bool same_uid(const uint8_t *uid, uint8_t uid_length)
{
    return uid_length == source_uid_length && uid_length > 0 &&
           memcmp(uid, source_uid, uid_length) == 0;
}

static bool restart_reader(Adafruit_PN532 &nfc)
{
    if (!nfc.begin()) return false;
    return nfc.SAMConfig();
}

static void set_waiting_ui(bool waiting)
{
    if (!copy_button) return;
    if (waiting) {
        lv_obj_add_state(copy_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(copy_button, LV_STATE_DISABLED);
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

static void finish_copy_attempt()
{
    if (copy_timer) {
        lv_timer_delete(copy_timer);
        copy_timer = nullptr;
    }
    close_wait_msgbox();
    copy_state = COPY_IDLE;
    removed_reads = 0;
    set_waiting_ui(false);
}

static void cancel_copy_cb(lv_event_t *event)
{
    (void)event;
    finish_copy_attempt();
    set_status("Copy cancelled", UI_COLOR_WARNING);
}

static void show_wait_msgbox(const char *title, const char *message)
{
    wait_msgbox = create_msgbox(lv_scr_act(), title, message, nullptr,
                                cancel_copy_cb, nullptr);
}

static void begin_target_wait(Adafruit_PN532 &nfc)
{
    if (!restart_reader(nfc)) {
        finish_copy_attempt();
        set_status("PN532 reader reset failed", UI_COLOR_WARNING);
        return;
    }
    copy_state = COPY_WAIT_TARGET;
    set_target_info("Target: waiting for card");
    set_status("Place a different writable card", UI_COLOR_ACCENT);
    show_wait_msgbox("NFC Copy", "Place the target card near the NFC area.\n"
                     "The target must be a writable Type 2 tag.");
}

static void read_source_card(Adafruit_PN532 &nfc, const uint8_t *uid, uint8_t uid_length)
{
    uint16_t capacity = 0;
    bool writable = false;
    if (!read_type2_capability(nfc, &capacity, &writable)) {
        set_source_info("Source: unsupported card");
        finish_copy_attempt();
        set_status("Source is not a Type 2 tag", UI_COLOR_WARNING);
        return;
    }

    size_t ndef_length = 0;
    if (!read_type2_ndef(nfc, capacity, source_ndef, sizeof(source_ndef), &ndef_length)) {
        set_source_info("Source: NDEF data unavailable");
        finish_copy_attempt();
        set_status("Source has no supported NDEF data", UI_COLOR_WARNING);
        return;
    }

    memcpy(source_uid, uid, uid_length);
    source_uid_length = uid_length;
    source_ndef_length = ndef_length;

    char uid_text[32] = {};
    format_uid(uid, uid_length, uid_text, sizeof(uid_text));
    lv_label_set_text_fmt(source_label, "Source: %u NDEF bytes\nUID %s",
                          (unsigned)ndef_length, uid_text);
    copy_state = COPY_WAIT_SOURCE_REMOVAL;
    removed_reads = 0;
    close_wait_msgbox();
    set_status("Source captured. Remove the source card", UI_COLOR_ACCENT);
    show_wait_msgbox("NFC Copy", "Source card captured.\n"
                     "Remove it from the NFC area to continue.");
}

static void write_target_card(Adafruit_PN532 &nfc, const uint8_t *uid, uint8_t uid_length)
{
    uint16_t capacity = 0;
    bool writable = false;
    if (!read_type2_capability(nfc, &capacity, &writable)) {
        set_target_info("Target: unsupported card");
        finish_copy_attempt();
        set_status("Target is not a Type 2 tag", UI_COLOR_WARNING);
        return;
    }

    char uid_text[32] = {};
    format_uid(uid, uid_length, uid_text, sizeof(uid_text));
    if (!writable) {
        lv_label_set_text_fmt(target_label, "Target: read-only\nUID %s", uid_text);
        finish_copy_attempt();
        set_status("Target card is read-only", UI_COLOR_WARNING);
        return;
    }
    if (source_ndef_length > capacity) {
        lv_label_set_text_fmt(target_label, "Target: %u bytes\nUID %s",
                              (unsigned)capacity, uid_text);
        finish_copy_attempt();
        set_status("Target card is too small", UI_COLOR_WARNING);
        return;
    }

    lv_label_set_text_fmt(target_label, "Target: %u bytes writable\nUID %s",
                          (unsigned)capacity, uid_text);
    set_status("Writing NDEF data...", UI_COLOR_ACCENT);
    const bool written = write_type2_ndef(nfc, source_ndef, source_ndef_length, capacity);
    finish_copy_attempt();
    if (written) {
        set_status("NDEF data copied successfully", UI_COLOR_ACCENT);
    } else {
        set_status("Copy or verification failed", UI_COLOR_WARNING);
    }
}

static void copy_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!copy_timer || copy_state == COPY_IDLE) return;

    Adafruit_PN532 &nfc = instance.getNFC();
    uint8_t uid[10] = {};
    uint8_t uid_length = 0;
    const bool found = detect_card(nfc, uid, &uid_length, 100);

    if (copy_state == COPY_WAIT_SOURCE) {
        if (!found) return;
        set_status("Reading source NDEF data...", UI_COLOR_ACCENT);
        read_source_card(nfc, uid, uid_length);
        return;
    }

    if (copy_state == COPY_WAIT_SOURCE_REMOVAL) {
        if (found) {
            removed_reads = 0;
            return;
        }
        if (++removed_reads >= PN532_COPY_CARD_REMOVED_READS) {
            close_wait_msgbox();
            begin_target_wait(nfc);
        }
        return;
    }

    if (copy_state == COPY_WAIT_TARGET) {
        if (!found) return;
        if (same_uid(uid, uid_length)) {
            set_status("Place a different target card", UI_COLOR_WARNING);
            return;
        }
        close_wait_msgbox();
        set_status("Target card detected. Checking...", UI_COLOR_ACCENT);
        write_target_card(nfc, uid, uid_length);
    }
}

static void copy_button_cb(lv_event_t *event)
{
    (void)event;
    if (!copy_button || copy_state != COPY_IDLE) return;

    source_ndef_length = 0;
    source_uid_length = 0;
    removed_reads = 0;
    set_source_info("Source: waiting for card");
    set_target_info("Target: --");
    set_waiting_ui(true);
    set_status("Resetting PN532...", UI_COLOR_ACCENT);

    if (!(instance.getDeviceProbe() & HW_NFC_ONLINE) && !instance.initNFC()) {
        finish_copy_attempt();
        set_status("PN532 reader unavailable", UI_COLOR_WARNING);
        return;
    }

    Adafruit_PN532 &nfc = instance.getNFC();
    if (!restart_reader(nfc)) {
        finish_copy_attempt();
        set_status("PN532 reader reset failed", UI_COLOR_WARNING);
        return;
    }

    copy_state = COPY_WAIT_SOURCE;
    set_status("Place the source card near the NFC area", UI_COLOR_ACCENT);
    copy_timer = lv_timer_create(copy_tick, 180, nullptr);
    if (!copy_timer) {
        finish_copy_attempt();
        set_status("Unable to wait for source card", UI_COLOR_WARNING);
        return;
    }
    show_wait_msgbox("NFC Copy", "Place the source card near the NFC area.\n"
                     "Its NDEF data will be captured.");
    if (!wait_msgbox) {
        finish_copy_attempt();
        set_status("Unable to show card prompt", UI_COLOR_WARNING);
    }
}

static void close_page()
{
    finish_copy_attempt();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    source_label = nullptr;
    target_label = nullptr;
    status_label = nullptr;
    copy_button = nullptr;
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page();
    ui_nfc_child_back();
}

static void ui_pn532_copy_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC Copy Tag", back_event_handler);
    set_low_power_mode_flag(false);

    lv_obj_t *card = ui_create_card(page_container, "Copy NDEF Data");
    lv_obj_t *source_row = ui_create_card_info(card, LV_SYMBOL_DOWNLOAD,
                                                "Source", "Source: --");
    source_label = lv_obj_get_child(source_row, lv_obj_get_child_count(source_row) - 1);
    lv_obj_set_style_text_align(source_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(source_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *target_row = ui_create_card_info(card, LV_SYMBOL_UPLOAD,
                                                "Target", "Target: --");
    target_label = lv_obj_get_child(target_row, lv_obj_get_child_count(target_row) - 1);
    lv_obj_set_style_text_align(target_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(target_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *status_row = ui_create_card_info(card, LV_SYMBOL_REFRESH,
                                                "Status", "Ready");
    status_label = lv_obj_get_child(status_row, lv_obj_get_child_count(status_row) - 1);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *button_row = ui_create_card_button(card, LV_SYMBOL_COPY,
                                                 "Copy", "Copy tag", copy_button_cb);
    copy_button = lv_obj_get_child(button_row, lv_obj_get_child_count(button_row) - 1);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Copies NDEF user data between writable Type 2 tags.\n"
                           "UID, CC, lock bytes and protected areas are not copied.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
}

static void ui_pn532_copy_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page();
}

app_t ui_pn532_copy_main = {
    .setup_func_cb = ui_pn532_copy_enter,
    .exit_func_cb = ui_pn532_copy_exit,
    .user_data = nullptr,
};

#endif
