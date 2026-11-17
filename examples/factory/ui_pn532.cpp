/**
 * @file      ui_pn532.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-07
 *
 */
#include "ui_define.h"

#if defined(ARDUINO) && (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && !defined(EXCLUDE_PN532)

static lv_obj_t *page_container = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *firmware_label = nullptr;
static lv_obj_t *uid_label = nullptr;
static lv_obj_t *ndef_label = nullptr;
static lv_obj_t *count_label = nullptr;
static lv_obj_t *scan_button_label = nullptr;
static lv_timer_t *scan_timer = nullptr;
static lv_timer_t *second_beep_timer = nullptr;
static bool buzzer_used = false;
static uint32_t read_count = 0;
static uint8_t last_uid[7] = {};
static uint8_t last_uid_length = 0;
static uint8_t missed_reads = 0;

#define PN532_BEEP_FREQUENCY_HZ  1000
#define PN532_BEEP_DURATION_MS   100
#define PN532_BEEP_GAP_MS        180
#define PN532_CARD_REMOVED_READS 3
#define PN532_NDEF_BUFFER_SIZE   256

static lv_obj_t *create_label(const char *text)
{
    lv_obj_t *label = lv_label_create(page_container);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(label, text);
    return label;
}

static void stop_scan()
{
    if (scan_timer) {
        lv_timer_delete(scan_timer);
        scan_timer = nullptr;
    }
    if (scan_button_label) lv_label_set_text(scan_button_label, LV_SYMBOL_PLAY " Start");
}

static void second_beep_cb(lv_timer_t *timer)
{
    second_beep_timer = nullptr;
#if defined(PN532_BUZZER) && PN532_BUZZER >= 0
    tone(PN532_BUZZER, PN532_BEEP_FREQUENCY_HZ, PN532_BEEP_DURATION_MS);
#endif
    lv_timer_delete(timer);
}

static void beep_for_card()
{
    if (second_beep_timer) {
        lv_timer_delete(second_beep_timer);
        second_beep_timer = nullptr;
    }
    buzzer_used = true;
#if defined(PN532_BUZZER) && PN532_BUZZER >= 0
    tone(PN532_BUZZER, PN532_BEEP_FREQUENCY_HZ, PN532_BEEP_DURATION_MS);
#endif
    second_beep_timer = lv_timer_create(second_beep_cb, PN532_BEEP_GAP_MS, nullptr);
}

static bool parse_text_record(const uint8_t *record, size_t record_length,
                              char *text, size_t text_size)
{
    if (!record || !text || text_size == 0 || record_length < 3) return false;
    text[0] = '\0';

    const uint8_t header = record[0];
    const uint8_t type_length = record[1];
    size_t offset = 2;
    uint32_t payload_length = 0;
    if (header & 0x10) {
        if (offset >= record_length) return false;
        payload_length = record[offset++];
    } else {
        if (offset + 4 > record_length) return false;
        payload_length = ((uint32_t)record[offset] << 24) |
                         ((uint32_t)record[offset + 1] << 16) |
                         ((uint32_t)record[offset + 2] << 8) |
                         record[offset + 3];
        offset += 4;
    }

    uint8_t id_length = 0;
    if (header & 0x08) {
        if (offset >= record_length) return false;
        id_length = record[offset++];
    }
    if (offset + type_length + id_length > record_length ||
            payload_length > record_length - offset - type_length - id_length) {
        return false;
    }
    if ((header & 0x07) != 0x01 || type_length != 1 ||
            record[offset] != 'T') {
        return false;
    }

    const size_t payload_offset = offset + type_length + id_length;
    if (payload_length < 1) return false;
    const uint8_t status = record[payload_offset];
    if (status & 0x80) return false; // NFC Write emits UTF-8 text records.
    const size_t language_length = status & 0x3F;
    if (language_length + 1 > payload_length) return false;

    const size_t text_length = payload_length - language_length - 1;
    const size_t copy_length = min(text_length, text_size - 1);
    memcpy(text, record + payload_offset + 1 + language_length, copy_length);
    text[copy_length] = '\0';
    return copy_length > 0;
}

static bool read_ndef_text(Adafruit_PN532 &nfc, char *text, size_t text_size)
{
    if (!text || text_size == 0) return false;
    text[0] = '\0';

    uint8_t capability[4] = {};
    if (!nfc.ntag2xx_ReadPage(3, capability)) return false;
    if (capability[0] != 0xE1 && capability[0] != 0xE2 && capability[0] != 0xE3) {
        return false;
    }

    const size_t capacity = (size_t)capability[2] * 8U;
    const size_t read_limit = min(capacity, (size_t)PN532_NDEF_BUFFER_SIZE);
    if (read_limit == 0) return false;

    uint8_t data[PN532_NDEF_BUFFER_SIZE] = {};
    size_t loaded = 0;
    size_t required = 0;
    for (uint8_t page = 0; loaded < read_limit; ++page) {
        uint8_t page_data[4] = {};
        if (!nfc.ntag2xx_ReadPage((uint8_t)(4 + page), page_data)) return false;
        const size_t count = min((size_t)4, read_limit - loaded);
        memcpy(data + loaded, page_data, count);
        loaded += count;

        size_t offset = 0;
        while (offset < loaded) {
            const uint8_t type = data[offset];
            if (type == 0x00) {
                ++offset;
                continue;
            }
            if (type == 0xFE) break;
            if (type != 0x03 || offset + 2 > loaded) break;

            size_t header_length = 2;
            uint32_t message_length = data[offset + 1];
            if (message_length == 0xFF) {
                if (offset + 4 > loaded) break;
                header_length = 4;
                message_length = ((uint32_t)data[offset + 2] << 8) |
                                 data[offset + 3];
            }
            if (message_length > sizeof(data) - offset - header_length) return false;
            required = offset + header_length + message_length;
            break;
        }
        if (required != 0 && loaded >= required) break;
    }

    if (required == 0 || required > loaded) return false;
    size_t tlv_offset = 0;
    while (tlv_offset < required && data[tlv_offset] == 0x00) ++tlv_offset;
    if (tlv_offset + 2 > required || data[tlv_offset] != 0x03) return false;

    size_t header_length = 2;
    size_t message_length = data[tlv_offset + 1];
    if (message_length == 0xFF) {
        if (tlv_offset + 4 > required) return false;
        header_length = 4;
        message_length = ((size_t)data[tlv_offset + 2] << 8) |
                         data[tlv_offset + 3];
    }
    if (tlv_offset + header_length + message_length > required) return false;
    return parse_text_record(data + tlv_offset + header_length,
                             message_length, text, text_size);
}

static void scan_tick(lv_timer_t *timer)
{
    (void)timer;
    // ISO14443A also permits 10-byte UIDs; reserve space before rejecting them.
    // Adafruit PN532 1.3.4 only reads enough response data for 4/7-byte UIDs.
    uint8_t uid[10] = {};
    uint8_t uid_length = 0;
    const bool found = instance.getNFC().readPassiveTargetID(PN532_MIFARE_ISO14443A,
                       uid, &uid_length, 150);
    if (!found) {
        if (missed_reads < PN532_CARD_REMOVED_READS) ++missed_reads;
        if (missed_reads >= PN532_CARD_REMOVED_READS) {
            last_uid_length = 0;
            lv_label_set_text(ndef_label, "NDEF text: --");
        }
        lv_label_set_text(status_label, "Waiting for card");
        return;
    }
    if (uid_length != 4 && uid_length != 7) {
        stop_scan();
        lv_label_set_text(status_label, "Invalid UID response");
        return;
    }

    missed_reads = 0;
    const bool new_card = uid_length != last_uid_length ||
                          memcmp(uid, last_uid, uid_length) != 0;
    if (new_card) {
        memcpy(last_uid, uid, uid_length);
        last_uid_length = uid_length;
        ++read_count;
        lv_label_set_text_fmt(count_label, "Reads: %lu", (unsigned long)read_count);
        beep_for_card();

        char ndef_text[PN532_NDEF_BUFFER_SIZE] = {};
        if (read_ndef_text(instance.getNFC(), ndef_text, sizeof(ndef_text))) {
            lv_label_set_text_fmt(ndef_label, "NDEF text:\n%s", ndef_text);
        } else {
            lv_label_set_text(ndef_label, "NDEF text: unavailable");
        }
    }

    char uid_text[32];
    size_t offset = 0;
    for (uint8_t i = 0; i < uid_length; ++i) {
        offset += snprintf(uid_text + offset, sizeof(uid_text) - offset,
                           i ? " %02X" : "%02X", uid[i]);
    }
    lv_label_set_text_fmt(uid_label, "UID (%u bytes)\n%s", uid_length, uid_text);
    lv_label_set_text(status_label, "Card detected");
}

static void start_scan()
{
    if (scan_timer) return;
    if (!(instance.getDeviceProbe() & HW_NFC_ONLINE) && !instance.initNFC()) {
        lv_label_set_text(status_label, "Reader unavailable");
        return;
    }
    const uint32_t version = instance.getNFCFirmwareVersion();
    lv_label_set_text_fmt(firmware_label, "Firmware: %u.%u",
                          (unsigned)((version >> 16) & 0xFF),
                          (unsigned)((version >> 8) & 0xFF));
    last_uid_length = 0;
    missed_reads = 0;
    scan_timer = lv_timer_create(scan_tick, 350, nullptr);
    if (!scan_timer) {
        lv_label_set_text(status_label, "Unable to start reader");
        return;
    }
    lv_label_set_text(status_label, "Waiting for card");
    lv_label_set_text(scan_button_label, LV_SYMBOL_STOP " Stop");
}

static void scan_button_cb(lv_event_t *event)
{
    (void)event;
    if (scan_timer) {
        stop_scan();
        lv_label_set_text(status_label, "Stopped");
    } else {
        start_scan();
    }
}

static void close_page()
{
    stop_scan();
    if (second_beep_timer) {
        lv_timer_delete(second_beep_timer);
        second_beep_timer = nullptr;
    }
    if (buzzer_used) {
#if defined(PN532_BUZZER) && PN532_BUZZER >= 0
        noTone(PN532_BUZZER);
#endif
        buzzer_used = false;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    status_label = firmware_label = uid_label = ndef_label = count_label = scan_button_label = nullptr;
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page();
    ui_nfc_child_back();
}

static void ui_pn532_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC Read", back_event_handler);
    set_low_power_mode_flag(false);
    read_count = 0;
    firmware_label = create_label("Firmware: --");
    lv_obj_set_style_text_color(firmware_label, UI_COLOR_TEXT_SECONDARY, 0);
    status_label = create_label("Starting");
    lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    uid_label = create_label("UID: --");
    lv_obj_set_style_min_height(uid_label, 42, 0);
    ndef_label = create_label("NDEF text: --");
    lv_obj_set_style_min_height(ndef_label, 50, 0);
    count_label = create_label("Reads: 0");

    lv_obj_t *button = lv_button_create(page_container);
    lv_obj_set_size(button, LV_PCT(100), 36);
    lv_obj_add_style(button, &ui_styles.accent_btn, 0);
    ui_add_accent_focus_style(button);
    scan_button_label = lv_label_create(button);
    lv_label_set_text(scan_button_label, LV_SYMBOL_PLAY " Start");
    lv_obj_center(scan_button_label);
    lv_obj_add_event_cb(button, scan_button_cb, LV_EVENT_CLICKED, nullptr);

    create_label("ISO14443A / 106 kbps");
    start_scan();
}

static void ui_pn532_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page();
}

app_t ui_pn532_main = {
    .setup_func_cb = ui_pn532_enter,
    .exit_func_cb = ui_pn532_exit,
    .user_data = nullptr,
};

#endif
