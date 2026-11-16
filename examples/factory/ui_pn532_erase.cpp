/**
 * @file      ui_pn532_erase.cpp
 * @brief     Clear NDEF user data from an NFC Forum Type 2 tag.
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 */
#include "ui_define.h"

#if defined(ARDUINO) && \
    (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && \
    !defined(EXCLUDE_PN532)

static lv_obj_t *page_container = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *capacity_label = nullptr;
static lv_obj_t *erase_button = nullptr;
static lv_obj_t *wait_msgbox = nullptr;
static lv_timer_t *wait_timer = nullptr;

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

static bool detect_card(Adafruit_PN532 &nfc, uint8_t *uid, uint8_t *uid_length,
                        uint16_t timeout = 1200)
{
    memset(uid, 0, 10);
    *uid_length = 0;
    return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uid_length, timeout);
}

static bool restart_reader(Adafruit_PN532 &nfc)
{
    // Clear the target selected by another PN532 app before waiting for a card.
    if (!nfc.begin()) return false;
    return nfc.SAMConfig();
}

static bool read_type2_capability(Adafruit_PN532 &nfc, uint16_t *capacity,
                                  bool *writable)
{
    uint8_t cc[4] = {};
    if (!nfc.ntag2xx_ReadPage(3, cc)) return false;
    if (cc[0] != 0xE1 && cc[0] != 0xE2 && cc[0] != 0xE3) return false;
    if (cc[2] == 0) return false;

    *capacity = (uint16_t)cc[2] * 8U;
    *writable = (cc[3] & 0x0F) == 0x00;
    return true;
}

static bool erase_type2_data(Adafruit_PN532 &nfc, uint16_t capacity)
{
    if (capacity < 4) return false;

    const uint16_t page_count = (capacity + 3U) / 4U;
    if (page_count > 222) return false; // User pages 4..225.

    // Clear the payload first, then publish an empty NDEF message last.
    for (int16_t page = (int16_t)page_count - 1; page >= 1; --page) {
        uint8_t empty_page[4] = {};
        if (!nfc.ntag2xx_WritePage((uint8_t)(4 + page), empty_page)) return false;
    }

    uint8_t empty_ndef[4] = {0x03, 0x00, 0xFE, 0x00};
    if (!nfc.ntag2xx_WritePage(4, empty_ndef)) return false;

    for (uint16_t page = 0; page < page_count; ++page) {
        uint8_t actual[4] = {};
        if (!nfc.ntag2xx_ReadPage((uint8_t)(4 + page), actual)) return false;

        uint8_t expected[4] = {};
        if (page == 0) memcpy(expected, empty_ndef, sizeof(expected));
        const size_t count = min((size_t)4, (size_t)capacity - page * 4U);
        if (memcmp(actual, expected, count) != 0) return false;
    }
    return true;
}

static void close_wait_msgbox()
{
    if (wait_msgbox) {
        lv_obj_t *msgbox = wait_msgbox;
        wait_msgbox = nullptr;
        destroy_msgbox(msgbox);
    }
}

static void set_waiting_ui(bool waiting)
{
    if (!erase_button) return;
    if (waiting) {
        lv_obj_add_state(erase_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(erase_button, LV_STATE_DISABLED);
    }
}

static void finish_erase_attempt()
{
    if (wait_timer) {
        lv_timer_delete(wait_timer);
        wait_timer = nullptr;
    }
    close_wait_msgbox();
    set_waiting_ui(false);
}

static void cancel_wait_cb(lv_event_t *event)
{
    (void)event;
    finish_erase_attempt();
    set_capacity(0, false);
    set_status("Erase cancelled", UI_COLOR_WARNING);
}

static void show_wait_msgbox()
{
    static const char *buttons[] = {"Cancel", ""};
    wait_msgbox = create_msgbox(lv_scr_act(), "NFC Erase",
                                "Place card near the NFC area.\n"
                                "NDEF data will be erased.",
                                buttons, cancel_wait_cb, nullptr);
}

static void erase_detected_card(Adafruit_PN532 &nfc)
{
    uint16_t capacity = 0;
    bool writable = false;
    if (!read_type2_capability(nfc, &capacity, &writable)) {
        set_capacity(0, false);
        set_status("Unsupported card", UI_COLOR_WARNING);
        finish_erase_attempt();
        return;
    }

    set_capacity(capacity, writable);
    if (!writable) {
        set_status("Card is read-only", UI_COLOR_WARNING);
        finish_erase_attempt();
        return;
    }

    set_status("Erasing NDEF data...", UI_COLOR_ACCENT);
    if (erase_type2_data(nfc, capacity)) {
        set_status("NDEF data erased. Card is writable.", UI_COLOR_ACCENT);
    } else {
        set_status("Erase or verification failed", UI_COLOR_WARNING);
    }
    finish_erase_attempt();
}

static void wait_for_card_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!wait_timer) return;

    uint8_t uid[10] = {};
    uint8_t uid_length = 0;
    Adafruit_PN532 &nfc = instance.getNFC();
    if (!detect_card(nfc, uid, &uid_length, 100)) return;

    lv_timer_delete(wait_timer);
    wait_timer = nullptr;
    close_wait_msgbox();
    set_status("Card detected. Checking...", UI_COLOR_ACCENT);
    erase_detected_card(nfc);
}

static void erase_button_cb(lv_event_t *event)
{
    (void)event;
    if (!erase_button || wait_timer) return;

    set_capacity(0, false);
    set_waiting_ui(true);
    set_status("Resetting PN532...", UI_COLOR_ACCENT);
    if (!(instance.getDeviceProbe() & HW_NFC_ONLINE) && !instance.initNFC()) {
        set_status("PN532 reader unavailable", UI_COLOR_WARNING);
        finish_erase_attempt();
        return;
    }

    Adafruit_PN532 &nfc = instance.getNFC();
    if (!restart_reader(nfc)) {
        set_status("PN532 reader reset failed", UI_COLOR_WARNING);
        finish_erase_attempt();
        return;
    }

    set_status("Waiting for NTAG or Ultralight card", UI_COLOR_ACCENT);
    wait_timer = lv_timer_create(wait_for_card_cb, 180, nullptr);
    if (!wait_timer) {
        set_status("Unable to wait for card", UI_COLOR_WARNING);
        finish_erase_attempt();
        return;
    }
    show_wait_msgbox();
    if (!wait_msgbox) {
        set_status("Unable to show card prompt", UI_COLOR_WARNING);
        finish_erase_attempt();
    }
}

static void close_page()
{
    finish_erase_attempt();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    status_label = nullptr;
    capacity_label = nullptr;
    erase_button = nullptr;
    wait_msgbox = nullptr;
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page();
    menu_show();
}

static void ui_pn532_erase_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NFC Erase", back_event_handler);
    set_low_power_mode_flag(false);

    lv_obj_t *card = ui_create_card(page_container, "Erase NDEF");
    lv_obj_t *capacity_row = ui_create_card_info(card, LV_SYMBOL_SETTINGS,
                                                  "Capacity", "Tag capacity: --");
    capacity_label = lv_obj_get_child(capacity_row, lv_obj_get_child_count(capacity_row) - 1);
    lv_obj_t *status_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Ready");
    status_label = lv_obj_get_child(status_row, lv_obj_get_child_count(status_row) - 1);
    lv_obj_t *button_row = ui_create_card_button(card, LV_SYMBOL_TRASH, "Erase", "Erase card",
                                                 erase_button_cb);
    erase_button = lv_obj_get_child(button_row, lv_obj_get_child_count(button_row) - 1);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Clears NDEF user data on writable Type 2 tags.\n"
                           "UID, CC and lock bytes are not modified.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
}

static void ui_pn532_erase_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page();
}

app_t ui_pn532_erase_main = {
    .setup_func_cb = ui_pn532_erase_enter,
    .exit_func_cb = ui_pn532_erase_exit,
    .user_data = nullptr,
};

#endif
