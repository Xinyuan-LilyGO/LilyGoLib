/**
 * @file      ui_i2c_scanner.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * I2C bus scanner — scans addresses 0x01-0x7F and lists found devices.
 */
#include "ui_define.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

#define I2C_SCAN_ADDR_MIN  0x01
#define I2C_SCAN_ADDR_MAX  0x7F

static lv_obj_t *page_container = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *count_label = NULL;
static lv_obj_t *scan_btn = NULL;
static lv_timer_t *scan_timer = NULL;
static bool scanning = false;

/* Scan state — kept outside the timer to avoid static-local issues */
static uint8_t scan_addr = I2C_SCAN_ADDR_MIN;
static uint8_t scan_found = 0;
static char scan_buf[512];

static void back_event_handler(lv_event_t *e)
{
    if (scan_timer) {
        lv_timer_del(scan_timer);
        scan_timer = NULL;
    }
    scanning = false;
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    menu_show();
}

#ifdef ARDUINO
static void scan_tick(lv_timer_t *t)
{
    if (scan_addr > I2C_SCAN_ADDR_MAX) {
        /* Scan complete */
        lv_timer_del(scan_timer);
        scan_timer = NULL;
        scanning = false;

        if (scan_found == 0) {
            lv_label_set_text(result_label, "No devices found");
        } else {
            lv_label_set_text(result_label, scan_buf);
        }
        lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(scan_btn, 0), "Scan");
        return;
    }

    Wire.beginTransmission(scan_addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
        scan_found++;
        /* Append address safely */
        size_t len = strlen(scan_buf);
        if (len + 10 < sizeof(scan_buf)) {
            snprintf(scan_buf + len, sizeof(scan_buf) - len, "0x%02X  ", scan_addr);
        }
        /* Newline every 4 devices */
        if (scan_found % 4 == 0) {
            len = strlen(scan_buf);
            if (len + 2 < sizeof(scan_buf)) {
                scan_buf[len] = '\n';
                scan_buf[len + 1] = '\0';
            }
        }
        lv_label_set_text_fmt(count_label, "Found: %u", scan_found);
    }

    scan_addr++;
}
#endif

static void scan_btn_cb(lv_event_t *e)
{
    if (scanning) return;

#ifdef ARDUINO
    scanning = true;
    scan_addr = I2C_SCAN_ADDR_MIN;
    scan_found = 0;
    memset(scan_buf, 0, sizeof(scan_buf));

    lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
    lv_label_set_text(lv_obj_get_child(scan_btn, 0), "Scanning...");
    lv_label_set_text(result_label, "");
    lv_label_set_text_fmt(count_label, "Found: 0");

    scan_timer = lv_timer_create(scan_tick, 10, NULL);
#else
    lv_label_set_text(result_label, "I2C scan not available in emulator");
#endif
}

void ui_i2c_scanner_enter(lv_obj_t *parent)
{
    scanning = false;
    scan_addr = I2C_SCAN_ADDR_MIN;
    scan_found = 0;
    memset(scan_buf, 0, sizeof(scan_buf));

    page_container = ui_create_app_page(parent, "I2C Scanner", back_event_handler);

    /* Status card */
    lv_obj_t *card = ui_create_card(page_container, "Bus Scan");

    lv_obj_t *count_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Devices", "0");
    count_label = lv_obj_get_child(count_row, lv_obj_get_child_count(count_row) - 1);

    /* Result display */
    result_label = lv_label_create(card);
    lv_label_set_text(result_label, "Tap Scan to start");
    lv_obj_set_style_text_color(result_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(result_label, LV_PCT(100));
    lv_obj_set_style_text_line_space(result_label, 4, 0);

    /* Scan button */
    scan_btn = lv_btn_create(card);
    lv_obj_set_size(scan_btn, LV_PCT(100), 36);
    lv_obj_add_style(scan_btn, &ui_styles.accent_btn, 0);
    ui_add_accent_focus_style(scan_btn);
    lv_obj_set_style_radius(scan_btn, 8, 0);

    lv_obj_t *btn_label = lv_label_create(scan_btn);
    lv_label_set_text(btn_label, "Scan");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Info card */
    lv_obj_t *info_card = ui_create_card(page_container, "Info");
    ui_create_card_info(info_card, LV_SYMBOL_SETTINGS, "Range", "0x01 - 0x7F");
    ui_create_card_info(info_card, LV_SYMBOL_SETTINGS, "Bus", "I2C (Wire)");
}

void ui_i2c_scanner_exit(lv_obj_t *parent)
{
}

app_t ui_i2c_scanner_main = {
    .setup_func_cb = ui_i2c_scanner_enter,
    .exit_func_cb  = ui_i2c_scanner_exit,
    .user_data     = nullptr,
};
