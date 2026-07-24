/**
 * @file      ui_notify_detail_screen.cpp
 * @brief     Notify Detail Screen LVGL implementation (Ardot node 24:24)
 */
#include "ui_notify_detail_screen.h"

extern "C" {
LV_FONT_DECLARE(AlibabaPuHuiTi_Regular_12px)
LV_FONT_DECLARE(AlibabaPuHuiTi_Regular_14px)
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#define ND_SCREEN_W  480
#define ND_HEADER_H   28
#define ND_BOTTOM_H   28
#define ND_CARD_PAD   16
#define ND_CARD_GAP    8
#define ND_CARD_R     12
#define ND_BADGE_H    16
#define ND_BADGE_R     2
#define ND_MSG_BOX_R   8
#define ND_MSG_BOX_H  38

static lv_obj_t *s_hdr_title    = NULL;
static lv_obj_t *s_badge_lbl    = NULL;
static lv_obj_t *s_session_lbl  = NULL;
static lv_obj_t *s_ts_lbl       = NULL;
static lv_obj_t *s_action_lbl   = NULL;
static lv_obj_t *s_msg_lbl      = NULL;
static lv_obj_t *s_tool_val_lbl = NULL;
static lv_group_t *s_nd_group   = NULL;

static notify_detail_cb_t s_cb_back  = NULL;
static notify_detail_cb_t s_cb_allow = NULL;
static notify_detail_cb_t s_cb_deny  = NULL;

static void add_divider(lv_obj_t *parent, int w, lv_color_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, w, 1);
    lv_obj_set_style_bg_color(d, color, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_radius(d, 0, 0);
}

void ui_notify_detail_screen_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, NS_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_margin_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 0, 0);

    // ---- Header ----
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, ND_SCREEN_W, ND_HEADER_H);
    lv_obj_set_style_bg_color(hdr, NS_WHITE, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);
    lv_obj_set_style_pad_right(hdr, 10, 0);
    lv_obj_set_style_pad_top(hdr, 0, 0);
    lv_obj_set_style_pad_bottom(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_hdr_title = lv_label_create(hdr);
    lv_label_set_text(s_hdr_title, "PERM REQUEST");
    lv_obj_set_style_text_font(s_hdr_title, &usr_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hdr_title, ND_TITLE_ORANGE, 0);
    lv_obj_set_flex_grow(s_hdr_title, 1);

    // 47px spacer on right (matches Ardot spacer node)
    lv_obj_t *hdr_sp = lv_obj_create(hdr);
    lv_obj_set_size(hdr_sp, 47, 1);
    lv_obj_set_style_bg_opa(hdr_sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_sp, 0, 0);

    // ---- Divider top ----
    add_divider(parent, ND_SCREEN_W, NS_DIVIDER_COLOR);

    // ---- Content card (164px = Frame2 height, centered with 3.5px top offset per design) ----
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 464, 157);
    lv_obj_set_style_bg_color(card, NS_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, ND_CARD_R, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, ND_CARD_PAD, 0);
    lv_obj_set_style_margin_left(card, 8, 0);
    lv_obj_set_style_margin_right(card, 8, 0);
    lv_obj_set_style_margin_top(card, 3, 0);
    lv_obj_set_style_margin_bottom(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, ND_CARD_GAP, 0);

    // -- Meta row: Badge | Session | Timestamp --
    lv_obj_t *meta = lv_obj_create(card);
    lv_obj_set_width(meta, LV_PCT(100));
    lv_obj_set_height(meta, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(meta, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *badge_box = lv_obj_create(meta);
    lv_obj_set_height(badge_box, ND_BADGE_H);
    lv_obj_set_width(badge_box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(badge_box, ND_BADGE_BG, 0);
    lv_obj_set_style_bg_opa(badge_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge_box, ND_BADGE_R, 0);
    lv_obj_set_style_border_width(badge_box, 0, 0);
    lv_obj_set_style_pad_left(badge_box, 5, 0);
    lv_obj_set_style_pad_right(badge_box, 5, 0);
    lv_obj_set_style_pad_top(badge_box, 0, 0);
    lv_obj_set_style_pad_bottom(badge_box, 0, 0);
    lv_obj_clear_flag(badge_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(badge_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(badge_box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_badge_lbl = lv_label_create(badge_box);
    lv_label_set_text(s_badge_lbl, "vk");
    lv_obj_set_style_text_font(s_badge_lbl, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(s_badge_lbl, ND_BADGE_TEXT_COLOR, 0);

    s_session_lbl = lv_label_create(meta);
    lv_label_set_text(s_session_lbl, "vibe-keyboard");
    lv_obj_set_style_text_font(s_session_lbl, &AlibabaPuHuiTi_Regular_14px, 0);
    lv_obj_set_style_text_color(s_session_lbl, NS_TEXT_PRIMARY, 0);
    lv_obj_set_style_pad_left(s_session_lbl, 8, 0);
    lv_obj_set_flex_grow(s_session_lbl, 1);
    lv_label_set_long_mode(s_session_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_session_lbl, 1);

    s_ts_lbl = lv_label_create(meta);
    lv_label_set_text(s_ts_lbl, "12:43");
    lv_obj_set_style_text_font(s_ts_lbl, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ts_lbl, NS_TEXT_SECONDARY, 0);

    // -- Content divider --
    add_divider(card, LV_PCT(100), ND_LIGHT_DIVIDER);

    // -- Action label --
    s_action_lbl = lv_label_create(card);
    lv_label_set_text(s_action_lbl, "ACTION");
    lv_obj_set_style_text_font(s_action_lbl, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(s_action_lbl, NS_TEXT_SECONDARY, 0);

    // -- Message box --
    lv_obj_t *msg_box = lv_obj_create(card);
    lv_obj_set_width(msg_box, LV_PCT(100));
    lv_obj_set_height(msg_box, ND_MSG_BOX_H);
    lv_obj_set_style_bg_color(msg_box, ND_MSG_BOX_BG, 0);
    lv_obj_set_style_bg_opa(msg_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(msg_box, ND_MSG_BOX_R, 0);
    lv_obj_set_style_border_width(msg_box, 0, 0);
    lv_obj_set_style_pad_left(msg_box, 10, 0);
    lv_obj_set_style_pad_right(msg_box, 10, 0);
    lv_obj_set_style_pad_top(msg_box, 6, 0);
    lv_obj_set_style_pad_bottom(msg_box, 6, 0);
    lv_obj_clear_flag(msg_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(msg_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(msg_box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_msg_lbl = lv_label_create(msg_box);
    lv_label_set_text(s_msg_lbl, "Write(crates/vk-daemon/src/server/api.rs)");
    lv_obj_set_style_text_font(s_msg_lbl, &AlibabaPuHuiTi_Regular_12px, 0);
    lv_obj_set_style_text_color(s_msg_lbl, ND_MSG_TEXT_COLOR, 0);
    lv_label_set_long_mode(s_msg_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_msg_lbl, LV_PCT(100));

    // -- Second divider --
    add_divider(card, LV_PCT(100), ND_LIGHT_DIVIDER);

    // -- Tool type row --
    lv_obj_t *type_row = lv_obj_create(card);
    lv_obj_set_width(type_row, LV_PCT(100));
    lv_obj_set_height(type_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(type_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(type_row, 0, 0);
    lv_obj_set_style_pad_all(type_row, 0, 0);
    lv_obj_clear_flag(type_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(type_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(type_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(type_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *type_key = lv_label_create(type_row);
    lv_label_set_text(type_key, "TOOL");
    lv_obj_set_style_text_font(type_key, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(type_key, NS_TEXT_SECONDARY, 0);

    s_tool_val_lbl = lv_label_create(type_row);
    lv_label_set_text(s_tool_val_lbl, "Write");
    lv_obj_set_style_text_font(s_tool_val_lbl, &usr_montserrat_14, 0);
    lv_obj_set_style_text_color(s_tool_val_lbl, NS_TEXT_PRIMARY, 0);

    // ---- Divider bottom ----
    add_divider(parent, ND_SCREEN_W, NS_DIVIDER_COLOR);

    // ---- Bottom bar ----
    lv_obj_t *btm = lv_obj_create(parent);
    lv_obj_set_size(btm, ND_SCREEN_W, ND_BOTTOM_H);
    lv_obj_set_style_bg_color(btm, NS_WHITE, 0);
    lv_obj_set_style_bg_opa(btm, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(btm, 10, 0);
    lv_obj_set_style_pad_right(btm, 10, 0);
    lv_obj_set_style_pad_top(btm, 0, 0);
    lv_obj_set_style_pad_bottom(btm, 0, 0);
    lv_obj_set_style_border_width(btm, 0, 0);
    lv_obj_set_style_radius(btm, 0, 0);
    lv_obj_clear_flag(btm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btm, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btm, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *nav_up = lv_label_create(btm);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(nav_up, NS_NAV_GRAY, 0);

    lv_obj_t *center = lv_obj_create(btm);
    lv_obj_set_height(center, ND_BOTTOM_H);
    lv_obj_set_width(center, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(center, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center, 12, 0);

    #define MAKE_BTN(par, bv, lv, w, col, txt) \
        lv_obj_t *bv = lv_obj_create(par); \
        lv_obj_set_size(bv, w, 22); \
        lv_obj_set_style_bg_color(bv, col, 0); \
        lv_obj_set_style_bg_opa(bv, (lv_opa_t)(255 * 0.12f), 0); \
        lv_obj_set_style_border_color(bv, col, 0); \
        lv_obj_set_style_border_width(bv, 1, 0); \
        lv_obj_set_style_radius(bv, 4, 0); \
        lv_obj_set_style_pad_left(bv, 6, 0); \
        lv_obj_set_style_pad_right(bv, 6, 0); \
        lv_obj_set_style_pad_top(bv, 3, 0); \
        lv_obj_set_style_pad_bottom(bv, 3, 0); \
        lv_obj_add_flag(bv, LV_OBJ_FLAG_CLICKABLE); \
        lv_obj_clear_flag(bv, LV_OBJ_FLAG_SCROLLABLE); \
        lv_obj_set_layout(bv, LV_LAYOUT_FLEX); \
        lv_obj_set_flex_align(bv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); \
        lv_obj_t *lv = lv_label_create(bv); \
        lv_label_set_text(lv, txt); \
        lv_obj_set_style_text_font(lv, &usr_montserrat_12, 0); \
        lv_obj_set_style_text_color(lv, col, 0);

    // Back button: blue #5865F2
    MAKE_BTN(center, back_btn, back_lbl, 80,
             lv_color_make(88, 101, 242), "\xe2\x86\x90 BACK")
    // Allow button: green #21A659
    MAKE_BTN(center, allow_btn, allow_lbl, 88,
             lv_color_make(33, 166, 89), "\xe2\x9c\x93 ALLOW")
    // Deny button: red #D93326
    MAKE_BTN(center, deny_btn, deny_lbl, 80,
             lv_color_make(217, 51, 38), "\xe2\x9c\x97 DENY")

    #undef MAKE_BTN

    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        (void)e;
        if (s_cb_back) s_cb_back();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(allow_btn, [](lv_event_t *e) {
        (void)e;
        if (s_cb_allow) s_cb_allow();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(deny_btn, [](lv_event_t *e) {
        (void)e;
        if (s_cb_deny) s_cb_deny();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *nav_dn = lv_label_create(btm);
    lv_label_set_text(nav_dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(nav_dn, NS_NAV_GRAY, 0);

    s_nd_group = lv_group_create();
    lv_obj_t *key_item = lv_obj_create(parent);
    lv_obj_remove_style_all(key_item);
    lv_obj_set_size(key_item, 0, 0);
    lv_obj_clear_flag(key_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(key_item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(key_item, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_group_add_obj(s_nd_group, key_item);
    lv_group_focus_obj(key_item);
}

void ui_notify_detail_screen_set_data(const char *title_type,
                                      const char *badge,
                                      const char *session,
                                      const char *timestamp,
                                      const char *action_label,
                                      const char *message,
                                      const char *tool_type)
{
    if (title_type)   lv_label_set_text(s_hdr_title,    title_type);
    if (badge)        lv_label_set_text(s_badge_lbl,    badge);
    if (session)      lv_label_set_text(s_session_lbl,  session);
    if (timestamp)    lv_label_set_text(s_ts_lbl,       timestamp);
    if (action_label) lv_label_set_text(s_action_lbl,   action_label);
    if (message)      lv_label_set_text(s_msg_lbl,      message);
    if (tool_type)    lv_label_set_text(s_tool_val_lbl, tool_type);
}

void ui_notify_detail_screen_set_callbacks(notify_detail_cb_t on_back,
                                           notify_detail_cb_t on_allow,
                                           notify_detail_cb_t on_deny)
{
    s_cb_back  = on_back;
    s_cb_allow = on_allow;
    s_cb_deny  = on_deny;
}

lv_group_t *ui_notify_detail_screen_get_group(void)
{
    return s_nd_group;
}
