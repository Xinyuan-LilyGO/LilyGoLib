 /**
 * @file      ui_ws2812.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-02
 * 
 * T-LoRa Pager no-radio WS2812 strip test.
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include <math.h>

#if defined(ARDUINO_T_LORA_PAGER)

#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER) && __has_include(<Adafruit_NeoPixel.h>)
#include <Adafruit_NeoPixel.h>
#define UI_WS2812_ENABLED 1
#else
#define UI_WS2812_ENABLED 0
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

#if LILYGO_LORA_PAGER_WS2812_LEFT_COUNT != LILYGO_LORA_PAGER_WS2812_RIGHT_COUNT
#error "The factory WS2812 app currently expects equal left/right LED counts."
#endif

#define WS2812_LED_COUNT LILYGO_LORA_PAGER_WS2812_LEFT_COUNT
#define WS2812_ENCODER_BRIGHTNESS_STEP 16
#define WS2812_ENCODER_SPEED_STEP      16
#define WS2812_ENCODER_HUE_STEP        16
#define WS2812_EXPRESSION_IDLE_MS      5000
#define WS2812_EXPRESSION_ANIM_MS      40
#define WS2812_EXPRESSION_LOOK_MS      1200
#define WS2812_EXPRESSION_BLINK_MS     260
#define WS2812_EXPRESSION_BLINK_PERIOD 3400
#define WS2812_EXPRESSION_BOB_MS       2200
#define WS2812_EXPRESSION_KB_BREATH_MS 1800
#define WS2812_EXPRESSION_BATTERY_POLL_MS 10000
#define WS2812_EXPRESSION_IMU_POLL_MS  50
#define WS2812_EXPRESSION_IMU_WARMUP_MS 300
#define WS2812_EXPRESSION_TOUCH_COOLDOWN_MS 300
#define WS2812_EXPRESSION_SHY_HOLD_MS  2400
#define WS2812_EXPRESSION_STARTLE_HOLD_MS 1600
#define WS2812_EXPRESSION_SHAKE_HOLD_MS 1700
#define WS2812_EXPRESSION_TOUCH_ACCEL_DELTA_SQ 0.75f
#define WS2812_EXPRESSION_SHAKE_ACCEL_DELTA_SQ 3.0f
#define WS2812_EXPRESSION_STARTLE_ACCEL_DELTA_SQ 8.0f
#define WS2812_EXPRESSION_TOUCH_ANGLE_DELTA 1.5f
#define WS2812_EXPRESSION_STARTLE_ANGLE_DELTA 6.0f
#ifndef WS2812_EXPRESSION_IMU_DEBUG
#define WS2812_EXPRESSION_IMU_DEBUG 1
#endif
#define WS2812_EXPRESSION_IMU_LOG_MS 200

enum ExpressionBatteryLevel : uint8_t {
    EXPRESSION_BATTERY_UNKNOWN,
    EXPRESSION_BATTERY_CRITICAL,
    EXPRESSION_BATTERY_LOW,
    EXPRESSION_BATTERY_MID,
    EXPRESSION_BATTERY_HIGH,
    EXPRESSION_BATTERY_FULL,
    EXPRESSION_BATTERY_CHARGING,
};

enum ExpressionInteraction : uint8_t {
    EXPRESSION_INTERACTION_NONE,
    EXPRESSION_INTERACTION_SHY,
    EXPRESSION_INTERACTION_STARTLED,
    EXPRESSION_INTERACTION_SHAKE,
};

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint16_t gap_ms;
} ExpressionSoundNote;

static const ExpressionSoundNote expression_shake_sound[] = {
    {1568, 45, 35},
    {2093, 55, 35},
    {2637, 85, 0},
};

static lv_obj_t *page_container = NULL;
static lv_timer_t *effect_timer = NULL;
static lv_timer_t *expression_timer = NULL;
static lv_timer_t *expression_anim_timer = NULL;
static lv_timer_t *expression_battery_timer = NULL;
static lv_timer_t *expression_imu_timer = NULL;
static lv_timer_t *expression_sound_timer = NULL;
static lv_obj_t *expression_overlay = NULL;
static lv_obj_t *expression_face = NULL;
static lv_obj_t *expression_left_eye = NULL;
static lv_obj_t *expression_right_eye = NULL;
static lv_obj_t *expression_left_pupil = NULL;
static lv_obj_t *expression_right_pupil = NULL;
static lv_obj_t *expression_left_cheek = NULL;
static lv_obj_t *expression_right_cheek = NULL;
static lv_obj_t *expression_mouth = NULL;
static lv_obj_t *expression_sleep_label = NULL;
static lv_obj_t *expression_previous_focus = NULL;
static bool expression_overlay_close_pending = false;
static bool ignore_encoder_activity = false;
static bool expression_kb_backlight_saved = false;
static uint8_t expression_blink_closed = 0;
static uint8_t expression_saved_kb_backlight = 0;
static ExpressionBatteryLevel expression_battery_level = EXPRESSION_BATTERY_UNKNOWN;
static ExpressionInteraction expression_interaction = EXPRESSION_INTERACTION_NONE;
static uint32_t expression_started_ms = 0;
static uint32_t expression_interaction_started_ms = 0;
static uint32_t expression_last_touch_ms = 0;
static uint32_t expression_imu_started_ms = 0;
static uint32_t expression_last_motion_log_ms = 0;
static uint32_t expression_last_accel_sequence = 0;
static uint32_t expression_last_bma_impulse1_events = 0;
static uint32_t expression_last_bma_impulse2_events = 0;
static uint32_t expression_last_bma_impulse3_events = 0;
static uint32_t expression_last_motion_events = 0;
static uint32_t expression_last_tilt_events = 0;
static uint32_t expression_sound_wait_started_ms = 0;
static uint32_t expression_sound_wait_ms = 0;
static int32_t expression_eye_w = 0;
static int32_t expression_eye_h = 0;
static int32_t expression_eye_gap = 0;
static int32_t expression_pupil_size = 0;
static uint8_t expression_sound_note = 0;
static bool expression_imu_registered = false;
static bool expression_accel_has_baseline = false;
static bool expression_pose_has_baseline = false;
static bool expression_bma_has_baseline = false;
static float expression_peak_accel_delta_sq = 0.0f;
static float expression_peak_angle_delta = 0.0f;
static float expression_last_accel_x = 0.0f;
static float expression_last_accel_y = 0.0f;
static float expression_last_accel_z = 0.0f;
static float expression_last_roll = 0.0f;
static float expression_last_pitch = 0.0f;
static float expression_last_heading = 0.0f;
static lv_obj_t *status_value_label = NULL;
static lv_obj_t *left_preview[WS2812_LED_COUNT] = {};
static lv_obj_t *right_preview[WS2812_LED_COUNT] = {};

#if UI_WS2812_ENABLED
static Adafruit_NeoPixel left_strip(LILYGO_LORA_PAGER_WS2812_LEFT_COUNT,
                                    LILYGO_LORA_PAGER_WS2812_LEFT_PIN,
                                    NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel right_strip(LILYGO_LORA_PAGER_WS2812_RIGHT_COUNT,
                                     LILYGO_LORA_PAGER_WS2812_RIGHT_PIN,
                                     NEO_GRB + NEO_KHZ800);
#endif

enum Ws2812Effect : uint8_t {
    WS2812_EFFECT_OFF,
    WS2812_EFFECT_SOLID,
    WS2812_EFFECT_BREATHING,
    WS2812_EFFECT_RAINBOW,
    WS2812_EFFECT_COLOR_WIPE,
    WS2812_EFFECT_THEATER,
    WS2812_EFFECT_COMET,
    WS2812_EFFECT_SCANNER,
    WS2812_EFFECT_SPARKLE,
    WS2812_EFFECT_COUNT
};

static const char *effect_options =
    "Off\n"
    "Solid\n"
    "Breathing\n"
    "Rainbow\n"
    "Color Wipe\n"
    "Theater\n"
    "Comet\n"
    "Scanner\n"
    "Sparkle";

static const char *effect_names[WS2812_EFFECT_COUNT] = {
    "Off",
    "Solid",
    "Breathing",
    "Rainbow",
    "Color Wipe",
    "Theater",
    "Comet",
    "Scanner",
    "Sparkle",
};

static uint8_t effect_mode = WS2812_EFFECT_RAINBOW;
static uint8_t brightness = 64;
static uint8_t speed = 50;
static uint8_t hue = 0;
static bool left_enabled = true;
static bool right_enabled = true;
static bool mirror_right = false;
static bool keyboard_sync_enabled = true;
static uint16_t phase = 0;
static uint32_t left_colors[WS2812_LED_COUNT] = {};
static uint32_t right_colors[WS2812_LED_COUNT] = {};

static void expression_overlay_event_cb(lv_event_t *e);
static void refresh_output(void);
static void restore_expression_keyboard_backlight(void);
static void apply_expression_battery_style(void);
static void update_expression_face(void);
static void update_expression_keyboard_backlight(void);
static void start_expression_imu_process(void);
static void stop_expression_imu_process(void);

void ui_ws2812_init(void)
{
#if UI_WS2812_ENABLED
    if (hw_has_lora_hardware()) {
        return;
    }

    left_strip.begin();
    right_strip.begin();
    left_strip.clear();
    right_strip.clear();
    left_strip.show();
    right_strip.show();
    pinMode(LILYGO_LORA_PAGER_WS2812_LEFT_PIN, INPUT);
    pinMode(LILYGO_LORA_PAGER_WS2812_RIGHT_PIN, INPUT);
#endif
}

static uint8_t clamp_u8(uint16_t value)
{
    return value > 255 ? 255 : value;
}

static int32_t clamp_i32(int32_t value, int32_t min_v, int32_t max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static uint32_t counter_delta_u32(uint32_t current, uint32_t previous)
{
    return current >= previous ? current - previous : current;
}

static float abs_f(float value)
{
    return value < 0.0f ? -value : value;
}

static float max_f(float a, float b)
{
    return a > b ? a : b;
}

static float angle_delta_deg(float a, float b)
{
    float delta = abs_f(a - b);
    while (delta > 360.0f) {
        delta -= 360.0f;
    }
    if (delta > 180.0f) {
        delta = 360.0f - delta;
    }
    return delta;
}

static const char *expression_interaction_name(ExpressionInteraction interaction)
{
    switch (interaction) {
    case EXPRESSION_INTERACTION_SHY:
        return "shy";
    case EXPRESSION_INTERACTION_STARTLED:
        return "startled";
    case EXPRESSION_INTERACTION_SHAKE:
        return "shake";
    default:
        return "none";
    }
}

static float expression_accel_delta_to_g(float delta_sq)
{
    if (delta_sq <= 0.0f) {
        return 0.0f;
    }
    return sqrtf(delta_sq) / 9.80665f;
}

#if WS2812_EXPRESSION_IMU_DEBUG
static bool expression_motion_log_due(bool force)
{
    if (force || expression_last_motion_log_ms == 0 ||
            lv_tick_elaps(expression_last_motion_log_ms) >= WS2812_EXPRESSION_IMU_LOG_MS) {
        expression_last_motion_log_ms = lv_tick_get();
        return true;
    }
    return false;
}

static void log_expression_imu_delta(float delta_sq, float accel_g,
                                     uint32_t accel_sequence, float angle_delta,
                                     bool touch, bool strong_touch, bool shake)
{
    bool force = touch || strong_touch || shake;
    if (!expression_motion_log_due(force)) {
        return;
    }

    LILYGO_LOG_I("WS2812 expr imu seq=%lu mag_g=%.2f delta_sq=%.2f delta_g=%.2f peak_sq=%.2f peak_g=%.2f angle=%.1f peak_angle=%.1f flags=%c%c%c thr_sq(touch/shake/startle)=%.2f/%.2f/%.2f thr_angle(touch/startle)=%.1f/%.1f",
                 (unsigned long)accel_sequence,
                 accel_g,
                 delta_sq,
                 expression_accel_delta_to_g(delta_sq),
                 expression_peak_accel_delta_sq,
                 expression_accel_delta_to_g(expression_peak_accel_delta_sq),
                 angle_delta,
                 expression_peak_angle_delta,
                 touch ? 'T' : '-',
                 strong_touch ? 'S' : '-',
                 shake ? 'K' : '-',
                 WS2812_EXPRESSION_TOUCH_ACCEL_DELTA_SQ,
                 WS2812_EXPRESSION_SHAKE_ACCEL_DELTA_SQ,
                 WS2812_EXPRESSION_STARTLE_ACCEL_DELTA_SQ,
                 WS2812_EXPRESSION_TOUCH_ANGLE_DELTA,
                 WS2812_EXPRESSION_STARTLE_ANGLE_DELTA);
}

static void log_expression_bma_delta(const bma_sensor_snapshot_t &snapshot,
                                     uint32_t impulse1_delta,
                                     uint32_t impulse2_delta,
                                     uint32_t impulse3_delta,
                                     uint32_t motion_delta,
                                     uint32_t tilt_delta,
                                     bool touch,
                                     bool strong_touch,
                                     bool shake)
{
    bool force = impulse1_delta || impulse2_delta || impulse3_delta ||
                 motion_delta || tilt_delta || touch || strong_touch || shake;
    if (!expression_motion_log_due(force)) {
        return;
    }

    LILYGO_LOG_I("WS2812 expr bma g=%.2f peak_g=%.2f bma_impulse=%lu/%lu/%lu motion=%lu tilt=%lu flags=%c%c%c",
                 snapshot.magnitude / 9.80665f,
                 snapshot.peak_magnitude / 9.80665f,
                 (unsigned long)impulse1_delta,
                 (unsigned long)impulse2_delta,
                 (unsigned long)impulse3_delta,
                 (unsigned long)motion_delta,
                 (unsigned long)tilt_delta,
                 touch ? 'T' : '-',
                 strong_touch ? 'S' : '-',
                 shake ? 'K' : '-');
}
#endif

static uint32_t expression_interaction_hold_ms(void)
{
    switch (expression_interaction) {
    case EXPRESSION_INTERACTION_SHY:
        return WS2812_EXPRESSION_SHY_HOLD_MS;
    case EXPRESSION_INTERACTION_STARTLED:
        return WS2812_EXPRESSION_STARTLE_HOLD_MS;
    case EXPRESSION_INTERACTION_SHAKE:
        return WS2812_EXPRESSION_SHAKE_HOLD_MS;
    default:
        return 0;
    }
}

static bool expression_has_active_interaction(void)
{
    uint32_t hold_ms = expression_interaction_hold_ms();
    return expression_interaction != EXPRESSION_INTERACTION_NONE &&
           hold_ms > 0 &&
           lv_tick_elaps(expression_interaction_started_ms) < hold_ms;
}

static bool use_encoder_slider_step(void)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return false;
    }
    lv_indev_type_t type = lv_indev_get_type(indev);
    return type == LV_INDEV_TYPE_ENCODER || type == LV_INDEV_TYPE_KEYPAD;
}

static bool event_from_encoder(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = NULL;

    switch (code) {
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_CLICKED:
    case LV_EVENT_RELEASED:
    case LV_EVENT_KEY:
        indev = lv_event_get_indev(e);
        break;
    case LV_EVENT_FOCUSED:
    case LV_EVENT_DEFOCUSED:
    case LV_EVENT_ROTARY:
    case LV_EVENT_VALUE_CHANGED:
        indev = lv_indev_active();
        break;
    default:
        return false;
    }

    if (!indev) {
        indev = lv_indev_active();
    }
    if (!indev) {
        return false;
    }

    return lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER;
}

static void save_expression_keyboard_backlight(void)
{
    if (!keyboard_sync_enabled || expression_kb_backlight_saved || !hw_has_keyboard()) {
        return;
    }

    expression_saved_kb_backlight = hw_get_kb_backlight();
    expression_kb_backlight_saved = true;
}

static void restore_expression_keyboard_backlight(void)
{
    if (!expression_kb_backlight_saved) {
        return;
    }

    if (hw_has_keyboard()) {
        hw_set_kb_backlight(expression_saved_kb_backlight);
    }
    expression_kb_backlight_saved = false;
}

static uint32_t expression_battery_accent_hex(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 0xFFF176;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 0xFF4FA3;
        }
        return 0xFF7DA8;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 0xFF4D6D;
    case EXPRESSION_BATTERY_LOW:
        return 0xFF9F1C;
    case EXPRESSION_BATTERY_MID:
        return 0xFFD166;
    case EXPRESSION_BATTERY_HIGH:
        return 0x39B8FF;
    case EXPRESSION_BATTERY_FULL:
        return 0x36E39A;
    case EXPRESSION_BATTERY_CHARGING:
        return 0x4DE0FF;
    default:
        return 0x888888;
    }
}

static lv_color_t expression_battery_bg_color(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return lv_color_hex(0x0B0A03);
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return lv_color_hex(0x0C0410);
        }
        return lv_color_hex(0x0C0408);
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return lv_color_hex(0x0B0306);
    case EXPRESSION_BATTERY_LOW:
        return lv_color_hex(0x0B0802);
    case EXPRESSION_BATTERY_MID:
        return lv_color_hex(0x0A0903);
    case EXPRESSION_BATTERY_CHARGING:
        return lv_color_hex(0x02090C);
    default:
        return UI_COLOR_BG;
    }
}

static lv_color_t expression_battery_eye_color(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return lv_color_hex(0xFFFFFF);
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return lv_color_hex(0xFFF7FB);
        }
        return lv_color_hex(0xFFF0F6);
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return lv_color_hex(0xFFE8EE);
    case EXPRESSION_BATTERY_LOW:
        return lv_color_hex(0xFFF1D6);
    case EXPRESSION_BATTERY_FULL:
        return lv_color_hex(0xEEFFF5);
    case EXPRESSION_BATTERY_CHARGING:
        return lv_color_hex(0xE8FBFF);
    default:
        return lv_color_white();
    }
}

static lv_color_t expression_battery_pupil_color(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return lv_color_hex(0x161000);
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return lv_color_hex(0x361028);
        }
        return lv_color_hex(0x30121F);
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return lv_color_hex(0x32131B);
    case EXPRESSION_BATTERY_LOW:
        return lv_color_hex(0x2D2110);
    case EXPRESSION_BATTERY_CHARGING:
        return lv_color_hex(0x071E27);
    default:
        return lv_color_hex(0x171A20);
    }
}

static const char *expression_battery_mouth_text(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return "o";
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return "^";
        }
        return "w";
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return "_";
    case EXPRESSION_BATTERY_LOW:
        return "~";
    case EXPRESSION_BATTERY_MID:
        return "o";
    case EXPRESSION_BATTERY_HIGH:
        return "w";
    case EXPRESSION_BATTERY_FULL:
        return "u";
    case EXPRESSION_BATTERY_CHARGING:
        return "v";
    default:
        return ".";
    }
}

static const char *expression_battery_float_text(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return "!!";
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return "<3";
        }
        return "//";
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return "..";
    case EXPRESSION_BATTERY_LOW:
        return "z";
    case EXPRESSION_BATTERY_MID:
        return "zZ";
    case EXPRESSION_BATTERY_HIGH:
        return "Zz";
    case EXPRESSION_BATTERY_FULL:
        return "!!";
    case EXPRESSION_BATTERY_CHARGING:
        return "++";
    default:
        return "?";
    }
}

static lv_opa_t expression_battery_cheek_opa(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return LV_OPA_50;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return LV_OPA_COVER;
        }
        return LV_OPA_80;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return LV_OPA_30;
    case EXPRESSION_BATTERY_LOW:
        return LV_OPA_40;
    case EXPRESSION_BATTERY_FULL:
    case EXPRESSION_BATTERY_CHARGING:
        return LV_OPA_70;
    default:
        return LV_OPA_60;
    }
}

static void apply_expression_battery_style(void)
{
    if (!expression_overlay || !lv_obj_is_valid(expression_overlay)) {
        return;
    }

    lv_color_t accent = lv_color_hex(expression_battery_accent_hex());
    lv_color_t eye_color = expression_battery_eye_color();
    lv_color_t pupil_color = expression_battery_pupil_color();

    lv_obj_set_style_bg_color(expression_overlay, expression_battery_bg_color(), 0);
    if (expression_left_eye) {
        lv_obj_set_style_bg_color(expression_left_eye, eye_color, 0);
    }
    if (expression_right_eye) {
        lv_obj_set_style_bg_color(expression_right_eye, eye_color, 0);
    }
    if (expression_left_pupil) {
        lv_obj_set_style_bg_color(expression_left_pupil, pupil_color, 0);
    }
    if (expression_right_pupil) {
        lv_obj_set_style_bg_color(expression_right_pupil, pupil_color, 0);
    }
    if (expression_left_cheek) {
        int32_t cheek_w = expression_eye_w / 2;
        int32_t cheek_h = 8;
        if (expression_has_active_interaction()) {
            if (expression_interaction == EXPRESSION_INTERACTION_SHY) {
                cheek_w = expression_eye_w * 3 / 4;
                cheek_h = 12;
            } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
                cheek_w = expression_eye_w * 2 / 3;
                cheek_h = 10;
            }
        }
        lv_obj_set_size(expression_left_cheek, cheek_w, cheek_h);
        lv_obj_set_style_bg_color(expression_left_cheek, accent, 0);
        lv_obj_set_style_bg_opa(expression_left_cheek, expression_battery_cheek_opa(), 0);
    }
    if (expression_right_cheek) {
        int32_t cheek_w = expression_eye_w / 2;
        int32_t cheek_h = 8;
        if (expression_has_active_interaction()) {
            if (expression_interaction == EXPRESSION_INTERACTION_SHY) {
                cheek_w = expression_eye_w * 3 / 4;
                cheek_h = 12;
            } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
                cheek_w = expression_eye_w * 2 / 3;
                cheek_h = 10;
            }
        }
        lv_obj_set_size(expression_right_cheek, cheek_w, cheek_h);
        lv_obj_set_style_bg_color(expression_right_cheek, accent, 0);
        lv_obj_set_style_bg_opa(expression_right_cheek, expression_battery_cheek_opa(), 0);
    }
    if (expression_mouth) {
        lv_label_set_text(expression_mouth, expression_battery_mouth_text());
        lv_obj_set_style_text_color(expression_mouth, accent, 0);
    }
    if (expression_sleep_label) {
        lv_label_set_text(expression_sleep_label, expression_battery_float_text());
        lv_obj_set_style_text_color(expression_sleep_label, accent, 0);
    }
}

static ExpressionBatteryLevel expression_battery_level_from_percent(int16_t percent)
{
    if (percent < 0) {
        return EXPRESSION_BATTERY_UNKNOWN;
    }
    if (percent <= 10) {
        return EXPRESSION_BATTERY_CRITICAL;
    }
    if (percent <= 30) {
        return EXPRESSION_BATTERY_LOW;
    }
    if (percent <= 60) {
        return EXPRESSION_BATTERY_MID;
    }
    if (percent <= 85) {
        return EXPRESSION_BATTERY_HIGH;
    }
    return EXPRESSION_BATTERY_FULL;
}

static ExpressionBatteryLevel expression_battery_level_from_voltage(float mv)
{
    if (mv <= 0.0f) {
        return EXPRESSION_BATTERY_UNKNOWN;
    }
    if (mv < 3450.0f) {
        return EXPRESSION_BATTERY_CRITICAL;
    }
    if (mv < 3650.0f) {
        return EXPRESSION_BATTERY_LOW;
    }
    if (mv < 3850.0f) {
        return EXPRESSION_BATTERY_MID;
    }
    if (mv < 4100.0f) {
        return EXPRESSION_BATTERY_HIGH;
    }
    return EXPRESSION_BATTERY_FULL;
}

static void poll_expression_battery(void)
{
    power_monitor_snapshot_t snapshot;
    hw_get_power_monitor_snapshot(snapshot);

    ExpressionBatteryLevel level = EXPRESSION_BATTERY_UNKNOWN;
    if (snapshot.battery_percent.valid) {
        int32_t percent = (int32_t)(snapshot.battery_percent.value + 0.5f);
        level = expression_battery_level_from_percent((int16_t)clamp_i32(percent, 0, 100));
    } else if (snapshot.battery_mv.valid) {
        level = expression_battery_level_from_voltage(snapshot.battery_mv.value);
    }

    bool charge_done = snapshot.charge_done;
    bool charging = snapshot.charging;
    if (!charging && !charge_done) {
        if (snapshot.vbus_present_valid) {
            charging = snapshot.vbus_present;
        } else {
            charging = hw_adapter_is_connected();
        }
    }

    if (charge_done) {
        level = EXPRESSION_BATTERY_FULL;
    } else if (charging) {
        level = EXPRESSION_BATTERY_CHARGING;
    }

    expression_battery_level = level;
    apply_expression_battery_style();
}

static void reset_expression_motion_tracking(void)
{
    expression_accel_has_baseline = false;
    expression_pose_has_baseline = false;
    expression_bma_has_baseline = false;
    expression_last_motion_log_ms = 0;
    expression_last_accel_sequence = 0;
    expression_last_bma_impulse1_events = 0;
    expression_last_bma_impulse2_events = 0;
    expression_last_bma_impulse3_events = 0;
    expression_last_motion_events = 0;
    expression_last_tilt_events = 0;
    expression_last_accel_x = 0.0f;
    expression_last_accel_y = 0.0f;
    expression_last_accel_z = 0.0f;
    expression_last_roll = 0.0f;
    expression_last_pitch = 0.0f;
    expression_last_heading = 0.0f;
    expression_peak_accel_delta_sq = 0.0f;
    expression_peak_angle_delta = 0.0f;
    expression_imu_started_ms = lv_tick_get();
}

static void update_expression_interaction_timeout(void)
{
    if (expression_interaction != EXPRESSION_INTERACTION_NONE &&
            !expression_has_active_interaction()) {
        expression_interaction = EXPRESSION_INTERACTION_NONE;
        apply_expression_battery_style();
    }
}

static void stop_expression_sound(void)
{
    if (expression_sound_timer) {
        lv_timer_del(expression_sound_timer);
        expression_sound_timer = NULL;
    }
    expression_sound_note = 0;
    expression_sound_wait_started_ms = 0;
    expression_sound_wait_ms = 0;
}

static void expression_sound_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (expression_sound_wait_ms != 0 &&
            lv_tick_elaps(expression_sound_wait_started_ms) < expression_sound_wait_ms) {
        return;
    }

    expression_sound_wait_ms = 0;
    if (expression_sound_note >=
            sizeof(expression_shake_sound) / sizeof(expression_shake_sound[0])) {
        stop_expression_sound();
        return;
    }

#ifdef ARDUINO
    if (hw_player_running()) {
        return;
    }
#endif

    const ExpressionSoundNote &note = expression_shake_sound[expression_sound_note++];
    hw_audio_beep(note.frequency_hz, note.duration_ms);
    expression_sound_wait_started_ms = lv_tick_get();
    expression_sound_wait_ms = note.duration_ms + note.gap_ms + 20;
}

static void play_expression_shake_sound(void)
{
    stop_expression_sound();
    expression_sound_note = 0;
    expression_sound_timer = lv_timer_create(expression_sound_timer_cb, 25, NULL);
    if (expression_sound_timer) {
        lv_timer_ready(expression_sound_timer);
    }
}

static void trigger_expression_interaction(ExpressionInteraction interaction)
{
    if (!expression_overlay || !lv_obj_is_valid(expression_overlay)) {
        return;
    }

    if (interaction == EXPRESSION_INTERACTION_SHAKE &&
            expression_interaction == EXPRESSION_INTERACTION_SHAKE &&
            expression_has_active_interaction()) {
        return;
    }

    bool can_override_cooldown = (interaction == EXPRESSION_INTERACTION_SHAKE &&
                                  expression_interaction != EXPRESSION_INTERACTION_SHAKE) ||
                                 (interaction == EXPRESSION_INTERACTION_STARTLED &&
                                  expression_interaction == EXPRESSION_INTERACTION_SHY);
    if (expression_last_touch_ms != 0 &&
            lv_tick_elaps(expression_last_touch_ms) < WS2812_EXPRESSION_TOUCH_COOLDOWN_MS &&
            !can_override_cooldown) {
        return;
    }

    expression_interaction = interaction;
    expression_interaction_started_ms = lv_tick_get();
    expression_last_touch_ms = expression_interaction_started_ms;
    if (interaction == EXPRESSION_INTERACTION_SHAKE) {
        play_expression_shake_sound();
    }
    LILYGO_LOG_I("WS2812 expr interaction=%s hold=%lums",
                 expression_interaction_name(interaction),
                 (unsigned long)expression_interaction_hold_ms());
    apply_expression_battery_style();
    update_expression_face();
    update_expression_keyboard_backlight();
    refresh_output();
}

static void detect_expression_bma_touch(bool warmup, bool *touch,
                                        bool *strong_touch, bool *shake)
{
    bma_sensor_snapshot_t snapshot;
    hw_get_bma_sensor_snapshot(snapshot);
    if (!snapshot.valid) {
        return;
    }

    if (!expression_bma_has_baseline) {
        expression_bma_has_baseline = true;
    } else if (!warmup) {
        uint32_t impulse1_delta = counter_delta_u32(snapshot.single_taps,
                                                   expression_last_bma_impulse1_events);
        uint32_t impulse2_delta = counter_delta_u32(snapshot.double_taps,
                                                   expression_last_bma_impulse2_events);
        uint32_t impulse3_delta = counter_delta_u32(snapshot.triple_taps,
                                                   expression_last_bma_impulse3_events);
        uint32_t motion_delta = counter_delta_u32(snapshot.motion_events,
                                                 expression_last_motion_events);
        uint32_t tilt_delta = counter_delta_u32(snapshot.tilt_events,
                                               expression_last_tilt_events);
        if (impulse1_delta != 0 || impulse2_delta != 0 || impulse3_delta != 0) {
            *shake = true;
        } else if (motion_delta != 0 || tilt_delta != 0) {
            *touch = true;
        }
#if WS2812_EXPRESSION_IMU_DEBUG
        log_expression_bma_delta(snapshot, impulse1_delta, impulse2_delta, impulse3_delta,
                                 motion_delta, tilt_delta, *touch, *strong_touch, *shake);
#endif
    }

    expression_last_bma_impulse1_events = snapshot.single_taps;
    expression_last_bma_impulse2_events = snapshot.double_taps;
    expression_last_bma_impulse3_events = snapshot.triple_taps;
    expression_last_motion_events = snapshot.motion_events;
    expression_last_tilt_events = snapshot.tilt_events;
}

static void detect_expression_imu_touch(bool warmup, bool *touch,
                                        bool *strong_touch, bool *shake)
{
    imu_params_t imu;
    hw_get_imu_params(imu);
    float accel_delta_sq = 0.0f;
    float pose_delta = 0.0f;
    bool have_motion_sample = false;

    if (imu.accel_valid) {
        if (expression_accel_has_baseline &&
                imu.accel_sequence != expression_last_accel_sequence &&
                !warmup) {
            float dx = imu.accel_x - expression_last_accel_x;
            float dy = imu.accel_y - expression_last_accel_y;
            float dz = imu.accel_z - expression_last_accel_z;
            float delta_sq = dx * dx + dy * dy + dz * dz;
            accel_delta_sq = delta_sq;
            have_motion_sample = true;
            if (delta_sq > expression_peak_accel_delta_sq) {
                expression_peak_accel_delta_sq = delta_sq;
            }
            if (delta_sq >= WS2812_EXPRESSION_STARTLE_ACCEL_DELTA_SQ) {
                *strong_touch = true;
                *shake = true;
            } else if (delta_sq >= WS2812_EXPRESSION_SHAKE_ACCEL_DELTA_SQ) {
                *shake = true;
            } else if (delta_sq >= WS2812_EXPRESSION_TOUCH_ACCEL_DELTA_SQ) {
                *touch = true;
            }
        }

        expression_accel_has_baseline = true;
        expression_last_accel_x = imu.accel_x;
        expression_last_accel_y = imu.accel_y;
        expression_last_accel_z = imu.accel_z;
        expression_last_accel_sequence = imu.accel_sequence;
    }

    bool last_pose_empty = expression_last_roll == 0.0f &&
                           expression_last_pitch == 0.0f &&
                           expression_last_heading == 0.0f;
    bool current_pose_empty = imu.roll == 0.0f &&
                              imu.pitch == 0.0f &&
                              imu.heading == 0.0f;
    if (expression_pose_has_baseline && !warmup && !current_pose_empty &&
            !(last_pose_empty && !current_pose_empty)) {
        float delta = angle_delta_deg(imu.roll, expression_last_roll);
        delta = max_f(delta, angle_delta_deg(imu.pitch, expression_last_pitch));
        delta = max_f(delta, angle_delta_deg(imu.heading, expression_last_heading));
        pose_delta = delta;
        have_motion_sample = true;
        if (delta > expression_peak_angle_delta) {
            expression_peak_angle_delta = delta;
        }
        if (delta >= WS2812_EXPRESSION_STARTLE_ANGLE_DELTA) {
            *strong_touch = true;
        } else if (delta >= WS2812_EXPRESSION_TOUCH_ANGLE_DELTA) {
            *touch = true;
        }
    }

    expression_pose_has_baseline = true;
    expression_last_roll = imu.roll;
    expression_last_pitch = imu.pitch;
    expression_last_heading = imu.heading;

#if WS2812_EXPRESSION_IMU_DEBUG
    if (!warmup && have_motion_sample) {
        log_expression_imu_delta(accel_delta_sq,
                                 imu.accel_valid ? imu.accel_magnitude / 9.80665f : 0.0f,
                                 imu.accel_sequence,
                                 pose_delta, *touch, *strong_touch, *shake);
    }
#endif
}

static void expression_imu_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!expression_overlay || !lv_obj_is_valid(expression_overlay)) {
        return;
    }

    if (!expression_imu_registered) {
        start_expression_imu_process();
    }
    if (!expression_imu_registered) {
        return;
    }

    update_expression_interaction_timeout();

    bool warmup = lv_tick_elaps(expression_imu_started_ms) < WS2812_EXPRESSION_IMU_WARMUP_MS;
    bool touch = false;
    bool strong_touch = false;
    bool shake = false;
    detect_expression_imu_touch(warmup, &touch, &strong_touch, &shake);
    detect_expression_bma_touch(warmup, &touch, &strong_touch, &shake);

    if (shake) {
        trigger_expression_interaction(EXPRESSION_INTERACTION_SHAKE);
    } else if (strong_touch) {
        trigger_expression_interaction(EXPRESSION_INTERACTION_STARTLED);
    } else if (touch) {
        trigger_expression_interaction(EXPRESSION_INTERACTION_SHY);
    }
}

static bool expression_motion_sensor_online(void)
{
#if !defined(EXCLUDE_IMU)
    uint32_t online = hw_get_device_online();
#if defined(HW_BHI260AP_ONLINE)
    if (online & HW_BHI260AP_ONLINE) {
        return true;
    }
#endif
#if defined(HW_BMA_ONLINE)
    if (online & HW_BMA_ONLINE) {
        return true;
    }
#endif
#if defined(HW_QMI8658_ONLINE)
    if (online & HW_QMI8658_ONLINE) {
        return true;
    }
#endif
#endif
    return false;
}

static void start_expression_imu_process(void)
{
#if !defined(EXCLUDE_IMU)
    if (!expression_imu_registered && expression_motion_sensor_online()) {
        uint32_t online = hw_get_device_online();
        hw_register_imu_process();
        expression_imu_registered = true;
        reset_expression_motion_tracking();
        LILYGO_LOG_I("WS2812 expr imu monitor start online=0x%08lx poll=%lums warmup=%lums thr_sq(touch/shake/startle)=%.2f/%.2f/%.2f thr_angle(touch/startle)=%.1f/%.1f",
                     (unsigned long)online,
                     (unsigned long)WS2812_EXPRESSION_IMU_POLL_MS,
                     (unsigned long)WS2812_EXPRESSION_IMU_WARMUP_MS,
                     WS2812_EXPRESSION_TOUCH_ACCEL_DELTA_SQ,
                     WS2812_EXPRESSION_SHAKE_ACCEL_DELTA_SQ,
                     WS2812_EXPRESSION_STARTLE_ACCEL_DELTA_SQ,
                     WS2812_EXPRESSION_TOUCH_ANGLE_DELTA,
                     WS2812_EXPRESSION_STARTLE_ANGLE_DELTA);
    }
#endif
}

static void stop_expression_imu_process(void)
{
#if !defined(EXCLUDE_IMU)
    if (expression_imu_registered) {
        LILYGO_LOG_I("WS2812 expr imu monitor stop peak_sq=%.2f peak_g=%.2f peak_angle=%.1f",
                     expression_peak_accel_delta_sq,
                     expression_accel_delta_to_g(expression_peak_accel_delta_sq),
                     expression_peak_angle_delta);
        hw_unregister_imu_process();
        expression_imu_registered = false;
    }
#endif
    reset_expression_motion_tracking();
    expression_interaction = EXPRESSION_INTERACTION_NONE;
    expression_interaction_started_ms = 0;
    expression_last_touch_ms = 0;
}

static void destroy_expression_overlay(void)
{
    stop_expression_sound();

    if (expression_imu_timer) {
        lv_timer_del(expression_imu_timer);
        expression_imu_timer = NULL;
    }
    stop_expression_imu_process();

    if (expression_battery_timer) {
        lv_timer_del(expression_battery_timer);
        expression_battery_timer = NULL;
    }

    if (expression_anim_timer) {
        lv_timer_del(expression_anim_timer);
        expression_anim_timer = NULL;
    }

    if (!expression_overlay || !lv_obj_is_valid(expression_overlay)) {
        expression_overlay = NULL;
        expression_interaction = EXPRESSION_INTERACTION_NONE;
        reset_expression_motion_tracking();
        restore_expression_keyboard_backlight();
        return;
    }

    lv_obj_t *overlay = expression_overlay;
    expression_overlay = NULL;
    lv_obj_remove_event_cb(overlay, expression_overlay_event_cb);
    lv_group_t *group = (lv_group_t *)lv_obj_get_group(overlay);
    bool ignore_prev = ignore_encoder_activity;
    ignore_encoder_activity = true;
    if (group) {
        lv_group_remove_obj(overlay);
        if (expression_previous_focus && lv_obj_is_valid(expression_previous_focus) &&
                lv_obj_get_group(expression_previous_focus) == group) {
            lv_group_focus_obj(expression_previous_focus);
        }
    }
    lv_obj_delete(overlay);
    expression_face = NULL;
    expression_left_eye = NULL;
    expression_right_eye = NULL;
    expression_left_pupil = NULL;
    expression_right_pupil = NULL;
    expression_left_cheek = NULL;
    expression_right_cheek = NULL;
    expression_mouth = NULL;
    expression_sleep_label = NULL;
    expression_previous_focus = NULL;
    expression_blink_closed = 0;
    expression_interaction = EXPRESSION_INTERACTION_NONE;
    reset_expression_motion_tracking();
    restore_expression_keyboard_backlight();
    ignore_encoder_activity = ignore_prev;
}

static void reset_expression_timer(void)
{
    if (expression_timer) {
        lv_timer_reset(expression_timer);
        lv_timer_resume(expression_timer);
    }
}

static void mark_encoder_activity(void)
{
    destroy_expression_overlay();
    reset_expression_timer();
}

static void close_expression_overlay_async_cb(void *user_data)
{
    (void)user_data;
    expression_overlay_close_pending = false;
    mark_encoder_activity();
}

static uint8_t stepped_slider_value(lv_obj_t *slider, uint8_t current, uint8_t step)
{
    int32_t value = lv_slider_get_value(slider);
    if (use_encoder_slider_step() && value != current) {
        mark_encoder_activity();
        int32_t delta = value - current;
        value = current + delta * step;
        value = clamp_i32(value, lv_slider_get_min_value(slider),
                          lv_slider_get_max_value(slider));
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
    }
    return (uint8_t)value;
}

static void expression_overlay_event_cb(lv_event_t *e)
{
    if (ignore_encoder_activity) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        return;
    }
    if (event_from_encoder(e)) {
        if (!expression_overlay_close_pending) {
            expression_overlay_close_pending = true;
            lv_async_call(close_expression_overlay_async_cb, NULL);
        }
        reset_expression_timer();
    }
}

static int32_t interpolate_i32(int32_t from, int32_t to, uint32_t phase, uint32_t period)
{
    if (period == 0) {
        return to;
    }
    return from + (to - from) * (int32_t)phase / (int32_t)period;
}

static int32_t triangle_wave_i32(uint32_t elapsed, uint32_t period, int32_t amplitude)
{
    if (period == 0) {
        return 0;
    }

    uint32_t phase = elapsed % period;
    if (phase > period / 2) {
        phase = period - phase;
    }
    return (int32_t)(phase * 2 * amplitude / period);
}

static uint32_t expression_look_period_ms(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 420;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 460;
        }
        return 760;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 1700;
    case EXPRESSION_BATTERY_LOW:
        return 1500;
    case EXPRESSION_BATTERY_CHARGING:
        return 800;
    default:
        return WS2812_EXPRESSION_LOOK_MS;
    }
}

static uint32_t expression_blink_period_ms(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 1000;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 760;
        }
        return 820;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 2100;
    case EXPRESSION_BATTERY_LOW:
        return 2700;
    case EXPRESSION_BATTERY_FULL:
        return 4300;
    case EXPRESSION_BATTERY_CHARGING:
        return 1500;
    default:
        return WS2812_EXPRESSION_BLINK_PERIOD;
    }
}

static uint32_t expression_blink_duration_ms(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 130;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 120;
        }
        return 460;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 420;
    case EXPRESSION_BATTERY_LOW:
        return 340;
    case EXPRESSION_BATTERY_CHARGING:
        return 190;
    default:
        return WS2812_EXPRESSION_BLINK_MS;
    }
}

static uint32_t expression_bob_period_ms(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 520;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 560;
        }
        return 1300;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 3000;
    case EXPRESSION_BATTERY_LOW:
        return 2600;
    case EXPRESSION_BATTERY_CHARGING:
        return 1500;
    default:
        return WS2812_EXPRESSION_BOB_MS;
    }
}

static int32_t expression_bob_amplitude(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 20;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 22;
        }
        return 10;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 4;
    case EXPRESSION_BATTERY_LOW:
        return 6;
    case EXPRESSION_BATTERY_FULL:
        return 10;
    case EXPRESSION_BATTERY_CHARGING:
        return 12;
    default:
        return 8;
    }
}

static int32_t expression_eye_base_y(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return -24;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return -20;
        }
        return -6;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return -8;
    case EXPRESSION_BATTERY_LOW:
        return -12;
    case EXPRESSION_BATTERY_CHARGING:
        return -22;
    default:
        return -18;
    }
}

static int32_t expression_rest_eye_h(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return clamp_i32(expression_eye_h * 125 / 100, 28, expression_eye_h + 14);
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return clamp_i32(expression_eye_h * 112 / 100, 24, expression_eye_h + 9);
        }
        return clamp_i32(expression_eye_h * 45 / 100, 14, expression_eye_h);
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return clamp_i32(expression_eye_h * 55 / 100, 14, expression_eye_h);
    case EXPRESSION_BATTERY_LOW:
        return clamp_i32(expression_eye_h * 72 / 100, 18, expression_eye_h);
    case EXPRESSION_BATTERY_FULL:
        return clamp_i32(expression_eye_h * 105 / 100, 20, expression_eye_h + 4);
    case EXPRESSION_BATTERY_CHARGING:
        return clamp_i32(expression_eye_h * 92 / 100, 20, expression_eye_h);
    default:
        return expression_eye_h;
    }
}

static int32_t expression_min_eye_h(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 8;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 4;
        }
        return 5;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 5;
    case EXPRESSION_BATTERY_LOW:
        return 7;
    case EXPRESSION_BATTERY_CHARGING:
        return 6;
    default:
        return 8;
    }
}

static int32_t expression_look_y_bias(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return -5;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return -2;
        }
        return 9;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 4;
    case EXPRESSION_BATTERY_LOW:
        return 3;
    case EXPRESSION_BATTERY_CHARGING:
        return -3;
    default:
        return 0;
    }
}

static int32_t expression_float_amplitude(void)
{
    if (expression_has_active_interaction()) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            return 18;
        }
        if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            return 20;
        }
        return 12;
    }

    switch (expression_battery_level) {
    case EXPRESSION_BATTERY_CRITICAL:
        return 4;
    case EXPRESSION_BATTERY_LOW:
        return 6;
    case EXPRESSION_BATTERY_CHARGING:
        return 14;
    default:
        return 10;
    }
}

static uint8_t expression_closed_from_elapsed(uint32_t elapsed)
{
    uint32_t blink_period = expression_blink_period_ms();
    uint32_t blink_ms = expression_blink_duration_ms();
    uint32_t blink_half_ms = blink_ms / 2;
    if (blink_period == 0 || blink_half_ms == 0) {
        return 0;
    }

    uint32_t blink_phase = elapsed % blink_period;
    if (blink_phase < blink_half_ms) {
        return (uint8_t)(blink_phase * 100 / blink_half_ms);
    }
    if (blink_phase < blink_ms) {
        return (uint8_t)(100 - (blink_phase - blink_half_ms) * 100 / blink_half_ms);
    }
    return 0;
}

static void update_expression_keyboard_backlight(void)
{
    if (!keyboard_sync_enabled || !expression_overlay || !expression_kb_backlight_saved ||
            !hw_has_keyboard()) {
        return;
    }

    uint32_t elapsed = lv_tick_elaps(expression_started_ms);
    int32_t saved = expression_saved_kb_backlight;
    int32_t low = saved > 0 ? clamp_i32(saved / 3, 8, 96) : 6;
    int32_t high = clamp_i32(saved + 96, 84, 255);

    if (saved > 190) {
        low = clamp_i32(saved - 90, 40, 180);
        high = 255;
    }

    int32_t breath = triangle_wave_i32(elapsed + 250, WS2812_EXPRESSION_KB_BREATH_MS, 100);
    int32_t level = low + (high - low) * breath / 100;
    level += (high - level) * expression_blink_closed / 100;
    if (expression_has_active_interaction()) {
        uint32_t mood_period = 520;
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            mood_period = 240;
        } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            mood_period = 300;
        }
        int32_t mood_pulse = triangle_wave_i32(lv_tick_elaps(expression_interaction_started_ms),
                                               mood_period, 100);
        int32_t mood_level = low + (high - low) * mood_pulse / 100;
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED ||
                expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            mood_level = high;
        }
        if (mood_level > level) {
            level = mood_level;
        }
    }
    hw_set_kb_backlight((uint8_t)clamp_i32(level, 0, 255));
}

static void style_expression_blob(lv_obj_t *obj, lv_color_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_expression_eye(lv_obj_t *parent, lv_obj_t **eye, lv_obj_t **pupil)
{
    *eye = lv_obj_create(parent);
    lv_obj_set_size(*eye, expression_eye_w, expression_eye_h);
    lv_obj_set_style_radius(*eye, LV_RADIUS_CIRCLE, 0);
    style_expression_blob(*eye, lv_color_white(), LV_OPA_COVER);

    *pupil = lv_obj_create(*eye);
    lv_obj_set_size(*pupil, expression_pupil_size, expression_pupil_size);
    lv_obj_set_style_radius(*pupil, LV_RADIUS_CIRCLE, 0);
    style_expression_blob(*pupil, lv_color_hex(0x171A20), LV_OPA_COVER);
    lv_obj_center(*pupil);
}

static int32_t expression_current_eye_w(void)
{
    if (!expression_has_active_interaction()) {
        return expression_eye_w;
    }

    if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
        return clamp_i32(expression_eye_w * 120 / 100, expression_eye_w, expression_eye_w + 16);
    }
    if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
        return clamp_i32(expression_eye_w * 112 / 100, expression_eye_w, expression_eye_w + 12);
    }
    return clamp_i32(expression_eye_w * 82 / 100, 44, expression_eye_w);
}

static int32_t expression_current_pupil_size(void)
{
    if (!expression_has_active_interaction()) {
        return expression_pupil_size;
    }

    if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
        return clamp_i32(expression_pupil_size * 130 / 100,
                         expression_pupil_size, expression_pupil_size + 8);
    }
    if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
        return clamp_i32(expression_pupil_size * 112 / 100,
                         expression_pupil_size, expression_pupil_size + 5);
    }
    return clamp_i32(expression_pupil_size * 70 / 100, 8, expression_pupil_size);
}

static void update_expression_eye(lv_obj_t *eye, lv_obj_t *pupil,
                                  int32_t center_x, int32_t center_y,
                                  int32_t look_x, int32_t look_y,
                                  int32_t eye_h)
{
    if (!eye || !pupil) {
        return;
    }

    int32_t eye_w = expression_current_eye_w();
    int32_t pupil_size = expression_current_pupil_size();

    lv_obj_set_size(eye, eye_w, eye_h);
    lv_obj_set_style_radius(eye, eye_h / 2, 0);
    lv_obj_align(eye, LV_ALIGN_CENTER, center_x, center_y);

    int32_t pupil_h = clamp_i32(eye_h - 10, 2, pupil_size);
    int32_t max_x = (eye_w - pupil_size) / 2 - 4;
    int32_t max_y = (eye_h - pupil_h) / 2 - 3;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    lv_obj_set_size(pupil, pupil_size, pupil_h);
    lv_obj_set_style_radius(pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(pupil, LV_ALIGN_CENTER,
                 clamp_i32(look_x, -max_x, max_x),
                 clamp_i32(look_y, -max_y, max_y));
}

static void update_expression_face(void)
{
    if (!expression_overlay || !expression_left_eye || !expression_right_eye) {
        return;
    }

    static const int8_t look_x_table[] = {0, -12, -16, 8, 14, 0};
    static const int8_t look_y_table[] = {0, -4, 5, 0, -3, 4};
    static const uint8_t look_count = sizeof(look_x_table) / sizeof(look_x_table[0]);

    uint32_t elapsed = lv_tick_elaps(expression_started_ms);
    bool interacting = expression_has_active_interaction();
    uint32_t interaction_elapsed = interacting ? lv_tick_elaps(expression_interaction_started_ms) : 0;
    if (expression_face) {
        int32_t face_x = 0;
        int32_t face_y = 0;
        if (interacting) {
            if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
                face_x = ((interaction_elapsed / 70) % 2) ? 6 : -6;
                face_y = -8;
            } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
                face_y = -triangle_wave_i32(interaction_elapsed, 420, 24);
            } else {
                face_y = 12;
            }
        }
        lv_obj_align(expression_face, LV_ALIGN_CENTER, face_x, face_y);
    }

    uint32_t look_period = expression_look_period_ms();
    uint32_t slot = (elapsed / look_period) % look_count;
    uint32_t next = (slot + 1) % look_count;
    uint32_t look_phase = elapsed % look_period;
    int32_t look_x = interpolate_i32(look_x_table[slot], look_x_table[next],
                                     look_phase, look_period);
    int32_t look_y = interpolate_i32(look_y_table[slot], look_y_table[next],
                                     look_phase, look_period) + expression_look_y_bias();
    int32_t left_look_x = look_x;
    int32_t right_look_x = look_x;
    if (interacting) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            left_look_x = 0;
            right_look_x = 0;
            look_y = -2;
        } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            int32_t sparkle = triangle_wave_i32(interaction_elapsed, 360, 12) - 6;
            left_look_x = sparkle;
            right_look_x = -sparkle;
            look_y = -4;
        } else {
            int32_t inward = clamp_i32(expression_eye_w / 5, 8, 18);
            left_look_x += inward;
            right_look_x -= inward;
        }
    }

    expression_blink_closed = expression_closed_from_elapsed(elapsed);

    int32_t rest_eye_h = expression_rest_eye_h();
    int32_t min_eye_h = clamp_i32(expression_min_eye_h(), 2, rest_eye_h);
    int32_t eye_h = rest_eye_h - (rest_eye_h - min_eye_h) * expression_blink_closed / 100;
    int32_t left_eye_h = eye_h;
    int32_t right_eye_h = eye_h;
    if (interacting && expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
        uint32_t wink_phase = interaction_elapsed % 620;
        if (wink_phase < 150) {
            left_eye_h = min_eye_h;
        } else if (wink_phase >= 260 && wink_phase < 410) {
            right_eye_h = min_eye_h;
        }
    }
    int32_t bob_amp = expression_bob_amplitude();
    int32_t bob = triangle_wave_i32(elapsed, expression_bob_period_ms(), bob_amp) - bob_amp / 2;
    int32_t eye_y = expression_eye_base_y() + bob;
    int32_t eye_w = expression_current_eye_w();
    int32_t eye_x = eye_w / 2 + expression_eye_gap / 2;

    update_expression_eye(expression_left_eye, expression_left_pupil,
                          -eye_x, eye_y, left_look_x, look_y, left_eye_h);
    update_expression_eye(expression_right_eye, expression_right_pupil,
                          eye_x, eye_y, right_look_x, look_y, right_eye_h);

    int32_t cheek_offset = interacting && expression_interaction == EXPRESSION_INTERACTION_SHY ? 8 : 18;
    int32_t mouth_offset = 24;
    if (interacting) {
        if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
            mouth_offset = 32;
        } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
            cheek_offset = 10;
            mouth_offset = 18;
        } else {
            mouth_offset = 16;
        }
    }
    if (expression_left_cheek) {
        lv_obj_align(expression_left_cheek, LV_ALIGN_CENTER,
                     -eye_x, eye_y + expression_eye_h / 2 + cheek_offset);
    }
    if (expression_right_cheek) {
        lv_obj_align(expression_right_cheek, LV_ALIGN_CENTER,
                     eye_x, eye_y + expression_eye_h / 2 + cheek_offset);
    }
    if (expression_mouth) {
        lv_obj_align(expression_mouth, LV_ALIGN_CENTER, 0, eye_y + expression_eye_h / 2 + mouth_offset);
    }
    if (expression_sleep_label) {
        int32_t z_lift = triangle_wave_i32(elapsed + 700, 1800, expression_float_amplitude());
        lv_obj_align(expression_sleep_label, LV_ALIGN_CENTER,
                     eye_x + eye_w / 2 + 20,
                     eye_y - expression_eye_h / 2 + 8 - z_lift);
    }
}

static void expression_anim_timer_cb(lv_timer_t *t)
{
    (void)t;
    update_expression_interaction_timeout();
    update_expression_face();
    refresh_output();
    update_expression_keyboard_backlight();
}

static void expression_battery_timer_cb(lv_timer_t *t)
{
    (void)t;
    poll_expression_battery();
    update_expression_face();
}

static void show_expression_overlay(void)
{
    if (expression_overlay || !page_container || !lv_obj_is_valid(page_container)) {
        return;
    }

    lv_obj_t *root = (lv_obj_t *)lv_obj_get_user_data(page_container);
    if (!root || !lv_obj_is_valid(root)) {
        return;
    }

    expression_overlay = lv_obj_create(root);
    lv_obj_set_size(expression_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_align(expression_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(expression_overlay, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(expression_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(expression_overlay, 0, 0);
    lv_obj_set_style_radius(expression_overlay, 0, 0);
    lv_obj_set_style_pad_all(expression_overlay, 0, 0);
    lv_obj_remove_flag(expression_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(expression_overlay, expression_overlay_event_cb, LV_EVENT_ALL, NULL);

    int32_t display_w = lv_display_get_horizontal_resolution(NULL);
    int32_t display_h = lv_display_get_vertical_resolution(NULL);
    int32_t face_w = clamp_i32(display_w - 24, 180, 340);
    int32_t face_h = clamp_i32(display_h - 40, 120, 190);
    expression_eye_w = clamp_i32(face_w / 3, 54, 98);
    expression_eye_h = clamp_i32(face_h * 2 / 5, 34, 68);
    expression_eye_gap = clamp_i32(face_w / 10, 18, 34);
    expression_pupil_size = clamp_i32(expression_eye_h / 2, 14, 28);

    expression_face = lv_obj_create(expression_overlay);
    lv_obj_set_size(expression_face, face_w, face_h);
    lv_obj_center(expression_face);
    lv_obj_set_style_bg_opa(expression_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(expression_face, 0, 0);
    lv_obj_set_style_pad_all(expression_face, 0, 0);
    lv_obj_remove_flag(expression_face, LV_OBJ_FLAG_SCROLLABLE);

    create_expression_eye(expression_face, &expression_left_eye, &expression_left_pupil);
    create_expression_eye(expression_face, &expression_right_eye, &expression_right_pupil);

    expression_left_cheek = lv_obj_create(expression_face);
    lv_obj_set_size(expression_left_cheek, expression_eye_w / 2, 8);
    lv_obj_set_style_radius(expression_left_cheek, LV_RADIUS_CIRCLE, 0);
    style_expression_blob(expression_left_cheek, lv_color_hex(0xFF7DA8), LV_OPA_60);

    expression_right_cheek = lv_obj_create(expression_face);
    lv_obj_set_size(expression_right_cheek, expression_eye_w / 2, 8);
    lv_obj_set_style_radius(expression_right_cheek, LV_RADIUS_CIRCLE, 0);
    style_expression_blob(expression_right_cheek, lv_color_hex(0xFF7DA8), LV_OPA_60);

    expression_mouth = lv_label_create(expression_face);
    lv_label_set_text(expression_mouth, "w");
    lv_obj_set_style_text_color(expression_mouth, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(expression_mouth, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(expression_mouth, LV_TEXT_ALIGN_CENTER, 0);

    expression_sleep_label = lv_label_create(expression_face);
    lv_label_set_text(expression_sleep_label, "zZ");
    lv_obj_set_style_text_color(expression_sleep_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(expression_sleep_label, &lv_font_montserrat_16, 0);

    expression_started_ms = lv_tick_get();
    expression_interaction = EXPRESSION_INTERACTION_NONE;
    expression_interaction_started_ms = 0;
    expression_last_touch_ms = 0;
    reset_expression_motion_tracking();
    apply_expression_battery_style();
    poll_expression_battery();
    save_expression_keyboard_backlight();
    update_expression_face();
    update_expression_keyboard_backlight();
    expression_anim_timer = lv_timer_create(expression_anim_timer_cb, WS2812_EXPRESSION_ANIM_MS, NULL);
    expression_battery_timer = lv_timer_create(expression_battery_timer_cb,
                                               WS2812_EXPRESSION_BATTERY_POLL_MS,
                                               NULL);
    expression_imu_timer = lv_timer_create(expression_imu_timer_cb,
                                           WS2812_EXPRESSION_IMU_POLL_MS,
                                           NULL);
    if (expression_imu_timer) {
        lv_timer_ready(expression_imu_timer);
    }

    lv_group_t *group = lv_group_get_default();
    if (group) {
        bool ignore_prev = ignore_encoder_activity;
        ignore_encoder_activity = true;
        expression_previous_focus = lv_group_get_focused(group);
        lv_group_set_editing(group, false);
        lv_group_add_obj(group, expression_overlay);
        lv_group_focus_obj(expression_overlay);
        ignore_encoder_activity = ignore_prev;
    }
    if (expression_timer) {
        lv_timer_pause(expression_timer);
    }
}

static void expression_timer_cb(lv_timer_t *t)
{
    (void)t;
    show_expression_overlay();
}

static void page_encoder_activity_cb(lv_event_t *e)
{
    if (ignore_encoder_activity) {
        return;
    }
    if (event_from_encoder(e)) {
        mark_encoder_activity();
    }
}

static lv_obj_tree_walk_res_t add_encoder_activity_bubble_cb(lv_obj_t *obj, void *user_data)
{
    lv_obj_t *content = (lv_obj_t *)user_data;
    if (obj != content) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static void start_expression_idle_timer(void)
{
    if (!page_container) {
        return;
    }

    lv_obj_add_event_cb(page_container, page_encoder_activity_cb, LV_EVENT_ALL, NULL);
    lv_obj_tree_walk(page_container, add_encoder_activity_bubble_cb, page_container);

    if (!expression_timer) {
        expression_timer = lv_timer_create(expression_timer_cb, WS2812_EXPRESSION_IDLE_MS, NULL);
    }
    if (expression_timer) {
        lv_timer_set_period(expression_timer, WS2812_EXPRESSION_IDLE_MS);
        reset_expression_timer();
    }
}

static void stop_expression_idle_timer(void)
{
    if (expression_overlay_close_pending) {
        lv_async_call_cancel(close_expression_overlay_async_cb, NULL);
        expression_overlay_close_pending = false;
    }
    if (expression_timer) {
        lv_timer_del(expression_timer);
        expression_timer = NULL;
    }
    destroy_expression_overlay();
    if (page_container && lv_obj_is_valid(page_container)) {
        lv_obj_remove_event_cb(page_container, page_encoder_activity_cb);
    }
}

static uint32_t rgb_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t color_wheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85) {
        return rgb_color(255 - pos * 3, 0, pos * 3);
    }
    if (pos < 170) {
        pos -= 85;
        return rgb_color(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return rgb_color(pos * 3, 255 - pos * 3, 0);
}

static uint32_t scale_color(uint32_t color, uint8_t amount)
{
    uint8_t r = ((color >> 16) & 0xFF) * amount / 255;
    uint8_t g = ((color >> 8) & 0xFF) * amount / 255;
    uint8_t b = (color & 0xFF) * amount / 255;
    return rgb_color(r, g, b);
}

static uint32_t blend_color(uint32_t from, uint32_t to, uint8_t amount)
{
    if (amount == 0) return from;
    if (amount == 255) return to;

    int32_t from_r = (from >> 16) & 0xFF;
    int32_t from_g = (from >> 8) & 0xFF;
    int32_t from_b = from & 0xFF;
    int32_t to_r = (to >> 16) & 0xFF;
    int32_t to_g = (to >> 8) & 0xFF;
    int32_t to_b = to & 0xFF;

    uint8_t r = (uint8_t)(from_r + (to_r - from_r) * amount / 255);
    uint8_t g = (uint8_t)(from_g + (to_g - from_g) * amount / 255);
    uint8_t b = (uint8_t)(from_b + (to_b - from_b) * amount / 255);
    return rgb_color(r, g, b);
}

static uint32_t expression_blink_led_color(uint32_t color, uint8_t index)
{
    if (!expression_overlay || effect_mode == WS2812_EFFECT_OFF || expression_blink_closed == 0) {
        return color;
    }

    int32_t dist = (int32_t)index * 2 - (int32_t)(WS2812_LED_COUNT - 1);
    if (dist < 0) {
        dist = -dist;
    }

    uint8_t base_scale = (uint8_t)clamp_i32(255 - expression_blink_closed * (80 + dist * 34) / 100,
                                           8, 255);
    uint8_t accent_level = (uint8_t)clamp_i32(255 - dist * 42, 0, 255);
    uint8_t accent_mix = (uint8_t)(expression_blink_closed * accent_level / 100);
    uint32_t accent = scale_color(expression_battery_accent_hex(), accent_level);

    return blend_color(scale_color(color, base_scale), accent, accent_mix);
}

static void apply_expression_blink_to_leds(void)
{
    if (!expression_overlay || effect_mode == WS2812_EFFECT_OFF || expression_blink_closed == 0) {
        return;
    }

    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        left_colors[i] = expression_blink_led_color(left_colors[i], i);
        right_colors[i] = expression_blink_led_color(right_colors[i], i);
    }
}

static void apply_expression_interaction_to_leds(void)
{
    if (!expression_overlay || effect_mode == WS2812_EFFECT_OFF ||
            !expression_has_active_interaction()) {
        return;
    }

    uint32_t elapsed = lv_tick_elaps(expression_interaction_started_ms);
    uint32_t period = 720;
    if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
        period = 320;
    } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
        period = 260;
    }
    int32_t pulse = triangle_wave_i32(elapsed, period, 100);
    uint8_t base_mix = 95;
    uint8_t peak_mix = 180;
    if (expression_interaction == EXPRESSION_INTERACTION_STARTLED) {
        base_mix = 170;
        peak_mix = 255;
    } else if (expression_interaction == EXPRESSION_INTERACTION_SHAKE) {
        base_mix = 140;
        peak_mix = 255;
    }
    uint8_t mix = (uint8_t)(base_mix + (peak_mix - base_mix) * pulse / 100);
    uint32_t accent = expression_battery_accent_hex();

    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        uint8_t wave = (uint8_t)clamp_i32(255 - i * 20, 120, 255);
        uint8_t amount = (uint8_t)((uint16_t)mix * wave / 255);
        left_colors[i] = blend_color(left_colors[i], accent, amount);
        right_colors[WS2812_LED_COUNT - 1 - i] =
            blend_color(right_colors[WS2812_LED_COUNT - 1 - i], accent, amount);
    }
}

static void set_all_colors(uint32_t color)
{
    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        left_colors[i] = left_enabled ? color : 0;
        right_colors[i] = right_enabled ? color : 0;
    }
}

static void set_pixel_pair(uint8_t index, uint32_t color)
{
    if (index >= WS2812_LED_COUNT) {
        return;
    }
    left_colors[index] = left_enabled ? color : 0;
    uint8_t right_index = mirror_right ? (WS2812_LED_COUNT - 1 - index) : index;
    right_colors[right_index] = right_enabled ? color : 0;
}

static void clear_colors(void)
{
    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        left_colors[i] = 0;
        right_colors[i] = 0;
    }
}

static void render_effect(void)
{
    clear_colors();
    uint32_t base = color_wheel(hue);
    uint8_t phase8 = (uint8_t)phase;

    switch (effect_mode) {
    case WS2812_EFFECT_OFF:
        break;
    case WS2812_EFFECT_SOLID:
        set_all_colors(base);
        break;
    case WS2812_EFFECT_BREATHING: {
        uint16_t breath = phase % 512;
        if (breath > 255) {
            breath = 511 - breath;
        }
        set_all_colors(scale_color(base, clamp_u8(32 + breath * 223 / 255)));
        break;
    }
    case WS2812_EFFECT_RAINBOW:
        for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
            set_pixel_pair(i, color_wheel(phase8 + i * 256 / WS2812_LED_COUNT));
        }
        break;
    case WS2812_EFFECT_COLOR_WIPE: {
        uint8_t lit = (phase / 24) % (WS2812_LED_COUNT + 1);
        for (uint8_t i = 0; i < lit; ++i) {
            set_pixel_pair(i, base);
        }
        break;
    }
    case WS2812_EFFECT_THEATER:
        for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
            if (((i + phase / 8) % 3) == 0) {
                set_pixel_pair(i, base);
            }
        }
        break;
    case WS2812_EFFECT_COMET: {
        uint8_t head = (phase / 12) % WS2812_LED_COUNT;
        for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
            uint8_t distance = (head + WS2812_LED_COUNT - i) % WS2812_LED_COUNT;
            uint8_t tail = distance == 0 ? 255 : (distance == 1 ? 120 : (distance == 2 ? 40 : 0));
            if (tail) {
                set_pixel_pair(i, scale_color(base, tail));
            }
        }
        break;
    }
    case WS2812_EFFECT_SCANNER: {
        uint8_t span = (WS2812_LED_COUNT - 1) * 2;
        uint8_t pos = (phase / 12) % span;
        if (pos >= WS2812_LED_COUNT) {
            pos = span - pos;
        }
        for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
            uint8_t distance = i > pos ? i - pos : pos - i;
            uint8_t level = distance == 0 ? 255 : (distance == 1 ? 80 : 0);
            if (level) {
                set_pixel_pair(i, scale_color(base, level));
            }
        }
        break;
    }
    case WS2812_EFFECT_SPARKLE:
        set_all_colors(scale_color(base, 20));
#if defined(ARDUINO)
        set_pixel_pair(random(WS2812_LED_COUNT), base);
#else
        set_pixel_pair((phase / 16) % WS2812_LED_COUNT, base);
#endif
        break;
    default:
        set_all_colors(base);
        break;
    }

    apply_expression_blink_to_leds();
    apply_expression_interaction_to_leds();

    if (!left_enabled) {
        for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
            left_colors[i] = 0;
        }
    }
    if (!right_enabled) {
        for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
            right_colors[i] = 0;
        }
    }
}

static lv_color_t preview_color(uint32_t color)
{
    uint32_t scaled = scale_color(color, brightness);
    return lv_color_hex(scaled);
}

static void update_preview(void)
{
    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        if (left_preview[i]) {
            lv_obj_set_style_bg_color(left_preview[i], preview_color(left_colors[i]), 0);
        }
        if (right_preview[i]) {
            lv_obj_set_style_bg_color(right_preview[i], preview_color(right_colors[i]), 0);
        }
    }
}

static void update_status(void)
{
    if (!status_value_label) {
        return;
    }
    lv_label_set_text_fmt(status_value_label, "%s  B:%u  S:%u",
                          effect_names[effect_mode], brightness, speed);
}

#if UI_WS2812_ENABLED
static void apply_to_strip(void)
{
    left_strip.setBrightness(brightness);
    right_strip.setBrightness(brightness);
    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        left_strip.setPixelColor(i, left_colors[i]);
        right_strip.setPixelColor(i, right_colors[i]);
    }
    left_strip.show();
    right_strip.show();
}
#endif

static void refresh_output(void)
{
    render_effect();
#if UI_WS2812_ENABLED
    apply_to_strip();
#endif
    update_preview();
    update_status();
}

static void effect_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t advance = (speed + 19) / 20;
    if (advance == 0) {
        advance = 1;
    }
    phase += advance;
    refresh_output();
}

static void stop_output(void)
{
    stop_expression_imu_process();

    if (effect_timer) {
        lv_timer_del(effect_timer);
        effect_timer = NULL;
    }
    clear_colors();
#if UI_WS2812_ENABLED
    left_strip.clear();
    right_strip.clear();
    left_strip.show();
    right_strip.show();
    pinMode(LILYGO_LORA_PAGER_WS2812_LEFT_PIN, INPUT);
    pinMode(LILYGO_LORA_PAGER_WS2812_RIGHT_PIN, INPUT);
#endif
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    stop_output();
    stop_expression_idle_timer();
    set_low_power_mode_flag(true);
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    status_value_label = NULL;
    memset(left_preview, 0, sizeof(left_preview));
    memset(right_preview, 0, sizeof(right_preview));
    menu_show();
}

static void effect_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(obj);
    if (selected >= WS2812_EFFECT_COUNT) {
        selected = WS2812_EFFECT_OFF;
    }
    effect_mode = selected;
    phase = 0;
    refresh_output();
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    brightness = stepped_slider_value(obj, brightness, WS2812_ENCODER_BRIGHTNESS_STEP);
    refresh_output();
}

static void speed_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    speed = stepped_slider_value(obj, speed, WS2812_ENCODER_SPEED_STEP);
}

static void hue_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    hue = stepped_slider_value(obj, hue, WS2812_ENCODER_HUE_STEP);
    refresh_output();
}

static void left_enable_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    left_enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    refresh_output();
}

static void right_enable_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    right_enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    refresh_output();
}

static void mirror_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    mirror_right = lv_obj_has_state(obj, LV_STATE_CHECKED);
    refresh_output();
}

static void keyboard_sync_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    keyboard_sync_enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);

    if (!keyboard_sync_enabled) {
        restore_expression_keyboard_backlight();
        return;
    }

    if (expression_overlay) {
        save_expression_keyboard_backlight();
        update_expression_keyboard_backlight();
    }
}

static lv_obj_t *create_led_preview_row(lv_obj_t *parent, const char *name, lv_obj_t **pixels)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_obj_set_width(label, 48);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);

    for (uint8_t i = 0; i < WS2812_LED_COUNT; ++i) {
        lv_obj_t *pixel = lv_obj_create(row);
        lv_obj_set_size(pixel, 22, 22);
        lv_obj_set_style_radius(pixel, 5, 0);
        lv_obj_set_style_bg_color(pixel, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(pixel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pixel, 1, 0);
        lv_obj_set_style_border_color(pixel, UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_pad_all(pixel, 0, 0);
        lv_obj_remove_flag(pixel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(pixel, LV_OBJ_FLAG_CLICKABLE);
        pixels[i] = pixel;
    }

    return row;
}

void ui_ws2812_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "LED Strip", back_event_handler);
    set_low_power_mode_flag(false);

    if (hw_has_lora_hardware()) {
        lv_obj_t *card = ui_create_card(page_container, "Unavailable");
        ui_create_card_info(card, LV_SYMBOL_WARNING, "Status", "LoRa detected");
        return;
    }

#if !UI_WS2812_ENABLED
    lv_obj_t *card = ui_create_card(page_container, "Unavailable");
    ui_create_card_info(card, LV_SYMBOL_WARNING, "Status", "NeoPixel disabled");
    return;
#else
    left_strip.begin();
    right_strip.begin();
    phase = 0;

    lv_obj_t *card = ui_create_card(page_container, "Preview");
    create_led_preview_row(card, "Left", left_preview);
    create_led_preview_row(card, "Right", right_preview);
    lv_obj_t *status_row = ui_create_card_info(card, LV_SYMBOL_EYE_OPEN, "Status", "");
    status_value_label = lv_obj_get_child(status_row, lv_obj_get_child_count(status_row) - 1);

    card = ui_create_card(page_container, "Effect");
    ui_create_card_dropdown(card, LV_SYMBOL_SETTINGS, "Mode", effect_options, effect_mode, effect_cb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Brightness", 1, 255, brightness, brightness_cb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Speed", 1, 255, speed, speed_cb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Hue", 0, 255, hue, hue_cb);

    card = ui_create_card(page_container, "Output");
    ui_create_card_switch(card, LV_SYMBOL_SETTINGS, "Left", left_enabled, left_enable_cb);
    ui_create_card_switch(card, LV_SYMBOL_SETTINGS, "Right", right_enabled, right_enable_cb);
    ui_create_card_switch(card, LV_SYMBOL_SETTINGS, "Mirror Right", mirror_right, mirror_cb);
    if (hw_has_keyboard()) {
        ui_create_card_switch(card, LV_SYMBOL_SETTINGS, "Keyboard Sync",
                              keyboard_sync_enabled, keyboard_sync_cb);
    }

    char pin_info[32];
    card = ui_create_card(page_container, "Pins");
    snprintf(pin_info, sizeof(pin_info), "GPIO%u x%u",
             LILYGO_LORA_PAGER_WS2812_LEFT_PIN, LILYGO_LORA_PAGER_WS2812_LEFT_COUNT);
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Left", pin_info);
    snprintf(pin_info, sizeof(pin_info), "GPIO%u x%u",
             LILYGO_LORA_PAGER_WS2812_RIGHT_PIN, LILYGO_LORA_PAGER_WS2812_RIGHT_COUNT);
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Right", pin_info);
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Format", "WS2812B GRB");

    refresh_output();
    effect_timer = lv_timer_create(effect_timer_cb, 40, NULL);
    start_expression_idle_timer();
#endif
}

void ui_ws2812_exit(lv_obj_t *parent)
{
    (void)parent;
    stop_output();
    stop_expression_idle_timer();
}

app_t ui_ws2812_main = {
    .setup_func_cb = ui_ws2812_enter,
    .exit_func_cb = ui_ws2812_exit,
    .user_data = nullptr,
};

#endif /* ARDUINO_T_LORA_PAGER */
