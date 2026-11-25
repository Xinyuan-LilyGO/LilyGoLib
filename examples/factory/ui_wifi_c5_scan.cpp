/**
 * @file      ui_wifi_c5_scan.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * ESP32-C5 ESP-AT Wi-Fi scanner with structured results and band details.
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#if defined(ARDUINO_T_EMBED_PLUS_V2)
#include "ui_c5_scan_transport.h"
#include "ui_scan_widgets.h"
#include <algorithm>
#include <vector>
namespace {
struct c5_ap_t {
    char ssid[96] = {};
    char bssid[20] = {};
    int rssi = -127;
    int channel = 0;
    int auth = -1;
};

static C5ScanTransport transport;
static lv_obj_t *page, *list;
static UIScanLoading loading;
static UIScanRow rows[64];
static UIScanRound scan_round;
static bool first_complete = false, rendered_light_theme = false;
static lv_timer_t *timer;
static bool dirty = false;
static uint32_t rendered = 0, rendered_track = 0;
static std::vector<c5_ap_t> aps;
static const char *band(int channel)
{
    if (channel >= 36) return "5 GHz";
    if (channel >= 1 && channel <= 14) return "2.4 GHz";
    return "Unknown band";
}

static const char *security(int auth)
{
    switch (auth) {
    case 0: return "Open";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/WPA2";
    case 5: return "WPA2 Enterprise";
    case 6: return "WPA3";
    case 7: return "WPA2/WPA3";
    default: return "Unknown";
    }
}

static bool next_field(const char *&cursor, char *out, size_t size)
{
    if (!cursor || !out || !size) return false;
    while (*cursor == ' ') ++cursor;
    if (!*cursor) return false;
    bool quoted = false;
    size_t length = 0;
    while (*cursor) {
        char ch = *cursor++;
        if (ch == '\\' && *cursor) {
            if (length + 1 < size) out[length++] = *cursor;
            ++cursor;
            continue;
        }
        if (ch == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && ch == ',') break;
        if (length + 1 < size) out[length++] = ch;
    }
    out[length] = '\0';
    return true;
}

static void parse_scan(const char *line)
{
    const char *cursor = line + 7;
    while (*cursor == ' ' || *cursor == '(') ++cursor;
    char field[128] = {};
    c5_ap_t ap;
    if (!next_field(cursor, field, sizeof(field))) return;
    ap.auth = atoi(field);
    if (!next_field(cursor, field, sizeof(field))) return;
    snprintf(ap.ssid, sizeof(ap.ssid), "%s", field);
    if (!next_field(cursor, field, sizeof(field))) return;
    ap.rssi = atoi(field);
    if (!next_field(cursor, field, sizeof(field))) return;
    snprintf(ap.bssid, sizeof(ap.bssid), "%s", field);
    if (!ui_scan_normalize_mac(ap.bssid)) return;
    if (!next_field(cursor, field, sizeof(field))) return;
    ap.channel = atoi(field);
    auto it = std::find_if(aps.begin(), aps.end(), [&ap](const c5_ap_t &item) {
        return ap.bssid[0] && strcmp(item.bssid, ap.bssid) == 0;
    });
    if (it == aps.end() && aps.size() >= 64) {
        const size_t slot = scan_round.first_unseen(aps.size());
        if (slot == aps.size()) return;
        it = aps.begin() + slot;
    }
    const size_t index = it == aps.end() ? aps.size() : size_t(it - aps.begin());
    if (it == aps.end() && aps.size() < 64) aps.push_back(ap);
    else *it = ap;
    scan_round.mark(index);
    dirty = true;
}

static void focus(lv_event_t *event) { lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(event), LV_ANIM_ON); }
static void clicked(lv_event_t *event) {
    const size_t index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= aps.size()) return;
    const auto &ap = aps[index]; char text[240];
    snprintf(text, sizeof(text), "%s\n%s  Ch %d  %d dBm\n%s", ap.bssid, band(ap.channel), ap.channel, ap.rssi, security(ap.auth));
    ui_msg_pop_up(ap.ssid[0] ? ap.ssid : "Hidden network", text);
}
static void render() {
    for (size_t i = 0; i < aps.size(); ++i) {
        if (!rows[i].button) {
            rows[i] = ui_scan_row_create(list, LV_SYMBOL_WIFI, true);
            lv_obj_add_event_cb(rows[i].button, clicked, LV_EVENT_CLICKED, (void *)i);
            lv_obj_add_event_cb(rows[i].button, focus, LV_EVENT_FOCUSED, nullptr);
        }
        const auto &ap = aps[i];
        ui_scan_set_text(rows[i].name, ap.ssid[0] ? ap.ssid : "Hidden network");
        char meta[24]; snprintf(meta, sizeof(meta), "%s/%d", ap.channel >= 36 ? "5G" : ap.channel >= 1 ? "2.4G" : "?", ap.channel);
        ui_scan_set_text(rows[i].meta, meta);
        ui_scan_row_signal(rows[i], ap.rssi);
    }
    dirty = false; rendered = millis(); rendered_light_theme = ui_active_light_theme; rendered_track = ui_active_track;
}
static void line(const char *text) {
    if (!strncmp(text, "+CWLAP:", 7)) parse_scan(text);
}
static void round_started() { scan_round.begin(); }
static void poll(lv_timer_t *) {
    transport.tick(line, round_started);
    if (transport.finished && scan_round.finish(aps, rows, clicked)) dirty = true;
    if (transport.finished || transport.failed) first_complete = true;
    if ((dirty && (millis() - rendered >= 200 || transport.finished)) || rendered_light_theme != ui_active_light_theme || rendered_track != ui_active_track) render();
    const bool show = first_complete && !aps.empty();
    ui_scan_show_results(loading, list, show);
    if (!show) ui_scan_loading_update(loading,
        transport.failed ? "C5 unavailable. Retrying in 5 s..." :
        transport.finished ? "No networks found. Retrying in 5 s..." :
        transport.scanning() ? "Scanning..." : "Starting C5...", !transport.failed && !transport.finished);
}
static void cleanup() {
    if (timer) { lv_timer_delete(timer); timer = nullptr; }
    transport.close();
}
static void back(lv_event_t *) {
    cleanup(); if (page) ui_destroy_app_page(page);
    page = list = nullptr; loading = {}; for (auto &row : rows) row = {};
    aps.clear(); menu_show();
}
} // namespace
void ui_wifi_c5_scan_enter(lv_obj_t *parent) {
    page = ui_create_app_page(parent, "C5 Wi-Fi", back);
    lv_obj_add_event_cb(page, [](lv_event_t *) { cleanup(); }, LV_EVENT_DELETE, nullptr);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    loading = ui_scan_loading_create(page, "Starting C5...");
    list = lv_list_create(page); lv_obj_set_size(list, LV_PCT(100), 1); lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_row(list, 4, 0);
    ui_scan_show_results(loading, list, false);
    aps.clear(); for (auto &row : rows) row = {}; dirty = false; first_complete = false;
    scan_round = {};
    rendered_light_theme = ui_active_light_theme; rendered_track = ui_active_track;
    transport.begin(false); timer = lv_timer_create(poll, 25, nullptr);
}
void ui_wifi_c5_scan_exit(lv_obj_t *) { cleanup(); }
app_t ui_wifi_c5_scan_main = { .setup_func_cb = ui_wifi_c5_scan_enter, .exit_func_cb = ui_wifi_c5_scan_exit, .user_data = nullptr };
#endif
