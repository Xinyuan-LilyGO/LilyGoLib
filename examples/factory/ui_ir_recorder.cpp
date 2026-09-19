/**
 * @file      ui_ir_recorder.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-21
 * IR Recorder — Record IR signals and save raw data to SD card
 */
#include "ui_define.h"

#if !defined(EXCLUDE_IR_RECORDER)

#include <IRrecv.h>
#include <IRutils.h>
#include <SD.h>

extern IRrecv irrecv;

static lv_obj_t *page_container = NULL;
static lv_timer_t *poll_timer = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *proto_lbl = NULL;
static lv_obj_t *value_lbl = NULL;
static lv_obj_t *raw_lbl = NULL;
static lv_obj_t *filename_ta = NULL;

/* Captured data */
static uint16_t *captured_raw = NULL;
static uint16_t captured_len = 0;
static uint32_t captured_proto = 0;
static uint64_t captured_value = 0;
static uint16_t captured_freq = 38000;
static bool has_capture = false;
static int file_counter = 1;

static void clear_capture(void)
{
    if (captured_raw) {
        free(captured_raw);
        captured_raw = NULL;
    }
    captured_len = 0;
    captured_proto = 0;
    captured_value = 0;
    has_capture = false;
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_lbl) return;
    lv_label_set_text(status_lbl, text);
    lv_obj_set_style_text_color(status_lbl, color, 0);
}

/* IR file header */
typedef struct {
    char magic[4];        // "IRAW"
    uint16_t version;     // 1
    uint16_t freq;        // Hz
    uint16_t raw_len;     // number of uint16_t
    uint16_t proto;       // decode_type_t
    uint64_t value;       // decoded value
} ir_file_header_t;

/* ── Find next available file number ── */
static void find_next_file_num(void)
{
    char path[64];
    for (int i = 1; i < 999; i++) {
        snprintf(path, sizeof(path), "/ir/signal_%03d.ir", i);
        if (!SD.exists(path)) {
            file_counter = i;
            return;
        }
    }
    file_counter = 999;
}

/* ── Poll for IR signal ── */
static void ir_poll_cb(lv_timer_t *timer)
{
#if defined(ARDUINO)
    decode_results results;
    if (!irrecv.decode(&results)) return;

    if (results.overflow) {
        clear_capture();
        irrecv.resume();
        set_status("Capture overflow. Try again closer to the receiver.", lv_color_hex(0xFF4444));
        if (proto_lbl) lv_label_set_text(proto_lbl, "Protocol: --");
        if (value_lbl) lv_label_set_text(value_lbl, "Value: --");
        if (raw_lbl) lv_label_set_text(raw_lbl, "Raw: overflow");
        return;
    }

    /* Got a signal */
    clear_capture();
    captured_proto = results.decode_type;
    captured_value = results.value;
    captured_freq = 38000;  // Most common

    /* Extract raw data */
    uint16_t *raw = resultToRawArray(&results);
    uint16_t len = getCorrectedRawLength(&results);

    if (raw && len > 0) {
        captured_raw = (uint16_t *)malloc(len * sizeof(uint16_t));
        if (captured_raw) {
            memcpy(captured_raw, raw, len * sizeof(uint16_t));
            captured_len = len;
            has_capture = true;
        }
        delete[] raw;
    }

    irrecv.resume();

    if (!has_capture) {
        set_status("Failed to capture raw data.", lv_color_hex(0xFF4444));
        return;
    }

    /* Update UI */
    set_status("Signal captured!", UI_COLOR_ACCENT);

    if (proto_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Protocol: %s", typeToString((decode_type_t)captured_proto).c_str());
        lv_label_set_text(proto_lbl, buf);
    }

    if (value_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Value: 0x%llX", captured_value);
        lv_label_set_text(value_lbl, buf);
    }

    if (raw_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Raw: %u samples", captured_len);
        lv_label_set_text(raw_lbl, buf);
    }
#endif
}

/* ── Save to SD card ── */
static void save_btn_cb(lv_event_t *e)
{
    if (!has_capture || !captured_raw) {
        set_status("No signal to save!", lv_color_hex(0xFF4444));
        return;
    }

    /* Get filename from textarea */
    const char *fname = lv_textarea_get_text(filename_ta);
    if (!fname || strlen(fname) == 0) {
        set_status("Enter a filename!", lv_color_hex(0xFF4444));
        return;
    }

    /* Build path */
    char path[64];
    snprintf(path, sizeof(path), "/ir/%s.ir", fname);

    /* Ensure /ir directory exists */
    if (!SD.exists("/ir")) {
        SD.mkdir("/ir");
    }

    /* Write file */
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        set_status("Failed to open file!", lv_color_hex(0xFF4444));
        return;
    }

    /* Write header */
    ir_file_header_t header;
    memcpy(header.magic, "IRAW", 4);
    header.version = 1;
    header.freq = captured_freq;
    header.raw_len = captured_len;
    header.proto = (uint16_t)captured_proto;
    header.value = captured_value;

    size_t header_written = f.write((uint8_t *)&header, sizeof(header));

    /* Write raw data */
    size_t raw_size = captured_len * sizeof(uint16_t);
    size_t data_written = f.write((uint8_t *)captured_raw, raw_size);
    f.close();

    if (header_written != sizeof(header) || data_written != raw_size) {
        SD.remove(path);
        set_status("Failed to write file!", lv_color_hex(0xFF4444));
        return;
    }

    /* Update UI */
    if (status_lbl) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Saved: %s", path);
        lv_label_set_text(status_lbl, buf);
        lv_obj_set_style_text_color(status_lbl, UI_COLOR_ACCENT, 0);
    }

    /* Update filename for next save */
    file_counter++;
    char new_name[32];
    snprintf(new_name, sizeof(new_name), "signal_%03d", file_counter);
    lv_textarea_set_text(filename_ta, new_name);
}

/* ── Back handler ── */
static void back_event_handler(lv_event_t *e)
{
    if (poll_timer) {
        lv_timer_del(poll_timer);
        poll_timer = NULL;
    }

    /* Restore radio mode */
    hw_ir_function_select(false);

    clear_capture();

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    status_lbl = NULL;
    proto_lbl = NULL;
    value_lbl = NULL;
    raw_lbl = NULL;
    filename_ta = NULL;
    menu_show();
}

/* ── Enter ── */
void ui_ir_recorder_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "IR Recorder", back_event_handler);

    /* Switch to receive mode */
    hw_ir_function_select(false);

    /* Find next file number */
    find_next_file_num();

    /* ── Status card ── */
    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    status_lbl = lv_label_create(status_card);
    lv_label_set_text(status_lbl, "Waiting for IR signal...");
    lv_obj_set_style_text_color(status_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, 0);

    /* ── Capture info card ── */
    lv_obj_t *info_card = ui_create_card(page_container, "Captured Data");

    proto_lbl = lv_label_create(info_card);
    lv_label_set_text(proto_lbl, "Protocol: --");
    lv_obj_set_style_text_color(proto_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(proto_lbl, &lv_font_montserrat_12, 0);

    value_lbl = lv_label_create(info_card);
    lv_label_set_text(value_lbl, "Value: --");
    lv_obj_set_style_text_color(value_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(value_lbl, &lv_font_montserrat_12, 0);

    raw_lbl = lv_label_create(info_card);
    lv_label_set_text(raw_lbl, "Raw: --");
    lv_obj_set_style_text_color(raw_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(raw_lbl, &lv_font_montserrat_12, 0);

    /* ── Save card ── */
    lv_obj_t *save_card = ui_create_card(page_container, "Save");

    /* Filename input */
    filename_ta = lv_textarea_create(save_card);
    lv_textarea_set_one_line(filename_ta, true);
    lv_textarea_set_max_length(filename_ta, 32);
    char default_name[32];
    snprintf(default_name, sizeof(default_name), "signal_%03d", file_counter);
    lv_textarea_set_text(filename_ta, default_name);
    lv_obj_set_style_bg_color(filename_ta, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(filename_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(filename_ta, lv_color_white(), 0);
    lv_obj_set_style_border_color(filename_ta, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(filename_ta, 1, 0);
    lv_obj_set_style_radius(filename_ta, 8, 0);
    ui_create_card_item(save_card, LV_SYMBOL_FILE, "Filename", filename_ta);

    /* Save button */
    lv_obj_t *save_btn = lv_btn_create(save_card);
    lv_obj_set_size(save_btn, LV_PCT(60), 36);
    lv_obj_set_style_bg_color(save_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(save_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    ui_add_accent_focus_style(save_btn);
    lv_obj_add_event_cb(save_btn, save_btn_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *l = lv_label_create(save_btn);
        lv_label_set_text(l, LV_SYMBOL_SAVE " Save to SD");
        lv_obj_center(l);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
    }

    /* Start polling for IR signals */
    poll_timer = lv_timer_create(ir_poll_cb, 100, NULL);
}

/* ── Exit ── */
void ui_ir_recorder_exit(lv_obj_t *parent)
{
}

app_t ui_ir_recorder_main = {
    .setup_func_cb = ui_ir_recorder_enter,
    .exit_func_cb = ui_ir_recorder_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_IR_RECORDER */
