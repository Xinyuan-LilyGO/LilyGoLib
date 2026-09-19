/**
 * @file      ui_ble_hid.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-29
 *
 * Composite BLE HID UI for a built-in keyboard and normalized pointer input.
 */

#include <LilyGoLog.h>
#include "ble_hid_helper.h"
#include "ui_define.h"

#if !defined(EXCLUDE_BLE_HID)

static constexpr int32_t BLE_HID_SENS_MIN = 1;
static constexpr int32_t BLE_HID_SENS_MAX = 64;
static constexpr int32_t BLE_HID_SENS_DEFAULT = 8;
static constexpr uint32_t BLE_HID_MOUSE_PERIOD_MS = 10;
static constexpr uint32_t BLE_HID_UI_PERIOD_MS = 250;
static constexpr uint32_t BLE_HID_START_TIMEOUT_MS = 8000;

static lv_obj_t *page_container = nullptr;
static lv_timer_t *mouse_timer = nullptr;
static lv_timer_t *ui_timer = nullptr;
static lv_obj_t *unavailable_msgbox = nullptr;
static lv_obj_t *state_label = nullptr;
static lv_obj_t *keyboard_label = nullptr;
static lv_obj_t *mouse_label = nullptr;
static lv_obj_t *sens_label = nullptr;
static lv_obj_t *key_label = nullptr;
static lv_obj_t *delta_label = nullptr;
static lv_obj_t *button_label = nullptr;
static lv_obj_t *cursor_dot = nullptr;
static int32_t preview_max_x = 0;
static int32_t preview_max_y = 0;
static int32_t preview_x = 0;
static int32_t preview_y = 0;
static int32_t sensitivity = BLE_HID_SENS_DEFAULT;
static float pending_x = 0.0f;
static float pending_y = 0.0f;
static int32_t last_report_x = 0;
static int32_t last_report_y = 0;
static uint32_t motion_count = 0;
static uint32_t key_count = 0;
static uint32_t button_count = 0;
static char key_text[16] = "--";
static char button_text[16] = "--";

static bool keyboard_available()
{
#if FACTORY_HAS_KEYBOARD
    return (hw_get_device_online() & HW_KEYBOARD_ONLINE) != 0;
#else
    return false;
#endif
}

static bool hid_available()
{
    return keyboard_available() || hw_pointer_available();
}

static int8_t clamp_hid_delta(float value)
{
    if (value > 127.0f) return 127;
    if (value < -127.0f) return -127;
    return static_cast<int8_t>(value);
}

static void set_label(lv_obj_t *label, const char *text)
{
    if (label) {
        lv_label_set_text(label, text ? text : "--");
    }
}

static void update_sensitivity_label()
{
    if (sens_label) {
        lv_label_set_text_fmt(sens_label, "%ldx", static_cast<long>(sensitivity));
    }
}

static void update_preview_dot(int8_t delta_x, int8_t delta_y)
{
    if (!cursor_dot) {
        return;
    }

    preview_x += delta_x;
    preview_y += delta_y;
    if (preview_x < -preview_max_x) preview_x = -preview_max_x;
    if (preview_x > preview_max_x) preview_x = preview_max_x;
    if (preview_y < -preview_max_y) preview_y = -preview_max_y;
    if (preview_y > preview_max_y) preview_y = preview_max_y;
    lv_obj_align(cursor_dot, LV_ALIGN_CENTER, preview_x, preview_y);
}

static void keyboard_callback(int state, char &character)
{
    if (state != 1 || character == '\0') {
        return;
    }

    if (character == '\n' || character == '\r') {
        snprintf(key_text, sizeof(key_text), "Enter");
    } else if (character == '\b') {
        snprintf(key_text, sizeof(key_text), "Backspace");
    } else if (character == '\t') {
        snprintf(key_text, sizeof(key_text), "Tab");
    } else if (character == ' ') {
        snprintf(key_text, sizeof(key_text), "Space");
    } else {
        snprintf(key_text, sizeof(key_text), "%c", character);
    }
    key_count++;
    ble_hid_helper::typeChar(character);
}

static void pointer_callback(int8_t delta_x, int8_t delta_y)
{
    pending_x += static_cast<float>(delta_x) * static_cast<float>(sensitivity);
    pending_y += static_cast<float>(delta_y) * static_cast<float>(sensitivity);
    last_report_x = delta_x;
    last_report_y = delta_y;
    motion_count++;
    update_preview_dot(delta_x, delta_y);
}

static void pointer_button_callback(uint8_t buttonMask)
{
    const char *name = "--";
    switch (buttonMask) {
    case HW_POINTER_BUTTON_LEFT:
        name = "Left";
        break;
    case HW_POINTER_BUTTON_RIGHT:
        name = "Right";
        break;
    case HW_POINTER_BUTTON_MIDDLE:
        name = "Middle";
        break;
    default:
        return;
    }

    snprintf(button_text, sizeof(button_text), "%s", name);
    button_count++;
    ble_hid_helper::clickMouse(buttonMask);
}

static void mouse_timer_cb(lv_timer_t *)
{
    if (!ble_hid_helper::isConnected()) {
        pending_x = 0.0f;
        pending_y = 0.0f;
        return;
    }

    const int8_t output_x = clamp_hid_delta(pending_x);
    const int8_t output_y = clamp_hid_delta(pending_y);
    if (output_x == 0 && output_y == 0) {
        return;
    }

    pending_x -= static_cast<float>(output_x);
    pending_y -= static_cast<float>(output_y);
    ble_hid_helper::moveMouse(output_x, output_y);
}

static void ui_timer_cb(lv_timer_t *)
{
    ble_hid_helper::poll();
    set_label(state_label, ble_hid_helper::stateText());
    set_label(keyboard_label, keyboard_available() ? "Ready" : "Not detected");
    set_label(mouse_label, hw_pointer_available() ? "Ready" : "Not detected");
    if (delta_label) {
        lv_label_set_text_fmt(delta_label, "%ld,%ld  %lu",
                              static_cast<long>(last_report_x),
                              static_cast<long>(last_report_y),
                              static_cast<unsigned long>(motion_count));
    }
    if (key_label) {
        lv_label_set_text_fmt(key_label, "%s  %lu", key_text,
                              static_cast<unsigned long>(key_count));
    }
    if (button_label) {
        lv_label_set_text_fmt(button_label, "%s  %lu", button_text,
                              static_cast<unsigned long>(button_count));
    }
}

static void sensitivity_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    sensitivity = lv_slider_get_value(slider);
    update_sensitivity_label();
}

static void disconnect_forget_cb(lv_event_t *)
{
    ble_hid_helper::disconnectAndForget();
    set_label(state_label, ble_hid_helper::stateText());
}

static void cleanup()
{
    if (mouse_timer) {
        lv_timer_delete(mouse_timer);
        mouse_timer = nullptr;
    }
    if (ui_timer) {
        lv_timer_delete(ui_timer);
        ui_timer = nullptr;
    }
    hw_set_keyboard_read_callback(nullptr);
    hw_set_trackball_callback(nullptr);
    hw_set_pointer_button_callback(nullptr);
    disable_keyboard();
    ble_hid_helper::stop();
    pending_x = 0.0f;
    pending_y = 0.0f;
    button_count = 0;
    snprintf(button_text, sizeof(button_text), "--");
}

static void destroy_page()
{
    if (unavailable_msgbox) {
        destroy_msgbox(unavailable_msgbox);
        unavailable_msgbox = nullptr;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    state_label = nullptr;
    keyboard_label = nullptr;
    mouse_label = nullptr;
    sens_label = nullptr;
    key_label = nullptr;
    delta_label = nullptr;
    button_label = nullptr;
    cursor_dot = nullptr;
}

static void back_event_handler(lv_event_t *)
{
    cleanup();
    destroy_page();
    menu_show();
}

static void unavailable_msgbox_cb(lv_event_t *)
{
    if (unavailable_msgbox) {
        destroy_msgbox(unavailable_msgbox);
        unavailable_msgbox = nullptr;
    }
    destroy_page();
    menu_show();
}

static void show_unavailable_msgbox()
{
    if (unavailable_msgbox) {
        return;
    }
    static const char *buttons[] = {"OK", ""};
    unavailable_msgbox = create_msgbox(
                             lv_scr_act(),
                             "BLE HID",
                             "Keyboard and pointer input are not detected.",
                             buttons,
                             unavailable_msgbox_cb,
                             nullptr);
}

void ui_ble_hid_enter(lv_obj_t *parent)
{
    if (!hid_available()) {
        show_unavailable_msgbox();
        return;
    }

    cleanup();
    page_container = ui_create_app_page(parent, "BLE HID", back_event_handler);

    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    lv_obj_t *row = ui_create_card_info(status_card, LV_SYMBOL_BLUETOOTH, "BLE", "Starting");
    state_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_KEYBOARD, "Keyboard",
                              keyboard_available() ? "Ready" : "Not detected");
    keyboard_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_SETTINGS, "Pointer",
                              hw_pointer_available() ? "Ready" : "Not detected");
    mouse_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_SETTINGS, "Sensitivity", "8x");
    sens_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_EDIT, "Last key", "--");
    key_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_REFRESH, "Delta", "0,0  0");
    delta_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_OK, "Button", "--  0");
    button_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    ui_create_card_button(status_card, LV_SYMBOL_CLOSE, "Connection", "Forget",
                          disconnect_forget_cb);

    lv_obj_t *slider = lv_slider_create(lv_obj_create(status_card));
    lv_slider_set_range(slider, BLE_HID_SENS_MIN, BLE_HID_SENS_MAX);
    lv_slider_set_value(slider, sensitivity, LV_ANIM_OFF);
    lv_obj_set_size(slider, LV_PCT(50), 12);
    lv_obj_set_flex_grow(slider, 1);
    ui_prepare_slider_for_encoder(slider);
    lv_obj_add_event_cb(slider, sensitivity_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    ui_create_card_item(status_card, LV_SYMBOL_SETTINGS, "Pointer gain", slider);
    update_sensitivity_label();

    lv_obj_t *preview_card = ui_create_card(page_container, nullptr);
    lv_obj_set_height(preview_card, is_screen_small() ? 72 : 100);
    lv_obj_remove_flag(preview_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *track_area = lv_obj_create(preview_card);
    lv_obj_set_size(track_area, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(track_area, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(track_area, LV_OPA_70, 0);
    lv_obj_set_style_border_color(track_area, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(track_area, 1, 0);
    lv_obj_set_style_radius(track_area, 4, 0);
    lv_obj_set_style_pad_all(track_area, 0, 0);
    lv_obj_set_scrollbar_mode(track_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(track_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_update_layout(track_area);
    const int32_t track_width = lv_obj_get_width(track_area);
    const int32_t track_height = lv_obj_get_height(track_area);
    preview_max_x = (track_width - 12) / 2;
    preview_max_y = (track_height - 12) / 2;
    preview_x = 0;
    preview_y = 0;

    cursor_dot = lv_obj_create(track_area);
    lv_obj_set_size(cursor_dot, 12, 12);
    lv_obj_set_style_radius(cursor_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cursor_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(cursor_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cursor_dot, 0, 0);
    lv_obj_center(cursor_dot);
    lv_obj_remove_flag(cursor_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cursor_dot, LV_OBJ_FLAG_CLICKABLE);

    snprintf(key_text, sizeof(key_text), "--");
    snprintf(button_text, sizeof(button_text), "--");
    last_report_x = 0;
    last_report_y = 0;
    motion_count = 0;
    key_count = 0;
    button_count = 0;
    pending_x = 0.0f;
    pending_y = 0.0f;

    if (keyboard_available()) {
        hw_set_keyboard_read_callback(keyboard_callback);
        enable_keyboard();
    }
    if (hw_pointer_available()) {
        hw_set_trackball_callback(pointer_callback);
        hw_set_pointer_button_callback(pointer_button_callback);
        mouse_timer = lv_timer_create(mouse_timer_cb, BLE_HID_MOUSE_PERIOD_MS, nullptr);
    }
    ui_timer = lv_timer_create(ui_timer_cb, BLE_HID_UI_PERIOD_MS, nullptr);

    char device_name[32];
    snprintf(device_name, sizeof(device_name), "%s HID", hw_get_variant_name());
    ble_hid_helper::startAsync(device_name, BLE_HID_START_TIMEOUT_MS);
    ui_timer_cb(ui_timer);
}

void ui_ble_hid_exit(lv_obj_t *)
{
    cleanup();
}

app_t ui_ble_hid_main = {
    .setup_func_cb = ui_ble_hid_enter,
    .exit_func_cb = ui_ble_hid_exit,
    .user_data = nullptr,
};

#endif
