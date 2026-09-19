/**
 * @file      ui_sensor_logger.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-13
 * 
 * Sensor data logger: IMU + BME280 + power monitor to SD CSV.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_SENSOR_LOGGER)

#include <SD.h>
#include <math.h>

static constexpr const char *SENSOR_LOG_DIR = "/sensor_logs";
static constexpr uint16_t UI_REFRESH_MS = 1000;
static constexpr uint8_t LOG_INTERVAL_COUNT = 4;

static const uint32_t log_intervals_ms[LOG_INTERVAL_COUNT] = {1000, 2000, 5000, 10000};

typedef struct {
    float bme_temp;
    float bme_humi;
    float bme_press;
    float bme_alt;
    imu_params_t imu;
    monitor_params_t monitor;
    char datetime[24];
} sensor_log_sample_t;

typedef struct {
    lv_obj_t *status;
    lv_obj_t *file;
    lv_obj_t *samples;
    lv_obj_t *interval;
    lv_obj_t *imu;
    lv_obj_t *bme;
    lv_obj_t *power;
} sensor_log_labels_t;

static lv_obj_t *page_container = NULL;
static lv_obj_t *interval_dd = NULL;
static lv_obj_t *start_btn = NULL;
static lv_obj_t *start_btn_label = NULL;
static lv_timer_t *ui_timer = NULL;
static lv_timer_t *log_timer = NULL;
static sensor_log_labels_t labels = {};
static File log_file;
static bool log_running = false;
static uint32_t sample_count = 0;
static uint8_t selected_interval = 0;
static char current_path[96] = "";

static void stop_logging(void);

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label) lv_label_set_text(label, text);
}

static void set_status(const char *text, lv_color_t color)
{
    if (!labels.status) return;
    lv_label_set_text(labels.status, text);
    lv_obj_set_style_text_color(labels.status, color, 0);
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    return row;
}

static uint32_t current_interval_ms(void)
{
    if (selected_interval >= LOG_INTERVAL_COUNT) selected_interval = 0;
    return log_intervals_ms[selected_interval];
}

static void collect_sample(sensor_log_sample_t &sample)
{
    sample.bme_temp = NAN;
    sample.bme_humi = NAN;
    sample.bme_press = NAN;
    sample.bme_alt = NAN;
    sample.imu = {};
    sample.monitor = monitor_params_t();
    sample.datetime[0] = '\0';

    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    strftime(sample.datetime, sizeof(sample.datetime), "%Y-%m-%d %H:%M:%S", &timeinfo);

    hw_get_imu_params(sample.imu);

#if defined(USING_BME280)
    hw_bme_get_data(sample.bme_temp, sample.bme_humi, sample.bme_press, sample.bme_alt);
#else
    sample.bme_temp = NAN;
    sample.bme_humi = NAN;
    sample.bme_press = NAN;
    sample.bme_alt = NAN;
#endif

    hw_get_monitor_params(sample.monitor);
}

static bool ensure_log_dir(void)
{
#if defined(ARDUINO)
    if (!hw_is_sd_insert()) {
        return false;
    }
    if (hw_get_sd_size() <= 0) return false;
    if (!SD.exists(SENSOR_LOG_DIR)) {
        return SD.mkdir(SENSOR_LOG_DIR);
    }
    return true;
#else
    return false;
#endif
}

static bool make_next_log_path(char *out, size_t out_size)
{
#if defined(ARDUINO)
    for (uint16_t i = 1; i < 1000; ++i) {
        int written = snprintf(out, out_size, "%s/sensor_%03u.csv", SENSOR_LOG_DIR, i);
        if (written <= 0 || written >= (int)out_size) return false;
        if (!SD.exists(out)) return true;
    }
#endif
    return false;
}

static void write_csv_header(File &file)
{
    file.println(
        "millis,datetime,roll,pitch,heading,orientation,reverse,"
        "bme_temp_c,bme_humi_pct,bme_press_hpa,bme_alt_m,"
        "sys_mv,batt_mv,usb_mv,batt_pct,pmic_temp_c,current_ma,power_w,"
        "charge_state,ntc_state");
}

static void write_sample(File &file, const sensor_log_sample_t &sample)
{
#if defined(ARDUINO)
    file.printf(
        "%lu,\"%s\",%.2f,%.2f,%.2f,%u,%u,"
        "%.2f,%.2f,%.2f,%.2f,"
        "%u,%u,%u,%d,%.2f,%d,%.2f,\"%s\",\"%s\"\n",
        (unsigned long)millis(),
        sample.datetime,
        sample.imu.roll,
        sample.imu.pitch,
        sample.imu.heading,
        sample.imu.orientation,
        sample.imu.reverse,
        sample.bme_temp,
        sample.bme_humi,
        sample.bme_press,
        sample.bme_alt,
        sample.monitor.sys_voltage,
        sample.monitor.battery_voltage,
        sample.monitor.usb_voltage,
        sample.monitor.battery_percent,
        sample.monitor.temperature,
        sample.monitor.instantaneousCurrent,
        sample.monitor.instantaneousPower,
        sample.monitor.charge_state,
        sample.monitor.ntc_state);
#endif
}

static void update_live_labels(const sensor_log_sample_t &sample)
{
    char buf[96];

    snprintf(buf, sizeof(buf), "R %.1f  P %.1f  H %.1f",
             sample.imu.roll, sample.imu.pitch, sample.imu.heading);
    set_label_text(labels.imu, buf);

    if (isnan(sample.bme_temp)) {
        snprintf(buf, sizeof(buf), "N.A");
    } else {
        snprintf(buf, sizeof(buf), "%.1fC  %.0f%%  %.0fhPa",
                 sample.bme_temp, sample.bme_humi, sample.bme_press);
    }
    set_label_text(labels.bme, buf);

    snprintf(buf, sizeof(buf), "%u%%  %umV  %s",
             sample.monitor.battery_percent,
             sample.monitor.battery_voltage,
             sample.monitor.charge_state);
    set_label_text(labels.power, buf);
}

static void update_status_labels(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)current_interval_ms());
    set_label_text(labels.interval, buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)sample_count);
    set_label_text(labels.samples, buf);

    set_label_text(labels.file, current_path[0] ? current_path : "--");
    if (start_btn_label) {
        lv_label_set_text(start_btn_label, log_running ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Start");
        lv_obj_center(start_btn_label);
    }
}

static void ui_update_cb(lv_timer_t *timer)
{
    sensor_log_sample_t sample;
    collect_sample(sample);
    update_live_labels(sample);
    update_status_labels();
}

static void log_sample_cb(lv_timer_t *timer)
{
    if (!log_running || !log_file) return;

    sensor_log_sample_t sample;
    collect_sample(sample);
    write_sample(log_file, sample);
    sample_count++;

    if ((sample_count % 10) == 0) log_file.flush();
    update_live_labels(sample);
    update_status_labels();
}

static bool start_logging(void)
{
#if defined(ARDUINO)
    if (log_running) return true;
    if (!ensure_log_dir()) {
        set_status("SD card is not available.", lv_color_hex(0xFF4444));
        return false;
    }
    if (!make_next_log_path(current_path, sizeof(current_path))) {
        set_status("Cannot create log path.", lv_color_hex(0xFF4444));
        return false;
    }

    log_file = SD.open(current_path, FILE_WRITE);
    if (!log_file) {
        current_path[0] = '\0';
        set_status("Failed to open log file.", lv_color_hex(0xFF4444));
        return false;
    }

    write_csv_header(log_file);
    log_file.flush();
    sample_count = 0;
    log_running = true;
    hw_register_imu_process();

    if (log_timer) {
        lv_timer_del(log_timer);
        log_timer = NULL;
    }
    log_timer = lv_timer_create(log_sample_cb, current_interval_ms(), NULL);
    log_sample_cb(log_timer);
    set_status("Recording to SD.", UI_COLOR_ACCENT);
    update_status_labels();
    return true;
#else
    return false;
#endif
}

static void stop_logging(void)
{
    if (log_timer) {
        lv_timer_del(log_timer);
        log_timer = NULL;
    }
    if (log_file) {
        log_file.flush();
        log_file.close();
    }
    if (log_running) {
        set_status("Recording stopped.", UI_COLOR_TEXT_SECONDARY);
    }
    log_running = false;
    update_status_labels();
}

static void start_stop_cb(lv_event_t *e)
{
    if (log_running) {
        stop_logging();
    } else {
        start_logging();
    }
}

static void interval_changed_cb(lv_event_t *e)
{
    if (!interval_dd) return;
    selected_interval = (uint8_t)lv_dropdown_get_selected(interval_dd);
    if (log_running && log_timer) {
        lv_timer_set_period(log_timer, current_interval_ms());
    }
    update_status_labels();
}

static void back_event_handler(lv_event_t *e)
{
    stop_logging();
    if (ui_timer) {
        lv_timer_del(ui_timer);
        ui_timer = NULL;
    }
    hw_unregister_imu_process();

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    memset(&labels, 0, sizeof(labels));
    interval_dd = NULL;
    start_btn = NULL;
    start_btn_label = NULL;
    current_path[0] = '\0';
    sample_count = 0;
    menu_show();
}

void ui_sensor_logger_enter(lv_obj_t *parent)
{
    hw_register_imu_process();
    page_container = ui_create_app_page(parent, "Sensor Log", back_event_handler);

    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    labels.status = lv_label_create(status_card);
    lv_label_set_text(labels.status, "Ready.");
    lv_obj_set_style_text_color(labels.status, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(labels.status, &lv_font_montserrat_12, 0);

    lv_obj_t *control_card = ui_create_card(page_container, "Record");

    interval_dd = lv_dropdown_create(lv_obj_create(control_card));
    lv_dropdown_set_options(interval_dd, "1s\n2s\n5s\n10s");
    lv_dropdown_set_selected(interval_dd, selected_interval);
    lv_obj_set_width(interval_dd, 92);
    lv_obj_set_height(interval_dd, 32);
    lv_obj_set_style_bg_color(interval_dd, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(interval_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(interval_dd, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(interval_dd, UI_COLOR_DIVIDER, 0);
    lv_obj_add_event_cb(interval_dd, interval_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(control_card, LV_SYMBOL_SETTINGS, "Interval", interval_dd);

    add_info_row(control_card, LV_SYMBOL_REFRESH, "Period", "--", &labels.interval);
    add_info_row(control_card, LV_SYMBOL_FILE, "File", "--", &labels.file);
    lv_obj_set_width(labels.file, 150);
    lv_label_set_long_mode(labels.file, LV_LABEL_LONG_DOT);
    add_info_row(control_card, LV_SYMBOL_LIST, "Samples", "0", &labels.samples);

    start_btn = lv_btn_create(control_card);
    lv_obj_set_size(start_btn, LV_PCT(60), 36);
    lv_obj_set_style_bg_color(start_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(start_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(start_btn, 8, 0);
    lv_obj_set_style_border_width(start_btn, 0, 0);
    ui_add_accent_focus_style(start_btn);
    lv_obj_add_event_cb(start_btn, start_stop_cb, LV_EVENT_CLICKED, NULL);
    start_btn_label = lv_label_create(start_btn);
    lv_obj_set_style_text_color(start_btn_label, lv_color_white(), 0);
    lv_label_set_text(start_btn_label, LV_SYMBOL_PLAY " Start");
    lv_obj_center(start_btn_label);

    lv_obj_t *live_card = ui_create_card(page_container, "Live Data");
    add_info_row(live_card, LV_SYMBOL_REFRESH, "IMU", "--", &labels.imu);
    add_info_row(live_card, LV_SYMBOL_REFRESH, "BME", "--", &labels.bme);
    add_info_row(live_card, LV_SYMBOL_POWER, "Power", "--", &labels.power);

    update_status_labels();
    ui_timer = lv_timer_create(ui_update_cb, UI_REFRESH_MS, NULL);
    ui_update_cb(ui_timer);
}

void ui_sensor_logger_exit(lv_obj_t *parent)
{
}

app_t ui_sensor_logger_main = {
    .setup_func_cb = ui_sensor_logger_enter,
    .exit_func_cb = ui_sensor_logger_exit,
    .user_data = nullptr,
};

#endif
