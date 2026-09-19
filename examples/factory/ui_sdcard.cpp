/**
 * @file      ui_sdcard.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * SD Card page — mount, info, and read/write speed test.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#ifdef ARDUINO
#include <SD.h>
#include <SPI.h>
#endif

#if !defined(EXCLUDE_SD_APPS)

static lv_obj_t *page_container = NULL;
static lv_timer_t *status_timer = NULL;

/* Status indicator */
static lv_obj_t *status_dot = NULL;
static lv_obj_t *status_text = NULL;

/* Info labels */
static lv_obj_t *size_label = NULL;
static lv_obj_t *type_label = NULL;
static lv_obj_t *mount_label = NULL;
static lv_obj_t *spi_label = NULL;

/* Speed test labels */
static lv_obj_t *read_speed_label = NULL;
static lv_obj_t *write_speed_label = NULL;
static lv_obj_t *test_status_label = NULL;
static lv_obj_t *test_btn = NULL;

static bool sd_mounted = false;
static uint32_t sd_mounted_freq = 0;

#define SD_MOUNT_FREQ_MAX 8

#ifdef ARDUINO
static bool sd_status_log_valid = false;
static bool sd_last_detect_capable = false;
static bool sd_last_inserted = false;
static bool sd_last_ready = false;
static bool sd_last_mounted = false;

static void log_sd_status_change(bool detect_capable, bool inserted, bool ready, bool mounted)
{
    if (!sd_status_log_valid ||
            sd_last_detect_capable != detect_capable ||
            sd_last_inserted != inserted ||
            sd_last_ready != ready ||
            sd_last_mounted != mounted) {
        LILYGO_LOG_PRINTF("[SD Info] detect_pin:%u inserted:%u ready:%u mounted:%u freq:%lu\n",
                      detect_capable ? 1 : 0,
                      inserted ? 1 : 0,
                      ready ? 1 : 0,
                      mounted ? 1 : 0,
                      (unsigned long)sd_mounted_freq);
        sd_last_detect_capable = detect_capable;
        sd_last_inserted = inserted;
        sd_last_ready = ready;
        sd_last_mounted = mounted;
        sd_status_log_valid = true;
    }
}
#endif

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label) lv_label_set_text(label, text);
}

static void format_spi_freq(char *buf, size_t buf_size, uint32_t freq)
{
    if (!buf || buf_size == 0) return;
    if (freq == 0) {
        snprintf(buf, buf_size, "--");
    } else if (freq >= 1000000U) {
        uint32_t mhz10 = freq / 100000U;
        if ((mhz10 % 10) == 0) {
            snprintf(buf, buf_size, "%lu MHz", (unsigned long)(mhz10 / 10));
        } else {
            snprintf(buf, buf_size, "%lu.%lu MHz",
                     (unsigned long)(mhz10 / 10),
                     (unsigned long)(mhz10 % 10));
        }
    } else {
        snprintf(buf, buf_size, "%lu kHz", (unsigned long)(freq / 1000U));
    }
}

static void set_spi_freq_label(uint32_t freq)
{
    if (!spi_label) return;
    char buf[24];
    format_spi_freq(buf, sizeof(buf), freq);
    lv_label_set_text(spi_label, buf);
}

static void update_mount_progress(const char *status, uint32_t freq)
{
    set_label_text(mount_label, status);
    set_spi_freq_label(freq);
    lv_refr_now(NULL);
}

static bool sd_storage_ready(float *out_size)
{
    float size = 0.0f;
#ifdef ARDUINO
#if defined(HAS_SD_CARD_SOCKET)
    if (hw_is_sd_insert()) {
        size = hw_get_sd_size();
    }
#elif defined(USING_FATFS)
    size = hw_get_sd_size();
#endif
#endif
    if (out_size) *out_size = size;
    return size > 0.0f;
}

static void clear_sd_info(void)
{
    sd_mounted = false;
    sd_mounted_freq = 0;
    set_label_text(size_label, "--");
    set_label_text(type_label, "--");
    set_label_text(mount_label, "Not Mounted");
    set_spi_freq_label(0);
}

static void update_sd_info(float size, uint32_t freq)
{
    sd_mounted = true;
    sd_mounted_freq = freq;

    if (size_label) {
        char buf[32];
#if defined(HAS_SD_CARD_SOCKET)
        snprintf(buf, sizeof(buf), "%.1f GB", size);
#elif defined(USING_FATFS)
        snprintf(buf, sizeof(buf), "%.1f MB", size);
#else
        snprintf(buf, sizeof(buf), "%.1f", size);
#endif
        lv_label_set_text(size_label, buf);
    }
    if (type_label) {
#if defined(HAS_SD_CARD_SOCKET)
        lv_label_set_text(type_label, "SD Card");
#elif defined(USING_FATFS)
        lv_label_set_text(type_label, "Flash (FFat)");
#else
        lv_label_set_text(type_label, "Unknown");
#endif
    }
    set_label_text(mount_label, "Mounted");
    set_spi_freq_label(freq);
}

static void back_event_handler(lv_event_t *e)
{
    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    status_dot = NULL;
    status_text = NULL;
    size_label = NULL;
    type_label = NULL;
    mount_label = NULL;
    spi_label = NULL;
    read_speed_label = NULL;
    write_speed_label = NULL;
    test_status_label = NULL;
    test_btn = NULL;
    sd_mounted = false;
    sd_mounted_freq = 0;
#ifdef ARDUINO
    sd_status_log_valid = false;
#endif
    menu_show();
}

static void update_status(lv_timer_t *t)
{
    bool inserted = false;
    bool detect_capable = false;
    bool ready = false;
#ifdef ARDUINO
#if defined(HAS_SD_CARD_SOCKET)
    detect_capable = hw_has_sd_detect_pin();
    ready = hw_is_sd_insert();
    inserted = detect_capable ? hw_is_sd_card_inserted() : ready;
    log_sd_status_change(detect_capable, inserted, ready, sd_mounted);
    if (detect_capable && !inserted) {
        if (ready || sd_mounted) {
#ifdef ARDUINO
            LILYGO_LOG_PRINTF("[SD Info] no card detected, clear cached state, ready:%u mounted:%u\n",
                          ready ? 1 : 0, sd_mounted ? 1 : 0);
#endif
            hw_unmount_sd();
            clear_sd_info();
        }
        ready = false;
    } else if (ready && !sd_mounted) {
        float size = 0.0f;
        if (sd_storage_ready(&size)) {
            update_sd_info(size, sd_mounted_freq ? sd_mounted_freq : hw_get_sd_default_spi_freq());
        }
    }
#elif defined(USING_FATFS)
    inserted = true; /* Flash storage always present */
    ready = true;
    if (!sd_mounted) {
        float size = 0.0f;
        if (sd_storage_ready(&size)) {
            update_sd_info(size, 0);
        }
    }
#endif
#endif

    if (status_dot) {
        lv_color_t color = inserted ? lv_color_hex(0x00DD00) :
                           (detect_capable ? lv_color_hex(0xFF3333) : lv_color_hex(0xFFAA00));
        lv_obj_set_style_bg_color(status_dot, color, 0);
        lv_obj_set_style_shadow_color(status_dot, color, 0);
    }
    if (status_text) {
        if (detect_capable) {
            lv_label_set_text(status_text, inserted ? "Inserted" : "No Card");
        } else {
            lv_label_set_text(status_text, ready ? "Ready" : "Unknown");
        }
    }
    if (mount_label && sd_mounted) {
        lv_label_set_text(mount_label, "Mounted");
    }
}

static void mount_btn_cb(lv_event_t *e)
{
    float size = 0.0f;

#if defined(HAS_SD_CARD_SOCKET)
    if (hw_has_sd_detect_pin() && !hw_is_sd_card_inserted()) {
#ifdef ARDUINO
        LILYGO_LOG_PRINTLN("[SD Info] mount button ignored, no card detected");
#endif
        hw_unmount_sd();
        clear_sd_info();
        set_label_text(mount_label, "No Card");
        set_label_text(test_status_label, "Insert SD card first");
        return;
    }
#endif

    if (sd_storage_ready(&size)) {
        update_sd_info(size, sd_mounted_freq ? sd_mounted_freq : hw_get_sd_default_spi_freq());
        return;
    }

#if defined(HAS_SD_CARD_SOCKET)
    uint32_t freqs[SD_MOUNT_FREQ_MAX];
    uint8_t freq_count = hw_get_sd_mount_freq_list(freqs, SD_MOUNT_FREQ_MAX);
    for (uint8_t i = 0; i < freq_count; ++i) {
        char freq_buf[24];
        char status_buf[48];
        format_spi_freq(freq_buf, sizeof(freq_buf), freqs[i]);
        snprintf(status_buf, sizeof(status_buf), "Mounting %s", freq_buf);
        update_mount_progress(status_buf, freqs[i]);

        if (hw_mount_sd(freqs[i]) && sd_storage_ready(&size)) {
            update_sd_info(size, freqs[i]);
            set_label_text(test_status_label, "Ready");
            lv_refr_now(NULL);
            return;
        }

        snprintf(status_buf, sizeof(status_buf), "%s failed", freq_buf);
        update_mount_progress(status_buf, freqs[i]);
    }

    clear_sd_info();
    set_label_text(mount_label, "Mount Failed");
    set_label_text(test_status_label, "Try another card");
#elif defined(USING_FATFS)
    if (sd_storage_ready(&size)) {
        update_sd_info(size, 0);
    } else {
        clear_sd_info();
        set_label_text(mount_label, "Mount Failed");
    }
#else
    clear_sd_info();
    set_label_text(mount_label, "Not Supported");
#endif
}

static void speed_test_cb(lv_event_t *e)
{
    if (!sd_mounted) {
        if (test_status_label) lv_label_set_text(test_status_label, "Mount card first!");
        return;
    }

    if (test_btn) lv_obj_add_state(test_btn, LV_STATE_DISABLED);
    if (test_status_label) lv_label_set_text(test_status_label, "Testing...");
    if (read_speed_label) lv_label_set_text(read_speed_label, "--");
    if (write_speed_label) lv_label_set_text(write_speed_label, "--");

    lv_refr_now(NULL);

#ifdef ARDUINO
    const char *test_file = "/speed_test.bin";
    const uint32_t buf_size = 4096;
    const uint32_t test_size = 256 * 1024; /* 256KB test */
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        if (test_status_label) lv_label_set_text(test_status_label, "Memory error");
        if (test_btn) lv_obj_clear_state(test_btn, LV_STATE_DISABLED);
        return;
    }

    /* Fill buffer with test pattern */
    for (uint32_t i = 0; i < buf_size; i++) buf[i] = i & 0xFF;

    /* Write test */
    uint32_t start = millis();
    File f = SD.open(test_file, FILE_WRITE);
    if (!f) {
        if (test_status_label) lv_label_set_text(test_status_label, "Open failed");
        free(buf);
        if (test_btn) lv_obj_clear_state(test_btn, LV_STATE_DISABLED);
        return;
    }

    uint32_t written = 0;
    while (written < test_size) {
        uint32_t to_write = test_size - written;
        if (to_write > buf_size) to_write = buf_size;
        f.write(buf, to_write);
        written += to_write;
    }
    f.close();
    uint32_t write_ms = millis() - start;
    float write_speed = (float)test_size / 1024.0f / ((float)write_ms / 1000.0f);

    /* Read test */
    start = millis();
    f = SD.open(test_file, FILE_READ);
    if (!f) {
        if (test_status_label) lv_label_set_text(test_status_label, "Read open failed");
        free(buf);
        if (test_btn) lv_obj_clear_state(test_btn, LV_STATE_DISABLED);
        return;
    }

    uint32_t read_total = 0;
    while (f.available()) {
        uint32_t rd = f.read(buf, buf_size);
        read_total += rd;
    }
    f.close();
    uint32_t read_ms = millis() - start;
    float read_speed = (float)read_total / 1024.0f / ((float)read_ms / 1000.0f);

    /* Delete test file */
    SD.remove(test_file);
    free(buf);

    /* Show results */
    char buf2[32];
    snprintf(buf2, sizeof(buf2), "%.1f KB/s", write_speed);
    if (write_speed_label) lv_label_set_text(write_speed_label, buf2);

    snprintf(buf2, sizeof(buf2), "%.1f KB/s", read_speed);
    if (read_speed_label) lv_label_set_text(read_speed_label, buf2);

    if (test_status_label) lv_label_set_text(test_status_label, "Done");
#else
    if (test_status_label) lv_label_set_text(test_status_label, "Not available");
#endif

    if (test_btn) lv_obj_clear_state(test_btn, LV_STATE_DISABLED);
}

void ui_sdcard_enter(lv_obj_t *parent)
{
    sd_mounted = false;
    sd_mounted_freq = 0;
#ifdef ARDUINO
    sd_status_log_valid = false;
#endif

    page_container = ui_create_app_page(parent, "SD Card", back_event_handler);

    /* ── Status card ── */
    lv_obj_t *card = ui_create_card(page_container, "Card Status");

    /* Status row with colored dot */
    lv_obj_t *status_row = lv_obj_create(card);
    lv_obj_set_size(status_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_radius(status_row, 0, 0);
    lv_obj_set_style_pad_top(status_row, 6, 0);
    lv_obj_set_style_pad_bottom(status_row, 6, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 8, 0);

    status_dot = lv_obj_create(status_row);
    lv_obj_set_size(status_dot, 10, 10);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_set_style_shadow_width(status_dot, 6, 0);
    lv_obj_set_style_shadow_color(status_dot, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_shadow_opa(status_dot, LV_OPA_40, 0);

    lv_obj_t *lbl = lv_label_create(status_row);
    lv_label_set_text(lbl, "Status:");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    status_text = lv_label_create(status_row);
    lv_label_set_text(status_text, "Checking...");
    lv_obj_set_style_text_color(status_text, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(status_text, &lv_font_montserrat_12, 0);

    /* Divider */
    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, LV_PCT(95), 1);
    lv_obj_set_style_bg_color(div, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);

    /* Info rows — store row, then extract value label */
    lv_obj_t *row;

    row = ui_create_card_info(card, LV_SYMBOL_SD_CARD, "Mount", "Not Mounted");
    mount_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    row = ui_create_card_info(card, LV_SYMBOL_SD_CARD, "Size", "--");
    size_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    row = ui_create_card_info(card, LV_SYMBOL_SD_CARD, "Type", "--");
    type_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    row = ui_create_card_info(card, LV_SYMBOL_SETTINGS, "SPI", "--");
    spi_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    /* Mount button */
    ui_create_card_button(card, LV_SYMBOL_SD_CARD, "Mount", "Mount", mount_btn_cb);

    /* ── Speed Test card ── */
    card = ui_create_card(page_container, "Speed Test");

    row = ui_create_card_info(card, LV_SYMBOL_DOWNLOAD, "Write", "--");
    write_speed_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    row = ui_create_card_info(card, LV_SYMBOL_UPLOAD, "Read", "--");
    read_speed_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Ready");
    test_status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    /* Test button */
    test_btn = lv_btn_create(card);
    lv_obj_set_size(test_btn, LV_PCT(100), 36);
    lv_obj_add_style(test_btn, &ui_styles.accent_btn, 0);
    ui_add_accent_focus_style(test_btn);
    lv_obj_set_style_radius(test_btn, 8, 0);

    lv_obj_t *btn_lbl = lv_label_create(test_btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_REFRESH " Run Speed Test");
    lv_obj_center(btn_lbl);

    lv_obj_add_event_cb(test_btn, speed_test_cb, LV_EVENT_CLICKED, NULL);

    /* ── Status timer ── */
    status_timer = lv_timer_create(update_status, 1000, NULL);
    update_status(NULL); /* Initial check */
}

void ui_sdcard_exit(lv_obj_t *parent)
{
}

app_t ui_sdcard_main = {
    .setup_func_cb = ui_sdcard_enter,
    .exit_func_cb  = ui_sdcard_exit,
    .user_data     = nullptr,
};

#endif
