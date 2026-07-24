/**
 * @file      ui_yolo_screen.cpp
 * @brief     YOLO Mode settings screen
 *
 * Layout (480x222):
 *   Top Bar (28px)    - "YOLO" title
 *   Divider (1px)
 *   Scroll Content (164px) - YOLO Mode section card:
 *     Section Header  - ⚡ YOLO Mode
 *     Divider
 *     Active Row      - "active"  | Toggle
 *     Divider
 *     Allow label     - "allow rules (one per line)"
 *     Allow text area - green bg, green text
 *     Divider
 *     Deny label      - "deny rules (one per line)"
 *     Deny text area  - red bg, red text
 *     Divider
 *     Notify Row      - "notify on auto-allow" | Toggle
 *     Notify desc     - description text
 *   Divider (1px)
 *   Bottom Bar (28px) - ↑ SCROLL | ← BACK | ● CONFIRM | ↓ SCROLL
 *
 * Design source: Ardot "YOLO Screen" frame
 *   BG          #F2F3F5  (242,243,245)
 *   Panel       #FFFFFF  (255,255,255)
 *   Divider     #D1D3D7  (209,211,215)
 *   Text        #1E2228  (30,34,40)
 *   Text Muted  #808590  (128,133,144)
 *   Text Sub    #4E5058  (78,80,88)
 *   Accent      #585EF2  (88,94,242)
 *   Green       #23A45B  (35,164,91)
 *   Green BG    #F0F7F0  (240,247,240)  allow area
 *   Green Txt   #23A55A  (35,165,90)
 *   Red         #F2171D  (242,23,29)    (unused)
 *   Red BG      #FEF0F0  (254,240,240)  deny area
 *   Red Txt     #F23F42  (242,63,66)
 */
#include <LilyGoLib.h>
#include <lvgl.h>

#include "ui_yolo_screen.h"

extern "C" {
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* ── Palette ─────────────────────────────────────────────────────────────── */
#define YS_BG           lv_color_make(242, 243, 245)
#define YS_PANEL        lv_color_make(255, 255, 255)
#define YS_DIVIDER      lv_color_make(209, 211, 215)
#define YS_TEXT         lv_color_make( 30,  34,  40)
#define YS_TEXT_MUTED   lv_color_make(128, 133, 144)
#define YS_TEXT_SUB     lv_color_make( 78,  80,  88)
#define YS_ACCENT       lv_color_make( 88,  94, 242)
#define YS_GREEN        lv_color_make( 35, 164,  91)
#define YS_GREEN_BG     lv_color_make(240, 247, 240)
#define YS_GREEN_TXT    lv_color_make( 35, 165,  90)
#define YS_RED_BG       lv_color_make(254, 240, 240)
#define YS_RED_TXT      lv_color_make(242,  63,  66)

/* ── Layout constants ────────────────────────────────────────────────────── */
#define TOPBAR_H   28
#define BTMBAR_H   28
#define SCREEN_H  222
#define CONTENT_H (SCREEN_H - TOPBAR_H - 1 - 1 - BTMBAR_H)  /* 164px */
#define SECTION_PAD 10
#define YOLO_SCROLL_STEP       30
#define YOLO_SCROLL_FOCUS_MAX  16

static lv_obj_t *s_scroll_content = NULL;
static lv_group_t *s_group = NULL;
static lv_obj_t *s_focus_items[YOLO_SCROLL_FOCUS_MAX] = {};
static lv_obj_t *s_allow_textarea = NULL;
static lv_obj_t *s_deny_textarea = NULL;
static lv_obj_t *s_allow_rules_item = NULL;
static lv_obj_t *s_deny_rules_item = NULL;
static lv_obj_t *s_active_toggle = NULL;
static lv_obj_t *s_notify_toggle = NULL;
static int8_t s_scroll_focus_count = 0;
static int8_t s_scroll_focus_index = 0;
static bool s_active = true;
static bool s_notify_auto_allow = true;
static ui_yolo_config_cb_t s_config_cb = NULL;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static lv_obj_t *make_divider(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    return d;
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(btn, YS_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, YS_ACCENT, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_hor(btn, 6, 0);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 4, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, YS_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

static int32_t get_scroll_max(void)
{
    if (!s_scroll_content) return 0;
    lv_obj_update_layout(s_scroll_content);
    int32_t max_scroll = lv_obj_get_scroll_y(s_scroll_content) +
                         lv_obj_get_scroll_bottom(s_scroll_content);
    return max_scroll > 0 ? max_scroll : 0;
}

static void scroll_to_focus_index(int8_t index, lv_anim_enable_t anim)
{
    if (!s_scroll_content || s_scroll_focus_count <= 0) return;

    if (index < 0) index = 0;
    if (index >= s_scroll_focus_count) index = s_scroll_focus_count - 1;
    s_scroll_focus_index = index;

    int32_t target = (int32_t)index * YOLO_SCROLL_STEP;
    int32_t max_scroll = get_scroll_max();
    if (target > max_scroll) target = max_scroll;
    lv_obj_scroll_to_y(s_scroll_content, target, anim);
}

static bool is_rules_textarea(lv_obj_t *obj)
{
    return obj == s_allow_rules_item || obj == s_deny_rules_item ||
           obj == s_allow_textarea || obj == s_deny_textarea;
}

static bool is_page_back_key(uint32_t key)
{
    return key == LV_KEY_LEFT || key == LV_KEY_BACKSPACE || key == '\b' ||
           key == 0x7F || key == LV_KEY_ESC;
}

static lv_obj_t *make_rules_textarea(lv_obj_t *parent,
                                     lv_obj_t **textarea,
                                     const char *text,
                                     lv_color_t bg_color,
                                     lv_color_t text_color)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), 60);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, bg_color, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, YS_DIVIDER, 0);
    lv_obj_set_style_radius(box, 3, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_style_outline_width(box, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(box, YS_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(box, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(box, 1, LV_STATE_FOCUSED);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ta = lv_textarea_create(box);
    lv_obj_remove_style_all(ta);
    lv_obj_set_size(ta, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 0, 0);
    lv_obj_set_style_text_color(ta, text_color, 0);
    lv_obj_set_style_text_font(ta, &usr_montserrat_12, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(ta, false);
    lv_textarea_set_max_length(ta, 192);
    lv_textarea_set_text(ta, text);
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    if (lv_obj_get_group(ta)) {
        lv_group_remove_obj(ta);
    }
    if (textarea) {
        *textarea = ta;
    }
    return box;
}

/* Toggle widget: 32×18 green pill, knob on right */
static lv_obj_t *make_toggle_on(lv_obj_t *parent)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, 32, 18);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, YS_GREEN, 0);
    lv_obj_set_style_radius(track, 9, 0);
    lv_obj_set_layout(track, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(track, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(track, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_outline_width(track, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(track, YS_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(track, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(track, 2, LV_STATE_FOCUSED);

    lv_obj_t *knob = lv_obj_create(track);
    lv_obj_remove_style_all(knob);
    lv_obj_set_size(knob, 14, 14);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(knob, YS_PANEL, 0);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_margin_right(knob, 2, 0);
    return track;
}

static void set_toggle_state(lv_obj_t *track, bool enabled)
{
    if (!track) return;
    lv_obj_set_style_bg_color(track, enabled ? YS_GREEN : YS_DIVIDER, 0);
    lv_obj_set_flex_align(track,
                          enabled ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

/* ── Top Bar ─────────────────────────────────────────────────────────────── */
static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), TOPBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, YS_PANEL, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, SECTION_PAD, 0);
    lv_obj_set_style_pad_right(bar, SECTION_PAD, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "YOLO");
    lv_obj_set_style_text_color(title, YS_TEXT, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
}

/* ── Section Card ────────────────────────────────────────────────────────── */
static void create_section_card(lv_obj_t *parent)
{
    /* Card container */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, YS_PANEL, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, YS_DIVIDER, 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(card, SECTION_PAD, 0);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Section header: ⚡ + "YOLO Mode" */
    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), 28);
    lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(hdr);
    lv_label_set_text(icon, "\xe2\x9a\xa1"); /* ⚡ U+26A1 */
    lv_obj_set_style_text_color(icon, YS_TEXT, 0);
    lv_obj_set_style_text_font(icon, &usr_montserrat_14, 0);

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, "YOLO Mode");
    lv_obj_set_style_text_color(hdr_lbl, YS_TEXT, 0);
    lv_obj_set_style_text_font(hdr_lbl, &usr_montserrat_14, 0);

    make_divider(card, YS_BG);

    /* Active row */
    lv_obj_t *active_row = lv_obj_create(card);
    lv_obj_remove_style_all(active_row);
    lv_obj_set_size(active_row, LV_PCT(100), 32);
    lv_obj_set_layout(active_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(active_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(active_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(active_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *active_lbl = lv_label_create(active_row);
    lv_label_set_text(active_lbl, "active");
    lv_obj_set_style_text_color(active_lbl, YS_TEXT_SUB, 0);
    lv_obj_set_style_text_font(active_lbl, &usr_montserrat_12, 0);
    lv_obj_set_flex_grow(active_lbl, 1);

    /* The active switch is the first encoder focus target. */
    s_active_toggle = make_toggle_on(active_row);
    s_focus_items[0] = s_active_toggle;
    set_toggle_state(s_active_toggle, s_active);

    make_divider(card, YS_BG);

    /* Allow rules label */
    lv_obj_t *allow_lbl = lv_label_create(card);
    lv_label_set_text(allow_lbl, "allow rules (one per line)");
    lv_obj_set_style_text_color(allow_lbl, YS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(allow_lbl, &usr_montserrat_12, 0);

    /* Allow text area */
    s_allow_rules_item = make_rules_textarea(card,
                                             &s_allow_textarea,
                                             "Read(*)\nGlob(*)\nGrep(*)",
                                             YS_GREEN_BG,
                                             YS_GREEN_TXT);
    s_focus_items[1] = s_allow_rules_item;

    make_divider(card, YS_BG);

    /* Deny rules label */
    lv_obj_t *deny_lbl = lv_label_create(card);
    lv_label_set_text(deny_lbl, "deny rules (one per line)");
    lv_obj_set_style_text_color(deny_lbl, YS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(deny_lbl, &usr_montserrat_12, 0);

    /* Deny text area */
    s_deny_rules_item = make_rules_textarea(card,
                                            &s_deny_textarea,
                                            "Bash(git push*)\nBash(rm -rf*)\nBash(sudo*)",
                                            YS_RED_BG,
                                            YS_RED_TXT);
    s_focus_items[2] = s_deny_rules_item;

    make_divider(card, YS_BG);

    /* Notify row */
    lv_obj_t *notify_row = lv_obj_create(card);
    lv_obj_remove_style_all(notify_row);
    lv_obj_set_size(notify_row, LV_PCT(100), 32);
    lv_obj_set_layout(notify_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(notify_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(notify_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(notify_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *notify_lbl = lv_label_create(notify_row);
    lv_label_set_text(notify_lbl, "notify on auto-allow");
    lv_obj_set_style_text_color(notify_lbl, YS_TEXT_SUB, 0);
    lv_obj_set_style_text_font(notify_lbl, &usr_montserrat_12, 0);
    lv_obj_set_flex_grow(notify_lbl, 1);

    s_notify_toggle = make_toggle_on(notify_row);
    s_focus_items[3] = s_notify_toggle;
    set_toggle_state(s_notify_toggle, s_notify_auto_allow);

    /* Notify description */
    lv_obj_t *notify_desc = lv_label_create(card);
    lv_label_set_text(notify_desc, "show notification when YOLO auto-allows a tool");
    lv_obj_set_style_text_color(notify_desc, YS_TEXT_SUB, 0);
    lv_obj_set_style_text_font(notify_desc, &usr_montserrat_12, 0);
    lv_obj_set_width(notify_desc, LV_PCT(100));
}

/* ── Scroll Content ──────────────────────────────────────────────────────── */
static void create_scroll_content(lv_obj_t *parent)
{
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_size(scroll, LV_PCT(100), CONTENT_H);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scroll, YS_BG, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scroll, 8, 0);
    lv_obj_set_style_pad_row(scroll, 0, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ACTIVE);
    s_scroll_content = scroll;

    create_section_card(scroll);
}

/* ── Bottom Bar ──────────────────────────────────────────────────────────── */
static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BTMBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, YS_PANEL, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* ↑ SCROLL */
    lv_obj_t *nav_up = lv_label_create(bar);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");  /* ↑ U+2191 */
    lv_obj_set_style_text_color(nav_up, YS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);

    /* Spacer left */
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
    make_bottom_btn(actions, "\xe2\x97\x8f  CONFIRM");  /* ● U+25CF */

    /* Spacer right */
    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_flex_grow(sp2, 1);

    /* ↓ SCROLL */
    lv_obj_t *nav_down = lv_label_create(bar);
    lv_label_set_text(nav_down, "\xe2\x86\x93 SCROLL");  /* ↓ U+2193 */
    lv_obj_set_style_text_color(nav_down, YS_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_down, &usr_montserrat_12, 0);
}

/* ── Encoder focus bridge ───────────────────────────────────────────────── */
static void focus_item_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *self = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        for (int8_t i = 0; i < s_scroll_focus_count; i++) {
            if (s_focus_items[i] == self) {
                scroll_to_focus_index(i, LV_ANIM_ON);
                break;
            }
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (is_rules_textarea(self)) {
            return;
        }
        ui_yolo_screen_confirm();
        return;
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            ui_yolo_screen_move(-1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            ui_yolo_screen_move(1);
            lv_event_stop_bubbling(e);
        } else if (is_rules_textarea(self) && !is_page_back_key(key)) {
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
            ui_yolo_screen_confirm();
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
    int32_t max_scroll = get_scroll_max();
    if (max_scroll > 0) {
        s_scroll_focus_count = (int8_t)((max_scroll + YOLO_SCROLL_STEP - 1) / YOLO_SCROLL_STEP + 1);
    } else {
        s_scroll_focus_count = YOLO_SCROLL_FOCUS_MAX;
    }
    if (s_scroll_focus_count > YOLO_SCROLL_FOCUS_MAX) s_scroll_focus_count = YOLO_SCROLL_FOCUS_MAX;

    s_group = lv_group_create();
    lv_group_set_wrap(s_group, false);
    for (int8_t i = 0; i < s_scroll_focus_count; i++) {
        lv_obj_t *item = s_focus_items[i];
        if (!item) {
            item = lv_obj_create(parent);
            lv_obj_remove_style_all(item);
            lv_obj_set_size(item, 0, 0);
            lv_obj_set_pos(item, 0, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        enable_focus_event_bubble(item, parent);
        lv_obj_add_event_cb(item, focus_item_event_cb, LV_EVENT_ALL, NULL);
        s_focus_items[i] = item;
        lv_group_add_obj(s_group, item);
    }

    s_scroll_focus_index = 0;
    lv_group_focus_obj(s_focus_items[0]);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void ui_yolo_screen_create(lv_obj_t *parent)
{
    s_scroll_content = NULL;
    s_group = NULL;
    s_allow_textarea = NULL;
    s_deny_textarea = NULL;
    s_allow_rules_item = NULL;
    s_deny_rules_item = NULL;
    s_active_toggle = NULL;
    s_notify_toggle = NULL;
    s_scroll_focus_count = 0;
    s_scroll_focus_index = 0;
    for (int8_t i = 0; i < YOLO_SCROLL_FOCUS_MAX; i++) {
        s_focus_items[i] = NULL;
    }

    lv_obj_remove_style_all(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, YS_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(parent);
    make_divider(parent, YS_DIVIDER);
    create_scroll_content(parent);
    make_divider(parent, YS_DIVIDER);
    create_bottom_bar(parent);
    create_focus_items(parent);
}

void ui_yolo_screen_move(int8_t dir)
{
    if (!s_scroll_content) return;

    int8_t next = s_scroll_focus_index + dir;
    if (next < 0) next = 0;
    if (next >= s_scroll_focus_count) next = s_scroll_focus_count - 1;

    if (s_focus_items[next]) {
        lv_group_focus_obj(s_focus_items[next]);
    } else {
        scroll_to_focus_index(next, LV_ANIM_ON);
    }
}

void ui_yolo_screen_confirm(void)
{
    if (s_scroll_focus_index == 0) {
        s_active = !s_active;
        set_toggle_state(s_active_toggle, s_active);
        ui_yolo_screen_submit();
    } else if (s_scroll_focus_index == 3) {
        s_notify_auto_allow = !s_notify_auto_allow;
        set_toggle_state(s_notify_toggle, s_notify_auto_allow);
        ui_yolo_screen_submit();
    }
}

void ui_yolo_screen_submit(void)
{
    if (!s_config_cb || !s_allow_textarea || !s_deny_textarea) return;
    s_config_cb(s_active,
                s_notify_auto_allow,
                lv_textarea_get_text(s_allow_textarea),
                lv_textarea_get_text(s_deny_textarea));
}

void ui_yolo_screen_set_config(bool active,
                               bool notify_auto_allow,
                               const char *allow_rules,
                               const char *deny_rules)
{
    s_active = active;
    s_notify_auto_allow = notify_auto_allow;
    set_toggle_state(s_active_toggle, s_active);
    set_toggle_state(s_notify_toggle, s_notify_auto_allow);
    if (s_allow_textarea) {
        lv_textarea_set_text(s_allow_textarea, allow_rules ? allow_rules : "");
    }
    if (s_deny_textarea) {
        lv_textarea_set_text(s_deny_textarea, deny_rules ? deny_rules : "");
    }
}

void ui_yolo_screen_set_config_cb(ui_yolo_config_cb_t callback)
{
    s_config_cb = callback;
}

lv_group_t *ui_yolo_screen_get_group(void)
{
    return s_group;
}
