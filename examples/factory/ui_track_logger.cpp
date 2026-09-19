/**
 * @file      ui_track_logger.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-25
 * 
 * GNSS track recorder with local track preview.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_TRACK_LOGGER)

#ifdef ARDUINO
#include <SD.h>
#include <esp_heap_caps.h>
#endif
#include <math.h>
#include <stdlib.h>

#define TRACK_DIR "/tracks"
#define TRACK_MAX_POINTS 180
#define TRACK_INTERVAL_COUNT 4

typedef struct {
    double lat;
    double lng;
    double altitude;
    double speed;
    bool altitude_valid;
    bool speed_valid;
} track_point_t;

typedef struct {
    lv_obj_t *status;
    lv_obj_t *file;
    lv_obj_t *points;
    lv_obj_t *distance;
    lv_obj_t *interval;
    lv_obj_t *position;
    lv_obj_t *speed;
    lv_obj_t *altitude;
} track_labels_t;

static const uint32_t track_intervals_ms[TRACK_INTERVAL_COUNT] = {1000, 2000, 5000, 10000};

static lv_obj_t *page_container = NULL;
static lv_timer_t *track_timer = NULL;
static lv_obj_t *interval_dd = NULL;
static lv_obj_t *start_btn = NULL;
static lv_obj_t *start_btn_label = NULL;
static lv_obj_t *track_canvas = NULL;
static lv_obj_t *track_line = NULL;
static lv_obj_t *current_dot = NULL;
static track_labels_t labels = {};
static File track_file;
static bool recording = false;
static bool has_prev_point = false;
static uint8_t selected_interval = 2;
static uint16_t track_point_count = 0;
static uint32_t total_track_points = 0;
static uint32_t last_log_ms = 0;
static uint32_t record_start_ms = 0;
static double total_distance_m = 0.0;
static track_point_t *track_points = NULL;
static track_point_t prev_point = {};
static lv_point_precise_t *draw_points = NULL;
static char current_path[96] = "";

static void stop_recording(void);

static void *track_alloc(size_t size)
{
#ifdef ARDUINO
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return ptr;
#else
    return malloc(size);
#endif
}

static void track_free(void *ptr)
{
    if (!ptr) return;
#ifdef ARDUINO
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

static bool alloc_track_buffers(void)
{
    if (track_points && draw_points) return true;
    track_points = (track_point_t *)track_alloc(TRACK_MAX_POINTS * sizeof(track_point_t));
    draw_points = (lv_point_precise_t *)track_alloc(TRACK_MAX_POINTS * sizeof(lv_point_precise_t));
    if (!track_points || !draw_points) return false;
    memset(track_points, 0, TRACK_MAX_POINTS * sizeof(track_point_t));
    memset(draw_points, 0, TRACK_MAX_POINTS * sizeof(lv_point_precise_t));
    return true;
}

static void free_track_buffers(void)
{
    track_free(track_points);
    track_points = NULL;
    track_free(draw_points);
    draw_points = NULL;
}

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
    if (selected_interval >= TRACK_INTERVAL_COUNT) selected_interval = 2;
    return track_intervals_ms[selected_interval];
}

static void format_distance(char *out, size_t out_size, double meters)
{
    if (meters >= 1000.0) {
        snprintf(out, out_size, "%.2f km", meters / 1000.0);
    } else {
        snprintf(out, out_size, "%.0f m", meters);
    }
}

static double deg_to_rad(double value)
{
    return value * M_PI / 180.0;
}

static double distance_between_m(const track_point_t &a, const track_point_t &b)
{
    const double earth_radius_m = 6371000.0;
    double lat1 = deg_to_rad(a.lat);
    double lat2 = deg_to_rad(b.lat);
    double dlat = deg_to_rad(b.lat - a.lat);
    double dlng = deg_to_rad(b.lng - a.lng);
    double sin_dlat = sin(dlat / 2.0);
    double sin_dlng = sin(dlng / 2.0);
    double h = sin_dlat * sin_dlat + cos(lat1) * cos(lat2) * sin_dlng * sin_dlng;
    double c = 2.0 * atan2(sqrt(h), sqrt(1.0 - h));
    return earth_radius_m * c;
}

static bool ensure_track_dir(void)
{
#ifdef ARDUINO
    if (!hw_is_sd_insert()) {
        return false;
    }
    if (hw_get_sd_size() <= 0) return false;
    if (!SD.exists(TRACK_DIR)) {
        return SD.mkdir(TRACK_DIR);
    }
    return true;
#else
    return false;
#endif
}

static bool make_next_track_path(char *out, size_t out_size)
{
#ifdef ARDUINO
    for (uint16_t i = 1; i < 1000; ++i) {
        int written = snprintf(out, out_size, "%s/track_%03u.gpx", TRACK_DIR, i);
        if (written <= 0 || written >= (int)out_size) return false;
        if (!SD.exists(out)) return true;
    }
#endif
    return false;
}

static void write_gpx_header(File &file)
{
    file.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    file.println("<gpx version=\"1.1\" creator=\"LilyGo T-Deck\" xmlns=\"http://www.topografix.com/GPX/1/1\">");
    file.println("<trk><name>T-Deck Track</name><trkseg>");
}

static bool format_gpx_time(char *out, size_t out_size, const gps_params_t &param)
{
    if (!param.datetime_valid) return false;
    return strftime(out, out_size, "%Y-%m-%dT%H:%M:%SZ", &param.datetime) > 0;
}

static void write_gpx_point(File &file, const gps_params_t &param)
{
#ifdef ARDUINO
    char time_buf[32];
    bool has_time = format_gpx_time(time_buf, sizeof(time_buf), param);

    file.printf("  <trkpt lat=\"%.08f\" lon=\"%.08f\">", param.lat, param.lng);
    if (param.altitude_valid) {
        file.printf("<ele>%.2f</ele>", param.altitude);
    }
    if (has_time) {
        file.printf("<time>%s</time>", time_buf);
    }
    if (param.speed_valid) {
        file.printf("<extensions><speed_kmh>%.2f</speed_kmh></extensions>", param.speed);
    }
    file.println("</trkpt>");
#endif
}

static void append_display_point(const gps_params_t &param)
{
    if (!alloc_track_buffers()) return;

    track_point_t point = {};
    point.lat = param.lat;
    point.lng = param.lng;
    point.altitude = param.altitude;
    point.speed = param.speed;
    point.altitude_valid = param.altitude_valid;
    point.speed_valid = param.speed_valid;

    if (has_prev_point) {
        double step = distance_between_m(prev_point, point);
        if (step >= 0.0 && step < 10000.0) {
            total_distance_m += step;
        }
    }
    prev_point = point;
    has_prev_point = true;

    if (track_point_count < TRACK_MAX_POINTS) {
        track_points[track_point_count++] = point;
    } else {
        memmove(track_points, track_points + 1, sizeof(track_points[0]) * (TRACK_MAX_POINTS - 1));
        track_points[TRACK_MAX_POINTS - 1] = point;
    }
    total_track_points++;
}

static void update_track_preview(void)
{
    if (!track_canvas || !track_line || !current_dot || !track_points || !draw_points) return;

    if (track_point_count == 0) {
        lv_obj_add_flag(track_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(current_dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_update_layout(track_canvas);
    int32_t w = lv_obj_get_width(track_canvas) - 12;
    int32_t h = lv_obj_get_height(track_canvas) - 12;
    if (w < 10 || h < 10) return;

    double min_lat = track_points[0].lat;
    double max_lat = track_points[0].lat;
    double min_lng = track_points[0].lng;
    double max_lng = track_points[0].lng;
    for (uint16_t i = 1; i < track_point_count; ++i) {
        if (track_points[i].lat < min_lat) min_lat = track_points[i].lat;
        if (track_points[i].lat > max_lat) max_lat = track_points[i].lat;
        if (track_points[i].lng < min_lng) min_lng = track_points[i].lng;
        if (track_points[i].lng > max_lng) max_lng = track_points[i].lng;
    }

    double lat_range = max_lat - min_lat;
    double lng_range = max_lng - min_lng;
    if (lat_range < 0.000001) lat_range = 0.000001;
    if (lng_range < 0.000001) lng_range = 0.000001;

    for (uint16_t i = 0; i < track_point_count; ++i) {
        double x_norm = (track_points[i].lng - min_lng) / lng_range;
        double y_norm = (max_lat - track_points[i].lat) / lat_range;
        draw_points[i].x = 6 + (int32_t)round(x_norm * w);
        draw_points[i].y = 6 + (int32_t)round(y_norm * h);
    }

    if (track_point_count >= 2) {
        lv_line_set_points_mutable(track_line, draw_points, track_point_count);
        lv_obj_remove_flag(track_line, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(track_line, LV_OBJ_FLAG_HIDDEN);
    }

    lv_point_precise_t *last = &draw_points[track_point_count - 1];
    lv_obj_set_pos(current_dot, (lv_coord_t)last->x - 3, (lv_coord_t)last->y - 3);
    lv_obj_remove_flag(current_dot, LV_OBJ_FLAG_HIDDEN);
}

static void update_summary_labels(void)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)current_interval_ms());
    set_label_text(labels.interval, buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)total_track_points);
    set_label_text(labels.points, buf);

    format_distance(buf, sizeof(buf), total_distance_m);
    set_label_text(labels.distance, buf);

    set_label_text(labels.file, current_path[0] ? current_path : "--");
    if (start_btn_label) {
        lv_label_set_text(start_btn_label, recording ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Start");
        lv_obj_center(start_btn_label);
    }
}

static void update_live_labels(const gps_params_t &param)
{
    char buf[64];
    if (param.location_valid) {
        snprintf(buf, sizeof(buf), "%.06f, %.06f", param.lat, param.lng);
        set_label_text(labels.position, buf);
    } else {
        set_label_text(labels.position, "--");
    }

    if (param.speed_valid) {
        snprintf(buf, sizeof(buf), "%.2f km/h", param.speed);
        set_label_text(labels.speed, buf);
    } else {
        set_label_text(labels.speed, "--");
    }

    if (param.altitude_valid) {
        snprintf(buf, sizeof(buf), "%.1f m", param.altitude);
        set_label_text(labels.altitude, buf);
    } else {
        set_label_text(labels.altitude, "--");
    }
}

static bool start_recording(void)
{
#ifdef ARDUINO
    if (recording) return true;
    if (!ensure_track_dir()) {
        set_status("SD card is not available.", lv_color_hex(0xFF4444));
        return false;
    }
    if (!make_next_track_path(current_path, sizeof(current_path))) {
        set_status("Cannot create track path.", lv_color_hex(0xFF4444));
        return false;
    }

    track_file = SD.open(current_path, FILE_WRITE);
    if (!track_file) {
        current_path[0] = '\0';
        set_status("Failed to open GPX file.", lv_color_hex(0xFF4444));
        return false;
    }

    write_gpx_header(track_file);
    track_file.flush();

    recording = true;
    has_prev_point = false;
    track_point_count = 0;
    total_track_points = 0;
    total_distance_m = 0.0;
    last_log_ms = 0;
    record_start_ms = millis();
    set_status("Waiting for GNSS fix.", UI_COLOR_TEXT_SECONDARY);
    update_track_preview();
    update_summary_labels();
    return true;
#else
    return false;
#endif
}

static void stop_recording(void)
{
    if (track_file) {
        track_file.println("</trkseg></trk></gpx>");
        track_file.flush();
        track_file.close();
    }
    if (recording) {
        set_status("Recording stopped.", UI_COLOR_TEXT_SECONDARY);
    }
    recording = false;
    update_summary_labels();
}

static void record_sample(const gps_params_t &param)
{
    if (!recording || !track_file) return;

    if (!param.location_valid) {
        set_status("Waiting for GNSS fix.", UI_COLOR_TEXT_SECONDARY);
        return;
    }

    uint32_t now = millis();
    if (last_log_ms != 0 && now - last_log_ms < current_interval_ms()) {
        return;
    }
    last_log_ms = now;

    write_gpx_point(track_file, param);
    append_display_point(param);
    if ((total_track_points % 10) == 0) {
        track_file.flush();
    }
    set_status("Recording GPX.", UI_COLOR_ACCENT);
    update_summary_labels();
    update_track_preview();
}

static void timer_cb(lv_timer_t *t)
{
    gps_params_t param = {};
    hw_get_gps_info(param);
    update_live_labels(param);
    record_sample(param);
    update_summary_labels();
}

static void start_stop_cb(lv_event_t *e)
{
    if (recording) {
        stop_recording();
    } else {
        start_recording();
    }
}

static void interval_changed_cb(lv_event_t *e)
{
    if (!interval_dd) return;
    selected_interval = (uint8_t)lv_dropdown_get_selected(interval_dd);
    update_summary_labels();
}

static void back_event_handler(lv_event_t *e)
{
    stop_recording();
    if (track_timer) {
        lv_timer_del(track_timer);
        track_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    memset(&labels, 0, sizeof(labels));
    interval_dd = NULL;
    start_btn = NULL;
    start_btn_label = NULL;
    track_canvas = NULL;
    track_line = NULL;
    current_dot = NULL;
    current_path[0] = '\0';
    track_point_count = 0;
    total_track_points = 0;
    free_track_buffers();
    menu_show();
}

static void create_track_canvas(lv_obj_t *card)
{
    track_canvas = lv_obj_create(card);
    lv_obj_set_size(track_canvas, LV_PCT(100), is_screen_small() ? 118 : 148);
    lv_obj_set_style_bg_color(track_canvas, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(track_canvas, LV_OPA_70, 0);
    lv_obj_set_style_border_width(track_canvas, 1, 0);
    lv_obj_set_style_border_color(track_canvas, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(track_canvas, 6, 0);
    lv_obj_set_style_pad_all(track_canvas, 0, 0);
    lv_obj_remove_flag(track_canvas, LV_OBJ_FLAG_SCROLLABLE);

    track_line = lv_line_create(track_canvas);
    lv_obj_set_size(track_line, LV_PCT(100), LV_PCT(100));
    lv_obj_align(track_line, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_line_color(track_line, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_line_width(track_line, 3, 0);
    lv_obj_set_style_line_rounded(track_line, true, 0);
    lv_obj_add_flag(track_line, LV_OBJ_FLAG_HIDDEN);

    current_dot = lv_obj_create(track_canvas);
    lv_obj_set_size(current_dot, 6, 6);
    lv_obj_set_style_radius(current_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(current_dot, lv_color_hex(0xFFD400), 0);
    lv_obj_set_style_bg_opa(current_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(current_dot, 0, 0);
    lv_obj_add_flag(current_dot, LV_OBJ_FLAG_HIDDEN);
}

void ui_track_logger_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "Track Log", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Status");
    labels.status = lv_label_create(card);
    if (alloc_track_buffers()) {
        lv_label_set_text(labels.status, "Ready.");
    } else {
        lv_label_set_text(labels.status, "No RAM for track preview.");
    }
    lv_obj_set_style_text_color(labels.status, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(labels.status, &lv_font_montserrat_12, 0);
    add_info_row(card, LV_SYMBOL_FILE, "File", "--", &labels.file);
    lv_obj_set_width(labels.file, 150);
    lv_label_set_long_mode(labels.file, LV_LABEL_LONG_DOT);
    add_info_row(card, LV_SYMBOL_LIST, "Points", "0", &labels.points);
    add_info_row(card, LV_SYMBOL_GPS, "Distance", "0 m", &labels.distance);

    card = ui_create_card(page_container, "Record");
    interval_dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(interval_dd, "1s\n2s\n5s\n10s");
    lv_dropdown_set_selected(interval_dd, selected_interval);
    lv_obj_set_width(interval_dd, 92);
    lv_obj_set_height(interval_dd, 32);
    lv_obj_set_style_bg_color(interval_dd, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(interval_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(interval_dd, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(interval_dd, UI_COLOR_DIVIDER, 0);
    lv_obj_add_event_cb(interval_dd, interval_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(card, LV_SYMBOL_REFRESH, "Interval", interval_dd);
    add_info_row(card, LV_SYMBOL_REFRESH, "Period", "--", &labels.interval);

    start_btn = lv_btn_create(card);
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

    card = ui_create_card(page_container, "Live");
    add_info_row(card, LV_SYMBOL_GPS, "Position", "--", &labels.position);
    lv_obj_set_width(labels.position, 150);
    lv_label_set_long_mode(labels.position, LV_LABEL_LONG_DOT);
    add_info_row(card, LV_SYMBOL_GPS, "Speed", "--", &labels.speed);
    add_info_row(card, LV_SYMBOL_GPS, "Altitude", "--", &labels.altitude);

    card = ui_create_card(page_container, "Preview");
    create_track_canvas(card);

    update_summary_labels();
    track_timer = lv_timer_create(timer_cb, 1000, NULL);
    timer_cb(track_timer);
}

void ui_track_logger_exit(lv_obj_t *parent)
{
}

app_t ui_track_logger_main = {
    .setup_func_cb = ui_track_logger_enter,
    .exit_func_cb = ui_track_logger_exit,
    .user_data = nullptr,
};

#endif
