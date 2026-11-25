/**
 * @file      ui_scan_widgets.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-25
 *
 * Shared centered scan loading and compact, themed signal-result rows.
 */
#pragma once
#include "ui_define.h"
LV_FONT_DECLARE(font_alibaba_12);

struct UIScanLoading {
    lv_obj_t *panel = nullptr, *spinner = nullptr, *label = nullptr;
};

static inline void ui_scan_set_text(lv_obj_t *label, const char *text)
{
    // Preserve an ongoing marquee when only signal strength changes.
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static inline UIScanLoading ui_scan_loading_create(lv_obj_t *parent, const char *text)
{
    UIScanLoading view;
    view.panel = lv_obj_create(parent);
    lv_obj_set_size(view.panel, LV_PCT(100), 1); lv_obj_set_flex_grow(view.panel, 1);
    lv_obj_set_style_bg_opa(view.panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view.panel, 0, 0); lv_obj_set_style_pad_all(view.panel, 0, 0);
    lv_obj_set_style_pad_row(view.panel, 8, 0);
    lv_obj_set_flex_flow(view.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view.panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(view.panel, LV_OBJ_FLAG_SCROLLABLE);
    const int width = lv_display_get_horizontal_resolution(nullptr) - 16;
    const int height = lv_display_get_vertical_resolution(nullptr) - 49;
    const int diameter = LV_CLAMP(44, LV_MIN(width, height) * 45 / 100, 88);
    view.spinner = lv_spinner_create(view.panel); lv_obj_set_size(view.spinner, diameter, diameter);
    lv_obj_set_style_arc_color(view.spinner, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(view.spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    view.label = lv_label_create(view.panel); lv_obj_set_width(view.label, LV_PCT(100));
    lv_obj_set_style_text_font(view.label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(view.label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view.label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_label_set_long_mode(view.label, LV_LABEL_LONG_WRAP); lv_label_set_text(view.label, text);
    return view;
}

static inline void ui_scan_loading_update(const UIScanLoading &view, const char *text, bool spinning)
{
    ui_scan_set_text(view.label, text);
    if (spinning) lv_obj_remove_flag(view.spinner, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(view.spinner, LV_OBJ_FLAG_HIDDEN);
}

static inline void ui_scan_show_results(const UIScanLoading &view, lv_obj_t *list, bool show)
{
    if (show) {
        lv_obj_add_flag(view.panel, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(list, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(view.panel, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    }
}

struct UIScanRow {
    lv_obj_t *button = nullptr, *name = nullptr, *meta = nullptr, *rssi = nullptr;
    lv_obj_t *bars[4] = {};
};

// Reject missing/malformed identifiers rather than accumulating unkeyed rows.
// Canonical casing makes repeated advertisements update the same result.
static inline bool ui_scan_normalize_mac(char *mac)
{
    if (strlen(mac) != 17) return false;
    for (unsigned i = 0; i < 17; ++i) {
        if (i % 3 == 2) { if (mac[i] != ':') return false; continue; }
        char ch = mac[i];
        if (ch >= 'A' && ch <= 'F') ch += 'a' - 'A';
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
        mac[i] = ch;
    }
    return true;
}

struct UIScanRound {
    bool seen[64] = {}, pending = false;

    void begin() { memset(seen, 0, sizeof(seen)); pending = true; }
    void mark(size_t index) { if (index < 64) seen[index] = true; }
    size_t first_unseen(size_t count) const {
        for (size_t i = 0; i < count; ++i) if (!seen[i]) return i;
        return count;
    }

    template<typename Result>
    bool finish(std::vector<Result> &results, UIScanRow (&rows)[64], lv_event_cb_t clicked)
    {
        if (!pending) return false;
        pending = false;
        const size_t before = results.size();
        size_t output = 0;
        for (size_t input = 0; input < before; ++input) {
            if (!seen[input]) {
                if (rows[input].button) lv_obj_delete(rows[input].button);
                continue;
            }
            if (output != input) {
                results[output] = results[input]; rows[output] = rows[input];
                if (rows[output].button) {
                    lv_obj_remove_event_cb(rows[output].button, clicked);
                    lv_obj_add_event_cb(rows[output].button, clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)output);
                }
            }
            ++output;
        }
        results.resize(output);
        for (size_t i = output; i < 64; ++i) rows[i] = {};
        return before != output;
    }
};

static inline UIScanRow ui_scan_row_create(lv_obj_t *list, const char *symbol, bool metadata)
{
    UIScanRow row;
    row.button = lv_list_add_button(list, nullptr, ""); lv_obj_clean(row.button);
    lv_obj_set_height(row.button, 30);
    lv_obj_set_style_pad_hor(row.button, 6, 0); lv_obj_set_style_pad_ver(row.button, 3, 0);
    lv_obj_set_style_pad_column(row.button, 4, 0);
    lv_obj_set_flex_flow(row.button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row.button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row.button, LV_OBJ_FLAG_SCROLLABLE);
    auto *icon = lv_label_create(row.button); lv_label_set_text(icon, symbol);
    lv_obj_set_size(icon, 14, 14); lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
    row.name = lv_label_create(row.button); lv_obj_set_size(row.name, 1, 13); lv_obj_set_flex_grow(row.name, 1);
    lv_obj_set_style_text_font(row.name, &font_alibaba_12, 0);
    lv_label_set_long_mode(row.name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    if (metadata) {
        row.meta = lv_label_create(row.button); lv_obj_set_size(row.meta, 48, 12);
        lv_obj_set_style_text_font(row.meta, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(row.meta, UI_COLOR_TEXT_SECONDARY, 0);
    }
    auto *signal = lv_obj_create(row.button); lv_obj_set_size(signal, 16, 12);
    lv_obj_set_style_pad_all(signal, 0, 0); lv_obj_set_style_border_width(signal, 0, 0);
    lv_obj_set_style_bg_opa(signal, LV_OPA_TRANSP, 0); lv_obj_remove_flag(signal, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; ++i) {
        row.bars[i] = lv_obj_create(signal); lv_obj_set_size(row.bars[i], 3, (i + 1) * 3);
        lv_obj_align(row.bars[i], LV_ALIGN_BOTTOM_LEFT, i * 4, 0);
        lv_obj_set_style_pad_all(row.bars[i], 0, 0); lv_obj_set_style_bg_opa(row.bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row.bars[i], 0, 0); lv_obj_set_style_radius(row.bars[i], 1, 0);
    }
    row.rssi = lv_label_create(row.button); lv_obj_set_size(row.rssi, 44, 12);
    lv_obj_set_style_text_font(row.rssi, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(row.rssi, LV_TEXT_ALIGN_RIGHT, 0);
    return row;
}

static inline void ui_scan_row_signal(const UIScanRow &row, int rssi)
{
    const lv_color_t color = rssi >= -60 ? UI_COLOR_SIGNAL_GOOD : rssi >= -75 ? UI_COLOR_SIGNAL_FAIR : UI_COLOR_SIGNAL_WEAK;
    const int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    char text[16]; snprintf(text, sizeof(text), "%ddBm", rssi); ui_scan_set_text(row.rssi, text);
    lv_obj_set_style_text_color(row.rssi, color, 0);
    for (int i = 0; i < 4; ++i) lv_obj_set_style_bg_color(row.bars[i], i < bars ? color : UI_COLOR_TRACK, 0);
}
