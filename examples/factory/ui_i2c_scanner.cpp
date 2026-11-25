/**
 * @file      ui_i2c_scanner.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * Scans nonreserved I2C addresses with progress and possible device names.
 */
#include "ui_define.h"
#include "ui_scan_widgets.h"
#ifdef ARDUINO
#include <Wire.h>
#endif
namespace {
static lv_obj_t *page, *list;
static UIScanLoading loading;
static lv_timer_t *timer;
static uint8_t address, found;
static const char *possible_device(uint8_t a) {
    switch (a) {
    case 0x18: case 0x19: return "Accelerometer / audio codec";
    case 0x22: return "XL9555 / PCF8574 GPIO expander";
    case 0x20: case 0x21: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: return "GPIO expander / light sensor";
    case 0x34: case 0x35: return "AXP power controller";
    case 0x38: return "Touch controller / AHT20";
    case 0x3C: case 0x3D: return "OLED display";
    case 0x40: return "INA219 / HTU21D";
    case 0x44: case 0x45: return "SHT3x temperature/humidity";
    case 0x48: case 0x49: return "ADS1115 / temperature sensor";
    case 0x50: case 0x51: return "EEPROM / RTC";
    case 0x55: return "BQ27220 fuel gauge";
    case 0x57: return "MAX3010x / EEPROM";
    case 0x68: return "RTC / IMU";
    case 0x6A: return "IMU / charger";
    case 0x6B: return "SY6970 / BQ25896 / IMU";
    case 0x76: case 0x77: return "BMP/BME pressure sensor";
    default: return "Unknown device";
    }
}
static void focused(lv_event_t *e) { lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(e), LV_ANIM_ON); }
static void details(lv_event_t *e) {
    const uint8_t a = (uintptr_t)lv_event_get_user_data(e); char text[160];
    snprintf(text, sizeof(text), "7-bit address: 0x%02X\nPossible: %s\nAn address does not identify a chip uniquely.", a, possible_device(a));
    ui_msg_pop_up("I2C device", text);
}
static void stop() { if (timer) { lv_timer_delete(timer); timer = nullptr; } }
static void tick(lv_timer_t *) {
#ifdef ARDUINO
    if (address > 0x77) {
        stop(); ui_scan_loading_update(loading, "No I2C devices found", false);
        ui_scan_show_results(loading, list, found != 0); return;
    }
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
        ++found; char text[96]; snprintf(text, sizeof(text), "0x%02X  %s", address, possible_device(address));
        auto *row = lv_list_add_button(list, LV_SYMBOL_SETTINGS, text);
        lv_obj_set_height(row, LV_SIZE_CONTENT); lv_obj_set_style_min_height(row, 32, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_set_style_text_font(row, &lv_font_montserrat_12, 0);
        auto *label = lv_obj_get_child(row, -1);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        // A fixed 14 px label is shorter than this font's 15 px line height,
        // which makes circular scrolling move vertically instead of sideways.
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_event_cb(row, focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(row, details, LV_EVENT_CLICKED, (void *)(uintptr_t)address);
    }
    char text[64]; snprintf(text, sizeof(text), "Scanning 0x%02X  /  %u found", address, found);
    ui_scan_loading_update(loading, text, true); ++address;
#endif
}
static void scan() {
    if (timer) return;
    address = 3; found = 0; lv_obj_clean(list);
    ui_scan_show_results(loading, list, false);
    ui_scan_loading_update(loading, "Scanning I2C...", true);
    timer = lv_timer_create(tick, 10, nullptr);
}
static void back(lv_event_t *) { stop(); if (page) ui_destroy_app_page(page); page = nullptr; menu_show(); }
}
void ui_i2c_scanner_enter(lv_obj_t *parent) {
    page = ui_create_app_page(parent, "I2C Scanner", back); lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(page, [](lv_event_t *) { stop(); page = list = nullptr; loading = {}; }, LV_EVENT_DELETE, nullptr);
    loading = ui_scan_loading_create(page, "Scanning I2C...");
    list = lv_list_create(page); lv_obj_set_size(list, LV_PCT(100), 1); lv_obj_set_flex_grow(list, 1); lv_obj_set_style_pad_row(list, 5, 0);
    scan();
}
void ui_i2c_scanner_exit(lv_obj_t *) { stop(); }
app_t ui_i2c_scanner_main = { .setup_func_cb = ui_i2c_scanner_enter, .exit_func_cb = ui_i2c_scanner_exit, .user_data = nullptr };
