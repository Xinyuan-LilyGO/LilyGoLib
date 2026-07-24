/**
 * @file      ui_vibe_keyboard.cpp
 * @brief     Vibe Keyboard Screen - LVGL implementation
 *            Generated from Ardot design (480x222px)
 *
 * Layout structure:
 *   Top Bar (28px)       - Title | Session Count | Time | BLE Status
 *   Divider (1px)
 *   Main Content (164px) - Session Detail Card + Encoder scrollbar
 *     Session Detail Card:
 *       Header Row       - Status Dot | Session Label | Index | Spacer | Model Badge
 *       Divider
 *       Stats Row        - STATUS | CONTEXT | COST | TOKENS
 *       Divider
 *       Prompt Section   - "PROMPT" label + prompt text
 *     Encoder (18px wide, right edge)
 *   Divider (1px)
 *   Bottom Bar (28px)    - [Fn Notify] [Space Voice] [CAP Setup]
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <string.h>
#include "ui_vibe_keyboard.h"
#include "vibe_input.h"
#include "vk_protocol.h"

extern "C" {
LV_FONT_DECLARE(AlibabaPuHuiTi_Bold_14px)
LV_FONT_DECLARE(AlibabaPuHuiTi_Regular_12px)
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* ── Color palette (from design) ──────────────────────────────────── */
#define CLR_BG          lv_color_make(242, 243, 245)   /* #F2F3F5 page bg      */
#define CLR_WHITE       lv_color_make(255, 255, 255)
#define CLR_DIVIDER     lv_color_make(209, 211, 215)   /* top/bottom dividers  */
#define CLR_DIVIDER2    lv_color_make(224, 226, 231)   /* inner card dividers  */
#define CLR_ACCENT      lv_color_make( 88, 101, 242)   /* #5865F2 blue-purple  */
#define CLR_TEXT_DARK   lv_color_make( 30,  34,  41)   /* #1E2229 body text    */
#define CLR_TEXT_MID    lv_color_make( 78,  80,  88)   /* #4E5058 session cnt  */
#define CLR_TEXT_MUTED  lv_color_make(128, 136, 153)   /* #808899 muted labels */
#define CLR_BLUETOOTH   lv_color_make( 10, 132, 255)   /* #0A84FF connected BT */
#define CLR_GREEN       lv_color_make( 35, 165,  90)   /* #23A55A active/dot   */
#define CLR_RED         lv_color_make(217,  51,  38)
#define CLR_YELLOW      lv_color_make(240, 178,  50)   /* #F0B232 cost value   */
#define CLR_ENCODER_BG  lv_color_make(224, 226, 231)   /* encoder track bg     */
#define CLR_ENCODER_BD  lv_color_make(209, 211, 215)   /* encoder border       */

/* ── Dimensions ────────────────────────────────────────────────────── */
#define SCREEN_W   480
#define SCREEN_H   222
#define TOPBAR_H    28
#define BTMBAR_H    28
#define CONTENT_H  164  /* SCREEN_H - TOPBAR_H - 1 - 1 - BTMBAR_H        */
#define CARD_PAD_H   6  /* horizontal padding inside main content          */
#define CARD_PAD_V   6  /* vertical padding inside main content            */
#define ENCODER_W   18
#define CARD_INNER_PAD_H 10
#define CARD_INNER_PAD_V  8

static lv_group_t *s_group      = NULL;
static ui_vibe_keyboard_open_session_cb_t s_open_session_cb = NULL;
static ui_vibe_keyboard_voice_cb_t s_voice_cb = NULL;
static bool s_space_down = false;
static bool s_voice_active = false;

static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_listening_overlay = NULL;
static lv_obj_t *s_time_label   = NULL;
static lv_obj_t *s_ble_icon     = NULL;
static lv_obj_t *s_ble_dot      = NULL;
static lv_obj_t *s_model_label  = NULL;
static lv_obj_t *s_ctx_val      = NULL;
static lv_obj_t *s_cost_val     = NULL;
static lv_obj_t *s_tok_val      = NULL;
static lv_obj_t *s_prompt_text  = NULL;
static lv_obj_t *s_sess_name    = NULL;
static lv_obj_t *s_sess_idx     = NULL;
static lv_obj_t *s_sess_label   = NULL;
static lv_obj_t *s_status_dot   = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_enc_track    = NULL;
#define VK_SESSION_COUNT VK_MAX_SESSIONS
#define VK_DEFAULT_SESSION_COUNT 4
#define VK_ENCODER_DOT_COUNT VK_SESSION_COUNT

static lv_obj_t *s_enc_dots[VK_ENCODER_DOT_COUNT]  = {};   /* dot indicator in encoder widget */

static int        s_vk_selected  = 0;
static lv_obj_t  *s_focus_items[VK_SESSION_COUNT] = {};
static int        s_vk_count     = VK_DEFAULT_SESSION_COUNT;
static ui_vibe_keyboard_selection_cb_t s_selection_cb = NULL;

typedef struct {
    char name[36];
    char index[8];
    char model[24];
    char context[10];
    char cost[12];
    char tokens[12];
    char status[16];   /* "ACTIVE" / "IDLE" / "DONE" / "ERROR" */
    char prompt[180];
} vk_session_t;

static vk_session_t vk_sessions[VK_SESSION_COUNT] = {
    {
        "NO SESSION", "#1", "--", "--", "$0.00", "0", "IDLE",
        "Waiting for daemon session data."
    },
    {
        "NO SESSION", "#2", "--", "--", "$0.00", "0", "IDLE",
        "Waiting for daemon session data."
    },
    {
        "NO SESSION", "#3", "--", "--", "$0.00", "0", "IDLE",
        "Waiting for daemon session data."
    },
    {
        "NO SESSION", "#4", "--", "--", "$0.00", "0", "IDLE",
        "Waiting for daemon session data."
    },
};

/* ── helpers ────────────────────────────────────────────────────────── */
static lv_color_t status_color_for_text(const char *status)
{
    if (!status) return CLR_TEXT_MUTED;
    if (strcmp(status, "ERROR") == 0) return CLR_RED;
    if (strcmp(status, "ACTIVE") == 0) return CLR_GREEN;
    return CLR_TEXT_MUTED;
}

static lv_obj_t *make_divider(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, color, 0);
    return div;
}

/* Small filled rounded rectangle dot */
static lv_obj_t *make_dot(lv_obj_t *parent, lv_coord_t size, lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, radius, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

static void encoder_dot_metrics(int count,
                                lv_coord_t *active_size,
                                lv_coord_t *inactive_size,
                                lv_coord_t *pad_row)
{
    if (count > 24) {
        *active_size = 4;
        *inactive_size = 2;
        *pad_row = 1;
    } else if (count > 16) {
        *active_size = 5;
        *inactive_size = 3;
        *pad_row = 2;
    } else {
        *active_size = 6;
        *inactive_size = 4;
        *pad_row = 3;
    }
}

static void update_encoder_dots(void)
{
    lv_coord_t active_size;
    lv_coord_t inactive_size;
    lv_coord_t pad_row;
    encoder_dot_metrics(s_vk_count, &active_size, &inactive_size, &pad_row);

    if (s_enc_track) {
        lv_obj_set_style_pad_row(s_enc_track, pad_row, 0);
    }

    for (int i = 0; i < VK_ENCODER_DOT_COUNT; i++) {
        if (!s_enc_dots[i]) continue;

        if (i >= s_vk_count) {
            lv_obj_add_flag(s_enc_dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(s_enc_dots[i], LV_OBJ_FLAG_HIDDEN);
        bool active = i == s_vk_selected;
        lv_coord_t size = active ? active_size : inactive_size;

        lv_obj_set_size(s_enc_dots[i], size, size);
        lv_obj_set_style_bg_color(s_enc_dots[i], active ? CLR_ACCENT : CLR_TEXT_MUTED, 0);
        lv_obj_set_style_opa(s_enc_dots[i], active ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_radius(s_enc_dots[i], size / 2, 0);
    }
}

/* ── Top Bar ────────────────────────────────────────────────────────── */
static lv_obj_t *create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), TOPBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, CLR_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 8, 0);
    lv_obj_set_style_pad_right(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "VIBE KEYBOARD");
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);

    /* Spacer */
    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    /* Session count */
    s_sess_label = lv_label_create(bar);
    lv_label_set_text(s_sess_label, "4 SESSIONS");
    lv_obj_set_style_text_color(s_sess_label, CLR_TEXT_MID, 0);
    lv_obj_set_style_text_font(s_sess_label, &usr_montserrat_14, 0);

    /* Spacer */
    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_flex_grow(sp2, 1);

    /* Time + Bluetooth status group */
    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 6, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    s_time_label = lv_label_create(right);
    lv_label_set_text(s_time_label, "--:--");
    lv_obj_set_style_text_color(s_time_label, CLR_TEXT_MID, 0);
    lv_obj_set_style_text_font(s_time_label, &usr_montserrat_14, 0);

    lv_obj_t *ble_status = lv_obj_create(right);
    lv_obj_remove_style_all(ble_status);
    lv_obj_set_size(ble_status, 16, 18);
    lv_obj_clear_flag(ble_status, LV_OBJ_FLAG_SCROLLABLE);

    s_ble_icon = lv_label_create(ble_status);
    lv_label_set_text(s_ble_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(s_ble_icon, &usr_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ble_icon, CLR_TEXT_MUTED, 0);
    lv_obj_center(s_ble_icon);

    s_ble_dot = make_dot(ble_status, 5, CLR_TEXT_MUTED, 3);
    lv_obj_align(s_ble_dot, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    return bar;
}

/* ── Bottom Bar ─────────────────────────────────────────────────────── */
static lv_obj_t *make_btn_badge(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, CLR_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, CLR_ACCENT, 0);
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
    lv_obj_set_style_text_color(lbl, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

static lv_obj_t *create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BTMBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, CLR_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 8, 0);
    lv_obj_set_style_pad_right(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav_up = lv_label_create(bar);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_color(nav_up, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);

    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    lv_obj_t *actions = lv_obj_create(bar);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, 22);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    make_btn_badge(actions, "Fn  Notify");
    make_btn_badge(actions, "\xe2\x90\xa3  Voice");
    make_btn_badge(actions, "CAP  Setup");

    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_flex_grow(sp2, 1);

    lv_obj_t *nav_dn = lv_label_create(bar);
    lv_label_set_text(nav_dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_color(nav_dn, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);

    return bar;
}

/* ── Session Detail Card ────────────────────────────────────────────── */
static lv_obj_t *create_session_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    /* width fills the space left of the encoder */
    lv_obj_set_size(card, LV_FLEX_FLOW_ROW, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, CLR_WHITE, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, CLR_ACCENT, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(card, CARD_INNER_PAD_H, 0);
    lv_obj_set_style_pad_right(card, CARD_INNER_PAD_H, 0);
    lv_obj_set_style_pad_top(card, CARD_INNER_PAD_V, 0);
    lv_obj_set_style_pad_bottom(card, CARD_INNER_PAD_V, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header Row ── */
    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    make_dot(hdr, 7, CLR_GREEN, 4);

    s_sess_name = lv_label_create(hdr);
    lv_label_set_text(s_sess_name, vk_sessions[0].name);
    lv_obj_set_style_text_color(s_sess_name, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(s_sess_name, &AlibabaPuHuiTi_Bold_14px, 0);

    s_sess_idx = lv_label_create(hdr);
    lv_label_set_text(s_sess_idx, vk_sessions[0].index);
    lv_obj_set_style_text_color(s_sess_idx, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_sess_idx, &AlibabaPuHuiTi_Bold_14px, 0);

    /* Spacer */
    lv_obj_t *sp = lv_obj_create(hdr);
    lv_obj_remove_style_all(sp);
    lv_obj_set_height(sp, 1);
    lv_obj_set_flex_grow(sp, 1);

    /* Model badge */
    lv_obj_t *badge = lv_obj_create(hdr);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(badge, 3, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(badge, CLR_ACCENT, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, CLR_ACCENT, 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(badge, 6, 0);
    lv_obj_set_style_pad_right(badge, 6, 0);
    lv_obj_set_layout(badge, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    s_model_label = lv_label_create(badge);
    lv_label_set_text(s_model_label, vk_sessions[0].model);
    lv_obj_set_style_text_color(s_model_label, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(s_model_label, &usr_montserrat_12, 0);

    /* ── Divider 1 ── */
    make_divider(card, CLR_DIVIDER2);

    /* ── Stats Row ── */
    lv_obj_t *stats = lv_obj_create(card);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(stats, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(stats, 12, 0);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);

    /* Stat: ACTIVE */
    {
        lv_obj_t *g = lv_obj_create(stats);
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g, 4, 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        s_status_dot = make_dot(g, 6, CLR_GREEN, 3);
        s_status_label = lv_label_create(g);
        lv_label_set_text(s_status_label, vk_sessions[0].status);
        lv_obj_set_style_text_color(s_status_label, status_color_for_text(vk_sessions[0].status), 0);
        lv_obj_set_style_text_font(s_status_label, &usr_montserrat_12, 0);
    }

    /* Stat: CONTEXT */
    {
        lv_obj_t *g = lv_obj_create(stats);
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g, 4, 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *key = lv_label_create(g);
        lv_label_set_text(key, "CONTEXT");
        lv_obj_set_style_text_color(key, CLR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(key, &usr_montserrat_12, 0);
        s_ctx_val = lv_label_create(g);
        lv_label_set_text(s_ctx_val, vk_sessions[0].context);
        lv_obj_set_style_text_color(s_ctx_val, CLR_TEXT_DARK, 0);
        lv_obj_set_style_text_font(s_ctx_val, &usr_montserrat_12, 0);
    }

    /* Stat: COST */
    {
        lv_obj_t *g = lv_obj_create(stats);
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g, 4, 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *key = lv_label_create(g);
        lv_label_set_text(key, "COST");
        lv_obj_set_style_text_color(key, CLR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(key, &usr_montserrat_12, 0);
        s_cost_val = lv_label_create(g);
        lv_label_set_text(s_cost_val, vk_sessions[0].cost);
        lv_obj_set_style_text_color(s_cost_val, CLR_YELLOW, 0);
        lv_obj_set_style_text_font(s_cost_val, &usr_montserrat_12, 0);
    }

    /* Stat: TOKENS */
    {
        lv_obj_t *g = lv_obj_create(stats);
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g, 4, 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *key = lv_label_create(g);
        lv_label_set_text(key, "TOKENS");
        lv_obj_set_style_text_color(key, CLR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(key, &usr_montserrat_12, 0);
        s_tok_val = lv_label_create(g);
        lv_label_set_text(s_tok_val, vk_sessions[0].tokens);
        lv_obj_set_style_text_color(s_tok_val, CLR_TEXT_DARK, 0);
        lv_obj_set_style_text_font(s_tok_val, &usr_montserrat_12, 0);
    }

    /* ── Divider 2 ── */
    make_divider(card, CLR_DIVIDER2);

    /* ── Prompt Section ── */
    lv_obj_t *prompt_sec = lv_obj_create(card);
    lv_obj_remove_style_all(prompt_sec);
    lv_obj_set_size(prompt_sec, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(prompt_sec, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(prompt_sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(prompt_sec, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(prompt_sec, 4, 0);
    lv_obj_clear_flag(prompt_sec, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *p_lbl = lv_label_create(prompt_sec);
    lv_label_set_text(p_lbl, "PROMPT");
    lv_obj_set_style_text_color(p_lbl, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(p_lbl, &usr_montserrat_12, 0);

    s_prompt_text = lv_label_create(prompt_sec);
    lv_label_set_text(s_prompt_text, vk_sessions[0].prompt);
    lv_label_set_long_mode(s_prompt_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_prompt_text, LV_PCT(100));
    lv_obj_set_style_text_color(s_prompt_text, CLR_TEXT_DARK, 0);
    lv_obj_set_style_text_font(s_prompt_text, &AlibabaPuHuiTi_Regular_12px, 0);

    return card;
}

/* ── Encoder (right-edge scrollbar widget) ──────────────────────────── */
static lv_obj_t *create_encoder_widget(lv_obj_t *parent)
{
    lv_obj_t *enc = lv_obj_create(parent);
    lv_obj_remove_style_all(enc);
    lv_obj_set_size(enc, ENCODER_W, LV_PCT(100));
    lv_obj_set_style_radius(enc, 9, 0);
    lv_obj_set_style_bg_opa(enc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(enc, CLR_ENCODER_BG, 0);
    lv_obj_set_style_border_width(enc, 1, 0);
    lv_obj_set_style_border_color(enc, CLR_ENCODER_BD, 0);
    lv_obj_set_style_border_opa(enc, LV_OPA_COVER, 0);
    lv_obj_set_layout(enc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(enc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(enc, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(enc, 8, 0);
    lv_obj_set_style_pad_bottom(enc, 8, 0);
    lv_obj_clear_flag(enc, LV_OBJ_FLAG_SCROLLABLE);

    /* Up arrow */
    lv_obj_t *up = lv_label_create(enc);
    lv_label_set_text(up, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(up, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(up, &usr_montserrat_12, 0);

    /* Track with dots */
    lv_obj_t *track = lv_obj_create(enc);
    s_enc_track = track;
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(track, 1);
    lv_obj_set_layout(track, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(track, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(track, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(track, 3, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < VK_ENCODER_DOT_COUNT; i++) {
        s_enc_dots[i] = make_dot(track, 4, CLR_TEXT_MUTED, 2);
        lv_obj_set_style_opa(s_enc_dots[i], LV_OPA_50, 0);
    }
    update_encoder_dots();

    /* Down arrow */
    lv_obj_t *dn = lv_label_create(enc);
    lv_label_set_text(dn, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(dn, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(dn, &usr_montserrat_12, 0);

    return enc;
}

/* ── Internal: update detail card to show session at index ─────────── */
static void update_detail_for_index(int idx)
{
    if (idx < 0 || idx >= s_vk_count) return;
    s_vk_selected = idx;
    const vk_session_t *s = &vk_sessions[idx];

    if (s_sess_name)   lv_label_set_text(s_sess_name,   s->name);
    if (s_sess_idx)    lv_label_set_text(s_sess_idx,    s->index);
    if (s_model_label) lv_label_set_text(s_model_label, s->model);
    if (s_ctx_val)     lv_label_set_text(s_ctx_val,     s->context);
    if (s_cost_val)    lv_label_set_text(s_cost_val,    s->cost);
    if (s_tok_val)     lv_label_set_text(s_tok_val,     s->tokens);
    if (s_prompt_text) lv_label_set_text(s_prompt_text, s->prompt);
    if (s_status_label) {
        lv_color_t status_color = status_color_for_text(s->status);
        lv_label_set_text(s_status_label, s->status);
        lv_obj_set_style_text_color(s_status_label, status_color, 0);
        if (s_status_dot) {
            lv_obj_set_style_bg_color(s_status_dot, status_color, 0);
        }
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d / %d", idx + 1, s_vk_count);
    if (s_sess_label) lv_label_set_text(s_sess_label, buf);

    update_encoder_dots();

    if (s_selection_cb) {
        s_selection_cb(idx);
    }
}

static lv_obj_t *make_mic_part(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                               lv_coord_t x, lv_coord_t y, lv_coord_t radius)
{
    lv_obj_t *part = lv_obj_create(parent);
    lv_obj_remove_style_all(part);
    lv_obj_set_size(part, width, height);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_style_radius(part, radius, 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(part, CLR_ACCENT, 0);
    lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    return part;
}

static void create_listening_overlay(lv_obj_t *parent)
{
    s_listening_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_listening_overlay);
    lv_obj_set_size(s_listening_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_listening_overlay, 0, 0);
    lv_obj_set_style_bg_opa(s_listening_overlay, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(s_listening_overlay, lv_color_black(), 0);
    lv_obj_add_flag(s_listening_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_listening_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_listening_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog = lv_obj_create(s_listening_overlay);
    lv_obj_remove_style_all(dialog);
    lv_obj_set_size(dialog, 190, 82);
    lv_obj_set_style_radius(dialog, 6, 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dialog, CLR_WHITE, 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_border_color(dialog, CLR_ACCENT, 0);
    lv_obj_set_layout(dialog, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dialog, 14, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(dialog);

    lv_obj_t *icon = lv_obj_create(dialog);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 48, 48);
    lv_obj_set_style_radius(icon, 6, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(icon, CLR_ACCENT, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    make_mic_part(icon, 10, 20, 19, 7, 5);
    make_mic_part(icon, 2, 12, 14, 16, 1);
    make_mic_part(icon, 2, 12, 32, 16, 1);
    make_mic_part(icon, 18, 2, 15, 27, 1);
    make_mic_part(icon, 2, 7, 23, 28, 1);
    make_mic_part(icon, 12, 2, 18, 35, 1);

    lv_obj_t *label = lv_label_create(dialog);
    lv_label_set_text(label, "正在监听...");
    lv_obj_set_style_text_color(label, CLR_TEXT_DARK, 0);
    lv_obj_set_style_text_font(label, &AlibabaPuHuiTi_Bold_14px, 0);

    lv_obj_add_flag(s_listening_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void set_listening_overlay_visible(bool visible)
{
    if (!s_listening_overlay) return;
    if (visible) {
        lv_obj_clear_flag(s_listening_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_listening_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_state_cb(uint32_t key, bool pressed)
{
    if (key != 0) return;
    if (pressed == s_space_down) return;
    s_space_down = pressed;

    if (pressed) {
        if (lv_screen_active() == s_screen) {
            s_voice_active = true;
            set_listening_overlay_visible(true);
            if (s_voice_cb) s_voice_cb(true);
        }
    } else if (s_voice_active) {
        s_voice_active = false;
        set_listening_overlay_visible(false);
        if (s_voice_cb) s_voice_cb(false);
    }
}

/* ── Invisible focus items ──────────────────────────────────────────── */
static void create_focus_items(lv_obj_t *parent)
{
    s_group = lv_group_create();
    lv_group_set_wrap(s_group, true);
    for (int i = 0; i < VK_SESSION_COUNT; i++) {
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
            for (int j = 0; j < VK_SESSION_COUNT; j++) {
                if (s_focus_items[j] == self) {
                    if (j >= s_vk_count) {
                        lv_group_focus_obj(s_focus_items[s_vk_count - 1]);
                        return;
                    }
                    update_detail_for_index(j);
                    break;
                }
            }
        }, LV_EVENT_FOCUSED, NULL);

        lv_obj_add_event_cb(item, [](lv_event_t *e) {
            /* encoder center press on any session item → open session modal */
            if (s_open_session_cb) s_open_session_cb();
        }, LV_EVENT_CLICKED, NULL);

        if (i < s_vk_count) {
            lv_group_add_obj(s_group, item);
        }
    }
    lv_group_focus_obj(s_focus_items[0]);
}

/**
 * @brief  Build the Vibe Keyboard screen on @p parent.
 *         Call once; the screen owns all child objects.
 *
 * @param  parent  Parent object (e.g. lv_scr_act() or a dedicated screen).
 */
void ui_vibe_keyboard_create(lv_obj_t *parent)
{
    s_screen = parent;

    /* Page background */
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, CLR_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Top bar */
    create_top_bar(parent);

    /* Top divider */
    make_divider(parent, CLR_DIVIDER);

    /* Main content row */
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_H);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(content, CARD_PAD_H, 0);
    lv_obj_set_style_pad_right(content, CARD_PAD_H, 0);
    lv_obj_set_style_pad_top(content, CARD_PAD_V, 0);
    lv_obj_set_style_pad_bottom(content, CARD_PAD_V, 0);
    lv_obj_set_style_pad_column(content, 6, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    create_session_card(content);
    create_encoder_widget(content);

    /* Bottom divider */
    make_divider(parent, CLR_DIVIDER);

    /* Bottom bar */
    create_bottom_bar(parent);

    /* Create invisible focus items for encoder navigation */
    create_focus_items(parent);

    create_listening_overlay(parent);

    vk_input_set_key_state_callback(keyboard_state_cb);
}

/**
 * @brief  Update dynamic data on the Vibe Keyboard screen.
 *
 * Pass NULL for any field you don't want to change.
 */
void ui_vibe_keyboard_update(const char *time_str,
                              const char *session_count,
                              const char *model_name,
                              const char *context_tokens,
                              const char *cost,
                              const char *tokens,
                              const char *prompt)
{
    if (time_str     && s_time_label)   lv_label_set_text(s_time_label,  time_str);
    if (session_count && s_sess_label)  lv_label_set_text(s_sess_label,  session_count);
    if (model_name   && s_model_label)  lv_label_set_text(s_model_label, model_name);
    if (context_tokens && s_ctx_val)    lv_label_set_text(s_ctx_val,     context_tokens);
    if (cost         && s_cost_val)     lv_label_set_text(s_cost_val,    cost);
    if (tokens       && s_tok_val)      lv_label_set_text(s_tok_val,     tokens);
    if (prompt       && s_prompt_text)  lv_label_set_text(s_prompt_text, prompt);
}

void ui_vibe_keyboard_set_ble_connected(bool connected)
{
    lv_color_t color = connected ? CLR_BLUETOOTH : CLR_TEXT_MUTED;
    lv_opa_t opacity = connected ? LV_OPA_COVER : LV_OPA_50;

    if (s_ble_icon) {
        lv_obj_set_style_text_color(s_ble_icon, color, 0);
        lv_obj_set_style_opa(s_ble_icon, opacity, 0);
    }
    if (s_ble_dot) {
        lv_obj_set_style_bg_color(s_ble_dot, connected ? CLR_GREEN : CLR_TEXT_MUTED, 0);
        lv_obj_set_style_opa(s_ble_dot, opacity, 0);
    }
}

void ui_vibe_keyboard_move(int delta)
{
    int next = (s_vk_selected + (delta % s_vk_count) + s_vk_count) % s_vk_count;
    if (next == s_vk_selected) return;
    update_detail_for_index(next);
    if (s_focus_items[next]) {
        lv_group_focus_obj(s_focus_items[next]);
    }
}

void ui_vibe_keyboard_select(int index)
{
    if (index < 0) index = 0;
    if (index >= s_vk_count) index = s_vk_count - 1;
    update_detail_for_index(index);
    if (s_focus_items[index]) {
        lv_group_focus_obj(s_focus_items[index]);
    }
}

int ui_vibe_keyboard_selected_index(void)
{
    return s_vk_selected;
}

void ui_vibe_keyboard_set_session_count(uint8_t count)
{
    if (count == 0) count = 1;
    if (count > VK_SESSION_COUNT) count = VK_SESSION_COUNT;
    int previous_count = s_vk_count;
    int selected = s_vk_selected;
    if (selected >= count) {
        selected = count - 1;
    }

    s_vk_count = count;
    s_vk_selected = selected;

    if (s_group && count < previous_count && s_focus_items[selected]) {
        lv_group_focus_obj(s_focus_items[selected]);
    }

    if (s_group && count > previous_count) {
        for (int i = previous_count; i < count; i++) {
            lv_group_add_obj(s_group, s_focus_items[i]);
        }
    } else if (s_group && count < previous_count) {
        for (int i = count; i < previous_count; i++) {
            lv_group_remove_obj(s_focus_items[i]);
        }
    }

    update_detail_for_index(s_vk_selected);
}

void ui_vibe_keyboard_set_session(uint8_t index,
                                  const char *name,
                                  const char *model,
                                  const char *context_tokens,
                                  const char *cost,
                                  const char *tokens,
                                  const char *status,
                                  const char *prompt)
{
    if (index >= VK_SESSION_COUNT) return;
    vk_session_t *s = &vk_sessions[index];
    snprintf(s->index, sizeof(s->index), "#%u", (unsigned)(index + 1));
    if (name)           lv_snprintf(s->name,    sizeof(s->name),    "%s", name);
    if (model)          lv_snprintf(s->model,   sizeof(s->model),   "%s", model);
    if (context_tokens) lv_snprintf(s->context, sizeof(s->context), "%s", context_tokens);
    if (cost)           lv_snprintf(s->cost,    sizeof(s->cost),    "%s", cost);
    if (tokens)         lv_snprintf(s->tokens,  sizeof(s->tokens),  "%s", tokens);
    if (status)         lv_snprintf(s->status,  sizeof(s->status),  "%s", status);
    if (prompt)         lv_snprintf(s->prompt,  sizeof(s->prompt),  "%s", prompt);

    if ((int)index == s_vk_selected) {
        update_detail_for_index(s_vk_selected);
    }
}

void ui_vibe_keyboard_set_open_session_cb(ui_vibe_keyboard_open_session_cb_t cb)
{
    s_open_session_cb = cb;
}

void ui_vibe_keyboard_set_selection_cb(ui_vibe_keyboard_selection_cb_t cb)
{
    s_selection_cb = cb;
}

void ui_vibe_keyboard_set_voice_cb(ui_vibe_keyboard_voice_cb_t cb)
{
    s_voice_cb = cb;
}

lv_group_t *ui_vibe_keyboard_get_group(void)
{
    return s_group;
}
