/**
 * @file      ui_keys_screen.cpp
 * @brief     Keys Screen - keymap viewer with Record/Edit per function key
 *
 * Layout (480x222):
 *   Header   (28px)  - "KEYS" title
 *   Divider  (1px)
 *   Scroll   (164px) - scrollable list of key-binding cards
 *   Divider  (1px)
 *   Bottom   (28px)  - ↑ SCROLL | ● CONFIRM | ↓ SCROLL
 *
 * Each card (448×103):
 *   Key Name  (JetBrains Mono Bold 13)
 *   Desc      (JetBrains Mono Regular 13, muted)
 *   Binding   (JetBrains Mono Regular 12, dark-muted)
 *   Buttons   [Record] [Edit] [Clear?]
 */
#include <LilyGoLib.h>
#include <lvgl.h>

#include "ui_keys_screen.h"

extern "C" {
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* ── Colour palette (from design) ───────────────────────────────────── */
#define KS_BG           lv_color_make(242, 243, 245)   /* #F2F3F5 */
#define KS_WHITE        lv_color_make(255, 255, 255)
#define KS_DIVIDER      lv_color_make(209, 211, 215)   /* #D1D3D7 */
#define KS_TEXT_DARK    lv_color_make( 30,  34,  41)   /* #1E2229 */
#define KS_TEXT_MUTED   lv_color_make(128, 132, 142)   /* #80848E */
#define KS_TEXT_DIM     lv_color_make( 78,  80,  88)   /* #4E5058 */
#define KS_ACCENT       lv_color_make( 88, 101, 242)   /* #5865F2 */
#define KS_BTN_RECORD   lv_color_make( 88, 101, 242)   /* #5865F2 solid */
#define KS_BTN_GHOST    lv_color_make(224, 227, 232)   /* #E0E3E8 */
#define KS_BTN_BORDER   lv_color_make(209, 211, 215)   /* #D1D3D7 */
#define KS_CARD_BG      lv_color_make(255, 255, 255)
#define KS_CARD_BORDER  lv_color_make(209, 211, 215)
#define KS_SCROLL_TRACK lv_color_make(224, 227, 232)
#define KS_SCROLL_THUMB lv_color_make( 88, 101, 242)
#define KS_CONFIRM_BG   lv_color_make( 88, 101, 242)   /* 12% opacity approx */

/* ── Dimensions ──────────────────────────────────────────────────────── */
#define SCREEN_W  480
#define SCREEN_H  222
#define HEADER_H   28
#define BOTTOM_H   28
#define SCROLL_H  (SCREEN_H - HEADER_H - 1 - 1 - BOTTOM_H)  /* 164 */
#define CARD_W    448
#define CARD_H    103
#define CARD_GAP    6
#define CARD_PAD   10
#define SCROLL_BAR_W  4
#define KEYS_SCROLL_FOCUS_MAX  16

/* ── Key entry ───────────────────────────────────────────────────────── */
typedef struct {
    const char *name;
    const char *desc;
    const char *binding;      /* NULL → "(default action)" */
    bool        has_clear;    /* DELETE card has a third Clear button */
} ks_key_def_t;

static const ks_key_def_t s_keys[] = {
    { "DELETE",  "Delete to beginning of line", "ctrl_u",          true  },
    { "CANCEL",  "Cancel current operation",    NULL,               false },
    { "MODE",    "Switch input mode",           NULL,               false },
    { "SESSION", "Select session",              NULL,               false },
    { "SEND",    "Send message to AI",          NULL,               false },
    { "VOICE",   "Toggle voice input",          NULL,               false },
};
#define KEY_COUNT  ((int)(sizeof(s_keys) / sizeof(s_keys[0])))

/* ── Runtime state ───────────────────────────────────────────────────── */
static lv_obj_t  *s_scroll_cont    = NULL;
static int        s_selected_idx   = 0;   /* highlighted card (0-based) */
static lv_group_t *s_group = NULL;
static lv_obj_t  *s_focus_items[KEYS_SCROLL_FOCUS_MAX] = {};
static int8_t     s_scroll_focus_count = 0;
static int8_t     s_scroll_focus_index = 0;

/* Per-card binding labels (updated live) */
static lv_obj_t  *s_binding_lbl[KEY_COUNT] = {};

/* ── Helpers ─────────────────────────────────────────────────────────── */
static lv_obj_t *make_divider(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, KS_DIVIDER, 0);
    return d;
}

static lv_obj_t *make_btn(lv_obj_t *row, const char *text, bool primary)
{
    lv_obj_t *btn = lv_obj_create(row);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, primary ? 80 : 70, 21);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, primary ? KS_BTN_RECORD : KS_BTN_GHOST, 0);
    if (!primary) {
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, KS_BTN_BORDER, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    }
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, primary ? KS_WHITE : KS_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

static lv_obj_t *make_card(lv_obj_t *parent, int idx)
{
    const ks_key_def_t *k = &s_keys[idx];

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, CARD_H, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, KS_CARD_BG, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, KS_CARD_BORDER, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(card, 8, 0);
    lv_obj_set_style_pad_bottom(card, 8, 0);
    lv_obj_set_style_pad_left(card, CARD_PAD, 0);
    lv_obj_set_style_pad_right(card, CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Key Name */
    lv_obj_t *name_lbl = lv_label_create(card);
    lv_label_set_text(name_lbl, k->name);
    lv_obj_set_style_text_color(name_lbl, KS_TEXT_DARK, 0);
    lv_obj_set_style_text_font(name_lbl, &usr_montserrat_14, 0);

    /* Description */
    lv_obj_t *desc_lbl = lv_label_create(card);
    lv_label_set_text(desc_lbl, k->desc);
    lv_obj_set_style_text_color(desc_lbl, KS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(desc_lbl, &usr_montserrat_12, 0);

    /* Binding */
    lv_obj_t *bind_lbl = lv_label_create(card);
    lv_label_set_text(bind_lbl, k->binding ? k->binding : "(default action)");
    lv_obj_set_style_text_color(bind_lbl, KS_TEXT_DIM, 0);
    lv_obj_set_style_text_font(bind_lbl, &usr_montserrat_12, 0);
    s_binding_lbl[idx] = bind_lbl;

    /* Button row */
    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 6, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    make_btn(btn_row, "Record", true);
    make_btn(btn_row, "Edit",   false);
    if (k->has_clear) {
        make_btn(btn_row, "Clear", false);
    }

    return card;
}

/* ── Header ──────────────────────────────────────────────────────────── */
static void create_header(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, KS_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "KEYS");
    lv_obj_set_style_text_color(title, KS_TEXT_DARK, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
}

/* ── Scroll area ─────────────────────────────────────────────────────── */
static void create_scroll_area(lv_obj_t *parent)
{
    /* Clip container */
    lv_obj_t *clip = lv_obj_create(parent);
    lv_obj_remove_style_all(clip);
    lv_obj_set_size(clip, LV_PCT(100), SCROLL_H);
    lv_obj_set_style_bg_opa(clip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(clip, KS_BG, 0);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrollable content column */
    lv_obj_t *cont = lv_obj_create(clip);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, SCREEN_W - SCROLL_BAR_W - 2, LV_SIZE_CONTENT);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cont, KS_BG, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_pad_row(cont, CARD_GAP, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    s_scroll_cont = cont;

    for (int i = 0; i < KEY_COUNT; i++) {
        make_card(cont, i);
    }

    /* Note section */
    lv_obj_t *note = lv_obj_create(cont);
    lv_obj_remove_style_all(note);
    lv_obj_set_size(note, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_layout(note, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(note, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(note, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(note, 2, 0);
    lv_obj_clear_flag(note, LV_OBJ_FLAG_SCROLLABLE);

    const char *note_lines[] = {
        "Record: press key combo (Escape to cancel)",
        "Edit: type action name (fn, enter, ctrl_shift_a, etc.)",
    };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *nl = lv_label_create(note);
        lv_label_set_text(nl, note_lines[i]);
        lv_obj_set_width(nl, CARD_W);
        lv_label_set_long_mode(nl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(nl, KS_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(nl, &usr_montserrat_12, 0);
    }

    /* Scroll track + thumb (decorative) */
    lv_obj_t *track = lv_obj_create(clip);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, SCROLL_BAR_W, SCROLL_H);
    lv_obj_set_pos(track, SCREEN_W - SCROLL_BAR_W - 2, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, KS_SCROLL_TRACK, 0);

    lv_obj_t *thumb = lv_obj_create(clip);
    lv_obj_remove_style_all(thumb);
    lv_obj_set_size(thumb, SCROLL_BAR_W, 60);
    lv_obj_set_pos(thumb, SCREEN_W - SCROLL_BAR_W - 2, 20);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(thumb, KS_SCROLL_THUMB, 0);
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, KS_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, KS_ACCENT, 0);
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
    lv_obj_set_style_text_color(lbl, KS_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

/* ── Bottom bar ──────────────────────────────────────────────────────── */
static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BOTTOM_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, KS_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* ↑ SCROLL */
    lv_obj_t *nav_up = lv_label_create(bar);
    lv_label_set_text(nav_up, "\xE2\x86\x91 SCROLL");
    lv_obj_set_style_text_color(nav_up, KS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);

    /* Spacer */
    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    /* BACK + CONFIRM buttons */
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

    /* Spacer */
    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_flex_grow(sp2, 1);

    /* ↓ SCROLL */
    lv_obj_t *nav_dn = lv_label_create(bar);
    lv_label_set_text(nav_dn, "\xE2\x86\x93 SCROLL");
    lv_obj_set_style_text_color(nav_dn, KS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);
}

/* ── Scroll helpers ──────────────────────────────────────────────────── */
static int s_scroll_offset = 0;
#define SCROLL_STEP  (CARD_H + CARD_GAP)

static void scroll_to(int offset)
{
    if (!s_scroll_cont) return;
    int max_scroll = KEY_COUNT * SCROLL_STEP - SCROLL_H + 16;
    if (max_scroll < 0) max_scroll = 0;
    if (offset < 0) offset = 0;
    if (offset > max_scroll) offset = max_scroll;
    s_scroll_offset = offset;
    lv_obj_set_y(s_scroll_cont, -offset);
}

/* ── Encoder focus bridge ────────────────────────────────────────────── */
static void focus_item_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *self = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        for (int8_t i = 0; i < s_scroll_focus_count; i++) {
            if (s_focus_items[i] == self) {
                s_scroll_focus_index = i;
                scroll_to((int)i * SCROLL_STEP);
                break;
            }
        }
        return;
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            ui_keys_screen_scroll_up();
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            ui_keys_screen_scroll_down();
            lv_event_stop_bubbling(e);
        }
    }
}

static void enable_focus_event_bubble(lv_obj_t *item, lv_obj_t *page)
{
    for (lv_obj_t *obj = item; obj && obj != page; obj = lv_obj_get_parent(obj)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void create_focus_items(lv_obj_t *parent)
{
    int max_scroll = KEY_COUNT * SCROLL_STEP - SCROLL_H + 16;
    if (max_scroll > 0) {
        s_scroll_focus_count = (int8_t)((max_scroll + SCROLL_STEP - 1) / SCROLL_STEP + 1);
    } else {
        s_scroll_focus_count = KEYS_SCROLL_FOCUS_MAX;
    }
    if (s_scroll_focus_count > KEYS_SCROLL_FOCUS_MAX) s_scroll_focus_count = KEYS_SCROLL_FOCUS_MAX;

    s_group = lv_group_create();
    lv_group_set_wrap(s_group, false);
    for (int8_t i = 0; i < s_scroll_focus_count; i++) {
        lv_obj_t *item = lv_obj_create(parent);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, 0, 0);
        lv_obj_set_pos(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        enable_focus_event_bubble(item, parent);
        lv_obj_add_event_cb(item, focus_item_event_cb, LV_EVENT_ALL, NULL);
        s_focus_items[i] = item;
        lv_group_add_obj(s_group, item);
    }

    s_scroll_focus_index = 0;
    lv_group_focus_obj(s_focus_items[0]);
}

/* ── Public API ──────────────────────────────────────────────────────── */
void ui_keys_screen_create(lv_obj_t *parent)
{
    s_scroll_offset = 0;
    s_scroll_cont   = NULL;
    s_group = NULL;
    s_scroll_focus_count = 0;
    s_scroll_focus_index = 0;
    for (int8_t i = 0; i < KEYS_SCROLL_FOCUS_MAX; i++) {
        s_focus_items[i] = NULL;
    }

    lv_obj_remove_style_all(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, KS_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_header(parent);
    make_divider(parent);
    create_scroll_area(parent);
    make_divider(parent);
    create_bottom_bar(parent);
    create_focus_items(parent);
}

void ui_keys_screen_scroll_up(void)
{
    scroll_to(s_scroll_offset - SCROLL_STEP);
    if (s_scroll_focus_count > 0) {
        s_scroll_focus_index = (int8_t)(s_scroll_offset / SCROLL_STEP);
    }
}

void ui_keys_screen_scroll_down(void)
{
    scroll_to(s_scroll_offset + SCROLL_STEP);
    if (s_scroll_focus_count > 0) {
        s_scroll_focus_index = (int8_t)(s_scroll_offset / SCROLL_STEP);
    }
}

void ui_keys_screen_set_binding(int key_idx, const char *binding_str)
{
    if (key_idx < 0 || key_idx >= KEY_COUNT) return;
    if (!s_binding_lbl[key_idx]) return;
    lv_label_set_text(s_binding_lbl[key_idx],
                      (binding_str && binding_str[0]) ? binding_str : "(default action)");
}

lv_group_t *ui_keys_screen_get_group(void)
{
    return s_group;
}
