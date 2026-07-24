/**
 * @file      ui_keys_screen.h
 * @brief     Keys Screen - keymap viewer public API
 */
#pragma once
#include <lvgl.h>

void ui_keys_screen_create(lv_obj_t *parent);
void ui_keys_screen_scroll_up(void);
void ui_keys_screen_scroll_down(void);
void ui_keys_screen_set_binding(int key_idx, const char *binding_str);
lv_group_t *ui_keys_screen_get_group(void);
