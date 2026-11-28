/**
 * @file      ui_nfc_mfc.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Unified ST25R3916 NFC utility page for reading, storing, writing, emulating
 * and erasing supported NFC data, with a hardware/software capability guide.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if defined(ARDUINO) && defined(USING_ST25R3916) && !defined(EXCLUDE_NFC_MFC)

#include <SD.h>
#if defined(USING_FATFS)
#include <FFat.h>
#endif
#include <algorithm>
#include <vector>
#include "nfc/LilyGoMifareClassicDumpFile.h"
#include "nfc/LilyGoNfcType2DumpFile.h"
#include "src/nfc_action_icons.h"

namespace {
static constexpr const char *kNfcDir = "/nfc";

enum nfc_mode_t : uint8_t {
    NFC_MODE_MENU,
    NFC_MODE_READ,
    NFC_MODE_WRITE,
    NFC_MODE_EMULATE,
    NFC_MODE_ERASE,
    NFC_MODE_INFO,
};

struct file_item_t {
    char path[96] = {};
    char source[12] = {};
    bool writable = false;
    bool type2 = false;
    bool classic = false;
};

struct nfc_menu_item_t {
    const char *title;
    const lv_image_dsc_t *icon;
    nfc_mode_t mode;
};

static constexpr nfc_menu_item_t kMenuItems[] = {
    {"Read", &nfc_action_read, NFC_MODE_READ},
    {"Write", &nfc_action_write, NFC_MODE_WRITE},
    {"Emulate", &nfc_action_emulate, NFC_MODE_EMULATE},
    {"Erase", &nfc_action_erase, NFC_MODE_ERASE},
    {"Info", &nfc_action_info, NFC_MODE_INFO},
};

static lv_obj_t *page = nullptr;
static lv_obj_t *root_parent = nullptr;
static lv_obj_t *content = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *hint_label = nullptr;
static lv_obj_t *file_list = nullptr;
static lv_obj_t *read_save = nullptr;
static lv_obj_t *write_button = nullptr;
static lv_obj_t *selected_file_label = nullptr;
static lv_obj_t *emulate_button = nullptr;
static lv_obj_t *emulate_tech_dropdown = nullptr;
static lv_obj_t *emulate_write_row = nullptr;
static lv_obj_t *emulate_write_switch = nullptr;
static lv_obj_t *menu_carousel = nullptr;
static lv_obj_t *menu_icons[sizeof(kMenuItems) / sizeof(kMenuItems[0])] = {};
static bool menu_ready = false;
static lv_timer_t *timer = nullptr;
static lv_obj_t *prompt = nullptr;
static lv_obj_t *save_name_input = nullptr;
static lv_obj_t *save_format_dropdown = nullptr;
static lv_obj_t *save_description = nullptr;
static lv_obj_t *save_keyboard = nullptr;
static bool save_keyboard_transition = false;
static nfc_mode_t mode = NFC_MODE_MENU;
static bool discovery_running = false;
static bool emulation_running = false;
static bool read_finished = false;
static bool current_ndef_valid = false;
static uint32_t current_ndef_length = 0U;
static bool sd_online = false;
static uint16_t selected_file = 0xffff;
static LilyGoMifareClassicDump current_dump = {};
static LilyGoMifareClassicDump selected_classic_dump = {};
static bool current_dump_valid = false;
static LilyGoNfcType2Dump current_type2_dump = {};
static LilyGoNfcType2Dump selected_type2_dump = {};
static bool current_type2_valid = false;
static uint8_t pending_ndef[ LILYGO_NFC_RAW_BUFFER_LEN ] = {};
static uint32_t pending_ndef_length = 0U;
static std::vector<file_item_t> files;
static uint8_t menu_selected = 0;

static void set_label(lv_obj_t *label, const char *text, lv_color_t color = UI_COLOR_TEXT_PRIMARY)
{
    if (!label) return;
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, color, 0);
}

static bool ensure_sd()
{
    if (hw_is_sd_insert() && hw_get_sd_size() > 0.0f) return true;
    uint32_t frequencies[8] = {};
    const uint8_t count = hw_get_sd_mount_freq_list(frequencies, 8);
    for (uint8_t i = 0; i < count; ++i) {
        if (frequencies[i] && hw_mount_sd(frequencies[i]) &&
                hw_is_sd_insert() && hw_get_sd_size() > 0.0f) return true;
    }
    return false;
}

static bool ensure_dir(fs::FS &filesystem)
{
    return filesystem.exists(kNfcDir) || filesystem.mkdir(kNfcDir);
}

static void update_save_buttons()
{
    const bool can_save = read_finished && (current_dump_valid || current_type2_valid || current_ndef_valid);
    if (read_save) {
        if (can_save) lv_obj_clear_state(read_save, LV_STATE_DISABLED);
        else lv_obj_add_state(read_save, LV_STATE_DISABLED);
    }
}

static void refresh_sd_state()
{
    sd_online = ensure_sd();
    update_save_buttons();
}

static void close_prompt()
{
    save_keyboard_transition = true;
    if (save_keyboard) {
        ui_soft_keyboard_hide(save_keyboard, nullptr);
        lv_obj_delete(save_keyboard);
        save_keyboard = nullptr;
    }
    save_name_input = nullptr;
    save_format_dropdown = nullptr;
    save_description = nullptr;
    if (prompt) {
        lv_obj_t *box = prompt;
        prompt = nullptr;
        destroy_msgbox(box);
    }
    save_keyboard_transition = false;
}

static void set_write_button_state();

static void stop_discovery()
{
    if (discovery_running || emulation_running) {
        hw_stop_nfc();
        discovery_running = false;
        emulation_running = false;
        set_write_button_state();
    }
}

static void clear_content_refs()
{
    menu_ready = false;
    menu_carousel = nullptr;
    for (lv_obj_t *&icon : menu_icons) icon = nullptr;
    status_label = hint_label = file_list = read_save = nullptr;
    write_button = selected_file_label = emulate_button = emulate_tech_dropdown = nullptr;
    emulate_write_row = emulate_write_switch = nullptr;
}

static void focus_scroll_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target_obj(event);
    if (target) lv_obj_scroll_to_view_recursive(target, LV_ANIM_ON);
}

static void rounded_focus(lv_obj_t *object, int32_t radius = 8)
{
    ui_add_accent_focus_style(object);
    lv_obj_set_style_radius(object, radius, LV_STATE_DEFAULT);
    for (lv_style_selector_t state : {
            lv_style_selector_t(LV_STATE_PRESSED),
            lv_style_selector_t(LV_STATE_FOCUSED), lv_style_selector_t(LV_STATE_FOCUS_KEY),
            lv_style_selector_t(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY),
            lv_style_selector_t(LV_STATE_PRESSED | LV_STATE_FOCUSED),
            lv_style_selector_t(LV_STATE_PRESSED | LV_STATE_FOCUS_KEY),
            lv_style_selector_t(LV_STATE_PRESSED | LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY)}) {
        lv_obj_set_style_radius(object, radius, state);
    }
}

static lv_obj_t *nfc_msgbox(lv_obj_t *parent, const char *title, const char *message,
        const char **buttons, lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *box = create_msgbox(parent, title, message, buttons, callback, user_data);
    lv_obj_t *footer = box ? lv_msgbox_get_footer(box) : nullptr;
    if (footer) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(footer); ++i) rounded_focus(lv_obj_get_child(footer, i));
    }
    return box;
}

static lv_obj_t *focusable_hint(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_all(label, 4, 0);
    lv_obj_add_flag(label, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
    rounded_focus(label);
    lv_obj_add_event_cb(label, focus_scroll_cb, LV_EVENT_FOCUSED, nullptr);
    if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, label);
    return label;
}

static void back_cb(lv_event_t *);
static void build_mode();

static void menu_button_cb(lv_event_t *event)
{
    const uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= sizeof(kMenuItems) / sizeof(kMenuItems[0])) return;
    mode = kMenuItems[index].mode;
    close_prompt();
    stop_discovery();
    clear_content_refs();
    if (page) {
        ui_destroy_app_page(page);
        page = nullptr;
    }
    content = nullptr;
    build_mode();
}

static lv_obj_t *primary_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback,
                                 void *user_data = nullptr)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 40);
    lv_obj_set_style_bg_color(button, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    rounded_focus(button);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_add_event_cb(button, focus_scroll_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, button);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_center(label);
    return button;
}

static void menu_x_exec(void *object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void menu_zoom_exec(void *object, int32_t value)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)object, value, 0);
}

static void menu_opa_exec(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void menu_animate_value(lv_obj_t *object, int32_t start, int32_t end,
                               lv_anim_exec_xcb_t exec_cb)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, exec_cb);
    lv_anim_start(&animation);
}

static void menu_apply_visual(bool animate)
{
    if (!menu_ready || !menu_carousel) return;
    const int32_t center_x = lv_obj_get_content_width(menu_carousel) / 2 - 36;
    const int32_t center_y = lv_obj_get_content_height(menu_carousel) / 2 - 42;
    const int32_t spacing = std::min<int32_t>(88, lv_obj_get_content_width(menu_carousel) / 3);
    for (size_t i = 0; i < sizeof(menu_icons) / sizeof(menu_icons[0]); ++i) {
        if (!menu_icons[i]) continue;
        int distance = static_cast<int>(i) - static_cast<int>(menu_selected);
        const int count = sizeof(kMenuItems) / sizeof(kMenuItems[0]);
        if (distance > count / 2) distance -= count;
        if (distance < -count / 2) distance += count;
        const int32_t target_x = center_x + distance * spacing;
        const int32_t target_zoom = distance == 0 ? 290 : 210;
        const int32_t target_opa = abs(distance) > 1 ? 0 : (distance == 0 ? 255 : 180);
        lv_obj_set_y(menu_icons[i], center_y);
        if (abs(distance) <= 1) lv_obj_add_flag(menu_icons[i], LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_remove_flag(menu_icons[i], LV_OBJ_FLAG_CLICKABLE);
        if (animate) {
            menu_animate_value(menu_icons[i], lv_obj_get_x(menu_icons[i]), target_x, menu_x_exec);
            menu_animate_value(menu_icons[i], lv_obj_get_style_transform_scale_x(menu_icons[i], (lv_part_t)0),
                               target_zoom, menu_zoom_exec);
            menu_animate_value(menu_icons[i], lv_obj_get_style_opa(menu_icons[i], (lv_part_t)0),
                               target_opa, menu_opa_exec);
        } else {
            lv_obj_set_x(menu_icons[i], target_x);
            lv_obj_set_style_transform_zoom(menu_icons[i], target_zoom, 0);
            lv_obj_set_style_opa(menu_icons[i], (lv_opa_t)target_opa, 0);
        }
        lv_obj_t *icon = lv_obj_get_child(menu_icons[i], 0);
        if (icon) {
            lv_obj_set_style_image_recolor(icon,
                                           distance == 0 ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY,
                                           0);
        }
        lv_obj_t *label = lv_obj_get_child(menu_icons[i], 1);
        if (label) lv_obj_set_style_text_color(label,
                    distance == 0 ? UI_COLOR_TEXT_PRIMARY : UI_COLOR_TEXT_SECONDARY, 0);
    }
    if (menu_icons[menu_selected]) lv_obj_move_foreground(menu_icons[menu_selected]);
}

static void menu_focus_cb(lv_event_t *event)
{
    const uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= sizeof(kMenuItems) / sizeof(kMenuItems[0])) return;
    menu_selected = (uint8_t)index;
    menu_apply_visual(true);
}

static void show_menu()
{
    page = ui_create_app_page(root_parent, "NFC", back_cb);
    content = page;
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    menu_ready = false;

    menu_carousel = lv_obj_create(content);
    lv_obj_set_width(menu_carousel, LV_PCT(100));
    lv_obj_set_flex_grow(menu_carousel, 1);
    lv_obj_set_style_bg_opa(menu_carousel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_carousel, 0, 0);
    lv_obj_set_style_pad_all(menu_carousel, 0, 0);
    lv_obj_remove_flag(menu_carousel, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < sizeof(kMenuItems) / sizeof(kMenuItems[0]); ++i) {
        lv_obj_t *button = lv_btn_create(menu_carousel);
        menu_icons[i] = button;
        lv_obj_set_size(button, 72, 84);
        lv_obj_set_style_radius(button, 12, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_bg_color(button, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_transform_pivot_x(button, 36, 0);
        lv_obj_set_style_transform_pivot_y(button, 42, 0);
        rounded_focus(button, 12);
        ui_disable_focus_shadow(button);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(button, menu_button_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(button, menu_focus_cb, LV_EVENT_FOCUSED, (void *)(uintptr_t)i);
        lv_obj_t *icon = lv_image_create(button);
        lv_image_set_src(icon, kMenuItems[i].icon);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, kMenuItems[i].title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -5);
        if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, button);
    }

    menu_selected = 0;
    lv_obj_update_layout(content);
    menu_ready = true;
    menu_apply_visual(false);
    if (lv_group_t *group = lv_group_get_default()) lv_group_focus_obj(menu_icons[0]);
}

static void prompt_cancel(lv_event_t *)
{
    close_prompt();
    stop_discovery();
    set_label(status_label, "Cancelled", UI_COLOR_WARNING);
}

static void read_start(lv_event_t *)
{
    current_dump = {};
    current_dump_valid = false;
    current_type2_dump = {};
    current_type2_valid = false;
    current_ndef_valid = false;
    current_ndef_length = 0U;
    read_finished = false;
    update_save_buttons();
    set_label(status_label, "Starting NFC reader...", UI_COLOR_ACCENT);
    set_label(hint_label, "Place a card near the NFC area.");
    if (!hw_start_nfc_discovery()) {
        set_label(status_label, "NFC reader unavailable", UI_COLOR_WARNING);
        return;
    }
    discovery_running = true;
}

static void show_read_prompt()
{
    prompt = nfc_msgbox(lv_screen_active(), "NFC Read",
                           "Place the card near the NFC area.\n"
                           "The reader will collect supported NFC data.",
                           nullptr, prompt_cancel, nullptr);
    if (prompt) read_start(nullptr);
}

static void read_button_cb(lv_event_t *)
{
    if (discovery_running) return;
    show_read_prompt();
}

static bool save_dump(fs::FS &filesystem, const char *path)
{
    if (!ensure_dir(filesystem)) return false;
    return current_dump_valid && lilygoSaveMifareClassicDump(filesystem, path, current_dump);
}

static bool save_ndef(fs::FS &filesystem, const char *path)
{
    if (!ensure_dir(filesystem) || !current_ndef_valid || current_ndef_length == 0U) return false;
    File file = filesystem.open(path, FILE_WRITE);
    if (!file) return false;
    const uint8_t *data = LilyGoNfc.ndefRawMessage();
    const size_t written = data ? file.write(data, current_ndef_length) : 0U;
    file.close();
    return written == current_ndef_length;
}

static bool saving_type2_image()
{
    return current_type2_valid && (!save_format_dropdown || lv_dropdown_get_selected(save_format_dropdown) == 0);
}

static bool save_read_result(fs::FS &filesystem, const char *path)
{
    if (saving_type2_image()) return ensure_dir(filesystem) && lilygoSaveNfcType2Dump(filesystem, path, current_type2_dump);
    return current_dump_valid ? save_dump(filesystem, path) : save_ndef(filesystem, path);
}

static void show_save_keyboard()
{
    if (save_keyboard_transition || !save_keyboard || !save_name_input || !prompt ||
            !ui_soft_keyboard_should_open()) return;
    save_keyboard_transition = true;

    // Keep the dialog intact; its content is managed by the message-box layout.
    // On short screens only the file-name field fits above the keyboard.
    lv_obj_t *header = lv_msgbox_get_header(prompt);
    lv_obj_t *footer = lv_msgbox_get_footer(prompt);
    if (header) lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
    if (footer) lv_obj_add_flag(footer, LV_OBJ_FLAG_HIDDEN);
    if (save_description) lv_obj_add_flag(save_description, LV_OBJ_FLAG_HIDDEN);
    if (save_format_dropdown) lv_obj_add_flag(save_format_dropdown, LV_OBJ_FLAG_HIDDEN);

    ui_soft_keyboard_show(save_keyboard, save_name_input, nullptr, nullptr);
    const int32_t available_height = lv_display_get_vertical_resolution(nullptr) -
                                     lv_obj_get_height(save_keyboard) - 8;
    lv_obj_set_height(prompt, available_height);
    lv_obj_align(prompt, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_update_layout(prompt);
    lv_obj_scroll_to_view(save_name_input, LV_ANIM_OFF);
    save_keyboard_transition = false;
}

static void hide_save_keyboard()
{
    if (save_keyboard_transition || !save_keyboard || !prompt ||
            lv_obj_has_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN)) return;
    save_keyboard_transition = true;
    ui_soft_keyboard_hide(save_keyboard, nullptr);
    lv_obj_t *header = lv_msgbox_get_header(prompt);
    lv_obj_t *footer = lv_msgbox_get_footer(prompt);
    if (header) lv_obj_remove_flag(header, LV_OBJ_FLAG_HIDDEN);
    if (footer) lv_obj_remove_flag(footer, LV_OBJ_FLAG_HIDDEN);
    if (save_description) lv_obj_remove_flag(save_description, LV_OBJ_FLAG_HIDDEN);
    if (save_format_dropdown) lv_obj_remove_flag(save_format_dropdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(prompt, LV_PCT(94), LV_PCT(78));
    lv_obj_center(prompt);
    lv_obj_update_layout(prompt);
    if (footer) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(footer); ++i) {
            lv_obj_t *button = lv_obj_get_child(footer, i);
            if (!lv_obj_has_state(button, LV_STATE_DISABLED) && !lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN)) {
                lv_group_focus_obj(button);
                break;
            }
        }
    }
    save_keyboard_transition = false;
}

static void save_name_input_event(lv_event_t *event)
{
    if (save_keyboard_transition) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if ((code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) && save_keyboard) {
        show_save_keyboard();
    } else if ((code == LV_EVENT_DEFOCUSED && !ui_soft_keyboard_focus_transition_active()) ||
               code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        hide_save_keyboard();
    }
}

static void make_save_path(char *path, size_t path_size)
{
    char safe_name[56] = {};
    const char *source = save_name_input ? lv_textarea_get_text(save_name_input) : "nfc_read";
    size_t length = 0;
    for (size_t i = 0; source && source[i] && length + 1 < sizeof(safe_name); ++i) {
        const char ch = source[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
            safe_name[length++] = ch;
        }
    }
    safe_name[length] = '\0';
    if (length == 0) snprintf(safe_name, sizeof(safe_name), "nfc_read");
    const char *extension = saving_type2_image() ? ".t2t" : current_dump_valid ? ".mfc" : ".ndef";
    snprintf(path, path_size, "%s/%s%s", kNfcDir, safe_name, extension);
}

static void save_dialog_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    lv_obj_t *label = button ? lv_obj_get_child(button, 0) : nullptr;
    const char *choice = label ? lv_label_get_text(label) : "";
    if (!choice[0] || !strcmp(choice, "Cancel")) {
        close_prompt();
        return;
    }
    char path[96] = {};
    make_save_path(path, sizeof(path));
    bool saved = false;
    if (!strcmp(choice, "Save SD")) {
        refresh_sd_state();
        if (!sd_online) {
            set_label(hint_label, "SD card is offline.", UI_COLOR_WARNING);
            return;
        }
        saved = save_read_result(SD, path);
    } else if (!strcmp(choice, "Save FAT")) {
#if defined(USING_FATFS)
        saved = save_read_result(FFat, path);
#endif
    }
    close_prompt();
    char message[128];
    snprintf(message, sizeof(message), "%s: %s", saved ? "Saved" : "Save failed", path);
    set_label(hint_label, message, saved ? UI_COLOR_ACCENT : UI_COLOR_WARNING);
}

static void open_save_dialog(lv_event_t *)
{
    if (!read_finished || (!current_dump_valid && !current_type2_valid && !current_ndef_valid)) {
        set_label(hint_label, "Read a card before saving.", UI_COLOR_WARNING);
        return;
    }
    refresh_sd_state();
    static const char *buttons[] = {"Save SD", "Save FAT", "Cancel", ""};
    prompt = nfc_msgbox(lv_screen_active(), "Save NFC data",
                           "Enter a file name. The extension is added automatically.",
                           buttons, save_dialog_cb, nullptr);
    if (!prompt) return;
    lv_obj_t *body = lv_msgbox_get_content(prompt);
    if (body) {
        save_description = lv_obj_get_child(body, 0);
        lv_obj_set_style_pad_all(body, 6, 0);
        lv_obj_set_style_pad_row(body, 4, 0);
        if (current_type2_valid && current_ndef_valid) {
            save_format_dropdown = lv_dropdown_create(body);
            lv_dropdown_set_options(save_format_dropdown, "Type 2 image (.t2t)\nNDEF message (.ndef)");
            lv_obj_set_size(save_format_dropdown, LV_PCT(100), 34);
            rounded_focus(save_format_dropdown);
            lv_obj_add_flag(save_format_dropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_event_cb(save_format_dropdown, focus_scroll_cb, LV_EVENT_FOCUSED, nullptr);
            if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, save_format_dropdown);
        }
        lv_obj_t *name_label = lv_label_create(body);
        lv_label_set_text(name_label, "File name");
        lv_obj_set_style_text_color(name_label, UI_COLOR_TEXT_SECONDARY, 0);
        save_name_input = lv_textarea_create(body);
        lv_obj_set_width(save_name_input, LV_PCT(100));
        lv_obj_set_height(save_name_input, 36);
        lv_textarea_set_one_line(save_name_input, true);
        lv_textarea_set_max_length(save_name_input, 42);
        lv_textarea_set_text(save_name_input, current_type2_valid ? "type2_dump" : current_dump_valid ? "mfc_dump" : "nfc_read");
        lv_obj_set_style_bg_color(save_name_input, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_text_color(save_name_input, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_border_color(save_name_input, UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_border_width(save_name_input, 1, 0);
        ui_prepare_textarea_for_encoder(save_name_input);
        rounded_focus(save_name_input, 7);
        lv_obj_add_event_cb(save_name_input, save_name_input_event, LV_EVENT_ALL, nullptr);
        if (lv_group_t *group = lv_group_get_default()) {
            lv_group_add_obj(group, save_name_input);
            lv_group_focus_obj(save_name_input);
        }
#if FACTORY_HAS_SOFT_KEYBOARD
        save_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_style_bg_color(save_keyboard, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(save_keyboard, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(save_keyboard, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_add_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (ui_soft_keyboard_should_open()) {
            show_save_keyboard();
        }
#endif
    }
    lv_obj_t *footer = lv_msgbox_get_footer(prompt);
    if (footer) {
#if !defined(USING_FATFS)
        if (lv_obj_get_child_count(footer) > 1) lv_obj_add_state(lv_obj_get_child(footer, 1), LV_STATE_DISABLED);
#endif
        if (!sd_online && lv_obj_get_child_count(footer) > 0) {
            lv_obj_add_state(lv_obj_get_child(footer, 0), LV_STATE_DISABLED);
        }
    }
}

static void read_page()
{
    read_finished = false;
    current_dump_valid = false;
    current_type2_valid = false;
    current_ndef_valid = false;
    current_ndef_length = 0U;
    lv_obj_t *card = ui_create_card(content, "Read NFC card");
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Ready");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    primary_button(card, "Read card", read_button_cb);
    /* A single Save action opens the destination picker after the filename is set. */
    read_save = primary_button(card, "Save", open_save_dialog);
    lv_obj_add_state(read_save, LV_STATE_DISABLED);
    hint_label = focusable_hint(card);
    set_label(hint_label, "Reads Type 2 identity and pages, NDEF data, or authenticated MIFARE Classic blocks.");
    refresh_sd_state();
}

static bool is_nfc_file(const char *name)
{
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".mfc") || !strcasecmp(dot, ".t2t") || !strcasecmp(dot, ".ndef") ||
                   !strcasecmp(dot, ".nfc"));
}

static bool is_writable_nfc_file(const char *name)
{
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".ndef") || !strcasecmp(dot, ".nfc"));
}

template <typename FS_TYPE>
static void collect_files(FS_TYPE &filesystem, const char *source)
{
    if (!filesystem.exists(kNfcDir)) return;
    File root = filesystem.open(kNfcDir, FILE_READ);
    if (!root || !root.isDirectory()) return;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && is_nfc_file(entry.name())) {
            file_item_t item;
            snprintf(item.path, sizeof(item.path), "%s", entry.path());
            snprintf(item.source, sizeof(item.source), "%s", source);
            item.writable = is_writable_nfc_file(item.path);
            const char *dot = strrchr(item.path, '.');
            item.type2 = dot && !strcasecmp(dot, ".t2t");
            item.classic = dot && !strcasecmp(dot, ".mfc");
            files.push_back(item);
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

static void set_write_button_state()
{
    const bool selected = selected_file < files.size() && files[selected_file].writable;
    if (write_button) {
        const bool type2 = selected_file < files.size() && files[selected_file].type2 &&
                           lilygoNfcType2CheckWriteSource(selected_type2_dump) == LILYGO_NFC_TYPE2_WRITE_READY;
        if ((selected || type2) && !discovery_running) lv_obj_clear_state(write_button, LV_STATE_DISABLED);
        else lv_obj_add_state(write_button, LV_STATE_DISABLED);
    }
    if (emulate_button) {
        const bool type2 = selected_file < files.size() && files[selected_file].type2 &&
                           lilygoNfcType2CanEmulate(selected_type2_dump);
        const bool classic = selected_file < files.size() && files[selected_file].classic &&
                             lilygoMifareClassicCanEmulate(selected_classic_dump);
        if (emulation_running || selected || type2 || classic) lv_obj_clear_state(emulate_button, LV_STATE_DISABLED);
        else lv_obj_add_state(emulate_button, LV_STATE_DISABLED);
    }
}

static fs::FS *selected_filesystem()
{
    if (selected_file >= files.size()) return nullptr;
    if (!strcmp(files[selected_file].source, "SD")) return ensure_sd() ? &SD : nullptr;
#if defined(USING_FATFS)
    if (!strcmp(files[selected_file].source, "FAT")) return &FFat;
#endif
    return nullptr;
}

static bool load_selected_type2()
{
    fs::FS *filesystem = selected_filesystem();
    selected_type2_dump = {};
    return filesystem && selected_file < files.size() && files[selected_file].type2 &&
           lilygoLoadNfcType2Dump(*filesystem, files[selected_file].path, selected_type2_dump);
}

static bool load_selected_classic()
{
    fs::FS *filesystem = selected_filesystem();
    selected_classic_dump = {};
    return filesystem && selected_file < files.size() && files[selected_file].classic &&
           lilygoLoadMifareClassicDump(*filesystem, files[selected_file].path, selected_classic_dump);
}

static void update_emulation_controls()
{
    const bool type2 = selected_file < files.size() && files[selected_file].type2;
    const bool classic = selected_file < files.size() && files[selected_file].classic;
    if (emulate_tech_dropdown) {
        if (emulation_running || type2 || classic) lv_obj_add_state(emulate_tech_dropdown, LV_STATE_DISABLED);
        else lv_obj_clear_state(emulate_tech_dropdown, LV_STATE_DISABLED);
    }
    if (emulate_write_row) {
        if (type2) lv_obj_remove_flag(emulate_write_row, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(emulate_write_row, LV_OBJ_FLAG_HIDDEN);
    }
    if (emulate_write_switch) {
        if (emulation_running) lv_obj_add_state(emulate_write_switch, LV_STATE_DISABLED);
        else lv_obj_clear_state(emulate_write_switch, LV_STATE_DISABLED);
    }
}

static void file_click(lv_event_t *event)
{
    if (emulation_running) return;
    selected_file = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    if (selected_file >= files.size()) return;
    char text[128];
    snprintf(text, sizeof(text), "%s: %s", files[selected_file].source, files[selected_file].path);
    set_label(selected_file_label, text, UI_COLOR_ACCENT);
    if (mode == NFC_MODE_EMULATE && emulate_tech_dropdown) {
        lv_dropdown_set_options(emulate_tech_dropdown, files[selected_file].type2 ?
                                "NFC-A / Type 2 (experimental)" :
                                files[selected_file].classic ? "Classic / read-only (experimental)" :
                                "NFC-A / Type 4 (experimental)\nNFC-F / Type 3");
        lv_dropdown_set_selected(emulate_tech_dropdown, 0);
        update_emulation_controls();
    }
    if (files[selected_file].type2) {
        const bool loaded = load_selected_type2();
        const auto writable = lilygoNfcType2CheckWriteSource(selected_type2_dump);
        const bool valid = loaded && (mode == NFC_MODE_WRITE ? writable == LILYGO_NFC_TYPE2_WRITE_READY :
                                     lilygoNfcType2CanEmulate(selected_type2_dump));
        snprintf(text, sizeof(text), "%s: %u/%u pages; UID %u bytes.",
                 lilygoNfcType2ModelName(selected_type2_dump.model), selected_type2_dump.pagesRead,
                 selected_type2_dump.pageCount, selected_type2_dump.uidLen);
        set_label(status_label, valid ? text : mode == NFC_MODE_WRITE ? lilygoNfcType2WriteStatusText(writable) :
                  "Image cannot be emulated: invalid or missing identity pages.",
                  valid ? UI_COLOR_ACCENT : UI_COLOR_WARNING);
        set_label(hint_label, mode == NFC_MODE_WRITE ?
                  "Writes user pages to a matching model and verifies each page. Target UID, lock and configuration pages are preserved." :
                  "Type 2 restores the captured identity and pages. Missing pages return NAK. "
                  "Hidden credentials are unknown. Temporary writes change user pages in RAM only.");
        set_write_button_state();
        return;
    }
    if (files[selected_file].classic) {
        const bool loaded = load_selected_classic();
        const bool eligible = loaded && lilygoMifareClassicCanEmulate(selected_classic_dump);
        snprintf(text, sizeof(text), "Classic: %u/%u blocks, %u known sectors%s.",
                 selected_classic_dump.blocksRead, selected_classic_dump.totalBlocks,
                 selected_classic_dump.authenticatedSectors, selected_classic_dump.complete ? "" : " (partial)");
        set_label(status_label, mode == NFC_MODE_EMULATE && eligible ? text : mode == NFC_MODE_WRITE ?
                  "Classic physical writing is unavailable." : "Image identity, access bits or known keys are unavailable.",
                  eligible && mode == NFC_MODE_EMULATE ? UI_COLOR_ACCENT : UI_COLOR_WARNING);
        set_label(hint_label, mode == NFC_MODE_EMULATE ?
                  "Experimental read-only Classic emulation with a 4-byte UID. Only captured blocks and known keys are used. "
                  "Missing blocks and unknown keys are refused; writing and value commands are unavailable." :
                  "Classic images can be selected for inspection and experimental emulation. Physical writing is unavailable.");
        set_write_button_state();
        return;
    }
    set_label(status_label, "NDEF file selected", UI_COLOR_ACCENT);
    set_label(hint_label, mode == NFC_MODE_WRITE ? "Writes the selected message to a supported writable NFC card." :
              "Emulates NDEF content with the selected protocol.");
    set_write_button_state();
}

static void list_files()
{
    files.clear();
    selected_type2_dump = {};
    selected_classic_dump = {};
    selected_file = 0xffff;
    refresh_sd_state();
    if (sd_online) collect_files(SD, "SD");
#if defined(USING_FATFS)
    collect_files(FFat, "FAT");
#endif
    if (!file_list) return;
    if (files.empty()) {
        lv_obj_t *empty = lv_label_create(file_list);
        lv_label_set_text(empty, "No NFC files found in /nfc.");
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
        return;
    }
    for (size_t i = 0; i < files.size(); ++i) {
        const bool supported = files[i].writable || files[i].type2 || (mode == NFC_MODE_EMULATE && files[i].classic);
        lv_obj_t *row = lv_btn_create(file_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 34);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        rounded_focus(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(row, focus_scroll_cb, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(row, file_click, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, row);
        lv_obj_t *label = lv_label_create(row);
        char text[128];
        snprintf(text, sizeof(text), "%s  %s%s", files[i].source, files[i].path,
                 files[i].type2 ? "  (Type 2 image)" : files[i].classic ? "  (Classic image)" : "");
        lv_label_set_text(label, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_color(label, supported ? UI_COLOR_TEXT_PRIMARY : UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_center(label);
    }
}

static void write_prompt_cancel(lv_event_t *event)
{
    prompt_cancel(event);
}

static bool load_selected_ndef()
{
    if (selected_file >= files.size()) return false;
    const file_item_t &item = files[selected_file];
    fs::FS *filesystem = selected_filesystem();
    if (!filesystem) return false;
    File file = filesystem->open(item.path, FILE_READ);
    if (!file || file.size() == 0 || file.size() > sizeof(pending_ndef)) {
        if (file) file.close();
        return false;
    }
    pending_ndef_length = file.read(pending_ndef, file.size());
    file.close();
    return pending_ndef_length != 0U;
}

static void write_start(lv_event_t *)
{
    set_label(status_label, "Place the target card near NFC area.", UI_COLOR_ACCENT);
    set_label(hint_label, "The selected NFC file will be validated when the card is detected.");
    const bool type2 = selected_file < files.size() && files[selected_file].type2;
    const bool started = type2 ? hw_start_nfc_type2_write(selected_type2_dump) :
                         hw_start_nfc_ndef_write(pending_ndef, pending_ndef_length);
    if (!started) {
        hw_stop_nfc();
        close_prompt();
        set_label(status_label, "NFC reader unavailable", UI_COLOR_WARNING);
        return;
    }
    discovery_running = true;
    set_write_button_state();
}

static void write_cb(lv_event_t *)
{
    if (selected_file >= files.size() || discovery_running) return;
    const bool type2 = files[selected_file].type2;
    if (type2 && (!load_selected_type2() ||
                  lilygoNfcType2CheckWriteSource(selected_type2_dump) != LILYGO_NFC_TYPE2_WRITE_READY)) {
        set_label(status_label, lilygoNfcType2WriteStatusText(lilygoNfcType2CheckWriteSource(selected_type2_dump)), UI_COLOR_WARNING);
        set_write_button_state();
        return;
    }
    if (!type2 && (!files[selected_file].writable || !load_selected_ndef())) {
        set_label(status_label, "Invalid NDEF file.", UI_COLOR_WARNING);
        return;
    }
    prompt = nfc_msgbox(lv_screen_active(), "NFC Write", type2 ?
                           "Place a matching Type 2 target near the NFC area. User pages will be overwritten and verified. UID and lock/configuration pages are preserved." :
                           "Place a writable NFC card near the NFC area.",
                           nullptr, write_prompt_cancel, nullptr);
    write_start(nullptr);
}

static void write_page()
{
    lv_obj_t *card = ui_create_card(content, "Select NFC file");
    file_list = lv_obj_create(card);
    lv_obj_set_width(file_list, LV_PCT(100));
    lv_obj_set_height(file_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(file_list, 0, 0);
    lv_obj_set_style_pad_all(file_list, 0, 0);
    lv_obj_set_style_pad_row(file_list, 4, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    selected_file_label = lv_label_create(card);
    lv_obj_set_width(selected_file_label, LV_PCT(100));
    lv_label_set_long_mode(selected_file_label, LV_LABEL_LONG_WRAP);
    set_label(selected_file_label, "No file selected", UI_COLOR_TEXT_SECONDARY);
    write_button = primary_button(card, "Write selected file", write_cb);
    lv_obj_add_state(write_button, LV_STATE_DISABLED);
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Select a file");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    hint_label = focusable_hint(card);
    set_label(hint_label, "Select NDEF content or a supported Type 2 image. Type 2 writing restores user pages on a matching model.");
    list_files();
}

static void emulate_page()
{
    lv_obj_t *card = ui_create_card(content, "Select NFC file");
    lv_obj_set_style_pad_row(card, 10, 0);
    file_list = lv_obj_create(card);
    lv_obj_set_width(file_list, LV_PCT(100));
    lv_obj_set_height(file_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(file_list, 0, 0);
    lv_obj_set_style_pad_all(file_list, 0, 0);
    lv_obj_set_style_pad_row(file_list, 4, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    selected_file_label = lv_label_create(card);
    lv_obj_set_width(selected_file_label, LV_PCT(100));
    lv_label_set_long_mode(selected_file_label, LV_LABEL_LONG_WRAP);
    set_label(selected_file_label, "No file selected", UI_COLOR_TEXT_SECONDARY);
    lv_obj_t *tech_row = ui_create_card_dropdown(card, nullptr, "Emulation protocol",
                         "NFC-A / Type 4 (experimental)\nNFC-F / Type 3", 0, nullptr);
    emulate_tech_dropdown = lv_obj_get_child(tech_row, lv_obj_get_child_count(tech_row) - 1);
    lv_obj_set_flex_flow(tech_row, LV_FLEX_FLOW_COLUMN);
    for (uint32_t i = 0; i + 1 < lv_obj_get_child_count(tech_row); ++i) {
        lv_obj_t *child = lv_obj_get_child(tech_row, i);
        if (lv_obj_check_type(child, &lv_label_class)) lv_obj_set_width(child, LV_PCT(100));
        else lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_size(emulate_tech_dropdown, LV_PCT(100), 36);
    rounded_focus(emulate_tech_dropdown);
    emulate_write_row = ui_create_card_switch(card, nullptr, "Temporary writes (RAM)", false, nullptr);
    emulate_write_switch = lv_obj_get_child(emulate_write_row, lv_obj_get_child_count(emulate_write_row) - 1);
    lv_obj_add_flag(emulate_write_row, LV_OBJ_FLAG_HIDDEN);
    emulate_button = primary_button(card, "Start emulation", [](lv_event_t *) {
        if (emulation_running) {
            hw_stop_nfc();
            emulation_running = false;
            update_emulation_controls();
            lv_label_set_text(lv_obj_get_child(emulate_button, 0), "Start emulation");
            set_label(status_label, "Stopped", UI_COLOR_TEXT_SECONDARY);
            set_label(hint_label, "Select a file and start again.");
            return;
        }
        LilyGoNfcEmulationConfig config = {};
        config.kind = LILYGO_NFC_EMULATION_TEXT;
        const bool type2 = selected_file < files.size() && files[selected_file].type2;
        const bool classic = selected_file < files.size() && files[selected_file].classic;
        if (classic) {
            if (!load_selected_classic() || !lilygoMifareClassicCanEmulate(selected_classic_dump)) {
                set_label(status_label, "Classic image or known keys unavailable.", UI_COLOR_WARNING);
                set_write_button_state();
                return;
            }
            config.tech = LILYGO_NFC_EMULATION_TECH_MIFARE_CLASSIC;
            config.templateName = "Classic read-only image";
            config.classicDump = &selected_classic_dump;
        } else if (type2) {
            if (!load_selected_type2()) {
                set_label(status_label, "Invalid Type 2 image or missing identity pages.", UI_COLOR_WARNING);
                return;
            }
            config.tech = LILYGO_NFC_EMULATION_TECH_NFCA_T2T;
            config.templateName = "Type 2 card image";
            config.type2Dump = &selected_type2_dump;
            config.type2AllowUserWrites = lv_obj_has_state(emulate_write_switch, LV_STATE_CHECKED);
        } else {
            if (!load_selected_ndef() || pending_ndef_length > LILYGO_NFC_MAX_EMULATION_NDEF_LEN) {
                set_label(status_label, "Invalid or oversized NDEF file.", UI_COLOR_WARNING);
                return;
            }
            config.tech = lv_dropdown_get_selected(emulate_tech_dropdown) == 0 ?
                          LILYGO_NFC_EMULATION_TECH_NFCA_T4T : LILYGO_NFC_EMULATION_TECH_NFCF_T3T;
            config.templateName = "Raw NDEF file";
            config.rawNdef = pending_ndef;
            config.rawNdefLen = pending_ndef_length;
        }
        config.discoveryDurationMs = 1000U;
        if (!hw_start_nfc_emulation(config)) {
            hw_stop_nfc();
            set_label(status_label, "NFC emulation unavailable.", UI_COLOR_WARNING);
            return;
        }
        emulation_running = true;
        update_emulation_controls();
        set_label(status_label, classic ? "Emulating Classic (read-only)" : type2 ? "Emulating NFC-A Type 2" : "Emulating NDEF file", UI_COLOR_ACCENT);
        set_label(hint_label, classic ? "Hold a compatible reader near the antenna. Missing blocks and unknown keys are refused. "
                  "Experimental RF timing requires device testing. The file remains unchanged." :
                  type2 ? "Hold a phone near the antenna. Captured UID, parameters and pages are active. "
                  "Missing pages and unknown credentials cannot be reproduced." :
                  "Hold a phone near the NFC antenna. NDEF files restore message content, without the source card identity.");
        lv_label_set_text(lv_obj_get_child(emulate_button, 0), "Stop emulation");
    });
    lv_obj_add_state(emulate_button, LV_STATE_DISABLED);
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Select a file");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    hint_label = focusable_hint(card);
    set_label(hint_label, "Select .mfc for experimental read-only Classic emulation, .t2t for Type 2, or .ndef for message content.", UI_COLOR_TEXT_SECONDARY);
    list_files();
}

static void erase_start(lv_event_t *)
{
    set_label(status_label, "Place the card near NFC area.", UI_COLOR_ACCENT);
    set_label(hint_label, "The current ST25R3916 path can identify cards; destructive erase is limited to supported NDEF operations.");
    if (!hw_start_nfc_ndef_erase()) {
        set_label(status_label, "NFC reader unavailable", UI_COLOR_WARNING);
        return;
    }
    discovery_running = true;
}

static void erase_cb(lv_event_t *)
{
    if (discovery_running) return;
    prompt = nfc_msgbox(lv_screen_active(), "NFC Erase",
                           "Place the card near the NFC area.\n"
                           "Only supported writable NDEF data will be cleared.",
                           nullptr, prompt_cancel, nullptr);
    erase_start(nullptr);
}

static void erase_page()
{
    lv_obj_t *card = ui_create_card(content, "Erase NFC data");
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Status", "Ready");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    hint_label = focusable_hint(card);
    set_label(hint_label, "UID and protected system data are never erased.", UI_COLOR_TEXT_SECONDARY);
    primary_button(card, "Erase card", erase_cb);
}

static void poll(lv_timer_t *)
{
    if (mode == NFC_MODE_EMULATE) {
        if (!emulation_running) return;
        const LilyGoNfcEmulationStatus &status = LilyGoNfc.emulationStatus();
        if (status.state == LILYGO_NFC_STATE_ERROR) {
            hw_stop_nfc();
            emulation_running = false;
            update_emulation_controls();
            set_label(status_label, "NFC emulation failed", UI_COLOR_WARNING);
            set_label(hint_label, status.errorText[0] ? status.errorText : "RFAL emulation error", UI_COLOR_WARNING);
            if (emulate_button) lv_label_set_text(lv_obj_get_child(emulate_button, 0), "Start emulation");
        } else if (!LilyGoNfc.isRunning()) {
            emulation_running = false;
            update_emulation_controls();
            set_label(status_label, "Stopped", UI_COLOR_TEXT_SECONDARY);
            set_label(hint_label, "Select a file and start again.");
            if (emulate_button) lv_label_set_text(lv_obj_get_child(emulate_button, 0), "Start emulation");
        } else if (status.readerActive) {
            set_label(status_label, "Reader connected to emulated tag", UI_COLOR_ACCENT);
        } else {
            set_label(status_label, status.tech == LILYGO_NFC_EMULATION_TECH_NFCA_T2T ?
                      "Emulating NFC-A Type 2" : status.tech == LILYGO_NFC_EMULATION_TECH_MIFARE_CLASSIC ?
                      "Emulating Classic (read-only)" : "Emulating NDEF file", UI_COLOR_ACCENT);
        }
        return;
    }
    if (!discovery_running) return;
    if (!LilyGoNfc.isRunning()) {
        discovery_running = false;
        close_prompt();
        set_label(status_label, "Stopped", UI_COLOR_TEXT_SECONDARY);
        return;
    }
    const LilyGoNfcReaderResult &result = LilyGoNfc.lastResult();
    const LilyGoMifareClassicDump &dump = LilyGoNfc.mifareClassicDump();
    if (result.state == LILYGO_NFC_STATE_POLLING) {
        set_label(status_label, "Waiting for card...", UI_COLOR_ACCENT);
        return;
    }
    if (mode == NFC_MODE_READ && result.state != LILYGO_NFC_STATE_POLLING &&
            (result.mifareReadAttempted || result.type2ReadAttempted || result.ndefAttempted ||
             result.state == LILYGO_NFC_STATE_WAIT_RELEASE)) {
        current_dump = dump;
        current_dump_valid = result.mifareReadAttempted && dump.blocksRead != 0;
        current_type2_dump = LilyGoNfc.type2Dump();
        current_type2_valid = result.type2ReadAttempted && lilygoNfcType2DumpValid(current_type2_dump);
        current_ndef_length = 0U;
        current_ndef_valid = false;
        if (!current_dump_valid && result.ndefAttempted) {
            const uint8_t *raw = LilyGoNfc.ndefRawMessage(&current_ndef_length);
            current_ndef_valid = result.lastError == ST_ERR_NONE && raw && current_ndef_length != 0U;
        }
        read_finished = true;
        stop_discovery();
        close_prompt();
        char text[128];
        if (current_type2_valid) {
            snprintf(text, sizeof(text), "%s: %u/%u pages%s.",
                     lilygoNfcType2ModelName(current_type2_dump.model), current_type2_dump.pagesRead,
                     current_type2_dump.pageCount, result.type2ReadComplete ? " captured" : " (partial)");
            set_label(status_label, text, result.type2ReadComplete ? UI_COLOR_ACCENT : UI_COLOR_WARNING);
            set_label(hint_label, "Save a .t2t image with the UID, card parameters and captured pages. "
                      "Hidden password/PACK values remain unknown, even when every page is readable.");
        } else if (current_dump_valid) {
            snprintf(text, sizeof(text), "Read %u/%u MIFARE blocks%s.",
                     (unsigned)dump.blocksRead, (unsigned)dump.totalBlocks,
                     dump.complete ? " (complete)" : " (partial)");
            set_label(status_label, text, dump.complete ? UI_COLOR_ACCENT : UI_COLOR_WARNING);
            set_label(hint_label, "Choose SD or internal FAT to save the MIFARE dump.");
        } else if (current_ndef_valid) {
            snprintf(text, sizeof(text), "Read %lu bytes of NDEF data.",
                     static_cast<unsigned long>(current_ndef_length));
            set_label(status_label, text, UI_COLOR_ACCENT);
            set_label(hint_label, "Choose SD or internal FAT to save the raw NDEF message.");
        } else if (result.ndefAttempted && result.ndefSupported &&
                   result.lastError == ST_ERR_NONE && result.ndefMessageLen == 0U) {
            snprintf(text, sizeof(text), "%s / NDEF empty", result.cardType);
            set_label(status_label, text, UI_COLOR_TEXT_PRIMARY);
            snprintf(text, sizeof(text), "NDEF area: %lu bytes; message: 0 bytes.\n"
                     "No NDEF message to save. Save buttons are disabled.",
                     static_cast<unsigned long>(result.ndefAreaLen));
            set_label(hint_label, text, UI_COLOR_TEXT_SECONDARY);
        } else {
            set_label(status_label, result.cardType[0] ? result.cardType : "Card detected",
                     UI_COLOR_WARNING);
            set_label(hint_label, result.errorText[0] ? result.errorText :
                      "This card has no readable MIFARE or NDEF payload.", UI_COLOR_WARNING);
        }
        refresh_sd_state();
    } else if ((mode == NFC_MODE_WRITE || mode == NFC_MODE_ERASE) &&
               result.state != LILYGO_NFC_STATE_POLLING) {
        stop_discovery();
        close_prompt();
        const bool success = result.type2WriteAttempted ? result.type2Write.status == LILYGO_NFC_TYPE2_WRITE_COMPLETE :
                             result.lastError == ST_ERR_NONE && strstr(result.ndefStateText, "complete") != nullptr;
        set_label(status_label, success ? result.ndefStateText : "NFC operation failed",
                  success ? UI_COLOR_ACCENT : UI_COLOR_WARNING);
        if (result.type2WriteAttempted) {
            char text[256];
            if (success) {
                snprintf(text, sizeof(text), "Verified %u changed pages; %u already matched. Target UID and lock/configuration pages were preserved.",
                         result.type2Write.verifiedPages, result.type2Write.unchangedPages);
            } else {
                char page_info[32] = {};
                if (result.type2Write.failedPage != UINT16_MAX) {
                    snprintf(page_info, sizeof(page_info), " Page %u.", result.type2Write.failedPage);
                }
                snprintf(text, sizeof(text), "%s.%s Verified %u pages; %u WRITE commands sent. %s",
                         lilygoNfcType2WriteStatusText(result.type2Write.status), page_info, result.type2Write.verifiedPages,
                         result.type2Write.attemptedPages, result.type2Write.attemptedPages ?
                         "The target may be partially written." : "No pages were written.");
            }
            set_label(hint_label, text, success ? UI_COLOR_TEXT_SECONDARY : UI_COLOR_WARNING);
        } else {
            set_label(hint_label, success ? "Operation completed. Remove the card." :
                      (result.errorText[0] ? result.errorText : "This card or file is not writable."),
                      success ? UI_COLOR_TEXT_SECONDARY : UI_COLOR_WARNING);
        }
    }
}

static void back_cb(lv_event_t *)
{
    close_prompt();
    stop_discovery();
    if (timer) {
        lv_timer_del(timer);
        timer = nullptr;
    }
    clear_content_refs();
    if (page) {
        ui_destroy_app_page(page);
        page = nullptr;
    }
    content = nullptr;
    files.clear();
    if (mode != NFC_MODE_MENU && root_parent) {
        mode = NFC_MODE_MENU;
        build_mode();
    } else {
        root_parent = nullptr;
        menu_show();
    }
}

static void build_mode()
{
    if (mode == NFC_MODE_MENU) {
        show_menu();
        return;
    }
    clear_content_refs();
    page = ui_create_app_page(root_parent, mode == NFC_MODE_READ ? "NFC Read" :
                              mode == NFC_MODE_WRITE ? "NFC Write" :
                              mode == NFC_MODE_EMULATE ? "NFC Emulate" :
                              mode == NFC_MODE_INFO ? "NFC Info" : "NFC Erase",
                              back_cb);
    content = page;
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (mode == NFC_MODE_INFO) { ui_nfc_info_create(content); return; }
    if (mode == NFC_MODE_READ) read_page();
    else if (mode == NFC_MODE_WRITE) write_page();
    else if (mode == NFC_MODE_EMULATE) emulate_page();
    else erase_page();
    if (!timer) timer = lv_timer_create(poll, 150, nullptr);
}

}  // namespace

void ui_nfc_mfc_enter(lv_obj_t *parent)
{
    close_prompt();
    stop_discovery();
    clear_content_refs();
    if (timer) {
        lv_timer_del(timer);
        timer = nullptr;
    }
    if (page) ui_destroy_app_page(page);
    page = nullptr;
    root_parent = parent;
    content = nullptr;
    mode = NFC_MODE_MENU;
    build_mode();
}

void ui_nfc_mfc_exit(lv_obj_t *parent)
{
    (void)parent;
    back_cb(nullptr);
}

app_t ui_nfc_mfc_main = {
    .setup_func_cb = ui_nfc_mfc_enter,
    .exit_func_cb = ui_nfc_mfc_exit,
    .user_data = nullptr,
};

#else

void ui_nfc_mfc_enter(lv_obj_t *) {}
void ui_nfc_mfc_exit(lv_obj_t *) {}
app_t ui_nfc_mfc_main = {
    .setup_func_cb = ui_nfc_mfc_enter,
    .exit_func_cb = ui_nfc_mfc_exit,
    .user_data = nullptr,
};

#endif
