#pragma once

#include <Arduino.h>

enum vk_sound_type_t : uint8_t {
    VK_SOUND_PERMISSION_ALERT = 0,
    VK_SOUND_SESSION_COMPLETE = 1,
    VK_SOUND_ERROR = 2,
    VK_SOUND_CLICK = 3,
    VK_SOUND_TYPE_COUNT = 4,
};

void vk_sound_begin(void);
void vk_sound_set_volume(uint8_t volume);
uint8_t vk_sound_get_volume(void);
void vk_sound_set_muted(bool muted);
bool vk_sound_get_muted(void);

bool vk_sound_play_event(uint8_t event_type);
bool vk_sound_preview(int8_t sound_type);

bool vk_sound_set_mapping(uint8_t event_type, int8_t sound_type);
bool vk_sound_set_mapping_id(uint8_t event_type, const char *sound_id);
int8_t vk_sound_get_mapping(uint8_t event_type);
const char *vk_sound_get_name(int8_t sound_type);
