/**
 * @file      ui_notify_detail_screen.h
 * @brief     Notify Detail Screen - public API (Ardot node 24:24)
 */
#pragma once
#include <lvgl.h>

// Reuse NS_ colors from ui_notify_screen.h (included by the .ino)
#include "ui_notify_screen.h"

// Detail-screen-specific colors
#define ND_TITLE_ORANGE   lv_color_make(249, 115,  22)   // #F97316
#define ND_BADGE_BG         lv_color_make(148,  69,   0)   // #944500
#define ND_BADGE_TEXT_COLOR lv_color_make(245, 158,  10)   // #F59E0A  badge text (orange-gold)
#define ND_MSG_BOX_BG     lv_color_make(241, 242, 245)   // #F1F2F5
#define ND_MSG_TEXT_COLOR lv_color_make( 77,  79,  87)   // #4D4F57
#define ND_LIGHT_DIVIDER  lv_color_make(235, 236, 237)   // #EBECED

typedef void (*notify_detail_cb_t)(void);

/** Create the detail screen inside parent (pass lv_obj_create(NULL)). */
void ui_notify_detail_screen_create(lv_obj_t *parent);

/**
 * Populate the detail screen with notification data.
 * @param title_type   Header label, e.g. "PERM REQUEST"
 * @param badge        Badge text, e.g. "vk"
 * @param session      Session name, e.g. "vibe-keyboard"
 * @param timestamp    Time string, e.g. "12:43"
 * @param action_label Label above the message box, e.g. "ACTION"
 * @param message      Message text, e.g. "Write(crates/...)"
 * @param tool_type    Tool type value, e.g. "Write"
 */
void ui_notify_detail_screen_set_data(const char *title_type,
                                      const char *badge,
                                      const char *session,
                                      const char *timestamp,
                                      const char *action_label,
                                      const char *message,
                                      const char *tool_type);

/**
 * Register callbacks for the three bottom-bar buttons.
 * Pass NULL to ignore a button.
 */
void ui_notify_detail_screen_set_callbacks(notify_detail_cb_t on_back,
                                           notify_detail_cb_t on_allow,
                                           notify_detail_cb_t on_deny);

lv_group_t *ui_notify_detail_screen_get_group(void);
