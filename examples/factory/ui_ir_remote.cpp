/**
 * @file      ui_ir_remote.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-05-15
 *
 * Provides factory NEC and SD-backed infrared transmission controls.
 */
#include <LilyGoLog.h>
#include <ctype.h>
#include <strings.h>
#include "ui_define.h"

#if !defined(EXCLUDE_IR_REMOTE)

#if defined(ARDUINO) && FACTORY_HAS_SD
#include <IRsend.h>
#include <SD.h>
extern IRsend irsend;
#endif

static constexpr uint32_t FACTORY_NEC_CODE = 0x00FF02FDUL;
static constexpr uint8_t IR_FILE_LIMIT = 24;
static constexpr uint8_t IR_SIGNAL_LIMIT = 48;
static constexpr uint16_t IR_RAW_MAX_SAMPLES = 2048;

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t freq;
    uint16_t raw_len;
    uint16_t proto;
    uint64_t value;
} ir_binary_header_t;

typedef struct {
    char path[128];
    uint32_t size;
} ir_file_entry_t;

enum ir_signal_type_t {
    IR_SIGNAL_BINARY_RAW,
    IR_SIGNAL_TEXT_RAW,
    IR_SIGNAL_TEXT_PARSED,
};

typedef struct {
    char name[48];
    char protocol[16];
    ir_signal_type_t type;
    uint32_t address;
    uint32_t command;
    uint32_t data_offset;
    uint32_t frequency;
    uint16_t raw_count;
    uint8_t duty_cycle;
} ir_signal_entry_t;

enum ir_send_mode_t {
    IR_SEND_MODE_FACTORY,
    IR_SEND_MODE_SD,
};

static lv_obj_t *page_container = NULL;
static lv_obj_t *status_dot = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *tx_count_label = NULL;
static lv_obj_t *factory_mode_btn = NULL;
static lv_obj_t *sd_mode_btn = NULL;
static lv_obj_t *factory_view = NULL;
static lv_obj_t *sd_view = NULL;
static lv_obj_t *list_title_label = NULL;
static lv_obj_t *list_back_btn = NULL;
static lv_obj_t *list_refresh_btn = NULL;
static lv_obj_t *item_list = NULL;
static lv_obj_t *selected_name_label = NULL;
static lv_obj_t *selected_meta_label = NULL;
static lv_obj_t *send_selected_btn = NULL;
static lv_obj_t *item_rows[IR_SIGNAL_LIMIT] = {};
static ir_file_entry_t file_entries[IR_FILE_LIMIT] = {};
static ir_signal_entry_t signal_entries[IR_SIGNAL_LIMIT] = {};
static uint8_t file_count = 0;
static uint8_t signal_count = 0;
static int8_t selected_signal = -1;
static uint32_t *loaded_raw = NULL;
static char active_file_path[128] = {};
static uint32_t tx_count = 0;
static bool showing_signals = false;
static bool ir_send_active = false;
static ir_send_mode_t current_mode = IR_SEND_MODE_FACTORY;

#if LILYGO_DEBUG_ENABLED
static const char *ir_signal_type_name(ir_signal_type_t type)
{
    switch (type) {
    case IR_SIGNAL_BINARY_RAW:
        return "binary-raw";
    case IR_SIGNAL_TEXT_RAW:
        return "text-raw";
    case IR_SIGNAL_TEXT_PARSED:
        return "text-parsed";
    default:
        return "unknown";
    }
}

static void log_ui_state(const char *stage)
{
    LILYGO_LOG_I(
        "IR_SEND_DBG state stage=%s mode=%s view=%s files=%u signals=%u selected=%d path=%s page_y=%ld page_bottom=%ld",
        stage,
        current_mode == IR_SEND_MODE_SD ? "sd" : "factory",
        showing_signals ? "signals" : "files",
        file_count,
        signal_count,
        selected_signal,
        active_file_path[0] ? active_file_path : "<none>",
        page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L,
        page_container ? (long)lv_obj_get_scroll_bottom(page_container) : -1L);
}

static void page_scroll_log_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        log_ui_state("scroll-begin");
    } else if (code == LV_EVENT_SCROLL_END) {
        log_ui_state("scroll-end");
    }
}

static void file_row_focus_log_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_FOCUSED && code != LV_EVENT_DEFOCUSED) return;
    uintptr_t token = (uintptr_t)lv_event_get_user_data(event);
    if (token == 0 || token > file_count) {
        LILYGO_LOG_W("IR_SEND_DBG file-focus invalid token=%lu files=%u event=%d",
                     (unsigned long)token, file_count, (int)code);
        return;
    }
    uint8_t index = token - 1;
    LILYGO_LOG_I("IR_SEND_DBG file-focus event=%s index=%u path=%s page_y=%ld",
                 code == LV_EVENT_FOCUSED ? "focused" : "defocused",
                 index, file_entries[index].path,
                 page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L);
}

static void signal_row_focus_log_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_FOCUSED && code != LV_EVENT_DEFOCUSED) return;
    uintptr_t token = (uintptr_t)lv_event_get_user_data(event);
    if (token == 0 || token > signal_count) {
        LILYGO_LOG_W("IR_SEND_DBG signal-focus invalid token=%lu signals=%u event=%d",
                     (unsigned long)token, signal_count, (int)code);
        return;
    }
    uint8_t index = token - 1;
    LILYGO_LOG_I("IR_SEND_DBG signal-focus event=%s index=%u name=%s page_y=%ld",
                 code == LV_EVENT_FOCUSED ? "focused" : "defocused",
                 index, signal_entries[index].name,
                 page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L);
}
#endif

static bool compact_layout()
{
    return lv_display_get_horizontal_resolution(NULL) <= 320 ||
           lv_display_get_vertical_resolution(NULL) <= 200;
}

static lv_obj_t *create_plain_container(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    return container;
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, compact_layout() ? 36 : 42);
    lv_obj_set_style_bg_color(button, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(button, UI_COLOR_ACCENT_DIM, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(button);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    return button;
}

static void set_status(const char *text, lv_color_t color)
{
    if (status_label) {
        lv_label_set_text(status_label, text);
        lv_obj_set_style_text_color(status_label, color, 0);
    }
    if (status_dot) lv_obj_set_style_bg_color(status_dot, color, 0);
}

static void update_tx_count()
{
    if (tx_count_label) {
        lv_label_set_text_fmt(tx_count_label, "TX %lu", (unsigned long)tx_count);
    }
}

static const char *path_basename(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool has_ir_extension(const char *name)
{
    if (!name) return false;
    size_t length = strlen(name);
    if (length < 3) return false;
    const char *extension = name + length - 3;
    return extension[0] == '.' &&
           (extension[1] == 'i' || extension[1] == 'I') &&
           (extension[2] == 'r' || extension[2] == 'R');
}

static void clear_loaded_signal()
{
    if (loaded_raw) {
        free(loaded_raw);
        loaded_raw = NULL;
    }
    selected_signal = -1;
    if (selected_name_label) lv_label_set_text(selected_name_label, "Choose a signal");
    if (selected_meta_label) lv_label_set_text(selected_meta_label, "Select a file, then a signal");
    if (send_selected_btn) lv_obj_add_state(send_selected_btn, LV_STATE_DISABLED);
}

static void clear_item_list()
{
    if (item_list) lv_obj_clean(item_list);
    memset(item_rows, 0, sizeof(item_rows));
}

static void create_empty_state(const char *text)
{
    lv_obj_t *label = lv_label_create(item_list);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_ver(label, 12, 0);
}

static void style_list_row(lv_obj_t *row)
{
    lv_obj_set_size(row, LV_PCT(100), compact_layout() ? 40 : 46);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(row);
}

static void add_row_contents(lv_obj_t *row, const char *icon_text, const char *title_text,
                             const char *detail_text)
{
    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, icon_text);
    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);

    lv_obj_t *labels = create_plain_container(row);
    lv_obj_set_width(labels, 1);
    lv_obj_set_height(labels, LV_PCT(100));
    lv_obj_set_flex_grow(labels, 1);

    lv_obj_t *title = lv_label_create(labels);
    lv_label_set_text(title, title_text);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *detail = lv_label_create(labels);
    lv_label_set_text(detail, detail_text);
    lv_obj_set_width(detail, LV_PCT(100));
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(detail, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_10, 0);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *arrow = lv_label_create(row);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, UI_COLOR_TEXT_SECONDARY, 0);
}

#if defined(ARDUINO) && FACTORY_HAS_SD

static uint64_t reverse_bits(uint64_t value, uint8_t width)
{
    uint64_t reversed = 0;
    for (uint8_t i = 0; i < width; ++i) {
        reversed = (reversed << 1) | (value & 1U);
        value >>= 1;
    }
    return reversed;
}

static bool parse_hex_bytes(const String &text, uint32_t *value)
{
    if (!value) return false;
    *value = 0;
    const char *cursor = text.c_str();
    uint8_t byte_index = 0;
    while (*cursor && byte_index < 4) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!isxdigit((unsigned char)*cursor)) break;
        char *end = NULL;
        unsigned long byte_value = strtoul(cursor, &end, 16);
        if (end == cursor || byte_value > 0xFF) return false;
        *value |= (uint32_t)byte_value << (byte_index * 8);
        byte_index++;
        cursor = end;
    }
    return byte_index > 0;
}

static uint16_t count_raw_values(const String &text)
{
    const char *cursor = text.c_str();
    uint16_t count = 0;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor) break;
        if (!isdigit((unsigned char)*cursor)) return 0;
        char *end = NULL;
        unsigned long value = strtoul(cursor, &end, 10);
        if (end == cursor || value == 0 || ++count > IR_RAW_MAX_SAMPLES) return 0;
        cursor = end;
    }
    return count;
}

static bool protocol_supported(const char *protocol)
{
    static const char *supported[] = {
        "NEC", "NECext", "Samsung32", "RC5", "RC5X", "RC6",
        "SIRC", "SIRC15", "SIRC20", "Kaseikyo"
    };
    for (const char *name : supported) {
        if (strcasecmp(protocol, name) == 0) return true;
    }
    return false;
}

static void scan_directory(const char *directory, uint8_t depth)
{
    if (file_count >= IR_FILE_LIMIT || depth > 3) {
        LILYGO_LOG_W("IR_SEND_DBG scan skipped dir=%s depth=%u files=%u",
                     directory, depth, file_count);
        return;
    }
    File root = SD.open(directory);
    if (!root || !root.isDirectory()) {
        LILYGO_LOG_W("IR_SEND_DBG scan open failed dir=%s depth=%u opened=%u directory=%u",
                     directory, depth, root ? 1U : 0U,
                     root && root.isDirectory() ? 1U : 0U);
        if (root) root.close();
        return;
    }

    LILYGO_LOG_I("IR_SEND_DBG scan begin dir=%s depth=%u files=%u",
                 directory, depth, file_count);

    File item = root.openNextFile();
    while (item && file_count < IR_FILE_LIMIT) {
        char path[128];
        const char *name = item.name();
        int written = name[0] == '/'
                      ? snprintf(path, sizeof(path), "%s", name)
                      : snprintf(path, sizeof(path), "%s/%s", directory, name);
        bool is_directory = item.isDirectory();
        uint32_t size = item.size();
        item.close();

        if (written > 0 && written < (int)sizeof(path)) {
            if (is_directory) {
                scan_directory(path, depth + 1);
            } else if (has_ir_extension(path)) {
                bool duplicate = false;
                for (uint8_t i = 0; i < file_count; ++i) {
                    if (strcmp(file_entries[i].path, path) == 0) duplicate = true;
                }
                if (!duplicate) {
                    snprintf(file_entries[file_count].path,
                             sizeof(file_entries[file_count].path), "%s", path);
                    file_entries[file_count].size = size;
                    LILYGO_LOG_I("IR_SEND_DBG scan file index=%u path=%s size=%lu",
                                 file_count, path, (unsigned long)size);
                    file_count++;
                } else {
                    LILYGO_LOG_W("IR_SEND_DBG scan duplicate path=%s", path);
                }
            }
        }
        item = root.openNextFile();
    }
    root.close();
    LILYGO_LOG_I("IR_SEND_DBG scan end dir=%s depth=%u files=%u",
                 directory, depth, file_count);
}

static bool parse_text_signals(File &file)
{
    file.seek(0);
    String first_line = file.readStringUntil('\n');
    first_line.trim();
    LILYGO_LOG_I("IR_SEND_DBG text header=%s", first_line.c_str());
    if (!first_line.startsWith("Filetype: IR signals file") &&
        !first_line.startsWith("Filetype: IR library file")) {
        LILYGO_LOG_W("IR_SEND_DBG text header unsupported");
        return false;
    }

    ir_signal_entry_t *signal = NULL;
    while (file.available() && signal_count < IR_SIGNAL_LIMIT) {
        uint32_t line_offset = file.position();
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.startsWith("name:")) {
            signal = &signal_entries[signal_count++];
            memset(signal, 0, sizeof(*signal));
            signal->duty_cycle = 33;
            String value = line.substring(5);
            value.trim();
            snprintf(signal->name, sizeof(signal->name), "%s", value.c_str());
            LILYGO_LOG_I("IR_SEND_DBG text signal begin index=%u name=%s offset=%lu",
                         signal_count - 1, signal->name, (unsigned long)line_offset);
        } else if (signal && line.startsWith("type:")) {
            String value = line.substring(5);
            value.trim();
            signal->type = value.equalsIgnoreCase("raw") ? IR_SIGNAL_TEXT_RAW : IR_SIGNAL_TEXT_PARSED;
        } else if (signal && line.startsWith("protocol:")) {
            String value = line.substring(9);
            value.trim();
            snprintf(signal->protocol, sizeof(signal->protocol), "%s", value.c_str());
        } else if (signal && line.startsWith("address:")) {
            String value = line.substring(8);
            value.trim();
            parse_hex_bytes(value, &signal->address);
        } else if (signal && line.startsWith("command:")) {
            String value = line.substring(8);
            value.trim();
            parse_hex_bytes(value, &signal->command);
        } else if (signal && line.startsWith("frequency:")) {
            String value = line.substring(10);
            value.trim();
            signal->frequency = strtoul(value.c_str(), NULL, 10);
        } else if (signal && line.startsWith("duty_cycle:")) {
            String value = line.substring(11);
            value.trim();
            int duty = (int)(value.toFloat() * 100.0F + 0.5F);
            signal->duty_cycle = constrain(duty, 1, 100);
        } else if (signal && line.startsWith("data:")) {
            signal->data_offset = line_offset + line.indexOf(':') + 1;
            String value = line.substring(5);
            value.trim();
            signal->raw_count = count_raw_values(value);
        }
    }
    LILYGO_LOG_I("IR_SEND_DBG text parse complete signals=%u", signal_count);
    LILYGO_LOG_ONLY(
        for (uint8_t i = 0; i < signal_count; ++i) {
            const ir_signal_entry_t &entry = signal_entries[i];
            LILYGO_LOG_I(
                "IR_SEND_DBG signal index=%u name=%s type=%s protocol=%s freq=%lu duty=%u raw=%u offset=%lu address=%08lX command=%08lX",
                i, entry.name, ir_signal_type_name(entry.type),
                entry.protocol[0] ? entry.protocol : "<none>",
                (unsigned long)entry.frequency, entry.duty_cycle, entry.raw_count,
                (unsigned long)entry.data_offset, (unsigned long)entry.address,
                (unsigned long)entry.command);
        }
    );
    return signal_count > 0;
}

static bool parse_signal_file(const char *path)
{
    LILYGO_LOG_I("IR_SEND_DBG parse begin path=%s", path ? path : "<null>");
    signal_count = 0;
    memset(signal_entries, 0, sizeof(signal_entries));
    File file = SD.open(path, FILE_READ);
    if (!file) {
        LILYGO_LOG_E("IR_SEND_DBG parse open failed path=%s", path ? path : "<null>");
        return false;
    }
    LILYGO_LOG_I("IR_SEND_DBG parse opened path=%s size=%lu",
                 path, (unsigned long)file.size());

    char magic[4] = {};
    bool binary = file.read((uint8_t *)magic, sizeof(magic)) == sizeof(magic) &&
                  memcmp(magic, "IRAW", sizeof(magic)) == 0;
    if (binary) {
        file.seek(0);
        ir_binary_header_t header = {};
        size_t header_read = file.read((uint8_t *)&header, sizeof(header));
        LILYGO_LOG_I(
            "IR_SEND_DBG binary header bytes=%u version=%u freq=%u raw=%u proto=%u file_size=%lu required=%lu",
            (unsigned)header_read, header.version, header.freq, header.raw_len,
            header.proto, (unsigned long)file.size(),
            (unsigned long)(sizeof(header) + header.raw_len * sizeof(uint16_t)));
        if (header_read != sizeof(header) || header.version != 1 ||
            header.freq == 0 || header.raw_len == 0 ||
            header.raw_len > IR_RAW_MAX_SAMPLES ||
            file.size() < sizeof(header) + header.raw_len * sizeof(uint16_t)) {
            LILYGO_LOG_E("IR_SEND_DBG binary validation failed path=%s", path);
            file.close();
            return false;
        }
        ir_signal_entry_t &signal = signal_entries[0];
        snprintf(signal.name, sizeof(signal.name), "%s", path_basename(path));
        snprintf(signal.protocol, sizeof(signal.protocol), "IRAW");
        signal.type = IR_SIGNAL_BINARY_RAW;
        signal.frequency = header.freq;
        signal.raw_count = header.raw_len;
        signal.data_offset = sizeof(header);
        signal.duty_cycle = 33;
        signal_count = 1;
        file.close();
        LILYGO_LOG_I("IR_SEND_DBG parse success path=%s format=binary signals=1", path);
        return true;
    }

    bool parsed = parse_text_signals(file);
    file.close();
    LILYGO_LOG_I("IR_SEND_DBG parse end path=%s format=text success=%u signals=%u",
                 path, parsed ? 1U : 0U, signal_count);
    return parsed;
}

static bool load_raw_signal(const ir_signal_entry_t &signal)
{
    LILYGO_LOG_I("IR_SEND_DBG raw load begin path=%s name=%s type=%d count=%u offset=%lu",
                 active_file_path, signal.name, (int)signal.type, signal.raw_count,
                 (unsigned long)signal.data_offset);
    if (signal.raw_count == 0 || signal.raw_count > IR_RAW_MAX_SAMPLES) {
        LILYGO_LOG_E("IR_SEND_DBG raw load invalid count=%u", signal.raw_count);
        return false;
    }
    File file = SD.open(active_file_path, FILE_READ);
    if (!file || !file.seek(signal.data_offset)) {
        LILYGO_LOG_E("IR_SEND_DBG raw load open/seek failed opened=%u offset=%lu",
                     file ? 1U : 0U, (unsigned long)signal.data_offset);
        if (file) file.close();
        return false;
    }

    uint32_t *raw = (uint32_t *)malloc(signal.raw_count * sizeof(uint32_t));
    if (!raw) {
        LILYGO_LOG_E("IR_SEND_DBG raw load allocation failed bytes=%lu",
                     (unsigned long)(signal.raw_count * sizeof(uint32_t)));
        file.close();
        return false;
    }

    bool valid = true;
    if (signal.type == IR_SIGNAL_BINARY_RAW) {
        for (uint16_t i = 0; i < signal.raw_count; ++i) {
            uint16_t value = 0;
            if (file.read((uint8_t *)&value, sizeof(value)) != sizeof(value)) {
                valid = false;
                break;
            }
            raw[i] = value;
        }
    } else {
        uint16_t count = 0;
        uint64_t value = 0;
        bool in_number = false;
        while (file.available() && count < signal.raw_count) {
            int input = file.read();
            if (input == '\r' || input == '\n') break;
            if (isdigit(input)) {
                value = value * 10 + (input - '0');
                if (value > UINT32_MAX) {
                    valid = false;
                    break;
                }
                in_number = true;
            } else if (in_number) {
                raw[count++] = (uint32_t)value;
                value = 0;
                in_number = false;
            }
        }
        if (valid && in_number && count < signal.raw_count) raw[count++] = (uint32_t)value;
        valid = valid && count == signal.raw_count;
    }
    file.close();

    if (!valid) {
        LILYGO_LOG_E("IR_SEND_DBG raw load data invalid name=%s", signal.name);
        free(raw);
        return false;
    }
    for (uint16_t i = 0; i < signal.raw_count; i += 2) {
        if (raw[i] == 0 || raw[i] > UINT16_MAX) {
            LILYGO_LOG_E("IR_SEND_DBG raw mark invalid sample=%u value=%lu",
                         i, (unsigned long)raw[i]);
            free(raw);
            return false;
        }
    }
    loaded_raw = raw;
    LILYGO_LOG_I("IR_SEND_DBG raw load success name=%s count=%u",
                 signal.name, signal.raw_count);
    return true;
}

static bool send_parsed_signal(const ir_signal_entry_t &signal)
{
    const uint32_t address = signal.address;
    const uint32_t command = signal.command;
    if (strcasecmp(signal.protocol, "NEC") == 0) {
        irsend.sendNEC(irsend.encodeNEC(address & 0xFF, command & 0xFF), 32);
    } else if (strcasecmp(signal.protocol, "NECext") == 0) {
        uint32_t data = ((uint32_t)reverse_bits(address & 0xFFFF, 16) << 16) |
                        (uint32_t)reverse_bits(command & 0xFFFF, 16);
        irsend.sendNEC(data, 32);
    } else if (strcasecmp(signal.protocol, "Samsung32") == 0) {
        irsend.sendSAMSUNG(irsend.encodeSAMSUNG(address & 0xFF, command & 0xFF), 32);
    } else if (strcasecmp(signal.protocol, "RC5") == 0) {
        irsend.sendRC5(irsend.encodeRC5(address & 0x1F, command & 0x3F), kRC5Bits);
    } else if (strcasecmp(signal.protocol, "RC5X") == 0) {
        irsend.sendRC5(irsend.encodeRC5X(address & 0x1F, command & 0x7F), kRC5XBits);
    } else if (strcasecmp(signal.protocol, "RC6") == 0) {
        irsend.sendRC6(irsend.encodeRC6(address & 0xFF, command & 0xFF), kRC6Mode0Bits);
    } else if (strncasecmp(signal.protocol, "SIRC", 4) == 0) {
        uint8_t bits = strcasecmp(signal.protocol, "SIRC15") == 0 ? 15 :
                       strcasecmp(signal.protocol, "SIRC20") == 0 ? 20 : 12;
        uint16_t base_address = address & (bits == 15 ? 0xFF : 0x1F);
        uint16_t extended = bits == 20 ? (address >> 5) & 0xFF : 0;
        irsend.sendSony(irsend.encodeSony(bits, command & 0x7F, base_address, extended), bits, 2);
    } else if (strcasecmp(signal.protocol, "Kaseikyo") == 0) {
        uint8_t id = (address >> 24) & 0xFF;
        uint16_t vendor = (address >> 8) & 0xFFFF;
        uint8_t genre1 = (address >> 4) & 0x0F;
        uint8_t genre2 = address & 0x0F;
        uint16_t data = command & 0x3FF;
        uint8_t bytes[6];
        bytes[0] = vendor & 0xFF;
        bytes[1] = vendor >> 8;
        uint8_t parity = bytes[0] ^ bytes[1];
        parity = (parity & 0x0F) ^ (parity >> 4);
        bytes[2] = (genre1 << 4) | (parity & 0x0F);
        bytes[3] = ((data & 0x0F) << 4) | genre2;
        bytes[4] = ((id & 0x03) << 6) | ((data >> 4) & 0x3F);
        bytes[5] = bytes[2] ^ bytes[3] ^ bytes[4];
        uint64_t lsb_data = 0;
        for (uint8_t i = 0; i < 6; ++i) lsb_data |= (uint64_t)bytes[i] << (i * 8);
        irsend.sendPanasonic64(reverse_bits(lsb_data, 48), 48);
    } else {
        return false;
    }
    return true;
}

static void send_raw_signal(const ir_signal_entry_t &signal)
{
    irsend.enableIROut(signal.frequency, signal.duty_cycle);
    for (uint16_t i = 0; i < signal.raw_count; ++i) {
        if ((i & 1U) == 0) irsend.mark((uint16_t)loaded_raw[i]);
        else irsend.space(loaded_raw[i]);
    }
    irsend.space(0);
}

#endif

static void update_selected_row_styles()
{
    if (!showing_signals) return;
    for (uint8_t i = 0; i < signal_count; ++i) {
        if (!item_rows[i]) continue;
        bool selected = selected_signal == (int8_t)i;
        lv_obj_set_style_border_width(item_rows[i], selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(item_rows[i],
                                      selected ? UI_COLOR_ACCENT : UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_bg_color(item_rows[i],
                                  selected ? UI_COLOR_TRACK : UI_COLOR_CARD_BG, 0);
    }
}

static void signal_row_clicked_cb(lv_event_t *event)
{
    uintptr_t token = (uintptr_t)lv_event_get_user_data(event);
    LILYGO_LOG_I("IR_SEND_DBG signal click token=%lu signals=%u page_y=%ld",
                 (unsigned long)token, signal_count,
                 page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L);
    if (token == 0 || token > signal_count) {
        LILYGO_LOG_E("IR_SEND_DBG signal click rejected token=%lu signals=%u",
                     (unsigned long)token, signal_count);
        return;
    }
    uint8_t index = token - 1;
    clear_loaded_signal();
    selected_signal = index;
    ir_signal_entry_t &signal = signal_entries[index];

    bool load_ok = true;
#if defined(ARDUINO) && FACTORY_HAS_SD
    if (signal.type == IR_SIGNAL_BINARY_RAW || signal.type == IR_SIGNAL_TEXT_RAW) {
        load_ok = load_raw_signal(signal);
    } else {
        load_ok = protocol_supported(signal.protocol);
    }
#else
    load_ok = false;
#endif

    if (!load_ok) {
        LILYGO_LOG_E("IR_SEND_DBG signal select failed index=%u name=%s type=%d protocol=%s",
                     index, signal.name, (int)signal.type,
                     signal.protocol[0] ? signal.protocol : "<none>");
        selected_signal = -1;
        set_status(signal.type == IR_SIGNAL_TEXT_PARSED ?
                   "Unsupported parsed protocol" : "Invalid raw IR data",
                   lv_color_hex(0xFF4444));
        update_selected_row_styles();
        return;
    }

    lv_label_set_text(selected_name_label, signal.name);
    if (signal.type == IR_SIGNAL_TEXT_PARSED) {
        lv_label_set_text_fmt(selected_meta_label, "%s | A %lX | C %lX", signal.protocol,
                              (unsigned long)signal.address, (unsigned long)signal.command);
    } else {
        lv_label_set_text_fmt(selected_meta_label, "RAW | %lu.%lu kHz | %u samples",
                              (unsigned long)(signal.frequency / 1000),
                              (unsigned long)((signal.frequency % 1000) / 100), signal.raw_count);
    }
    lv_obj_remove_state(send_selected_btn, LV_STATE_DISABLED);
    update_selected_row_styles();
    set_status("Signal loaded", UI_COLOR_ACCENT);
    LILYGO_LOG_I("IR_SEND_DBG signal selected index=%u name=%s type=%d path=%s",
                 index, signal.name, (int)signal.type, active_file_path);
    LILYGO_LOG_ONLY(log_ui_state("signal-selected"));
    hw_feedback();
}

static void build_signal_rows()
{
    LILYGO_LOG_I("IR_SEND_DBG signal view build begin path=%s signals=%u page_y=%ld",
                 active_file_path, signal_count,
                 page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L);
    clear_item_list();
    clear_loaded_signal();
    showing_signals = true;
    lv_label_set_text(list_title_label, path_basename(active_file_path));
    lv_obj_remove_flag(list_back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(list_refresh_btn, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < signal_count; ++i) {
        ir_signal_entry_t &signal = signal_entries[i];
        lv_obj_t *row = lv_btn_create(item_list);
        item_rows[i] = row;
        style_list_row(row);
        char detail[64];
        if (signal.type == IR_SIGNAL_TEXT_PARSED) {
            snprintf(detail, sizeof(detail), "%s  A:%lX  C:%lX", signal.protocol,
                     (unsigned long)signal.address, (unsigned long)signal.command);
        } else {
            snprintf(detail, sizeof(detail), "RAW  %lu.%lu kHz  %u samples",
                     (unsigned long)(signal.frequency / 1000),
                     (unsigned long)((signal.frequency % 1000) / 100), signal.raw_count);
        }
        add_row_contents(row, LV_SYMBOL_PLAY, signal.name, detail);
        lv_obj_add_event_cb(row, signal_row_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i + 1));
#if LILYGO_DEBUG_ENABLED
        lv_obj_add_event_cb(row, signal_row_focus_log_cb, LV_EVENT_ALL,
                            (void *)(uintptr_t)(i + 1));
#endif
    }
    LILYGO_LOG_ONLY(log_ui_state("signal-view-built"));
}

static void file_row_clicked_cb(lv_event_t *event)
{
    uintptr_t token = (uintptr_t)lv_event_get_user_data(event);
    LILYGO_LOG_I("IR_SEND_DBG file click token=%lu files=%u view=%s page_y=%ld",
                 (unsigned long)token, file_count,
                 showing_signals ? "signals" : "files",
                 page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L);
    if (token == 0 || token > file_count) {
        LILYGO_LOG_E("IR_SEND_DBG file click rejected token=%lu files=%u",
                     (unsigned long)token, file_count);
        return;
    }
    uint8_t index = token - 1;
#if defined(ARDUINO) && FACTORY_HAS_SD
    snprintf(active_file_path, sizeof(active_file_path), "%s", file_entries[index].path);
    LILYGO_LOG_I("IR_SEND_DBG file selected index=%u path=%s size=%lu",
                 index, active_file_path, (unsigned long)file_entries[index].size);
    if (!parse_signal_file(active_file_path)) {
        LILYGO_LOG_E("IR_SEND_DBG file parse failed index=%u path=%s",
                     index, active_file_path);
        set_status("Unsupported or empty IR file", lv_color_hex(0xFF4444));
        return;
    }
    LILYGO_LOG_I("IR_SEND_DBG file parse success index=%u path=%s signals=%u",
                 index, active_file_path, signal_count);
    build_signal_rows();
    set_status("Choose a signal", UI_COLOR_TEXT_SECONDARY);
    LILYGO_LOG_ONLY(log_ui_state("file-click-complete"));
    hw_feedback();
#else
    (void)index;
#endif
}

static void build_file_rows()
{
    LILYGO_LOG_ONLY(log_ui_state("file-view-build-begin"));
    clear_item_list();
    clear_loaded_signal();
    showing_signals = false;
    active_file_path[0] = '\0';
    lv_label_set_text(list_title_label, "IR FILES");
    lv_obj_add_flag(list_back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(list_refresh_btn, LV_OBJ_FLAG_HIDDEN);
    file_count = 0;
    memset(file_entries, 0, sizeof(file_entries));

#if defined(ARDUINO) && FACTORY_HAS_SD
    if (!hw_is_sd_insert()) {
        LILYGO_LOG_W("IR_SEND_DBG file view SD not detected");
        create_empty_state(LV_SYMBOL_SD_CARD "  Insert an SD card, then refresh");
        set_status("SD card not detected", UI_COLOR_WARNING);
        return;
    }
    scan_directory("/ir", 0);
    scan_directory("/infrared", 0);
#endif

    if (file_count == 0) {
        LILYGO_LOG_W("IR_SEND_DBG file view no files found");
        create_empty_state("No .ir files in /ir or /infrared");
        set_status("No IR files found", UI_COLOR_WARNING);
        return;
    }

    for (uint8_t i = 0; i < file_count; ++i) {
        lv_obj_t *row = lv_btn_create(item_list);
        item_rows[i] = row;
        style_list_row(row);
        char detail[48];
        snprintf(detail, sizeof(detail), "%lu bytes", (unsigned long)file_entries[i].size);
        add_row_contents(row, LV_SYMBOL_FILE, path_basename(file_entries[i].path), detail);
        lv_obj_add_event_cb(row, file_row_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i + 1));
#if LILYGO_DEBUG_ENABLED
        lv_obj_add_event_cb(row, file_row_focus_log_cb, LV_EVENT_ALL,
                            (void *)(uintptr_t)(i + 1));
#endif
    }
    set_status("Choose an IR file", UI_COLOR_TEXT_SECONDARY);
    LILYGO_LOG_ONLY(log_ui_state("file-view-built"));
}

static void factory_send_cb(lv_event_t *)
{
    hw_feedback();
    hw_ir_function_select(true);
    hw_set_remote_code(FACTORY_NEC_CODE);
    tx_count++;
    update_tx_count();
    set_status("Factory NEC code sent", UI_COLOR_ACCENT);
    LILYGO_LOG_PRINTF("IR Send factory NEC: 0x%08lX\n", (unsigned long)FACTORY_NEC_CODE);
}

static void selected_send_cb(lv_event_t *)
{
    if (selected_signal < 0 || selected_signal >= signal_count) {
        set_status("Load a signal first", lv_color_hex(0xFF4444));
        return;
    }
#if defined(ARDUINO) && FACTORY_HAS_SD
    ir_signal_entry_t &signal = signal_entries[selected_signal];
    hw_feedback();
    hw_ir_function_select(true);
    bool sent = true;
    if (signal.type == IR_SIGNAL_TEXT_PARSED) sent = send_parsed_signal(signal);
    else if (loaded_raw) send_raw_signal(signal);
    else sent = false;
    if (!sent) {
        set_status("Signal send failed", lv_color_hex(0xFF4444));
        return;
    }
    tx_count++;
    update_tx_count();
    set_status("IR signal sent", UI_COLOR_ACCENT);
    LILYGO_LOG_PRINTF("IR Send: file=%s signal=%s\n", active_file_path, signal.name);
#else
    set_status("SD playback is unavailable", lv_color_hex(0xFF4444));
#endif
}

static void list_back_cb(lv_event_t *)
{
    LILYGO_LOG_I("IR_SEND_DBG list back clicked");
    hw_feedback();
    build_file_rows();
}

static void refresh_files_cb(lv_event_t *)
{
    LILYGO_LOG_I("IR_SEND_DBG file refresh clicked");
    hw_feedback();
    build_file_rows();
}

static void update_mode_styles()
{
    if (factory_mode_btn) {
        bool active = current_mode == IR_SEND_MODE_FACTORY;
        lv_obj_set_style_bg_color(factory_mode_btn, active ? UI_COLOR_ACCENT : UI_COLOR_TRACK, 0);
        lv_obj_set_style_border_width(factory_mode_btn, active ? 0 : 1, 0);
        lv_obj_set_style_border_color(factory_mode_btn, UI_COLOR_DIVIDER, 0);
    }
    if (sd_mode_btn) {
        bool active = current_mode == IR_SEND_MODE_SD;
        lv_obj_set_style_bg_color(sd_mode_btn, active ? UI_COLOR_ACCENT : UI_COLOR_TRACK, 0);
        lv_obj_set_style_border_width(sd_mode_btn, active ? 0 : 1, 0);
        lv_obj_set_style_border_color(sd_mode_btn, UI_COLOR_DIVIDER, 0);
    }
}

static void set_mode(ir_send_mode_t mode)
{
    LILYGO_LOG_I("IR_SEND_DBG set mode from=%s to=%s page_y=%ld",
                 current_mode == IR_SEND_MODE_SD ? "sd" : "factory",
                 mode == IR_SEND_MODE_SD ? "sd" : "factory",
                 page_container ? (long)lv_obj_get_scroll_y(page_container) : -1L);
    current_mode = mode;
    if (factory_view) {
        if (mode == IR_SEND_MODE_FACTORY) lv_obj_remove_flag(factory_view, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(factory_view, LV_OBJ_FLAG_HIDDEN);
    }
    if (sd_view) {
        if (mode == IR_SEND_MODE_SD) lv_obj_remove_flag(sd_view, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(sd_view, LV_OBJ_FLAG_HIDDEN);
    }
    update_mode_styles();
    if (page_container) lv_obj_scroll_to_y(page_container, 0, LV_ANIM_OFF);
    if (mode == IR_SEND_MODE_FACTORY) {
        set_status("Ready to send factory code", UI_COLOR_ACCENT);
    } else if (file_count == 0) {
        set_status("No IR files found", UI_COLOR_WARNING);
    } else if (selected_signal >= 0) {
        set_status("Signal loaded", UI_COLOR_ACCENT);
    } else {
        set_status(showing_signals ? "Choose a signal" : "Choose an IR file",
                   UI_COLOR_TEXT_SECONDARY);
    }
    LILYGO_LOG_ONLY(log_ui_state("mode-set-complete"));
}

static void mode_button_cb(lv_event_t *event)
{
    LILYGO_LOG_I("IR_SEND_DBG mode button clicked requested=%lu",
                 (unsigned long)(uintptr_t)lv_event_get_user_data(event));
    hw_feedback();
    set_mode((ir_send_mode_t)(uintptr_t)lv_event_get_user_data(event));
}

static void create_status_panel()
{
    lv_obj_t *panel = lv_obj_create(page_container);
    lv_obj_set_size(panel, LV_PCT(100), compact_layout() ? 40 : 48);
    lv_obj_set_style_bg_color(panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_pad_hor(panel, 10, 0);
    lv_obj_set_style_pad_ver(panel, 6, 0);
    lv_obj_set_style_pad_column(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    status_dot = lv_obj_create(panel);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_set_style_pad_all(status_dot, 0, 0);

    status_label = lv_label_create(panel);
    lv_label_set_text(status_label, "Ready to transmit");
    lv_obj_set_width(status_label, 1);
    lv_obj_set_flex_grow(status_label, 1);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    tx_count_label = lv_label_create(panel);
    lv_label_set_text(tx_count_label, "TX 0");
    lv_obj_set_style_text_color(tx_count_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(tx_count_label, &lv_font_montserrat_12, 0);
}

static lv_obj_t *create_mode_button(lv_obj_t *parent, const char *text, ir_send_mode_t mode)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_width(button, 1);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(button);
    lv_obj_add_event_cb(button, mode_button_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);
    return button;
}

static void create_mode_selector()
{
#if FACTORY_HAS_SD
    lv_obj_t *selector = lv_obj_create(page_container);
    lv_obj_set_size(selector, LV_PCT(100), 34);
    lv_obj_set_style_bg_color(selector, UI_COLOR_TRACK, 0);
    lv_obj_set_style_bg_opa(selector, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(selector, 0, 0);
    lv_obj_set_style_radius(selector, 6, 0);
    lv_obj_set_style_pad_all(selector, 2, 0);
    lv_obj_set_style_pad_column(selector, 2, 0);
    lv_obj_set_flex_flow(selector, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(selector, LV_OBJ_FLAG_SCROLLABLE);
    factory_mode_btn = create_mode_button(selector, "Factory", IR_SEND_MODE_FACTORY);
    sd_mode_btn = create_mode_button(selector, "SD Card", IR_SEND_MODE_SD);
#endif
}

static void create_factory_view()
{
    factory_view = create_plain_container(page_container);
    lv_obj_set_size(factory_view, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(factory_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(factory_view, 8, 0);

    lv_obj_t *preset = lv_obj_create(factory_view);
    lv_obj_set_size(preset, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(preset, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(preset, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(preset, 1, 0);
    lv_obj_set_style_border_color(preset, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(preset, 6, 0);
    lv_obj_set_style_pad_all(preset, compact_layout() ? 10 : 14, 0);
    lv_obj_set_style_pad_row(preset, 3, 0);
    lv_obj_set_flex_flow(preset, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(preset, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow = lv_label_create(preset);
    lv_label_set_text(eyebrow, "FACTORY PRESET");
    lv_obj_set_style_text_color(eyebrow, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_10, 0);

    lv_obj_t *code = lv_label_create(preset);
    lv_label_set_text_fmt(code, "0x%08lX", (unsigned long)FACTORY_NEC_CODE);
    lv_obj_set_style_text_color(code, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(code,
                               compact_layout() ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0);

    lv_obj_t *meta = lv_label_create(preset);
    lv_label_set_text(meta, "NEC | 32-bit | fixed test code");
    lv_obj_set_style_text_color(meta, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_10, 0);

    create_action_button(factory_view, LV_SYMBOL_PLAY "  Send test code", factory_send_cb);
}

static void create_sd_view()
{
#if FACTORY_HAS_SD
    sd_view = create_plain_container(page_container);
    lv_obj_set_size(sd_view, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sd_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sd_view, 8, 0);

    lv_obj_t *selected = lv_obj_create(sd_view);
    lv_obj_set_size(selected, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(selected, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(selected, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(selected, 1, 0);
    lv_obj_set_style_border_color(selected, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(selected, 6, 0);
    lv_obj_set_style_pad_all(selected, 10, 0);
    lv_obj_set_style_pad_row(selected, 3, 0);
    lv_obj_set_flex_flow(selected, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(selected, LV_OBJ_FLAG_SCROLLABLE);

    selected_name_label = lv_label_create(selected);
    lv_label_set_text(selected_name_label, "Choose a signal");
    lv_obj_set_width(selected_name_label, LV_PCT(100));
    lv_label_set_long_mode(selected_name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(selected_name_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(selected_name_label, &lv_font_montserrat_14, 0);

    selected_meta_label = lv_label_create(selected);
    lv_label_set_text(selected_meta_label, "Select a file, then a signal");
    lv_obj_set_width(selected_meta_label, LV_PCT(100));
    lv_label_set_long_mode(selected_meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(selected_meta_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(selected_meta_label, &lv_font_montserrat_10, 0);

    send_selected_btn = create_action_button(sd_view, LV_SYMBOL_PLAY "  Send selected", selected_send_cb);
    lv_obj_add_state(send_selected_btn, LV_STATE_DISABLED);

    lv_obj_t *toolbar = create_plain_container(sd_view);
    lv_obj_set_size(toolbar, LV_PCT(100), 30);
    lv_obj_set_style_pad_column(toolbar, 6, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    list_back_btn = lv_btn_create(toolbar);
    lv_obj_set_size(list_back_btn, 30, 28);
    lv_obj_add_flag(list_back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(list_back_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_color(list_back_btn, UI_COLOR_TRACK, 0);
    lv_obj_set_style_border_width(list_back_btn, 1, 0);
    lv_obj_set_style_border_color(list_back_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(list_back_btn, 6, 0);
    lv_obj_set_style_pad_all(list_back_btn, 0, 0);
    ui_add_accent_focus_style(list_back_btn);
    lv_obj_add_event_cb(list_back_btn, list_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_icon = lv_label_create(list_back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(back_icon);

    list_title_label = lv_label_create(toolbar);
    lv_label_set_text(list_title_label, "IR FILES");
    lv_obj_set_width(list_title_label, 1);
    lv_obj_set_flex_grow(list_title_label, 1);
    lv_label_set_long_mode(list_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(list_title_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(list_title_label, &lv_font_montserrat_10, 0);

    list_refresh_btn = lv_btn_create(toolbar);
    lv_obj_set_size(list_refresh_btn, 30, 28);
    lv_obj_add_flag(list_refresh_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_color(list_refresh_btn, UI_COLOR_TRACK, 0);
    lv_obj_set_style_border_width(list_refresh_btn, 1, 0);
    lv_obj_set_style_border_color(list_refresh_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(list_refresh_btn, 6, 0);
    lv_obj_set_style_pad_all(list_refresh_btn, 0, 0);
    ui_add_accent_focus_style(list_refresh_btn);
    lv_obj_add_event_cb(list_refresh_btn, refresh_files_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_icon = lv_label_create(list_refresh_btn);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(refresh_icon, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(refresh_icon);

    item_list = create_plain_container(sd_view);
    lv_obj_set_size(item_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(item_list, 6, 0);

    build_file_rows();
    lv_obj_add_flag(sd_view, LV_OBJ_FLAG_HIDDEN);
#endif
}

static void cleanup_page()
{
    LILYGO_LOG_ONLY(log_ui_state("cleanup-begin"));
    clear_loaded_signal();
    if (ir_send_active) {
        hw_ir_function_select(false);
        ir_send_active = false;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    status_dot = NULL;
    status_label = NULL;
    tx_count_label = NULL;
    factory_mode_btn = NULL;
    sd_mode_btn = NULL;
    factory_view = NULL;
    sd_view = NULL;
    list_title_label = NULL;
    list_back_btn = NULL;
    list_refresh_btn = NULL;
    item_list = NULL;
    selected_name_label = NULL;
    selected_meta_label = NULL;
    send_selected_btn = NULL;
    memset(item_rows, 0, sizeof(item_rows));
    file_count = 0;
    signal_count = 0;
}

static void back_event_handler(lv_event_t *)
{
    LILYGO_LOG_I("IR_SEND_DBG app back clicked");
    cleanup_page();
    menu_show();
}

void ui_ir_remote_enter(lv_obj_t *parent)
{
    LILYGO_LOG_I("IR_SEND_DBG app enter debug=%d", LILYGO_DEBUG_ENABLED);
    tx_count = 0;
    current_mode = IR_SEND_MODE_FACTORY;
    hw_ir_function_select(true);
    ir_send_active = true;

    page_container = ui_create_app_page(parent, "IR Send", back_event_handler);
    lv_obj_set_scroll_dir(page_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page_container, LV_SCROLLBAR_MODE_AUTO);
#if LILYGO_DEBUG_ENABLED
    lv_obj_add_event_cb(page_container, page_scroll_log_cb, LV_EVENT_ALL, NULL);
#endif
    create_status_panel();
    create_mode_selector();
    create_factory_view();
    create_sd_view();
    set_mode(IR_SEND_MODE_FACTORY);
    update_tx_count();
    set_status("Ready to transmit", UI_COLOR_ACCENT);
    LILYGO_LOG_ONLY(log_ui_state("app-enter-complete"));
}

void ui_ir_remote_exit(lv_obj_t *)
{
    LILYGO_LOG_I("IR_SEND_DBG app exit");
    cleanup_page();
}

app_t ui_ir_remote_main = {
    .setup_func_cb = ui_ir_remote_enter,
    .exit_func_cb = ui_ir_remote_exit,
    .user_data = nullptr,
};

#endif
