/**
 * @file      ui_about_screen.h
 * @brief     About Screen - public API
 */
#pragma once
#include <lvgl.h>

void ui_about_screen_create(lv_obj_t *parent);
void ui_about_screen_move(int8_t dir);
lv_group_t *ui_about_screen_get_group(void);
