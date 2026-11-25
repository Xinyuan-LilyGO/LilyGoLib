/**
 * @file      ui_wifi_c5.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-20
 *
 * Factory UI for the ESP32-C5 ESP-AT co-processor on T-Embed-Plus V2.
 */
#include "ui_define.h"

/* The legacy command console is retained for reference but is intentionally
   disabled. Use the dedicated Wi-Fi and BLE scan pages instead. */
#if defined(ARDUINO_T_EMBED_PLUS_V2) && defined(ENABLE_LEGACY_WIFI_C5_APP)

static HardwareSerial c5_at_serial(1);
static lv_obj_t *c5_page = nullptr;
static lv_obj_t *c5_command = nullptr;
static lv_obj_t *c5_response = nullptr;
static lv_obj_t *c5_status = nullptr;
static lv_obj_t *c5_keyboard = nullptr;
static ui_soft_keyboard_lift_t c5_keyboard_lift = {nullptr, nullptr, -1};
static bool c5_serial_started = false;
static constexpr uint8_t kC5RxPin = C5_UART_RX;
static constexpr uint8_t kC5TxPin = C5_UART_TX;

static void c5_set_status(const char *text, lv_color_t color)
{
    if (!c5_status) return;
    lv_label_set_text(c5_status, text);
    lv_obj_set_style_text_color(c5_status, color, 0);
}

static void c5_set_response(const char *text)
{
    if (c5_response) lv_label_set_text(c5_response, text && text[0] ? text : "No response");
}

static void c5_wait_ready_banner(void)
{
    delay(1200);
    while (c5_at_serial.available()) c5_at_serial.read();
}

static void c5_start_serial(void)
{
    if (c5_serial_started) return;
    if (!instance.selectC5Path() || !instance.resetC5()) {
        c5_set_status("C5 path unavailable", lv_color_hex(0xFF4444));
        LILYGO_LOG_E("[WiFiC5] failed to select C5 path");
        return;
    }
    c5_at_serial.begin(115200, SERIAL_8N1, kC5RxPin, kC5TxPin);
    // ESP-AT emits its ready banner after reset. Give UART1 time to finish
    // booting before a user command is sent, then discard that banner.
    c5_wait_ready_banner();
    c5_serial_started = true;
    c5_set_status("UART1 AT link ready", UI_COLOR_ACCENT);
    LILYGO_LOG_I("[WiFiC5] AT UART started at 115200 RX=%d TX=%d",
                  kC5RxPin, kC5TxPin);
}

static void c5_send_command(const char *command)
{
    if (!command || !command[0]) return;
    c5_start_serial();
    if (!c5_serial_started) return;
    while (c5_at_serial.available()) c5_at_serial.read();
    c5_at_serial.print(command);
    c5_at_serial.print("\r\n");

    char response[768] = {0};
    size_t length = 0;
    const uint32_t deadline = millis() + 2500;
    while (millis() < deadline && length + 1 < sizeof(response)) {
        while (c5_at_serial.available() && length + 1 < sizeof(response)) {
            response[length++] = static_cast<char>(c5_at_serial.read());
        }
        if (length && strstr(response, "\r\nOK\r\n")) break;
        delay(5);
    }
    response[length] = '\0';
    c5_set_response(response);
    c5_set_status(length ? "Response received" : "No response: check C5 firmware/link",
                  length ? UI_COLOR_ACCENT : lv_color_hex(0xFF4444));
    LILYGO_LOG_I("[WiFiC5] command=%s response_bytes=%u", command,
                  static_cast<unsigned>(length));
}

static void c5_at_test_event(lv_event_t *event) { (void)event; c5_send_command("AT"); }
static void c5_version_event(lv_event_t *event) { (void)event; c5_send_command("AT+GMR"); }
static void c5_state_event(lv_event_t *event) { (void)event; c5_send_command("AT+CWSTATE?"); }
static void c5_scan_event(lv_event_t *event) { (void)event; c5_send_command("AT+CWLAP"); }

static void c5_custom_command_event(lv_event_t *event)
{
    (void)event;
    c5_send_command(lv_textarea_get_text(c5_command));
}

static void c5_command_input_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED) {
        enable_keyboard();
#if FACTORY_HAS_SOFT_KEYBOARD
        if (c5_keyboard) {
            ui_soft_keyboard_show(c5_keyboard, c5_command, lv_obj_get_parent(c5_command),
                                  &c5_keyboard_lift);
        }
#endif
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
               (code == LV_EVENT_DEFOCUSED && !ui_soft_keyboard_focus_transition_active())) {
#if FACTORY_HAS_SOFT_KEYBOARD
        if (c5_keyboard) ui_soft_keyboard_hide(c5_keyboard, &c5_keyboard_lift);
#endif
        disable_keyboard();
    }
}

static void c5_reset_event(lv_event_t *event)
{
    (void)event;
    if (c5_serial_started) {
        c5_at_serial.end();
        c5_serial_started = false;
    }
    if (!instance.selectC5Path() || !instance.resetC5()) {
        c5_set_status("XL9555 is offline", lv_color_hex(0xFF4444));
        return;
    }
    c5_at_serial.begin(115200, SERIAL_8N1, kC5RxPin, kC5TxPin);
    c5_wait_ready_banner();
    c5_serial_started = true;
    c5_set_status("C5 reset; AT link restarting", UI_COLOR_ACCENT);
    LILYGO_LOG_I("[WiFiC5] C5 reset asserted");
}

static void c5_back_event(lv_event_t *event)
{
    (void)event;
    if (c5_page) {
#if FACTORY_HAS_SOFT_KEYBOARD
        if (c5_keyboard) {
            ui_soft_keyboard_hide(c5_keyboard, &c5_keyboard_lift);
            lv_obj_delete(c5_keyboard);
            c5_keyboard = nullptr;
        }
#endif
        if (c5_serial_started) {
            c5_at_serial.end();
            c5_serial_started = false;
        }
        instance.selectNRF24Path();
        disable_keyboard();
        ui_destroy_app_page(c5_page);
        c5_page = nullptr;
    }
    c5_command = nullptr;
    c5_response = nullptr;
    c5_status = nullptr;
    menu_show();
}

static void ui_wifi_c5_enter(lv_obj_t *parent)
{
    c5_page = ui_create_app_page(parent, "WiFi C5", c5_back_event);

    lv_obj_t *card = ui_create_card(c5_page, "ESP-AT link");
    lv_obj_t *status_row = ui_create_card_info(card, LV_SYMBOL_WIFI, "Status", "Starting...");
    c5_status = lv_obj_get_child(status_row, lv_obj_get_child_count(status_row) - 1);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Reset C5", "Restart", c5_reset_event);
    ui_create_card_button(card, LV_SYMBOL_OK, "AT test", "Send", c5_at_test_event);
    ui_create_card_button(card, LV_SYMBOL_SETTINGS, "Version", "Send", c5_version_event);
    ui_create_card_button(card, LV_SYMBOL_WIFI, "WiFi state", "Send", c5_state_event);
    ui_create_card_button(card, LV_SYMBOL_LIST, "Scan APs", "Send", c5_scan_event);

    card = ui_create_card(c5_page, "Custom AT command");
    c5_command = lv_textarea_create(card);
    lv_obj_set_width(c5_command, LV_PCT(100));
    lv_obj_set_height(c5_command, 38);
    lv_textarea_set_one_line(c5_command, true);
    lv_textarea_set_text(c5_command, "AT+CWJAP?");
    lv_obj_add_event_cb(c5_command, c5_command_input_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(c5_command, c5_command_input_event, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(c5_command, c5_command_input_event, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(c5_command, c5_command_input_event, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_set_style_border_color(c5_command, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_color(c5_command, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    ui_create_card_item(card, LV_SYMBOL_EDIT, "Command", c5_command);
    ui_create_card_button(card, LV_SYMBOL_UPLOAD, "Send", "Send", c5_custom_command_event);

    c5_response = lv_label_create(card);
    lv_label_set_text(c5_response, "No response");
    lv_label_set_long_mode(c5_response, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(c5_response, LV_PCT(100));
    lv_obj_set_height(c5_response, 150);
    lv_obj_set_style_text_color(c5_response, UI_COLOR_TEXT_SECONDARY, 0);
    ui_create_card_item(card, LV_SYMBOL_LIST, "Response", c5_response);

#if FACTORY_HAS_SOFT_KEYBOARD
    c5_keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_style_bg_color(c5_keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(c5_keyboard, LV_OPA_COVER, 0);
    lv_obj_add_flag(c5_keyboard, LV_OBJ_FLAG_HIDDEN);
#endif

    c5_start_serial();
}

static void ui_wifi_c5_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_wifi_c5_main = {
    .setup_func_cb = ui_wifi_c5_enter,
    .exit_func_cb = ui_wifi_c5_exit,
    .user_data = nullptr,
};

#endif
