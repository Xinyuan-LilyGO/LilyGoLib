/**
 * @file      ui_theme.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 */

#include <lvgl.h>
#include "ui_define.h"

ui_styles_t ui_styles;

/* Static per-object-type styles (shared, updated on theme change) */
static lv_style_t s_btn;
static lv_style_t s_switch_off, s_switch_on, s_switch_knob, s_dropdown, s_dropdown_list;
static bool styles_initialized = false;

/* Update only the COLOR properties of already-initialized styles.
   Safe to call repeatedly — does not reset non-color properties. */
static void update_style_colors(void)
{
    /* Shared styles — only update color properties */
    lv_style_set_bg_color(&ui_styles.card, UI_COLOR_CARD_BG);
    lv_style_set_bg_color(&ui_styles.accent_btn, UI_COLOR_ACCENT);
    lv_style_set_border_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_BORDER);
    lv_style_set_outline_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_OUTLINE);
    lv_style_set_border_color(&ui_styles.focus_glow, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.focus_glow, UI_COLOR_ACCENT);
    lv_style_set_bg_color(&ui_styles.slider_track, UI_COLOR_TRACK);
    lv_style_set_bg_color(&ui_styles.slider_knob, UI_COLOR_KNOB);
    lv_style_set_text_color(&ui_styles.accent_text, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.accent_text, UI_COLOR_ACCENT);
    lv_style_set_shadow_color(&ui_styles.accent_shadow, UI_COLOR_ACCENT);

    /* Per-object-type styles */
    lv_style_set_bg_color(&s_btn, UI_COLOR_CARD_BG);
    lv_style_set_bg_color(&s_switch_off, UI_COLOR_TRACK);
    lv_style_set_bg_color(&s_switch_on, UI_COLOR_ACCENT);
    lv_style_set_bg_color(&s_switch_knob, UI_COLOR_KNOB);
    lv_style_set_bg_color(&s_dropdown, UI_COLOR_CARD_BG);
    lv_style_set_border_color(&s_dropdown, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown, UI_COLOR_TEXT_PRIMARY);
    lv_style_set_bg_color(&s_dropdown_list, UI_COLOR_CARD_BG);
    lv_style_set_border_color(&s_dropdown_list, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown_list, UI_COLOR_TEXT_PRIMARY);
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

    /* Accent button */
    lv_style_init(&ui_styles.accent_btn);
    lv_style_set_bg_color(&ui_styles.accent_btn, UI_COLOR_ACCENT);
    lv_style_set_bg_opa(&ui_styles.accent_btn, LV_OPA_COVER);
    lv_style_set_radius(&ui_styles.accent_btn, 8);
    lv_style_set_text_color(&ui_styles.accent_btn, lv_color_white());
    lv_style_set_border_width(&ui_styles.accent_btn, 0);

    /* High-contrast focus ring for widgets whose default background is already accent */
    lv_style_init(&ui_styles.accent_focus_ring);
    lv_style_set_border_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_BORDER);
    lv_style_set_border_width(&ui_styles.accent_focus_ring, 2);
    lv_style_set_border_opa(&ui_styles.accent_focus_ring, LV_OPA_COVER);
    lv_style_set_outline_color(&ui_styles.accent_focus_ring, UI_COLOR_ACCENT_FOCUS_OUTLINE);
    lv_style_set_outline_width(&ui_styles.accent_focus_ring, 1);
    lv_style_set_outline_opa(&ui_styles.accent_focus_ring, LV_OPA_60);
    lv_style_set_outline_pad(&ui_styles.accent_focus_ring, 1);

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
    lv_style_set_text_color(&s_dropdown, UI_COLOR_TEXT_PRIMARY);
    if (lv_disp_get_hor_res(NULL) <= 320) {
        lv_style_set_text_font(&s_dropdown, &lv_font_montserrat_12);
    }

    lv_style_init(&s_dropdown_list);
    lv_style_set_bg_color(&s_dropdown_list, UI_COLOR_CARD_BG);
    lv_style_set_bg_opa(&s_dropdown_list, LV_OPA_COVER);
    lv_style_set_radius(&s_dropdown_list, 8);
    lv_style_set_border_width(&s_dropdown_list, 1);
    lv_style_set_border_color(&s_dropdown_list, UI_COLOR_TRACK);
    lv_style_set_text_color(&s_dropdown_list, UI_COLOR_TEXT_PRIMARY);
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

    /* Re-register theme so new objects get updated styles */
    theme_init();

    /* Walk entire object tree and invalidate each object for redraw */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_tree_walk(scr, invalidate_cb, NULL);

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
        lv_obj_add_style(obj, &ui_styles.accent_btn,
                         static_cast<lv_style_selector_t>(LV_PART_SELECTED) |
                         static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
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
