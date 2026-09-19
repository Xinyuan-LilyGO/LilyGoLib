/**
 * @file      ui_tools.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

LV_FONT_DECLARE(font_alibaba_12);
LV_FONT_DECLARE(font_alibaba_24);

static lv_group_t *msg_group = NULL;
static lv_group_t *prev_group;

#define UI_NAV_AUTO_HIDE_HOT_ZONE_PX    28
#define UI_NAV_AUTO_HIDE_SWIPE_MIN_PX   8

typedef struct {
    lv_obj_t *root;
    lv_obj_t *content;
    lv_obj_t *nav;
    lv_obj_t *divider;
    lv_timer_t *timer;
    uint32_t timeout_ms;
    bool hidden;
    bool enabled;
} ui_nav_auto_hide_t;

#if defined(ARDUINO_T_LORA_PAGER)
typedef struct {
    lv_obj_t *back_button;
    lv_obj_t *top_layer;
    lv_indev_t *keyboard;
    bool back_pending;
} ui_keyboard_navigation_t;

static lv_key_t ui_app_page_keyboard_remap_cb(lv_indev_t *indev, lv_key_t key)
{
    (void)indev;
    if (hw_get_keyboard_navigation_enabled() && key == LV_KEY_BACKSPACE) {
        return LV_KEY_ESC;
    }
    return key;
}

static void ui_app_page_keyboard_back_async_cb(void *user_data)
{
    ui_keyboard_navigation_t *state = (ui_keyboard_navigation_t *)user_data;
    if (!state) return;

    state->back_pending = false;
    if (state->back_button && lv_obj_is_valid(state->back_button)) {
        lv_obj_send_event(state->back_button, LV_EVENT_CLICKED, state->keyboard);
    }
}

static void ui_app_page_keyboard_event_cb(lv_event_t *e)
{
    ui_keyboard_navigation_t *state = (ui_keyboard_navigation_t *)lv_event_get_user_data(e);
    if (!state || !hw_get_keyboard_navigation_enabled() ||
            !state->back_button || !lv_obj_is_valid(state->back_button)) {
        return;
    }
    if (lv_event_get_key(e) != LV_KEY_ESC) {
        return;
    }

    lv_event_stop_processing(e);
    lv_indev_wait_release(state->keyboard);
    if (!state->back_pending) {
        state->back_pending =
            lv_async_call(ui_app_page_keyboard_back_async_cb, state) == LV_RESULT_OK;
    }
}

static lv_obj_tree_walk_res_t ui_app_page_keyboard_bubble_walk_cb(lv_obj_t *obj, void *user_data)
{
    lv_obj_t *root = (lv_obj_t *)user_data;
    if (obj != root) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static void ui_app_page_keyboard_child_created_cb(lv_event_t *e)
{
    lv_obj_t *child = (lv_obj_t *)lv_event_get_param(e);
    lv_obj_t *root = lv_event_get_current_target_obj(e);
    if (child && root) {
        lv_obj_tree_walk(child, ui_app_page_keyboard_bubble_walk_cb, root);
    }
}

static void ui_app_page_keyboard_delete_cb(lv_event_t *e)
{
    ui_keyboard_navigation_t *state = (ui_keyboard_navigation_t *)lv_event_get_user_data(e);
    if (!state) return;

    if (state->back_pending) {
        lv_async_call_cancel(ui_app_page_keyboard_back_async_cb, state);
        state->back_pending = false;
    }
    if (state->keyboard) {
        lv_indev_set_key_remap_cb(state->keyboard, NULL);
    }
    if (state->top_layer && lv_obj_is_valid(state->top_layer)) {
        lv_obj_remove_event_cb_with_user_data(state->top_layer,
                                              ui_app_page_keyboard_event_cb, state);
        lv_obj_remove_event_cb_with_user_data(state->top_layer,
                                              ui_app_page_keyboard_child_created_cb, state);
    }
    lv_free(state);
}

static void ui_app_page_enable_keyboard_navigation(lv_obj_t *root, lv_obj_t *back_button)
{
    lv_indev_t *keyboard = lv_get_keyboard_indev();
    if (!root || !back_button || !keyboard) return;

    ui_keyboard_navigation_t *state =
        (ui_keyboard_navigation_t *)lv_malloc(sizeof(ui_keyboard_navigation_t));
    if (!state) return;

    state->back_button = back_button;
    state->top_layer = lv_layer_top();
    state->keyboard = keyboard;
    state->back_pending = false;
    lv_indev_set_key_remap_cb(keyboard, ui_app_page_keyboard_remap_cb);
    lv_obj_add_event_cb(root, ui_app_page_keyboard_event_cb, LV_EVENT_KEY, state);
    lv_obj_add_event_cb(root, ui_app_page_keyboard_child_created_cb, LV_EVENT_CHILD_CREATED, state);
    lv_obj_add_event_cb(root, ui_app_page_keyboard_delete_cb, LV_EVENT_DELETE, state);
    lv_obj_tree_walk(root, ui_app_page_keyboard_bubble_walk_cb, root);
    if (state->top_layer) {
        lv_obj_add_event_cb(state->top_layer, ui_app_page_keyboard_event_cb, LV_EVENT_KEY, state);
        lv_obj_add_event_cb(state->top_layer, ui_app_page_keyboard_child_created_cb,
                            LV_EVENT_CHILD_CREATED, state);
        lv_obj_tree_walk(state->top_layer, ui_app_page_keyboard_bubble_walk_cb, state->top_layer);
    }
}
#endif

static int32_t ui_app_page_bottom_bar_height(void)
{
    return 0;
}

static void ui_app_page_nav_set_hidden(ui_nav_auto_hide_t *state, bool hidden)
{
    if (!state || !state->content || !state->nav || !state->divider) return;
    if (!lv_obj_is_valid(state->content) || !lv_obj_is_valid(state->nav) || !lv_obj_is_valid(state->divider)) return;
    if (state->hidden == hidden) return;

    state->hidden = hidden;
    const int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    const int32_t bottom_bar_h = ui_app_page_bottom_bar_height();

    if (hidden) {
        lv_obj_add_flag(state->nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->divider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(state->content, screen_h - bottom_bar_h);
        lv_obj_align(state->content, LV_ALIGN_TOP_MID, 0, 0);
    } else {
        lv_obj_remove_flag(state->nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->divider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(state->content, screen_h - 33 - bottom_bar_h);
        lv_obj_align(state->content, LV_ALIGN_TOP_MID, 0, 33);
    }
}

static void ui_app_page_nav_auto_hide_timer_cb(lv_timer_t *timer)
{
    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_timer_get_user_data(timer);
    if (!state || !state->enabled) return;
    if (!state->content || !state->nav || !state->divider ||
            !lv_obj_is_valid(state->content) ||
            !lv_obj_is_valid(state->nav) ||
            !lv_obj_is_valid(state->divider)) {
        lv_timer_pause(timer);
        return;
    }
    ui_app_page_nav_set_hidden(state, true);
}

static void ui_app_page_nav_activity_cb(lv_event_t *e)
{
    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_event_get_user_data(e);
    if (!state) return;
    if (!state->enabled) return;

    bool should_show = !state->hidden;
    lv_event_code_t code = lv_event_get_code(e);

    if (state->hidden) {
        should_show = false;

        if (code == LV_EVENT_PRESSED) {
            lv_indev_t *indev = lv_event_get_indev(e);
            lv_point_t point = {};
            if (indev) {
                lv_indev_get_point(indev, &point);
                should_show = point.y <= UI_NAV_AUTO_HIDE_HOT_ZONE_PX;
            }
        } else if (code == LV_EVENT_SCROLL || code == LV_EVENT_GESTURE) {
            lv_indev_t *indev = lv_event_get_indev(e);
            if (indev) {
                if (code == LV_EVENT_GESTURE) {
                    should_show = lv_indev_get_gesture_dir(indev) == LV_DIR_TOP;
                } else {
                    lv_point_t vect = {};
                    lv_indev_get_vect(indev, &vect);
                    should_show = vect.y < -UI_NAV_AUTO_HIDE_SWIPE_MIN_PX;
                }
            }
        } else if (code == LV_EVENT_KEY) {
            should_show = true;
        }
    }

    if (!should_show) return;

    ui_app_page_nav_set_hidden(state, false);
    if (state->timer) {
        lv_timer_reset(state->timer);
    }
}

static void ui_app_page_nav_release(ui_nav_auto_hide_t *state)
{
    if (!state) return;

    if (state->timer) {
        lv_timer_del(state->timer);
        state->timer = NULL;
    }
    if (state->content && lv_obj_is_valid(state->content)) {
        lv_obj_remove_event_cb_with_user_data(state->content, NULL, state);
    }
    if (state->root && lv_obj_is_valid(state->root)) {
        lv_obj_set_user_data(state->root, NULL);
    }
    state->root = NULL;
    state->content = NULL;
    state->nav = NULL;
    state->divider = NULL;
    state->enabled = false;
    lv_free(state);
}

static void ui_app_page_nav_delete_cb(lv_event_t *e)
{
    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_event_get_user_data(e);
    ui_app_page_nav_release(state);
}

static lv_obj_tree_walk_res_t ui_app_page_nav_bubble_walk_cb(lv_obj_t *obj, void *user_data)
{
    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)user_data;
    if (state && obj != state->content) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static void ui_app_page_nav_child_created_cb(lv_event_t *e)
{
    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_event_get_user_data(e);
    lv_obj_t *child = (lv_obj_t *)lv_event_get_param(e);
    if (!state || !child) return;

    lv_obj_tree_walk(child, ui_app_page_nav_bubble_walk_cb, state);
}

#define UI_SLIDER_SCROLL_LOCK_MAX 8
static lv_obj_t *ui_slider_scroll_locks[UI_SLIDER_SCROLL_LOCK_MAX];
static uint8_t ui_slider_scroll_lock_count = 0;

static lv_indev_type_t ui_event_indev_type(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        indev = lv_indev_active();
    }
    return indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
}

static void ui_slider_unlock_parent_scroll(void)
{
    for (uint8_t i = 0; i < ui_slider_scroll_lock_count; ++i) {
        lv_obj_t *obj = ui_slider_scroll_locks[i];
        if (obj && lv_obj_is_valid(obj)) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        }
        ui_slider_scroll_locks[i] = NULL;
    }
    ui_slider_scroll_lock_count = 0;
}

static void ui_slider_lock_parent_scroll(lv_obj_t *slider)
{
    ui_slider_unlock_parent_scroll();

    lv_obj_t *obj = lv_obj_get_parent(slider);
    while (obj && ui_slider_scroll_lock_count < UI_SLIDER_SCROLL_LOCK_MAX) {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) {
            ui_slider_scroll_locks[ui_slider_scroll_lock_count++] = obj;
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        }
        obj = lv_obj_get_parent(obj);
    }
}

static void ui_slider_encoder_guard_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_type_t indev_type = ui_event_indev_type(e);

    switch (code) {
    case LV_EVENT_FOCUSED:
        lv_obj_scroll_to_view_recursive(slider, LV_ANIM_ON);
        if (indev_type == LV_INDEV_TYPE_ENCODER || indev_type == LV_INDEV_TYPE_KEYPAD) {
            ui_slider_lock_parent_scroll(slider);
        }
        break;
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
        ui_slider_lock_parent_scroll(slider);
        break;
    case LV_EVENT_VALUE_CHANGED: {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(slider);
        if (lv_group_get_editing(group) ||
                indev_type == LV_INDEV_TYPE_ENCODER ||
                indev_type == LV_INDEV_TYPE_KEYPAD) {
            ui_slider_lock_parent_scroll(slider);
        }
        break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_CANCEL:
        if (indev_type == LV_INDEV_TYPE_POINTER) {
            ui_slider_unlock_parent_scroll();
        }
        break;
    case LV_EVENT_KEY: {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC) {
            lv_group_t *group = (lv_group_t *)lv_obj_get_group(slider);
            if (group) {
                lv_group_set_editing(group, false);
            }
        }
        break;
    }
    case LV_EVENT_DEFOCUSED: {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(slider);
        if (group) {
            lv_group_set_editing(group, false);
        }
        ui_slider_unlock_parent_scroll();
        break;
    }
    case LV_EVENT_DELETE:
        ui_slider_unlock_parent_scroll();
        break;
    default:
        break;
    }

    /* LVGL slider re-enables scroll-chain on size/release; keep it local. */
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
}

void ui_prepare_slider_for_encoder(lv_obj_t *slider)
{
    if (!slider || !lv_obj_check_type(slider, &lv_slider_class)) return;

    lv_group_t *group = lv_group_get_default();
    if (group && lv_obj_get_group(slider) != group) {
        lv_group_add_obj(group, slider);
    }
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_margin_left(slider, is_screen_small() ? 0 : 8, 0);
    lv_obj_set_style_margin_right(slider, is_screen_small() ? 0 : 8, 0);
    lv_obj_remove_event_cb(slider, ui_slider_encoder_guard_event_cb);
    lv_obj_add_event_cb(slider, ui_slider_encoder_guard_event_cb, LV_EVENT_ALL, NULL);
}

static lv_obj_t *ui_active_textarea = NULL;

static void ui_textarea_begin_edit(lv_obj_t *textarea)
{
    if (!textarea || !lv_obj_is_valid(textarea)) return;

    ui_active_textarea = textarea;
    enable_keyboard();
}

static void ui_textarea_end_edit(lv_obj_t *textarea)
{
    if (textarea && lv_obj_is_valid(textarea)) {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(textarea);
        if (group) {
            lv_group_set_editing(group, false);
        }
    }
    if (!textarea || ui_active_textarea == textarea) {
        ui_active_textarea = NULL;
    }
    disable_keyboard();
}

static void ui_textarea_encoder_guard_event_cb(lv_event_t *e)
{
    lv_obj_t *textarea = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_type_t indev_type = ui_event_indev_type(e);

    switch (code) {
    case LV_EVENT_FOCUSED:
        lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_ON);
        if (lv_obj_has_state(textarea, LV_STATE_EDITED)) {
            ui_textarea_begin_edit(textarea);
        }
        break;
    case LV_EVENT_CLICKED:
        if ((indev_type == LV_INDEV_TYPE_ENCODER || indev_type == LV_INDEV_TYPE_KEYPAD) &&
                lv_obj_has_state(textarea, LV_STATE_EDITED)) {
            ui_textarea_end_edit(textarea);
        } else if (indev_type == LV_INDEV_TYPE_POINTER) {
            ui_textarea_begin_edit(textarea);
        }
        break;
    case LV_EVENT_STATE_CHANGED:
        if (lv_obj_has_state(textarea, LV_STATE_FOCUSED) &&
                lv_obj_has_state(textarea, LV_STATE_EDITED)) {
            ui_textarea_begin_edit(textarea);
        } else if (ui_active_textarea == textarea &&
                   !lv_obj_has_state(textarea, LV_STATE_EDITED)) {
            ui_textarea_end_edit(textarea);
        }
        break;
    case LV_EVENT_READY:
    case LV_EVENT_CANCEL: {
        ui_textarea_end_edit(textarea);
        break;
    }
    case LV_EVENT_KEY: {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC) {
            ui_textarea_end_edit(textarea);
            lv_event_stop_processing(e);
        }
        break;
    }
    case LV_EVENT_DEFOCUSED:
    case LV_EVENT_LEAVE:
        ui_textarea_end_edit(textarea);
        break;
    case LV_EVENT_DELETE:
        if (ui_active_textarea == textarea) {
            ui_active_textarea = NULL;
            disable_keyboard();
        }
        break;
    default:
        break;
    }

    lv_obj_remove_flag(textarea, LV_OBJ_FLAG_SCROLL_CHAIN);
}

void ui_style_textarea(lv_obj_t *textarea)
{
    if (!textarea || !lv_obj_check_type(textarea, &lv_textarea_class)) return;

    lv_textarea_set_text_selection(textarea, false);
    lv_textarea_set_cursor_click_pos(textarea, false);
    lv_obj_set_scrollbar_mode(textarea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(textarea, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_bg_color(textarea, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(textarea, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(textarea, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_color(textarea, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_radius(textarea, 7, 0);
    lv_obj_set_style_bg_color(textarea, UI_COLOR_TEXT_PRIMARY, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER,
                            static_cast<lv_style_selector_t>(LV_PART_CURSOR) |
                            static_cast<lv_style_selector_t>(LV_STATE_EDITED));
}

void ui_prepare_textarea_for_encoder(lv_obj_t *textarea)
{
    if (!textarea || !lv_obj_check_type(textarea, &lv_textarea_class)) return;

    ui_style_textarea(textarea);
    lv_group_t *group = lv_group_get_default();
    if (group && lv_obj_get_group(textarea) != group) {
        lv_group_add_obj(group, textarea);
    }
    lv_obj_add_flag(textarea, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(textarea, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scrollbar_mode(textarea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_event_cb(textarea, ui_textarea_encoder_guard_event_cb);
    lv_obj_add_event_cb(textarea, ui_textarea_encoder_guard_event_cb, LV_EVENT_ALL, NULL);
}

bool ui_soft_keyboard_should_open(void)
{
#if defined(USING_TOUCHPAD) || defined(USING_INPUT_DEV_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
    return !hw_has_keyboard();
#else
    return false;
#endif
}

void ui_soft_keyboard_restore(ui_soft_keyboard_lift_t *lift)
{
    if (!lift || !lift->lifted_obj) return;

    lv_obj_t *obj = lift->lifted_obj;
    lv_obj_t *parent = lift->original_parent;
    if (lv_obj_is_valid(obj) && parent && lv_obj_is_valid(parent) && lv_obj_get_parent(obj) != parent) {
        lv_obj_set_parent(obj, parent);
        if (lift->original_index >= 0) {
            lv_obj_move_to_index(obj, lift->original_index);
        }
        lv_obj_set_width(obj, LV_PCT(100));
        lv_obj_update_layout(parent);
    }
    lift->lifted_obj = NULL;
    lift->original_parent = NULL;
    lift->original_index = -1;
}

void ui_soft_keyboard_show(lv_obj_t *keyboard, lv_obj_t *textarea,
                           lv_obj_t *lift_obj, ui_soft_keyboard_lift_t *lift)
{
    if (!ui_soft_keyboard_should_open() || !keyboard || !textarea) return;

    if (lv_obj_get_parent(keyboard) != lv_layer_top()) {
        lv_obj_set_parent(keyboard, lv_layer_top());
    }
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(45));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(keyboard);

    if (lift && lift_obj) {
        if (lift->lifted_obj && lift->lifted_obj != lift_obj) {
            ui_soft_keyboard_restore(lift);
        }
        if (!lift->lifted_obj) {
            lift->lifted_obj = lift_obj;
            lift->original_parent = lv_obj_get_parent(lift_obj);
            lift->original_index = lv_obj_get_index(lift_obj);
        }
        if (lv_obj_get_parent(lift_obj) != lv_layer_top()) {
            lv_obj_set_parent(lift_obj, lv_layer_top());
        }
        lv_obj_set_width(lift_obj, LV_PCT(100));
        lv_obj_align(lift_obj, LV_ALIGN_BOTTOM_MID, 0, -lv_obj_get_height(keyboard));
        lv_obj_move_foreground(lift_obj);
    }
    lv_obj_move_foreground(keyboard);
}

void ui_soft_keyboard_hide(lv_obj_t *keyboard, ui_soft_keyboard_lift_t *lift)
{
    if (keyboard && lv_obj_is_valid(keyboard)) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    ui_soft_keyboard_restore(lift);
}

static void ui_dropdown_encoder_guard_event_cb(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code) {
    case LV_EVENT_FOCUSED:
        lv_obj_scroll_to_view_recursive(dropdown, LV_ANIM_ON);
        ui_slider_lock_parent_scroll(dropdown);
        break;
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
    case LV_EVENT_KEY:
    case LV_EVENT_ROTARY:
        ui_slider_lock_parent_scroll(dropdown);
        break;
    case LV_EVENT_RELEASED:
        if (lv_dropdown_is_open(dropdown)) {
            ui_slider_lock_parent_scroll(dropdown);
        } else {
            ui_slider_unlock_parent_scroll();
        }
        break;
    case LV_EVENT_READY:
    case LV_EVENT_CANCEL:
    case LV_EVENT_DEFOCUSED:
    case LV_EVENT_LEAVE:
    case LV_EVENT_DELETE:
        ui_slider_unlock_parent_scroll();
        break;
    default:
        break;
    }

    lv_obj_t *list = lv_dropdown_get_list(dropdown);
    if (list) {
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN);
    }
}

static void ui_prepare_dropdown_for_encoder(lv_obj_t *dropdown)
{
    if (!dropdown || !lv_obj_check_type(dropdown, &lv_dropdown_class)) return;

    lv_group_t *group = lv_group_get_default();
    if (group && lv_obj_get_group(dropdown) != group) {
        lv_group_add_obj(group, dropdown);
    }
    lv_obj_add_flag(dropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_t *list = lv_dropdown_get_list(dropdown);
    if (list) {
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN);
    }
    lv_obj_remove_event_cb(dropdown, ui_dropdown_encoder_guard_event_cb);
    lv_obj_add_event_cb(dropdown, ui_dropdown_encoder_guard_event_cb, LV_EVENT_ALL, NULL);
}


lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cont, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_center(cont);

    lv_obj_t *bar = lv_bar_create(cont);
    lv_obj_set_size(bar, 200, 8);
    lv_obj_center(bar);
    lv_obj_set_user_data(bar, cont);
    lv_bar_set_value(bar, 0, LV_ANIM_ON);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, LV_PART_MAIN);
    lv_obj_align_to(label, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    return bar;
}

lv_obj_t *ui_create_option(lv_obj_t *parent, const char *title, const char *symbol_txt, lv_obj_t *(*widget_create)(lv_obj_t *parent), lv_event_cb_t btn_event_cb)
{
    lv_obj_t *cont;
    lv_obj_t *label;
    lv_obj_t *obj;
    lv_obj_t *btn;
    cont = lv_menu_cont_create(parent);

    label = lv_label_create(cont);
    lv_obj_set_width(label, lv_pct(25));
    lv_label_set_text(label, title);
    obj = widget_create(cont);
    if (symbol_txt) {
        lv_obj_set_size(obj, lv_pct(55), 40);
    } else {
        lv_obj_set_size(obj, lv_pct(65), 40);
    }
    lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUS_KEY);

    if (symbol_txt) {
        btn = lv_btn_create(cont);
        if (btn_event_cb) {
            lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_set_size(btn, lv_pct(12), 40);
        label = lv_label_create(btn);
        lv_obj_center(label);
        lv_label_set_text(label, symbol_txt);
    }
    return cont;
}

void destroy_msgbox(lv_obj_t *msgbox)
{
    lv_msgbox_close(msgbox);
    set_default_group(prev_group);
}

static void ui_msgbox_keyboard_back_async_cb(void *user_data)
{
    lv_obj_t *button = (lv_obj_t *)user_data;
    if (button && lv_obj_is_valid(button)) {
        lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    }
}

static void ui_msgbox_keyboard_back_cb(lv_event_t *e)
{
    if (!hw_get_keyboard_navigation_enabled()) return;

    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE && key != LV_KEY_ESC) return;

    lv_obj_t *cancel_button = (lv_obj_t *)lv_event_get_user_data(e);
    if (!cancel_button || !lv_obj_is_valid(cancel_button)) return;

    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_wait_release(indev);
    }
    lv_async_call(ui_msgbox_keyboard_back_async_cb, cancel_button);
}

lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data)
{

    prev_group = lv_group_get_default();

    if (!msg_group) {
        msg_group = lv_group_create();
    }
    set_default_group(msg_group);

    lv_obj_t *msgbox;

    msgbox = lv_msgbox_create(NULL);
    lv_obj_t *title_obj = lv_msgbox_add_title(msgbox, title_txt);
    lv_obj_t *text_obj = lv_msgbox_add_text(msgbox, msg_txt);
    /* Give multi-line notices enough room on small displays. */
    lv_obj_set_size(msgbox, lv_pct(94), lv_pct(78));

    if (text_obj) {
        /* LVGL 9 does not enable wrapping in lv_msgbox_add_text(). */
        lv_label_set_long_mode(text_obj, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text_obj, LV_PCT(100));
    }

    lv_obj_t *content_obj = lv_msgbox_get_content(msgbox);
    if (content_obj) {
        /* Keep unusually long notices readable instead of clipping them. */
        lv_obj_add_flag(content_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(content_obj, LV_DIR_VER);
    }

    uint32_t btn_cnt = 0;
    lv_obj_t *btn = NULL;
    while (btns[btn_cnt] && btns[btn_cnt][0] != '\0') {
        btn = lv_msgbox_add_footer_button(msgbox, btns[btn_cnt]);
        lv_obj_add_event_cb(btn, btns_event_cb, LV_EVENT_CLICKED, user_data);
        lv_group_add_obj(msg_group, btn);
        btn_cnt++;
    }
    if (btn) {
        lv_obj_t *footer = lv_msgbox_get_footer(msgbox);
        uint32_t footer_child_count = footer ? lv_obj_get_child_count(footer) : 0;
        for (uint32_t i = 0; i < footer_child_count; ++i) {
            lv_obj_t *footer_button = lv_obj_get_child(footer, i);
            lv_obj_add_event_cb(footer_button, ui_msgbox_keyboard_back_cb,
                                (lv_event_code_t)(LV_EVENT_KEY | LV_EVENT_PREPROCESS), btn);
        }
        lv_group_focus_obj(btn);
        lv_obj_add_state(btn, LV_STATE_FOCUS_KEY);
    }

    /* Msgbox container */
    lv_obj_set_style_bg_color(msgbox, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(msgbox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(msgbox, 20, 0);
    lv_obj_set_style_border_width(msgbox, 1, 0);
    lv_obj_set_style_border_color(msgbox, UI_COLOR_DIVIDER, 0);

    /* Title — accent color */
    if (title_obj) {
        lv_obj_set_style_text_color(title_obj, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(title_obj, &lv_font_montserrat_18, 0);
    }

    /* Message text — white */
    if (text_obj) {
        lv_obj_set_style_text_color(text_obj, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(text_obj, &lv_font_montserrat_14, 0);
    }

    return msgbox;

}

lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                      lv_menu_builder_variant_t builder_variant)
{
    lv_obj_t *obj = lv_menu_cont_create(parent);

    lv_obj_t *img = NULL;
    lv_obj_t *label = NULL;

    if (icon) {
        img = lv_image_create(obj);
        lv_image_set_src(img, icon);
    }

    if (txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}

lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                        int32_t val, lv_event_cb_t cb, lv_event_code_t filter)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t *slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);
    ui_prepare_slider_for_encoder(slider);

    if (cb != NULL) {
        lv_obj_add_event_cb(slider, cb, filter, NULL);
    }

    if (icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return slider;
}

lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk, lv_event_cb_t cb)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

    lv_obj_t *sw = lv_switch_create(obj);
    lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

lv_obj_t *create_button(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_obj_t *btn = lv_btn_create(obj);
    lv_obj_set_size(btn, lv_pct(10), lv_pct(100));
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return obj;
}

lv_obj_t *create_label(lv_obj_t *parent, const char *icon, const char *txt, const char *default_text)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    if (default_text) {
        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(label, default_text);
        return label;
    }
    return obj;
}

lv_obj_t *create_dropdown(lv_obj_t *parent, const char *icon, const char *txt, const char *options, uint8_t default_sel, lv_event_cb_t cb)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_obj_t *dd = lv_dropdown_create(obj);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, default_sel);
    ui_prepare_dropdown_for_encoder(dd);
    if (cb) {
        lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    return dd;
}



static void float_button_event_cb(lv_event_t * e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *create_floating_button(lv_event_cb_t event_cb, void* user_data)
{
    /* No-op: nav bar back button in ui_create_app_page handles back navigation */
    return NULL;
}


lv_obj_t *create_radius_button(lv_obj_t *parent, const void *image, lv_event_cb_t event_cb, void* user_data)
{
    lv_obj_t *float_btn = lv_btn_create(parent);
    lv_obj_set_size(float_btn, FLOAT_BUTTON_WIDTH, FLOAT_BUTTON_HEIGHT);
    lv_obj_add_flag(float_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_event_cb(float_btn, event_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_style_radius(float_btn, LV_RADIUS_CIRCLE, 0);
    if (image) {
        lv_obj_set_style_bg_image_src(float_btn, image, 0);
    }
    lv_obj_set_style_text_font(float_btn, lv_theme_get_font_large(float_btn), 0);
    lv_obj_set_style_bg_color(float_btn, UI_COLOR_ACCENT, 0);
    ui_add_accent_focus_style(float_btn);
    return float_btn;
}

lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb)
{
    lv_obj_t *menu = lv_menu_create(parent);
#ifndef USING_TOUCHPAD
    lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
#endif
    lv_obj_add_event_cb(menu, event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_center(menu);
    lv_obj_set_style_bg_color(menu, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, 0);
    return menu;
}

#ifndef ARDUINO
lv_indev_t *lv_get_encoder_indev()
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            return indev;
        }
        indev = lv_indev_get_next(indev);
    }
    return NULL;
}


lv_indev_t *lv_get_keyboard_indev()
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
        indev = lv_indev_get_next(indev);
    }
    return NULL;
}
#endif

void disable_input_devices()
{
    hw_disable_input_devices();
    lv_indev_enable(lv_get_encoder_indev(), false);
    if (hw_has_keyboard()) {
        lv_indev_enable(lv_get_keyboard_indev(), false);
    }
}

void enable_input_devices()
{
    hw_enable_input_devices();
    lv_indev_enable(lv_get_encoder_indev(), true);
    if (hw_has_keyboard()) {
        lv_indev_enable(lv_get_keyboard_indev(), true);
    }
}

void disable_keyboard()
{
    if (hw_has_keyboard()) {
#if defined(ARDUINO_T_LORA_PAGER)
        if (hw_get_keyboard_navigation_enabled()) {
            hw_enable_keyboard();
            lv_indev_enable(lv_get_keyboard_indev(), true);
            return;
        }
#endif
        hw_disable_keyboard();
        lv_indev_enable(lv_get_keyboard_indev(), false);
    }
}

void enable_keyboard()
{
    if (hw_has_keyboard()) {
        hw_enable_keyboard();
        hw_flush_keyboard();
        lv_indev_enable(lv_get_keyboard_indev(), true);
    }
}

bool is_screen_small()
{
    lv_coord_t w = lv_disp_get_hor_res(NULL);
    lv_coord_t h = lv_disp_get_ver_res(NULL);
    if (w <= 240 || h <= 240) {
        return true;
    }
    return false;
}

/* ============================================================
 *  New card-style UI components
 * ============================================================ */

lv_obj_t *ui_create_app_page(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    ui_styles_init();

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_remove_all_objs(group);
        lv_group_set_editing(group, false);
    }

    /* Root container filling parent */
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* Top navigation bar */
    lv_obj_t *nav = lv_obj_create(page);
    lv_obj_set_size(nav, LV_PCT(100), 32);
    lv_obj_set_style_bg_color(nav, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_hor(nav, 8, 0);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(nav, LV_ALIGN_TOP_MID, 0, 0);

    /* Back button — colors from shared styles (updated on theme change) */
    lv_obj_t *back_btn = lv_btn_create(nav);
    lv_obj_set_size(back_btn, 28, 28);
    lv_obj_set_style_radius(back_btn, 14, 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_set_style_min_width(back_btn, 28, 0);
    lv_obj_set_style_max_width(back_btn, 28, 0);
    lv_obj_set_style_min_height(back_btn, 28, 0);
    lv_obj_set_style_max_height(back_btn, 28, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
#if FACTORY_HAS_TOUCH_INPUT
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
#endif

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_add_style(back_label, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);

    if (back_cb) {
        lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    /* Title */
    lv_obj_t *title_label = lv_label_create(nav);
    lv_label_set_text(title_label, title);
    const lv_font_t *title_font = is_screen_small() ? &font_alibaba_12 : &font_alibaba_24;
    lv_obj_set_style_text_font(title_label, title_font, 0);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(title_label);

    /* Divider line under nav */
    lv_obj_t *div_line = lv_obj_create(page);
    lv_obj_set_size(div_line, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div_line, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div_line, 0, 0);
    lv_obj_set_style_radius(div_line, 0, 0);
    lv_obj_align_to(div_line, nav, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    /* Touch devices use edge swipe navigation instead of a bottom button. */
    int bottom_bar_h = 0;



    /* Scrollable content area */
    int content_h = lv_display_get_vertical_resolution(NULL) - 33 - bottom_bar_h;
    lv_obj_t *content = lv_obj_create(page);
    lv_obj_set_size(content, LV_PCT(100), content_h);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_hor(content, 8, 0);
    lv_obj_set_style_pad_ver(content, 8, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_style_bg_color(content, UI_COLOR_ACCENT, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(content, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 33);

    /* Store root page reference in content's user_data for proper cleanup */
    lv_obj_set_user_data(content, page);
    if (back_cb) {
        ui_enable_edge_swipe_back(page, back_btn);
    }
    if (hw_get_nav_auto_hide_enabled()) {
        ui_app_page_enable_nav_auto_hide(content, 3000);
    }
#if defined(ARDUINO_T_LORA_PAGER)
    if (back_cb) {
        ui_app_page_enable_keyboard_navigation(page, back_btn);
    }
#endif

    return content;
}

void ui_destroy_app_page(lv_obj_t *content)
{
    if (!content) return;
    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_remove_all_objs(group);
        lv_group_set_editing(group, false);
    }
    /* The root page is stored in content's user_data */
    lv_obj_t *root = (lv_obj_t *)lv_obj_get_user_data(content);
    if (root) {
        ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_obj_get_user_data(root);
        if (state) {
            lv_obj_remove_event_cb_with_user_data(root, ui_app_page_nav_delete_cb, state);
            ui_app_page_nav_release(state);
        }
        lv_obj_delete(root);
    }
}

void ui_app_page_enable_nav_auto_hide(lv_obj_t *content, uint32_t timeout_ms)
{
    if (!content) return;

    lv_obj_t *root = (lv_obj_t *)lv_obj_get_user_data(content);
    if (!root || lv_obj_get_child_count(root) < 3) return;

    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_obj_get_user_data(root);
    if (state) {
        state->timeout_ms = timeout_ms;
        state->enabled = true;
        if (state->timer) {
            lv_timer_set_period(state->timer, timeout_ms);
            lv_timer_reset(state->timer);
            lv_timer_resume(state->timer);
        }
        return;
    }

    state = (ui_nav_auto_hide_t *)lv_malloc_zeroed(sizeof(ui_nav_auto_hide_t));
    if (!state) return;

    state->root = root;
    state->content = content;
    state->nav = lv_obj_get_child(root, 0);
    state->divider = lv_obj_get_child(root, 1);
    state->timeout_ms = timeout_ms;
    state->enabled = true;

    state->timer = lv_timer_create(ui_app_page_nav_auto_hide_timer_cb, timeout_ms, state);
    if (!state->timer) {
        lv_free(state);
        return;
    }

    lv_obj_set_user_data(root, state);
    lv_obj_add_event_cb(root, ui_app_page_nav_delete_cb, LV_EVENT_DELETE, state);
    lv_obj_add_event_cb(content, ui_app_page_nav_child_created_cb, LV_EVENT_CHILD_CREATED, state);
    lv_obj_add_event_cb(content, ui_app_page_nav_activity_cb, LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(content, ui_app_page_nav_activity_cb, LV_EVENT_SCROLL, state);
    lv_obj_add_event_cb(content, ui_app_page_nav_activity_cb, LV_EVENT_GESTURE, state);
    lv_obj_add_event_cb(content, ui_app_page_nav_activity_cb, LV_EVENT_KEY, state);
    lv_obj_tree_walk(content, ui_app_page_nav_bubble_walk_cb, state);
    lv_timer_reset(state->timer);
}

void ui_app_page_disable_nav_auto_hide(lv_obj_t *content)
{
    if (!content) return;

    lv_obj_t *root = (lv_obj_t *)lv_obj_get_user_data(content);
    if (!root) return;

    ui_nav_auto_hide_t *state = (ui_nav_auto_hide_t *)lv_obj_get_user_data(root);
    if (!state) return;

    state->enabled = false;
    ui_app_page_nav_set_hidden(state, false);
    if (state->timer) {
        lv_timer_pause(state->timer);
    }
}

lv_obj_t *ui_create_card(lv_obj_t *parent, const char *title)
{
    ui_styles_init();

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(card, &ui_styles.card, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_bottom(lbl, 8, 0);
    }

    return card;
}

/* Active theme colors — default to Mint preset */
uint32_t ui_active_accent = 0x00D4AA;
uint32_t ui_active_accent_dim = 0x009977;
uint32_t ui_active_card_bg = 0x1A1A1A;
uint32_t ui_active_track = 0x333333;

void ui_apply_theme_preset(uint8_t preset_idx)
{
    if (preset_idx >= UI_THEME_PRESET_COUNT) return;
    const ui_theme_preset_t *p = &ui_theme_presets[preset_idx];
    ui_active_accent = p->accent;
    ui_active_accent_dim = p->accent_dim;
    ui_active_card_bg = p->card_bg;
    ui_active_track = p->track;
}

void ui_add_accent_focus_style(lv_obj_t *obj)
{
    if (!obj) return;
    ui_styles_init();
    lv_obj_add_style(obj, &ui_styles.accent_focus_ring, LV_STATE_FOCUSED);
    lv_obj_add_style(obj, &ui_styles.accent_focus_ring, LV_STATE_FOCUS_KEY);
}

/* Font size preference: 0=auto, 1=small, 2=medium, 3=large */
static uint8_t g_font_size_pref = 0;

void ui_set_font_size_pref(uint8_t pref)
{
    g_font_size_pref = pref;
}

uint8_t ui_get_font_size_pref(void)
{
    return g_font_size_pref;
}

static bool use_small_font(void)
{
    if (g_font_size_pref == 1) return true;  /* force small */
    if (g_font_size_pref >= 2) return false; /* medium or large */
    return is_screen_small(); /* auto */
}

static bool ui_widget_owns_card_focus(lv_obj_t *widget)
{
    if (!widget) return false;

    return lv_obj_check_type(widget, &lv_slider_class) ||
           lv_obj_check_type(widget, &lv_button_class) ||
           lv_obj_check_type(widget, &lv_switch_class) ||
           lv_obj_check_type(widget, &lv_dropdown_class) ||
           lv_obj_check_type(widget, &lv_textarea_class);
}

lv_obj_t *ui_create_card_item(lv_obj_t *card, const char *icon, const char *title, lv_obj_t *widget)
{
    ui_styles_init();
    const bool widget_is_slider = widget && lv_obj_check_type(widget, &lv_slider_class);
    const bool widget_is_dropdown = widget && lv_obj_check_type(widget, &lv_dropdown_class);
    const bool small_full_width_widget = is_screen_small() && (widget_is_slider || widget_is_dropdown);
    const bool widget_owns_focus = ui_widget_owns_card_focus(widget);

    /* Non-touch: use lv_btn so encoder can focus; touch: use lv_obj */
#ifdef USING_TOUCHPAD
    lv_obj_t *row = lv_obj_create(card);
#else
    lv_obj_t *row = lv_btn_create(card);
    /* Remove all default button effects — only for encoder focus navigation */
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_transform_width(row, 0, 0);
    lv_obj_set_style_transform_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(row, 0, 0);
    lv_obj_set_style_transform_height(row, 0, LV_STATE_PRESSED);
    /* Accent border on focus for visual feedback */
    lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(row, LV_OPA_60, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(row, 8, LV_STATE_FOCUSED);
#endif
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_set_flex_flow(row, small_full_width_widget ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_pad_row(row, small_full_width_widget ? 6 : 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (icon) {
        lv_obj_t *ic = lv_label_create(row);
        lv_label_set_text(ic, icon);
        lv_obj_add_style(ic, &ui_styles.accent_text, 0);
    }

    if (title) {
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_PRIMARY, 0);
        if (use_small_font()) {
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        }
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        /* Fixed title width keeps controls aligned without forcing overflow on small screens. */
        lv_obj_set_width(lbl, use_small_font() ? (small_full_width_widget ? 58 : 70) : 100);
    }

    if (widget && lv_obj_get_style_flex_grow(widget, LV_PART_MAIN) == 0) {
        lv_obj_t *spacer = lv_obj_create(row);
        lv_obj_set_size(spacer, 1, 1);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_set_style_pad_all(spacer, 0, 0);
        lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    }

    if (widget) {
        /* Re-parent widget from temp parent to row, then delete orphan temp parent */
        lv_obj_t *old_parent = lv_obj_get_parent(widget);
        lv_obj_set_parent(widget, row);
        if (widget_is_slider) {
            if (small_full_width_widget) {
                lv_obj_add_flag(widget, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
                lv_obj_set_width(widget, LV_PCT(100));
                lv_obj_set_flex_grow(widget, 0);
            } else {
                lv_obj_set_width(widget, 1);
                lv_obj_set_flex_grow(widget, 1);
            }
            ui_prepare_slider_for_encoder(widget);
            if (small_full_width_widget) {
                lv_obj_set_width(widget, LV_PCT(100));
                lv_obj_set_flex_grow(widget, 0);
                lv_obj_set_style_margin_left(widget, 0, 0);
                lv_obj_set_style_margin_right(widget, 0, 0);
            }
        } else if (widget_is_dropdown) {
            if (small_full_width_widget) {
                lv_obj_add_flag(widget, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
                lv_obj_set_width(widget, LV_PCT(100));
                lv_obj_set_flex_grow(widget, 0);
                lv_obj_set_style_margin_left(widget, 0, 0);
                lv_obj_set_style_margin_right(widget, 0, 0);
            }
            ui_prepare_dropdown_for_encoder(widget);
        } else if (lv_obj_check_type(widget, &lv_textarea_class)) {
            ui_prepare_textarea_for_encoder(widget);
        }
        if (widget_owns_focus) {
            lv_obj_add_flag(widget, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_group_t *row_group = (lv_group_t *)lv_obj_get_group(row);
            if (row_group) {
                lv_group_remove_obj(row);
            }
            lv_group_t *widget_group = lv_group_get_default();
            if (widget_group && lv_obj_get_group(widget) != widget_group) {
                lv_group_add_obj(widget_group, widget);
            }
        }
        if (old_parent && old_parent != card && old_parent != row) {
            lv_obj_delete(old_parent);
        }
    }

    /* Divider */
    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, LV_PCT(95), 1);
    lv_obj_set_style_bg_color(div, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);

    return row;
}

lv_obj_t *ui_create_card_slider(lv_obj_t *card, const char *icon, const char *title,
                                 int32_t min, int32_t max, int32_t val, lv_event_cb_t cb)
{
    lv_obj_t *slider = lv_slider_create(lv_obj_create(card)); /* temp parent, re-parented */
    lv_obj_set_size(slider, LV_PCT(50), 12);

    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);
    lv_obj_set_flex_grow(slider, 1);
    ui_prepare_slider_for_encoder(slider);

    if (cb) {
        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    return ui_create_card_item(card, icon, title, slider);
}

lv_obj_t *ui_create_card_switch(lv_obj_t *card, const char *icon, const char *title,
                                 bool checked, lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(lv_obj_create(card)); /* temp parent, re-parented */
    lv_obj_set_size(sw, 44, 24);
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    if (cb) {
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    return ui_create_card_item(card, icon, title, sw);
}

lv_obj_t *ui_create_card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                                   const char *options, uint8_t sel, lv_event_cb_t cb)
{
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card)); /* temp parent */
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_set_width(dd, is_screen_small() ? LV_PCT(100) : 100);
    lv_obj_set_height(dd, 32);
    ui_prepare_dropdown_for_encoder(dd);

    if (cb) {
        lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    return ui_create_card_item(card, icon, title, dd);
}

lv_obj_t *ui_create_card_button(lv_obj_t *card, const char *icon, const char *title,
                                 const char *btn_text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(lv_obj_create(card));
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    ui_add_accent_focus_style(btn);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, btn_text);
    lv_obj_center(lbl);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    return ui_create_card_item(card, icon, title, btn);
}

lv_obj_t *ui_create_card_info(lv_obj_t *card, const char *icon, const char *title, const char *value)
{
    lv_obj_t *lbl = lv_label_create(lv_obj_create(card));
    lv_label_set_text(lbl, value ? value : "--");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
    if (use_small_font()) {
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    }
    lv_obj_set_flex_grow(lbl, 1);

    return ui_create_card_item(card, icon, title, lbl);
}

/* Status bar */
static struct {
    lv_obj_t *cont;
    lv_obj_t *time_label;
    lv_obj_t *batt_label;
    lv_obj_t *wifi_icon;
    lv_obj_t *bt_icon;
} status_bar = {NULL};

lv_obj_t *ui_create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 20);
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    /* Push down on round screens to avoid corner clipping */
    int bar_y = 0;
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    bar_y = 8;
#endif
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, bar_y);

    /* Battery — left edge */
    status_bar.batt_label = lv_label_create(bar);
    lv_label_set_text(status_bar.batt_label, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_color(status_bar.batt_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_bar.batt_label, &lv_font_montserrat_12, 0);
    lv_obj_align(status_bar.batt_label, LV_ALIGN_LEFT_MID, 6, 0);

    /* Time — dead center */
    status_bar.time_label = lv_label_create(bar);
    lv_label_set_text(status_bar.time_label, "--:--");
    lv_obj_set_style_text_color(status_bar.time_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(status_bar.time_label, is_screen_small() ? &lv_font_montserrat_12 : &lv_font_montserrat_16, 0);
    lv_obj_align(status_bar.time_label, LV_ALIGN_CENTER, 0, 0);

    /* WiFi — right edge */
    status_bar.wifi_icon = lv_label_create(bar);
    lv_label_set_text(status_bar.wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(status_bar.wifi_icon, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_bar.wifi_icon, &lv_font_montserrat_12, 0);
    lv_obj_align(status_bar.wifi_icon, LV_ALIGN_RIGHT_MID, -6, 0);

    status_bar.cont = bar;
    return bar;
}

void ui_status_bar_update(void)
{
    if (!status_bar.time_label) return;

    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    lv_label_set_text_fmt(status_bar.time_label, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    monitor_params_t params;
    hw_get_monitor_params(params);
    if (params.charging) {
        lv_label_set_text(status_bar.batt_label, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(status_bar.batt_label, UI_COLOR_ACCENT, 0);
    } else {
        const char *batt_sym = LV_SYMBOL_BATTERY_FULL;
        if (params.battery_percent < 20) batt_sym = LV_SYMBOL_BATTERY_EMPTY;
        else if (params.battery_percent < 50) batt_sym = LV_SYMBOL_BATTERY_2;
        else if (params.battery_percent < 80) batt_sym = LV_SYMBOL_BATTERY_3;
        lv_label_set_text_fmt(status_bar.batt_label, "%s %d%%", batt_sym, params.battery_percent);
        lv_obj_set_style_text_color(status_bar.batt_label, UI_COLOR_TEXT_SECONDARY, 0);
    }

    bool wifi_on = hw_get_wifi_connected();
    lv_obj_set_style_text_color(status_bar.wifi_icon,
                                wifi_on ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
}
