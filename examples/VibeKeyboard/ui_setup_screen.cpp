/**
 * @file      ui_setup_screen.cpp
 * @brief     Setup Screen - navigation menu (AI Agent / YOLO / Sound / About)
 *
 * Layout (480x222):
 *   Top Bar (28px)  - "SETUP" title
 *   Divider (1px)
 *   Content (164px) - scrollable nav rows (44px each)
 *   Divider (1px)
 *   Bottom Bar (28px) - ↑ SCROLL  ● CONFIRM  ↓ SCROLL
 */
#include <LilyGoLib.h>
#include <lvgl.h>

#include "ui_setup_screen.h"

extern "C" {
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* ── Palette ─────────────────────────────────────────────────────────── */
#define SS_BG          lv_color_make(242, 243, 245)
#define SS_WHITE       lv_color_make(255, 255, 255)
#define SS_DIVIDER     lv_color_make(209, 211, 215)
#define SS_TEXT_DARK   lv_color_make( 30,  34,  41)
#define SS_TEXT_MUTED  lv_color_make(128, 136, 153)
#define SS_SEL_BG      lv_color_make(226, 234, 249)
#define SS_SEL_BORDER  lv_color_make( 88, 130, 244)
#define SS_ACCENT      lv_color_make( 88, 101, 242)
#define SS_GREEN       lv_color_make( 35, 165,  90)

#define TOPBAR_H   28
#define BTMBAR_H   28
#define SCREEN_W  480
#define SCREEN_H  222
#define ROW_H      44

/* ── Menu items ──────────────────────────────────────────────────────── */
typedef struct {
    const char *label;
} menu_item_t;

#define ITEM_COUNT 4
static const menu_item_t s_items[ITEM_COUNT] = {
    { "AI Agent" },
    { "YOLO"     },
    { "Sound"    },
    { "About"    },
};

static int8_t    s_selected = 0;
static lv_obj_t *s_rows[ITEM_COUNT] = {};
static lv_obj_t *s_focus_items[ITEM_COUNT] = {};
static lv_group_t *s_group = NULL;
static ui_setup_screen_confirm_cb_t s_confirm_cb = NULL;

/* ── helpers ─────────────────────────────────────────────────────────── */
static lv_obj_t *make_divider(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, SS_DIVIDER, 0);
    return d;
}

static void update_row_style(int8_t idx, bool selected)
{
    if (!s_rows[idx]) return;
    if (selected) {
        lv_obj_set_style_bg_color(s_rows[idx], SS_SEL_BG, 0);
        lv_obj_set_style_border_color(s_rows[idx], SS_SEL_BORDER, 0);
        lv_obj_set_style_border_width(s_rows[idx], 1, 0);
    } else {
        lv_obj_set_style_bg_color(s_rows[idx], SS_WHITE, 0);
        lv_obj_set_style_border_color(s_rows[idx], SS_DIVIDER, 0);
        lv_obj_set_style_border_width(s_rows[idx], 1, 0);
    }
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, SS_ACCENT, 0);
    lv_obj_set_style_border_color(btn, SS_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_left(btn, 6, 0);
    lv_obj_set_style_pad_right(btn, 6, 0);
    lv_obj_set_style_pad_top(btn, 3, 0);
    lv_obj_set_style_pad_bottom(btn, 3, 0);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, SS_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

static void select_item(int8_t idx)
{
    if (idx < 0) idx = ITEM_COUNT - 1;
    if (idx >= ITEM_COUNT) idx = 0;
    if (idx == s_selected) {
        if (s_rows[s_selected]) lv_obj_scroll_to_view(s_rows[s_selected], LV_ANIM_ON);
        return;
    }

    update_row_style(s_selected, false);
    s_selected = idx;
    update_row_style(s_selected, true);
    lv_obj_scroll_to_view(s_rows[s_selected], LV_ANIM_ON);
}

/* ── Top Bar ─────────────────────────────────────────────────────────── */
static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), TOPBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, SS_WHITE, 0);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "SETUP");
    lv_obj_set_style_text_color(title, SS_TEXT_DARK, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
}

/* ── Content List ────────────────────────────────────────────────────── */
static void create_content(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), SCREEN_H - TOPBAR_H - 1 - 1 - BTMBAR_H);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(list, SS_BG, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 16, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        s_rows[i] = row;

        /* Label */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, s_items[i].label);
        lv_obj_set_style_text_color(lbl, SS_TEXT_DARK, 0);
        lv_obj_set_style_text_font(lbl, &usr_montserrat_14, 0);
        lv_obj_set_flex_grow(lbl, 1);

        /* Arrow */
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, ">");
        lv_obj_set_style_text_color(arrow, SS_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(arrow, &usr_montserrat_14, 0);

        update_row_style(i, i == 0);
    }
}

/* ── Bottom Bar ──────────────────────────────────────────────────────── */
static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BTMBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, SS_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *up = lv_label_create(bar);
    lv_label_set_text(up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_color(up, SS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(up, &usr_montserrat_12, 0);

    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_size(sp1, 1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    lv_obj_t *actions = lv_obj_create(bar);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, 22);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    make_bottom_btn(actions, "\xe2\x86\x90 BACK");
    make_bottom_btn(actions, "\xe2\x97\x8f  CONFIRM");

    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_size(sp2, 1, 1);
    lv_obj_set_flex_grow(sp2, 1);

    lv_obj_t *dn = lv_label_create(bar);
    lv_label_set_text(dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_color(dn, SS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(dn, &usr_montserrat_12, 0);
}

/* ── Encoder focus bridge ────────────────────────────────────────────── */
static void create_focus_items(lv_obj_t *parent)
{
    s_group = lv_group_create();
    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_t *item = lv_obj_create(parent);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, 0, 0);
        lv_obj_set_pos(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE);
        s_focus_items[i] = item;

        lv_obj_add_event_cb(item, [](lv_event_t *e) {
            lv_obj_t *self = (lv_obj_t *)lv_event_get_target(e);
            for (int j = 0; j < ITEM_COUNT; j++) {
                if (s_focus_items[j] == self) {
                    select_item((int8_t)j);
                    break;
                }
            }
        }, LV_EVENT_FOCUSED, NULL);

        lv_obj_add_event_cb(item, [](lv_event_t *e) {
            if (s_confirm_cb) s_confirm_cb(s_selected);
        }, LV_EVENT_CLICKED, NULL);

        lv_group_add_obj(s_group, item);
    }
    lv_group_focus_obj(s_focus_items[0]);
}

/* ── Public API ──────────────────────────────────────────────────────── */
void ui_setup_screen_create(lv_obj_t *parent)
{
    s_selected = 0;

    lv_obj_remove_style_all(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, SS_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(parent);
    make_divider(parent);
    create_content(parent);
    make_divider(parent);
    create_bottom_bar(parent);
    create_focus_items(parent);
}

void ui_setup_screen_move(int8_t dir)
{
    select_item((s_selected + dir + ITEM_COUNT) % ITEM_COUNT);
    if (s_focus_items[s_selected]) {
        lv_group_focus_obj(s_focus_items[s_selected]);
    }
}

/* Returns the index of the currently selected menu item (0-based). */
int8_t ui_setup_screen_selected(void)
{
    return s_selected;
}

void ui_setup_screen_confirm(void)
{
    if (s_confirm_cb) s_confirm_cb(s_selected);
}

void ui_setup_screen_set_confirm_cb(ui_setup_screen_confirm_cb_t cb)
{
    s_confirm_cb = cb;
}

lv_group_t *ui_setup_screen_get_group(void)
{
    return s_group;
}
