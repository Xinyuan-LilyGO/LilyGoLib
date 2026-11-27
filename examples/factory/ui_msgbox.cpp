/**
 * @file      ui_msgbox.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-25
 *
 * Themed message boxes and full-screen settings with shared input isolation.
 */
#include "ui_define.h"

LV_FONT_DECLARE(font_alibaba_12);
LV_FONT_DECLARE(font_alibaba_24);

namespace {
struct InputBinding { lv_indev_t *input; lv_group_t *group; };
struct MessageBox {
    lv_obj_t *box = nullptr, *content = nullptr, *owner = nullptr, *screen = nullptr;
    lv_obj_t *title = nullptr, *text = nullptr, *close = nullptr, *cancel = nullptr;
    lv_group_t *group = nullptr, *previous_group = nullptr;
    lv_obj_t *previous_focus = nullptr;
    bool previous_editing = false, queued = false, owner_deleting = false, fullscreen = false;
    std::vector<InputBinding> inputs;
};
static std::vector<MessageBox *> dialogs;
static lv_obj_t *observed_screen = nullptr, *observed_top = nullptr;

static void focus_deleted(lv_event_t *event)
{
    auto *object = lv_event_get_target_obj(event);
    for (auto *dialog : dialogs) if (dialog->previous_focus == object) dialog->previous_focus = nullptr;
}

static void root_deleted(lv_event_t *event)
{
    auto *object = lv_event_get_target_obj(event);
    if (observed_screen == object) observed_screen = nullptr;
    if (observed_top == object) observed_top = nullptr;
}

static bool inside(lv_obj_t *object, lv_obj_t *root)
{
    for (; object; object = lv_obj_get_parent(object)) if (object == root) return true;
    return false;
}

static lv_group_t *object_group(lv_obj_t *object)
{
    for (auto i = dialogs.rbegin(); i != dialogs.rend(); ++i) {
        if (inside(object, (*i)->box)) return (*i)->group;
    }
    // Soft keyboards may live beside the modal on the top layer.
    for (auto *p = object; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_check_type(p, &lv_keyboard_class)) return dialogs.back()->group;
    }
    return dialogs.front()->previous_group;
}

static void child_created(lv_event_t *event)
{
    if (dialogs.empty()) return;
    auto *object = static_cast<lv_obj_t *>(lv_event_get_param(event));
    if (!object) return;
    auto *group = lv_obj_get_group(object);
    auto *wanted = object_group(object);
    bool modal_group = false;
    for (auto *dialog : dialogs) modal_group |= group == dialog->group;
    // Background scan results are auto-added to the current default group by
    // LVGL. Move them back before an encoder can navigate to them.
    if (modal_group && group != wanted) {
        if (wanted) lv_group_add_obj(wanted, object);
        else lv_group_remove_obj(object);
    }
    for (auto *dialog : dialogs) {
        if (inside(object, dialog->box)) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
            break;
        }
    }
}

static const lv_font_t *message_font(lv_obj_t *box)
{
    const auto preference = ui_get_font_size_pref();
    const lv_font_t *normal = lv_theme_get_font_normal(box);
    if (!normal) normal = LV_FONT_DEFAULT;
    if (!preference) return normal;
    const auto *font = preference == 1 ? lv_theme_get_font_small(box) :
                       preference >= 3 ? lv_theme_get_font_large(box) : normal;
    if (!font) font = normal;
    // The default theme supplies one face for all three roles. Select available
    // sizes in that same family; custom themes retain their own font roles.
    if (lv_theme_get_font_small(box) == normal && lv_theme_get_font_large(box) == normal) {
        if (normal == &lv_font_montserrat_12 || normal == &lv_font_montserrat_14 ||
            normal == &lv_font_montserrat_16 || normal == &lv_font_montserrat_18 ||
            normal == &lv_font_montserrat_20 || normal == &lv_font_montserrat_22 || normal == &lv_font_montserrat_24)
            font = preference == 1 ? &lv_font_montserrat_12 : preference >= 3 ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
        else if (normal == &font_alibaba_12 || normal == &font_alibaba_24)
            font = preference == 1 ? &font_alibaba_12 : &font_alibaba_24;
    }
    return font;
}

static void style_box(MessageBox *dialog)
{
    auto *box = dialog->box;
    const auto *font = message_font(box);
    const int line = lv_font_get_line_height(font);
    const int pad = lv_display_get_vertical_resolution(nullptr) <= 240 ? 6 : 10;
    if (dialog->fullscreen) {
        lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
        lv_obj_set_style_text_color(box, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(dialog->content, font, 0);
        lv_obj_set_style_text_font(dialog->title, font, 0);
        lv_obj_set_style_text_color(dialog->title, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_bg_color(dialog->close, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_text_color(dialog->close, UI_COLOR_ACCENT, 0);
        return;
    }
    lv_obj_set_style_bg_color(box, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(box, font, 0);
    lv_obj_set_style_text_color(box, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_pad_all(box, pad, 0); lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_set_style_border_width(box, 1, 0); lv_obj_set_style_border_color(box, UI_COLOR_DIVIDER, 0);
    auto *backdrop = lv_obj_get_parent(box);
    lv_obj_set_style_bg_color(backdrop, UI_COLOR_BG, 0); lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    auto *header = lv_msgbox_get_header(box);
    lv_obj_set_height(header, LV_MAX(28, line + 8));
    lv_obj_set_style_pad_all(header, 0, 0); lv_obj_set_style_pad_column(header, 6, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_text_font(dialog->title, font, 0); lv_obj_set_style_text_color(dialog->title, UI_COLOR_ACCENT, 0);
    lv_obj_set_height(dialog->title, line);
    lv_obj_set_size(dialog->close, 26, 26); lv_obj_set_style_pad_all(dialog->close, 2, 0);
    lv_obj_set_style_bg_color(dialog->close, UI_COLOR_CARD_BG, 0);
    auto *content = lv_msgbox_get_content(box);
    lv_obj_set_style_pad_all(content, 4, 0); lv_obj_set_style_pad_row(content, 4, 0);
    lv_obj_set_style_border_width(content, 2, 0); lv_obj_set_style_border_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(content, 8, 0); lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(content, font, 0);
    lv_obj_set_style_bg_color(content, UI_COLOR_ACCENT, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(content, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_text_font(dialog->text, font, 0); lv_obj_set_style_text_color(dialog->text, UI_COLOR_TEXT_PRIMARY, 0);
    if (auto *footer = lv_msgbox_get_footer(box)) {
        lv_obj_set_height(footer, LV_MAX(32, line + 14));
        lv_obj_set_style_pad_all(footer, 0, 0); lv_obj_set_style_pad_column(footer, 6, 0);
        lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(footer, 0, 0);
        for (uint32_t i = 0; i < lv_obj_get_child_count(footer); ++i) {
            auto *button = lv_obj_get_child(footer, i);
            lv_obj_set_style_text_font(button, font, 0);
            lv_obj_set_style_bg_color(button, UI_COLOR_CARD_FOCUS, 0);
            lv_obj_set_style_text_color(button, UI_COLOR_TEXT_PRIMARY, 0);
        }
    }
}

static void close_layer(MessageBox *dialog)
{
    if (dialog->fullscreen) lv_obj_delete(dialog->box);
    else lv_msgbox_close(dialog->box);
}

static void close_clicked(lv_event_t *event)
{
    auto *dialog = static_cast<MessageBox *>(lv_event_get_user_data(event));
    if (dialog->cancel != dialog->close) {
        lv_obj_send_event(dialog->cancel, LV_EVENT_CLICKED, nullptr);
    } else close_layer(dialog);
}

static void close_async(void *data)
{
    auto *dialog = static_cast<MessageBox *>(data);
    dialog->queued = false;
    lv_obj_send_event(dialog->close, LV_EVENT_CLICKED, nullptr);
}

static void key_event(lv_event_t *event)
{
    auto *dialog = static_cast<MessageBox *>(lv_event_get_user_data(event));
    const auto key = lv_event_get_key(event);
    auto *focused = lv_group_get_focused(dialog->group);
    lv_event_stop_bubbling(event);
    if (focused == dialog->content && key == LV_KEY_ENTER && lv_group_get_editing(dialog->group)) {
        lv_group_set_editing(dialog->group, false);
        lv_event_stop_processing(event);
    } else if (key == LV_KEY_ESC || (key == LV_KEY_BACKSPACE && hw_get_keyboard_navigation_enabled() &&
               (!focused || !lv_obj_check_type(focused, &lv_textarea_class)))) {
        lv_event_stop_processing(event);
        if (auto *input = lv_indev_active()) lv_indev_wait_release(input);
        if (!dialog->queued) dialog->queued = lv_async_call(close_async, dialog) == LV_RESULT_OK;
    }
}

static void owner_deleted(lv_event_t *event)
{
    auto *dialog = static_cast<MessageBox *>(lv_event_get_user_data(event));
    dialog->owner_deleting = true;
    close_layer(dialog);
}

static void deleted(lv_event_t *event)
{
    auto *dialog = static_cast<MessageBox *>(lv_event_get_user_data(event));
    if (dialog->queued) lv_async_call_cancel(close_async, dialog);
    if (dialog->owner && !dialog->owner_deleting)
        lv_obj_remove_event_cb_with_user_data(dialog->owner, owner_deleted, dialog);
    const bool top = !dialogs.empty() && dialogs.back() == dialog;
    for (auto *other : dialogs) {
        if (other == dialog) continue;
        if (other->previous_group == dialog->group) {
            other->previous_group = dialog->previous_group;
            other->previous_focus = dialog->previous_focus;
            other->previous_editing = dialog->previous_editing;
        }
        for (auto &binding : other->inputs) if (binding.group == dialog->group) {
            binding.group = dialog->previous_group;
            for (const auto &saved : dialog->inputs) if (saved.input == binding.input) binding.group = saved.group;
        }
    }
    for (auto i = dialogs.begin(); i != dialogs.end(); ++i) if (*i == dialog) { dialogs.erase(i); break; }
    if (lv_group_get_default() == dialog->group) lv_group_set_default(dialog->previous_group);
    for (const auto &binding : dialog->inputs) {
        for (auto *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
            if (input == binding.input && lv_indev_get_group(input) == dialog->group) {
                lv_indev_wait_release(input); lv_indev_set_group(input, binding.group);
            }
        }
    }
    if (top && !dialog->owner_deleting && dialog->previous_focus &&
        lv_obj_get_group(dialog->previous_focus) == dialog->previous_group) {
        lv_group_focus_obj(dialog->previous_focus);
        lv_group_set_editing(dialog->previous_group, dialog->previous_editing);
    }
    bool focus_watched = false;
    for (auto *other : dialogs) focus_watched |= other->previous_focus == dialog->previous_focus;
    if (dialog->previous_focus && !focus_watched) lv_obj_remove_event_cb(dialog->previous_focus, focus_deleted);
    if (dialogs.empty()) {
        for (auto *root : {observed_screen, observed_top}) if (root) {
            lv_obj_remove_event_cb(root, child_created);
            lv_obj_remove_event_cb(root, root_deleted);
        }
        observed_screen = observed_top = nullptr;
    }
    lv_group_delete(dialog->group);
    delete dialog;
}
} // namespace

void destroy_msgbox(lv_obj_t *box)
{
    // Never dereference an already closed box retained by a page's cleanup.
    for (auto *dialog : dialogs) if (!dialog->fullscreen && dialog->box == box) { lv_msgbox_close(box); return; }
}

namespace {
static MessageBox *begin_layer(lv_obj_t *parent)
{
    auto *dialog = new MessageBox;
    dialog->previous_group = lv_group_get_default();
    dialog->previous_focus = dialog->previous_group ? lv_group_get_focused(dialog->previous_group) : nullptr;
    if (dialog->previous_focus) {
        lv_obj_remove_event_cb(dialog->previous_focus, focus_deleted);
        lv_obj_add_event_cb(dialog->previous_focus, focus_deleted, LV_EVENT_DELETE, nullptr);
    }
    dialog->previous_editing = dialog->previous_group && lv_group_get_editing(dialog->previous_group);
    dialog->screen = lv_screen_active(); dialog->owner = parent ? parent : dialog->screen;
    if (dialog->owner == dialog->screen && dialog->previous_focus && inside(dialog->previous_focus, dialog->screen)) {
        dialog->owner = dialog->previous_focus;
        while (lv_obj_get_parent(dialog->owner) != dialog->screen) dialog->owner = lv_obj_get_parent(dialog->owner);
    }
    dialog->group = lv_group_create();
    for (auto *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
        const auto type = lv_indev_get_type(input);
        if (type == LV_INDEV_TYPE_ENCODER || type == LV_INDEV_TYPE_KEYPAD || type == LV_INDEV_TYPE_POINTER) {
            dialog->inputs.push_back({input, lv_indev_get_group(input)});
            lv_indev_set_group(input, dialog->group); lv_indev_wait_release(input);
        }
    }
    lv_group_set_default(dialog->group);
    return dialog;
}

static void attach_layer(MessageBox *dialog)
{
    if (dialogs.empty()) {
        observed_screen = dialog->screen; observed_top = lv_layer_top();
        lv_obj_add_event_cb(observed_screen, child_created, LV_EVENT_CHILD_CREATED, nullptr);
        lv_obj_add_event_cb(observed_top, child_created, LV_EVENT_CHILD_CREATED, nullptr);
        lv_obj_add_event_cb(observed_screen, root_deleted, LV_EVENT_DELETE, nullptr);
        lv_obj_add_event_cb(observed_top, root_deleted, LV_EVENT_DELETE, nullptr);
    }
    dialogs.push_back(dialog);
    lv_obj_add_event_cb(dialog->box, deleted, LV_EVENT_DELETE, dialog);
    lv_obj_add_event_cb(dialog->owner, owner_deleted, LV_EVENT_DELETE, dialog);
    lv_obj_add_event_cb(dialog->box, key_event, LV_EVENT_KEY, dialog);
    lv_obj_remove_flag(dialog->box, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_SCROLLABLE));
}
} // namespace

lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt, const char *msg_txt,
                        const char **buttons, lv_event_cb_t callback, void *user_data)
{
    auto *dialog = begin_layer(parent);
    dialog->box = lv_msgbox_create(nullptr);
    attach_layer(dialog);
    lv_obj_set_size(dialog->box, LV_PCT(96), LV_PCT(94));
    dialog->title = lv_msgbox_add_title(dialog->box, title_txt ? title_txt : "");
    lv_obj_set_width(dialog->title, 1); lv_obj_set_flex_grow(dialog->title, 1);
    lv_label_set_long_mode(dialog->title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    dialog->close = lv_msgbox_add_header_button(dialog->box, nullptr);
    lv_obj_set_style_radius(dialog->close, 6, 0);
    auto *icon = lv_label_create(dialog->close); lv_label_set_text(icon, LV_SYMBOL_CLOSE); lv_obj_center(icon);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
    ui_add_accent_focus_style(dialog->close);
    lv_obj_add_event_cb(dialog->close, close_clicked, LV_EVENT_CLICKED, dialog);
    dialog->cancel = dialog->close;
    dialog->text = lv_msgbox_add_text(dialog->box, msg_txt ? msg_txt : "");
    lv_label_set_long_mode(dialog->text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(dialog->text, LV_PCT(100));
    auto *content = lv_msgbox_get_content(dialog->box);
    dialog->content = content;
    lv_obj_set_style_radius(content, 8, 0);
    lv_obj_add_flag(content, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_EVENT_BUBBLE));
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER); lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    ui_add_accent_focus_style(content); lv_group_add_obj(dialog->group, content);
    for (size_t i = 0; buttons && buttons[i] && buttons[i][0]; ++i) {
        auto *button = lv_msgbox_add_footer_button(dialog->box, buttons[i]);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_size(button, 1, LV_PCT(100)); lv_obj_set_flex_grow(button, 1);
        ui_add_accent_focus_style(button);
        if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
        if (callback) dialog->cancel = button;
    }
    if (dialog->cancel == dialog->close && callback) {
        lv_obj_remove_event_cb(dialog->close, close_clicked);
        lv_obj_add_event_cb(dialog->close, callback, LV_EVENT_CLICKED, user_data);
    }
    style_box(dialog);
    lv_obj_update_layout(dialog->box);
    lv_group_focus_obj(content); lv_group_set_editing(dialog->group, false);
    return dialog->box;
}

lv_obj_t *ui_create_settings_page(lv_obj_t *owner, const char *title)
{
    auto *dialog = begin_layer(owner);
    dialog->fullscreen = true;
    // A regular opaque page covers the app, with no popup backdrop or margins.
    dialog->box = lv_obj_create(lv_layer_top());
    attach_layer(dialog);
    auto *root = dialog->box;
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, 0, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    auto *header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 6, 0);
    lv_obj_set_style_pad_column(header, 8, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(header, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    dialog->close = lv_button_create(header);
    lv_obj_set_size(dialog->close, 28, 28);
    lv_obj_set_style_pad_all(dialog->close, 0, 0);
    lv_obj_set_style_radius(dialog->close, LV_RADIUS_CIRCLE, 0);
    ui_add_accent_focus_style(dialog->close);
    auto *icon = lv_label_create(dialog->close);
    lv_label_set_text(icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_center(icon);
    dialog->cancel = dialog->close;
    lv_obj_add_event_cb(dialog->close, close_clicked, LV_EVENT_CLICKED, dialog);
    dialog->title = lv_label_create(header);
    lv_label_set_text(dialog->title, title ? title : "Settings");
    lv_obj_set_width(dialog->title, 0); lv_obj_set_flex_grow(dialog->title, 1);
    lv_label_set_long_mode(dialog->title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    auto *content = lv_obj_create(root);
    dialog->content = content;
    lv_obj_set_user_data(content, root);
    lv_obj_set_size(content, LV_PCT(100), 0); lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_remove_flag(content, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
    style_box(dialog);
    lv_obj_update_layout(root);
    lv_group_focus_obj(dialog->close);
    return content;
}

void ui_destroy_settings_page(lv_obj_t *content)
{
    for (auto *dialog : dialogs) {
        if (dialog->fullscreen && dialog->content == content) { close_layer(dialog); return; }
    }
}

void ui_msgbox_refresh()
{
    for (auto *dialog : dialogs) style_box(dialog);
}
