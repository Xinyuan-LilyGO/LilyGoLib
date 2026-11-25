/**
 * @file      ui_ble_c5_scan.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * ESP32-C5 ESP-AT BLE scanner with live signal/name filters and device details.
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#if defined(ARDUINO_T_EMBED_PLUS_V2)
#include "ui_c5_scan_transport.h"
#include "ui_scan_widgets.h"
#include "ui_ble_scan_filter.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <vector>
namespace {
enum class NameKind { InvalidText = -4, InvalidAD, InvalidHex, Invalid, None, Shortened, Complete };
struct c5_ble_device_t {
    char address[24] = {};
    char name[64] = {};
    char adv[256] = {};
    int rssi = -127;
    int address_type = -1;
    bool connectable = false;
};

static C5ScanTransport transport;
static lv_obj_t *page, *list;
static lv_obj_t *settings_button, *settings_box, *search_input, *search_card, *search_keyboard, *rssi_value;
static ui_soft_keyboard_lift_t search_lift = {nullptr, nullptr, -1};
static bool keyboard_transition = false, settings_closing = false;
static UIBleScanFilter filter;
static size_t visible_count = 0;
static UIScanLoading loading;
static UIScanRow rows[64];
static UIScanRound scan_round;
static bool first_complete = false, rendered_light_theme = false;
static lv_timer_t *timer;
static bool dirty = false;
static uint32_t rendered = 0, rendered_track = 0;
static std::vector<c5_ble_device_t> devices;

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static bool valid_name(const unsigned char *text, size_t length)
{
    for (size_t i = 0; i < length; ) {
        const unsigned char lead = text[i++];
        unsigned codepoint, continuation, minimum;
        if (lead < 0x80) { codepoint = lead; continuation = 0; minimum = 0; }
        else if (lead >= 0xC2 && lead <= 0xDF) { codepoint = lead & 0x1F; continuation = 1; minimum = 0x80; }
        else if (lead >= 0xE0 && lead <= 0xEF) { codepoint = lead & 0x0F; continuation = 2; minimum = 0x800; }
        else if (lead >= 0xF0 && lead <= 0xF4) { codepoint = lead & 7; continuation = 3; minimum = 0x10000; }
        else return false;
        if (continuation > length - i) return false;
        while (continuation--) {
            const unsigned char next = text[i++];
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint < 0x20 ||
            (codepoint >= 0x7F && codepoint <= 0x9F)) return false;
    }
    return true;
}

static NameKind decode_name(const char *hex, char *name, size_t name_size)
{
    if (!hex || !name || !name_size) return NameKind::Invalid;
    name[0] = '\0';
    const size_t hex_length = strlen(hex);
    unsigned char bytes[255];
    if ((hex_length & 1) || hex_length / 2 > sizeof(bytes)) {
        return NameKind::InvalidHex;
    }
    const size_t count = hex_length / 2;
    for (size_t i = 0; i < count; ++i) {
        const int high = hex_value(hex[i * 2]), low = hex_value(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return NameKind::InvalidHex;
        }
        bytes[i] = (high << 4) | low;
    }
    NameKind kind = NameKind::None;
    const unsigned char *selected = nullptr;
    size_t selected_length = 0;
    for (size_t i = 0; i < count; ) {
        const size_t length = bytes[i++];
        if (!length) break;
        if (length > count - i) {
            return NameKind::InvalidAD;
        }
        const unsigned type = bytes[i];
        if ((type == 0x08 || type == 0x09) && length > 1) {
            if (!valid_name(bytes + i + 1, length - 1)) {
                if (kind == NameKind::None) kind = NameKind::InvalidText;
            } else {
                const NameKind candidate = type == 0x09 ? NameKind::Complete : NameKind::Shortened;
                if (candidate > kind) { kind = candidate; selected = bytes + i + 1; selected_length = length - 1; }
            }
        }
        i += length;
    }
    if (selected) {
        size_t output = LV_MIN(selected_length, name_size - 1);
        // Validation covers the entire field, including bytes beyond the display limit.
        while (output && output < selected_length && (selected[output] & 0xC0) == 0x80) --output;
        memcpy(name, selected, output); name[output] = '\0';
    }
    return kind;
}

static bool next_field(const char *&cursor, char *out, size_t size)
{
    if (!cursor || !out || !size) return false;
    while (*cursor == ' ') ++cursor;
    const bool quoted = *cursor == '"';
    if (quoted) ++cursor;
    size_t length = 0;
    while (*cursor && (quoted ? *cursor != '"' : *cursor != ',')) {
        char ch = *cursor++;
        if (quoted && ch == '\\') { if (!*cursor) return false; ch = *cursor++; }
        else if (!quoted && (ch == '"' || ch == '\\')) return false;
        if (length + 1 >= size) return false;
        out[length++] = ch;
    }
    if (quoted) { if (*cursor != '"') return false; ++cursor; }
    else while (length && out[length - 1] == ' ') --length;
    while (*cursor == ' ') ++cursor;
    if (*cursor && *cursor != ',') return false;
    cursor = *cursor ? cursor + 1 : nullptr;
    out[length] = '\0';
    return true;
}

static bool number(const char *field, int minimum, int maximum, int &value)
{
    char *end = nullptr;
    errno = 0;
    const long parsed = strtol(field, &end, 10);
    if (errno || end == field || *end || parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

static void parse_scan(const char *line)
{
    if (!line || strncmp(line, "+BLESCAN:", 9)) return;
    const char *cursor = line + 9;
    char field[16] = {};
    c5_ble_device_t dev;
    int report_type = -1;
    char response[256] = {}, response_name[64] = {};
    if (!next_field(cursor, dev.address, sizeof(dev.address)) || !ui_scan_normalize_mac(dev.address)) {
        return;
    }
    if (!next_field(cursor, field, sizeof(field)) || !number(field, -127, 127, dev.rssi)) {
        return;
    }
    if (!next_field(cursor, dev.adv, sizeof(dev.adv)) || !next_field(cursor, response, sizeof(response)) ||
        (cursor && (!next_field(cursor, field, sizeof(field)) || !number(field, 0, 3, dev.address_type))) ||
        // Some C5 firmware appends numeric report metadata after addr_type.
        // Accept both forms; this metadata must not filter scan results.
        (cursor && (!next_field(cursor, field, sizeof(field)) || !number(field, INT_MIN, INT_MAX, report_type))) || cursor) {
        return;
    }
    const auto adv_kind = decode_name(dev.adv, dev.name, sizeof(dev.name));
    const auto response_kind = decode_name(response, response_name, sizeof(response_name));
    if (response_name[0] && response_kind >= adv_kind) {
        snprintf(dev.name, sizeof(dev.name), "%s", response_name);
    }
    auto it = std::find_if(devices.begin(), devices.end(), [&dev](const c5_ble_device_t &item) {
        return dev.address[0] && strcmp(item.address, dev.address) == 0;
    });
    if (it == devices.end() && devices.size() >= 64) {
        const size_t slot = scan_round.first_unseen(devices.size());
        if (slot != devices.size()) it = devices.begin() + slot;
        else if (filter.matches(dev.rssi, dev.name))
            // Dense advertising must not let rejected devices fill every slot.
            it = std::find_if(devices.begin(), devices.end(), [](const c5_ble_device_t &item) {
                return !filter.matches(item.rssi, item.name);
            });
        if (it == devices.end()) return;
    }
    const size_t index = it == devices.end() ? devices.size() : size_t(it - devices.begin());
    if (it == devices.end() && devices.size() < 64) devices.push_back(dev);
    else {
        if (!dev.name[0] && !strcmp(it->address, dev.address)) {
            snprintf(dev.name, sizeof(dev.name), "%s", it->name);
        }
        *it = dev;
    }
    scan_round.mark(index);
    dirty = true;
}

static void focus(lv_event_t *event) { lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(event), LV_ANIM_ON); }
static void clicked(lv_event_t *event) {
    const size_t index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= devices.size()) return;
    const auto &dev = devices[index]; char text[360];
    snprintf(text, sizeof(text), "%s\n%d dBm\n%s", dev.address, dev.rssi, dev.adv);
    ui_msg_pop_up(dev.name[0] ? dev.name : "BLE device", text);
}
static void render() {
    visible_count = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto &dev = devices[i];
        if (!filter.matches(dev.rssi, dev.name)) {
            if (rows[i].button) lv_obj_delete(rows[i].button);
            rows[i] = {};
            continue;
        }
        ++visible_count;
        if (!rows[i].button) {
            rows[i] = ui_scan_row_create(list, LV_SYMBOL_BLUETOOTH, false);
            lv_obj_add_event_cb(rows[i].button, clicked, LV_EVENT_CLICKED, (void *)i);
            lv_obj_add_event_cb(rows[i].button, focus, LV_EVENT_FOCUSED, nullptr);
        }
        ui_scan_set_text(rows[i].name, dev.name[0] ? dev.name : dev.address);
        ui_scan_row_signal(rows[i], dev.rssi);
    }
    dirty = false; rendered = millis(); rendered_light_theme = ui_active_light_theme; rendered_track = ui_active_track;
}
static void line(const char *text) {
    if (!strncmp(text, "+BLESCAN:", 9)) parse_scan(text);
}
static void round_started() {
    scan_round.begin();
}
static void poll(lv_timer_t *) {
    transport.tick(line, round_started);
    if (transport.finished && scan_round.pending) {
        if (scan_round.finish(devices, rows, clicked)) dirty = true;
    }
    if (transport.finished || transport.failed) first_complete = true;
    if ((dirty && (millis() - rendered >= 200 || transport.finished)) || rendered_light_theme != ui_active_light_theme || rendered_track != ui_active_track) render();
    const bool show = first_complete && visible_count != 0;
    ui_scan_show_results(loading, list, show);
    if (!show && first_complete && !devices.empty() && !transport.failed)
        ui_scan_loading_update(loading, "No devices match filters", false);
    else if (!show) ui_scan_loading_update(loading,
        transport.failed ? "C5 unavailable. Retrying in 5 s..." :
        transport.finished ? "No BLE devices found. Retrying in 5 s..." :
        transport.scanning() ? "Scanning..." : "Starting C5...", !transport.failed && !transport.finished);
}

static void hide_search_keyboard()
{
    if (keyboard_transition) return;
    keyboard_transition = true;
    if (search_keyboard) ui_soft_keyboard_hide(search_keyboard, &search_lift);
    if (search_card) lv_obj_remove_local_style_prop(search_card, LV_STYLE_TEXT_FONT, 0);
    if (search_input) {
        lv_obj_remove_state(search_input, LV_STATE_EDITED);
        if (auto *group = lv_obj_get_group(search_input)) lv_group_set_editing(group, false);
    }
    disable_keyboard();
    keyboard_transition = false;
}

static void keyboard_event(lv_event_t *)
{
    hide_search_keyboard();
    if (search_input) lv_group_focus_obj(search_input);
}

static void search_event(lv_event_t *event)
{
    if (settings_closing || keyboard_transition) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        snprintf(filter.name, sizeof(filter.name), "%s", lv_textarea_get_text(search_input));
        dirty = true;
    } else if (code == LV_EVENT_CLICKED) {
        enable_keyboard();
        if (search_keyboard) {
            // Keep the inherited modal face while raised onto the top layer;
            // normal inheritance resumes when the card returns to the dialog.
            if (!search_lift.lifted_obj)
                lv_obj_set_style_text_font(search_card, lv_obj_get_style_text_font(search_card, LV_PART_MAIN), 0);
            ui_soft_keyboard_show(search_keyboard, search_input, search_card, &search_lift);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
               (code == LV_EVENT_DEFOCUSED && !ui_soft_keyboard_focus_transition_active())) {
        hide_search_keyboard();
    }
}

static void settings_deleted(lv_event_t *)
{
    settings_closing = true;
    hide_search_keyboard();
    if (search_keyboard) lv_obj_delete(search_keyboard);
    settings_box = search_input = search_card = search_keyboard = rssi_value = nullptr;
    search_lift = {nullptr, nullptr, -1};
    settings_closing = false;
}

static void rssi_changed(lv_event_t *event)
{
    filter.min_rssi = lv_slider_get_value(lv_event_get_target_obj(event));
    lv_label_set_text_fmt(rssi_value, "%d dBm", filter.min_rssi);
    dirty = true;
}

static void name_only_changed(lv_event_t *event)
{
    filter.with_name = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    dirty = true;
}

static lv_obj_t *settings_line(lv_obj_t *parent)
{
    auto *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0); lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    return row;
}

static lv_obj_t *settings_label(lv_obj_t *parent, const char *text, bool grow = false)
{
    auto *label = lv_label_create(parent); lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    if (grow) {
        lv_obj_set_width(label, 1); lv_obj_set_flex_grow(label, 1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    return label;
}

static void open_settings(lv_event_t *)
{
    if (settings_box) return;
    settings_box = create_msgbox(page, "BLE Filters", "", nullptr, nullptr, nullptr);
    auto *body = lv_msgbox_get_content(settings_box);
    // This dialog contains form controls; its layout must not be an extra
    // encoder stop like the scrollable body of a text-only information dialog.
    if (lv_obj_get_group(body)) lv_group_remove_obj(body);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(lv_obj_get_child(body, 0), LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_add_event_cb(settings_box, settings_deleted, LV_EVENT_DELETE, nullptr);
    auto *card = ui_create_card(body, nullptr);
    lv_obj_remove_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    lv_obj_set_style_pad_all(card, 8, 0); lv_obj_set_style_pad_row(card, 8, 0);
    auto *row = settings_line(card);
    settings_label(row, "Min RSSI", true);
    rssi_value = settings_label(row, ""); lv_label_set_text_fmt(rssi_value, "%d dBm", filter.min_rssi);
    auto *slider = lv_slider_create(card); lv_obj_set_width(slider, LV_PCT(100)); lv_obj_set_height(slider, 12);
    lv_slider_set_range(slider, -127, 0); lv_slider_set_value(slider, filter.min_rssi, LV_ANIM_OFF);
    lv_obj_set_style_margin_ver(slider, 6, 0);
    ui_prepare_slider_for_encoder(slider); ui_add_accent_focus_style(slider);
    lv_obj_add_event_cb(slider, rssi_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    row = settings_line(card); settings_label(row, "With name", true);
    auto *name_switch = lv_switch_create(row); lv_obj_set_size(name_switch, 44, 24);
    if (filter.with_name) lv_obj_add_state(name_switch, LV_STATE_CHECKED);
    ui_add_accent_focus_style(name_switch);
    lv_obj_add_event_cb(name_switch, focus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(name_switch, name_only_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    search_card = ui_create_card(body, nullptr);
    lv_obj_remove_flag(search_card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    lv_obj_set_style_pad_all(search_card, 8, 0); lv_obj_set_style_pad_row(search_card, 6, 0);
    auto *font = lv_obj_get_style_text_font(body, LV_PART_MAIN);
    auto *caption = settings_label(search_card, "Name contains"); lv_obj_set_width(caption, LV_PCT(100));
    search_input = lv_textarea_create(search_card);
    lv_obj_set_size(search_input, LV_PCT(100), LV_MAX(34, lv_font_get_line_height(font) + 16));
    lv_textarea_set_one_line(search_input, true); lv_textarea_set_max_length(search_input, sizeof(filter.name) - 1);
    lv_textarea_set_placeholder_text(search_input, "Search name"); lv_textarea_set_text(search_input, filter.name);
    ui_prepare_textarea_for_encoder(search_input); ui_add_accent_focus_style(search_input);
    lv_obj_add_event_cb(search_input, search_event, LV_EVENT_ALL, nullptr);
    if (ui_soft_keyboard_should_open()) {
        search_keyboard = lv_keyboard_create(lv_layer_top()); lv_obj_add_flag(search_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (lv_obj_get_group(search_keyboard)) lv_group_remove_obj(search_keyboard);
        lv_obj_add_event_cb(search_keyboard, keyboard_event, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(search_keyboard, keyboard_event, LV_EVENT_CANCEL, nullptr);
    }
    lv_group_focus_obj(slider);
}

static void cleanup() {
    if (settings_box) destroy_msgbox(settings_box);
    if (timer) { lv_timer_delete(timer); timer = nullptr; }
    transport.close();
}
static void back(lv_event_t *) {
    cleanup(); if (page) ui_destroy_app_page(page);
    page = list = nullptr; loading = {}; for (auto &row : rows) row = {};
    settings_button = nullptr;
    devices.clear(); menu_show();
}
} // namespace
void ui_ble_c5_scan_enter(lv_obj_t *parent) {
    page = ui_create_app_page(parent, "C5 BLE", back);
    settings_button = ui_app_page_add_header_button(page, LV_SYMBOL_SETTINGS, open_settings);
    lv_obj_add_event_cb(page, [](lv_event_t *) { cleanup(); }, LV_EVENT_DELETE, nullptr);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    loading = ui_scan_loading_create(page, "Starting C5...");
    list = lv_list_create(page); lv_obj_set_size(list, LV_PCT(100), 1); lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_row(list, 4, 0);
    ui_scan_show_results(loading, list, false);
    devices.clear(); for (auto &row : rows) row = {}; dirty = false; first_complete = false; visible_count = 0;
    scan_round = {};
    rendered_light_theme = ui_active_light_theme; rendered_track = ui_active_track;
    transport.begin(true); timer = lv_timer_create(poll, 25, nullptr);
}
void ui_ble_c5_scan_exit(lv_obj_t *) { cleanup(); }
app_t ui_ble_c5_scan_main = { .setup_func_cb = ui_ble_c5_scan_enter, .exit_func_cb = ui_ble_c5_scan_exit, .user_data = nullptr };
#endif
