/**
 * @file      ui_keyboard.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"
#include <ctype.h>

#if !defined(EXCLUDE_KEYBOARD)

static lv_obj_t *page_container = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *key_label = NULL;
static lv_obj_t *detail_label = NULL;
static lv_obj_t *modifier_label = NULL;
static lv_obj_t *history_label = NULL;
static lv_obj_t *symbol_chip = NULL;
static lv_obj_t *alt_chip = NULL;
static lv_obj_t *caps_chip = NULL;
static lv_obj_t *backspace_chip = NULL;

static uint32_t pressed_count = 0;
static uint32_t released_count = 0;
static uint32_t combo_count = 0;
static bool symbol_active = false;
static bool alt_active = false;
static bool caps_active = false;
static bool backspace_active = false;
static char history_lines[7][56];

struct keyboard_monitor_layout_t {
    uint8_t rows;
    uint8_t cols;
    const char *normal;
    const char *symbol;
    uint8_t symbol_key;
    uint8_t alt_key;
    uint8_t caps_key;
    uint8_t caps_b_key;
    uint8_t backspace_key;
    bool space_as_symbol;
};

#if defined(ARDUINO_T_LORA_PAGER)
static constexpr char normal_map[] = {
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n',
    '\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\0',
    ' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'
};
static constexpr char symbol_map[] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0',
    '\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0',
    ' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'
};
static const keyboard_monitor_layout_t keyboard_layout = {
    4, 10, normal_map, symbol_map, 0x1E, 0x14, 0x1C, 0xFF, 0x1D, true
};
#elif defined(ARDUINO_T_DECK_V2)
static constexpr char normal_map[] = {
    'o', 'e', 'r', 'u', 'q',
    'w', 's', 'g', 'h', 'l',
    '\0', 'd', 't', 'y', 'i',
    'a', 'p', '\0', '\n', '\0',
    '\0', 'x', 'v', 'b', '$',
    ' ', 'z', 'c', 'n', 'm',
    '\0', '\0', 'f', 'j', 'k'
};
static constexpr char symbol_map[] = {
    '#', '2', '3', '_', '+',
    '1', '4', '/', ':', '"',
    '\0', '5', '(', ')', '-',
    '*', '@', '\0', '\0', '\0',
    '\0', '8', '?', '!', '\0',
    '\0', '7', '9', ',', '.',
    '0', '\0', '6', ';', '\''
};
static const keyboard_monitor_layout_t keyboard_layout = {
    7, 5, normal_map, symbol_map, 0x14, 0x28, 0x20, 0x3D, 0x22, false
};
#else
static constexpr char normal_map[] = "";
static constexpr char symbol_map[] = "";
static const keyboard_monitor_layout_t keyboard_layout = {
    0, 0, normal_map, symbol_map, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, false
};
#endif

static const char *state_name(bool pressed)
{
    return pressed ? "Pressed" : "Released";
}

static const char *state_short_name(bool pressed)
{
    return pressed ? "Down" : "Up";
}

static bool compact_keyboard_screen()
{
    return is_screen_small();
}

static uint8_t visible_history_lines()
{
    return compact_keyboard_screen() ? 2 : 5;
}

static lv_coord_t compact_card_height()
{
    lv_coord_t height = lv_disp_get_ver_res(NULL) - 33 - 16;
    return height < 132 ? 132 : height;
}

static lv_coord_t detail_width()
{
    return lv_disp_get_hor_res(NULL) >= 420 ? 190 : 150;
}

static const char *key_name(char c, char *buffer, size_t size)
{
    switch (c) {
    case '\0':
        return "Unmapped";
    case '\b':
        return "Backspace";
    case '\n':
        return "Enter";
    case '\r':
        return "Return";
    case '\t':
        return "Tab";
    case ' ':
        return "Space";
    default:
        break;
    }

    if (isprint((unsigned char)c)) {
        snprintf(buffer, size, "'%c'", c);
    } else {
        snprintf(buffer, size, "0x%02X", (unsigned int)(uint8_t)c);
    }
    return buffer;
}

static char mapped_key_char(uint8_t logical, bool use_symbol_layer, bool use_caps)
{
    if (keyboard_layout.cols == 0 || keyboard_layout.rows == 0) {
        return '\0';
    }

    uint8_t row = logical / 10;
    uint8_t col = logical % 10;
    if (row >= keyboard_layout.rows || col >= keyboard_layout.cols) {
        return '\0';
    }

    char c = keyboard_layout.normal[row * keyboard_layout.cols + col];
    if (use_symbol_layer) {
        char symbol = keyboard_layout.symbol[row * keyboard_layout.cols + col];
        if (symbol != '\0') {
            c = symbol;
        }
    }
    if (use_caps && c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }
    return c;
}

static const char *logical_key_name(uint8_t logical, char *buffer, size_t size)
{
#if defined(USING_TDECK_KEYBOARD)
    return key_name(static_cast<char>(logical), buffer, size);
#endif
    if (logical == keyboard_layout.symbol_key) {
        return keyboard_layout.space_as_symbol ? "Space" : "Symbol";
    }
    if (logical == keyboard_layout.alt_key) {
        return "Alt";
    }
    if (logical == keyboard_layout.caps_key || logical == keyboard_layout.caps_b_key) {
        return "Cap";
    }
    if (logical == keyboard_layout.backspace_key) {
        return "Backspace";
    }

    if (keyboard_layout.cols == 0 || keyboard_layout.rows == 0) {
        snprintf(buffer, size, "Raw 0x%02X", logical);
        return buffer;
    }

    uint8_t row = logical / 10;
    uint8_t col = logical % 10;
    if (row >= keyboard_layout.rows || col >= keyboard_layout.cols) {
        snprintf(buffer, size, "Raw 0x%02X", logical);
        return buffer;
    }

    char c = mapped_key_char(logical, symbol_active, caps_active);
    return key_name(c, buffer, size);
}

static void style_chip(lv_obj_t *chip, bool active)
{
    if (!chip) {
        return;
    }
    lv_obj_set_style_bg_color(chip, active ? UI_COLOR_ACCENT : UI_COLOR_TRACK, 0);
    lv_obj_set_style_bg_opa(chip, active ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_border_color(chip, active ? UI_COLOR_ACCENT : UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_text_color(chip, active ? lv_color_black() : UI_COLOR_TEXT_SECONDARY, 0);
}

static lv_obj_t *create_chip(lv_obj_t *parent, const char *text)
{
    lv_obj_t *chip = lv_label_create(parent);
    bool compact = compact_keyboard_screen();
    lv_label_set_text(chip, text);
    lv_obj_set_style_radius(chip, 5, 0);
    lv_obj_set_style_pad_hor(chip, compact ? 6 : 8, 0);
    lv_obj_set_style_pad_ver(chip, compact ? 3 : 5, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_text_font(chip, &lv_font_montserrat_12, 0);
    style_chip(chip, false);
    return chip;
}

static void refresh_modifier_view()
{
    style_chip(symbol_chip, symbol_active);
    style_chip(alt_chip, alt_active);
    style_chip(caps_chip, caps_active);
    style_chip(backspace_chip, backspace_active);

    if (modifier_label) {
        lv_label_set_text_fmt(modifier_label,
                              "Symbol: %s    Alt: %s    Cap: %s    Backspace: %s",
                              symbol_active ? "ON" : "OFF",
                              alt_active ? "ON" : "OFF",
                              caps_active ? "ON" : "OFF",
                              backspace_active ? "ON" : "OFF");
    }
}

static void refresh_history_label()
{
    if (!history_label) {
        return;
    }

    char text[sizeof(history_lines) + 32];
    size_t offset = 0;

    text[0] = '\0';
    for (size_t i = 0; i < visible_history_lines(); i++) {
        if (history_lines[i][0] == '\0') {
            continue;
        }
        int written = snprintf(text + offset, sizeof(text) - offset, "%s\n", history_lines[i]);
        if (written < 0 || (size_t)written >= sizeof(text) - offset) {
            break;
        }
        offset += (size_t)written;
    }
    lv_label_set_text(history_label, text[0] ? text : "No key events yet.");
}

static void push_history(bool pressed, uint8_t raw, uint8_t logical, const char *name)
{
    for (size_t i = sizeof(history_lines) / sizeof(history_lines[0]) - 1; i > 0; i--) {
        snprintf(history_lines[i], sizeof(history_lines[i]), "%s", history_lines[i - 1]);
    }

    if (compact_keyboard_screen()) {
        snprintf(history_lines[0], sizeof(history_lines[0]), "%lu %-4s %-10s r%02X k%02X",
                 (unsigned long)millis(), state_short_name(pressed), name, raw, logical);
    } else {
        snprintf(history_lines[0], sizeof(history_lines[0]), "%lu ms  %-8s %-14s raw 0x%02X key 0x%02X",
                 (unsigned long)millis(), state_name(pressed), name, raw, logical);
    }
    refresh_history_label();
}

static void push_combo_history(uint8_t raw, uint8_t logical, bool backlight_on)
{
    for (size_t i = sizeof(history_lines) / sizeof(history_lines[0]) - 1; i > 0; i--) {
        snprintf(history_lines[i], sizeof(history_lines[i]), "%s", history_lines[i - 1]);
    }

    if (compact_keyboard_screen()) {
        snprintf(history_lines[0], sizeof(history_lines[0]), "%lu Combo FN/ALT+B BL %s r%02X k%02X",
                 (unsigned long)millis(), backlight_on ? "ON" : "OFF", raw, logical);
    } else {
        snprintf(history_lines[0], sizeof(history_lines[0]), "%lu ms  Combo    FN/ALT+B      BL %s raw 0x%02X key 0x%02X",
                 (unsigned long)millis(), backlight_on ? "ON" : "OFF", raw, logical);
    }
    refresh_history_label();
}

static void update_key_view(bool pressed, uint8_t raw, uint8_t logical)
{
    char name_buffer[20];
    const char *name = logical_key_name(logical, name_buffer, sizeof(name_buffer));

    if (pressed) {
        pressed_count++;
    } else {
        released_count++;
    }

    if (key_label) {
        lv_label_set_text(key_label, name);
    }

    if (detail_label) {
        uint8_t row = keyboard_layout.cols ? logical / 10 : 0;
        uint8_t col = keyboard_layout.cols ? logical % 10 : 0;
        lv_label_set_text_fmt(detail_label,
                              "%s r%02X k%02X\n"
                              "R%u C%u P:%lu U:%lu",
                              state_short_name(pressed),
                              raw,
                              logical,
                              (unsigned int)row,
                              (unsigned int)col,
                              (unsigned long)pressed_count,
                              (unsigned long)released_count);
    }

    push_history(pressed, raw, logical, name);
}

static bool is_backlight_combo_key(uint8_t logical)
{
    char c = mapped_key_char(logical, false, false);
    return c == 'b' || c == 'B';
}

static void show_backlight_combo(uint8_t raw, uint8_t logical)
{
    uint8_t current = hw_get_kb_backlight();
    bool turn_on = current == 0;
    hw_kb_enable_backlight(turn_on);
    combo_count++;

    if (key_label) {
        lv_label_set_text(key_label, "FN/ALT+B");
    }
    if (detail_label) {
        lv_label_set_text_fmt(detail_label,
                              "Combo BL %s\n"
                              "r%02X k%02X C:%lu",
                              turn_on ? "ON" : "OFF",
                              raw,
                              logical,
                              (unsigned long)combo_count);
    }
    if (status_label) {
        lv_label_set_text_fmt(status_label, "FN/ALT+B toggled keyboard BL %s.", turn_on ? "ON" : "OFF");
    }
    push_combo_history(raw, logical, turn_on);
}

static void keyboard_raw_cb(bool pressed, uint8_t raw)
{
    if (!page_container) {
        return;
    }

    if (raw == 0
#if !defined(USING_TDECK_KEYBOARD)
        || raw > 0x60
#endif
    ) {
        return;
    }

#if defined(USING_TDECK_KEYBOARD)
    uint8_t logical = raw;
#else
    uint8_t logical = raw - 1;
#endif
    if (logical == keyboard_layout.symbol_key) {
        symbol_active = pressed;
    } else if (logical == keyboard_layout.alt_key) {
        alt_active = pressed;
    } else if (logical == keyboard_layout.caps_key || logical == keyboard_layout.caps_b_key) {
        caps_active = pressed;
    } else if (logical == keyboard_layout.backspace_key) {
        backspace_active = pressed;
    }

    if (pressed && alt_active && is_backlight_combo_key(logical)) {
        show_backlight_combo(raw, logical);
        refresh_modifier_view();
        hw_feedback();
        return;
    }

    update_key_view(pressed, raw, logical);
    refresh_modifier_view();
    hw_feedback();
}

static void reset_monitor_state()
{
    pressed_count = 0;
    released_count = 0;
    combo_count = 0;
    symbol_active = false;
    alt_active = false;
    caps_active = false;
    backspace_active = false;
    memset(history_lines, 0, sizeof(history_lines));
}

static void release_keyboard_monitor()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.setRawCallback(NULL);
#elif defined(ARDUINO) && defined(USING_TDECK_KEYBOARD)
    instance.kb.setRawCallback(NULL);
#endif
    disable_keyboard();
}

static void back_event_handler(lv_event_t *e)
{
    LV_UNUSED(e);

    release_keyboard_monitor();

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    status_label = NULL;
    key_label = NULL;
    detail_label = NULL;
    modifier_label = NULL;
    history_label = NULL;
    symbol_chip = NULL;
    alt_chip = NULL;
    caps_chip = NULL;
    backspace_chip = NULL;

    menu_show();
}

static lv_obj_t *create_monitor_panel(lv_obj_t *parent)
{
    bool compact = compact_keyboard_screen();
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, compact ? 1 : LV_SIZE_CONTENT);
    if (compact) {
        lv_obj_set_flex_grow(panel, 1);
    }
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, compact ? 6 : 8, 0);
    lv_obj_set_style_pad_row(panel, compact ? 2 : 6, 0);
    lv_obj_set_style_pad_column(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void create_keyboard_monitor_ui()
{
    bool compact = compact_keyboard_screen();
    lv_obj_t *card = ui_create_card(page_container, NULL);
    if (compact) {
        lv_obj_set_height(card, compact_card_height());
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_style_pad_all(card, compact ? 8 : 12, 0);
    lv_obj_set_style_pad_row(card, compact ? 6 : 8, 0);
    lv_obj_set_style_radius(card, compact ? 8 : 16, 0);

    status_label = lv_label_create(card);
    lv_obj_set_width(status_label, LV_PCT(100));
    lv_obj_set_height(status_label, compact ? 16 : LV_SIZE_CONTENT);
    lv_label_set_long_mode(status_label, compact ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(status_label, "Press keys. FN/ALT+B toggles BL.");

    lv_obj_t *key_panel = create_monitor_panel(card);
    key_label = lv_label_create(key_panel);
    lv_label_set_text(key_label, "--");
    lv_label_set_long_mode(key_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(key_label, LV_PCT(100));
    lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(key_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(key_label, compact ? &lv_font_montserrat_24 : &lv_font_montserrat_32, 0);

    lv_obj_t *bottom_row = lv_obj_create(card);
    lv_obj_set_width(bottom_row, LV_PCT(100));
    lv_obj_set_height(bottom_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottom_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_row, 0, 0);
    lv_obj_set_style_pad_all(bottom_row, 0, 0);
    lv_obj_set_style_pad_column(bottom_row, compact ? 6 : 10, 0);
    lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bottom_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chip_row = lv_obj_create(bottom_row);
    lv_obj_set_size(chip_row, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(chip_row, 1);
    lv_obj_set_height(chip_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(chip_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chip_row, 0, 0);
    lv_obj_set_style_pad_all(chip_row, 0, 0);
    lv_obj_set_style_pad_row(chip_row, compact ? 4 : 6, 0);
    lv_obj_set_style_pad_column(chip_row, compact ? 4 : 6, 0);
    lv_obj_set_flex_flow(chip_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(chip_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chip_row, LV_OBJ_FLAG_SCROLLABLE);

    symbol_chip = create_chip(chip_row, "SYM");
    alt_chip = create_chip(chip_row, compact ? "FN" : "FN/ALT");
    caps_chip = create_chip(chip_row, "CAP");
    backspace_chip = create_chip(chip_row, compact ? "BK" : "BKSP");

    detail_label = lv_label_create(bottom_row);
    lv_obj_set_width(detail_label, detail_width());
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(detail_label, "Idle r-- k--\nR-- C-- P:0 U:0");
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(detail_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_12, 0);

    if (!compact) {
        modifier_label = lv_label_create(card);
        lv_obj_set_width(modifier_label, LV_PCT(100));
        lv_label_set_long_mode(modifier_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(modifier_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(modifier_label, &lv_font_montserrat_12, 0);
    }
    refresh_modifier_view();

    if (!compact) {
        history_label = lv_label_create(card);
        lv_obj_set_width(history_label, LV_PCT(100));
        lv_label_set_long_mode(history_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(history_label, lv_color_hex(0xA7F3D0), 0);
        lv_obj_set_style_text_font(history_label, &lv_font_montserrat_12, 0);
        refresh_history_label();
    }
}

void ui_keyboard_enter(lv_obj_t *parent)
{
    reset_monitor_state();
    page_container = ui_create_app_page(parent, "Keyboard", back_event_handler);
    create_keyboard_monitor_ui();

    if (!hw_has_keyboard()) {
        lv_label_set_text(status_label, "Keyboard was not detected on this device.");
        lv_label_set_text(key_label, "No Keyboard");
        return;
    }

#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.setRawCallback(keyboard_raw_cb);
    enable_keyboard();
    lv_label_set_text(status_label, "Raw monitor active. FN/ALT+B toggles BL.");
#elif defined(ARDUINO) && defined(USING_TDECK_KEYBOARD)
    instance.kb.setRawCallback(keyboard_raw_cb);
    enable_keyboard();
    lv_label_set_text(status_label, "Character monitor active.");
#else
    lv_label_set_text(status_label, "Keyboard monitor is not available in this build.");
#endif
}

void ui_keyboard_exit(lv_obj_t *parent)
{
    LV_UNUSED(parent);
    release_keyboard_monitor();
}

app_t ui_keyboard_main = {
    .setup_func_cb = ui_keyboard_enter,
    .exit_func_cb = ui_keyboard_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_KEYBOARD */
