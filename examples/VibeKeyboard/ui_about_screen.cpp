/**
 * @file      ui_about_screen.cpp
 * @brief     About Screen - device info, firmware version, hardware specs
 *
 * Layout (480x222):
 *   Header (32px)   - "ABOUT" title
 *   Divider (1px)
 *   Scroll Content (160px) - single card with 4 info rows
 *   Divider (1px)
 *   Bottom Bar (28px) - ↑ SCROLL | ← BACK | ↓ SCROLL
 */
#include <LilyGoLib.h>
#include <esp_mac.h>
#include <lvgl.h>
#include <stdio.h>

#include "ui_about_screen.h"

extern "C" {
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* Colors from Ardot design */
#define AB_BG          lv_color_make(242, 243, 245)   /* #F2F3F5 - screen background */
#define AB_WHITE       lv_color_make(255, 255, 255)   /* #FFFFFF  */
#define AB_DIVIDER     lv_color_make(209, 211, 215)   /* #D1D3D7 - top divider */
#define AB_DIVIDER2    lv_color_make(224, 226, 231)   /* #E0E2E7 - row dividers */
#define AB_CARD_BORDER lv_color_make(209, 211, 215)   /* #D1D3D7 - card border */
#define AB_TEXT_DARK   lv_color_make( 30,  34,  41)   /* #1E2229 - title & value */
#define AB_TEXT_MUTED  lv_color_make(128, 132, 142)   /* #80848E - label */
#define AB_FIRMWARE    lv_color_make(240, 178,  50)   /* #F0B232 - firmware value */
#define AB_ACCENT      lv_color_make( 88, 101, 242)   /* #5865F2 - github link */

#define HEADER_H   32
#define BOTTOM_H   28
#define SCREEN_H  222
#define ROW_H      44
#define ABOUT_SCROLL_STEP       30
#define ABOUT_SCROLL_FOCUS_MAX  16

static lv_obj_t *s_scroll_content = NULL;
static lv_group_t *s_group = NULL;
static lv_obj_t *s_focus_items[ABOUT_SCROLL_FOCUS_MAX] = {};
static int8_t s_scroll_focus_count = 0;
static int8_t s_scroll_focus_index = 0;

static const char *get_device_name(void)
{
    static char device_name[sizeof("vibe-keyboard-ffff")] = "vibe-keyboard-0000";
    uint8_t mac[6] = {};

    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(device_name, sizeof(device_name), "vibe-keyboard-%02x%02x", mac[4], mac[5]);
    }

    return device_name;
}

static lv_obj_t *make_row_divider(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, AB_DIVIDER2, 0);
    return d;
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, AB_ACCENT, 0);
    lv_obj_set_style_border_color(btn, AB_ACCENT, 0);
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
    lv_obj_set_style_text_color(lbl, AB_ACCENT, 0);
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

    int32_t target = (int32_t)index * ABOUT_SCROLL_STEP;
    int32_t max_scroll = get_scroll_max();
    if (target > max_scroll) target = max_scroll;
    lv_obj_scroll_to_y(s_scroll_content, target, anim);
}

static void make_info_row(lv_obj_t *parent, const char *key, const char *val, lv_color_t val_color)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, AB_WHITE, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, AB_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(k, &usr_montserrat_12, 0);
    lv_obj_set_flex_grow(k, 1);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, val);
    lv_obj_set_style_text_color(v, val_color, 0);
    lv_obj_set_style_text_font(v, &usr_montserrat_12, 0);
}

/* ── Header ──────────────────────────────────────────────────────────── */
static void create_header(lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hdr, AB_WHITE, 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);
    lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "ABOUT");
    lv_obj_set_style_text_color(title, AB_TEXT_DARK, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
}

/* ── Scroll Content ──────────────────────────────────────────────────── */
static void create_content(lv_obj_t *parent)
{
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_size(scroll, LV_PCT(100), SCREEN_H - HEADER_H - 1 - 1 - BOTTOM_H);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scroll, AB_BG, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scroll, 8, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    s_scroll_content = scroll;

    /* Card */
    lv_obj_t *card = lv_obj_create(scroll);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, AB_WHITE, 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, AB_CARD_BORDER, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    make_info_row(card, "Device Name", get_device_name(),               AB_TEXT_DARK);
    make_row_divider(card);
    make_info_row(card, "Model",       "T-LoRa Pager",                  AB_TEXT_DARK);
    make_row_divider(card);
    make_info_row(card, "Firmware",    "v0.1.0-dev",                    AB_FIRMWARE);
    make_row_divider(card);
    make_info_row(card, "GitHub",      "github.com/Xinyuan-LilyGO/LilyGoLib", AB_ACCENT);
}

/* ── Bottom Bar ──────────────────────────────────────────────────────── */
static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BOTTOM_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, AB_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav_up = lv_label_create(bar);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_color(nav_up, AB_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);

    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_size(sp1, 1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    make_bottom_btn(bar, "\xe2\x86\x90 BACK");

    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_size(sp2, 1, 1);
    lv_obj_set_flex_grow(sp2, 1);

    lv_obj_t *nav_dn = lv_label_create(bar);
    lv_label_set_text(nav_dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_color(nav_dn, AB_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);
}

/* ── Encoder focus bridge ────────────────────────────────────────────── */
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

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            ui_about_screen_move(-1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            ui_about_screen_move(1);
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
        s_scroll_focus_count = (int8_t)((max_scroll + ABOUT_SCROLL_STEP - 1) / ABOUT_SCROLL_STEP + 1);
    } else {
        s_scroll_focus_count = ABOUT_SCROLL_FOCUS_MAX;
    }
    if (s_scroll_focus_count > ABOUT_SCROLL_FOCUS_MAX) s_scroll_focus_count = ABOUT_SCROLL_FOCUS_MAX;

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
void ui_about_screen_create(lv_obj_t *parent)
{
    s_scroll_content = NULL;
    s_group = NULL;
    s_scroll_focus_count = 0;
    s_scroll_focus_index = 0;
    for (int8_t i = 0; i < ABOUT_SCROLL_FOCUS_MAX; i++) {
        s_focus_items[i] = NULL;
    }

    lv_obj_remove_style_all(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, AB_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_header(parent);

    /* Top divider */
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, AB_DIVIDER, 0);

    create_content(parent);

    /* Bottom divider */
    lv_obj_t *div_btm = lv_obj_create(parent);
    lv_obj_remove_style_all(div_btm);
    lv_obj_set_size(div_btm, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(div_btm, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div_btm, AB_DIVIDER, 0);

    create_bottom_bar(parent);
    create_focus_items(parent);
}

void ui_about_screen_move(int8_t dir)
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

lv_group_t *ui_about_screen_get_group(void)
{
    return s_group;
}
