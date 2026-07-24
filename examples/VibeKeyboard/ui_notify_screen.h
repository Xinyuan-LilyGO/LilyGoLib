/**
 * @file      ui_notify_screen.h
 * @brief     Notify Screen - public API
 */
#pragma once
#include <lvgl.h>

// Color palette from Ardot design
#define NS_BG_COLOR       lv_color_make(242, 243, 245)
#define NS_WHITE          lv_color_make(255, 255, 255)
#define NS_TITLE_BLUE     lv_color_make(88,  101, 242)
#define NS_COUNT_GRAY     lv_color_make(181, 186, 193)
#define NS_DIVIDER_COLOR  lv_color_make(209, 211, 215)
#define NS_BADGE_OK_BG    lv_color_make(18,  76,  42)
#define NS_BADGE_ERR_BG   lv_color_make(81,  23,  23)
#define NS_BADGE_ERR_TEXT lv_color_make(242, 63,  67)
#define NS_ROW_SEL_BG     lv_color_make(226, 234, 249)
#define NS_ROW_SEL_BORDER lv_color_make(88,  130, 244)
#define NS_TEXT_PRIMARY   lv_color_make(30,  34,  39)
#define NS_TEXT_SECONDARY lv_color_make(128, 132, 142)
#define NS_BTN_CONFIRM_BG     lv_color_make(88,  101, 242)  // ~12% opacity on blue
#define NS_BTN_CONFIRM_BORDER lv_color_make(88,  101, 242)
#define NS_NAV_GRAY       lv_color_make(128, 132, 142)

/** Create the Notify Screen content inside parent (pass lv_obj_create(NULL)). */
void ui_notify_screen_create(lv_obj_t *parent);

/**
 * Update a notify row (0-based, max NS_MAX_ROWS).
 * @param badge     Badge label text ("ok", "err", etc.)
 * @param badge_ok  true = green badge, false = red badge
 * @param session   Session name
 * @param status    Status text
 * @param count     Count string e.g. "(1)"
 * @param selected  Whether this row shows the selection highlight
 */
void ui_notify_screen_set_row(uint8_t row,
                              const char *badge, bool badge_ok,
                              const char *session, const char *status,
                              const char *count, bool selected);

/** Update header count text, e.g. "2 in 2 sessions" */
void ui_notify_screen_set_count(const char *text);

/** Move selection up (-1) or down (+1); wraps around. */
void ui_notify_screen_move(int8_t dir);

typedef void (*ui_notify_select_cb_t)(uint8_t row);

/** Register callback fired when user confirms the selected row. */
void ui_notify_screen_set_select_cb(ui_notify_select_cb_t cb);

/** Confirm the current selection (triggers the select callback). */
void ui_notify_screen_confirm(void);

/** Return the currently highlighted row index. */
uint8_t ui_notify_screen_get_selected(void);

/** Return this screen's LVGL group (one per screen). */
lv_group_t *ui_notify_screen_get_group(void);
