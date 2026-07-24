/**
 * @file      ui_setup_screen.h
 * @brief     Setup Screen - system configuration UI
 */
#pragma once
#include <lvgl.h>

typedef void (*ui_setup_screen_confirm_cb_t)(int index);

void ui_setup_screen_create(lv_obj_t *parent);
void ui_setup_screen_move(int8_t dir);
void ui_setup_screen_confirm(void);
int8_t ui_setup_screen_selected(void);
void ui_setup_screen_set_confirm_cb(ui_setup_screen_confirm_cb_t cb);
lv_group_t *ui_setup_screen_get_group(void);
