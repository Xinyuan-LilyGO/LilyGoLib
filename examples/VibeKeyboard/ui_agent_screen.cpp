/**
 * @file      ui_agent_screen.cpp
 * @brief     AI Agent Screen - tool management, network & system status
 *
 * Layout (480x600):
 *   Header (28px)        - "AI Agent" title
 *   Divider Top (1px)
 *   Scroll Content (flex-grow) - vertical sections, 8px pad, 6px gap
 *     Section AI Tools       (Claude Code: name+badge / hook+btn rows)
 *                            (Codex:       name+badge / hook+btn rows)
 *     Section Recommended    (iTerm2 / terminal-notifier: name+badge / desc+btn rows)
 *     Section Network        (Daemon Port +/- spinner with styled input box)
 *     Section System         (BLE / Daemon status badges)
 *   Divider Bottom (1px)
 *   Bottom Bar (28px)    - UP SCROLL / CONFIRM / DN SCROLL
 */
#include <LilyGoLib.h>
#include <lvgl.h>
#include <string.h>

#include "ui_agent_screen.h"

extern "C" {
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* ── Color palette ───────────────────────────────────────────────────── */
#define AG_BG          lv_color_make(242, 243, 245)
#define AG_WHITE       lv_color_make(255, 255, 255)
#define AG_CARD_BDR    lv_color_make(209, 211, 215)
#define AG_ROW_DIV     lv_color_make(224, 226, 231)
#define AG_ACCENT      lv_color_make( 88, 101, 242)
#define AG_TEXT_DARK   lv_color_make( 30,  34,  40)
#define AG_TEXT_MID    lv_color_make(128, 132, 142)
#define AG_GREEN       lv_color_make( 35, 165,  90)
#define AG_RED         lv_color_make(214,  52,  52)
#define AG_BTN_GRAY    lv_color_make(224, 226, 231)
#define AG_INPUT_BG    lv_color_make(243, 245, 249)
#define AG_GREEN_BG    lv_color_make(208, 243, 220)

#define HEADER_H   28
#define BOTTOM_H   28
#define AG_SCROLL_STEP        30
#define AG_SCROLL_FOCUS_MAX   16
#define AG_ACTION_COUNT        4

static lv_obj_t *s_port_label  = NULL;
static lv_obj_t *s_scroll_area = NULL;
static lv_obj_t *s_recommended_card = NULL;
static lv_group_t *s_group     = NULL;
static lv_obj_t *s_focus_items[AG_SCROLL_FOCUS_MAX] = {};
static int32_t s_scroll_focus_targets[AG_SCROLL_FOCUS_MAX] = {};
static int8_t s_scroll_focus_count = 0;
static int8_t s_scroll_focus_index = 0;
static int32_t   s_daemon_port = 19280;
static ui_agent_action_cb_t s_action_cb = NULL;
static ui_agent_status_request_cb_t s_status_request_cb = NULL;
static uint32_t s_next_request_id = 1;

typedef struct {
    ui_agent_action_t action;
    const char *tool;
    const char *command;
    const char *idle_text;
    const char *success_text;
    lv_color_t idle_bg;
    lv_color_t success_bg;
    lv_obj_t *button;
    lv_obj_t *label;
    uint32_t pending_request_id;
} agent_action_btn_t;

static agent_action_btn_t s_action_buttons[AG_ACTION_COUNT] = {};

typedef struct {
    const char *id;
    const char *display_name;
    agent_action_btn_t *action_btn;
    lv_obj_t *install_icon;
    lv_obj_t *install_label;
    lv_obj_t *hook_icon;
    lv_obj_t *hook_label;
    lv_obj_t *detail_label;
} agent_tool_status_row_t;

static agent_tool_status_row_t s_claude_status = {};
static agent_tool_status_row_t s_codex_status = {};

/* ── Low-level helpers ───────────────────────────────────────────────── */

static lv_obj_t *row_divider(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, AG_ROW_DIV, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    return d;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, AG_ACCENT, 0);
    lv_obj_set_style_border_color(btn, AG_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_hor(btn, 6, 0);
    lv_obj_set_style_pad_ver(btn, 3, 0);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, AG_ACCENT, 0);
    return btn;
}

static int32_t get_scroll_max(void)
{
    if (!s_scroll_area) return 0;
    lv_obj_update_layout(s_scroll_area);
    int32_t max_scroll = lv_obj_get_scroll_y(s_scroll_area) +
                         lv_obj_get_scroll_bottom(s_scroll_area);
    return max_scroll > 0 ? max_scroll : 0;
}

static void scroll_to_focus_index(int8_t index, lv_anim_enable_t anim)
{
    if (!s_scroll_area || s_scroll_focus_count <= 0) return;

    if (index < 0) index = 0;
    if (index >= s_scroll_focus_count) index = s_scroll_focus_count - 1;
    s_scroll_focus_index = index;

    int32_t target = s_scroll_focus_targets[index];
    int32_t max_scroll = get_scroll_max();
    if (target > max_scroll) target = max_scroll;
    lv_obj_scroll_to_y(s_scroll_area, target, anim);
}

/* Horizontal flex row, full-width, content-height */
static lv_obj_t *make_hrow(lv_obj_t *parent, lv_flex_align_t main_align)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, AG_WHITE, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/* Inline badge: two labels side-by-side (icon char + text), no background box */
static void make_inline_badge(lv_obj_t *parent,
                              const char *icon, const char *text,
                              lv_color_t color,
                              lv_obj_t **icon_label,
                              lv_obj_t **text_label)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(wrap, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wrap, 3, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = lv_label_create(wrap);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(ic, color, 0);
    if (icon_label) *icon_label = ic;

    lv_obj_t *tx = lv_label_create(wrap);
    lv_label_set_text(tx, text);
    lv_obj_set_style_text_font(tx, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(tx, color, 0);
    if (text_label) *text_label = tx;
}

/* Action button (Install / Uninstall) */
static void set_action_btn_visual(agent_action_btn_t *action_btn,
                                  const char *text,
                                  lv_color_t bg)
{
    if (!action_btn || !action_btn->button || !action_btn->label) return;
    lv_obj_set_style_bg_color(action_btn->button, bg, 0);
    lv_label_set_text(action_btn->label, text);
}

static void configure_action_btn(agent_action_btn_t *action_btn,
                                 const char *command,
                                 const char *idle_text,
                                 const char *success_text,
                                 lv_color_t idle_bg,
                                 lv_color_t success_bg)
{
    if (!action_btn) return;
    action_btn->command = command;
    action_btn->idle_text = idle_text;
    action_btn->success_text = success_text;
    action_btn->idle_bg = idle_bg;
    action_btn->success_bg = success_bg;
    if (action_btn->pending_request_id == 0) {
        set_action_btn_visual(action_btn, idle_text, idle_bg);
    }
}

static agent_action_btn_t *find_action_btn_by_obj(lv_obj_t *obj)
{
    if (!obj) return NULL;

    for (int8_t i = 0; i < AG_ACTION_COUNT; i++) {
        if (s_action_buttons[i].button == obj) {
            return &s_action_buttons[i];
        }
    }

    return NULL;
}

static void trigger_action_btn(agent_action_btn_t *action_btn)
{
    if (!action_btn || action_btn->pending_request_id != 0) return;

    uint32_t request_id = s_next_request_id++;
    if (request_id == 0) {
        request_id = s_next_request_id++;
    }
    action_btn->pending_request_id = request_id;
    set_action_btn_visual(action_btn, "Sending", AG_BTN_GRAY);

    if (s_action_cb) {
        s_action_cb(action_btn->action,
                    action_btn->tool,
                    action_btn->command,
                    request_id);
    } else {
        ui_agent_screen_action_result(request_id, true);
    }
}

static void action_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    agent_action_btn_t *action_btn =
        (agent_action_btn_t *)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        if (action_btn && action_btn->button) {
            lv_obj_scroll_to_view_recursive(action_btn->button, LV_ANIM_ON);
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        trigger_action_btn(action_btn);
        return;
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            ui_agent_screen_move(-1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            ui_agent_screen_move(1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
            trigger_action_btn(action_btn);
            lv_event_stop_bubbling(e);
        }
    }
}

static lv_obj_t *make_action_btn(lv_obj_t *parent,
                                  ui_agent_action_t action,
                                  const char *tool,
                                  const char *command,
                                  const char *text,
                                  const char *success_text,
                                  lv_color_t bg,
                                  lv_color_t success_bg)
{
    if ((int)action < 0 || (int)action >= AG_ACTION_COUNT) return NULL;

    agent_action_btn_t *action_btn = &s_action_buttons[(int)action];
    action_btn->action = action;
    action_btn->tool = tool;
    action_btn->command = command;
    action_btn->idle_text = text;
    action_btn->success_text = success_text;
    action_btn->idle_bg = bg;
    action_btn->success_bg = success_bg;
    action_btn->pending_request_id = 0;

    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 19);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_radius(b, 2, 0);
    lv_obj_set_style_pad_hor(b, 6, 0);
    lv_obj_set_style_pad_ver(b, 3, 0);
    lv_obj_set_layout(b, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_outline_width(b, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(b, AG_TEXT_DARK, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(b, 1, LV_STATE_FOCUSED);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(b, action_btn_event_cb, LV_EVENT_ALL, action_btn);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l, AG_WHITE, 0);
    action_btn->button = b;
    action_btn->label = l;
    return b;
}

static void set_label_color_text(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label) return;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, color, 0);
}

static agent_tool_status_row_t *find_tool_status_row(const char *tool)
{
    if (!tool) return NULL;
    if (strcmp(tool, "claude-code") == 0) return &s_claude_status;
    if (strcmp(tool, "codex") == 0) return &s_codex_status;
    return NULL;
}

static void apply_tool_status(agent_tool_status_row_t *row,
                              const char *name,
                              bool detected,
                              bool hook_installed,
                              const char *detail)
{
    (void)name;
    if (!row) return;

    lv_color_t install_color = detected ? AG_GREEN : AG_TEXT_MID;
    set_label_color_text(row->install_icon,
                         detected ? "\xe2\x9c\x93" : "\xe2\x97\x8b",
                         install_color);
    set_label_color_text(row->install_label,
                         detected ? "Installed" : "Not Found",
                         install_color);

    const char *hook_text = hook_installed ? "Active" : "None";
    lv_color_t hook_color = hook_installed ? AG_GREEN : AG_TEXT_MID;
    if (!detected) {
        hook_text = "Unavailable";
    } else if (detail && detail[0] != '\0') {
        hook_text = detail;
    }
    set_label_color_text(row->hook_icon,
                         hook_installed ? "\xe2\x9c\x93" : "\xe2\x97\x8b",
                         hook_color);
    set_label_color_text(row->hook_label, hook_text, hook_color);

    configure_action_btn(row->action_btn,
                         hook_installed ? "uninstall_hook" : "install_hook",
                         hook_installed ? "Uninstall" : "Install",
                         hook_installed ? "Removed" : "Active",
                         hook_installed ? AG_RED : AG_ACCENT,
                         AG_GREEN);
}

/* Section card: white box with border/radius, vertical flex, returns card obj */
static lv_obj_t *make_section_card(lv_obj_t *parent, const char *title,
                                    const lv_font_t *title_font)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, AG_WHITE, 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, AG_CARD_BDR, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Section title inside card */
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, title_font, 0);
    lv_obj_set_style_text_color(lbl, AG_TEXT_DARK, 0);

    row_divider(card);

    return card;
}

/* ── Section: AI Tools ───────────────────────────────────────────────── */

static void build_section_ai_tools(lv_obj_t *parent)
{
    lv_obj_t *card = make_section_card(parent, "AI Tools", &usr_montserrat_12);

    /* ---- Claude Code ---- */
    /* Row 1: name + installed badge */
    lv_obj_t *r1a = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *n1  = make_label(r1a, "Claude Code", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_style_text_font(n1, &usr_montserrat_12, 0); /* 12px Bold via weight */
    lv_obj_set_flex_grow(n1, 1);
    make_inline_badge(r1a,
                      "\xe2\x97\x8b",
                      "Unknown",
                      AG_TEXT_MID,
                      &s_claude_status.install_icon,
                      &s_claude_status.install_label);

    /* Row 2: Hook label + active status + Uninstall button */
    lv_obj_t *r1b = make_hrow(card, LV_FLEX_ALIGN_START);
    make_label(r1b, "Hook:", &usr_montserrat_12, AG_TEXT_MID);
    s_claude_status.hook_icon = make_label(r1b,
                                           "\xe2\x97\x8b",
                                           &usr_montserrat_12,
                                           AG_TEXT_MID);
    lv_obj_t *act1 = make_label(r1b, "Unknown", &usr_montserrat_12, AG_TEXT_MID);
    s_claude_status.hook_label = act1;
    lv_obj_set_flex_grow(act1, 1);
    make_action_btn(r1b,
                    UI_AGENT_ACTION_CLAUDE_UNINSTALL,
                    "claude-code",
                    "install_hook",
                    "Install",
                    "Active",
                    AG_ACCENT,
                    AG_GREEN);
    s_claude_status.id = "claude-code";
    s_claude_status.display_name = "Claude Code";
    s_claude_status.action_btn = &s_action_buttons[(int)UI_AGENT_ACTION_CLAUDE_UNINSTALL];

    row_divider(card);

    /* ---- Codex ---- */
    /* Row 1: name + installed badge */
    lv_obj_t *r2a = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *n2  = make_label(r2a, "Codex", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_flex_grow(n2, 1);
    make_inline_badge(r2a,
                      "\xe2\x97\x8b",
                      "Unknown",
                      AG_TEXT_MID,
                      &s_codex_status.install_icon,
                      &s_codex_status.install_label);

    /* Row 2: Hook label + None + Install button */
    lv_obj_t *r2b = make_hrow(card, LV_FLEX_ALIGN_START);
    make_label(r2b, "Hook:", &usr_montserrat_12, AG_TEXT_MID);
    s_codex_status.hook_icon = make_label(r2b,
                                          "\xe2\x97\x8b",
                                          &usr_montserrat_12,
                                          AG_TEXT_MID);
    lv_obj_t *none2 = make_label(r2b, "Unknown", &usr_montserrat_12, AG_TEXT_MID);
    s_codex_status.hook_label = none2;
    lv_obj_set_flex_grow(none2, 1);
    make_action_btn(r2b,
                    UI_AGENT_ACTION_CODEX_INSTALL,
                    "codex",
                    "install_hook",
                    "Install",
                    "Active",
                    AG_ACCENT,
                    AG_GREEN);
    s_codex_status.id = "codex";
    s_codex_status.display_name = "Codex";
    s_codex_status.action_btn = &s_action_buttons[(int)UI_AGENT_ACTION_CODEX_INSTALL];
}

/* ── Section: Recommended Tools ─────────────────────────────────────── */

static void build_section_recommended(lv_obj_t *parent)
{
    lv_obj_t *card = make_section_card(parent, "Recommended Tools", &usr_montserrat_12);
    s_recommended_card = card;

    /* ---- iTerm2 ---- */
    /* Row 1: name + Not Found badge */
    lv_obj_t *r1a = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *n1  = make_label(r1a, "iTerm2", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_flex_grow(n1, 1);
    make_inline_badge(r1a,
                      "\xe2\x97\x8b",
                      "Not Found",
                      AG_TEXT_MID,
                      NULL,
                      NULL);

    /* Row 2: description + Install button */
    lv_obj_t *r1b = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *d1  = make_label(r1b, "Tab-level focus switching", &usr_montserrat_12, AG_TEXT_MID);
    lv_obj_set_flex_grow(d1, 1);
    make_action_btn(r1b,
                    UI_AGENT_ACTION_ITERM_INSTALL,
                    "iterm2",
                    "install_tool",
                    "Install",
                    "Installed",
                    AG_ACCENT,
                    AG_GREEN);

    row_divider(card);

    /* ---- terminal-notifier ---- */
    /* Row 1: name + Not Found badge */
    lv_obj_t *r2a = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *n2  = make_label(r2a, "terminal-notifier", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_flex_grow(n2, 1);
    make_inline_badge(r2a,
                      "\xe2\x97\x8b",
                      "Not Found",
                      AG_TEXT_MID,
                      NULL,
                      NULL);

    /* Row 2: description + Install button */
    lv_obj_t *r2b = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *d2  = make_label(r2b, "Click-to-jump notifications", &usr_montserrat_12, AG_TEXT_MID);
    lv_obj_set_flex_grow(d2, 1);
    make_action_btn(r2b,
                    UI_AGENT_ACTION_NOTIFIER_INSTALL,
                    "terminal-notifier",
                    "install_tool",
                    "Install",
                    "Installed",
                    AG_ACCENT,
                    AG_GREEN);
}

/* ── Section: Network ────────────────────────────────────────────────── */

static void port_dec_cb(lv_event_t *e)
{
    (void)e;
    if (s_daemon_port > 1) s_daemon_port--;
    if (s_port_label) {
        char buf[16];
        lv_snprintf(buf, sizeof(buf), "%d", (int)s_daemon_port);
        lv_label_set_text(s_port_label, buf);
    }
}

static void port_inc_cb(lv_event_t *e)
{
    (void)e;
    if (s_daemon_port < 65535) s_daemon_port++;
    if (s_port_label) {
        char buf[16];
        lv_snprintf(buf, sizeof(buf), "%d", (int)s_daemon_port);
        lv_label_set_text(s_port_label, buf);
    }
}

static void build_section_network(lv_obj_t *parent)
{
    lv_obj_t *card = make_section_card(parent, "Network", &usr_montserrat_12);

    lv_obj_t *row = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);

    lv_obj_t *port_lbl = make_label(row, "Daemon Port", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_flex_grow(port_lbl, 1);

    /* Port controls group: [Dec] [Input] [Inc] */
    lv_obj_t *ctrl = lv_obj_create(row);
    lv_obj_remove_style_all(ctrl);
    lv_obj_set_size(ctrl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(ctrl, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl, 8, 0);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);

    /* Decrement button: 16x18, gray */
    lv_obj_t *btn_dec = lv_obj_create(ctrl);
    lv_obj_remove_style_all(btn_dec);
    lv_obj_set_size(btn_dec, 16, 18);
    lv_obj_set_style_bg_opa(btn_dec, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_dec, AG_BTN_GRAY, 0);
    lv_obj_set_style_radius(btn_dec, 2, 0);
    lv_obj_set_layout(btn_dec, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn_dec, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_dec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_dec, port_dec_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dec_lbl = lv_label_create(btn_dec);
    lv_label_set_text(dec_lbl, "-");
    lv_obj_set_style_text_font(dec_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dec_lbl, AG_TEXT_DARK, 0);

    /* Input box: 42x19, accent bg + accent border */
    lv_obj_t *input_box = lv_obj_create(ctrl);
    lv_obj_remove_style_all(input_box);
    lv_obj_set_size(input_box, 42, 19);
    lv_obj_set_style_bg_opa(input_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(input_box, AG_INPUT_BG, 0);
    lv_obj_set_style_border_width(input_box, 1, 0);
    lv_obj_set_style_border_color(input_box, AG_ACCENT, 0);
    lv_obj_set_style_radius(input_box, 2, 0);
    lv_obj_set_style_pad_hor(input_box, 4, 0);
    lv_obj_set_layout(input_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(input_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(input_box, LV_OBJ_FLAG_SCROLLABLE);
    char port_buf[16];
    lv_snprintf(port_buf, sizeof(port_buf), "%d", (int)s_daemon_port);
    s_port_label = lv_label_create(input_box);
    lv_label_set_text(s_port_label, port_buf);
    lv_obj_set_style_text_font(s_port_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_port_label, AG_ACCENT, 0);

    /* Increment button: 16x18, gray */
    lv_obj_t *btn_inc = lv_obj_create(ctrl);
    lv_obj_remove_style_all(btn_inc);
    lv_obj_set_size(btn_inc, 16, 18);
    lv_obj_set_style_bg_opa(btn_inc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_inc, AG_BTN_GRAY, 0);
    lv_obj_set_style_radius(btn_inc, 2, 0);
    lv_obj_set_layout(btn_inc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn_inc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_inc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_inc, port_inc_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *inc_lbl = lv_label_create(btn_inc);
    lv_label_set_text(inc_lbl, "+");
    lv_obj_set_style_text_font(inc_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(inc_lbl, AG_TEXT_DARK, 0);
}

/* ── Section: System ─────────────────────────────────────────────────── */

static void build_section_system(lv_obj_t *parent)
{
    lv_obj_t *card = make_section_card(parent, "System", &usr_montserrat_12);

    /* Row: BLE */
    lv_obj_t *r1 = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *ble_lbl = make_label(r1, "BLE", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_flex_grow(ble_lbl, 1);
    make_inline_badge(r1,
                      "\xe2\x9c\x93",
                      "Connected",
                      AG_GREEN,
                      NULL,
                      NULL);

    row_divider(card);

    /* Row: Daemon */
    lv_obj_t *r2 = make_hrow(card, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *daemon_lbl = make_label(r2, "Daemon", &usr_montserrat_12, AG_TEXT_DARK);
    lv_obj_set_flex_grow(daemon_lbl, 1);
    make_inline_badge(r2,
                      "\xe2\x9c\x93",
                      "Running:19280",
                      AG_GREEN,
                      NULL,
                      NULL);
}

/* ── Layout builders ─────────────────────────────────────────────────── */

static void create_header(lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hdr, AG_WHITE, 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);
    lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "AI Agent");
    lv_obj_set_style_text_color(title, AG_TEXT_DARK, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
}

static void create_full_divider(lv_obj_t *parent)
{
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(div, AG_CARD_BDR, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_scroll_area(lv_obj_t *parent)
{
    /* Fixed height: 222 - header(28) - top_div(1) - bot_div(1) - bottom_bar(28) = 164 */
    lv_obj_t *area = lv_obj_create(parent);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, LV_PCT(100), 164);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(area, AG_BG, 0);
    lv_obj_set_layout(area, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(area, 8, 0);
    lv_obj_set_style_pad_row(area, 6, 0);
    lv_obj_add_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_OFF);
    s_scroll_area = area;

    build_section_ai_tools(area);
    build_section_recommended(area);
    // build_section_network(area);
    build_section_system(area);
}

static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BOTTOM_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, AG_WHITE, 0);
    lv_obj_set_style_pad_hor(bar, 10, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: UP SCROLL label */
    lv_obj_t *up = lv_label_create(bar);
    lv_label_set_text(up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_font(up, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(up, AG_TEXT_MID, 0);

    /* Left spacer */
    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_size(sp1, 1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    /* Center: BACK + CONFIRM button group */
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

    /* Right spacer */
    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_size(sp2, 1, 1);
    lv_obj_set_flex_grow(sp2, 1);

    /* Right: DN SCROLL label */
    lv_obj_t *dn = lv_label_create(bar);
    lv_label_set_text(dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_font(dn, &usr_montserrat_12, 0);
    lv_obj_set_style_text_color(dn, AG_TEXT_MID, 0);
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

    if (code == LV_EVENT_CLICKED) {
        ui_agent_screen_confirm();
        return;
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            ui_agent_screen_move(-1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            ui_agent_screen_move(1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
            ui_agent_screen_confirm();
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
    int32_t first_scroll_target = max_scroll;
    if (s_scroll_area && s_recommended_card && max_scroll > 0) {
        lv_area_t scroll_coords;
        lv_area_t card_coords;
        lv_obj_get_coords(s_scroll_area, &scroll_coords);
        lv_obj_get_coords(s_recommended_card, &card_coords);

        int32_t scroll_y = lv_obj_get_scroll_y(s_scroll_area);
        int32_t card_bottom = scroll_y + card_coords.y2 - scroll_coords.y1 + 1;
        int32_t recommended_end = card_bottom - lv_obj_get_height(s_scroll_area);
        if (recommended_end < 0) recommended_end = 0;

        first_scroll_target = recommended_end + AG_SCROLL_STEP;
        if (first_scroll_target > max_scroll) first_scroll_target = max_scroll;
    }

    s_scroll_focus_count = 0;
    if (max_scroll > 0) {
        for (int32_t target = first_scroll_target;
                target < max_scroll && s_scroll_focus_count < AG_SCROLL_FOCUS_MAX;
                target += AG_SCROLL_STEP) {
            s_scroll_focus_targets[s_scroll_focus_count++] = target;
        }
        if (s_scroll_focus_count < AG_SCROLL_FOCUS_MAX &&
                (s_scroll_focus_count == 0 ||
                 s_scroll_focus_targets[s_scroll_focus_count - 1] != max_scroll)) {
            s_scroll_focus_targets[s_scroll_focus_count++] = max_scroll;
        }
    }

    s_group = lv_group_create();
    lv_group_set_wrap(s_group, false);

    for (int8_t i = 0; i < AG_ACTION_COUNT; i++) {
        if (s_action_buttons[i].button) {
            enable_focus_event_bubble(s_action_buttons[i].button, parent);
            lv_group_add_obj(s_group, s_action_buttons[i].button);
        }
    }

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
    if (s_action_buttons[0].button) {
        lv_group_focus_obj(s_action_buttons[0].button);
    } else {
        lv_group_focus_obj(s_focus_items[0]);
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void ui_agent_screen_create(lv_obj_t *parent)
{
    s_port_label          = NULL;
    s_scroll_area         = NULL;
    s_recommended_card    = NULL;
    s_group               = NULL;
    s_scroll_focus_count  = 0;
    s_scroll_focus_index  = 0;
    s_daemon_port = 19280;
    s_next_request_id = 1;
    for (int8_t i = 0; i < AG_SCROLL_FOCUS_MAX; i++) {
        s_focus_items[i] = NULL;
        s_scroll_focus_targets[i] = 0;
    }
    for (int8_t i = 0; i < AG_ACTION_COUNT; i++) {
        s_action_buttons[i] = {};
    }
    s_claude_status = {};
    s_codex_status = {};

    lv_obj_remove_style_all(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, AG_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_header(parent);
    create_full_divider(parent);
    create_scroll_area(parent);
    create_full_divider(parent);
    create_bottom_bar(parent);
    create_focus_items(parent);
}

void ui_agent_screen_move(int8_t dir)
{
    if (!s_group || dir == 0) return;

    lv_obj_t *focused = lv_group_get_focused(s_group);
    agent_action_btn_t *focused_action = find_action_btn_by_obj(focused);
    if (focused_action) {
        int8_t action_index = (int8_t)(focused_action - s_action_buttons);
        int8_t next_action = (int8_t)(action_index + (dir < 0 ? -1 : 1));
        if (next_action >= 0 && next_action < AG_ACTION_COUNT &&
                s_action_buttons[next_action].button) {
            lv_group_focus_obj(s_action_buttons[next_action].button);
            return;
        }
    }

    if (dir < 0) {
        lv_group_focus_prev(s_group);
    } else if (dir > 0) {
        lv_group_focus_next(s_group);
    }
}

void ui_agent_screen_confirm(void)
{
    if (!s_group) return;

    lv_obj_t *focused = lv_group_get_focused(s_group);
    trigger_action_btn(find_action_btn_by_obj(focused));
}

void ui_agent_screen_append_log(const char *text)
{
    (void)text;
}

void ui_agent_screen_set_action_cb(ui_agent_action_cb_t cb)
{
    s_action_cb = cb;
}

void ui_agent_screen_set_status_request_cb(ui_agent_status_request_cb_t cb)
{
    s_status_request_cb = cb;
}

void ui_agent_screen_request_status(void)
{
    uint32_t request_id = s_next_request_id++;
    if (request_id == 0) {
        request_id = s_next_request_id++;
    }
    if (s_status_request_cb) {
        s_status_request_cb(request_id);
    }
}

void ui_agent_screen_action_result(uint32_t request_id, bool success)
{
    if (request_id == 0) return;

    for (int8_t i = 0; i < AG_ACTION_COUNT; i++) {
        agent_action_btn_t *action_btn = &s_action_buttons[i];
        if (action_btn->pending_request_id != request_id) continue;

        action_btn->pending_request_id = 0;
        if (success) {
            set_action_btn_visual(action_btn,
                                  action_btn->success_text,
                                  action_btn->success_bg);
        } else {
            set_action_btn_visual(action_btn,
                                  action_btn->idle_text,
                                  action_btn->idle_bg);
        }
        if (success) {
            ui_agent_screen_request_status();
        }
        return;
    }
}

void ui_agent_screen_set_tool_status(const char *tool,
                                     const char *name,
                                     bool detected,
                                     bool hook_installed,
                                     const char *detail)
{
    apply_tool_status(find_tool_status_row(tool),
                      name,
                      detected,
                      hook_installed,
                      detail);
}

int32_t ui_agent_screen_get_daemon_port(void)
{
    return s_daemon_port;
}

lv_group_t *ui_agent_screen_get_group(void)
{
    return s_group;
}
