/**
 * @file      ui_sound_screen.h
 * @brief     Sound Screen - public API
 */
#pragma once
#include <lvgl.h>

void ui_sound_screen_create(lv_obj_t *parent);
void ui_sound_screen_move(int8_t dir);
void ui_sound_screen_confirm(void);
void ui_sound_screen_refresh(void);
lv_group_t *ui_sound_screen_get_group(void);
