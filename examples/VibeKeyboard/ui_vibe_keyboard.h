/**
 * @file      ui_vibe_keyboard.h
 * @brief     Vibe Keyboard Screen - public API
 */
#pragma once
#include <lvgl.h>

typedef void (*ui_vibe_keyboard_open_session_cb_t)(void);
typedef void (*ui_vibe_keyboard_selection_cb_t)(int index);
typedef void (*ui_vibe_keyboard_voice_cb_t)(bool pressed);

void ui_vibe_keyboard_create(lv_obj_t *parent);
void ui_vibe_keyboard_update(const char *time_str,
                              const char *session_count,
                              const char *model_name,
                              const char *context_tokens,
                              const char *cost,
                              const char *tokens,
                              const char *prompt);
void ui_vibe_keyboard_move(int delta);  /* cyclic: +1 = next session, -1 = prev */
void ui_vibe_keyboard_select(int index);
int  ui_vibe_keyboard_selected_index(void);
void ui_vibe_keyboard_set_session_count(uint8_t count);
void ui_vibe_keyboard_set_session(uint8_t index,
                                  const char *name,
                                  const char *model,
                                  const char *context_tokens,
                                  const char *cost,
                                  const char *tokens,
                                  const char *status,
                                  const char *prompt);
void ui_vibe_keyboard_set_ble_connected(bool connected);
void ui_vibe_keyboard_set_open_session_cb(ui_vibe_keyboard_open_session_cb_t cb);
void ui_vibe_keyboard_set_selection_cb(ui_vibe_keyboard_selection_cb_t cb);
void ui_vibe_keyboard_set_voice_cb(ui_vibe_keyboard_voice_cb_t cb);
lv_group_t *ui_vibe_keyboard_get_group(void);
