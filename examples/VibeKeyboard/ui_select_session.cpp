/**
 * @file      ui_select_session.cpp
 * @brief     Select Session Screen - LVGL implementation
 *            Generated from Ardot design (480x222px)
 */
#include <LilyGoLib.h>
#include <lvgl.h>
#include "ui_select_session.h"

extern "C" {
LV_FONT_DECLARE(AlibabaPuHuiTi_Bold_14px)
LV_FONT_DECLARE(AlibabaPuHuiTi_Regular_12px)
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_scr_act  lv_screen_active
#endif

/* ── Color palette ──────────────────────────────────────────────────── */
#define SS_BG            lv_color_make(242, 243, 245)
#define SS_WHITE         lv_color_make(255, 255, 255)
#define SS_DIVIDER       lv_color_make(209, 211, 215)
#define SS_GREEN         lv_color_make( 35, 165,  90)
#define SS_GREEN_LIGHT   lv_color_make( 78, 154, 107)
#define SS_GREEN_CONFIRM lv_color_make( 35, 165,  90)
#define SS_SEL_BG        lv_color_make(226, 234, 249)
#define SS_SEL_BORDER    lv_color_make( 88, 130, 244)
#define SS_TEXT_DARK     lv_color_make( 30,  34,  41)
#define SS_TEXT_MUTED    lv_color_make(128, 132, 142)
#define SS_TEXT_DONE     lv_color_make( 78,  84,  92)
#define SS_RED           lv_color_make(242,  63,  67)

static lv_obj_t *s_count_label      = NULL;
static lv_obj_t *s_list             = NULL;
static lv_obj_t *s_screen_focus     = NULL;
static int        s_selected         = 0;
static uint8_t    s_session_count    = 0;
static lv_group_t *s_ss_group        = NULL;
static ui_select_session_confirm_cb_t s_confirm_cb = NULL;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *badge_box;
    lv_obj_t *badge_label;
    lv_obj_t *name_label;
    lv_obj_t *status_label;
    lv_obj_t *subtitle_label;
} session_row_widgets_t;

static session_row_widgets_t *s_rows = NULL;
static uint8_t s_row_capacity = 0;

/* ── Helpers ────────────────────────────────────────────────────────── */
static void set_row_style(lv_obj_t *row, bool selected)
{
    if (!row) return;
    if (selected) {
        lv_obj_set_style_bg_color(row, SS_SEL_BG, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, SS_SEL_BORDER, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(row, SS_WHITE, 0);
        lv_obj_set_style_border_width(row, 0, 0);
    }
}

static void update_count_label(void)
{
    if (!s_count_label) return;
    char buf[16];
    if (s_session_count == 0) {
        snprintf(buf, sizeof(buf), "0 / 0");
    } else {
        snprintf(buf, sizeof(buf), "%d / %u", s_selected + 1, (unsigned)s_session_count);
    }
    lv_label_set_text(s_count_label, buf);
}

static lv_obj_t *make_badge(lv_obj_t *parent, const char *text, lv_color_t bg, lv_obj_t **label_out)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 14);
    lv_obj_set_style_radius(badge, 2, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, bg, 0);
    lv_obj_set_style_pad_left(badge, 4, 0);
    lv_obj_set_style_pad_right(badge, 4, 0);
    lv_obj_set_layout(badge, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, SS_WHITE, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    if (label_out) *label_out = lbl;
    return badge;
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, SS_GREEN_CONFIRM, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, SS_GREEN_CONFIRM, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(btn, 6, 0);
    lv_obj_set_style_pad_right(btn, 6, 0);
    lv_obj_set_style_pad_top(btn, 3, 0);
    lv_obj_set_style_pad_bottom(btn, 3, 0);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, SS_GREEN_CONFIRM, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

static void select_row(uint8_t idx)
{
    if (s_session_count == 0 || idx >= s_session_count) return;
    if (s_selected < s_row_capacity) {
        set_row_style(s_rows[s_selected].row, false);
    }
    s_selected = idx;
    set_row_style(s_rows[s_selected].row, true);
    lv_obj_scroll_to_view(s_rows[s_selected].row, LV_ANIM_ON);
    update_count_label();
}

static uint8_t row_index_for_obj(lv_obj_t *obj)
{
    for (uint8_t i = 0; i < s_row_capacity; i++) {
        if (s_rows[i].row == obj) return i;
    }
    return 0xFF;
}

static void row_focused_cb(lv_event_t *e)
{
    uint8_t idx = row_index_for_obj((lv_obj_t *)lv_event_get_current_target(e));
    if (idx == 0xFF) return;
    if (idx >= s_session_count) {
        if (s_session_count > 0 && s_ss_group) {
            lv_group_focus_obj(s_rows[s_selected].row);
        }
        return;
    }
    select_row(idx);
}

static void row_clicked_cb(lv_event_t *e)
{
    uint8_t idx = row_index_for_obj((lv_obj_t *)lv_event_get_current_target(e));
    if (idx != 0xFF && idx < s_session_count) select_row(idx);
    if (s_confirm_cb) s_confirm_cb(s_session_count == 0 ? -1 : s_selected);
}

static void create_row(lv_obj_t *parent, uint8_t idx)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(row, row_focused_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(row, row_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_rows[idx].row = row;
    set_row_style(row, false);

    /* Top row: badge + name + status */
    lv_obj_t *top = lv_obj_create(row);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 6, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    s_rows[idx].badge_box = make_badge(top, "", SS_GREEN, &s_rows[idx].badge_label);

    lv_obj_t *name = lv_label_create(top);
    lv_label_set_text(name, "");
    lv_obj_set_style_text_color(name, SS_TEXT_DARK, 0);
    lv_obj_set_style_text_font(name, &AlibabaPuHuiTi_Bold_14px, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    s_rows[idx].name_label = name;

    lv_obj_t *stag = lv_label_create(top);
    lv_label_set_text(stag, "");
    lv_obj_set_style_text_color(stag, SS_GREEN, 0);
    lv_obj_set_style_text_font(stag, &usr_montserrat_12, 0);
    s_rows[idx].status_label = stag;

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(row);
    lv_label_set_text(sub, "");
    lv_obj_set_style_text_color(sub, SS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sub, &AlibabaPuHuiTi_Regular_12px, 0);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(sub, LV_PCT(100));
    s_rows[idx].subtitle_label = sub;

    if (s_ss_group) {
        lv_group_add_obj(s_ss_group, row);
    }
}

static bool ensure_row_capacity(uint8_t count)
{
    if (count <= s_row_capacity) return true;

    session_row_widgets_t *next = new session_row_widgets_t[count]();
    if (!next) return false;

    for (uint8_t i = 0; i < s_row_capacity; i++) {
        next[i] = s_rows[i];
    }
    delete[] s_rows;
    s_rows = next;

    for (uint8_t i = s_row_capacity; i < count; i++) {
        create_row(s_list, i);
        lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }

    s_row_capacity = count;
    return true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

static void create_focus_group(void)
{
    s_ss_group = lv_group_create();
}

void ui_select_session_create(lv_obj_t *parent)
{
    s_screen_focus = parent;
    s_selected = 0;
    s_session_count = 0;
    create_focus_group();
    lv_group_add_obj(s_ss_group, parent);

    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, SS_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header ── */
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header, SS_WHITE, 0);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_style_pad_right(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "SESSION");
    lv_obj_set_style_text_color(title, SS_GREEN, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
    lv_obj_set_flex_grow(title, 1);

    s_count_label = lv_label_create(header);
    lv_obj_set_style_text_color(s_count_label, SS_GREEN_LIGHT, 0);
    lv_obj_set_style_text_font(s_count_label, &usr_montserrat_12, 0);
    update_count_label();

    /* ── Top Divider ── */
    lv_obj_t *div_top = lv_obj_create(parent);
    lv_obj_remove_style_all(div_top);
    lv_obj_set_size(div_top, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(div_top, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div_top, SS_DIVIDER, 0);

    /* ── Session List (scrollable) ── */
    lv_obj_t *list = lv_obj_create(parent);
    s_list = list;
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), 164);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(list, SS_BG, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    /* scrollable so selected row always comes into view */
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    /* ── Bottom Divider ── */
    lv_obj_t *div_btm = lv_obj_create(parent);
    lv_obj_remove_style_all(div_btm);
    lv_obj_set_size(div_btm, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(div_btm, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div_btm, SS_DIVIDER, 0);

    /* ── Bottom Bar ── */
    lv_obj_t *btm = lv_obj_create(parent);
    lv_obj_remove_style_all(btm);
    lv_obj_set_size(btm, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(btm, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btm, SS_WHITE, 0);
    lv_obj_set_layout(btm, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btm, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(btm, 10, 0);
    lv_obj_set_style_pad_right(btm, 10, 0);
    lv_obj_set_style_pad_column(btm, 0, 0);
    lv_obj_clear_flag(btm, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav_up = lv_label_create(btm);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_color(nav_up, SS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);

    lv_obj_t *sp1 = lv_obj_create(btm);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    lv_obj_t *actions = lv_obj_create(btm);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, 22);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    make_bottom_btn(actions, "\xe2\x86\x90 BACK");
    make_bottom_btn(actions, "\xe2\x97\x8f  CONFIRM");

    lv_obj_t *sp2 = lv_obj_create(btm);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_flex_grow(sp2, 1);

    lv_obj_t *nav_dn = lv_label_create(btm);
    lv_label_set_text(nav_dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_color(nav_dn, SS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);

    update_count_label();
}

void ui_select_session_set_count(const char *count_str)
{
    if (count_str && s_count_label) {
        lv_label_set_text(s_count_label, count_str);
    }
}

void ui_select_session_set_session_count(uint8_t count)
{
    if (!s_list || !ensure_row_capacity(count)) return;

    if (s_session_count > 0 && s_selected < s_row_capacity) {
        set_row_style(s_rows[s_selected].row, false);
    }
    s_session_count = count;

    if (s_session_count == 0) {
        s_selected = 0;
        if (s_ss_group && s_screen_focus) {
            lv_group_focus_obj(s_screen_focus);
        }
    } else if (s_selected >= s_session_count) {
        s_selected = s_session_count - 1;
    }

    for (uint8_t i = 0; i < s_row_capacity; i++) {
        if (!s_rows[i].row) continue;
        if (i < s_session_count) {
            lv_obj_clear_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rows[i].row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_session_count > 0) {
        set_row_style(s_rows[s_selected].row, true);
        if (s_ss_group) lv_group_focus_obj(s_rows[s_selected].row);
    }
    update_count_label();
}

void ui_select_session_set_row(uint8_t row,
                               const char *badge,
                               bool badge_ok,
                               const char *name,
                               const char *status,
                               const char *subtitle)
{
    if (row >= s_row_capacity && !ensure_row_capacity(row + 1)) return;
    if (row >= s_session_count) {
        s_session_count = row + 1;
        lv_obj_clear_flag(s_rows[row].row, LV_OBJ_FLAG_HIDDEN);
    }

    session_row_widgets_t *rw = &s_rows[row];
    lv_color_t state_color = badge_ok ? SS_GREEN : SS_RED;
    if (rw->badge_box) {
        lv_obj_set_style_bg_color(rw->badge_box, state_color, 0);
    }
    if (rw->badge_label && badge) {
        lv_label_set_text(rw->badge_label, badge);
    }
    if (rw->name_label && name) {
        lv_label_set_text(rw->name_label, name);
        lv_obj_set_style_text_color(rw->name_label, SS_TEXT_DARK, 0);
    }
    if (rw->status_label && status) {
        lv_label_set_text(rw->status_label, status);
        lv_obj_set_style_text_color(rw->status_label, state_color, 0);
    }
    if (rw->subtitle_label && subtitle) {
        lv_label_set_text(rw->subtitle_label, subtitle);
        lv_obj_set_style_text_color(rw->subtitle_label,
                                    badge_ok ? SS_TEXT_MUTED : SS_RED, 0);
    }
}

void ui_select_session_move(int delta)
{
    if (s_session_count == 0) return;
    int next = s_selected + delta;
    if (next < 0) next = 0;
    if (next >= s_session_count) next = s_session_count - 1;
    if (next == s_selected) return;

    select_row((uint8_t)next);
    if (s_ss_group && s_selected < s_row_capacity) {
        lv_group_focus_obj(s_rows[s_selected].row);
    }
}

void ui_select_session_select(uint8_t index)
{
    if (s_session_count == 0) return;
    if (index >= s_session_count) index = s_session_count - 1;
    select_row(index);
    if (s_ss_group && s_selected < s_row_capacity) {
        lv_group_focus_obj(s_rows[s_selected].row);
    }
}

void ui_select_session_focus_current(void)
{
    if (!s_ss_group) return;
    if (s_session_count > 0 && s_selected < s_row_capacity) {
        lv_group_focus_obj(s_rows[s_selected].row);
    } else if (s_screen_focus) {
        lv_group_focus_obj(s_screen_focus);
    }
}

int ui_select_session_selected_index()
{
    return s_session_count == 0 ? -1 : s_selected;
}

void ui_select_session_set_confirm_cb(ui_select_session_confirm_cb_t cb)
{
    s_confirm_cb = cb;
}

lv_group_t *ui_select_session_get_group(void)
{
    return s_ss_group;
}
