/**
 * @file      ui_sd_manager.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-03
 *
 * Browses SD/FAT with full-screen storage settings and a translucent path overlay.
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#if !defined(EXCLUDE_SD_MANAGER)
#include <algorithm>
#include <vector>
#ifdef ARDUINO
#include <SD.h>
#if defined(USING_FATFS)
#include <FFat.h>
#endif
namespace {
struct Entry { String name; bool directory; size_t size; };
static std::vector<Entry> entries;
static String directory = "/", selected_path;
static bool selected_directory = false, use_fat = false;
static lv_obj_t *page, *list, *path_label, *path_bar, *storage, *notice, *settings_page;
static String status_text;
static lv_obj_t *overlay, *dialog, *name_input, *keyboard, *name_save;
static lv_group_t *dialog_group, *previous_group;
static void refresh();
static fs::FS &filesystem() {
#if defined(USING_FATFS)
    if (use_fat) return FFat;
#endif
    return SD;
}
static bool ready() {
#if defined(USING_FATFS)
    if (use_fat) return FFat.totalBytes() > 0;
#endif
    return hw_is_sd_insert() && hw_get_sd_size() > 0;
}
static void set_notice(const char *text) {
    status_text = text;
    if (notice) lv_label_set_text(notice, text);
}
static void message(const char *text) {
    set_notice(text);
    if (!notice) ui_msg_pop_up("Files", text);
}
static void focus_cb(lv_event_t *e) { lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(e), LV_ANIM_ON); }
static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
    auto *b = lv_button_create(parent);
    lv_obj_set_size(b, LV_PCT(100), 32);
    lv_obj_add_style(b, &ui_styles.accent_btn, 0);
    ui_add_accent_focus_style(b);
    lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(b, focus_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    auto *label = lv_label_create(b); lv_label_set_text(label, text); lv_obj_center(label);
    return b;
}
static void close_dialog(lv_event_t * = nullptr) {
    if (!overlay) return;
    ui_soft_keyboard_hide(keyboard, nullptr);
    if (keyboard) { lv_obj_delete(keyboard); keyboard = nullptr; }
    lv_group_set_default(previous_group);
    if (lv_indev_t *i = lv_get_encoder_indev()) lv_indev_set_group(i, previous_group);
    if (lv_indev_t *i = lv_get_keyboard_indev()) lv_indev_set_group(i, previous_group);
    lv_obj_delete(overlay); overlay = dialog = name_input = keyboard = name_save = nullptr;
    lv_group_delete(dialog_group); dialog_group = nullptr;
}
static void create_dialog(const char *title) {
    close_dialog();
    previous_group = lv_group_get_default(); dialog_group = lv_group_create();
    lv_group_set_default(dialog_group);
    if (lv_indev_t *i = lv_get_encoder_indev()) lv_indev_set_group(i, dialog_group);
    if (lv_indev_t *i = lv_get_keyboard_indev()) lv_indev_set_group(i, dialog_group);
    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, UI_COLOR_BG, 0); lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0); lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    dialog = lv_obj_create(overlay); lv_obj_set_size(dialog, LV_PCT(94), LV_PCT(94)); lv_obj_center(dialog);
    lv_obj_add_style(dialog, &ui_styles.card, 0); lv_obj_set_style_pad_all(dialog, 8, 0);
    lv_obj_set_style_pad_row(dialog, 6, 0); lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    auto *label = lv_label_create(dialog); lv_label_set_text(label, title);
    lv_obj_set_width(label, LV_PCT(100)); lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    button(dialog, "Cancel", close_dialog);
}
static void open_directory(lv_event_t *) {
    const String next = selected_path; close_dialog(); directory = next; refresh();
}
static bool remove_tree(const String &path, unsigned depth = 0) {
    if (depth > 16 || path == "/") return false;
    File node = filesystem().open(path);
    if (!node) return false;
    const bool dir = node.isDirectory(); node.close();
    if (!dir) return filesystem().remove(path);
    // Reopen between removals: directory iterators may invalidate after deletion.
    for (;;) {
        File root = filesystem().open(path); File child = root.openNextFile();
        if (!child) { root.close(); break; }
        String name = child.name();
        int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
        const String child_path = path + "/" + name;
        child.close(); root.close();
        if (name == "." || name == "..") return false;
        if (!remove_tree(child_path, depth + 1)) return false;
    }
    return filesystem().rmdir(path);
}
static void delete_confirmed(lv_event_t *) {
    const bool ok = ready() && remove_tree(selected_path);
    LILYGO_LOG_I("[Files] delete storage=%s success=%u", use_fat ? "FAT" : "SD", ok);
    close_dialog(); refresh(); message(ok ? "Deleted" : "Delete incomplete. Check storage and contents.");
}
static void delete_prompt(lv_event_t *) {
    create_dialog(selected_directory ? "Delete folder and ALL its contents?" : "Delete this file?");
    button(dialog, "Delete", delete_confirmed);
}
static bool valid_name(const char *name) {
    if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, "..") || strpbrk(name, "/\\:*?\"<>|")) return false;
    const size_t length = strlen(name);
    if (name[length - 1] == ' ' || name[length - 1] == '.') return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p) if (*p < 0x20) return false;
    return true;
}
static void rename_save(lv_event_t *) {
    const char *name = lv_textarea_get_text(name_input);
    if (!valid_name(name)) {
        ui_msg_pop_up("Invalid name", "Use a filename without path separators or reserved characters."); return;
    }
    String next = directory == "/" ? String("/") + name : directory + "/" + name;
    if (next != selected_path && filesystem().exists(next)) { ui_msg_pop_up("Name exists", "Choose another name."); return; }
    const bool ok = ready() && (next == selected_path || filesystem().rename(selected_path, next));
    LILYGO_LOG_I("[Files] rename storage=%s success=%u", use_fat ? "FAT" : "SD", ok);
    close_dialog(); refresh(); message(ok ? "Renamed" : "Rename failed. Check storage.");
}
static void keyboard_event(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_READY && lv_event_get_code(e) != LV_EVENT_CANCEL) return;
    ui_soft_keyboard_hide(keyboard, nullptr);
    lv_obj_set_size(dialog, LV_PCT(94), LV_PCT(94)); lv_obj_center(dialog);
    if (name_save) lv_group_focus_obj(name_save);
}
static void input_event(lv_event_t *e) {
    if ((lv_event_get_code(e) == LV_EVENT_CLICKED) && ui_soft_keyboard_should_open()) {
        ui_soft_keyboard_show(keyboard, name_input, nullptr, nullptr);
        lv_obj_update_layout(keyboard);
        int h = lv_obj_get_height(overlay) - lv_obj_get_height(keyboard) - 4;
        lv_obj_set_height(dialog, h); lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_scroll_to_view_recursive(name_input, LV_ANIM_OFF);
    }
}
static void rename_prompt(lv_event_t *) {
    create_dialog("Rename");
    name_input = lv_textarea_create(dialog); lv_obj_set_size(name_input, LV_PCT(100), 34);
    lv_textarea_set_one_line(name_input, true); lv_textarea_set_max_length(name_input, 127);
    lv_textarea_set_text(name_input, selected_path.substring(selected_path.lastIndexOf('/') + 1).c_str());
    ui_add_accent_focus_style(name_input);
    keyboard = lv_keyboard_create(overlay); lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(48));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0); lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_ALL, nullptr);
    lv_obj_add_event_cb(name_input, input_event, LV_EVENT_ALL, nullptr);
    name_save = button(dialog, "Save name", rename_save);
}
static void preview(lv_event_t *) {
    File file = filesystem().open(selected_path); String text;
    if (!file) { close_dialog(); message("Open failed"); return; }
    for (size_t i = 0; i < 4096 && file.available(); ++i) { char c = file.read(); text += c ? c : '.'; }
    bool more = file.available(); file.close(); create_dialog(more ? "Preview (first 4 KB)" : "Preview");
    auto *body = lv_textarea_create(dialog); lv_obj_set_size(body, LV_PCT(100), 1); lv_obj_set_flex_grow(body, 1);
    lv_textarea_set_text(body, text.c_str()); lv_textarea_set_cursor_click_pos(body, false);
    ui_add_accent_focus_style(body);
}
static void entry_click(lv_event_t *e) {
    const size_t index = (uintptr_t)lv_event_get_user_data(e);
    if (index >= entries.size()) return;
    const auto item = entries[index];
    selected_path = directory == "/" ? String("/") + item.name : directory + "/" + item.name;
    selected_directory = item.directory;
    create_dialog(item.name.c_str());
    if (item.directory) button(dialog, "Open folder", open_directory);
    else button(dialog, "Preview", preview);
    button(dialog, "Rename", rename_prompt); button(dialog, "Delete", delete_prompt);
}
static void up(lv_event_t *) {
    int slash = directory.lastIndexOf('/'); directory = slash <= 0 ? "/" : directory.substring(0, slash); refresh();
}
static void refresh() {
    lv_obj_clean(list); entries.clear();
    lv_label_set_text_fmt(path_label, "%s: %s", use_fat ? "FAT" : "SD", directory.c_str());
    if (!ready()) {
        const char *text = use_fat ? "Internal FAT is unavailable" : "SD is unavailable. Choose storage in settings.";
        set_notice(text); lv_list_add_text(list, text); return;
    }
    File root = filesystem().open(directory);
    if (!root || !root.isDirectory()) { set_notice("Cannot open directory"); lv_list_add_text(list, status_text.c_str()); return; }
    for (File file = root.openNextFile(); file; file = root.openNextFile()) {
        String name = file.name(); int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
        if (name != "." && name != "..") entries.push_back({name, file.isDirectory(), file.size()}); file.close();
    }
    root.close();
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    if (directory != "/") { auto *b = lv_list_add_button(list, LV_SYMBOL_UP, ".."); lv_obj_add_event_cb(b, up, LV_EVENT_CLICKED, nullptr); }
    for (size_t i = 0; i < entries.size(); ++i) {
        auto *b = lv_list_add_button(list, entries[i].directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, entries[i].name.c_str());
        lv_obj_add_event_cb(b, entry_click, LV_EVENT_CLICKED, (void *)i);
        lv_obj_add_event_cb(b, focus_cb, LV_EVENT_FOCUSED, nullptr);
    }
    char count[32]; snprintf(count, sizeof(count), "%u items", unsigned(entries.size()));
    set_notice(count);
}
static void storage_change(lv_event_t *) { use_fat = lv_dropdown_get_selected(storage) == 1; directory = "/"; refresh(); }
static void settings_deleted(lv_event_t *) { settings_page = storage = notice = nullptr; }
static void open_settings(lv_event_t *) {
    if (settings_page) return;
    settings_page = ui_create_settings_page(page, "File storage");
    lv_obj_add_event_cb(settings_page, settings_deleted, LV_EVENT_DELETE, nullptr);
    auto *label = lv_label_create(settings_page); lv_label_set_text(label, "Storage");
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    storage = lv_dropdown_create(settings_page);
    lv_dropdown_set_options(storage,
#if defined(USING_FATFS)
        "SD\nFAT"
#else
        "SD"
#endif
    );
    lv_obj_set_width(storage, LV_PCT(100)); lv_dropdown_set_selected(storage, use_fat ? 1 : 0);
    ui_add_accent_focus_style(storage);
    lv_obj_add_flag(storage, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(storage, focus_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(storage, storage_change, LV_EVENT_VALUE_CHANGED, nullptr);
    notice = lv_label_create(settings_page); lv_obj_set_width(notice, LV_PCT(100));
    lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
    lv_label_set_text(notice, status_text.c_str());
    lv_obj_set_style_text_color(notice, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_radius(notice, 8, 0);
    lv_obj_add_flag(notice, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    ui_add_accent_focus_style(notice);
    if (auto *group = lv_group_get_default()) lv_group_add_obj(group, notice);
    lv_obj_add_event_cb(notice, focus_cb, LV_EVENT_FOCUSED, nullptr);
    lv_group_focus_obj(storage);
}
static void cleanup() {
    close_dialog();
    if (settings_page) ui_destroy_settings_page(settings_page);
    entries.clear(); page = list = path_label = path_bar = storage = notice = nullptr;
}
static void back(lv_event_t *) { close_dialog(); auto *old = page; cleanup(); if (old) ui_destroy_app_page(old); menu_show(); }
} // namespace
#endif
void ui_sd_manager_enter(lv_obj_t *parent) {
#ifdef ARDUINO
    directory = "/"; use_fat = !(hw_is_sd_insert() && hw_get_sd_size() > 0);
#if !defined(USING_FATFS)
    use_fat = false;
#endif
    page = ui_create_app_page(parent, "Files", back);
    lv_obj_add_event_cb(page, [](lv_event_t *) { cleanup(); }, LV_EVENT_DELETE, nullptr);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_pad_row(page, 4, 0);
    ui_app_page_add_header_button(page, LV_SYMBOL_SETTINGS, open_settings);
    list = lv_list_create(page); lv_obj_set_size(list, LV_PCT(100), 1); lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_row(list, 5, 0);
    path_bar = lv_obj_create(page);
    lv_obj_add_flag(path_bar, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING));
    lv_obj_remove_flag(path_bar, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    lv_obj_set_size(path_bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(path_bar, &ui_styles.card, 0);
    // 80% transparent background; keep the path text fully opaque.
    lv_obj_set_style_bg_opa(path_bar, LV_OPA_20, 0);
    lv_obj_set_style_border_width(path_bar, 0, 0);
    lv_obj_set_style_pad_hor(path_bar, 6, 0); lv_obj_set_style_pad_ver(path_bar, 3, 0);
    lv_obj_set_style_radius(path_bar, 0, 0);
    lv_obj_align(path_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    path_label = lv_label_create(path_bar); lv_obj_set_width(path_label, LV_PCT(100));
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(path_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(path_bar, [](lv_event_t *event) {
        if (list) lv_obj_set_style_pad_bottom(list, lv_obj_get_height(lv_event_get_target_obj(event)) + 6, 0);
    }, LV_EVENT_SIZE_CHANGED, nullptr);
    lv_obj_update_layout(page);
    // The list extends below the overlay, but its final row can scroll clear.
    lv_obj_set_style_pad_bottom(list, lv_obj_get_height(path_bar) + 6, 0);
    refresh();
#else
    (void)parent;
#endif
}
void ui_sd_manager_exit(lv_obj_t *) {
#ifdef ARDUINO
    cleanup();
#endif
}
app_t ui_sd_manager_main = { .setup_func_cb = ui_sd_manager_enter, .exit_func_cb = ui_sd_manager_exit, .user_data = nullptr };
#endif
