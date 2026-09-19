/**
 * @file      ui_wifi_analyzer.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-08-24
 * 
 * WiFi Analyzer: RF environment overview, channel overlap graph and AP details.
 */
#include "ui_define.h"

#ifndef EXCLUDE_WIFI_ANALYZER

#include <algorithm>

#ifdef ARDUINO
#include <SD.h>
#endif

#define WIFI_AUTO_SCAN_DEFAULT_MS 10000
#define WIFI_SCAN_POLL_MS        200
#define WIFI_MAX_CHANNELS        14
#define WIFI_MAX_AP_DISPLAY      24
#define WIFI_MAX_CURVES          16
#define WIFI_CURVE_POINTS        5
#define WIFI_GRAPH_W             262
#define WIFI_GRAPH_H             108

static lv_obj_t *page_container = NULL;
static lv_timer_t *scan_poll_timer = NULL;
static lv_timer_t *auto_scan_timer = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *scan_label = NULL;
static lv_obj_t *count_label = NULL;
static lv_obj_t *open_label = NULL;
static lv_obj_t *best_channel_label = NULL;
static lv_obj_t *strongest_label = NULL;
static lv_obj_t *connected_label = NULL;
static lv_obj_t *connected_rssi_label = NULL;
static lv_obj_t *recommend_label = NULL;

static lv_obj_t *graph_area = NULL;
static lv_obj_t *ap_list_card = NULL;
static lv_obj_t *detail_ssid_label = NULL;
static lv_obj_t *detail_bssid_label = NULL;
static lv_obj_t *detail_channel_label = NULL;
static lv_obj_t *detail_auth_label = NULL;
static lv_obj_t *detail_quality_label = NULL;
static lv_obj_t *detail_interference_label = NULL;
static lv_obj_t *auto_switch = NULL;
static lv_obj_t *interval_dd = NULL;

static lv_obj_t *ap_rows[WIFI_MAX_AP_DISPLAY];
static lv_obj_t *curve_lines[WIFI_MAX_CURVES];
static lv_point_precise_t curve_points[WIFI_MAX_CURVES][WIFI_CURVE_POINTS];
static vector<wifi_scan_params_t> scan_results;

static bool scanning = false;
static bool auto_scan_enabled = true;
static int selected_index = -1;

static int rssi_to_percent(int8_t rssi)
{
    if (rssi >= -30) return 100;
    if (rssi <= -90) return 0;
    return (int)((rssi + 90) * 100 / 60);
}

static const char *auth_mode_str(uint8_t mode)
{
    switch (mode) {
    case 0: return "Open";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/WPA2";
    case 5: return "WPA2 Ent";
    case 6: return "WPA3";
    case 7: return "WPA2/WPA3";
    case 8: return "WAPI";
    default: return "Unknown";
    }
}

static bool auth_is_open(uint8_t mode)
{
    return mode == 0;
}

static const char *ssid_display(const wifi_scan_params_t &ap)
{
    return ap.ssid[0] ? ap.ssid : "<hidden>";
}

static void format_bssid(const uint8_t *bssid, char *out, size_t out_size)
{
    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static lv_color_t rssi_color(int8_t rssi)
{
    if (rssi >= -55) return lv_color_hex(0x00D4AA);
    if (rssi >= -70) return lv_color_hex(0xFFB800);
    if (rssi >= -82) return lv_color_hex(0xFF6B35);
    return lv_color_hex(0x777777);
}

static uint32_t interval_from_dropdown(void)
{
    if (!interval_dd) return WIFI_AUTO_SCAN_DEFAULT_MS;
    switch (lv_dropdown_get_selected(interval_dd)) {
    case 0: return 5000;
    case 1: return 10000;
    case 2: return 30000;
    default: return WIFI_AUTO_SCAN_DEFAULT_MS;
    }
}

static int graph_width(void)
{
    if (graph_area) {
        lv_obj_update_layout(graph_area);
        int width = lv_obj_get_content_width(graph_area);
        if (width > 16) return width;
        width = lv_obj_get_width(graph_area);
        if (width > 16) return width;
    }
    return WIFI_GRAPH_W;
}

static int channel_x(float channel)
{
    float norm = (channel - 1.0f) / (float)(WIFI_MAX_CHANNELS - 1);
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    return 8 + (int)(norm * (graph_width() - 16));
}

static int rssi_y(int8_t rssi)
{
    int pct = rssi_to_percent(rssi);
    return WIFI_GRAPH_H - 8 - (pct * (WIFI_GRAPH_H - 22) / 100);
}

static int channel_score(int channel)
{
    int score = 0;
    for (auto &ap : scan_results) {
        int ap_ch = (int)ap.channel;
        if (ap_ch < 1 || ap_ch > WIFI_MAX_CHANNELS) continue;
        int delta = abs(ap_ch - channel);
        if (delta > 4) continue;
        int weight = 5 - delta;
        score += rssi_to_percent(ap.rssi) * weight;
    }
    return score;
}

static int same_channel_count(int channel)
{
    int count = 0;
    for (auto &ap : scan_results) {
        if ((int)ap.channel == channel) count++;
    }
    return count;
}

static int adjacent_channel_count(int channel)
{
    int count = 0;
    for (auto &ap : scan_results) {
        int delta = abs((int)ap.channel - channel);
        if (delta > 0 && delta <= 2) count++;
    }
    return count;
}

static int best_24g_channel(void)
{
    static const int candidates[] = {1, 6, 11};
    int best = candidates[0];
    int best_score = channel_score(best);
    for (uint8_t i = 1; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        int score = channel_score(candidates[i]);
        if (score < best_score) {
            best = candidates[i];
            best_score = score;
        }
    }
    return best;
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void clear_ap_rows(void)
{
    for (int i = 0; i < WIFI_MAX_AP_DISPLAY; ++i) {
        if (ap_rows[i]) {
            lv_obj_delete(ap_rows[i]);
            ap_rows[i] = NULL;
        }
    }
}

static void clear_curves(void)
{
    for (int i = 0; i < WIFI_MAX_CURVES; ++i) {
        if (curve_lines[i]) {
            lv_obj_delete(curve_lines[i]);
            curve_lines[i] = NULL;
        }
    }
}

static void update_connection_labels(void)
{
    char ssid[WIFI_SSID_MAX_LEN];
    hw_get_wifi_ssid(ssid, sizeof(ssid));
    wl_status_t wifi_status = hw_get_wifi_status();
    if (connected_label) {
        lv_label_set_text(connected_label, wifi_status == WL_CONNECTED ? ssid : "Not connected");
    }
    if (connected_rssi_label) {
        if (wifi_status == WL_CONNECTED) {
            lv_label_set_text_fmt(connected_rssi_label, "%d dBm", hw_get_wifi_rssi());
        } else {
            lv_label_set_text(connected_rssi_label, "--");
        }
    }
}

static void update_detail(int index)
{
    selected_index = index;
    if (index < 0 || index >= (int)scan_results.size()) {
        if (detail_ssid_label) lv_label_set_text(detail_ssid_label, "--");
        if (detail_bssid_label) lv_label_set_text(detail_bssid_label, "--");
        if (detail_channel_label) lv_label_set_text(detail_channel_label, "--");
        if (detail_auth_label) lv_label_set_text(detail_auth_label, "--");
        if (detail_quality_label) lv_label_set_text(detail_quality_label, "--");
        if (detail_interference_label) lv_label_set_text(detail_interference_label, "--");
        return;
    }

    const wifi_scan_params_t &ap = scan_results[index];
    char bssid[24];
    char buf[64];
    format_bssid(ap.bssid, bssid, sizeof(bssid));

    if (detail_ssid_label) lv_label_set_text(detail_ssid_label, ssid_display(ap));
    if (detail_bssid_label) lv_label_set_text(detail_bssid_label, bssid);
    if (detail_channel_label) {
        snprintf(buf, sizeof(buf), "CH%d  %d dBm", (int)ap.channel, ap.rssi);
        lv_label_set_text(detail_channel_label, buf);
    }
    if (detail_auth_label) lv_label_set_text(detail_auth_label, auth_mode_str(ap.authmode));
    if (detail_quality_label) {
        snprintf(buf, sizeof(buf), "%d%%", rssi_to_percent(ap.rssi));
        lv_label_set_text(detail_quality_label, buf);
    }
    if (detail_interference_label) {
        int same = same_channel_count((int)ap.channel);
        int adj = adjacent_channel_count((int)ap.channel);
        snprintf(buf, sizeof(buf), "Same %d / Adj %d", same > 0 ? same - 1 : 0, adj);
        lv_label_set_text(detail_interference_label, buf);
    }

    for (int i = 0; i < WIFI_MAX_AP_DISPLAY; ++i) {
        if (!ap_rows[i]) continue;
        if (i == index) {
            lv_obj_set_style_bg_color(ap_rows[i], UI_COLOR_CARD_FOCUS, 0);
            lv_obj_set_style_bg_opa(ap_rows[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(ap_rows[i], UI_COLOR_ACCENT, 0);
            lv_obj_set_style_border_width(ap_rows[i], 1, 0);
        } else {
            lv_obj_set_style_bg_opa(ap_rows[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(ap_rows[i], 0, 0);
        }
    }
}

static void ap_row_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_group_t *group = obj ? (lv_group_t *)lv_obj_get_group(obj) : NULL;
    if (group) {
        lv_group_set_editing(group, false);
    }
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    update_detail(index);
}

static void focus_scroll_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (obj) {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(obj);
        if (group) {
            lv_group_set_editing(group, false);
        }
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
    }
}

static void build_graph_axis(void)
{
    if (!graph_area) return;
    lv_obj_update_layout(graph_area);

    for (int ch = 1; ch <= WIFI_MAX_CHANNELS; ++ch) {
        int x = channel_x((float)ch);
        lv_obj_t *line = lv_obj_create(graph_area);
        lv_obj_set_size(line, 1, WIFI_GRAPH_H - 18);
        lv_obj_set_pos(line, x, 4);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_70, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_radius(line, 0, 0);

        if (ch == 1 || ch == 6 || ch == 11 || ch == 14) {
            char label[4];
            snprintf(label, sizeof(label), "%d", ch);
            lv_obj_t *lbl = lv_label_create(graph_area);
            lv_label_set_text(lbl, label);
            lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_pos(lbl, x - 5, WIFI_GRAPH_H - 15);
        }
    }
}

static void build_channel_graph(void)
{
    clear_curves();
    if (!graph_area || scan_results.empty()) return;

    int curves = scan_results.size() < WIFI_MAX_CURVES ? scan_results.size() : WIFI_MAX_CURVES;
    for (int i = 0; i < curves; ++i) {
        const wifi_scan_params_t &ap = scan_results[i];
        int ch = (int)ap.channel;
        if (ch < 1 || ch > WIFI_MAX_CHANNELS) continue;

        int base_y = WIFI_GRAPH_H - 20;
        int peak_y = rssi_y(ap.rssi);
        int mid_y = peak_y + (base_y - peak_y) / 2;

        curve_points[i][0].x = channel_x(ch - 2.2f);
        curve_points[i][0].y = base_y;
        curve_points[i][1].x = channel_x(ch - 1.0f);
        curve_points[i][1].y = mid_y;
        curve_points[i][2].x = channel_x((float)ch);
        curve_points[i][2].y = peak_y;
        curve_points[i][3].x = channel_x(ch + 1.0f);
        curve_points[i][3].y = mid_y;
        curve_points[i][4].x = channel_x(ch + 2.2f);
        curve_points[i][4].y = base_y;

        lv_obj_t *line = lv_line_create(graph_area);
        curve_lines[i] = line;
        lv_line_set_points_mutable(line, curve_points[i], WIFI_CURVE_POINTS);
        lv_obj_set_style_line_color(line, rssi_color(ap.rssi), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_opa(line, auth_is_open(ap.authmode) ? LV_OPA_90 : LV_OPA_70, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }
}

static void update_recommendation(void)
{
    if (!recommend_label) return;
    if (scan_results.empty()) {
        lv_label_set_text(recommend_label, "No AP data.");
        return;
    }

    int score1 = channel_score(1);
    int score6 = channel_score(6);
    int score11 = channel_score(11);
    int best = best_24g_channel();
    char buf[96];
    snprintf(buf, sizeof(buf), "Best CH%d   load 1:%d  6:%d  11:%d", best, score1, score6, score11);
    lv_label_set_text(recommend_label, buf);
}

static void build_ap_list(void)
{
    clear_ap_rows();
    if (!ap_list_card) return;

    if (scan_results.empty()) {
        lv_obj_t *empty = lv_label_create(ap_list_card);
        ap_rows[0] = empty;
        lv_label_set_text(empty, "No AP found. Press Scan.");
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
        update_detail(-1);
        return;
    }

    int limit = scan_results.size() < WIFI_MAX_AP_DISPLAY ? scan_results.size() : WIFI_MAX_AP_DISPLAY;
    for (int i = 0; i < limit; ++i) {
        const wifi_scan_params_t &ap = scan_results[i];
        char bssid[24];
        char sub[96];
        char rssi[16];
        format_bssid(ap.bssid, bssid, sizeof(bssid));
        snprintf(sub, sizeof(sub), "%s  CH%d  %s", bssid, (int)ap.channel, auth_mode_str(ap.authmode));
        snprintf(rssi, sizeof(rssi), "%d dBm", ap.rssi);

        lv_obj_t *row = lv_obj_create(ap_list_card);
        ap_rows[i] = row;
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, auth_is_open(ap.authmode) ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, auth_is_open(ap.authmode) ? lv_color_hex(0x4A2E17) : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_top(row, 5, 0);
        lv_obj_set_style_pad_bottom(row, 5, 0);
        lv_obj_set_style_pad_left(row, 0, 0);
        lv_obj_set_style_pad_right(row, 0, 0);
        lv_obj_set_style_pad_column(row, 7, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_FOCUS, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(row, ap_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, focus_scroll_cb, LV_EVENT_FOCUSED, NULL);
        lv_group_t *group = lv_group_get_default();
        if (group && lv_obj_get_group(row) != group) {
            lv_group_add_obj(group, row);
        }

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, rssi_color(ap.rssi), 0);

        lv_obj_t *text_col = lv_obj_create(row);
        lv_obj_set_size(text_col, 1, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(text_col, 1);
        lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_col, 0, 0);
        lv_obj_set_style_pad_all(text_col, 0, 0);
        lv_obj_set_style_pad_row(text_col, 2, 0);
        lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *ssid = lv_label_create(text_col);
        lv_label_set_text(ssid, ssid_display(ap));
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid, LV_PCT(100));
        lv_obj_set_style_text_color(ssid, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(ssid, &lv_font_montserrat_12, 0);

        lv_obj_t *meta = lv_label_create(text_col);
        lv_label_set_text(meta, sub);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_obj_set_width(meta, LV_PCT(100));
        lv_obj_set_style_text_color(meta, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);

        lv_obj_t *right = lv_obj_create(row);
        lv_obj_set_size(right, 58, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right, 0, 0);
        lv_obj_set_style_pad_all(right, 0, 0);
        lv_obj_set_style_pad_row(right, 4, 0);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *rssi_lbl = lv_label_create(right);
        lv_label_set_text(rssi_lbl, rssi);
        lv_obj_set_style_text_color(rssi_lbl, rssi_color(ap.rssi), 0);
        lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_12, 0);

        lv_obj_t *bar = lv_bar_create(right);
        lv_obj_set_size(bar, 52, 6);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, rssi_to_percent(ap.rssi), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, rssi_color(ap.rssi), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    }

    update_detail(0);
}

static void update_overview(void)
{
    int open_count = 0;
    for (auto &ap : scan_results) {
        if (auth_is_open(ap.authmode)) open_count++;
    }

    if (count_label) lv_label_set_text_fmt(count_label, "%u", (unsigned)scan_results.size());
    if (open_label) lv_label_set_text_fmt(open_label, "%d", open_count);
    if (strongest_label) {
        if (scan_results.empty()) {
            lv_label_set_text(strongest_label, "--");
        } else {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s  %d dBm", ssid_display(scan_results[0]), scan_results[0].rssi);
            lv_label_set_text(strongest_label, buf);
        }
    }
    if (best_channel_label) {
        if (scan_results.empty()) {
            lv_label_set_text(best_channel_label, "--");
        } else {
            lv_label_set_text_fmt(best_channel_label, "CH%d", best_24g_channel());
        }
    }
    update_connection_labels();
    update_recommendation();
}

static void apply_scan_results(vector<wifi_scan_params_t> &results)
{
    scan_results = results;
    sort(scan_results.begin(), scan_results.end(), [](const wifi_scan_params_t &a, const wifi_scan_params_t &b) {
        if (a.rssi != b.rssi) return a.rssi > b.rssi;
        return strcmp(a.ssid, b.ssid) < 0;
    });

    update_overview();
    build_channel_graph();
    build_ap_list();

    if (scan_label) {
        char now[24];
        hw_get_date_time(now, sizeof(now));
        lv_label_set_text_fmt(scan_label, "Last scan %s", now);
    }

    if (scan_results.empty()) {
        set_status("Scan complete. No AP found.", UI_COLOR_WARNING);
    } else {
        set_status("Scan complete.", UI_COLOR_ACCENT);
    }
}

static void scan_complete_cb(lv_timer_t *timer)
{
    if (hw_get_wifi_scanning()) return;

    scanning = false;
    if (scan_poll_timer) {
        lv_timer_del(scan_poll_timer);
        scan_poll_timer = NULL;
    }

    vector<wifi_scan_params_t> results;
    hw_get_wifi_scan_result(results);
    apply_scan_results(results);
}

static void trigger_scan(void)
{
    if (scanning) return;
    scanning = true;
    set_status("Scanning...", UI_COLOR_TEXT_SECONDARY);
    if (scan_label) lv_label_set_text(scan_label, "Scanning channels...");

    int16_t rc = hw_set_wifi_scan();
    if (rc == -2) {
        scanning = false;
        set_status("Scan failed to start.", lv_color_hex(0xFF4444));
        return;
    }

    if (scan_poll_timer) {
        lv_timer_del(scan_poll_timer);
        scan_poll_timer = NULL;
    }
    scan_poll_timer = lv_timer_create(scan_complete_cb, WIFI_SCAN_POLL_MS, NULL);
}

static void auto_scan_cb(lv_timer_t *timer)
{
    if (auto_scan_enabled) trigger_scan();
}

static void refresh_btn_cb(lv_event_t *e)
{
    trigger_scan();
}

static void auto_switch_cb(lv_event_t *e)
{
    auto_scan_enabled = lv_obj_has_state(auto_switch, LV_STATE_CHECKED);
    if (auto_scan_enabled) {
        if (!auto_scan_timer) {
            auto_scan_timer = lv_timer_create(auto_scan_cb, interval_from_dropdown(), NULL);
        }
        set_status("Auto scan enabled.", UI_COLOR_ACCENT);
    } else {
        if (auto_scan_timer) {
            lv_timer_del(auto_scan_timer);
            auto_scan_timer = NULL;
        }
        set_status("Auto scan disabled.", UI_COLOR_TEXT_SECONDARY);
    }
}

static void interval_cb(lv_event_t *e)
{
    if (auto_scan_timer) {
        lv_timer_set_period(auto_scan_timer, interval_from_dropdown());
    }
}

static void export_csv_cb(lv_event_t *e)
{
#ifdef ARDUINO
    if (!hw_is_sd_insert()) {
        set_status("No SD card.", lv_color_hex(0xFF4444));
        return;
    }

    bool exists = SD.exists("/wifi_scan.csv");
    File file = SD.open("/wifi_scan.csv", FILE_APPEND);
    if (!file) {
        set_status("Export failed.", lv_color_hex(0xFF4444));
        return;
    }

    if (!exists) {
        file.println("time,ssid,bssid,channel,rssi,quality,auth");
    }

    char now[24];
    hw_get_date_time(now, sizeof(now));
    char bssid[24];
    for (auto &ap : scan_results) {
        format_bssid(ap.bssid, bssid, sizeof(bssid));
        file.printf("\"%s\",\"%s\",%s,%d,%d,%d,\"%s\"\n",
                    now, ssid_display(ap), bssid, (int)ap.channel,
                    ap.rssi, rssi_to_percent(ap.rssi), auth_mode_str(ap.authmode));
    }
    file.close();
    set_status("Exported /wifi_scan.csv", UI_COLOR_ACCENT);
#else
    set_status("Export unavailable.", lv_color_hex(0xFF4444));
#endif
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    if (out_label && *out_label) {
        lv_obj_set_width(*out_label, 138);
        lv_label_set_long_mode(*out_label, LV_LABEL_LONG_DOT);
    }
    return row;
}

static lv_obj_t *card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                               const char *options, uint8_t sel, lv_event_cb_t cb)
{
    lv_obj_t *row = ui_create_card_dropdown(card, icon, title, options, sel, cb);
    lv_obj_t *dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    lv_obj_set_width(dd, 110);
    return dd;
}

static lv_obj_t *card_switch(lv_obj_t *card, const char *icon, const char *title, bool checked, lv_event_cb_t cb)
{
    lv_obj_t *row = ui_create_card_switch(card, icon, title, checked, cb);
    return lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
}

static void back_event_handler(lv_event_t *e)
{
    if (auto_scan_timer) {
        lv_timer_del(auto_scan_timer);
        auto_scan_timer = NULL;
    }
    if (scan_poll_timer) {
        lv_timer_del(scan_poll_timer);
        scan_poll_timer = NULL;
    }
    scanning = false;

    clear_curves();
    clear_ap_rows();
    scan_results.clear();

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    status_label = NULL;
    scan_label = NULL;
    count_label = NULL;
    open_label = NULL;
    best_channel_label = NULL;
    strongest_label = NULL;
    connected_label = NULL;
    connected_rssi_label = NULL;
    recommend_label = NULL;
    graph_area = NULL;
    ap_list_card = NULL;
    detail_ssid_label = NULL;
    detail_bssid_label = NULL;
    detail_channel_label = NULL;
    detail_auth_label = NULL;
    detail_quality_label = NULL;
    detail_interference_label = NULL;
    auto_switch = NULL;
    interval_dd = NULL;
    selected_index = -1;

    menu_show();
}

void ui_wifi_analyzer_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "WiFi Analyzer", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Overview");
    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Ready.");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    add_info_row(card, LV_SYMBOL_WIFI, "Connected", "--", &connected_label);
    add_info_row(card, LV_SYMBOL_WIFI, "RSSI", "--", &connected_rssi_label);
    add_info_row(card, LV_SYMBOL_LIST, "AP Count", "0", &count_label);
    add_info_row(card, LV_SYMBOL_WARNING, "Open AP", "0", &open_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "Best CH", "--", &best_channel_label);
    add_info_row(card, LV_SYMBOL_WIFI, "Strongest", "--", &strongest_label);

    scan_label = lv_label_create(card);
    lv_label_set_text(scan_label, "No scan yet.");
    lv_obj_set_style_text_color(scan_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_12, 0);

    card = ui_create_card(page_container, "Channel Overlap");
    graph_area = lv_obj_create(card);
    lv_obj_set_width(graph_area, LV_PCT(100));
    lv_obj_set_height(graph_area, WIFI_GRAPH_H);
    lv_obj_set_style_bg_color(graph_area, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(graph_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(graph_area, 1, 0);
    lv_obj_set_style_border_color(graph_area, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(graph_area, 4, 0);
    lv_obj_set_style_pad_all(graph_area, 0, 0);
    lv_obj_remove_flag(graph_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(graph_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(graph_area, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_border_color(graph_area, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(graph_area, LV_OPA_80, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(graph_area, focus_scroll_cb, LV_EVENT_FOCUSED, NULL);
    lv_group_t *group = lv_group_get_default();
    if (group && lv_obj_get_group(graph_area) != group) {
        lv_group_add_obj(group, graph_area);
    }
    build_graph_axis();

    recommend_label = lv_label_create(card);
    lv_label_set_text(recommend_label, "No AP data.");
    lv_obj_set_style_text_color(recommend_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(recommend_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(recommend_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(recommend_label, LV_PCT(100));

    card = ui_create_card(page_container, "Selected AP");
    add_info_row(card, LV_SYMBOL_WIFI, "SSID", "--", &detail_ssid_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "BSSID", "--", &detail_bssid_label);
    add_info_row(card, LV_SYMBOL_LIST, "Channel", "--", &detail_channel_label);
    add_info_row(card, LV_SYMBOL_WARNING, "Security", "--", &detail_auth_label);
    add_info_row(card, LV_SYMBOL_WIFI, "Quality", "--", &detail_quality_label);
    add_info_row(card, LV_SYMBOL_LIST, "Interference", "--", &detail_interference_label);

    card = ui_create_card(page_container, "Controls");
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Scan", "Scan Now", refresh_btn_cb);
    auto_switch = card_switch(card, LV_SYMBOL_REFRESH, "Auto Scan", true, auto_switch_cb);
    interval_dd = card_dropdown(card, LV_SYMBOL_SETTINGS, "Interval", "5s\n10s\n30s", 1, interval_cb);
    ui_create_card_button(card, LV_SYMBOL_SAVE, "Export", "CSV", export_csv_cb);

    ap_list_card = ui_create_card(page_container, "Access Points");
    lv_obj_t *empty = lv_label_create(ap_list_card);
    ap_rows[0] = empty;
    lv_label_set_text(empty, "Scanning...");
    lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);

    update_connection_labels();
    trigger_scan();
    auto_scan_timer = lv_timer_create(auto_scan_cb, WIFI_AUTO_SCAN_DEFAULT_MS, NULL);
}

void ui_wifi_analyzer_exit(lv_obj_t *parent)
{
}

app_t ui_wifi_analyzer_main = {
    .setup_func_cb = ui_wifi_analyzer_enter,
    .exit_func_cb = ui_wifi_analyzer_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_WIFI_ANALYZER */
