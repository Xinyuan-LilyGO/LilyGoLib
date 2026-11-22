/**
 * @file      ui_theme.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 * Defines shared Factory styles and refreshes their colors when the active
 * theme changes.
 */

#include <lvgl.h>
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6
#include <lvgl_private.h>
#endif
#include "ui_define.h"

ui_styles_t ui_styles;

/* Static per-object-type styles (shared, updated on theme change) */
static lv_style_t s_btn;
static lv_style_t s_switch_off, s_switch_on, s_switch_knob, s_dropdown, s_dropdown_list;
static lv_style_t s_focus_outline_clear;
static bool styles_initialized = false;

static bool has_focusable_child(lv_obj_t *obj)
{
    if (!obj) return false;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (lv_obj_check_type(child, &lv_slider_class) ||
                lv_obj_check_type(child, &lv_switch_class) ||
                lv_obj_check_type(child, &lv_dropdown_class) ||
                lv_obj_check_type(child, &lv_textarea_class) ||
                lv_obj_check_type(child, &lv_list_button_class)) {
            return true;
        }
    }
    return false;
}

static bool is_focus_control(lv_obj_t *obj)
{
    if (!obj) return false;
    if (lv_obj_check_type(obj, &lv_button_class)) {
        /* Card rows are buttons for encoder scrolling, but their child widget
           owns focus when a dropdown, slider, switch or textarea is present. */
        return !has_focusable_child(obj);
    }
    if (lv_obj_check_type(obj, &lv_slider_class) ||
            lv_obj_check_type(obj, &lv_switch_class) ||
            lv_obj_check_type(obj, &lv_dropdown_class) ||
            lv_obj_check_type(obj, &lv_textarea_class) ||
            lv_obj_check_type(obj, &lv_list_button_class)) {
        return true;
    }
    /* Custom clickable rows are focus controls only after they join an input group. */
    return lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_group(obj) != NULL;
}

static bool focus_needs_contrast(lv_obj_t *obj)
{
    if (!obj) return false;
    if (lv_obj_check_type(obj, &lv_slider_class) ||
            lv_obj_check_type(obj, &lv_switch_class)) {
        return true;
    }
    if (lv_obj_check_type(obj, &lv_button_class)) {
        const lv_color_t bg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
        return lv_color_eq(bg, UI_COLOR_ACCENT) || lv_color_eq(bg, UI_COLOR_ACCENT_DIM);
    }
    return false;
}

static void clear_delegated_focus_style(lv_obj_t *obj)
{
    if (!obj || !has_focusable_child(obj)) return;
    const lv_style_selector_t focus = static_cast<lv_style_selector_t>(LV_STATE_FOCUSED);
    const lv_style_selector_t focus_key = static_cast<lv_style_selector_t>(LV_STATE_FOCUS_KEY);
    const lv_style_selector_t focus_both = static_cast<lv_style_selector_t>(
        LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    for (lv_style_selector_t selector : {focus, focus_key, focus_both}) {
        lv_obj_set_style_border_width(obj, 0, selector);
        lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, selector);
        lv_obj_set_style_outline_width(obj, 0, selector);
        lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, selector);
        lv_obj_set_style_shadow_width(obj, 0, selector);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, selector);
    }
}

static void apply_dropdown_selected_style(lv_obj_t *obj)
{
    if (!obj || !lv_obj_check_type(obj, &lv_dropdownlist_class)) return;

    const lv_style_selector_t selected = static_cast<lv_style_selector_t>(LV_PART_SELECTED);
    const lv_style_selector_t selected_checked = static_cast<lv_style_selector_t>(
        selected | static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
    const lv_style_selector_t selected_pressed = static_cast<lv_style_selector_t>(
        selected | static_cast<lv_style_selector_t>(LV_STATE_PRESSED));

    /* The default theme installs primary-color styles at the more specific
       checked/pressed selectors. Remove those theme entries before applying
       the live Factory accent. */
    lv_obj_remove_theme(obj, selected);
    lv_obj_remove_theme(obj, selected_checked);
    lv_obj_remove_theme(obj, selected_pressed);
    lv_obj_add_style(obj, &ui_styles.accent_btn, selected);
    lv_obj_add_style(obj, &ui_styles.accent_btn, selected_checked);
    lv_obj_add_style(obj, &ui_styles.accent_btn, selected_pressed);
}

static void clear_focus_outline(lv_obj_t *obj)
{
    if (!is_focus_control(obj)) return;

    const lv_style_selector_t focus = static_cast<lv_style_selector_t>(LV_STATE_FOCUSED);
    const lv_style_selector_t focus_key = static_cast<lv_style_selector_t>(LV_STATE_FOCUS_KEY);
    const lv_style_selector_t focus_both = static_cast<lv_style_selector_t>(
        LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    const lv_state_t active_state = static_cast<lv_state_t>(
        lv_obj_get_state(obj) & static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
    const bool rounded_button = lv_obj_check_type(obj, &lv_button_class) ||
                                lv_obj_check_type(obj, &lv_list_button_class);
    int32_t focus_radius = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    if (rounded_button && focus_radius == 0) {
        focus_radius = 8;
    }
    const lv_color_t focus_border = focus_needs_contrast(obj) ?
                                    UI_COLOR_TEXT_ON_ACCENT : UI_COLOR_ACCENT;

    /* The default LVGL theme owns outline_primary at FOCUS_KEY.  A local
       width=0 style cannot reliably override that theme entry after a page
       creates a new button, so remove the theme entries first. */
    lv_obj_remove_theme(obj, focus);
    lv_obj_remove_theme(obj, focus_key);
    lv_obj_remove_theme(obj, focus_both);
    lv_obj_add_style(obj, &ui_styles.accent_focus_ring, focus);
    lv_obj_add_style(obj, &ui_styles.accent_focus_ring, focus_key);
    lv_obj_add_style(obj, &ui_styles.accent_focus_ring, focus_both);

    lv_obj_set_style_outline_width(obj, 0, focus);
    lv_obj_set_style_outline_width(obj, 0, focus_key);
    lv_obj_set_style_outline_width(obj, 0, focus_both);
    lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, focus);
    lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, focus_key);
    lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, focus_both);
    lv_obj_set_style_border_color(obj, focus_border, focus);
    lv_obj_set_style_border_color(obj, focus_border, focus_key);
    lv_obj_set_style_border_color(obj, focus_border, focus_both);
    lv_obj_set_style_outline_color(obj, focus_border, focus);
    lv_obj_set_style_outline_color(obj, focus_border, focus_key);
    lv_obj_set_style_outline_color(obj, focus_border, focus_both);
    if (focus_needs_contrast(obj)) {
        lv_obj_set_style_border_color(obj, focus_border, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(obj, focus_border, LV_STATE_CHECKED);
    }
    if (active_state & LV_STATE_FOCUSED) {
        lv_obj_set_style_radius(obj, focus_radius, focus);
    }
    if (active_state & LV_STATE_FOCUS_KEY) {
        lv_obj_set_style_radius(obj, focus_radius, focus_key);
    }
    if (active_state == (LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY)) {
        lv_obj_set_style_radius(obj, focus_radius, focus_both);
    }
    if (rounded_button) {
        lv_obj_set_style_radius(obj, focus_radius, focus);
        lv_obj_set_style_radius(obj, focus_radius, focus_key);
        lv_obj_set_style_radius(obj, focus_radius, focus_both);
    }
    if (lv_obj_check_type(obj, &lv_list_button_class)) {
        lv_obj_set_style_radius(obj, 8, focus);
        lv_obj_set_style_radius(obj, 8, focus_key);
        lv_obj_set_style_radius(obj, 8, focus_both);
    }
}

static void refresh_theme_object_colors(lv_obj_t *obj)
{
    if (!obj) return;

    if (is_focus_control(obj)) {
        lv_obj_add_style(obj, &s_focus_outline_clear, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &s_focus_outline_clear, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &s_focus_outline_clear,
                         static_cast<lv_style_selector_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));

        /* Local focus styles are snapshots. Rewrite them after the active accent changes. */
        lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_shadow_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_color(obj, UI_COLOR_ACCENT, LV_STATE_FOCUS_KEY);
        const lv_style_selector_t focus_selector =
            static_cast<lv_style_selector_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, focus_selector);
        lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, focus_selector);
        lv_obj_set_style_shadow_color(obj, UI_COLOR_ACCENT, focus_selector);
        lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_color(obj, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_color(obj, UI_COLOR_ACCENT, LV_STATE_CHECKED);

        clear_focus_outline(obj);
    } else {
        clear_delegated_focus_style(obj);
    }

    /* LVGL dropdown selection is drawn from the popup's selected part. */
    if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        const lv_style_selector_t selected_part = static_cast<lv_style_selector_t>(LV_PART_SELECTED);
        const lv_style_selector_t selected_checked = static_cast<lv_style_selector_t>(
            selected_part | static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
        const lv_style_selector_t selected_pressed = static_cast<lv_style_selector_t>(
            selected_part | static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
        lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, selected_part);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selected_part);
        lv_obj_set_style_text_color(obj, UI_COLOR_TEXT_ON_ACCENT, selected_part);
        lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, selected_checked);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selected_checked);
        lv_obj_set_style_text_color(obj, UI_COLOR_TEXT_ON_ACCENT, selected_checked);
        lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, selected_pressed);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selected_pressed);
        lv_obj_set_style_text_color(obj, UI_COLOR_TEXT_ON_ACCENT, selected_pressed);
        apply_dropdown_selected_style(obj);
    }

    if (lv_obj_check_type(obj, &lv_list_button_class)) {
        /* LVGL's default list button keeps a bottom separator. Keep the
           separator visible, but derive its color from the active theme. */
        lv_obj_set_style_border_color(obj, UI_COLOR_DIVIDER, LV_PART_MAIN);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_PRESSED);
    }

    /* Some pages use a direct default-state accent border or glow. Refresh only
       values that actually belonged to the previous theme. */
    const lv_color_t previous_accent = lv_color_hex(ui_previous_accent);
    const lv_color_t previous_bg = lv_color_hex(ui_previous_bg);
    const lv_color_t previous_card_focus = lv_color_hex(ui_previous_card_focus);
    if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == LV_OPA_COVER) {
        const lv_color_t current_bg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
        if (lv_color_eq(current_bg, previous_bg)) {
            lv_obj_set_style_bg_color(obj, UI_COLOR_BG, LV_PART_MAIN);
        } else if (lv_color_eq(current_bg, previous_card_focus)) {
            lv_obj_set_style_bg_color(obj, UI_COLOR_CARD_FOCUS, LV_PART_MAIN);
        }
    }
    /* Accent borders and focus glows belong to the focused control. Do not
       recolor a parent container's default part when themes are switched. */
    if (is_focus_control(obj)) {
        if (lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), previous_accent)) {
            lv_obj_set_style_border_color(obj, UI_COLOR_ACCENT, LV_PART_MAIN);
        }
        if (lv_color_eq(lv_obj_get_style_outline_color(obj, LV_PART_MAIN), previous_accent)) {
            lv_obj_set_style_outline_color(obj, UI_COLOR_ACCENT, LV_PART_MAIN);
        }
        if (lv_color_eq(lv_obj_get_style_shadow_color(obj, LV_PART_MAIN), previous_accent)) {
            lv_obj_set_style_shadow_color(obj, UI_COLOR_ACCENT, LV_PART_MAIN);
        }
    }

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const lv_color_t current = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
        const lv_color_t previous_primary = lv_color_hex(ui_previous_text_primary);
        lv_obj_t *parent = lv_obj_get_parent(obj);
        const bool accent_button = parent && lv_obj_check_type(parent, &lv_button_class) &&
                                   lv_color_eq(lv_obj_get_style_bg_color(parent, LV_PART_MAIN),
                                               previous_accent);
        if (accent_button) {
            lv_obj_set_style_text_color(obj, UI_COLOR_TEXT_ON_ACCENT, LV_PART_MAIN);
        } else if (lv_color_eq(current, previous_accent)) {
            lv_obj_set_style_text_color(obj, UI_COLOR_ACCENT, LV_PART_MAIN);
        } else if (lv_color_eq(current, previous_primary)) {
            lv_obj_set_style_text_color(obj, UI_COLOR_TEXT_PRIMARY, LV_PART_MAIN);
        }
    }
}

static lv_obj_tree_walk_res_t refresh_theme_object_colors_cb(lv_obj_t *obj, void *user_data)
{
    (void)user_data;
    refresh_theme_object_colors(obj);
    return LV_OBJ_TREE_WALK_NEXT;
}

/* Update only the COLOR properties of already-initialized styles.
   Safe to call repeatedly — does not reset non-color properties. */
static void update_style_colors(void)
{
    /* Shared styles — only update color properties */
    lv_style_set_bg_color(&ui_styles.card, UI_COLOR_CARD_BG);
    lv_style_set_bg_color(&ui_styles.app_btn, UI_COLOR_APP_BG);
    lv_style_set_bg_color(&ui_styles.accent_btn, UI_COLOR_ACCENT);
    lv_style_set_text_color(&ui_styles.accent_btn, UI_COLOR_TEXT_ON_ACCENT);
    lv_style_set_border_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_BORDER);
    lv_style_set_outline_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_OUTLINE);
    lv_style_set_border_color(&ui_styles.focus_glow, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.focus_glow, UI_COLOR_ACCENT);
    lv_style_set_bg_color(&ui_styles.slider_track, UI_COLOR_TRACK);
    lv_style_set_bg_color(&ui_styles.slider_knob, UI_COLOR_KNOB);
    lv_style_set_bg_color(&ui_styles.divider, UI_COLOR_DIVIDER);
    lv_style_set_text_color(&ui_styles.accent_text, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.accent_text, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.accent_shadow, UI_COLOR_ACCENT);

    /* Per-object-type styles */
    lv_style_set_bg_color(&s_btn, UI_COLOR_CARD_BG);
    lv_style_set_text_color(&s_btn, UI_COLOR_TEXT_PRIMARY);
    lv_style_set_bg_color(&s_switch_off, UI_COLOR_TRACK);
    lv_style_set_bg_color(&s_switch_on, UI_COLOR_ACCENT);
    lv_style_set_bg_color(&s_switch_knob, UI_COLOR_KNOB);
    lv_style_set_bg_color(&s_dropdown, UI_COLOR_CARD_BG);
    lv_style_set_border_color(&s_dropdown, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown, UI_COLOR_CONTROL_TEXT);
    lv_style_set_bg_color(&s_dropdown_list, UI_COLOR_CARD_BG);
    lv_style_set_border_color(&s_dropdown_list, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown_list, UI_COLOR_CONTROL_TEXT);
}

void ui_styles_refresh(void)
{
    if (styles_initialized) {
        /* Already initialized — just update colors */
        update_style_colors();
        return;
    }
    styles_initialized = true;

    /* ── First-time initialization ── */

    /* Card container */
    lv_style_init(&ui_styles.card);
    lv_style_set_bg_color(&ui_styles.card, UI_COLOR_CARD_BG);
    lv_style_set_bg_opa(&ui_styles.card, LV_OPA_COVER);
    lv_style_set_radius(&ui_styles.card, 16);
    lv_style_set_pad_all(&ui_styles.card, 12);
    lv_style_set_pad_row(&ui_styles.card, 0);
    lv_style_set_border_width(&ui_styles.card, 0);
    lv_style_set_shadow_width(&ui_styles.card, 0);

    /* Card item row */
    lv_style_init(&ui_styles.card_item);
    lv_style_set_pad_top(&ui_styles.card_item, 8);
    lv_style_set_pad_bottom(&ui_styles.card_item, 8);
    lv_style_set_pad_left(&ui_styles.card_item, 4);
    lv_style_set_pad_right(&ui_styles.card_item, 4);

    /* App launcher icon button */
    lv_style_init(&ui_styles.app_btn);
    lv_style_set_bg_color(&ui_styles.app_btn, UI_COLOR_APP_BG);
    lv_style_set_bg_opa(&ui_styles.app_btn, LV_OPA_COVER);
    lv_style_set_radius(&ui_styles.app_btn, 16);
    lv_style_set_border_width(&ui_styles.app_btn, 0);
    lv_style_set_shadow_width(&ui_styles.app_btn, 0);

    /* Accent button */
    lv_style_init(&ui_styles.accent_btn);
    lv_style_set_bg_color(&ui_styles.accent_btn, UI_COLOR_ACCENT);
    lv_style_set_bg_opa(&ui_styles.accent_btn, LV_OPA_COVER);
    lv_style_set_radius(&ui_styles.accent_btn, 8);
    lv_style_set_text_color(&ui_styles.accent_btn, UI_COLOR_TEXT_ON_ACCENT);
    lv_style_set_border_width(&ui_styles.accent_btn, 0);

    /* Theme-colored focus ring shared by encoder and keyboard controls. */
    lv_style_init(&ui_styles.accent_focus_ring);
    lv_style_set_border_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_BORDER);
    lv_style_set_border_width(&ui_styles.accent_focus_ring, 2);
    lv_style_set_border_opa(&ui_styles.accent_focus_ring, LV_OPA_COVER);
    lv_style_set_outline_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_OUTLINE);
    lv_style_set_outline_width(&ui_styles.accent_focus_ring, 0);
    lv_style_set_outline_opa(&ui_styles.accent_focus_ring, LV_OPA_TRANSP);
    lv_style_set_outline_pad(&ui_styles.accent_focus_ring, 0);

    /* Remove the default LVGL outline from focus states. The theme border is
       the single visible focus ring and follows the active accent. */
    lv_style_init(&s_focus_outline_clear);
    lv_style_set_outline_width(&s_focus_outline_clear, 0);
    lv_style_set_outline_opa(&s_focus_outline_clear, LV_OPA_TRANSP);

    /* Focus glow */
    lv_style_init(&ui_styles.focus_glow);
    lv_style_set_border_color(&ui_styles.focus_glow, UI_COLOR_ACCENT);
    lv_style_set_border_width(&ui_styles.focus_glow, 2);
    lv_style_set_border_opa(&ui_styles.focus_glow, LV_OPA_COVER);
    lv_style_set_shadow_width(&ui_styles.focus_glow, 12);
    lv_style_set_shadow_color(&ui_styles.focus_glow, UI_COLOR_ACCENT);
    lv_style_set_shadow_opa(&ui_styles.focus_glow, LV_OPA_30);

    /* Secondary text */
    lv_style_init(&ui_styles.text_secondary);
    lv_style_set_text_color(&ui_styles.text_secondary, UI_COLOR_TEXT_SECONDARY);

    /* Accent text (also used for accent-colored shadows on clock digits) */
    lv_style_init(&ui_styles.accent_text);
    lv_style_set_text_color(&ui_styles.accent_text, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.accent_text, UI_COLOR_ACCENT);

    /* Accent shadow — for glow/charging effects */
    lv_style_init(&ui_styles.accent_shadow);
    lv_style_set_shadow_color(&ui_styles.accent_shadow, UI_COLOR_ACCENT);

    /* Divider */
    lv_style_init(&ui_styles.divider);
    lv_style_set_bg_color(&ui_styles.divider, UI_COLOR_DIVIDER);
    lv_style_set_bg_opa(&ui_styles.divider, LV_OPA_COVER);
    lv_style_set_height(&ui_styles.divider, 1);
    lv_style_set_border_width(&ui_styles.divider, 0);

    /* Slider track */
    lv_style_init(&ui_styles.slider_track);
    lv_style_set_bg_color(&ui_styles.slider_track, UI_COLOR_TRACK);
    lv_style_set_bg_opa(&ui_styles.slider_track, LV_OPA_COVER);
    lv_style_set_radius(&ui_styles.slider_track, 4);
    lv_style_set_height(&ui_styles.slider_track, 6);

    /* Slider knob */
    lv_style_init(&ui_styles.slider_knob);
    lv_style_set_bg_color(&ui_styles.slider_knob, UI_COLOR_KNOB);
    lv_style_set_bg_opa(&ui_styles.slider_knob, LV_OPA_COVER);
    lv_style_set_radius(&ui_styles.slider_knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&ui_styles.slider_knob, 4);

    /* Per-object-type styles */
    lv_style_init(&s_btn);
    lv_style_set_bg_color(&s_btn, UI_COLOR_CARD_BG);
    lv_style_set_bg_opa(&s_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_btn, 8);
    lv_style_set_text_color(&s_btn, UI_COLOR_TEXT_PRIMARY);
    lv_style_set_border_width(&s_btn, 0);
    lv_style_set_shadow_width(&s_btn, 0);

    lv_style_init(&s_switch_off);
    lv_style_set_bg_color(&s_switch_off, UI_COLOR_TRACK);
    lv_style_set_bg_opa(&s_switch_off, LV_OPA_COVER);
    lv_style_set_radius(&s_switch_off, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_switch_off, 0);

    lv_style_init(&s_switch_on);
    lv_style_set_bg_color(&s_switch_on, UI_COLOR_ACCENT);
    lv_style_set_bg_opa(&s_switch_on, LV_OPA_COVER);
    lv_style_set_radius(&s_switch_on, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_switch_on, 0);

    lv_style_init(&s_switch_knob);
    lv_style_set_bg_color(&s_switch_knob, UI_COLOR_KNOB);
    lv_style_set_bg_opa(&s_switch_knob, LV_OPA_COVER);
    lv_style_set_radius(&s_switch_knob, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_switch_knob, 0);

    lv_style_init(&s_dropdown);
    lv_style_set_bg_color(&s_dropdown, UI_COLOR_CARD_BG);
    lv_style_set_bg_opa(&s_dropdown, LV_OPA_COVER);
    lv_style_set_radius(&s_dropdown, 8);
    lv_style_set_border_width(&s_dropdown, 1);
    lv_style_set_border_color(&s_dropdown, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown, UI_COLOR_CONTROL_TEXT);
    if (lv_disp_get_hor_res(NULL) <= 320) {
        lv_style_set_text_font(&s_dropdown, &lv_font_montserrat_12);
    }

    lv_style_init(&s_dropdown_list);
    lv_style_set_bg_color(&s_dropdown_list, UI_COLOR_CARD_BG);
    lv_style_set_bg_opa(&s_dropdown_list, LV_OPA_COVER);
    lv_style_set_radius(&s_dropdown_list, 8);
    lv_style_set_border_width(&s_dropdown_list, 1);
    lv_style_set_border_color(&s_dropdown_list, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown_list, UI_COLOR_CONTROL_TEXT);
    if (lv_disp_get_hor_res(NULL) <= 320) {
        lv_style_set_text_font(&s_dropdown_list, &lv_font_montserrat_12);
    }
}

void ui_styles_init(void)
{
    ui_styles_refresh();
}

/* Walk callback: invalidate every object in the tree */
static lv_obj_tree_walk_res_t invalidate_cb(lv_obj_t *obj, void *user_data)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) {
        lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(obj, LV_OPA_60, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(obj, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(obj, 2, LV_PART_SCROLLBAR);
    }
    lv_obj_invalidate(obj);
    return LV_OBJ_TREE_WALK_NEXT;
}

void ui_theme_apply(void)
{
    /* Update colors in existing styles (no reset) */
    update_style_colors();

    /* The screen is created before the settings page can change the theme. */
    lv_obj_set_style_bg_color(lv_screen_active(), UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    if (main_screen) {
        lv_obj_set_style_bg_color(main_screen, UI_COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, LV_PART_MAIN);
        for (uint32_t i = 0; i < lv_obj_get_child_count(main_screen); ++i) {
            lv_obj_t *tile = lv_obj_get_child(main_screen, i);
            lv_obj_set_style_bg_color(tile, UI_COLOR_BG, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
    ui_refresh_status_bar_theme();

    /* Re-register theme so new objects get updated styles */
    theme_init();

    /* Walk entire object tree and invalidate each object for redraw */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_tree_walk(scr, refresh_theme_object_colors_cb, NULL);
    lv_obj_tree_walk(scr, invalidate_cb, NULL);
    /* Dropdown popups, keyboards and modal overlays live outside the screen
       tree.  Refresh those layers too or they retain the old accent. */
    lv_obj_t *top_layer = lv_layer_top();
    lv_obj_t *sys_layer = lv_layer_sys();
    lv_obj_t *bottom_layer = lv_layer_bottom();
    lv_obj_tree_walk(top_layer, refresh_theme_object_colors_cb, NULL);
    lv_obj_tree_walk(top_layer, invalidate_cb, NULL);
    lv_obj_tree_walk(sys_layer, refresh_theme_object_colors_cb, NULL);
    lv_obj_tree_walk(sys_layer, invalidate_cb, NULL);
    lv_obj_tree_walk(bottom_layer, refresh_theme_object_colors_cb, NULL);
    lv_obj_tree_walk(bottom_layer, invalidate_cb, NULL);
    ui_refresh_app_grid_theme();

    /* Update page indicator dots (uses direct inline colors) */
    update_page_indicator();

    lv_refr_now(NULL);
}

static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    ui_styles_init();

    /* Button */
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_add_style(obj, &s_btn, LV_PART_MAIN);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_PRESSED);
    }

    /* Slider */
    if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_add_style(obj, &ui_styles.slider_track, LV_PART_MAIN);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &ui_styles.slider_knob, LV_PART_KNOB);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
    }

    /* Switch */
    if (lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_add_style(obj, &s_switch_off, LV_PART_MAIN);
        lv_obj_add_style(obj, &s_switch_on,
                         static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
                         static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
        lv_obj_add_style(obj, &s_switch_knob, LV_PART_KNOB);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
    }

    /* Dropdown button */
    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_add_style(obj, &s_dropdown, LV_PART_MAIN);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
    }

    /* Dropdown list popup */
    if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, &s_dropdown_list, 0);
        apply_dropdown_selected_style(obj);
    }

    /* Textarea */
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
    }

    /* List */
    if (lv_obj_check_type(obj, &lv_list_class)) {
        static lv_style_t s_list;
        static bool s_list_inited = false;
        if (!s_list_inited) {
            lv_style_init(&s_list);
            lv_style_set_border_width(&s_list, 0);
            lv_style_set_bg_opa(&s_list, LV_OPA_TRANSP);
            lv_style_set_radius(&s_list, 0);
            lv_style_set_pad_all(&s_list, 0);
            s_list_inited = true;
        }
        lv_obj_add_style(obj, &s_list, 0);
    }

    if (lv_obj_check_type(obj, &lv_list_button_class)) {
        lv_obj_set_style_border_color(obj, UI_COLOR_DIVIDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &ui_styles.accent_btn, LV_STATE_PRESSED);
    }

    /* Menu container */
    if (lv_obj_check_type(obj, &lv_menu_cont_class)) {
        lv_obj_add_style(obj, &ui_styles.card, 0);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &ui_styles.focus_glow, LV_STATE_PRESSED);
    }

    /* Menu page */
    if (lv_obj_check_type(obj, &lv_menu_page_class)) {
        static lv_style_t s_menupage;
        static bool s_menupage_inited = false;
        if (!s_menupage_inited) {
            lv_style_init(&s_menupage);
            lv_style_set_border_width(&s_menupage, 0);
            lv_style_set_radius(&s_menupage, 0);
            lv_style_set_pad_all(&s_menupage, 8);
            lv_style_set_bg_opa(&s_menupage, LV_OPA_TRANSP);
            s_menupage_inited = true;
        }
        lv_obj_add_style(obj, &s_menupage, 0);
    }

    /* Scrollbar — accent color for scrollable objects */
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) {
        lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(obj, LV_OPA_60, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(obj, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(obj, 2, LV_PART_SCROLLBAR);
    }

    if (is_focus_control(obj)) {
        lv_obj_add_style(obj, &s_focus_outline_clear, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &s_focus_outline_clear, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &s_focus_outline_clear,
                         static_cast<lv_style_selector_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
        clear_focus_outline(obj);
    }

}

void theme_init()
{
    /* Save the original (default) theme on first call.
       Always use it as parent to avoid self-referencing loops. */
    static lv_theme_t *original_theme = NULL;
    lv_theme_t *th_act = lv_display_get_theme(NULL);
    if (!original_theme) {
        original_theme = th_act;
    }

    static lv_theme_t th_new;
    th_new = *original_theme;
    lv_theme_set_parent(&th_new, original_theme);
    lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);
    lv_display_set_theme(NULL, &th_new);
}
