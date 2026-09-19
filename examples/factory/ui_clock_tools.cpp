 /**
 * @file      ui_clock_tools.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-05-29
 * 
 * Timezone, manual clock setting, and NTP sync tools.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_CLOCK_TOOLS)

#define CLOCK_YEAR_START 2024
#define CLOCK_YEAR_END   2035

typedef struct {
    const char *name;
    int32_t offset_sec;
} clock_tz_entry_t;

static const clock_tz_entry_t tz_entries[] = {
    {"UTC-12", -12 * 3600},
    {"UTC-11", -11 * 3600},
    {"UTC-10", -10 * 3600},
    {"UTC-09", -9 * 3600},
    {"UTC-08 Pacific", -8 * 3600},
    {"UTC-07 Mountain", -7 * 3600},
    {"UTC-06 Central", -6 * 3600},
    {"UTC-05 Eastern", -5 * 3600},
    {"UTC-04", -4 * 3600},
    {"UTC-03", -3 * 3600},
    {"UTC-02", -2 * 3600},
    {"UTC-01", -1 * 3600},
    {"UTC", 0},
    {"UTC+01 CET", 1 * 3600},
    {"UTC+02", 2 * 3600},
    {"UTC+03", 3 * 3600},
    {"UTC+04", 4 * 3600},
    {"UTC+05", 5 * 3600},
    {"UTC+05:30 India", 5 * 3600 + 1800},
    {"UTC+06", 6 * 3600},
    {"UTC+07", 7 * 3600},
    {"UTC+08 China", 8 * 3600},
    {"UTC+09 Japan", 9 * 3600},
    {"UTC+10", 10 * 3600},
    {"UTC+11", 11 * 3600},
    {"UTC+12", 12 * 3600},
};

static lv_obj_t *page_container = NULL;
static lv_obj_t *current_time_label = NULL;
static lv_obj_t *timezone_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *timezone_dd = NULL;
static lv_obj_t *year_dd = NULL;
static lv_obj_t *month_dd = NULL;
static lv_obj_t *day_dd = NULL;
static lv_obj_t *hour_dd = NULL;
static lv_obj_t *minute_dd = NULL;
static lv_obj_t *second_dd = NULL;
static lv_timer_t *clock_timer = NULL;

static char year_options[96];
static char hour_options[96];
static char minute_options[192];
static char timezone_options[384];

static void append_option(char *out, size_t out_size, const char *text, bool newline)
{
    size_t len = strlen(out);
    if (len >= out_size) return;
    snprintf(out + len, out_size - len, "%s%s", newline ? "\n" : "", text);
}

static uint8_t tz_index_from_offset(int32_t offset)
{
    uint8_t best = 0;
    int32_t best_delta = labs(tz_entries[0].offset_sec - offset);
    for (uint8_t i = 1; i < sizeof(tz_entries) / sizeof(tz_entries[0]); ++i) {
        int32_t delta = labs(tz_entries[i].offset_sec - offset);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static int days_in_month(int year, int month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) return 31;
    return days[month - 1];
}

static void build_options(void)
{
    year_options[0] = 0;
    for (int y = CLOCK_YEAR_START; y <= CLOCK_YEAR_END; ++y) {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%d", y);
        append_option(year_options, sizeof(year_options), tmp, y != CLOCK_YEAR_START);
    }

    hour_options[0] = 0;
    for (int i = 0; i < 24; ++i) {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%02d", i);
        append_option(hour_options, sizeof(hour_options), tmp, i != 0);
    }

    minute_options[0] = 0;
    for (int i = 0; i < 60; ++i) {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%02d", i);
        append_option(minute_options, sizeof(minute_options), tmp, i != 0);
    }

    timezone_options[0] = 0;
    for (uint8_t i = 0; i < sizeof(tz_entries) / sizeof(tz_entries[0]); ++i) {
        append_option(timezone_options, sizeof(timezone_options), tz_entries[i].name, i != 0);
    }
}

static lv_obj_t *card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                               const char *options, uint8_t sel)
{
    lv_obj_t *row = ui_create_card_dropdown(card, icon, title, options, sel, NULL);
    lv_obj_t *dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    lv_obj_set_width(dd, 118);
    return dd;
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void format_offset(char *out, size_t out_size, int32_t offset)
{
    char sign = '+';
    if (offset < 0) {
        sign = '-';
        offset = -offset;
    }
    int hours = offset / 3600;
    int minutes = (offset % 3600) / 60;
    snprintf(out, out_size, "UTC%c%02d:%02d", sign, hours, minutes);
}

static void update_current_time_label(void)
{
    if (!current_time_label) return;
    char current[24];
    hw_get_date_time(current, sizeof(current));
    lv_label_set_text(current_time_label, current);

    if (timezone_label) {
        char buf[24];
        format_offset(buf, sizeof(buf), hw_get_timezone_offset());
        lv_label_set_text(timezone_label, buf);
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    update_current_time_label();
}

static void load_now_to_dropdowns(void)
{
    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    int year = timeinfo.tm_year + 1900;
    if (year < CLOCK_YEAR_START || year > CLOCK_YEAR_END) {
        year = CLOCK_YEAR_START;
    }
    lv_dropdown_set_selected(year_dd, year - CLOCK_YEAR_START);
    lv_dropdown_set_selected(month_dd, timeinfo.tm_mon >= 0 && timeinfo.tm_mon < 12 ? timeinfo.tm_mon : 0);
    int max_day = days_in_month(year, timeinfo.tm_mon + 1);
    int day = timeinfo.tm_mday;
    if (day < 1 || day > max_day) day = 1;
    lv_dropdown_set_selected(day_dd, day - 1);
    lv_dropdown_set_selected(hour_dd, timeinfo.tm_hour >= 0 && timeinfo.tm_hour < 24 ? timeinfo.tm_hour : 0);
    lv_dropdown_set_selected(minute_dd, timeinfo.tm_min >= 0 && timeinfo.tm_min < 60 ? timeinfo.tm_min : 0);
    lv_dropdown_set_selected(second_dd, timeinfo.tm_sec >= 0 && timeinfo.tm_sec < 60 ? timeinfo.tm_sec : 0);
    lv_dropdown_set_selected(timezone_dd, tz_index_from_offset(hw_get_timezone_offset()));
}

static void apply_time_cb(lv_event_t *e)
{
    uint8_t tz = lv_dropdown_get_selected(timezone_dd);
    if (tz >= sizeof(tz_entries) / sizeof(tz_entries[0])) {
        tz = tz_index_from_offset(GMT_OFFSET_SECOND);
    }

    int year = CLOCK_YEAR_START + lv_dropdown_get_selected(year_dd);
    int month = 1 + lv_dropdown_get_selected(month_dd);
    int day = 1 + lv_dropdown_get_selected(day_dd);
    int hour = lv_dropdown_get_selected(hour_dd);
    int minute = lv_dropdown_get_selected(minute_dd);
    int second = lv_dropdown_get_selected(second_dd);
    int max_day = days_in_month(year, month);
    if (day > max_day) day = max_day;

    struct tm next;
    memset(&next, 0, sizeof(next));
    next.tm_year = year - 1900;
    next.tm_mon = month - 1;
    next.tm_mday = day;
    next.tm_hour = hour;
    next.tm_min = minute;
    next.tm_sec = second;

    hw_set_timezone_offset(tz_entries[tz].offset_sec, 0);
    if (hw_set_date_time(next)) {
        set_status("Time applied.", UI_COLOR_ACCENT);
    } else {
        set_status("Set time failed.", lv_color_hex(0xFF4444));
    }
    update_current_time_label();
}

static void load_now_cb(lv_event_t *e)
{
    load_now_to_dropdowns();
    set_status("Loaded current clock.", UI_COLOR_TEXT_SECONDARY);
}

static void apply_timezone_cb(lv_event_t *e)
{
    uint8_t tz = lv_dropdown_get_selected(timezone_dd);
    if (tz >= sizeof(tz_entries) / sizeof(tz_entries[0])) {
        return;
    }
    hw_set_timezone_offset(tz_entries[tz].offset_sec, 0);
    set_status("Timezone saved.", UI_COLOR_ACCENT);
    update_current_time_label();
}

static void ntp_sync_cb(lv_event_t *e)
{
    uint8_t tz = lv_dropdown_get_selected(timezone_dd);
    if (tz >= sizeof(tz_entries) / sizeof(tz_entries[0])) {
        tz = tz_index_from_offset(GMT_OFFSET_SECOND);
    }

    set_status("Syncing NTP...", UI_COLOR_TEXT_SECONDARY);
    bool ok = hw_sync_time_from_ntp(tz_entries[tz].offset_sec, 0,
                                    "pool.ntp.org", "time.nist.gov", 5000);
    if (ok) {
        load_now_to_dropdowns();
        set_status("NTP sync OK.", UI_COLOR_ACCENT);
    } else {
        set_status("NTP sync failed. Check WiFi.", lv_color_hex(0xFF4444));
    }
    update_current_time_label();
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    return row;
}

static void back_event_handler(lv_event_t *e)
{
    if (clock_timer) {
        lv_timer_delete(clock_timer);
        clock_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    current_time_label = NULL;
    timezone_label = NULL;
    status_label = NULL;
    timezone_dd = NULL;
    year_dd = NULL;
    month_dd = NULL;
    day_dd = NULL;
    hour_dd = NULL;
    minute_dd = NULL;
    second_dd = NULL;
    menu_show();
}

void ui_clock_tools_enter(lv_obj_t *parent)
{
    build_options();
    page_container = ui_create_app_page(parent, "Clock Tools", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Clock");
    add_info_row(card, LV_SYMBOL_BELL, "Current", "--", &current_time_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "Timezone", "--", &timezone_label);
    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Ready.");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    card = ui_create_card(page_container, "Timezone");
    timezone_dd = card_dropdown(card, LV_SYMBOL_SETTINGS, "Zone", timezone_options,
                                tz_index_from_offset(hw_get_timezone_offset()));
    ui_create_card_button(card, LV_SYMBOL_SAVE, "Save Zone", "Apply", apply_timezone_cb);
    ui_create_card_button(card, LV_SYMBOL_WIFI, "Network Time", "NTP Sync", ntp_sync_cb);

    card = ui_create_card(page_container, "Manual Time");
    year_dd = card_dropdown(card, LV_SYMBOL_EDIT, "Year", year_options, 0);
    month_dd = card_dropdown(card, LV_SYMBOL_EDIT, "Month",
                             "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12", 0);
    day_dd = card_dropdown(card, LV_SYMBOL_EDIT, "Day",
                           "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31", 0);
    hour_dd = card_dropdown(card, LV_SYMBOL_EDIT, "Hour", hour_options, 0);
    minute_dd = card_dropdown(card, LV_SYMBOL_EDIT, "Minute", minute_options, 0);
    second_dd = card_dropdown(card, LV_SYMBOL_EDIT, "Second", minute_options, 0);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Read Clock", "Load Now", load_now_cb);
    ui_create_card_button(card, LV_SYMBOL_SAVE, "Set Clock", "Apply", apply_time_cb);

    load_now_to_dropdowns();
    update_current_time_label();
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
}

void ui_clock_tools_exit(lv_obj_t *parent)
{
}

app_t ui_clock_tools_main = {
    .setup_func_cb = ui_clock_tools_enter,
    .exit_func_cb = ui_clock_tools_exit,
    .user_data = nullptr,
};

#endif
