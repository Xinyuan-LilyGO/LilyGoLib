#include "vk_sound.h"

#include <LilyGoLib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "sounds/click_wav.inc"
#include "sounds/error_wav.inc"
#include "sounds/permission_alert_wav.inc"
#include "sounds/session_complete_wav.inc"

struct sound_asset_t {
    const uint8_t *data;
    size_t size;
    const char *name;
};

static const size_t WAV_HEADER_SIZE = 44;
static const uint32_t SOUND_SAMPLE_RATE = 22050;
static const uint8_t SOUND_BITS_PER_SAMPLE = 16;
static const uint8_t SOUND_CHANNELS = 1;

static const sound_asset_t s_assets[VK_SOUND_TYPE_COUNT] = {
    { permission_alert_wav, permission_alert_wav_len, "alert" },
    { session_complete_wav, session_complete_wav_len, "ding" },
    { error_wav, error_wav_len, "buzz" },
    { click_wav, click_wav_len, "click" },
};

static int8_t s_mapping[VK_SOUND_TYPE_COUNT] = {
    VK_SOUND_PERMISSION_ALERT,
    VK_SOUND_SESSION_COMPLETE,
    VK_SOUND_ERROR,
    VK_SOUND_CLICK,
};
static QueueHandle_t s_play_queue = NULL;
static uint8_t s_volume = 51;
static bool s_muted = false;

static void sound_task(void *parameter)
{
    (void)parameter;
    int8_t sound_type = -1;
    bool stream_open = false;

    for (;;) {
        if (xQueueReceive(s_play_queue, &sound_type, portMAX_DELAY) != pdTRUE) continue;
        if (s_muted || s_volume == 0 || sound_type < 0 || sound_type >= VK_SOUND_TYPE_COUNT) continue;

        const sound_asset_t &asset = s_assets[sound_type];
        if (asset.size <= WAV_HEADER_SIZE) continue;

        if (!stream_open) {
            int result = instance.codec.open(SOUND_BITS_PER_SAMPLE,
                                             SOUND_CHANNELS,
                                             SOUND_SAMPLE_RATE);
            if (result != 0) {
                Serial.printf("[sound] failed to open audio stream: %d\n", result);
                continue;
            }
            stream_open = true;
        }

        instance.powerControl(POWER_SPEAK, true);
        instance.codec.setVolume(s_volume);
        int result = instance.codec.write((uint8_t *)(asset.data + WAV_HEADER_SIZE),
                                          asset.size - WAV_HEADER_SIZE);
        if (result != 0) {
            Serial.printf("[sound] failed to write PCM data: %d\n", result);
        }
    }
}

void vk_sound_begin(void)
{
    if (s_play_queue) return;

    instance.codec.setVolume(s_volume);
    s_play_queue = xQueueCreate(1, sizeof(int8_t));
    if (!s_play_queue) {
        Serial.println("[sound] failed to create playback queue");
        return;
    }

    if (xTaskCreate(sound_task, "vk_sound", 4096, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(s_play_queue);
        s_play_queue = NULL;
        Serial.println("[sound] failed to create playback task");
    }
}

void vk_sound_set_volume(uint8_t volume)
{
    s_volume = volume > 100 ? 100 : volume;
}

uint8_t vk_sound_get_volume(void)
{
    return s_volume;
}

void vk_sound_set_muted(bool muted)
{
    s_muted = muted;
}

bool vk_sound_get_muted(void)
{
    return s_muted;
}

bool vk_sound_preview(int8_t sound_type)
{
    if (!s_play_queue || s_muted || s_volume == 0 ||
            sound_type < 0 || sound_type >= VK_SOUND_TYPE_COUNT) {
        return false;
    }
    return xQueueOverwrite(s_play_queue, &sound_type) == pdPASS;
}

bool vk_sound_play_event(uint8_t event_type)
{
    if (event_type >= VK_SOUND_TYPE_COUNT) return false;
    return vk_sound_preview(s_mapping[event_type]);
}

bool vk_sound_set_mapping(uint8_t event_type, int8_t sound_type)
{
    if (event_type >= VK_SOUND_TYPE_COUNT ||
            sound_type < -1 || sound_type >= VK_SOUND_TYPE_COUNT) {
        return false;
    }
    s_mapping[event_type] = sound_type;
    return true;
}

bool vk_sound_set_mapping_id(uint8_t event_type, const char *sound_id)
{
    if (!sound_id) return false;

    static const char *const ids[VK_SOUND_TYPE_COUNT] = {
        "builtin:alert", "builtin:ding", "builtin:buzz", "builtin:click"
    };
    for (int8_t i = 0; i < VK_SOUND_TYPE_COUNT; i++) {
        if (strcmp(sound_id, ids[i]) == 0) return vk_sound_set_mapping(event_type, i);
    }
    if (strcmp(sound_id, "mute") == 0 || strcmp(sound_id, "off") == 0) {
        return vk_sound_set_mapping(event_type, -1);
    }
    return false;
}

int8_t vk_sound_get_mapping(uint8_t event_type)
{
    if (event_type >= VK_SOUND_TYPE_COUNT) return -1;
    return s_mapping[event_type];
}

const char *vk_sound_get_name(int8_t sound_type)
{
    if (sound_type < 0 || sound_type >= VK_SOUND_TYPE_COUNT) return "mute";
    return s_assets[sound_type].name;
}
