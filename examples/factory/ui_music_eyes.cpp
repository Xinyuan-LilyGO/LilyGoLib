 /**
 * @file      ui_music_eyes.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-14
 * 
 */
#include "ui_define.h"
#include "hal_es7210_microphone.h"
#include "hal_microphone_waveform.h"

#include <math.h>
#include <string.h>

#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER) && \
        __has_include(<Adafruit_NeoPixel.h>)
#include <Adafruit_NeoPixel.h>
#define MUSIC_EYES_HAS_PIXELS 1
#else
#define MUSIC_EYES_HAS_PIXELS 0
#endif

#ifndef LILYGO_LORA_PAGER_WS2812_LEFT_PIN
#define LILYGO_LORA_PAGER_WS2812_LEFT_PIN LORA_BUSY
#endif
#ifndef LILYGO_LORA_PAGER_WS2812_RIGHT_PIN
#define LILYGO_LORA_PAGER_WS2812_RIGHT_PIN LORA_RST
#endif
#ifndef LILYGO_LORA_PAGER_WS2812_LEFT_COUNT
#define LILYGO_LORA_PAGER_WS2812_LEFT_COUNT 6
#endif
#ifndef LILYGO_LORA_PAGER_WS2812_RIGHT_COUNT
#define LILYGO_LORA_PAGER_WS2812_RIGHT_COUNT 6
#endif

#define MUSIC_EYES_FRAME_MS             50
#define MUSIC_EYES_BEAT_COOLDOWN_MS     220
#define MUSIC_EYES_SURPRISE_HOLD_MS     420
#define MUSIC_EYES_RANDOM_MIN_MS        1700
#define MUSIC_EYES_RANDOM_SPAN_MS       2600
#if defined(ARDUINO_T_LORA_PAGER)
#define MUSIC_EYES_PAGER_MIC_GAIN_DB    30.0f
#endif

enum MusicEyesMood : uint8_t {
    MUSIC_EYES_CALM,
    MUSIC_EYES_HAPPY,
    MUSIC_EYES_GROOVE,
    MUSIC_EYES_SURPRISED,
    MUSIC_EYES_EXCITED,
    MUSIC_EYES_SLEEPY,
    MUSIC_EYES_SKEPTIC,
    MUSIC_EYES_MOOD_COUNT,
};

enum MusicEyesMouth : uint8_t {
    MUSIC_EYES_MOUTH_FLAT,
    MUSIC_EYES_MOUTH_SMILE,
    MUSIC_EYES_MOUTH_OPEN,
};

typedef struct {
    float eye_w;
    float eye_h;
    float pupil;
    float eye_y;
    float eye_angle;
    float brow_angle;
    float brow_y;
    float mouth_w;
    float mouth_h;
    uint32_t background;
    uint32_t accent;
    MusicEyesMouth mouth;
} MusicEyesPreset;

/* Geometry is relative to the responsive base eye size. */
static const MusicEyesPreset mood_presets[MUSIC_EYES_MOOD_COUNT] = {
    {0.92f, 0.82f, 0.40f,  0.00f,  0.0f,   0.0f, 0.00f, 0.34f, 0.04f,
     0x07131A, 0x55D6BE, MUSIC_EYES_MOUTH_FLAT},
    {1.00f, 0.45f, 0.32f,  0.12f,  0.0f,   0.0f, 0.10f, 0.58f, 0.34f,
     0x10150A, 0xD8F05A, MUSIC_EYES_MOUTH_SMILE},
    {0.92f, 0.72f, 0.34f, -0.03f,  5.0f,  12.0f, 0.00f, 0.45f, 0.18f,
     0x090C1B, 0x5DA9FF, MUSIC_EYES_MOUTH_SMILE},
    {1.08f, 1.10f, 0.27f, -0.08f,  0.0f,   0.0f, 0.08f, 0.29f, 0.42f,
     0x170B12, 0xFF6A9A, MUSIC_EYES_MOUTH_OPEN},
    {1.05f, 0.88f, 0.38f, -0.08f, -5.0f, -14.0f, 0.00f, 0.62f, 0.38f,
     0x160D05, 0xFFB84D, MUSIC_EYES_MOUTH_SMILE},
    {0.90f, 0.22f, 0.26f,  0.13f,  0.0f,   0.0f, 0.14f, 0.30f, 0.04f,
     0x0B0D12, 0x93A4BC, MUSIC_EYES_MOUTH_FLAT},
    {0.92f, 0.64f, 0.31f,  0.00f,  0.0f,  18.0f, 0.00f, 0.38f, 0.04f,
     0x0D1012, 0x70D7EA, MUSIC_EYES_MOUTH_FLAT},
};

static lv_timer_t *frame_timer = NULL;
static lv_obj_t *page_container = NULL;
static lv_obj_t *face_layer = NULL;
static lv_obj_t *left_eye = NULL;
static lv_obj_t *right_eye = NULL;
static lv_obj_t *left_pupil = NULL;
static lv_obj_t *right_pupil = NULL;
static lv_obj_t *left_brow = NULL;
static lv_obj_t *right_brow = NULL;
static lv_obj_t *mouth = NULL;
static lv_obj_t *mouth_cover = NULL;
static lv_obj_t *source_icon = NULL;
static lv_obj_t *level_bars[7] = {};

static MusicEyesPreset current_face;
static MusicEyesMood current_mood = MUSIC_EYES_CALM;
static bool microphone_running = false;
static bool es7210_input = false;
#if defined(ARDUINO_T_LORA_PAGER)
static bool microphone_gain_overridden = false;
static float previous_microphone_gain = 0.0f;
#endif
static bool random_mode = true;
static bool audio_calibrated = false;
static uint8_t failed_audio_reads = 0;
static float energy = 0.0f;
static float average_level = 0.0f;
static float noise_floor = 0.0f;
static float adaptive_peak = 0.0f;
static float gaze_x = 0.0f;
static float gaze_y = 0.0f;
static float gaze_target_x = 0.0f;
static float gaze_target_y = 0.0f;
static uint32_t app_started_ms = 0;
static uint32_t next_blink_ms = 0;
static uint32_t blink_started_ms = 0;
static uint32_t next_gaze_ms = 0;
static uint32_t next_random_mood_ms = 0;
static uint32_t surprise_until_ms = 0;
static uint32_t last_beat_ms = 0;
static uint32_t random_state = 0x6D2B79F5U;

#if MUSIC_EYES_HAS_PIXELS
static Adafruit_NeoPixel left_pixels(LILYGO_LORA_PAGER_WS2812_LEFT_COUNT,
                                     LILYGO_LORA_PAGER_WS2812_LEFT_PIN,
                                     NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel right_pixels(LILYGO_LORA_PAGER_WS2812_RIGHT_COUNT,
                                      LILYGO_LORA_PAGER_WS2812_RIGHT_PIN,
                                      NEO_GRB + NEO_KHZ800);
static bool pixels_running = false;
#endif

static void stop_microphone();

static float clamp_f(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t next_random()
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static uint32_t random_range(uint32_t minimum, uint32_t span)
{
    return minimum + (span ? next_random() % span : 0);
}

static uint8_t color_channel(uint32_t color, uint8_t shift)
{
    return (uint8_t)((color >> shift) & 0xFFU);
}

static uint32_t blend_hex(uint32_t from, uint32_t to, float amount)
{
    amount = clamp_f(amount, 0.0f, 1.0f);
    uint8_t r = (uint8_t)(color_channel(from, 16) +
                          (color_channel(to, 16) - color_channel(from, 16)) * amount);
    uint8_t g = (uint8_t)(color_channel(from, 8) +
                          (color_channel(to, 8) - color_channel(from, 8)) * amount);
    uint8_t b = (uint8_t)(color_channel(from, 0) +
                          (color_channel(to, 0) - color_channel(from, 0)) * amount);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t scale_hex(uint32_t color, uint8_t level)
{
    uint32_t r = color_channel(color, 16) * level / 255;
    uint32_t g = color_channel(color, 8) * level / 255;
    uint32_t b = color_channel(color, 0) * level / 255;
    return (r << 16) | (g << 8) | b;
}

static void style_blob(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static void set_mood(MusicEyesMood mood)
{
    if (mood >= MUSIC_EYES_MOOD_COUNT || current_mood == mood) return;
    current_mood = mood;
}

static void update_random_behavior(uint32_t now)
{
    if (!time_reached(now, next_random_mood_ms)) return;

    static const MusicEyesMood choices[] = {
        MUSIC_EYES_CALM, MUSIC_EYES_HAPPY, MUSIC_EYES_GROOVE,
        MUSIC_EYES_SURPRISED, MUSIC_EYES_EXCITED, MUSIC_EYES_SLEEPY,
        MUSIC_EYES_SKEPTIC, MUSIC_EYES_HAPPY,
    };
    MusicEyesMood next = choices[next_random() % (sizeof(choices) / sizeof(choices[0]))];
    if (next == current_mood) {
        next = choices[(next_random() + 1) % (sizeof(choices) / sizeof(choices[0]))];
    }
    set_mood(next);
    static const float mood_energy[MUSIC_EYES_MOOD_COUNT] = {
        0.08f, 0.28f, 0.50f, 0.92f, 0.76f, 0.02f, 0.18f,
    };
    energy += (mood_energy[next] - energy) * 0.65f;
    next_random_mood_ms = now + random_range(MUSIC_EYES_RANDOM_MIN_MS,
                                             MUSIC_EYES_RANDOM_SPAN_MS);
}

static bool read_microphone_level(uint16_t *level)
{
    if (!level) return false;
    *level = 0;
#if FACTORY_HAS_MICROPHONE
    uint32_t sum = 0;
    uint8_t count = 0;
    if (es7210_input) {
        ES7210WaveformData data;
        if (!hw_audio_get_es7210_waveform_data(&data)) return false;
        uint8_t mask = hw_get_es7210_mic_mask();
        for (uint8_t i = 0; i < ES7210_MIC_CHANNELS; ++i) {
            if (mask & (1U << i)) {
                sum += data.mic_level[i];
                ++count;
            }
        }
    } else {
        MicrophoneWaveformData data;
        if (!hw_audio_get_waveform_data(&data)) return false;
        for (uint8_t i = 0; i < data.channels && i < MIC_WAVEFORM_MAX_CHANNELS; ++i) {
            sum += data.level[i];
            ++count;
        }
    }
    if (!count) return false;
    *level = (uint16_t)(sum / count);
    return true;
#else
    return false;
#endif
}

static void update_audio_behavior(uint32_t now)
{
    uint16_t raw = 0;
    if (!read_microphone_level(&raw)) {
        energy *= 0.92f;
        if (++failed_audio_reads >= 20) {
            stop_microphone();
            random_mode = true;
            next_random_mood_ms = now;
            if (source_icon) lv_label_set_text(source_icon, LV_SYMBOL_SHUFFLE);
        }
        return;
    }
    failed_audio_reads = 0;

    if (!audio_calibrated) {
        noise_floor = raw;
        adaptive_peak = raw + 700.0f;
        average_level = raw;
        audio_calibrated = true;
    }

    float floor_rate = raw < noise_floor * 1.35f ? 0.025f : 0.0008f;
    noise_floor += ((float)raw - noise_floor) * floor_rate;
    if (raw > adaptive_peak) {
        adaptive_peak = raw;
    } else {
        adaptive_peak += (noise_floor + 650.0f - adaptive_peak) * 0.008f;
    }
    if (adaptive_peak < noise_floor + 300.0f) adaptive_peak = noise_floor + 300.0f;

    float adaptive = ((float)raw - noise_floor) / (adaptive_peak - noise_floor);
    float absolute = (float)raw / 6000.0f;
    float target = clamp_f(fmaxf(adaptive * 0.82f, absolute), 0.0f, 1.0f);
    float rate = target > energy ? 0.48f : 0.13f;
    energy += (target - energy) * rate;

    bool beat = raw > average_level * 1.38f + 160.0f &&
                time_reached(now, last_beat_ms + MUSIC_EYES_BEAT_COOLDOWN_MS);
    average_level += ((float)raw - average_level) * 0.075f;
    if (beat) {
        last_beat_ms = now;
        energy = fmaxf(energy, 0.72f);
        surprise_until_ms = now + MUSIC_EYES_SURPRISE_HOLD_MS;
    }

    if (!time_reached(now, surprise_until_ms)) {
        set_mood(MUSIC_EYES_SURPRISED);
    } else if (energy < 0.10f) {
        set_mood(MUSIC_EYES_CALM);
    } else if (energy < 0.32f) {
        set_mood(MUSIC_EYES_HAPPY);
    } else if (energy < 0.62f) {
        set_mood(MUSIC_EYES_GROOVE);
    } else {
        set_mood(MUSIC_EYES_EXCITED);
    }
}

static float approach(float value, float target, float amount)
{
    return value + (target - value) * amount;
}

static void approach_preset()
{
    const MusicEyesPreset &target = mood_presets[current_mood];
    const float speed = current_mood == MUSIC_EYES_SURPRISED ? 0.34f : 0.20f;
    current_face.eye_w = approach(current_face.eye_w, target.eye_w, speed);
    current_face.eye_h = approach(current_face.eye_h, target.eye_h, speed);
    current_face.pupil = approach(current_face.pupil, target.pupil, speed);
    current_face.eye_y = approach(current_face.eye_y, target.eye_y, speed);
    current_face.eye_angle = approach(current_face.eye_angle, target.eye_angle, speed);
    current_face.brow_angle = approach(current_face.brow_angle, target.brow_angle, speed);
    current_face.brow_y = approach(current_face.brow_y, target.brow_y, speed);
    current_face.mouth_w = approach(current_face.mouth_w, target.mouth_w, speed);
    current_face.mouth_h = approach(current_face.mouth_h, target.mouth_h, speed);
    current_face.background = blend_hex(current_face.background, target.background, speed);
    current_face.accent = blend_hex(current_face.accent, target.accent, speed);
    current_face.mouth = target.mouth;
}

static float blink_closed(uint32_t now)
{
    if (blink_started_ms == 0) return 0.0f;
    uint32_t elapsed = now - blink_started_ms;
    if (elapsed < 85) return (float)elapsed / 85.0f;
    if (elapsed < 175) return 1.0f - (float)(elapsed - 85) / 90.0f;
    blink_started_ms = 0;
    return 0.0f;
}

static void update_gaze_and_blink(uint32_t now)
{
    if (time_reached(now, next_blink_ms)) {
        blink_started_ms = now;
        uint32_t minimum = current_mood == MUSIC_EYES_EXCITED ? 1100 : 2100;
        next_blink_ms = now + random_range(minimum, 2600);
    }

    if (time_reached(now, next_gaze_ms)) {
        gaze_target_x = ((int32_t)(next_random() % 201) - 100) / 100.0f;
        gaze_target_y = ((int32_t)(next_random() % 141) - 70) / 100.0f;
        if (current_mood == MUSIC_EYES_SURPRISED) {
            gaze_target_x = 0.0f;
            gaze_target_y = 0.0f;
        }
        next_gaze_ms = now + random_range(850, 1400);
    }
    gaze_x = approach(gaze_x, gaze_target_x, 0.12f);
    gaze_y = approach(gaze_y, gaze_target_y, 0.12f);
}

static void update_meter()
{
    int active = (int)(energy * 7.0f + 0.5f);
    uint32_t accent = current_face.accent;
    for (int i = 0; i < 7; ++i) {
        bool on = i < active;
        lv_obj_set_style_bg_color(level_bars[i], lv_color_hex(accent), 0);
        lv_obj_set_style_bg_opa(level_bars[i], on ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

static void update_mouth(int center_y, int base_eye_h)
{
    int width = clamp_i32((int)(base_eye_h * current_face.mouth_w * 1.7f), 22, 100);
    int height = clamp_i32((int)(base_eye_h * current_face.mouth_h), 4, 48);
    int y = center_y + base_eye_h * 8 / 10;
    lv_obj_set_style_bg_color(mouth_cover, lv_color_hex(current_face.background), 0);

    if (current_face.mouth == MUSIC_EYES_MOUTH_FLAT) {
        lv_obj_set_size(mouth, width, 4);
        lv_obj_set_style_bg_color(mouth, lv_color_hex(current_face.accent), 0);
        lv_obj_set_style_bg_opa(mouth, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mouth, 0, 0);
        lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(mouth_cover, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(mouth, width, height);
        lv_obj_set_style_bg_opa(mouth, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(mouth, 4, 0);
        lv_obj_set_style_border_color(mouth, lv_color_hex(current_face.accent), 0);
        lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
        if (current_face.mouth == MUSIC_EYES_MOUTH_SMILE) {
            lv_obj_remove_flag(mouth_cover, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(mouth_cover, width + 10, height / 2 + 5);
            lv_obj_align(mouth_cover, LV_ALIGN_CENTER, 0, y - height / 4 - 2);
        } else {
            lv_obj_add_flag(mouth_cover, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_align(mouth, LV_ALIGN_CENTER, 0, y);
}

static void update_face(uint32_t now)
{
    if (!face_layer) return;
    approach_preset();
    update_gaze_and_blink(now);

    int width = lv_obj_get_content_width(face_layer);
    int height = lv_obj_get_content_height(face_layer);
    int base_eye_w = clamp_i32(width * 24 / 100, 46, 118);
    int base_eye_h = clamp_i32(height * 31 / 100, 42, 104);
    int gap = clamp_i32(width * 7 / 100, 16, 46);
    int eye_w = clamp_i32((int)(base_eye_w * current_face.eye_w), 30, 128);
    int eye_h = clamp_i32((int)(base_eye_h * current_face.eye_h), 4, 116);
    int pupil = clamp_i32((int)(base_eye_h * current_face.pupil), 7, eye_h - 5);
    float closed = blink_closed(now);
    eye_h = clamp_i32((int)(eye_h * (1.0f - closed * 0.91f)), 4, 116);
    pupil = clamp_i32(pupil, 3, eye_h - 2);

    float elapsed = (now - app_started_ms) / 1000.0f;
    int bob = (int)(sinf(elapsed * (4.0f + energy * 7.0f)) *
                    (1.5f + energy * height * 0.025f));
    int center_y = (int)(height * 0.43f + current_face.eye_y * base_eye_h) + bob;
    int center_offset = (eye_w + gap) / 2;
    int look_limit_x = clamp_i32((eye_w - pupil) / 2 - 3, 0, 22);
    int look_limit_y = clamp_i32((eye_h - pupil) / 2 - 2, 0, 15);
    int look_x = (int)(gaze_x * look_limit_x);
    int look_y = (int)(gaze_y * look_limit_y + sinf(elapsed * 8.0f) * energy * 3.0f);

    lv_obj_set_style_bg_color(face_layer, lv_color_hex(current_face.background), 0);
    lv_obj_set_size(left_eye, eye_w, eye_h);
    lv_obj_set_size(right_eye, eye_w, eye_h);
    lv_obj_set_style_radius(left_eye, eye_h / 2, 0);
    lv_obj_set_style_radius(right_eye, eye_h / 2, 0);
    lv_obj_set_style_transform_rotation(left_eye, (int)(current_face.eye_angle * 10), 0);
    lv_obj_set_style_transform_rotation(right_eye, (int)(-current_face.eye_angle * 10), 0);
    lv_obj_align(left_eye, LV_ALIGN_TOP_MID, -center_offset, center_y - eye_h / 2);
    lv_obj_align(right_eye, LV_ALIGN_TOP_MID, center_offset, center_y - eye_h / 2);

    lv_obj_set_size(left_pupil, pupil, pupil);
    lv_obj_set_size(right_pupil, pupil, pupil);
    lv_obj_set_style_radius(left_pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(right_pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(left_pupil, LV_ALIGN_CENTER, look_x, look_y);
    lv_obj_align(right_pupil, LV_ALIGN_CENTER, look_x, look_y);

    int brow_w = eye_w * 4 / 5;
    int brow_y = center_y - eye_h / 2 - 11 + (int)(current_face.brow_y * base_eye_h);
    lv_obj_set_size(left_brow, brow_w, 5);
    lv_obj_set_size(right_brow, brow_w, 5);
    lv_obj_set_style_bg_color(left_brow, lv_color_hex(current_face.accent), 0);
    lv_obj_set_style_bg_color(right_brow, lv_color_hex(current_face.accent), 0);
    lv_obj_set_style_transform_rotation(left_brow, (int)(current_face.brow_angle * 10), 0);
    lv_obj_set_style_transform_rotation(right_brow, (int)(-current_face.brow_angle * 10), 0);
    lv_obj_align(left_brow, LV_ALIGN_TOP_MID, -center_offset, brow_y);
    lv_obj_align(right_brow, LV_ALIGN_TOP_MID, center_offset, brow_y);

    lv_obj_set_style_bg_color(left_pupil, lv_color_hex(current_face.background), 0);
    lv_obj_set_style_bg_color(right_pupil, lv_color_hex(current_face.background), 0);
    update_mouth(center_y, base_eye_h);
    update_meter();
}

#if MUSIC_EYES_HAS_PIXELS
static void start_pixels()
{
    /* Pager LED accessories reuse radio BUSY/RST pins. Never contend with a radio. */
    if (hw_has_lora_hardware()) return;
    left_pixels.begin();
    right_pixels.begin();
    left_pixels.clear();
    right_pixels.clear();
    left_pixels.show();
    right_pixels.show();
    pixels_running = true;
}

static void update_pixels(uint32_t now)
{
    if (!pixels_running) return;
    uint8_t brightness = (uint8_t)(18 + energy * 145.0f);
    float wave = (sinf((now - app_started_ms) * (0.006f + energy * 0.012f)) + 1.0f) * 0.5f;
    uint32_t base = scale_hex(current_face.accent,
                              (uint8_t)(brightness * (0.60f + wave * 0.40f)));
    uint8_t left_count = left_pixels.numPixels();
    uint8_t right_count = right_pixels.numPixels();
    uint8_t active = (uint8_t)(1 + energy * (left_count - 1));
    for (uint8_t i = 0; i < left_count; ++i) {
        uint8_t level = i < active ? 255 : 30;
        if (current_mood == MUSIC_EYES_SURPRISED) level = 255;
        left_pixels.setPixelColor(i, scale_hex(base, level));
    }
    for (uint8_t i = 0; i < right_count; ++i) {
        uint8_t level = i < active ? 255 : 30;
        if (current_mood == MUSIC_EYES_SURPRISED) level = 255;
        right_pixels.setPixelColor(i, scale_hex(base, level));
    }
    left_pixels.show();
    right_pixels.show();
}

static void stop_pixels()
{
    if (!pixels_running) return;
    left_pixels.clear();
    right_pixels.clear();
    left_pixels.show();
    right_pixels.show();
    pinMode(LILYGO_LORA_PAGER_WS2812_LEFT_PIN, INPUT);
    pinMode(LILYGO_LORA_PAGER_WS2812_RIGHT_PIN, INPUT);
    pixels_running = false;
}
#else
static void start_pixels() {}
static void update_pixels(uint32_t now) { (void)now; }
static void stop_pixels() {}
#endif

static void stop_microphone()
{
    if (!microphone_running) return;
#if defined(ARDUINO_T_LORA_PAGER)
    if (microphone_gain_overridden) {
        hw_set_mic_gain(previous_microphone_gain);
        microphone_gain_overridden = false;
    }
#endif
    if (es7210_input) {
        hw_set_es7210_mic_stop();
    } else {
        hw_set_mic_waveform_stop();
    }
    microphone_running = false;
}

static bool start_microphone()
{
#if FACTORY_HAS_MICROPHONE
    es7210_input = hw_has_es7210_mic_array();
    microphone_running = es7210_input ?
                         hw_set_es7210_mic_start(HW_ES7210_DEFAULT_MIC_MASK) :
                         hw_set_mic_waveform_start();
#if defined(ARDUINO_T_LORA_PAGER)
    if (microphone_running) {
        previous_microphone_gain = hw_get_mic_gain();
        hw_set_mic_gain(MUSIC_EYES_PAGER_MIC_GAIN_DB);
        microphone_gain_overridden = true;
    }
#endif
    return microphone_running;
#else
    es7210_input = false;
    microphone_running = false;
    return false;
#endif
}

static void clear_references()
{
    face_layer = NULL;
    left_eye = NULL;
    right_eye = NULL;
    left_pupil = NULL;
    right_pupil = NULL;
    left_brow = NULL;
    right_brow = NULL;
    mouth = NULL;
    mouth_cover = NULL;
    source_icon = NULL;
    memset(level_bars, 0, sizeof(level_bars));
}

static void close_app()
{
    if (frame_timer) {
        lv_timer_del(frame_timer);
        frame_timer = NULL;
    }
    stop_microphone();
    stop_pixels();
    set_low_power_mode_flag(true);
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    clear_references();
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    close_app();
    menu_show();
}

static void face_click_cb(lv_event_t *e)
{
    (void)e;
    uint32_t now = lv_tick_get();
    surprise_until_ms = now + 650;
    set_mood(MUSIC_EYES_SURPRISED);
    energy = fmaxf(energy, 0.88f);
}

static void frame_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    if (random_mode) {
        update_random_behavior(now);
        static const float random_targets[MUSIC_EYES_MOOD_COUNT] = {
            0.08f, 0.28f, 0.50f, 0.92f, 0.76f, 0.02f, 0.18f,
        };
        energy = approach(energy, random_targets[current_mood], 0.035f);
    } else {
        update_audio_behavior(now);
    }
    update_face(now);
    update_pixels(now);
}

static lv_obj_t *create_eye(lv_obj_t *parent, lv_obj_t **pupil)
{
    lv_obj_t *eye = lv_obj_create(parent);
    style_blob(eye, 0xFFFFFF);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(eye, true, 0);

    *pupil = lv_obj_create(eye);
    style_blob(*pupil, mood_presets[MUSIC_EYES_CALM].background);
    lv_obj_set_style_radius(*pupil, LV_RADIUS_CIRCLE, 0);
    return eye;
}

static void create_face(lv_obj_t *parent)
{
    face_layer = lv_obj_create(parent);
    lv_obj_set_size(face_layer, LV_PCT(100), LV_PCT(100));
    style_blob(face_layer, mood_presets[MUSIC_EYES_CALM].background);
    lv_obj_add_flag(face_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face_layer, face_click_cb, LV_EVENT_CLICKED, NULL);

    left_eye = create_eye(face_layer, &left_pupil);
    right_eye = create_eye(face_layer, &right_pupil);
    left_brow = lv_obj_create(face_layer);
    right_brow = lv_obj_create(face_layer);
    style_blob(left_brow, mood_presets[MUSIC_EYES_CALM].accent);
    style_blob(right_brow, mood_presets[MUSIC_EYES_CALM].accent);
    lv_obj_set_style_radius(left_brow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(right_brow, LV_RADIUS_CIRCLE, 0);

    mouth = lv_obj_create(face_layer);
    mouth_cover = lv_obj_create(face_layer);
    style_blob(mouth, mood_presets[MUSIC_EYES_CALM].accent);
    style_blob(mouth_cover, mood_presets[MUSIC_EYES_CALM].background);

    source_icon = lv_label_create(face_layer);
    lv_label_set_text(source_icon, random_mode ? LV_SYMBOL_SHUFFLE : LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(source_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(source_icon, LV_OPA_50, 0);
    lv_obj_align(source_icon, LV_ALIGN_TOP_RIGHT, -12, 10);

    lv_obj_t *meter = lv_obj_create(face_layer);
    lv_obj_set_size(meter, 68, 16);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter, 0, 0);
    lv_obj_set_style_pad_all(meter, 0, 0);
    lv_obj_set_style_pad_column(meter, 4, 0);
    lv_obj_set_flex_flow(meter, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meter, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(meter, LV_ALIGN_BOTTOM_MID, 0, -7);
    for (int i = 0; i < 7; ++i) {
        level_bars[i] = lv_obj_create(meter);
        lv_obj_set_size(level_bars[i], 6, 4 + i * 2);
        style_blob(level_bars[i], mood_presets[MUSIC_EYES_CALM].accent);
        lv_obj_set_style_radius(level_bars[i], 2, 0);
        lv_obj_set_style_bg_opa(level_bars[i], LV_OPA_20, 0);
    }
}

void ui_music_eyes_enter(lv_obj_t *parent)
{
    hw_set_es7210_mic_stop();
    hw_set_mic_waveform_stop();
    clear_references();
    set_low_power_mode_flag(false);

    page_container = ui_create_app_page(parent, "Music Eyes", back_event_handler);
    lv_obj_set_style_pad_all(page_container, 0, 0);
    lv_obj_set_style_pad_row(page_container, 0, 0);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(page_container, LV_SCROLLBAR_MODE_OFF);
    ui_app_page_enable_nav_auto_hide(page_container, 5000);

    random_state ^= lv_tick_get() + (uint32_t)(uintptr_t)page_container;
    app_started_ms = lv_tick_get();
    next_blink_ms = app_started_ms + random_range(1200, 1600);
    next_gaze_ms = app_started_ms + 600;
    next_random_mood_ms = app_started_ms + 900;
    surprise_until_ms = app_started_ms;
    last_beat_ms = app_started_ms - MUSIC_EYES_BEAT_COOLDOWN_MS;
    blink_started_ms = 0;
    energy = 0.0f;
    average_level = 0.0f;
    noise_floor = 0.0f;
    adaptive_peak = 0.0f;
    audio_calibrated = false;
    failed_audio_reads = 0;
    gaze_x = gaze_y = gaze_target_x = gaze_target_y = 0.0f;
    current_mood = MUSIC_EYES_CALM;
    current_face = mood_presets[MUSIC_EYES_CALM];

    random_mode = !start_microphone();
    create_face(page_container);
    start_pixels();
    update_face(app_started_ms);
    update_pixels(app_started_ms);
    frame_timer = lv_timer_create(frame_timer_cb, MUSIC_EYES_FRAME_MS, NULL);
}

void ui_music_eyes_exit(lv_obj_t *parent)
{
    (void)parent;
    close_app();
}

app_t ui_music_eyes_main = {
    .setup_func_cb = ui_music_eyes_enter,
    .exit_func_cb = ui_music_eyes_exit,
    .user_data = nullptr,
};
