/**
 * @file      ui_select_session.h
 * @brief     Select Session Screen - public API
 */
#pragma once
#include <lvgl.h>

typedef void (*ui_select_session_confirm_cb_t)(int index);

void ui_select_session_create(lv_obj_t *parent);
void ui_select_session_set_count(const char *count_str);
void ui_select_session_set_session_count(uint8_t count);
void ui_select_session_set_row(uint8_t row,
                               const char *badge,
                               bool badge_ok,
                               const char *name,
                               const char *status,
                               const char *subtitle);
void ui_select_session_move(int delta);   /* +1 = down, -1 = up */
void ui_select_session_select(uint8_t index);
void ui_select_session_focus_current(void);
int  ui_select_session_selected_index();  /* 0-based */
void ui_select_session_set_confirm_cb(ui_select_session_confirm_cb_t cb);
lv_group_t *ui_select_session_get_group(void);
