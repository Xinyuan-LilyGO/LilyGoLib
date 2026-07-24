#pragma once

#include <LilyGoLib.h>
#include <lvgl.h>

#define VK_KEY_CAP  0x100u
#define VK_KEY_FN   0x101u

typedef void (*vk_input_key_state_cb_t)(uint32_t key, bool pressed);

void vk_input_begin(LilyGoLoRaPager &board);
void vk_input_set_key_state_callback(vk_input_key_state_cb_t callback);
