/**
 * @file      ui_ir_universal.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-28
 * 
 * Universal IR remote with per-device learned keys.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_IR_UNIVERSAL)

#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <SD.h>

extern IRrecv irrecv;
extern IRsend irsend;

static constexpr const char *IR_REMOTE_ROOT = "/ir_remote";
static constexpr uint16_t IR_RAW_MAX_SAMPLES = 2048;
static constexpr uint16_t IR_DEFAULT_FREQ = 38000;
static constexpr uint8_t MAX_KEY_BUTTONS = 16;

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t freq;
    uint16_t raw_len;
    uint16_t proto;
    uint64_t value;
} ir_file_header_t;

typedef struct {
    const char *label;
    const char *file;
} ir_remote_key_t;

typedef struct {
    const char *name;
    const ir_remote_key_t *keys;
    uint8_t key_count;
} ir_remote_profile_t;

static const ir_remote_key_t ac_keys[] = {
    {"Power", "Power"}, {"Mode", "Mode"}, {"Temp+", "TempUp"}, {"Temp-", "TempDown"},
    {"Fan", "Fan"}, {"Swing", "Swing"}, {"Turbo", "Turbo"}, {"Sleep", "Sleep"},
    {"Timer", "Timer"}, {"Eco", "Eco"}, {"Dry", "Dry"}, {"Heat", "Heat"}
};

static const ir_remote_key_t tv_keys[] = {
    {"Power", "Power"}, {"Mute", "Mute"}, {"Vol+", "VolUp"}, {"Vol-", "VolDown"},
    {"Ch+", "ChannelUp"}, {"Ch-", "ChannelDown"}, {"Source", "Source"}, {"Menu", "Menu"},
    {"OK", "Ok"}, {"Back", "Back"}, {"Up", "Up"}, {"Down", "Down"},
    {"Left", "Left"}, {"Right", "Right"}
};

static const ir_remote_key_t light_keys[] = {
    {"Power", "Power"}, {"Bright+", "BrightUp"}, {"Bright-", "BrightDown"}, {"Warm", "Warm"},
    {"Cool", "Cool"}, {"Red", "Red"}, {"Green", "Green"}, {"Blue", "Blue"},
    {"White", "White"}, {"Scene", "Scene"}
};

static const ir_remote_key_t custom_keys[] = {
    {"Key 1", "Key1"}, {"Key 2", "Key2"}, {"Key 3", "Key3"}, {"Key 4", "Key4"},
    {"Key 5", "Key5"}, {"Key 6", "Key6"}, {"Key 7", "Key7"}, {"Key 8", "Key8"},
    {"Key 9", "Key9"}, {"Key 10", "Key10"}, {"Key 11", "Key11"}, {"Key 12", "Key12"}
};

static const ir_remote_profile_t profiles[] = {
    {"AC", ac_keys, (uint8_t)(sizeof(ac_keys) / sizeof(ac_keys[0]))},
    {"TV", tv_keys, (uint8_t)(sizeof(tv_keys) / sizeof(tv_keys[0]))},
    {"Light", light_keys, (uint8_t)(sizeof(light_keys) / sizeof(light_keys[0]))},
    {"Custom", custom_keys, (uint8_t)(sizeof(custom_keys) / sizeof(custom_keys[0]))},
};

static lv_obj_t *page_container = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *learned_lbl = NULL;
static lv_obj_t *profile_dd = NULL;
static lv_obj_t *record_btn = NULL;
static lv_obj_t *key_grid = NULL;
static lv_timer_t *record_timer = NULL;
static lv_obj_t *key_buttons[MAX_KEY_BUTTONS] = {NULL};

static uint8_t current_profile = 0;
static int8_t pending_key = -1;
static bool record_mode = false;

static void build_key_grid(void);
static void update_key_styles(void);

static const ir_remote_profile_t *active_profile(void)
{
    if (current_profile >= sizeof(profiles) / sizeof(profiles[0])) current_profile = 0;
    return &profiles[current_profile];
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_lbl) return;
    lv_label_set_text(status_lbl, text);
    lv_obj_set_style_text_color(status_lbl, color, 0);
}

static bool make_profile_dir(char *out, size_t out_size)
{
    const ir_remote_profile_t *profile = active_profile();
    int written = snprintf(out, out_size, "%s/%s", IR_REMOTE_ROOT, profile->name);
    return written > 0 && written < (int)out_size;
}

static bool make_key_path(uint8_t key_index, char *out, size_t out_size)
{
    const ir_remote_profile_t *profile = active_profile();
    if (key_index >= profile->key_count) return false;
    char dir[64];
    if (!make_profile_dir(dir, sizeof(dir))) return false;
    int written = snprintf(out, out_size, "%s/%s.ir", dir, profile->keys[key_index].file);
    return written > 0 && written < (int)out_size;
}

static void ensure_remote_dirs(void)
{
#if defined(ARDUINO)
    if (!SD.exists(IR_REMOTE_ROOT)) SD.mkdir(IR_REMOTE_ROOT);
    char dir[64];
    if (make_profile_dir(dir, sizeof(dir)) && !SD.exists(dir)) SD.mkdir(dir);
#endif
}

static bool key_has_code(uint8_t key_index)
{
#if defined(ARDUINO)
    char path[96];
    return make_key_path(key_index, path, sizeof(path)) && SD.exists(path);
#else
    return false;
#endif
}

static uint8_t count_learned_keys(void)
{
    const ir_remote_profile_t *profile = active_profile();
    uint8_t count = 0;
    for (uint8_t i = 0; i < profile->key_count; ++i) {
        if (key_has_code(i)) count++;
    }
    return count;
}

static void update_learned_label(void)
{
    if (!learned_lbl) return;
    const ir_remote_profile_t *profile = active_profile();
    char buf[32];
    snprintf(buf, sizeof(buf), "%u/%u", count_learned_keys(), profile->key_count);
    lv_label_set_text(learned_lbl, buf);
}

static void cancel_pending_recording(void)
{
    if (record_timer) {
        lv_timer_del(record_timer);
        record_timer = NULL;
    }
    pending_key = -1;
    update_key_styles();
}

static bool write_ir_file(uint8_t key_index, const decode_results *results)
{
#if defined(ARDUINO)
    char path[96];
    if (!make_key_path(key_index, path, sizeof(path))) return false;

    uint16_t len = getCorrectedRawLength(results);
    if (len == 0 || len > IR_RAW_MAX_SAMPLES) {
        set_status("Raw data is too long.", lv_color_hex(0xFF4444));
        return false;
    }

    uint16_t *raw = resultToRawArray(results);
    if (!raw) {
        set_status("Out of memory while capturing.", lv_color_hex(0xFF4444));
        return false;
    }

    ensure_remote_dirs();
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        delete[] raw;
        set_status("Failed to open file.", lv_color_hex(0xFF4444));
        return false;
    }

    ir_file_header_t header;
    memcpy(header.magic, "IRAW", 4);
    header.version = 1;
    header.freq = IR_DEFAULT_FREQ;
    header.raw_len = len;
    header.proto = (uint16_t)results->decode_type;
    header.value = results->value;

    size_t header_written = f.write((uint8_t *)&header, sizeof(header));
    size_t raw_size = len * sizeof(uint16_t);
    size_t data_written = f.write((uint8_t *)raw, raw_size);
    f.close();
    delete[] raw;

    if (header_written != sizeof(header) || data_written != raw_size) {
        SD.remove(path);
        set_status("Failed to write file.", lv_color_hex(0xFF4444));
        return false;
    }
    return true;
#else
    return false;
#endif
}

static bool send_ir_file(uint8_t key_index)
{
#if defined(ARDUINO)
    char path[96];
    if (!make_key_path(key_index, path, sizeof(path)) || !SD.exists(path)) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) {
        set_status("Failed to open file.", lv_color_hex(0xFF4444));
        return false;
    }

    ir_file_header_t header;
    if (f.read((uint8_t *)&header, sizeof(header)) != sizeof(header)) {
        f.close();
        set_status("Invalid IR file.", lv_color_hex(0xFF4444));
        return false;
    }

    if (memcmp(header.magic, "IRAW", 4) != 0 ||
        header.version != 1 ||
        header.raw_len == 0 ||
        header.raw_len > IR_RAW_MAX_SAMPLES) {
        f.close();
        set_status("Unsupported IR file.", lv_color_hex(0xFF4444));
        return false;
    }

    uint16_t *raw = (uint16_t *)malloc(header.raw_len * sizeof(uint16_t));
    if (!raw) {
        f.close();
        set_status("Out of memory.", lv_color_hex(0xFF4444));
        return false;
    }

    size_t raw_size = header.raw_len * sizeof(uint16_t);
    if (f.read((uint8_t *)raw, raw_size) != raw_size) {
        free(raw);
        f.close();
        set_status("Failed to read file.", lv_color_hex(0xFF4444));
        return false;
    }
    f.close();

    hw_ir_function_select(true);
    irsend.sendRaw(raw, header.raw_len, header.freq);
    hw_ir_function_select(false);
    free(raw);
    return true;
#else
    return false;
#endif
}

static void record_poll_cb(lv_timer_t *timer)
{
#if defined(ARDUINO)
    if (pending_key < 0) return;

    decode_results results;
    if (!irrecv.decode(&results)) return;

    const ir_remote_profile_t *profile = active_profile();
    uint8_t key_index = (uint8_t)pending_key;
    if (results.overflow) {
        irrecv.resume();
        cancel_pending_recording();
        set_status("Capture overflow. Try again closer.", lv_color_hex(0xFF4444));
        return;
    }

    bool ok = write_ir_file(key_index, &results);
    irrecv.resume();
    cancel_pending_recording();
    update_learned_label();
    update_key_styles();

    if (ok) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Learned %s / %s", profile->name, profile->keys[key_index].label);
        set_status(buf, UI_COLOR_ACCENT);
    }
#endif
}

static void start_recording(uint8_t key_index)
{
    const ir_remote_profile_t *profile = active_profile();
    if (key_index >= profile->key_count) return;

    cancel_pending_recording();
    ensure_remote_dirs();
    pending_key = (int8_t)key_index;
    hw_ir_function_select(false);
    record_timer = lv_timer_create(record_poll_cb, 50, NULL);

    char buf[72];
    snprintf(buf, sizeof(buf), "Learning %s. Press remote now.", profile->keys[key_index].label);
    set_status(buf, UI_COLOR_WARNING);
    update_key_styles();
}

static void key_btn_cb(lv_event_t *e)
{
    uint8_t key_index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const ir_remote_profile_t *profile = active_profile();
    if (key_index >= profile->key_count) return;

    if (record_mode) {
        start_recording(key_index);
        return;
    }

    if (!send_ir_file(key_index)) {
        char buf[72];
        snprintf(buf, sizeof(buf), "No code for %s.", profile->keys[key_index].label);
        set_status(buf, lv_color_hex(0xFF4444));
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Sent %s / %s", profile->name, profile->keys[key_index].label);
    set_status(buf, UI_COLOR_ACCENT);
}

static void profile_changed_cb(lv_event_t *e)
{
    if (!profile_dd) return;
    cancel_pending_recording();
    current_profile = lv_dropdown_get_selected(profile_dd);
    ensure_remote_dirs();
    build_key_grid();
    update_learned_label();
    const ir_remote_profile_t *profile = active_profile();
    char buf[48];
    snprintf(buf, sizeof(buf), "%s remote selected.", profile->name);
    set_status(buf, UI_COLOR_TEXT_SECONDARY);
}

static void record_toggle_cb(lv_event_t *e)
{
    record_mode = !record_mode;
    if (record_mode) {
        lv_obj_add_state(record_btn, LV_STATE_CHECKED);
        set_status("REC on. Tap a key to learn.", UI_COLOR_WARNING);
    } else {
        lv_obj_remove_state(record_btn, LV_STATE_CHECKED);
        cancel_pending_recording();
        set_status("REC off. Tap learned keys to send.", UI_COLOR_TEXT_SECONDARY);
    }
    update_key_styles();
}

static void refresh_cb(lv_event_t *e)
{
    cancel_pending_recording();
    update_learned_label();
    update_key_styles();
    set_status("Remote refreshed.", UI_COLOR_TEXT_SECONDARY);
}

static void update_key_styles(void)
{
    const ir_remote_profile_t *profile = active_profile();
    for (uint8_t i = 0; i < profile->key_count && i < MAX_KEY_BUTTONS; ++i) {
        lv_obj_t *btn = key_buttons[i];
        if (!btn) continue;

        lv_color_t color = lv_color_hex(0x303030);
        lv_color_t border = UI_COLOR_DIVIDER;
        if (key_has_code(i)) {
            color = record_mode ? lv_color_hex(0x2C3C34) : UI_COLOR_ACCENT_DIM;
            border = UI_COLOR_ACCENT;
        }
        if (pending_key == (int8_t)i) {
            color = UI_COLOR_WARNING;
            border = UI_COLOR_WARNING;
        }
        lv_obj_set_style_bg_color(btn, color, 0);
        lv_obj_set_style_border_color(btn, border, 0);
    }
}

static lv_obj_t *create_remote_button(lv_obj_t *parent, uint8_t key_index)
{
    const ir_remote_profile_t *profile = active_profile();
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, is_screen_small() ? 62 : 68, 36);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, key_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)key_index);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, profile->keys[key_index].label);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, LV_PCT(92));
    lv_obj_center(lbl);
    return btn;
}

static void build_key_grid(void)
{
    if (!key_grid) return;

    for (uint8_t i = 0; i < MAX_KEY_BUTTONS; ++i) key_buttons[i] = NULL;
    lv_obj_clean(key_grid);

    const ir_remote_profile_t *profile = active_profile();
    for (uint8_t i = 0; i < profile->key_count && i < MAX_KEY_BUTTONS; ++i) {
        key_buttons[i] = create_remote_button(key_grid, i);
    }
    update_key_styles();
}

static void back_event_handler(lv_event_t *e)
{
    cancel_pending_recording();
    hw_ir_function_select(false);

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    status_lbl = NULL;
    learned_lbl = NULL;
    profile_dd = NULL;
    record_btn = NULL;
    key_grid = NULL;
    record_mode = false;
    menu_show();
}

void ui_ir_universal_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "IR Remote", back_event_handler);
    if (!hw_is_sd_insert()) {
        return;
    }
    ensure_remote_dirs();

    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    status_lbl = lv_label_create(status_card);
    lv_label_set_text(status_lbl, "Tap learned keys to send.");
    lv_obj_set_style_text_color(status_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *control_card = ui_create_card(page_container, "Device");
    profile_dd = lv_dropdown_create(control_card);
    lv_dropdown_set_options(profile_dd, "AC\nTV\nLight\nCustom");
    lv_dropdown_set_selected(profile_dd, current_profile);
    lv_obj_set_width(profile_dd, LV_PCT(55));
    lv_obj_set_style_bg_color(profile_dd, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(profile_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(profile_dd, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(profile_dd, UI_COLOR_DIVIDER, 0);
    lv_obj_add_event_cb(profile_dd, profile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(control_card, LV_SYMBOL_SETTINGS, "Profile", profile_dd);

    learned_lbl = lv_label_create(control_card);
    lv_obj_set_style_text_color(learned_lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(learned_lbl, &lv_font_montserrat_12, 0);
    ui_create_card_item(control_card, LV_SYMBOL_OK, "Learned", learned_lbl);
    update_learned_label();

    lv_obj_t *control_row = lv_obj_create(control_card);
    lv_obj_set_size(control_row, LV_PCT(100), 38);
    lv_obj_set_style_bg_opa(control_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_row, 0, 0);
    lv_obj_set_style_pad_all(control_row, 0, 0);
    lv_obj_set_style_pad_column(control_row, 8, 0);
    lv_obj_set_flex_flow(control_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    record_btn = lv_btn_create(control_row);
    lv_obj_set_size(record_btn, 88, 32);
    lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_color(record_btn, UI_COLOR_WARNING, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(record_btn, 1, 0);
    lv_obj_set_style_border_color(record_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(record_btn, 6, 0);
    lv_obj_add_event_cb(record_btn, record_toggle_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *lbl = lv_label_create(record_btn);
        lv_label_set_text(lbl, "REC");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
    }

    lv_obj_t *refresh_btn = lv_btn_create(control_row);
    lv_obj_set_size(refresh_btn, 88, 32);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(refresh_btn, 1, 0);
    lv_obj_set_style_border_color(refresh_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(refresh_btn, 6, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *lbl = lv_label_create(refresh_btn);
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(lbl);
    }

    lv_obj_t *key_card = ui_create_card(page_container, "Keys");
    key_grid = lv_obj_create(key_card);
    lv_obj_set_size(key_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(key_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(key_grid, 0, 0);
    lv_obj_set_style_radius(key_grid, 0, 0);
    lv_obj_set_style_pad_all(key_grid, 0, 0);
    lv_obj_set_style_pad_row(key_grid, 8, 0);
    lv_obj_set_style_pad_column(key_grid, 8, 0);
    lv_obj_set_flex_flow(key_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(key_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    build_key_grid();
}

void ui_ir_universal_exit(lv_obj_t *parent)
{
}

app_t ui_ir_universal_main = {
    .setup_func_cb = ui_ir_universal_enter,
    .exit_func_cb = ui_ir_universal_exit,
    .user_data = nullptr,
};

#endif
