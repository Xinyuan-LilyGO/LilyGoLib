/**
 * @file      ui_pn532.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-07
 *
 */
#include "ui_define.h"

#if defined(ARDUINO) && defined(ARDUINO_TWATCH_BASE) && !defined(EXCLUDE_PN532)

static lv_obj_t *page_container = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *firmware_label = nullptr;
static lv_obj_t *uid_label = nullptr;
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
    tone(PN532_BUZZER, PN532_BEEP_FREQUENCY_HZ, PN532_BEEP_DURATION_MS);
    lv_timer_delete(timer);
}

static void beep_for_card()
{
    if (second_beep_timer) {
        lv_timer_delete(second_beep_timer);
        second_beep_timer = nullptr;
    }
    buzzer_used = true;
    tone(PN532_BUZZER, PN532_BEEP_FREQUENCY_HZ, PN532_BEEP_DURATION_MS);
    second_beep_timer = lv_timer_create(second_beep_cb, PN532_BEEP_GAP_MS, nullptr);
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
        if (missed_reads >= PN532_CARD_REMOVED_READS) last_uid_length = 0;
        lv_label_set_text(status_label, "Waiting for card");
        return;
    }
    if (uid_length != 4 && uid_length != 7) {
        stop_scan();
        lv_label_set_text(status_label, "Invalid UID response");
        return;
    }

    missed_reads = 0;
    if (uid_length != last_uid_length || memcmp(uid, last_uid, uid_length) != 0) {
        memcpy(last_uid, uid, uid_length);
        last_uid_length = uid_length;
        ++read_count;
        lv_label_set_text_fmt(count_label, "Reads: %lu", (unsigned long)read_count);
        beep_for_card();
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
        noTone(PN532_BUZZER);
        buzzer_used = false;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    status_label = firmware_label = uid_label = count_label = scan_button_label = nullptr;
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page();
    menu_show();
}

static void ui_pn532_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "PN532", back_event_handler);
    set_low_power_mode_flag(false);
    read_count = 0;
    firmware_label = create_label("Firmware: --");
    lv_obj_set_style_text_color(firmware_label, UI_COLOR_TEXT_SECONDARY, 0);
    status_label = create_label("Starting");
    lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    uid_label = create_label("UID: --");
    lv_obj_set_style_min_height(uid_label, 42, 0);
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
