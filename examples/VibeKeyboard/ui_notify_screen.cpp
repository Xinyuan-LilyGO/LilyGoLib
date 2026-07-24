/**
 * @file      ui_notify_screen.cpp
 * @brief     Notify Screen LVGL implementation
 *            Matches Ardot design (480×222, "Notify Screen" node 2:135)
 *            Bottom Bar: ↑ SCROLL | ● CONFIRM | ↓ SCROLL
 *            Encoder scrolls selection up/down through rows.
 */
#include "ui_notify_screen.h"
#include "vk_protocol.h"

extern "C" {
LV_FONT_DECLARE(AlibabaPuHuiTi_Regular_14px)
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

// ---- Layout constants (from Ardot) ----
#define NS_SCREEN_W   480
#define NS_HEADER_H   28
#define NS_LIST_H     164
#define NS_BOTTOM_H   28
#define NS_ROW_H      40
#define NS_ROW_GAP    6
#define NS_ROW_PAD_H  8
#define NS_BADGE_H    16
#define NS_BADGE_R    2
#define NS_ROW_R      4
#define NS_MAX_ROWS   VK_MAX_NOTIFICATIONS

// ---- Internal state ----
static lv_obj_t *s_hdr_count = NULL;
static int8_t    s_sel_row   = 0;
static ui_notify_select_cb_t s_select_cb = NULL;
static lv_group_t *s_ns_group = NULL;
static lv_obj_t   *s_focus_items[NS_MAX_ROWS];
static bool        s_row_visible[NS_MAX_ROWS] = { false };

static struct {
    lv_obj_t *row;
    lv_obj_t *badge_box;
    lv_obj_t *badge_lbl;
    lv_obj_t *session_lbl;
    lv_obj_t *status_lbl;
    lv_obj_t *count_lbl;
    // stored data for re-highlighting
    char      badge_text[8];
    bool      badge_ok;
    char      session[32];
    char      status[32];
    char      count[8];
} s_rows[NS_MAX_ROWS];

static int8_t first_visible_row(void)
{
    for (uint8_t i = 0; i < NS_MAX_ROWS; i++) {
        if (s_row_visible[i]) return (int8_t)i;
    }
    return -1;
}

static void focus_selected_item(void)
{
    if (!s_ns_group || s_sel_row < 0 || s_sel_row >= NS_MAX_ROWS) return;
    if (s_focus_items[s_sel_row]) lv_group_focus_obj(s_focus_items[s_sel_row]);
    if (s_rows[s_sel_row].row && s_row_visible[s_sel_row]) {
        lv_obj_scroll_to_view(s_rows[s_sel_row].row, LV_ANIM_ON);
    }
}

static void apply_row_style(uint8_t idx, bool selected)
{
    lv_obj_t *r = s_rows[idx].row;
    if (selected) {
        lv_obj_set_style_bg_color(r, NS_ROW_SEL_BG, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(r, NS_ROW_SEL_BORDER, 0);
        lv_obj_set_style_border_width(r, 1, 0);
    } else {
        lv_obj_set_style_bg_color(r, NS_WHITE, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(r, 0, 0);
    }
    lv_obj_set_style_text_color(s_rows[idx].session_lbl,
                                selected ? NS_TEXT_PRIMARY : NS_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_color(s_rows[idx].count_lbl,
                                selected ? NS_TEXT_PRIMARY : NS_TEXT_SECONDARY, 0);
}

static void create_row(lv_obj_t *parent, uint8_t idx)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, NS_ROW_H);
    lv_obj_set_style_pad_left(row, NS_ROW_PAD_H, 0);
    lv_obj_set_style_pad_right(row, NS_ROW_PAD_H, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_set_style_radius(row, NS_ROW_R, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_height(badge, NS_BADGE_H);
    lv_obj_set_width(badge, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(badge, NS_BADGE_R, 0);
    lv_obj_set_style_pad_left(badge, 5, 0);
    lv_obj_set_style_pad_right(badge, 5, 0);
    lv_obj_set_style_pad_top(badge, 0, 0);
    lv_obj_set_style_pad_bottom(badge, 0, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(badge, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *badge_lbl = lv_label_create(badge);
    lv_obj_set_style_text_font(badge_lbl, &usr_montserrat_12, 0);
    lv_label_set_text(badge_lbl, "ok");

    lv_obj_t *session_lbl = lv_label_create(row);
    lv_obj_set_style_text_font(session_lbl, &AlibabaPuHuiTi_Regular_14px, 0);
    lv_label_set_text(session_lbl, "session");
    lv_obj_set_style_pad_left(session_lbl, 8, 0);

    lv_obj_t *status_lbl = lv_label_create(row);
    lv_obj_set_style_text_font(status_lbl, &AlibabaPuHuiTi_Regular_14px, 0);
    lv_label_set_text(status_lbl, "");
    lv_obj_set_flex_grow(status_lbl, 1);
    lv_obj_set_style_pad_left(status_lbl, 8, 0);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(status_lbl, 1);

    lv_obj_t *count_lbl = lv_label_create(row);
    lv_obj_set_style_text_font(count_lbl, &usr_montserrat_12, 0);
    lv_label_set_text(count_lbl, "(1)");

    s_rows[idx].row         = row;
    s_rows[idx].badge_box   = badge;
    s_rows[idx].badge_lbl   = badge_lbl;
    s_rows[idx].session_lbl = session_lbl;
    s_rows[idx].status_lbl  = status_lbl;
    s_rows[idx].count_lbl   = count_lbl;
}

static lv_obj_t *create_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, 22);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn, NS_BTN_CONFIRM_BG, 0);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_border_color(btn, NS_BTN_CONFIRM_BORDER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_left(btn, 6, 0);
    lv_obj_set_style_pad_right(btn, 6, 0);
    lv_obj_set_style_pad_top(btn, 3, 0);
    lv_obj_set_style_pad_bottom(btn, 3, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, NS_TITLE_BLUE, 0);
    return btn;
}

void ui_notify_screen_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, NS_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_margin_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(parent, 0, 0);    // kill default theme gap between flex children

    // ---- Header ----
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, NS_SCREEN_W, NS_HEADER_H);
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
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hdr_title = lv_label_create(hdr);
    lv_label_set_text(hdr_title, "NOTIFY");
    lv_obj_set_style_text_font(hdr_title, &usr_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr_title, NS_TITLE_BLUE, 0);
    lv_obj_set_flex_grow(hdr_title, 1);

    s_hdr_count = lv_label_create(hdr);
    lv_label_set_text(s_hdr_count, "0 in 0 sessions");
    lv_obj_set_style_text_font(s_hdr_count, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hdr_count, NS_COUNT_GRAY, 0);

    // ---- Divider top ----
    lv_obj_t *div_top = lv_obj_create(parent);
    lv_obj_set_size(div_top, NS_SCREEN_W, 1);
    lv_obj_set_style_bg_color(div_top, NS_DIVIDER_COLOR, 0);
    lv_obj_set_style_bg_opa(div_top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div_top, 0, 0);
    lv_obj_set_style_radius(div_top, 0, 0);

    // ---- Notify list ----
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, NS_SCREEN_W, NS_LIST_H);
    lv_obj_set_style_bg_color(list, NS_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(list, NS_ROW_GAP, 0);
    lv_obj_set_style_pad_column(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (uint8_t i = 0; i < NS_MAX_ROWS; i++) {
        create_row(list, i);
    }

    // ---- Divider bottom ----
    lv_obj_t *div_bot = lv_obj_create(parent);
    lv_obj_set_size(div_bot, NS_SCREEN_W, 1);
    lv_obj_set_style_bg_color(div_bot, NS_DIVIDER_COLOR, 0);
    lv_obj_set_style_bg_opa(div_bot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div_bot, 0, 0);
    lv_obj_set_style_radius(div_bot, 0, 0);

    // ---- Bottom Bar (matches Ardot: ↑ SCROLL | ← BACK  ● CONFIRM | ↓ SCROLL) ----
    lv_obj_t *btm = lv_obj_create(parent);
    lv_obj_set_size(btm, NS_SCREEN_W, NS_BOTTOM_H);
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
    lv_obj_set_flex_align(btm, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btm, 0, 0);

    // Left: ↑ SCROLL
    lv_obj_t *nav_up = lv_label_create(btm);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(nav_up, NS_NAV_GRAY, 0);

    // Spacer left
    lv_obj_t *sp1 = lv_obj_create(btm);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_style_bg_opa(sp1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp1, 0, 0);
    lv_obj_set_flex_grow(sp1, 1);

    // Center: ← BACK + ● CONFIRM buttons
    lv_obj_t *actions = lv_obj_create(btm);
    lv_obj_set_height(actions, 22);
    lv_obj_set_width(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 12, 0);

    create_bottom_btn(actions, "\xe2\x86\x90 BACK");
    create_bottom_btn(actions, "\xe2\x97\x8f  CONFIRM");

    // Spacer right
    lv_obj_t *sp2 = lv_obj_create(btm);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_style_bg_opa(sp2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp2, 0, 0);
    lv_obj_set_flex_grow(sp2, 1);

    // Right: ↓ SCROLL
    lv_obj_t *nav_dn = lv_label_create(btm);
    lv_label_set_text(nav_dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(nav_dn, NS_NAV_GRAY, 0);

    // ---- Empty initial list; BLE downlink data populates rows later. ----
    s_sel_row = 0;
    for (uint8_t i = 0; i < NS_MAX_ROWS; i++) {
        ui_notify_screen_set_row(i, "", true, "", "", "", false);
    }
    ui_notify_screen_set_count("0 in 0 sessions");

    // ---- Focus group ----
    s_ns_group = lv_group_create();
    for (int i = 0; i < NS_MAX_ROWS; i++) {
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
            for (int j = 0; j < NS_MAX_ROWS; j++) {
                if (s_focus_items[j] == self) {
                    if (!s_row_visible[j]) {
                        int8_t next = first_visible_row();
                        if (next >= 0) {
                            s_sel_row = next;
                            apply_row_style((uint8_t)next, true);
                            focus_selected_item();
                        }
                        break;
                    }
                    apply_row_style((uint8_t)s_sel_row, false);
                    s_sel_row = (int8_t)j;
                    apply_row_style((uint8_t)s_sel_row, true);
                    lv_obj_scroll_to_view(s_rows[s_sel_row].row, LV_ANIM_ON);
                    break;
                }
            }
        }, LV_EVENT_FOCUSED, NULL);

        lv_obj_add_event_cb(item, [](lv_event_t *e) {
            if (first_visible_row() < 0 || !s_row_visible[s_sel_row]) return;
            if (s_select_cb) s_select_cb((uint8_t)s_sel_row);
        }, LV_EVENT_CLICKED, NULL);

        lv_group_add_obj(s_ns_group, item);
    }
    lv_group_focus_obj(s_focus_items[0]);
}

void ui_notify_screen_set_row(uint8_t row,
                              const char *badge, bool badge_ok,
                              const char *session, const char *status,
                              const char *count, bool selected)
{
    if (row >= NS_MAX_ROWS) return;

    bool visible = (session && session[0] != '\0') ||
                   (status && status[0] != '\0') ||
                   (count && count[0] != '\0');
    bool had_visible_rows = first_visible_row() >= 0;
    s_row_visible[row] = visible;

    // Cache data for re-highlight
    lv_snprintf(s_rows[row].badge_text, sizeof(s_rows[row].badge_text), "%s", badge ? badge : "");
    s_rows[row].badge_ok = badge_ok;
    lv_snprintf(s_rows[row].session, sizeof(s_rows[row].session), "%s", session ? session : "");
    lv_snprintf(s_rows[row].status,  sizeof(s_rows[row].status),  "%s", status ? status : "");
    lv_snprintf(s_rows[row].count,   sizeof(s_rows[row].count),   "%s", count ? count : "");

    if (!visible) {
        lv_obj_add_flag(s_rows[row].row, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_rows[row].badge_lbl, "");
        lv_label_set_text(s_rows[row].session_lbl, "");
        lv_label_set_text(s_rows[row].status_lbl, "");
        lv_label_set_text(s_rows[row].count_lbl, "");

        if (s_sel_row == (int8_t)row) {
            int8_t next = first_visible_row();
            s_sel_row = next >= 0 ? next : 0;
            if (next >= 0) {
                apply_row_style((uint8_t)next, true);
                focus_selected_item();
            }
        }
        return;
    }

    lv_obj_clear_flag(s_rows[row].row, LV_OBJ_FLAG_HIDDEN);
    if (selected || !had_visible_rows) s_sel_row = (int8_t)row;

    lv_obj_t *bb = s_rows[row].badge_box;
    lv_obj_set_style_bg_color(bb, badge_ok ? NS_BADGE_OK_BG : NS_BADGE_ERR_BG, 0);
    lv_obj_set_style_bg_opa(bb, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_rows[row].badge_lbl,
                                badge_ok ? NS_TEXT_PRIMARY : NS_BADGE_ERR_TEXT, 0);
    lv_label_set_text(s_rows[row].badge_lbl, badge ? badge : "");

    lv_label_set_text(s_rows[row].session_lbl, session ? session : "");
    lv_label_set_text(s_rows[row].status_lbl,  status ? status : "");
    lv_obj_set_style_text_color(s_rows[row].status_lbl, NS_TEXT_SECONDARY, 0);
    lv_label_set_text(s_rows[row].count_lbl, count ? count : "");

    apply_row_style(row, s_sel_row == (int8_t)row);
}

void ui_notify_screen_set_count(const char *text)
{
    if (s_hdr_count) lv_label_set_text(s_hdr_count, text);
}

void ui_notify_screen_move(int8_t dir)
{
    if (first_visible_row() < 0) return;

    apply_row_style((uint8_t)s_sel_row, false);
    do {
        s_sel_row += dir;
        if (s_sel_row < 0)            s_sel_row = NS_MAX_ROWS - 1;
        if (s_sel_row >= NS_MAX_ROWS) s_sel_row = 0;
    } while (!s_row_visible[s_sel_row]);
    apply_row_style((uint8_t)s_sel_row, true);
    focus_selected_item();
}

void ui_notify_screen_set_select_cb(ui_notify_select_cb_t cb)
{
    s_select_cb = cb;
}

void ui_notify_screen_confirm(void)
{
    if (first_visible_row() < 0) return;
    if (s_select_cb) s_select_cb((uint8_t)s_sel_row);
}

uint8_t ui_notify_screen_get_selected(void)
{
    return (uint8_t)s_sel_row;
}

lv_group_t *ui_notify_screen_get_group(void)
{
    return s_ns_group;
}
