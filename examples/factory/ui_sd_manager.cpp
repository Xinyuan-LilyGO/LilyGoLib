/**
 * @file      ui_sd_manager.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-03
 * 
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_SD_MANAGER)

#include <stdlib.h>
#include <strings.h>

#ifdef ARDUINO
#include <SD.h>
#include <esp_heap_caps.h>
#endif

#define SDM_MAX_ITEMS 32
#define SDM_MOUNT_FREQ_MAX 8

typedef struct {
    char path[96];
    bool is_dir;
    bool is_parent;
    uint64_t size;
} sd_manager_item_t;

static lv_obj_t *page_container = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *capacity_label = NULL;
static lv_obj_t *used_label = NULL;
static lv_obj_t *item_count_label = NULL;
static lv_obj_t *path_label = NULL;
static lv_obj_t *selected_label = NULL;
static lv_obj_t *view_btn = NULL;
static lv_obj_t *delete_btn = NULL;
static lv_obj_t *file_list_card = NULL;
static lv_obj_t *viewer_box = NULL;
static lv_obj_t *file_rows[SDM_MAX_ITEMS];
static sd_manager_item_t *file_items = NULL;
static char current_dir[96] = "/";
static uint8_t file_count = 0;
static uint8_t root_item_count = 0;
static int8_t selected_index = -1;
static bool viewer_scroll_mode = false;

static void build_file_list(void);
static bool is_viewable_file(const char *path);
static void view_btn_cb(lv_event_t *e);

static void *sdm_alloc(size_t size)
{
#ifdef ARDUINO
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return ptr;
#else
    return malloc(size);
#endif
}

static void sdm_free(void *ptr)
{
    if (!ptr) return;
#ifdef ARDUINO
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

static bool alloc_file_items(void)
{
    if (file_items) return true;
    file_items = (sd_manager_item_t *)sdm_alloc(SDM_MAX_ITEMS * sizeof(sd_manager_item_t));
    if (!file_items) return false;
    memset(file_items, 0, SDM_MAX_ITEMS * sizeof(sd_manager_item_t));
    return true;
}

static void free_file_items(void)
{
    sdm_free(file_items);
    file_items = NULL;
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static bool sd_ready(void)
{
#ifdef ARDUINO
    return hw_is_sd_insert() && hw_get_sd_size() > 0.0f;
#else
    return false;
#endif
}

static bool mount_sd_best_effort(void)
{
#ifdef ARDUINO
    uint32_t freqs[SDM_MOUNT_FREQ_MAX];
    uint8_t freq_count = hw_get_sd_mount_freq_list(freqs, SDM_MOUNT_FREQ_MAX);

    if (!hw_has_sd_detect_pin()) {
        uint32_t lowest_freq = 0;
        for (uint8_t i = 0; i < freq_count; ++i) {
            if (freqs[i] && (lowest_freq == 0 || freqs[i] < lowest_freq)) {
                lowest_freq = freqs[i];
            }
        }
        if (!lowest_freq) {
            return false;
        }
        LILYGO_LOG_PRINTF("[SD Manager] no detect pin, probe once at lowest freq:%lu\n", (unsigned long)lowest_freq);
        return hw_mount_sd(lowest_freq) && sd_ready();
    }

    for (uint8_t i = 0; i < freq_count; ++i) {
        if (hw_mount_sd(freqs[i]) && sd_ready()) {
            return true;
        }
    }
#endif
    return false;
}

static const char *base_name(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static const char *display_name(const sd_manager_item_t *item)
{
    if (!item) return "";
    if (item->is_parent) return "..";
    const char *name = base_name(item->path);
    return name[0] ? name : item->path;
}

static bool is_root_dir(void)
{
    return strcmp(current_dir, "/") == 0;
}

static bool join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    if (!out || !name || out_size == 0) return false;
    int written;
    const char *base = base_name(name);
    if (!dir || strcmp(dir, "/") == 0) {
        written = snprintf(out, out_size, "/%s", base);
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, base);
    }
    return written > 0 && written < (int)out_size;
}

static bool go_parent_dir(void)
{
    if (is_root_dir()) return false;
    char tmp[sizeof(current_dir)];
    snprintf(tmp, sizeof(tmp), "%s", current_dir);

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = 0;
    }
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        snprintf(current_dir, sizeof(current_dir), "/");
    } else {
        *slash = 0;
        snprintf(current_dir, sizeof(current_dir), "%s", tmp);
    }
    return true;
}

static void format_size(char *out, size_t out_size, uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(out, out_size, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
    }
}

static bool ensure_sd(void)
{
#ifdef ARDUINO
    if (hw_has_sd_detect_pin() && !hw_is_sd_card_inserted()) {
        if (hw_is_sd_insert()) {
            LILYGO_LOG_PRINTLN("[SD Manager] no card detected, clear cached SD state");
            hw_unmount_sd();
        }
        set_status("No SD card detected.", lv_color_hex(0xFF4444));
        return false;
    }
    if (sd_ready()) {
        return true;
    }
    set_status("Mounting SD card...", UI_COLOR_TEXT_SECONDARY);
    lv_refr_now(NULL);
    return mount_sd_best_effort();
#else
    return false;
#endif
}

static void add_to_default_group(lv_obj_t *obj)
{
    lv_group_t *group = lv_group_get_default();
    if (group && obj && lv_obj_get_group(obj) != group) {
        lv_group_add_obj(group, obj);
    }
}

static lv_indev_type_t event_indev_type(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        indev = lv_indev_active();
    }
    return indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
}

static bool event_from_encoder_or_keypad(lv_event_t *e)
{
    lv_indev_type_t type = event_indev_type(e);
    return type == LV_INDEV_TYPE_ENCODER || type == LV_INDEV_TYPE_KEYPAD;
}

static void update_action_buttons(void)
{
    bool has_file = file_items && selected_index >= 0 && !file_items[selected_index].is_dir;
    if (delete_btn) {
        if (has_file) {
            lv_obj_clear_state(delete_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(delete_btn, LV_STATE_DISABLED);
        }
    }

    if (view_btn) {
        if (has_file && is_viewable_file(file_items[selected_index].path)) {
            lv_obj_clear_state(view_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(view_btn, LV_STATE_DISABLED);
        }
    }
}

static void clear_selection(void)
{
    selected_index = -1;
    if (selected_label) lv_label_set_text(selected_label, "--");
    update_action_buttons();
}

static void update_card_info(void)
{
#ifdef ARDUINO
    if (!ensure_sd()) {
        if (capacity_label) lv_label_set_text(capacity_label, "--");
        if (used_label) lv_label_set_text(used_label, "--");
        if (item_count_label) lv_label_set_text(item_count_label, "0");
        if (path_label) lv_label_set_text(path_label, "--");
        set_status("SD card is not available.", lv_color_hex(0xFF4444));
        return;
    }

    char buf[48];
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();

    format_size(buf, sizeof(buf), total);
    if (capacity_label) lv_label_set_text(capacity_label, buf);

    char used_buf[32];
    char total_buf[32];
    format_size(used_buf, sizeof(used_buf), used);
    format_size(total_buf, sizeof(total_buf), total > used ? total - used : 0);
    snprintf(buf, sizeof(buf), "%s / %s free", used_buf, total_buf);
    if (used_label) lv_label_set_text(used_label, buf);

    if (item_count_label) lv_label_set_text_fmt(item_count_label, "%u", root_item_count);
    if (path_label) lv_label_set_text(path_label, current_dir);
    set_status("Mounted.", UI_COLOR_ACCENT);
#else
    set_status("Not available.", lv_color_hex(0xFF4444));
#endif
}

static void clear_file_rows(void)
{
    for (uint8_t i = 0; i < SDM_MAX_ITEMS; ++i) {
        if (file_rows[i]) {
            lv_obj_delete(file_rows[i]);
            file_rows[i] = NULL;
        }
    }
    file_count = 0;
    root_item_count = 0;
}

static void add_empty_row(const char *text)
{
    if (!file_list_card || SDM_MAX_ITEMS == 0) return;
    lv_obj_t *lbl = lv_label_create(file_list_card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    file_rows[0] = lbl;
    file_count = 1;
}

static int8_t find_file_item_index(sd_manager_item_t *item)
{
    if (!file_items || !item) return -1;
    for (uint8_t i = 0; i < file_count; ++i) {
        if (&file_items[i] == item) return (int8_t)i;
    }
    return -1;
}

static void update_row_selection_styles(void)
{
    for (uint8_t i = 0; i < file_count; ++i) {
        if (!file_rows[i]) continue;
        if (selected_index == (int8_t)i) {
            lv_obj_add_state(file_rows[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(file_rows[i], LV_STATE_CHECKED);
        }
    }
}

static void select_file_item(sd_manager_item_t *item)
{
    int8_t index = find_file_item_index(item);
    if (index < 0) return;

    selected_index = index;
    if (selected_label) {
        lv_label_set_text(selected_label, item->is_parent ? ".." : item->path);
    }
    update_row_selection_styles();
    update_action_buttons();
}

static void activate_file_item(sd_manager_item_t *item, bool preview_file)
{
    if (!item) return;

    select_file_item(item);

    if (item->is_parent) {
        if (go_parent_dir()) {
            build_file_list();
        }
        return;
    }

    if (item->is_dir) {
        snprintf(current_dir, sizeof(current_dir), "%s", item->path);
        build_file_list();
        return;
    }

    if (preview_file && is_viewable_file(item->path)) {
        view_btn_cb(NULL);
    }
}

static void file_row_cb(lv_event_t *e)
{
    sd_manager_item_t *item = (sd_manager_item_t *)lv_event_get_user_data(e);
    if (!item) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *row = lv_event_get_target_obj(e);

    if (code == LV_EVENT_FOCUSED) {
        select_file_item(item);
        if (row) lv_obj_scroll_to_view_recursive(row, LV_ANIM_ON);
    } else if (code == LV_EVENT_CLICKED) {
        activate_file_item(item, event_from_encoder_or_keypad(e));
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_file_item(item, true);
            lv_event_stop_processing(e);
        }
    }
}

static void create_file_row(sd_manager_item_t *item, uint8_t index)
{
    lv_obj_t *row = lv_obj_create(file_list_card);
    file_rows[index] = row;

    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(row, LV_OPA_70, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD_FOCUS, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(row, UI_COLOR_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_CHECKED);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);
    lv_obj_set_style_pad_left(row, 0, 0);
    lv_obj_set_style_pad_right(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, file_row_cb, LV_EVENT_ALL, item);
    add_to_default_group(row);

    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, item->is_parent ? LV_SYMBOL_UP : (item->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE));
    lv_obj_set_style_text_color(icon, item->is_dir ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, display_name(item));
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 1);
    lv_obj_set_flex_grow(name, 1);

    char size_buf[24];
    if (item->is_dir) {
        snprintf(size_buf, sizeof(size_buf), "DIR");
    } else {
        format_size(size_buf, sizeof(size_buf), item->size);
    }
    lv_obj_t *size = lv_label_create(row);
    lv_label_set_text(size, size_buf);
    lv_obj_set_style_text_color(size, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(size, &lv_font_montserrat_12, 0);
}

static void build_file_list(void)
{
    clear_file_rows();
    clear_selection();

    if (!file_list_card) return;
    if (!alloc_file_items()) {
        add_empty_row("No RAM for file list");
        update_card_info();
        return;
    }
    if (!ensure_sd()) {
        add_empty_row("No SD card detected");
        update_card_info();
        return;
    }

#ifdef ARDUINO
    File root = SD.open(current_dir);
    if (!root || !root.isDirectory()) {
        add_empty_row("Cannot open directory");
        if (root) root.close();
        update_card_info();
        return;
    }

    if (!is_root_dir() && file_count < SDM_MAX_ITEMS) {
        sd_manager_item_t *item = &file_items[file_count];
        memset(item, 0, sizeof(*item));
        snprintf(item->path, sizeof(item->path), "..");
        item->is_dir = true;
        item->is_parent = true;
        create_file_row(item, file_count);
        file_count++;
    }

    uint8_t visible_count = 0;
    File file = root.openNextFile();
    while (file && file_count < SDM_MAX_ITEMS) {
        sd_manager_item_t *item = &file_items[file_count];
        memset(item, 0, sizeof(*item));
        if (join_path(item->path, sizeof(item->path), current_dir, file.name())) {
            item->is_dir = file.isDirectory();
            item->size = item->is_dir ? 0 : file.size();
            create_file_row(item, file_count);
            file_count++;
            root_item_count++;
            visible_count++;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    if (visible_count == 0 && is_root_dir()) {
        add_empty_row("Root is empty");
    } else if (visible_count == 0 && file_count == 1) {
        set_status("Directory is empty.", UI_COLOR_TEXT_SECONDARY);
    }
#endif

    update_card_info();
}

static bool has_ext(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    return strcasecmp(path + plen - elen, ext) == 0;
}

static bool is_viewable_file(const char *path)
{
    static const char *exts[] = {
        ".txt", ".log", ".csv", ".json", ".cfg", ".ini", ".md",
        ".duck", ".ducky", ".gpx", ".kml", ".ir", ".nmea"
    };
    for (uint8_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (has_ext(path, exts[i])) return true;
    }
    return false;
}

static uint16_t clean_dir_files(const char *dir, const char *ext)
{
    uint16_t removed = 0;
#ifdef ARDUINO
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            char path[96];
            const char *name = file.name();
            if (name[0] == '/') {
                snprintf(path, sizeof(path), "%s", name);
            } else {
                snprintf(path, sizeof(path), "%s/%s", dir, name);
            }
            if (!ext || has_ext(path, ext)) {
                file.close();
                if (SD.remove(path)) removed++;
                file = root.openNextFile();
                continue;
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
#endif
    return removed;
}

static void close_viewer_cb(lv_event_t *e)
{
    if (viewer_box) {
        lv_obj_delete(viewer_box);
        viewer_box = NULL;
    }
    viewer_scroll_mode = false;
}

static void viewer_scroll_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_group_t *group = (lv_group_t *)lv_obj_get_group(obj);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
    } else if (code == LV_EVENT_CLICKED && event_from_encoder_or_keypad(e)) {
        viewer_scroll_mode = !viewer_scroll_mode;
        if (group) {
            lv_group_set_editing(group, viewer_scroll_mode);
        }
    } else if (code == LV_EVENT_ROTARY && viewer_scroll_mode) {
        int32_t diff = lv_event_get_rotary_diff(e);
        lv_obj_scroll_to_y(obj, lv_obj_get_scroll_y(obj) + diff * 24, LV_ANIM_OFF);
        lv_event_stop_processing(e);
    } else if (code == LV_EVENT_KEY && viewer_scroll_mode) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
            lv_obj_scroll_to_y(obj, lv_obj_get_scroll_y(obj) + 24, LV_ANIM_OFF);
            lv_event_stop_processing(e);
        } else if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
            lv_obj_scroll_to_y(obj, lv_obj_get_scroll_y(obj) - 24, LV_ANIM_OFF);
            lv_event_stop_processing(e);
        } else if (key == LV_KEY_ESC || key == LV_KEY_ENTER) {
            viewer_scroll_mode = false;
            if (group) {
                lv_group_set_editing(group, false);
            }
            lv_event_stop_processing(e);
        }
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_DELETE) {
        viewer_scroll_mode = false;
        if (group) {
            lv_group_set_editing(group, false);
        }
    }
}

static void show_text_viewer(const char *path, const char *text, bool truncated)
{
    close_viewer_cb(NULL);

    viewer_box = lv_obj_create(lv_screen_active());
    lv_obj_set_size(viewer_box, LV_PCT(92), LV_PCT(82));
    lv_obj_align(viewer_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(viewer_box, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(viewer_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(viewer_box, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(viewer_box, 1, 0);
    lv_obj_set_style_radius(viewer_box, 8, 0);
    lv_obj_set_style_pad_all(viewer_box, 8, 0);
    lv_obj_set_flex_flow(viewer_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(viewer_box, 8, 0);

    lv_obj_t *header = lv_obj_create(viewer_box);
    lv_obj_set_size(header, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 8, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, base_name(path));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 1);
    lv_obj_set_flex_grow(title, 1);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 34, 28);
    lv_obj_set_style_bg_color(close_btn, UI_COLOR_WARNING, 0);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_add_event_cb(close_btn, close_viewer_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    add_to_default_group(close_btn);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);

    lv_obj_t *scroll = lv_obj_create(viewer_box);
    lv_obj_set_size(scroll, LV_PCT(100), 1);
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_radius(scroll, 4, 0);
    lv_obj_set_style_pad_all(scroll, 6, 0);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_border_width(scroll, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(scroll, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(scroll, LV_OPA_70, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(scroll, viewer_scroll_cb, LV_EVENT_ALL, NULL);
    add_to_default_group(scroll);

    lv_obj_t *body = lv_label_create(scroll);
    lv_label_set_text(body, text ? text : "");
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_color(body, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);

    if (truncated) {
        lv_obj_t *hint = lv_label_create(viewer_box);
        lv_label_set_text(hint, "Preview truncated to 4 KB.");
        lv_obj_set_style_text_color(hint, UI_COLOR_WARNING, 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    }

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_focus_obj(close_btn);
    }
}

static void view_btn_cb(lv_event_t *e)
{
    if (!file_items || selected_index < 0 || file_items[selected_index].is_dir) {
        set_status("Select a text file first.", lv_color_hex(0xFF4444));
        return;
    }
    if (!is_viewable_file(file_items[selected_index].path)) {
        set_status("File type is not previewed.", lv_color_hex(0xFF4444));
        return;
    }

#ifdef ARDUINO
    File file = SD.open(file_items[selected_index].path, FILE_READ);
    if (!file) {
        set_status("Open failed.", lv_color_hex(0xFF4444));
        return;
    }

    char *viewer_buffer = (char *)sdm_alloc(4096);
    if (!viewer_buffer) {
        file.close();
        set_status("No RAM for preview.", lv_color_hex(0xFF4444));
        return;
    }

    size_t read_len = file.readBytes(viewer_buffer, 4095);
    bool truncated = file.available() > 0;
    file.close();
    viewer_buffer[read_len] = 0;
    show_text_viewer(file_items[selected_index].path, viewer_buffer, truncated);
    sdm_free(viewer_buffer);
    set_status("Preview opened.", UI_COLOR_ACCENT);
#endif
}

static void refresh_btn_cb(lv_event_t *e)
{
    build_file_list();
}

static void delete_btn_cb(lv_event_t *e)
{
    if (!file_items || selected_index < 0 || file_items[selected_index].is_dir) {
        set_status("Select a file first.", lv_color_hex(0xFF4444));
        return;
    }

#ifdef ARDUINO
    char path[sizeof(file_items[0].path)];
    snprintf(path, sizeof(path), "%s", file_items[selected_index].path);
    if (SD.remove(path)) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Deleted %s", base_name(path));
        build_file_list();
        set_status(buf, UI_COLOR_WARNING);
    } else {
        set_status("Delete failed.", lv_color_hex(0xFF4444));
    }
#endif
}

static void clean_logs_cb(lv_event_t *e)
{
    if (!ensure_sd()) {
        set_status("SD card is not available.", lv_color_hex(0xFF4444));
        return;
    }

    uint16_t removed = 0;
    removed += clean_dir_files("/sensor_logs", ".csv");
    removed += clean_dir_files("/tracks", ".gpx");

    char buf[48];
    snprintf(buf, sizeof(buf), "Removed %u log files.", removed);
    build_file_list();
    set_status(buf, removed ? UI_COLOR_WARNING : UI_COLOR_TEXT_SECONDARY);
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                               const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    return row;
}

static lv_obj_t *create_action_button(lv_obj_t *card, const char *icon, const char *title,
                                      const char *btn_text, lv_color_t color, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(lv_obj_create(card));
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, btn_text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_center(lbl);

    ui_create_card_item(card, icon, title, btn);
    return btn;
}

static void back_event_handler(lv_event_t *e)
{
    close_viewer_cb(NULL);
    clear_file_rows();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    free_file_items();
    root_item_count = 0;
    status_label = NULL;
    capacity_label = NULL;
    used_label = NULL;
    item_count_label = NULL;
    path_label = NULL;
    selected_label = NULL;
    view_btn = NULL;
    delete_btn = NULL;
    file_list_card = NULL;
    selected_index = -1;
    menu_show();
}

void ui_sd_manager_enter(lv_obj_t *parent)
{
    alloc_file_items();
    snprintf(current_dir, sizeof(current_dir), "/");
    page_container = ui_create_app_page(parent, "SD Manager", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Storage");
    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Checking...");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    add_info_row(card, LV_SYMBOL_SD_CARD, "Capacity", "--", &capacity_label);
    add_info_row(card, LV_SYMBOL_SAVE, "Used", "--", &used_label);
    add_info_row(card, LV_SYMBOL_DIRECTORY, "Path", "/", &path_label);
    lv_obj_set_width(path_label, 150);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_DOT);
    add_info_row(card, LV_SYMBOL_LIST, "Items", "0", &item_count_label);

    card = ui_create_card(page_container, "Actions");
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Refresh", "Refresh", refresh_btn_cb);
    view_btn = create_action_button(card, LV_SYMBOL_EYE_OPEN, "Preview",
                                    LV_SYMBOL_EYE_OPEN " View", UI_COLOR_ACCENT, view_btn_cb);
    delete_btn = create_action_button(card, LV_SYMBOL_TRASH, "Delete",
                                      LV_SYMBOL_TRASH " Delete", UI_COLOR_WARNING, delete_btn_cb);
    ui_create_card_button(card, LV_SYMBOL_TRASH, "Logs", "Clean Logs", clean_logs_cb);

    card = ui_create_card(page_container, "Selected");
    add_info_row(card, LV_SYMBOL_FILE, "Path", "--", &selected_label);
    lv_obj_set_width(selected_label, 150);
    lv_label_set_long_mode(selected_label, LV_LABEL_LONG_DOT);

    file_list_card = ui_create_card(page_container, "Files");
    build_file_list();
    update_action_buttons();
}

void ui_sd_manager_exit(lv_obj_t *parent)
{
}

app_t ui_sd_manager_main = {
    .setup_func_cb = ui_sd_manager_enter,
    .exit_func_cb = ui_sd_manager_exit,
    .user_data = nullptr,
};

#endif
