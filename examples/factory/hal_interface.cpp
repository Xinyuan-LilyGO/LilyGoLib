/**
 * @file      hal_interface.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 */
#include <LilyGoLog.h>
#include "hal_interface.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <lvgl.h>
#include <string.h>
#include <strings.h>

#ifdef ARDUINO
#include <sys/time.h>

extern "C" void lilygo_power_menu_request(void);
#endif

#define NVS_NAME    "pager"
#define APP_SETTINGS_KEY "settings"
#define ROTARY_STEP_DIVIDER_MIN 1
#define ROTARY_STEP_DIVIDER_MAX 4
static constexpr uint32_t APP_SETTINGS_MAGIC = 0x53455431; // SET1
static constexpr uint16_t APP_SETTINGS_VERSION = 1;
static constexpr uint8_t HAPTIC_EFFECT_MIN = 1;
static constexpr uint8_t HAPTIC_EFFECT_MAX = 117;
static constexpr uint8_t HAPTIC_EFFECT_DEFAULT = 1;
static constexpr uint8_t KEYBOARD_NAV_ENABLED = 1;
static constexpr uint8_t KEYBOARD_NAV_DISABLED = 2;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    user_setting_params_t user;
    int32_t timezone_offset_sec;
    int32_t daylight_offset_sec;
    uint8_t nav_auto_hide_enabled;
    uint8_t rotary_step_divider;
    uint8_t audio_jack_mode;
    uint8_t mic_input_source;
    uint8_t haptic_effect;
    uint8_t touch_guide_dismissed;
    uint8_t keyboard_navigation_mode;
    // Keep the blob size stable when adding small persisted settings later.
    uint8_t reserved[59];
} app_settings_storage_t;

static user_setting_params_t user_setting;
static app_settings_storage_t app_settings;
static int32_t timezone_offset_sec = GMT_OFFSET_SECOND;
static int32_t daylight_offset_sec = 0;
static bool nav_auto_hide_enabled = false;
static uint8_t rotary_step_divider = 1;
static uint8_t audio_jack_mode = AUDIO_JACK_MODE_CTIA;
static uint8_t mic_input_source = MIC_INPUT_SOURCE_INTERNAL;
static uint8_t haptic_effect = HAPTIC_EFFECT_DEFAULT;
static bool touch_guide_dismissed = false;
static bool keyboard_navigation_enabled = true;

#ifndef ARDUINO
/* Desktop builds use deterministic values so screenshots and UI reviews are repeatable. */
static bool apply_mic_input_source(uint8_t source)
{
    mic_input_source = source == MIC_INPUT_SOURCE_JACK ? MIC_INPUT_SOURCE_JACK : MIC_INPUT_SOURCE_INTERNAL;
    return true;
}

static void appendDefaultNetworkStreams(vector < NetworkAudioStreamParams_t > &list)
{
    static const NetworkAudioStreamParams_t streams[] = {
        {"Demo Jazz", "https://example.invalid/demo-jazz"},
        {"Demo News", "https://example.invalid/demo-news"},
    };
    for (const auto &stream : streams) {
        list.push_back(stream);
    }
}

static void appendAudioFile(vector < AudioParams_t > &list, audio_source_type_t source_type, const char *file_name)
{
    AudioParams_t item = {};
    item.source_type = source_type;
    snprintf(item.file_name, sizeof(item.file_name), "%s", file_name ? file_name : "");
    list.push_back(item);
}
#endif

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static uint8_t clamp_rotary_step_divider(uint8_t divider)
{
    uint8_t min_divider = hw_get_rotary_step_divider_min();
    uint8_t max_divider = hw_get_rotary_step_divider_max();
    if (max_divider < min_divider) {
        max_divider = min_divider;
    }
    if (divider < min_divider) {
        return min_divider;
    }
    if (divider > max_divider) {
        return max_divider;
    }
    return divider;
}

static uint8_t clamp_audio_jack_mode(uint8_t mode)
{
    return mode == AUDIO_JACK_MODE_OMTP ? AUDIO_JACK_MODE_OMTP : AUDIO_JACK_MODE_CTIA;
}

static uint8_t clamp_mic_input_source(uint8_t source)
{
    return source == MIC_INPUT_SOURCE_JACK ? MIC_INPUT_SOURCE_JACK : MIC_INPUT_SOURCE_INTERNAL;
}

static uint8_t clamp_haptic_effect(uint8_t effect)
{
    if (effect < HAPTIC_EFFECT_MIN || effect > HAPTIC_EFFECT_MAX) {
        return HAPTIC_EFFECT_DEFAULT;
    }
    return effect;
}

typedef struct {
    uint16_t max_brightness_level;
    uint16_t min_brightness_level;
    uint16_t max_charge_current_ma;
    uint16_t min_charge_current_ma;
    uint16_t charge_current_step_ma;
    uint16_t max_charge_level_index;
} device_limits_t;

#ifdef ARDUINO

#include "Esp.h"
#include <LilyGoLib.h>
#include <esp_mac.h>
#include <WiFi.h>
#include <SD.h>
#include <cbuf.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include "driver/rtc_io.h"
#include <FFat.h>

static Preferences           prefs;
static TaskHandle_t          recTaskHandle;
static TaskHandle_t          playerTaskHandler = NULL;
static QueueHandle_t         playerQueue  = NULL;
static EventGroupHandle_t    playerEvent = NULL;
static volatile bool         pps_trigger = false;

extern void instanceLockTake();
extern void instanceLockGive();

class InstanceLockGuard
{
public:
    InstanceLockGuard()
    {
        instanceLockTake();
    }

    ~InstanceLockGuard()
    {
        instanceLockGive();
    }
};

static uint8_t get_default_rotary_step_divider()
{
#if defined(ARDUINO_T_LORA_PAGER) && defined(USING_INPUT_DEV_ROTARY)
    return clamp_rotary_step_divider(instance.getRotaryStepDivider());
#else
    return hw_get_rotary_step_divider_min();
#endif
}

static uint8_t apply_rotary_step_divider(uint8_t divider)
{
    divider = clamp_rotary_step_divider(divider);
    rotary_step_divider = divider;

#if defined(ARDUINO_T_LORA_PAGER) && defined(USING_INPUT_DEV_ROTARY)
    if (instance.hasEncoder()) {
        instance.setRotaryStepDivider(divider);
    }
#else
#endif
    return divider;
}

static uint8_t apply_audio_jack_mode(uint8_t mode)
{
    mode = clamp_audio_jack_mode(mode);
    audio_jack_mode = mode;

#if defined(ARDUINO) && defined(T_DECK_V2_REV07) && defined(EXPANDS_AUDIO_JACK_SEL)
    instance.expander.digitalWrite(EXPANDS_AUDIO_JACK_SEL, mode == AUDIO_JACK_MODE_OMTP ? HIGH : LOW);
#endif
    return mode;
}

static bool apply_mic_input_source(uint8_t source)
{
    source = clamp_mic_input_source(source);
    mic_input_source = source;

#if defined(ARDUINO) && defined(T_DECK_V2_REV07) && defined(USING_AUDIO_CODEC)
    const uint8_t es8388_adc_control1 = 0x09;
    const uint8_t es8388_adc_control2 = 0x0A;
    const uint8_t es8388_adc_control4 = 0x0C;
    const uint8_t pga_gain_0db = 0x00;
    const uint8_t input_internal = 0x00; // LIN1/RIN1
    const uint8_t input_jack = 0x50; // LIN2/RIN2; software keeps LIN2 as the effective headset mic channel.
    const uint8_t data_normal_16bit_i2s = 0x0C;
    const uint8_t data_left_to_both_16bit_i2s = 0x4C;
    uint8_t input_reg = source == MIC_INPUT_SOURCE_JACK ? input_jack : input_internal;
    uint8_t data_reg = source == MIC_INPUT_SOURCE_JACK ? data_left_to_both_16bit_i2s : data_normal_16bit_i2s;
    uint8_t gain_reg = pga_gain_0db;
    bool ok = instance.codec.writeRegister(es8388_adc_control2, input_reg);
    ok &= instance.codec.writeRegister(es8388_adc_control4, data_reg);
    if (source == MIC_INPUT_SOURCE_JACK) {
        ok &= instance.codec.writeRegister(es8388_adc_control1, gain_reg);
    } else {
        instance.codec.setGain(instance.codec.getGain());
        ok &= instance.codec.readRegister(es8388_adc_control1, gain_reg);
    }
    LILYGO_LOG_I("Mic input source=%u ES8388 reg09=0x%02X reg0A=0x%02X reg0C=0x%02X %s",
          (unsigned)source,
          (unsigned)gain_reg,
          (unsigned)input_reg,
          (unsigned)data_reg,
          ok ? "ok" : "failed");
    return ok;
#else
    return false;
#endif
}

#define PLAYER_PLAY                 _BV(0)
#define PLAYER_END                  _BV(1)
#define PLAYER_RUNNING              _BV(2)


#if defined(HAS_SD_CARD_SOCKET)
#define FILESYSTEM                  SD
#else
#define FILESYSTEM                  FFat
#endif

#ifndef LILYGO_WATCH_SD_SPI_FREQ
#define LILYGO_WATCH_SD_SPI_FREQ 4000000U
#endif

#ifndef LILYGO_WATCH_ULTRA_SD_SPI_FREQ
#define LILYGO_WATCH_ULTRA_SD_SPI_FREQ 4000000U
#endif

#ifndef LILYGO_DECK_V2_SD_SPI_FREQ
#define LILYGO_DECK_V2_SD_SPI_FREQ 40000000U
#endif

#ifndef LILYGO_LORA_PAGER_SD_SPI_FREQ
#define LILYGO_LORA_PAGER_SD_SPI_FREQ 40000000U
#endif
#endif

static device_limits_t device_limits = {
    .max_brightness_level = DEVICE_MAX_BRIGHTNESS_LEVEL,
    .min_brightness_level = DEVICE_MIN_BRIGHTNESS_LEVEL,
#if FACTORY_HAS_POWER_MANAGE
    .max_charge_current_ma = DEVICE_MAX_CHARGE_CURRENT,
    .min_charge_current_ma = DEVICE_MIN_CHARGE_CURRENT,
    .charge_current_step_ma = DEVICE_CHARGE_LEVEL_NUMS,
    .max_charge_level_index = DEVICE_CHARGE_STEPS,
#else
    .max_charge_current_ma = 0,
    .min_charge_current_ma = 0,
    .charge_current_step_ma = 0,
    .max_charge_level_index = 0,
#endif
};

static void set_default_user_setting(user_setting_params_t &setting)
{
    memset(&setting, 0, sizeof(setting));
#ifdef ARDUINO
    setting.brightness_level = DEVICE_MAX_BRIGHTNESS_LEVEL;
    setting.keyboard_bl_level = 80;
#if FACTORY_HAS_POWER_MANAGE
    setting.charger_current = DEVICE_CHARGE_CURRENT_RECOMMEND;
#else
    setting.charger_current = 0;
    setting.charger_enable = false;
#endif
#else
    setting.brightness_level = 10;
    setting.keyboard_bl_level = 255;
    setting.charger_current = 1000;
#endif
    setting.led_indicator_level = 0;
    setting.disp_timeout_second = 30;
#if defined(ARDUINO) && !FACTORY_HAS_POWER_MANAGE
    setting.charger_enable = false;
#else
    setting.charger_enable = true;
#endif
    setting.theme_preset_idx = 0;
}

static void set_default_app_settings(app_settings_storage_t &settings)
{
    memset(&settings, 0, sizeof(settings));
    settings.magic = APP_SETTINGS_MAGIC;
    settings.version = APP_SETTINGS_VERSION;
    settings.size = (uint16_t)sizeof(settings);
    set_default_user_setting(settings.user);
    settings.timezone_offset_sec = GMT_OFFSET_SECOND;
    settings.daylight_offset_sec = 0;
    settings.nav_auto_hide_enabled = false;
    settings.audio_jack_mode = AUDIO_JACK_MODE_CTIA;
    settings.mic_input_source = MIC_INPUT_SOURCE_INTERNAL;
    settings.haptic_effect = HAPTIC_EFFECT_DEFAULT;
    settings.touch_guide_dismissed = false;
    settings.keyboard_navigation_mode = KEYBOARD_NAV_ENABLED;
#ifdef ARDUINO
    settings.rotary_step_divider = get_default_rotary_step_divider();
#else
    settings.rotary_step_divider = ROTARY_STEP_DIVIDER_MIN;
#endif
}

static bool app_settings_is_valid(const app_settings_storage_t &settings)
{
    return settings.magic == APP_SETTINGS_MAGIC &&
           settings.version == APP_SETTINGS_VERSION &&
           settings.size == sizeof(app_settings_storage_t);
}

static void normalize_app_settings(app_settings_storage_t &settings)
{
    settings.magic = APP_SETTINGS_MAGIC;
    settings.version = APP_SETTINGS_VERSION;
    settings.size = (uint16_t)sizeof(settings);
    settings.nav_auto_hide_enabled = settings.nav_auto_hide_enabled ? 1 : 0;
    settings.rotary_step_divider = clamp_rotary_step_divider(settings.rotary_step_divider);
    settings.audio_jack_mode = clamp_audio_jack_mode(settings.audio_jack_mode);
    settings.mic_input_source = clamp_mic_input_source(settings.mic_input_source);
    settings.haptic_effect = clamp_haptic_effect(settings.haptic_effect);
    settings.touch_guide_dismissed = settings.touch_guide_dismissed ? 1 : 0;
    settings.keyboard_navigation_mode =
        settings.keyboard_navigation_mode == KEYBOARD_NAV_DISABLED ?
        KEYBOARD_NAV_DISABLED : KEYBOARD_NAV_ENABLED;
}

static void apply_app_settings(const app_settings_storage_t &settings)
{
    user_setting = settings.user;
    timezone_offset_sec = settings.timezone_offset_sec;
    daylight_offset_sec = settings.daylight_offset_sec;
    nav_auto_hide_enabled = settings.nav_auto_hide_enabled != 0;
    rotary_step_divider = clamp_rotary_step_divider(settings.rotary_step_divider);
    audio_jack_mode = clamp_audio_jack_mode(settings.audio_jack_mode);
    mic_input_source = clamp_mic_input_source(settings.mic_input_source);
    haptic_effect = clamp_haptic_effect(settings.haptic_effect);
    touch_guide_dismissed = settings.touch_guide_dismissed != 0;
    keyboard_navigation_enabled = settings.keyboard_navigation_mode != KEYBOARD_NAV_DISABLED;
}

static void print_user_setting(const char *tag, const user_setting_params_t &setting)
{
    if (!LILYGO_DEBUG_ENABLED) return;
    LILYGO_LOG_PRINTF("\n========== %s ==========\n", tag);
    LILYGO_LOG_PRINTF("[Display]\n");
    LILYGO_LOG_PRINTF("  %-22s : %u / %u\n", "Backlight", (unsigned)setting.brightness_level,
           (unsigned)device_limits.max_brightness_level);
    LILYGO_LOG_PRINTF("  %-22s : %u s\n", "Timeout", (unsigned)setting.disp_timeout_second);
    LILYGO_LOG_PRINTF("  %-22s : %s\n", "Nav auto hide", nav_auto_hide_enabled ? "enabled" : "disabled");
    LILYGO_LOG_PRINTF("  %-22s : %u\n", "Theme preset", (unsigned)setting.theme_preset_idx);

    LILYGO_LOG_PRINTF("[Input]\n");
    LILYGO_LOG_PRINTF("  %-22s : %u\n", "Keyboard backlight", (unsigned)setting.keyboard_bl_level);
#if defined(ARDUINO_T_LORA_PAGER)
    LILYGO_LOG_PRINTF("  %-22s : %s\n", "Keyboard navigation",
                      keyboard_navigation_enabled ? "enabled" : "disabled");
#endif
    LILYGO_LOG_PRINTF("  %-22s : %u\n", "Rotary step divider", (unsigned)rotary_step_divider);

    LILYGO_LOG_PRINTF("[Power]\n");
    LILYGO_LOG_PRINTF("  %-22s : %s\n", "Charger", setting.charger_enable ? "enabled" : "disabled");
    LILYGO_LOG_PRINTF("  %-22s : %u mA\n", "Charge current", (unsigned)setting.charger_current);
    LILYGO_LOG_PRINTF("  %-22s : %u - %u mA\n", "Charge range",
           (unsigned)device_limits.min_charge_current_ma,
           (unsigned)device_limits.max_charge_current_ma);
    LILYGO_LOG_PRINTF("  %-22s : %u mA\n", "Charge step", (unsigned)device_limits.charge_current_step_ma);
    LILYGO_LOG_PRINTF("  %-22s : 0 - %u\n", "Charge level range", (unsigned)device_limits.max_charge_level_index);
    LILYGO_LOG_PRINTF("  %-22s : %u\n", "LED indicator", (unsigned)setting.led_indicator_level);

    LILYGO_LOG_PRINTF("[System]\n");
    LILYGO_LOG_PRINTF("  %-22s : %ld s\n", "Timezone offset", (long)timezone_offset_sec);
    LILYGO_LOG_PRINTF("  %-22s : %ld s\n", "Daylight offset", (long)daylight_offset_sec);
    LILYGO_LOG_PRINTF("  %-22s : %u (%s)\n", "Audio jack mode", (unsigned)audio_jack_mode,
           audio_jack_mode == AUDIO_JACK_MODE_OMTP ? "OMTP" : "CTIA");
    LILYGO_LOG_PRINTF("  %-22s : %u (%s)\n", "Mic input source", (unsigned)mic_input_source,
           mic_input_source == MIC_INPUT_SOURCE_JACK ? "jack" : "internal");
    LILYGO_LOG_PRINTF("========================================\n");
}

static bool print_setting_change_u32(const char *name, uint32_t before, uint32_t after)
{
    if (before == after) {
        return false;
    }
    LILYGO_LOG_PRINTF("- %s:%lu -> %lu\n", name, (unsigned long)before, (unsigned long)after);
    return true;
}

static void print_user_setting_changes(const user_setting_params_t &before,
                                       const user_setting_params_t &after)
{
    if (!LILYGO_DEBUG_ENABLED) return;
    LILYGO_LOG_PRINTF("user_setting changes:\n");
    bool changed = false;
    changed |= print_setting_change_u32("brightness_level", before.brightness_level, after.brightness_level);
    changed |= print_setting_change_u32("keyboard_bl_level", before.keyboard_bl_level, after.keyboard_bl_level);
    changed |= print_setting_change_u32("led_indicator_level", before.led_indicator_level, after.led_indicator_level);
    changed |= print_setting_change_u32("disp_timeout_second", before.disp_timeout_second, after.disp_timeout_second);
    changed |= print_setting_change_u32("charger_current", before.charger_current, after.charger_current);
    changed |= print_setting_change_u32("charger_enable", before.charger_enable, after.charger_enable);
    changed |= print_setting_change_u32("theme_preset_idx", before.theme_preset_idx, after.theme_preset_idx);
    if (!changed) {
        LILYGO_LOG_PRINTF("- no change\n");
    }
}

static bool user_setting_has_changed(const user_setting_params_t &before,
                                     const user_setting_params_t &after)
{
    return before.brightness_level != after.brightness_level ||
           before.keyboard_bl_level != after.keyboard_bl_level ||
           before.led_indicator_level != after.led_indicator_level ||
           before.disp_timeout_second != after.disp_timeout_second ||
           before.charger_current != after.charger_current ||
           before.charger_enable != after.charger_enable ||
           before.theme_preset_idx != after.theme_preset_idx;
}

static uint8_t button_monitor_id = 0;
static uint8_t button_monitor_event = 0;
static uint32_t button_monitor_count = 0;
static uint8_t pmu_button_monitor_event = 0;
static uint32_t pmu_button_monitor_count = 0;

static void update_button_monitor_state(uint8_t id, uint8_t event)
{
    button_monitor_id = id;
    button_monitor_event = event;
    button_monitor_count++;
}

static void update_pmu_button_monitor_state(uint8_t event)
{
    pmu_button_monitor_event = event;
    pmu_button_monitor_count++;
}

static const char *button_event_name(uint8_t event)
{
#ifdef ARDUINO
    switch ((ButtonEvent_t)event) {
    case BUTTON_EVENT_RELEASED:
        return "Released";
    case BUTTON_EVENT_PRESSED:
        return "Pressed";
    case BUTTON_EVENT_CLICK:
        return "Click";
    case BUTTON_EVENT_LONG_PRESSED:
        return "Long Press";
    case BUTTON_EVENT_DOUBLE_CLICK:
        return "Double Click";
    default:
        break;
    }
#else
    (void)event;
#endif
    return "Unknown";
}

static const char *pmu_button_event_name(uint8_t event)
{
#ifdef ARDUINO
    switch ((PMUEventType_t)event) {
    case PMU_EVENT_KEY_CLICKED:
        return "Click";
    case PMU_EVENT_KEY_LONG_PRESSED:
        return "Long Press";
    default:
        break;
    }
#else
    (void)event;
#endif
    return "Unknown";
}

#ifdef ARDUINO
static bool load_app_settings()
{
    app_settings_storage_t stored = {};
    bool valid = prefs.getBytes(APP_SETTINGS_KEY, &stored, sizeof(stored)) == sizeof(stored) &&
                 app_settings_is_valid(stored);
    if (!valid) {
        set_default_app_settings(app_settings);
        return false;
    }
    app_settings = stored;
    normalize_app_settings(app_settings);
    return true;
}

static bool save_app_settings()
{
    normalize_app_settings(app_settings);
    bool saved = prefs.putBytes(APP_SETTINGS_KEY, &app_settings, sizeof(app_settings)) == sizeof(app_settings);
    if (!saved) {
        LILYGO_LOG_E("Failed to save app settings");
    }
    return saved;
}
#endif


static const char *hw_devices[] = {

#ifdef EXCLUDE_LORA
    "",
#else
    USING_RADIO_NAME,
#endif


#ifdef USING_INPUT_DEV_TOUCHPAD
    "Touch Panel",
#else
    "",
#endif

#ifdef EXCLUDE_DRV2605
    "",
#else
    "Haptic Drive",
#endif

#if FACTORY_HAS_POWER_MANAGE
    "Power management",
#else
    "",
#endif
#if FACTORY_HAS_RTC
    "Real-time clock",
#else
    "",
#endif
    "PSRAM",

#ifdef EXCLUDE_GPS
    "",
#else
    "GPS",
#endif

#ifdef HAS_SD_CARD_SOCKET
    "SD card",
#else
    "",
#endif
#ifdef USING_ST25R3916
    "NFC",
#else
    "",
#endif

#ifdef USING_BHI260_SENSOR
    "BHI260AP 6-Axis Sensor",
#else
    "",
#endif

#if defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD)
    "Keyboard",
#else
    "",
#endif

#if FACTORY_HAS_BQ_GAUGE
    "BQ27220 Gauge",
#elif FACTORY_HAS_AXP2602_GUAGE
    "AXP2602 Gauge",
#else
    "",
#endif

#ifdef USING_XL9555_EXPANDS
    "Expands Control",
#else
    "",
#endif

#ifdef USING_AUDIO_CODEC
    "Audio codec",
#else
    "",
#endif

#ifdef USING_EXTERN_NRF2401
    "NRF2401 Sub 1G",
#else
    "",
#endif

#ifdef USING_SI473X_RADIO
    "SI4735 Radio",
#else
    "",
#endif

#ifdef USING_BME280
    "Pressure & Temperature",
#else
    "",
#endif

#ifdef USING_MAG_COMPASS
    "Magnetometer",
#else
    "",
#endif

#ifdef USING_BMA423_SENSOR
    "Accelerometer",
#else
    "",
#endif

#ifdef USING_QMI8658_SENSOR
    "6-Axis Sensor",
#else
    "",
#endif

#ifdef USING_MCU_EXPANDS
    "MCU Expands",
#else
    "",
#endif

#ifdef USING_TRACKBALL_V2
    "Trackball V2",
#else
    "",
#endif

};

static bool sync_date_time = false;

#ifdef ARDUINO
/* Convert a UTC tm to a Unix timestamp without depending on the process TZ. */
static bool gps_utc_to_epoch(const struct tm &utc, time_t &epoch)
{
    const int year = utc.tm_year + 1900;
    const int month = utc.tm_mon + 1;
    const int day = utc.tm_mday;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
            utc.tm_hour < 0 || utc.tm_hour > 23 || utc.tm_min < 0 || utc.tm_min > 59 ||
            utc.tm_sec < 0 || utc.tm_sec > 60) {
        return false;
    }

    // Howard Hinnant's civil-date calculation, relative to 1970-01-01.
    const int adjusted_year = year - (month <= 2 ? 1 : 0);
    const int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    const unsigned year_of_era = (unsigned)(adjusted_year - era * 400);
    const unsigned month_prime = (unsigned)(month + (month > 2 ? -3 : 9));
    const unsigned day_of_year = (153 * month_prime + 2) / 5 + (unsigned)day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const int64_t days = (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
    const int64_t seconds = days * 86400LL + utc.tm_hour * 3600LL + utc.tm_min * 60LL + utc.tm_sec;
    epoch = (time_t)seconds;
    return true;
}
#endif

extern void hw_nrf24_begin();
extern void hw_radio_begin();


#ifndef ARDUINO
int random(int min, int max)
{
    if (min > max) {
        int temp = min;
        min = max;
        max = temp;
    }
    int range = max - min + 1;
    return rand() % range + min;
}
#endif


#ifdef ARDUINO

size_t getArduinoLoopTaskStackSize(void)
{
    return 30 * 1024;
}

#include <esp_arduino_version.h>
#if __has_include(<ESP8266AudioVer.h>)
#include <ESP8266AudioVer.h>
#else
// ESP8266Audio 2.0.0 does not ship ESP8266AudioVer.h.
#define ESP8266AUDIO_MAJOR    2
#define ESP8266AUDIO_MINOR    0
#define ESP8266AUDIO_REVISION 0
#endif

#define FACTORY_AUDIO_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define FACTORY_ESP8266AUDIO_VERSION FACTORY_AUDIO_VERSION_VAL(ESP8266AUDIO_MAJOR, ESP8266AUDIO_MINOR, ESP8266AUDIO_REVISION)

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#if FACTORY_ESP8266AUDIO_VERSION != FACTORY_AUDIO_VERSION_VAL(2, 0, 0)
#error "Arduino ESP32 core < 3.0.0 requires ESP8266Audio 2.0.0. Set platformio.ini lib_deps to: earlephilhower/ESP8266Audio @ 2.0.0"
#endif
#else
#if FACTORY_ESP8266AUDIO_VERSION < FACTORY_AUDIO_VERSION_VAL(2, 4, 1)
#error "Arduino ESP32 core >= 3.0.0 requires ESP8266Audio 2.4.1. Update ESP8266Audio before building."
#endif
#endif

#include <AudioFileSource.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceICYStream.h>
#include <AudioGeneratorFLAC.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioLogger.h>
#include <AudioOutput.h>

#define NETWORK_AUDIO_CONFIG_PATH "/radio_streams.txt"
#define NETWORK_AUDIO_PREFILL_TIMEOUT_MS 5000
#define NETWORK_AUDIO_RECONNECT_TRIES 3
#define NETWORK_AUDIO_RECONNECT_DELAY_MS 800
#define NETWORK_AUDIO_RESTART_MAX 5
#define BOOT_SOUND_PATH "/boot.wav"
#define PCM_WAV_STREAM_BUFFER_SIZE 2048
#define LOCAL_AUDIO_BUFFER_SIZE (16 * 1024)
#define AUDIO_STOP_WAIT_TIMEOUT_MS 300

class AudioOutputLilyGo : public AudioOutput
{
public:
    AudioOutputLilyGo(AudioOutputIf *output) :
        _output(output),
        _opened(false),
        _sampleOffset(0),
        _yieldPending(false),
        _resampleAccumulator(0)
    {
        hertz = 44100;
        _bitsPerSample = 16;
        channels = 2;
    }

    bool SetRate(int hz) override
    {
        if (hertz != hz) {
            flush();
            _resampleAccumulator = 0;
        }
        hertz = hz;
        return true;
    }

    bool SetBitsPerSample(int bits)
    {
        if (bits != 8 && bits != 16 && bits != 24) {
            return false;
        }
        _bitsPerSample = bits;
        return true;
    }

    bool SetChannels(int chan) override
    {
        if (chan < 1 || chan > 2) {
            return false;
        }
        if (channels != chan) {
            flush();
            _resampleAccumulator = 0;
        }
        channels = chan;
        return true;
    }

    bool begin() override
    {
        return _output != NULL;
    }

    bool ConsumeSample(int16_t sample[2]) override
    {
        if (_yieldPending) {
            _yieldPending = false;
            return false;
        }
        if (!ensureOpen()) {
            return false;
        }
        int16_t left = sample[AudioOutput::LEFTCHANNEL];
        int16_t right = sample[AudioOutput::RIGHTCHANNEL];
        if (_bitsPerSample == 8) {
            left = (int16_t)(((int32_t)(uint8_t)sample[AudioOutput::LEFTCHANNEL] - 128) << 8);
            right = (int16_t)(((int32_t)(uint8_t)sample[AudioOutput::RIGHTCHANNEL] - 128) << 8);
        }

        return consumeResampledFrame(left, channels == 1 ? left : right);
    }

    bool stop() override
    {
        flush();
        closeOutput();
        return true;
    }

    void flush() override
    {
        if (_sampleOffset > 0) {
            writeBuffered();
        }
    }

private:
    static const size_t SAMPLE_BUFFER_COUNT = 2048;
    static const uint32_t OUTPUT_SAMPLE_RATE = 16000;

    bool ensureOpen()
    {
        if (_opened) {
            return true;
        }
        if (!_output) {
            return false;
        }
        InstanceLockGuard lock;
        _opened = _output->open(16, 2, OUTPUT_SAMPLE_RATE);
        return _opened;
    }

    bool consumeResampledFrame(int16_t left, int16_t right)
    {
        uint32_t input_rate = hertz > 0 ? (uint32_t)hertz : OUTPUT_SAMPLE_RATE;
        _resampleAccumulator += OUTPUT_SAMPLE_RATE;

        while (_resampleAccumulator >= input_rate) {
            _resampleAccumulator -= input_rate;
            if (!appendOutputFrame(left, right)) {
                return false;
            }
        }
        return true;
    }

    bool appendOutputFrame(int16_t left, int16_t right)
    {
        _samples[_sampleOffset++] = left;
        _samples[_sampleOffset++] = right;
        if (_sampleOffset >= SAMPLE_BUFFER_COUNT) {
            if (!writeBuffered()) {
                return false;
            }
            _yieldPending = true;
        }
        return true;
    }

    bool writeBuffered()
    {
        if (!_output || !_opened || _sampleOffset == 0) {
            _sampleOffset = 0;
            return false;
        }
        size_t bytes = _sampleOffset * sizeof(_samples[0]);
        int written = _output->write((const uint8_t *)_samples, bytes);
        _sampleOffset = 0;
        return written == 0 || written == (int)bytes;
    }

    void closeOutput()
    {
        if (_output && _opened) {
            InstanceLockGuard lock;
            _output->close();
        }
        _opened = false;
        _sampleOffset = 0;
        _resampleAccumulator = 0;
    }

    AudioOutputIf *_output;
    bool _opened;
    int16_t _samples[SAMPLE_BUFFER_COUNT];
    size_t _sampleOffset;
    bool _yieldPending;
    uint32_t _resampleAccumulator;
    uint8_t _bitsPerSample;
};

class AudioFileSourceLocalFS : public AudioFileSource
{
public:
    AudioFileSourceLocalFS(fs::FS &fs, bool use_spi_lock) :
        _fs(&fs),
        _use_spi_lock(use_spi_lock)
    {
    }

    ~AudioFileSourceLocalFS() override
    {
        close();
    }

    bool open(const char *filename) override
    {
        if (!filename || !strlen(filename)) {
            return false;
        }
        lock();
        _file = _fs->open(filename, FILE_READ);
        unlock();
        return _file;
    }

    uint32_t read(void *data, uint32_t len) override
    {
        if (!_file || !data || len == 0) {
            return 0;
        }
        lock();
        uint32_t read_size = _file.read(reinterpret_cast<uint8_t *>(data), len);
        unlock();
        return read_size;
    }

    bool seek(int32_t pos, int dir) override
    {
        if (!_file) {
            return false;
        }

        bool result = false;
        lock();
        if (dir == SEEK_SET) {
            result = _file.seek(pos, SeekSet);
        } else if (dir == SEEK_CUR) {
            result = _file.seek(pos, SeekCur);
        } else if (dir == SEEK_END) {
            result = _file.seek(pos, SeekEnd);
        }
        unlock();
        return result;
    }

    bool close() override
    {
        if (_file) {
            lock();
            _file.close();
            unlock();
        }
        return true;
    }

    bool isOpen() override
    {
        return _file ? true : false;
    }

    uint32_t getSize() override
    {
        if (!_file) {
            return 0;
        }
        lock();
        uint32_t size = _file.size();
        unlock();
        return size;
    }

    uint32_t getPos() override
    {
        if (!_file) {
            return 0;
        }
        lock();
        uint32_t pos = _file.position();
        unlock();
        return pos;
    }

private:
    void lock()
    {
        if (_use_spi_lock) {
            instance.lockSPI();
        }
    }

    void unlock()
    {
        if (_use_spi_lock) {
            instance.unlockSPI();
        }
    }

    fs::FS *_fs;
    File _file;
    bool _use_spi_lock;
};

static String normalize_audio_path(const char *filename)
{
    if (!filename) {
        return "";
    }

    String path = filename;
    path.trim();
    if (!path.length()) {
        return "";
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    return path;
}

typedef enum {
    LOCAL_AUDIO_FORMAT_UNSUPPORTED = 0,
    LOCAL_AUDIO_FORMAT_MP3,
    LOCAL_AUDIO_FORMAT_WAV,
    LOCAL_AUDIO_FORMAT_FLAC,
} local_audio_format_t;

static local_audio_format_t get_local_audio_format(const String &path)
{
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".mp3")) {
        return LOCAL_AUDIO_FORMAT_MP3;
    }
    if (lower.endsWith(".wav")) {
        return LOCAL_AUDIO_FORMAT_WAV;
    }
    if (lower.endsWith(".flac") || lower.endsWith(".fla")) {
        return LOCAL_AUDIO_FORMAT_FLAC;
    }
    return LOCAL_AUDIO_FORMAT_UNSUPPORTED;
}

static const char *local_audio_format_name(local_audio_format_t format)
{
    switch (format) {
    case LOCAL_AUDIO_FORMAT_MP3:
        return "MP3";
    case LOCAL_AUDIO_FORMAT_WAV:
        return "WAV";
    case LOCAL_AUDIO_FORMAT_FLAC:
        return "FLAC";
    default:
        return "unsupported";
    }
}

static uint32_t id3_syncsafe_to_u32(const uint8_t *bytes)
{
    if (!bytes) {
        return 0;
    }
    if ((bytes[0] & 0x80) || (bytes[1] & 0x80) ||
            (bytes[2] & 0x80) || (bytes[3] & 0x80)) {
        return 0;
    }
    return ((uint32_t)bytes[0] << 21) |
           ((uint32_t)bytes[1] << 14) |
           ((uint32_t)bytes[2] << 7) |
           (uint32_t)bytes[3];
}

static bool skip_local_mp3_id3(AudioFileSourceLocalFS *file, const char *path)
{
    if (!file || !file->isOpen()) {
        return false;
    }

    uint8_t header[10] = {0};
    if (!file->seek(0, SEEK_SET)) {
        return false;
    }
    if (file->read(header, sizeof(header)) != sizeof(header)) {
        file->seek(0, SEEK_SET);
        return false;
    }

    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3' ||
            header[3] < 2 || header[3] > 4) {
        file->seek(0, SEEK_SET);
        return false;
    }

    uint32_t tag_size = id3_syncsafe_to_u32(&header[6]);
    uint32_t skip_size = 10 + tag_size + ((header[5] & 0x10) ? 10 : 0);
    uint32_t file_size = file->getSize();
    if (skip_size >= file_size) {
        LILYGO_LOG_PRINTF("Local MP3 ID3 skip ignored: %s tag=%lu file=%lu\n",
                      path ? path : "N.A", (unsigned long)tag_size,
                      (unsigned long)file_size);
        file->seek(0, SEEK_SET);
        return false;
    }

    bool ok = file->seek(skip_size, SEEK_SET);
    LILYGO_LOG_PRINTF("Local MP3 ID3 skipped: %s offset=%lu tag=%lu ok=%d\n",
                  path ? path : "N.A", (unsigned long)skip_size,
                  (unsigned long)tag_size, ok ? 1 : 0);
    if (!ok) {
        file->seek(0, SEEK_SET);
    }
    return ok;
}

static const NetworkAudioStreamParams_t default_network_streams[] = {
    {"Groove Salad", "http://ice3.somafm.com/groovesalad-128-mp3"},
    {"Drone Zone", "http://ice3.somafm.com/dronezone-128-mp3"},
    {"Deep Space One", "http://ice3.somafm.com/deepspaceone-128-mp3"},
    {"Secret Agent", "http://ice3.somafm.com/secretagent-128-mp3"},
};

static void appendDefaultNetworkStreams(vector < NetworkAudioStreamParams_t > &list)
{
    for (size_t i = 0; i < sizeof(default_network_streams) / sizeof(default_network_streams[0]); ++i) {
        list.push_back(default_network_streams[i]);
    }
}

static void appendAudioFile(vector < AudioParams_t > &list, audio_source_type_t source_type, const char *file_name)
{
    AudioParams_t item = {};
    item.source_type = source_type;
    copy_cstr(item.file_name, sizeof(item.file_name), file_name);
    list.push_back(item);
}

static uint8_t *allocNetworkAudioBuffer(size_t &size)
{
    static const size_t candidate_sizes[] = {
        128 * 1024,
        96 * 1024,
        64 * 1024,
        32 * 1024,
        16 * 1024,
        8 * 1024,
    };

    for (size_t i = 0; i < sizeof(candidate_sizes) / sizeof(candidate_sizes[0]); ++i) {
        uint8_t *buffer = NULL;
#if defined(BOARD_HAS_PSRAM)
        if (psramFound()) {
            buffer = (uint8_t *)heap_caps_malloc(candidate_sizes[i], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
#endif
        if (!buffer) {
            buffer = (uint8_t *)malloc(candidate_sizes[i]);
        }
        if (buffer) {
            size = candidate_sizes[i];
            return buffer;
        }
    }

    size = 0;
    return NULL;
}

static uint8_t *allocLocalAudioBuffer(size_t &size)
{
    static const size_t candidate_sizes[] = {
        LOCAL_AUDIO_BUFFER_SIZE,
        8 * 1024,
        4 * 1024,
        2 * 1024,
    };

    for (size_t i = 0; i < sizeof(candidate_sizes) / sizeof(candidate_sizes[0]); ++i) {
        uint8_t *buffer = NULL;
#if defined(BOARD_HAS_PSRAM)
        if (psramFound()) {
            buffer = (uint8_t *)heap_caps_malloc(candidate_sizes[i], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
#endif
        if (!buffer) {
            buffer = (uint8_t *)malloc(candidate_sizes[i]);
        }
        if (buffer) {
            size = candidate_sizes[i];
            return buffer;
        }
    }

    size = 0;
    return NULL;
}

static bool waitNetworkAudioBuffer(AudioFileSourceBuffer *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return false;
    }

    size_t target_fill = buffer_size / 2;
    if (target_fill > 48 * 1024) {
        target_fill = 48 * 1024;
    }
    if (target_fill < 4 * 1024) {
        target_fill = buffer_size < 4 * 1024 ? buffer_size : 4 * 1024;
    }

    uint8_t dummy = 0;
    buffer->read(&dummy, 0);

    uint32_t start_ms = millis();
    while (buffer->getFillLevel() < target_fill &&
            millis() - start_ms < NETWORK_AUDIO_PREFILL_TIMEOUT_MS) {
        EventBits_t bits = xEventGroupGetBits(playerEvent);
        if (bits & PLAYER_END) {
            return false;
        }
        if (!(bits & PLAYER_PLAY)) {
            delay(20);
            continue;
        }
        buffer->loop();
        delay(10);
    }

    LILYGO_LOG_PRINTF("Network audio buffer: %lu/%lu bytes\n",
                  (unsigned long)buffer->getFillLevel(),
                  (unsigned long)buffer_size);
    return buffer->getFillLevel() > 0;
}

static bool parseNetworkStreamLine(String line, NetworkAudioStreamParams_t &stream)
{
    line.trim();
    if (!line.length() || line.startsWith("#")) {
        return false;
    }

    int separator = line.indexOf('|');
    if (separator < 0) {
        separator = line.indexOf(',');
    }
    if (separator <= 0) {
        return false;
    }

    String name = line.substring(0, separator);
    String url = line.substring(separator + 1);
    name.trim();
    url.trim();
    if (!name.length() || !url.startsWith("http://")) {
        return false;
    }

    copy_cstr(stream.name, sizeof(stream.name), name.c_str());
    copy_cstr(stream.url, sizeof(stream.url), url.c_str());
    return true;
}

static void writeDefaultNetworkStreamConfig()
{
#if defined(HAS_SD_CARD_SOCKET)
    File file = SD.open(NETWORK_AUDIO_CONFIG_PATH, FILE_WRITE);
    if (!file) {
        return;
    }
    file.println("# LilyGo network audio streams");
    file.println("# Format: Name|http://host/path");
    for (size_t i = 0; i < sizeof(default_network_streams) / sizeof(default_network_streams[0]); ++i) {
        file.print(default_network_streams[i].name);
        file.print("|");
        file.println(default_network_streams[i].url);
    }
    file.close();
#endif
}

static void networkStreamStatusCallback(void *cbData, int code, const char *string)
{
    (void)cbData;
    static uint32_t last_log_ms = 0;
    static int last_code = -9999;
    static char last_message[80] = "";

    char status[80];
    strncpy(status, string, sizeof(status));
    status[sizeof(status) - 1] = '\0';

    uint32_t now = millis();
    if (code == last_code && strcmp(status, last_message) == 0 && now - last_log_ms < 1500) {
        return;
    }
    last_code = code;
    strncpy(last_message, status, sizeof(last_message));
    last_message[sizeof(last_message) - 1] = '\0';
    last_log_ms = now;

    LILYGO_LOG_PRINTF("NETAUDIO STATUS %d: %s\n", code, status);
}

static void networkStreamMetadataCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
    (void)cbData;
    (void)isUnicode;
    char metadata_type[32];
    char metadata_value[96];
    strncpy(metadata_type, type, sizeof(metadata_type));
    strncpy(metadata_value, string, sizeof(metadata_value));
    metadata_type[sizeof(metadata_type) - 1] = '\0';
    metadata_value[sizeof(metadata_value) - 1] = '\0';
    LILYGO_LOG_PRINTF("NETAUDIO METADATA %s: %s\n", metadata_type, metadata_value);
}

static bool playNetworkAudioStream(const char *name, const char *url)
{
    if (!name || !url || !strlen(url)) {
        return false;
    }
    if (!WiFi.isConnected()) {
        LILYGO_LOG_PRINTLN("Network audio stream requires WiFi.");
        return false;
    }

    AudioOutputIf *codec_output = instance.getAudioOutput();
    if (!codec_output) {
        LILYGO_LOG_PRINTLN("Audio output is not available.");
        return false;
    }

#if LILYGO_DEBUG_ENABLED
    audioLogger = &Serial;
#else
    audioLogger = &silencedLogger;
#endif
    LILYGO_LOG_PRINTF("Playing network stream: %s <%s>\n", name, url);

    bool result = false;
    bool stop_requested = false;

    for (int attempt = 0; attempt < NETWORK_AUDIO_RESTART_MAX && !stop_requested; ++attempt) {
        AudioFileSourceICYStream *file = NULL;
        AudioFileSourceBuffer *buffer = NULL;
        AudioGeneratorMP3 *mp3 = NULL;
        AudioOutputLilyGo *output = NULL;
        uint8_t *network_buffer = NULL;
        size_t network_buffer_size = 0;
        bool mp3_started = false;

        if (!WiFi.isConnected()) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: WiFi disconnected.");
            break;
        }

        if (attempt > 0) {
            LILYGO_LOG_PRINTF("Reopening network stream: %s (%d/%d)\n",
                          name, attempt + 1, NETWORK_AUDIO_RESTART_MAX);
        }

        file = new AudioFileSourceICYStream();
        if (!file) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: no memory for stream.");
            break;
        }
        file->SetReconnect(NETWORK_AUDIO_RECONNECT_TRIES, NETWORK_AUDIO_RECONNECT_DELAY_MS);
        file->RegisterMetadataCB(networkStreamMetadataCallback, NULL);
        file->RegisterStatusCB(networkStreamStatusCallback, NULL);
        if (!file->open(url)) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: open failed.");
            goto cleanup_session;
        }

        network_buffer = allocNetworkAudioBuffer(network_buffer_size);
        if (!network_buffer) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: no memory for buffer.");
            goto cleanup_session;
        }

        buffer = new AudioFileSourceBuffer(file, network_buffer, network_buffer_size);
        if (!buffer) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: no memory for buffer source.");
            goto cleanup_session;
        }
        buffer->RegisterStatusCB(networkStreamStatusCallback, NULL);
        if (!waitNetworkAudioBuffer(buffer, network_buffer_size)) {
            stop_requested = xEventGroupGetBits(playerEvent) & PLAYER_END;
            LILYGO_LOG_PRINTLN("Network audio stream stopped: no stream data.");
            goto cleanup_session;
        }

        output = new AudioOutputLilyGo(codec_output);
        mp3 = new AudioGeneratorMP3();
        if (!output || !mp3) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: no memory for decoder.");
            goto cleanup_session;
        }
        mp3->RegisterStatusCB(networkStreamStatusCallback, NULL);
        if (!mp3->begin(buffer, output)) {
            LILYGO_LOG_PRINTLN("Network audio stream stopped: decoder start failed.");
            goto cleanup_session;
        }

        mp3_started = true;
        result = true;
        xEventGroupSetBits(playerEvent, PLAYER_RUNNING);

        while (mp3->isRunning()) {
            EventBits_t eventBits = xEventGroupWaitBits(playerEvent, PLAYER_PLAY | PLAYER_END,
                                    pdFALSE, pdFALSE, pdMS_TO_TICKS(50));
            if (eventBits & PLAYER_END) {
                stop_requested = true;
                break;
            }
            if (!(eventBits & PLAYER_PLAY)) {
                delay(20);
                continue;
            }
            if (!WiFi.isConnected()) {
                LILYGO_LOG_PRINTLN("Network audio stream interrupted: WiFi disconnected.");
                break;
            }
            if (!mp3->loop()) {
                LILYGO_LOG_PRINTLN("Network audio stream interrupted: decoder stopped.");
                break;
            }
        }

cleanup_session:
        if (mp3) {
            if (mp3_started) {
                mp3->stop();
            }
            delete mp3;
        }
        if (output) {
            if (!mp3_started) {
                output->stop();
            }
            delete output;
        }
        if (buffer) {
            buffer->close();
            delete buffer;
        }
        if (file) {
            file->close();
            delete file;
        }
        if (network_buffer) {
            free(network_buffer);
        }

        if (stop_requested || !result || !WiFi.isConnected() || attempt + 1 >= NETWORK_AUDIO_RESTART_MAX) {
            break;
        }

        uint32_t reconnect_start = millis();
        LILYGO_LOG_PRINTLN("Network audio stream will retry.");
        while (millis() - reconnect_start < 1500) {
            if (xEventGroupGetBits(playerEvent) & PLAYER_END) {
                stop_requested = true;
                break;
            }
            delay(50);
        }
    }

    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
    LILYGO_LOG_PRINTLN("Network stream done.");
    return result;
}

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} pcm_wav_info_t;

static bool wav_read_exact(File &file, uint8_t *dst, size_t size)
{
    return file.read(dst, size) == size;
}

static uint16_t wav_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t wav_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool wav_read_chunk_header(File &file, char id[4], uint32_t *size)
{
    uint8_t header[8];
    if (!wav_read_exact(file, header, sizeof(header))) {
        return false;
    }
    memcpy(id, header, 4);
    *size = wav_le32(header + 4);
    return true;
}

static bool parse_pcm_wav(File &file, pcm_wav_info_t &info)
{
    memset(&info, 0, sizeof(info));

    uint8_t riff_header[12];
    if (!wav_read_exact(file, riff_header, sizeof(riff_header))) {
        return false;
    }
    if (memcmp(riff_header, "RIFF", 4) != 0 || memcmp(riff_header + 8, "WAVE", 4) != 0) {
        LILYGO_LOG_PRINTLN("Boot sound is not a RIFF/WAVE file.");
        return false;
    }

    bool found_fmt = false;
    bool found_data = false;
    const uint32_t file_size = file.size();

    while (file.position() + 8 <= file_size) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (!wav_read_chunk_header(file, chunk_id, &chunk_size)) {
            break;
        }

        uint32_t chunk_start = file.position();
        uint32_t chunk_next = chunk_start + chunk_size + (chunk_size & 1);
        if (chunk_next < chunk_start || chunk_next > file_size + 1) {
            LILYGO_LOG_PRINTLN("Boot sound WAV chunk size is invalid.");
            return false;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                LILYGO_LOG_PRINTLN("Boot sound WAV fmt chunk is too small.");
                return false;
            }

            uint8_t fmt[16];
            if (!wav_read_exact(file, fmt, sizeof(fmt))) {
                return false;
            }

            info.audio_format = wav_le16(fmt);
            info.channels = wav_le16(fmt + 2);
            info.sample_rate = wav_le32(fmt + 4);
            info.bits_per_sample = wav_le16(fmt + 14);
            found_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            info.data_offset = chunk_start;
            info.data_size = chunk_size;
            found_data = true;
        }

        if (found_fmt && found_data) {
            break;
        }

        if (!file.seek(chunk_next)) {
            return false;
        }
    }

    if (!found_fmt || !found_data) {
        LILYGO_LOG_PRINTLN("Boot sound WAV is missing fmt or data chunk.");
        return false;
    }
    if (info.audio_format != 1 || info.bits_per_sample != 16 ||
            info.channels < 1 || info.channels > 2 || info.sample_rate == 0 ||
            info.data_size == 0) {
        LILYGO_LOG_PRINTF("Unsupported boot sound WAV: format=%u bits=%u channels=%u rate=%lu size=%lu\n",
                      info.audio_format, info.bits_per_sample, info.channels,
                      (unsigned long)info.sample_rate, (unsigned long)info.data_size);
        return false;
    }

    return true;
}

static bool play_pcm_wav_from_ffat(const char *path)
{
    if (!path || !strlen(path)) {
        return false;
    }

    File file = FFat.open(path, FILE_READ);
    if (!file) {
        LILYGO_LOG_PRINTF("Boot sound file not found: %s\n", path);
        return false;
    }

    pcm_wav_info_t wav;
    if (!parse_pcm_wav(file, wav)) {
        file.close();
        return false;
    }

    AudioOutputIf *codec_output = instance.getAudioOutput();
    if (!codec_output) {
        LILYGO_LOG_PRINTLN("Boot sound skipped: audio output is not available.");
        file.close();
        return false;
    }
    if (!file.seek(wav.data_offset)) {
        file.close();
        return false;
    }

    uint8_t *buffer = (uint8_t *)malloc(PCM_WAV_STREAM_BUFFER_SIZE);
    if (!buffer) {
        LILYGO_LOG_PRINTLN("Boot sound skipped: no stream buffer.");
        file.close();
        return false;
    }

    LILYGO_LOG_PRINTF("Playing boot sound: %s rate=%lu channels=%u bits=%u size=%lu\n",
                  path, (unsigned long)wav.sample_rate, wav.channels,
                  wav.bits_per_sample, (unsigned long)wav.data_size);

    AudioOutputLilyGo output(codec_output);
    if (!output.begin()) {
        LILYGO_LOG_PRINTLN("Boot sound skipped: audio output open failed.");
        free(buffer);
        file.close();
        return false;
    }
    output.SetRate(wav.sample_rate);
    output.SetBitsPerSample(wav.bits_per_sample);
    output.SetChannels(wav.channels);

    xEventGroupSetBits(playerEvent, PLAYER_RUNNING);

    uint32_t remaining = wav.data_size;
    const size_t bytes_per_frame = wav.channels * sizeof(int16_t);
    while (remaining > 0) {
        EventBits_t bits = xEventGroupGetBits(playerEvent);
        if (bits & PLAYER_END) {
            break;
        }
        if (!(bits & PLAYER_PLAY)) {
            delay(5);
            continue;
        }

        size_t request = remaining > PCM_WAV_STREAM_BUFFER_SIZE ? PCM_WAV_STREAM_BUFFER_SIZE : remaining;
        request -= request % bytes_per_frame;
        if (request == 0) {
            break;
        }
        int read_size = file.read(buffer, request);
        if (read_size <= 0) {
            break;
        }
        bool output_failed = false;
        for (int pos = 0; pos + (int)bytes_per_frame <= read_size; pos += bytes_per_frame) {
            int16_t sample[2];
            sample[0] = (int16_t)wav_le16(buffer + pos);
            sample[1] = wav.channels == 2 ? (int16_t)wav_le16(buffer + pos + sizeof(int16_t)) : sample[0];
            if (!output.ConsumeSample(sample)) {
                delay(1);
                if (!output.ConsumeSample(sample)) {
                    output_failed = true;
                    break;
                }
            }
        }
        if (output_failed) {
            break;
        }
        remaining -= read_size;
        delay(1);
    }

    output.stop();
    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
    free(buffer);
    file.close();
    return remaining == 0;
}

static AudioGenerator *create_local_audio_decoder(local_audio_format_t format)
{
    switch (format) {
    case LOCAL_AUDIO_FORMAT_MP3:
        return new AudioGeneratorMP3();
    case LOCAL_AUDIO_FORMAT_WAV: {
        AudioGeneratorWAV *wav = new AudioGeneratorWAV();
        if (wav) {
            wav->SetBufferSize(512);
        }
        return wav;
    }
    case LOCAL_AUDIO_FORMAT_FLAC:
        return new AudioGeneratorFLAC();
    default:
        return NULL;
    }
}

static bool play_local_audio_stream(audio_source_type_t source, const char *filename, local_audio_format_t format)
{
    String path = normalize_audio_path(filename);
    if (!path.length() || format == LOCAL_AUDIO_FORMAT_UNSUPPORTED) {
        return false;
    }

    fs::FS *filesystem = &FFat;
    bool use_spi_lock = false;

    if (source == AUDIO_SOURCE_SDCARD) {
#if defined(HAS_SD_CARD_SOCKET)
        if (!instance.isCardReady()) {
            LILYGO_LOG_PRINTLN("Local audio skipped: SD card is not ready.");
            return false;
        }
        bool sd_ready = instance.installSD();
        if (!sd_ready) {
            LILYGO_LOG_PRINTLN("Local audio skipped: SD mount failed.");
            return false;
        }
        filesystem = &SD;
        use_spi_lock = true;
#else
        LILYGO_LOG_PRINTLN("Local audio skipped: SD card is not supported.");
        return false;
#endif
    }

    AudioOutputIf *codec_output = instance.getAudioOutput();
    if (!codec_output) {
        LILYGO_LOG_PRINTLN("Local audio skipped: audio output is not available.");
        return false;
    }

    AudioFileSourceLocalFS *file = NULL;
    AudioFileSource *decoder_source = NULL;
    AudioFileSourceBuffer *buffer = NULL;
    AudioGenerator *decoder = NULL;
    AudioOutputLilyGo *output = NULL;
    uint8_t *local_buffer = NULL;
    size_t local_buffer_size = 0;
    bool decoder_started = false;
    bool result = false;

    file = new AudioFileSourceLocalFS(*filesystem, use_spi_lock);
    if (!file || !file->open(path.c_str())) {
        LILYGO_LOG_PRINTF("Local audio open failed: %s source:%d\n", path.c_str(), source);
        goto cleanup;
    }

    local_buffer = allocLocalAudioBuffer(local_buffer_size);
    if (!local_buffer) {
        LILYGO_LOG_PRINTLN("Local audio skipped: no stream buffer.");
        goto cleanup;
    }

    decoder_source = file;
    if (format == LOCAL_AUDIO_FORMAT_MP3) {
        skip_local_mp3_id3(file, path.c_str());
    }

    buffer = new AudioFileSourceBuffer(decoder_source, local_buffer, local_buffer_size);
    if (!buffer) {
        LILYGO_LOG_PRINTLN("Local audio skipped: no memory for stream buffer.");
        goto cleanup;
    }
    output = new AudioOutputLilyGo(codec_output);
    decoder = create_local_audio_decoder(format);
    if (!output || !decoder) {
        LILYGO_LOG_PRINTLN("Local audio skipped: no memory for decoder.");
        goto cleanup;
    }

    LILYGO_LOG_PRINTF("Playing local %s stream: %s source:%d buffer:%lu\n",
                  local_audio_format_name(format), path.c_str(), source,
                  (unsigned long)local_buffer_size);

    if (!decoder->begin(buffer, output)) {
        LILYGO_LOG_PRINTLN("Local audio skipped: decoder start failed.");
        goto cleanup;
    }

    decoder_started = true;
    result = true;
    xEventGroupSetBits(playerEvent, PLAYER_RUNNING);

    while (decoder->isRunning()) {
        EventBits_t eventBits = xEventGroupWaitBits(playerEvent, PLAYER_PLAY | PLAYER_END,
                                pdFALSE, pdFALSE, pdMS_TO_TICKS(20));
        if (eventBits & PLAYER_END) {
            break;
        }
        if (!(eventBits & PLAYER_PLAY)) {
            delay(10);
            continue;
        }
        if (!decoder->loop()) {
            break;
        }
        delay(1);
    }

cleanup:
    if (decoder) {
        if (decoder_started) {
            decoder->stop();
        }
        delete decoder;
    }
    if (output) {
        if (!decoder_started) {
            output->stop();
        }
        delete output;
    }
    if (buffer) {
        buffer->close();
        delete buffer;
    } else if (file) {
        file->close();
    }
    if (file) {
        delete file;
    }
    if (local_buffer) {
        free(local_buffer);
    }

    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
    LILYGO_LOG_PRINTLN("Local audio stream done.");
    return result;
}

static void hw_sd_play(audio_source_type_t source, const char *filename)
{
    String path = normalize_audio_path(filename);
    local_audio_format_t format = get_local_audio_format(path);

    if (format != LOCAL_AUDIO_FORMAT_UNSUPPORTED) {
        play_local_audio_stream(source, path.c_str(), format);
        return;
    }

    LILYGO_LOG_PRINTF("Unsupported local audio format: %s\n", path.c_str());
}

static bool playBeep(uint16_t frequency_hz, uint16_t duration_ms)
{
    if (frequency_hz == 0 || duration_ms == 0) {
        return false;
    }

    AudioOutputIf *codec_output = instance.getAudioOutput();
    AudioOutputLilyGo output(codec_output);
    if (!codec_output || !output.begin()) {
        LILYGO_LOG_E("Audio output is not available");
        return false;
    }

    static const uint32_t sample_rate = 16000;
    static const uint8_t channels = 2;
    static const uint8_t bits = 16;
    static const uint16_t pre_roll_ms = 120;
    static const uint16_t post_roll_ms = 120;
    const float amplitude = 18000.0f;
    float phase = 0.0f;
    const float phase_step = (2.0f * (float)M_PI * (float)frequency_hz) / (float)sample_rate;
    uint32_t total_frames = ((uint32_t)sample_rate * duration_ms) / 1000;
    if (total_frames == 0) {
        total_frames = 1;
    }
    LILYGO_LOG_I("Play beep");

    if (!output.SetRate(sample_rate) ||
            !output.SetBitsPerSample(bits) ||
            !output.SetChannels(channels)) {
        output.stop();
        return false;
    }

    xEventGroupSetBits(playerEvent, PLAYER_RUNNING);
    auto consumeFrame = [&output](int16_t left, int16_t right) -> bool {
        int16_t frame[2];
        frame[0] = left;
        frame[1] = right;
        if (output.ConsumeSample(frame)) {
            return true;
        }
        delay(1);
        return output.ConsumeSample(frame);
    };

    auto consumeSilence = [&consumeFrame](uint32_t frames) -> bool {
        for (uint32_t i = 0; i < frames; ++i) {
            if (xEventGroupGetBits(playerEvent) & PLAYER_END) {
                return false;
            }
            if (!consumeFrame(0, 0)) {
                return false;
            }
        }
        return true;
    };

    const uint32_t pre_roll_frames = ((uint32_t)sample_rate * pre_roll_ms) / 1000;
    const uint32_t post_roll_frames = ((uint32_t)sample_rate * post_roll_ms) / 1000;
    bool completed = consumeSilence(pre_roll_frames);
    for (uint32_t i = 0; i < total_frames; ++i) {
        if (!completed) {
            break;
        }
        if (xEventGroupGetBits(playerEvent) & PLAYER_END) {
            completed = false;
            break;
        }
        int16_t sample = (int16_t)(sinf(phase) * amplitude);
        if (!consumeFrame(sample, sample)) {
            completed = false;
            break;
        }
        phase += phase_step;
        if (phase >= (2.0f * (float)M_PI)) {
            phase -= (2.0f * (float)M_PI);
        }
    }
    if (completed) {
        completed = consumeSilence(post_roll_frames);
    }
    output.flush();
    uint32_t playback_hold_ms = pre_roll_ms + duration_ms + post_roll_ms + 40;
    while (completed && playback_hold_ms > 0) {
        if (xEventGroupGetBits(playerEvent) & PLAYER_END) {
            break;
        }
        uint32_t step_ms = playback_hold_ms > 20 ? 20 : playback_hold_ms;
        delay(step_ms);
        playback_hold_ms -= step_ms;
    }
    output.stop();
    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);
    return completed;
}

static void playerTask(void *args)
{
    audio_params_t params;
    while (1) {
        if (xQueueReceive(playerQueue, &params, portMAX_DELAY) != pdPASS) {
            continue;
        }
        switch (params.event) {
        case APP_EVENT_PLAY:
            LILYGO_LOG_PRINTF("Event: filename:%s source:%d\n", params.filename, params.source_type);
            xEventGroupClearBits(playerEvent, PLAYER_END);
            xEventGroupSetBits(playerEvent, PLAYER_PLAY);
            hw_sd_play(params.source_type, params.filename);
            break;
        case APP_EVENT_PLAY_STREAM:
            LILYGO_LOG_PRINTF("Event: stream:%s url:%s\n", params.filename ? params.filename : "N.A",
                          params.url ? params.url : "N.A");
            xEventGroupClearBits(playerEvent, PLAYER_END);
            xEventGroupSetBits(playerEvent, PLAYER_PLAY);
            playNetworkAudioStream(params.filename, params.url);
            if (params.filename) {
                free((void *)params.filename);
            }
            if (params.url) {
                free((void *)params.url);
            }
            break;
        case APP_EVENT_PLAY_BEEP:
            playBeep(params.frequency_hz, params.duration_ms);
            break;
        case APP_EVENT_PLAY_BOOT_SOUND:
            play_pcm_wav_from_ffat(params.filename ? params.filename : BOOT_SOUND_PATH);
            break;
        case APP_EVENT_RECOVER:
            break;
        default:
            break;
        }
    }
    playerTaskHandler = NULL;
    vTaskDelete(NULL);
}

#endif

#if  defined(USING_ST25R3916) && defined(ARDUINO)

extern void ui_nfc_pop_up(wifi_conn_params_t &params);
extern void ui_nfc_on_reader_event(LilyGoNfcEvent event, const LilyGoNfcReaderResult &result);

static void nfc_reader_event_callback(LilyGoNfcEvent event, const LilyGoNfcReaderResult &result, void *userData)
{
    static wifi_conn_params_t params;
    (void)userData;

    LILYGO_LOG_PRINTF("NFC event: %s  state:%s  card:%s  id:%s  ndef:%s  err:%u  records:%u\n",
                  lilygoNfcEventName(event), lilygoNfcStateName(result.state),
                  result.cardType[0] ? result.cardType : "--",
                  result.uidText,
                  result.ndefStateText[0] ? result.ndefStateText : "--",
                  result.lastError,
                  result.recordCount);

    if (event == LILYGO_NFC_EVENT_CARD_DETECTED) {
        hw_feedback();
    }

    if (event == LILYGO_NFC_EVENT_NDEF_READ) {
        for (uint8_t i = 0; i < result.recordCount; ++i) {
            const LilyGoNfcRecord &record = result.records[i];
            if (record.kind != LILYGO_NFC_RECORD_WIFI) continue;

            copy_cstr(params.ssid, sizeof(params.ssid), record.wifiSsid);
            copy_cstr(params.password, sizeof(params.password), record.wifiPassword);
            LILYGO_LOG_PRINTF("NFC WiFi ssid:<%s> password:<%s>\n",
                          params.ssid, params.password);
            ui_nfc_pop_up(params);
            break;
        }
    }

    ui_nfc_on_reader_event(event, result);
}
#endif  /*USING_ST25R3916*/



bool hw_start_nfc_discovery()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    instance.powerControl(POWER_NFC, true);
    return LilyGoNfc.beginReader(nfc_reader_event_callback);
#else
    return false;
#endif
}

#if  defined(USING_ST25R3916) && defined(ARDUINO)
bool hw_start_nfc_emulation(const LilyGoNfcEmulationConfig &config)
{
    instance.powerControl(POWER_NFC, true);
    return LilyGoNfc.beginEmulation(config);
}
#endif

void hw_loop_nfc()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    LilyGoNfc.loop();
#endif
}

void hw_stop_nfc()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    LilyGoNfc.stop();
    instance.powerControl(POWER_NFC, false);
#endif
}

void hw_stop_nfc_discovery()
{
    hw_stop_nfc();
}

static bool _feedback_enable = true;

void hw_disable_feedback()
{
    _feedback_enable = false;
}

void hw_enable_feedback()
{
    _feedback_enable = true;
}

#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
// ALT+B brightness toggle callback - migrated from hardcoded LilyGoKeyboard handler
// Users can register this or any other key combo via kb.registerKeyCombo()
static void alt_b_brightness_toggle()
{
    uint8_t current = hw_get_kb_backlight();
    if (current > 0) {
        hw_set_kb_backlight(0);
    } else {
        hw_set_kb_backlight(user_setting.keyboard_bl_level);
    }
}
#endif

void hw_init()
{
#ifdef ARDUINO
#if defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3_ULTRA)
    // Let the application handle a PEK long press instead of automatic power-off.
    bool long_press_shutdown_disabled = instance.disableLongPressShutdown();
    bool long_press_event_enabled = instance.enablePowerEvent(PowerEvent::IRQ_PEKEY_LONG_PRESSED, true);
    if (!long_press_shutdown_disabled || !long_press_event_enabled) {
        LILYGO_LOG_E("Failed to configure PMU long-press event");
    }
#endif

    playerQueue =  xQueueCreate(2, sizeof(audio_params_t));
    playerEvent =  xEventGroupCreate();

#ifndef EXCLUDE_LORA
    hw_radio_begin();
#endif

#ifdef USING_EXTERN_NRF2401
    hw_nrf24_begin();
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
    instance.attachKeyboardFeedback(true, 1);

    instance.setFeedbackCallback([](void *) {

        if (!_feedback_enable) {
            return;
        }
        instance.vibrator();
    });

    // Register ALT+B brightness toggle via new KeyComboManager API.
    // This migrates the previously hardcoded ALT+B brightness handling
    // from LilyGoKeyboard into user space, allowing users to customize
    // or replace this combo as needed.
    instance.kb.registerKeyCombo(ModifierKey::ALT, 'B', alt_b_brightness_toggle);
#endif //USING_INPUT_DEV_KEYBOARD


    xTaskCreate(playerTask, "app/play", 16 * 1024, NULL, 12, &playerTaskHandler);

    prefs.begin(NVS_NAME);
    if (!load_app_settings()) {
        LILYGO_LOG_E("App settings are invalid, use default settings");
        save_app_settings();
    }

    apply_app_settings(app_settings);
#if defined(ARDUINO) && FACTORY_HAS_HAPTIC_DRV
    instance.setHapticEffects(haptic_effect);
#endif
    apply_rotary_step_divider(rotary_step_divider);
    apply_audio_jack_mode(audio_jack_mode);
    // Restore the persisted timezone for localtime()/mktime() after reboot.
    configTime(timezone_offset_sec, daylight_offset_sec, "pool.ntp.org", "time.nist.gov");

    user_setting.charger_current = hw_get_charger_current();
    app_settings.user = user_setting;

    instance.getChargeConfig(device_limits.min_charge_current_ma,
                             device_limits.max_charge_current_ma,
                             device_limits.charge_current_step_ma,
                             device_limits.max_charge_level_index);
    print_user_setting("INIT", user_setting);

    // Attach device event
    instance.onEvent(POWER_EVENT, [](const DeviceEvent &event, void *user_data) {
        PMUEventType_t type = instance.getPMUEventType(event);
        if (type == PMU_EVENT_KEY_CLICKED || type == PMU_EVENT_KEY_LONG_PRESSED) {
            update_pmu_button_monitor_state(type);
        }
        if (type == PMU_EVENT_KEY_CLICKED) {
            LILYGO_LOG_D("ON EVENT PMU CLICK");
        }
#if defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3_ULTRA)
        if (type == PMU_EVENT_KEY_LONG_PRESSED) {
            LILYGO_LOG_D("ON EVENT PMU LONG PRESS");
            lilygo_power_menu_request();
        }
#endif
    });

    instance.onEvent(BUTTON_EVENT, [](const DeviceEvent &event, void *user_data) {
        const ButtonEventParam_t *p = instance.getButtonEventParam(event);
        if (p) {
            update_button_monitor_state(p->id, p->event);
        }
        if (instance.getButtonEventType(event) == BUTTON_EVENT_CLICK) {
            LILYGO_LOG_D("ON EVENT BUTTON CLICK");
        }
    });

#else
    set_default_app_settings(app_settings);
    apply_app_settings(app_settings);
#endif

}

void hw_get_user_setting(user_setting_params_t &param)
{
    param = user_setting;
}

void hw_set_user_setting(user_setting_params_t &param)
{
    user_setting_params_t previous = user_setting;
    if (!user_setting_has_changed(previous, param)) {
        return;
    }

    user_setting = param;
    app_settings.user = user_setting;
#ifdef ARDUINO
    save_app_settings();
#endif
    print_user_setting_changes(previous, user_setting);
    print_user_setting("SET", user_setting);
}

bool hw_get_nav_auto_hide_enabled(void)
{
    return nav_auto_hide_enabled;
}

void hw_set_nav_auto_hide_enabled(bool enabled)
{
    nav_auto_hide_enabled = enabled;
    app_settings.nav_auto_hide_enabled = enabled ? 1 : 0;
#ifdef ARDUINO
    save_app_settings();
#endif
    LILYGO_LOG_PRINTF("nav_auto_hide       :%u\n", (unsigned)enabled);
}

bool hw_get_keyboard_navigation_enabled(void)
{
#if defined(ARDUINO_T_LORA_PAGER)
    return keyboard_navigation_enabled;
#else
    return false;
#endif
}

void hw_set_keyboard_navigation_enabled(bool enabled)
{
    keyboard_navigation_enabled = enabled;
    app_settings.keyboard_navigation_mode = enabled ? KEYBOARD_NAV_ENABLED : KEYBOARD_NAV_DISABLED;
#ifdef ARDUINO
    save_app_settings();
#endif
    LILYGO_LOG_PRINTF("keyboard_navigation :%u\n", (unsigned)enabled);
}

bool hw_get_touch_guide_dismissed(void)
{
    return touch_guide_dismissed;
}

void hw_set_touch_guide_dismissed(bool dismissed)
{
    touch_guide_dismissed = dismissed;
    app_settings.touch_guide_dismissed = dismissed ? 1 : 0;
#ifdef ARDUINO
    save_app_settings();
#endif
}

const uint32_t hw_get_disp_timeout_ms()
{
    return user_setting.disp_timeout_second * 1000UL;
}

uint16_t hw_get_devices_nums()
{
    return sizeof(hw_devices) / sizeof(hw_devices[0]);
}

const char *hw_get_devices_name(int index)
{
    if (index > hw_get_devices_nums()) {
        return "NULL";
    }
    return hw_devices[index];
}

const char *hw_get_variant_name()
{
#ifdef ARDUINO
    const LilyGoDeviceCapability &capability = instance.getCapability();
    return capability.boardName ? capability.boardName : instance.getName();
#else
    return "LilyGo T-LoRa-Pager (2025)";
#endif
}


bool hw_get_mac(uint8_t *mac)
{
#ifdef ARDUINO
    esp_efuse_mac_get_default(mac);
    return true;
#endif
    return false;
}

void hw_get_wifi_ssid(char *param, size_t param_size)
{
#ifdef ARDUINO
    copy_cstr(param, param_size, WiFi.isConnected() ? WiFi.SSID().c_str() : "N.A");
#else
    copy_cstr(param, param_size, "NO CONFIG");
#endif
}


void hw_get_date_time(char *param, size_t param_size)
{
    if (!param || param_size == 0) {
        return;
    }
#ifdef ARDUINO
    static struct tm last_timeinfo;
    struct tm timeinfo;
#if FACTORY_HAS_RTC
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.getDateTime(&timeinfo);
        snprintf(param, param_size, "%04d/%02d/%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        if (memcmp(&last_timeinfo, &timeinfo, sizeof(struct tm)) == 0) {
            // Reset RTC
            Wire.beginTransmission(0x51);
            Wire.write(0x00);
            Wire.write(0x58);
            Wire.endTransmission();
            // instance.rtc.reset();
            delay(10);
            hw_write_rtc_from_system_time();
        }
        last_timeinfo = timeinfo;
    } else
#endif
    {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        snprintf(param, param_size, "%04d/%02d/%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
#else
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    snprintf(param, param_size, "%04d/%02d/%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
#endif
}

void hw_get_date_time(struct tm &timeinfo)
{
    memset(&timeinfo, 0, sizeof(timeinfo));
#ifdef ARDUINO
#if FACTORY_HAS_RTC
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.getDateTime(&timeinfo);
        return;
    }
#endif
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
#else
    time_t now;
    time(&now);
    timeinfo = *localtime(&now);
#endif
}

bool hw_set_date_time(const struct tm &timeinfo)
{
    struct tm next = timeinfo;
    next.tm_isdst = -1;
    time_t epoch = mktime(&next);
    if (epoch < 0) {
        return false;
    }

#ifdef ARDUINO
#if FACTORY_HAS_RTC
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.setDateTime(next);
        instance.rtc.hwClockRead();
        return true;
    }
#endif

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    return settimeofday(&tv, NULL) == 0;
#else
    return false;
#endif
}

bool hw_write_rtc_from_system_time()
{
#ifdef ARDUINO
#if FACTORY_HAS_RTC
    if (!(hw_get_device_online() & HW_RTC_ONLINE)) {
        return false;
    }
    instance.rtc.hwClockWrite();
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
}

int32_t hw_get_timezone_offset()
{
    return timezone_offset_sec;
}

int32_t hw_get_daylight_offset()
{
    return daylight_offset_sec;
}

void hw_set_timezone_offset(int32_t gmt_offset_sec, int32_t daylight_offset)
{
    timezone_offset_sec = gmt_offset_sec;
    daylight_offset_sec = daylight_offset;
    app_settings.timezone_offset_sec = timezone_offset_sec;
    app_settings.daylight_offset_sec = daylight_offset_sec;
#ifdef ARDUINO
    save_app_settings();
    configTime(timezone_offset_sec, daylight_offset_sec, "pool.ntp.org", "time.nist.gov");
#endif
}

bool hw_sync_time_from_ntp(int32_t gmt_offset_sec, int32_t daylight_offset,
                           const char *server1, const char *server2, uint32_t wait_ms)
{
#ifdef ARDUINO
    if (!WiFi.isConnected()) {
        return false;
    }

    hw_set_timezone_offset(gmt_offset_sec, daylight_offset);
    configTime(timezone_offset_sec, daylight_offset_sec,
               server1 && server1[0] ? server1 : "pool.ntp.org",
               server2 && server2[0] ? server2 : "time.nist.gov");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, wait_ms)) {
        return false;
    }

    hw_write_rtc_from_system_time();
    return true;
#else
    return false;
#endif
}


wl_status_t hw_get_wifi_status()
{
#ifdef ARDUINO
    return WiFi.status();
#else
    return WL_NO_SSID_AVAIL;
#endif
}

void hw_get_ip_address(char *param, size_t param_size)
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        copy_cstr(param, param_size, WiFi.localIP().toString().c_str());
        return;
    }
#endif
    copy_cstr(param, param_size, "N.A");
}

int16_t hw_get_wifi_rssi()
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        return (WiFi.RSSI());
    }
#endif
    return -99;
}

int16_t hw_get_battery_voltage()
{
#ifdef ARDUINO
    return instance.getBattVoltage();
#else
    return 0;
#endif
}

float hw_get_sd_size()
{
    float size = 0.0;
#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
    uint32_t online_mask = hw_get_device_online();
    if (!(online_mask & HW_SD_ONLINE)) {
        return 0.0;
    }
    size = SD.cardSize() / 1024 / 1024 / 1024.0;

#elif defined(USING_FATFS)
    size = FFat.totalBytes() / 1024 / 1024;
#endif

#endif
    return size;
}

void hw_get_arduino_version(char *param, size_t param_size)
{
    if (!param || param_size == 0) {
        return;
    }
#ifdef ARDUINO
    snprintf(param, param_size, "V%d.%d.%d",
             ESP_ARDUINO_VERSION_MAJOR,
             ESP_ARDUINO_VERSION_MINOR,
             ESP_ARDUINO_VERSION_PATCH);
#else
    copy_cstr(param, param_size, "V2.0.17");
#endif
}

void hw_set_gps_nmea_serial(bool enable)
{
#if defined(ARDUINO) && !defined(EXCLUDE_GPS)
    instance.gps.setNMEASerialOutput(enable);
#else
    (void)enable;
#endif
}


void hw_gps_attach_pps()
{
#ifdef GPS_PPS
    pinMode(GPS_PPS, INPUT);
    attachInterrupt(GPS_PPS, []() {
        pps_trigger = true;
    }, CHANGE);
#endif
}

void hw_gps_detach_pps()
{
#ifdef GPS_PPS
    detachInterrupt(GPS_PPS);
    pinMode(GPS_PPS, OPEN_DRAIN);
#endif
}

#if defined(ARDUINO) && !defined(EXCLUDE_GPS)
static gps_satellite_system_t map_gps_system(GNSSSatelliteSystem system)
{
    switch (system) {
    case GNSS_SYSTEM_GPS:
        return GPS_SAT_SYSTEM_GPS;
    case GNSS_SYSTEM_GLONASS:
        return GPS_SAT_SYSTEM_GLONASS;
    case GNSS_SYSTEM_BEIDOU:
        return GPS_SAT_SYSTEM_BEIDOU;
    case GNSS_SYSTEM_GALILEO:
        return GPS_SAT_SYSTEM_GALILEO;
    case GNSS_SYSTEM_QZSS:
        return GPS_SAT_SYSTEM_QZSS;
    default:
        return GPS_SAT_SYSTEM_UNKNOWN;
    }
}

static gps_antenna_state_t map_gps_antenna(GNSSAntennaState state)
{
    switch (state) {
    case GNSS_ANTENNA_OK:
        return GPS_ANTENNA_OK;
    case GNSS_ANTENNA_OPEN:
        return GPS_ANTENNA_OPEN;
    case GNSS_ANTENNA_SHORT:
        return GPS_ANTENNA_SHORT;
    default:
        return GPS_ANTENNA_UNKNOWN;
    }
}
#endif

bool hw_get_gps_info(gps_params_t &param)
{
#if defined(ARDUINO) && !defined(EXCLUDE_GPS)
    static uint32_t interval = 0;
    bool pps = pps_trigger;
    pps_trigger = false;

    bool nmea_to_serial = param.nmea_to_serial;

    if (!nmea_to_serial) {
        if (millis() < interval) {
            return false;
        }
        interval = millis() + 1000;
    }

    gps_params_t next = {};
    next.nmea_to_serial = nmea_to_serial;
    param = next;
    param.pps = pps;


    copy_cstr(param.model, sizeof(param.model), instance.gps.getModel().c_str());
    param.rx_size = instance.gps.loop(nmea_to_serial);

    bool location = instance.gps.location.isValid();
    bool datetime = (instance.gps.date.year() > 2000);
    bool speed = instance.gps.speed.isValid();
    bool altitude = instance.gps.altitude.isValid();

    param.location_valid = location;
    param.datetime_valid = datetime;
    param.speed_valid = speed;
    param.altitude_valid = altitude;
    param.ttff_ms = instance.gps.timeToFirstFix();
    param.ttff_valid = param.ttff_ms != UINT32_MAX;

    if (location) {
        param.lat = instance.gps.location.lat();
        param.lng = instance.gps.location.lng();
    }

    if (speed) {
        param.speed = instance.gps.speed.kmph();
    }

    if (altitude) {
        param.altitude = instance.gps.altitude.meters();
    }

    if (datetime) {
        /* A GPS time is only trusted for clock sync once a position fix exists. */
        if (location && !nmea_to_serial && !sync_date_time) {
            struct tm utc_tm = {0};
            utc_tm.tm_year = instance.gps.date.year() - 1900;
            utc_tm.tm_mon = instance.gps.date.month() - 1;
            utc_tm.tm_mday = instance.gps.date.day();
            utc_tm.tm_hour = instance.gps.time.hour();
            utc_tm.tm_min = instance.gps.time.minute();
            utc_tm.tm_sec = instance.gps.time.second();

            bool synced = false;
            time_t utc_timestamp;
            if (gps_utc_to_epoch(utc_tm, utc_timestamp)) {
#if FACTORY_HAS_RTC
                bool rtc_online = (hw_get_device_online() & HW_RTC_ONLINE) != 0;
                if (rtc_online) {
                    struct tm local_tm = utc_tm;
                    instance.rtc.convertUtcToTimezone(local_tm, timezone_offset_sec + daylight_offset_sec);
                    instance.rtc.setDateTime(local_tm);
                }
#endif
                struct timeval tv;
                tv.tv_sec = utc_timestamp;
                tv.tv_usec = 0;
                bool system_clock_set = settimeofday(&tv, NULL) == 0;
#if FACTORY_HAS_RTC
                /* Keep retrying RTC writes if the chip was temporarily offline. */
                synced = rtc_online && system_clock_set;
#else
                synced = system_clock_set;
#endif
            }

            /* Keep retrying after a transient RTC/system-clock failure. */
            if (synced) {
                sync_date_time = true;
            }
        }
        param.datetime.tm_year = instance.gps.date.year() - 1900;
        param.datetime.tm_mon = instance.gps.date.month() - 1;
        param.datetime.tm_mday = instance.gps.date.day();
        param.datetime.tm_hour = instance.gps.time.hour();
        param.datetime.tm_min =  instance.gps.time.minute();
        param.datetime.tm_sec = instance.gps.time.second();
    }

    if (instance.gps.satellites.isValid()) {
        param.satellite = instance.gps.satellites.value();
    }

    param.antenna_state = map_gps_antenna(instance.gps.getAntennaState());
    param.antenna_age = instance.gps.getAntennaAge();

    GNSSSatelliteInfo satellites[GPS_SIGNAL_MAX_SATELLITES];
    size_t satellite_count = instance.gps.copySatellites(satellites, GPS_SIGNAL_MAX_SATELLITES);
    param.signal_satellite_count = satellite_count;
    for (size_t i = 0; i < satellite_count; ++i) {
        param.signal_satellites[i].system = map_gps_system(satellites[i].system);
        param.signal_satellites[i].prn = satellites[i].prn;
        param.signal_satellites[i].elevation = satellites[i].elevation;
        param.signal_satellites[i].azimuth = satellites[i].azimuth;
        param.signal_satellites[i].cn0 = satellites[i].cn0;
        param.signal_satellites[i].has_cn0 = satellites[i].has_cn0;
        param.signal_satellites[i].used = satellites[i].used;
    }

    GNSSConstellationInfo constellations[GPS_SIGNAL_MAX_CONSTELLATIONS];
    size_t constellation_count = instance.gps.getConstellationInfo(constellations, GPS_SIGNAL_MAX_CONSTELLATIONS);
    param.constellation_count = constellation_count;
    for (size_t i = 0; i < constellation_count; ++i) {
        param.constellations[i].system = map_gps_system(constellations[i].system);
        param.constellations[i].visible = constellations[i].visible;
        param.constellations[i].tracking = constellations[i].tracking;
        param.constellations[i].used = constellations[i].used;
        param.constellations[i].max_cn0 = constellations[i].max_cn0;
        param.constellations[i].avg_cn0 = constellations[i].avg_cn0;
    }

    return nmea_to_serial ? false : (location && datetime);
#else
    copy_cstr(param.model, sizeof(param.model), "Dummy");
    param.lat = 0.0;
    param.lng = 0.0;
    param.speed = rand() % 120;
    param.altitude = 28.9;
    param.location_valid = true;
    param.datetime_valid = true;
    param.speed_valid = true;
    param.altitude_valid = true;
    param.ttff_ms = 1000;
    param.ttff_valid = true;
    param.rx_size = 366666;
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo  = localtime(&now);
    param.datetime = *timeinfo;
    param.satellite = rand() % 30;
    param.antenna_state = GPS_ANTENNA_OK;
    param.antenna_age = 0;
    param.constellation_count = GPS_SIGNAL_MAX_CONSTELLATIONS;
    static const gps_satellite_system_t systems[GPS_SIGNAL_MAX_CONSTELLATIONS] = {
        GPS_SAT_SYSTEM_GPS,
        GPS_SAT_SYSTEM_GLONASS,
        GPS_SAT_SYSTEM_BEIDOU,
        GPS_SAT_SYSTEM_GALILEO,
        GPS_SAT_SYSTEM_QZSS
    };
    uint16_t sat_index = 0;
    for (uint16_t i = 0; i < GPS_SIGNAL_MAX_CONSTELLATIONS; ++i) {
        param.constellations[i].system = systems[i];
        param.constellations[i].visible = 4 + i;
        param.constellations[i].tracking = 3 + i;
        param.constellations[i].used = 2;
        param.constellations[i].max_cn0 = 38 - i;
        param.constellations[i].avg_cn0 = 24 + i;
        for (uint16_t s = 0; s < param.constellations[i].visible && sat_index < GPS_SIGNAL_MAX_SATELLITES; ++s) {
            param.signal_satellites[sat_index].system = systems[i];
            param.signal_satellites[sat_index].prn = s + 1;
            param.signal_satellites[sat_index].elevation = 15 + s;
            param.signal_satellites[sat_index].azimuth = 40 * s;
            param.signal_satellites[sat_index].cn0 = 20 + s + i;
            param.signal_satellites[sat_index].has_cn0 = true;
            param.signal_satellites[sat_index].used = s < 2;
            sat_index++;
        }
    }
    param.signal_satellite_count = sat_index;
    return true;
#endif
}


uint32_t hw_get_device_online()
{
#ifdef ARDUINO
    return instance.getDeviceProbe();
#else
    uint32_t hw_online =   HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE;
#ifdef USING_INPUT_DEV_KEYBOARD
    hw_online |= HW_KEYBOARD_ONLINE;
#endif
    return hw_online;
#endif
}

bool hw_has_lora_hardware()
{
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER)
    return instance.isLoRaHardwarePresent();
#elif defined(ARDUINO) && defined(ARDUINO_T_DECK)
    return instance.hasRadio();
#elif FACTORY_HAS_RADIO
    return true;
#else
    return false;
#endif
}

void hw_disp_enable_backlight(bool enable)
{
    if (enable) {
        hw_set_disp_backlight(user_setting.brightness_level);
    } else {
        hw_set_disp_backlight(0);
    }
}

void hw_set_disp_backlight(uint8_t level)
{
#ifdef ARDUINO
    instance.setBrightness(level);
#endif
}

uint8_t hw_get_disp_backlight()
{
#ifdef ARDUINO
    return instance.getBrightness();
#else
    return 100;
#endif
}

bool hw_get_disp_is_on()
{
#ifdef ARDUINO
    return instance.getBrightness() != 0;
#else
    return true;
#endif
}

void hw_kb_enable_backlight(bool enable)
{
    if (enable) {
        hw_set_kb_backlight(user_setting.keyboard_bl_level);
    } else {
        hw_set_kb_backlight(0);
    }
}

void hw_set_kb_backlight(uint8_t level)
{
#if defined(ARDUINO) && (defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD))
    instance.kb.setBrightness(level);
#endif
}

void hw_set_led_backlight(uint8_t level)
{
#if defined(ARDUINO) && defined(USING_LED_INDICATOR)
    instance.setLedIndicatorBrightness(level);
#endif
}

uint8_t hw_get_kb_backlight()
{
#if defined(ARDUINO) && (defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD))
    return instance.kb.getBrightness();
#else
    return 100;
#endif
}

int16_t hw_set_wifi_scan()
{
#ifdef ARDUINO
    LILYGO_LOG_PRINTF("hw_set_wifi_scan\n");
    return  WiFi.scanNetworks(true);
#endif
    return 0;
}

bool hw_get_wifi_scanning()
{
#ifdef ARDUINO
    return WiFi.getStatusBits() & WIFI_SCANNING_BIT ;
#endif
    return false;
}


void hw_get_wifi_scan_result(vector < wifi_scan_params_t > &list)
{
    list.clear();
#ifdef ARDUINO
    int16_t nums = WiFi.scanComplete();
    if (nums < 0) {
        LILYGO_LOG_PRINTF("Nothing network found. return code : %d\n", nums);
        return;
    } else {
        LILYGO_LOG_PRINTF("find %d network\n", nums);
    }
    // uint8_t networkItem, String &ssid, uint8_t &encryptionType, int32_t &RSSI, uint8_t *&BSSID, int32_t &channel
    wifi_scan_params_t param = {};
    for (int i = 0; i < nums; ++i) {
        String ssid;
        uint8_t encryptionType;
        int32_t rssi;
        uint8_t *BSSID;
        int32_t channel;
        WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, BSSID, channel);
        LILYGO_LOG_PRINTF("SSID:%s RSSI:%d\n", ssid.c_str(), rssi);
        param.authmode = encryptionType;
        copy_cstr(param.ssid, sizeof(param.ssid), ssid.c_str());
        param.rssi = rssi;
        param.channel = channel;
        memcpy(param.bssid, BSSID, 6);
        list.push_back(param);
    }
#else
    wifi_scan_params_t param = {};
    param.authmode = 1;
    copy_cstr(param.ssid, sizeof(param.ssid), "LilyGo-AABB0");
    param.rssi = -10;
    param.channel = 0;
    list.push_back(param);
#endif
}

void hw_set_wifi_connect(wifi_conn_params_t &params)
{
    LILYGO_LOG_PRINTF("hw_set_wifi_connect:ssid:<%s> password <%s>\n", params.ssid, params.password);
#ifdef ARDUINO
    String ssid = params.ssid;
    String password = params.password;
    LILYGO_LOG_PRINT("SSID :"); LILYGO_LOG_PRINTLN(ssid);
    LILYGO_LOG_PRINT("PWD :"); LILYGO_LOG_PRINTLN(password);
    WiFi.begin(ssid, password);
#endif
}

bool hw_get_wifi_connected()
{
#ifdef ARDUINO
    return WiFi.isConnected();
#endif
    return false;
}

#ifdef ARDUINO
static bool has_audio_file_ext(const char *file_name)
{
    if (!file_name) {
        return false;
    }
    const char *dot = strrchr(file_name, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".mp3") == 0 ||
           strcasecmp(dot, ".wav") == 0 ||
           strcasecmp(dot, ".flac") == 0 ||
           strcasecmp(dot, ".fla") == 0;
}

static void listDir(vector < AudioParams_t > &list, fs::FS &fs, const char *dirname, uint8_t levels, audio_source_type_t source_type)
{
    LILYGO_LOG_PRINTF("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        LILYGO_LOG_PRINTLN("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        LILYGO_LOG_PRINTLN(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        const char *file_name = file.name();
        if (file.isDirectory()) {
            LILYGO_LOG_PRINT("  DIR : ");
            LILYGO_LOG_PRINTLN(file_name);
            if (levels) {
                char next_dir[AUDIO_FILE_NAME_MAX_LEN];
                if (strcmp(dirname, "/") == 0) {
                    snprintf(next_dir, sizeof(next_dir), "/%s", file_name);
                } else {
                    snprintf(next_dir, sizeof(next_dir), "%s/%s", dirname, file_name);
                }
                listDir(list, fs, next_dir, levels - 1, source_type);
            }
        } else {
            if (has_audio_file_ext(file_name)) {
                appendAudioFile(list, source_type, file_name);
            }

            LILYGO_LOG_PRINT("  FILE: ");
            LILYGO_LOG_PRINT(file_name);
            LILYGO_LOG_PRINT("\tSIZE: ");
            LILYGO_LOG_PRINTLN(file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}
#endif

void hw_fat_list(vector < AudioParams_t > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO)
    LILYGO_LOG_PRINTF("FFAT Listing directory: %s\n", dirname);
    listDir(list, FFat, dirname, levels, AUDIO_SOURCE_FATFS);
#endif
}

bool hw_sd_list(vector < AudioParams_t > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    if (instance.installSD()) {
        LILYGO_LOG_PRINTLN("SD Card mount success.");
    } else {
        LILYGO_LOG_PRINTLN("SD Card mount failed.");
        return false;
    }
    if (!instance.lockSPI()) {
        LILYGO_LOG_PRINTLN("SD Card lock failed.");
        return false;
    }
    listDir(list, SD, dirname, levels, AUDIO_SOURCE_SDCARD);
    instance.unlockSPI();
#endif
    return true;
}

static bool hw_sd_ready()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    bool ready = false;
    if (instance.lockSPI(pdMS_TO_TICKS(100))) {
        ready = SD.sectorSize() != 0 && SD.cardType() != CARD_NONE;
        instance.unlockSPI();
    }
    return ready;
#else
    return false;
#endif
}

bool hw_is_sd_insert()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    return hw_sd_ready();
#endif
    return false;
}

bool hw_has_sd_detect_pin()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
#if defined(ARDUINO_T_WATCH_S3_ULTRA) && defined(EXPANDS_SD_DET)
    return true;
#elif defined(ARDUINO_T_LORA_PAGER) && defined(EXPANDS_SD_DET)
    return true;
#elif defined(ARDUINO_T_DECK_V2) && defined(TCA_DIR_SD_DETECT)
    return true;
#endif
#endif
    return false;
}

bool hw_is_sd_card_inserted()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
#if defined(ARDUINO_T_WATCH_S3_ULTRA) && defined(EXPANDS_SD_DET)
    instance.io.pinMode(EXPANDS_SD_DET, INPUT);
    return instance.io.digitalRead(EXPANDS_SD_DET) == LOW;
#elif defined(ARDUINO_T_LORA_PAGER) && defined(EXPANDS_SD_DET)
    instance.io.pinMode(EXPANDS_SD_DET, INPUT);
    return instance.io.digitalRead(EXPANDS_SD_DET) == LOW;
#elif defined(ARDUINO_T_DECK_V2) && defined(TCA_DIR_SD_DETECT)
#if defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_get_device_online() & HW_KEYBOARD_ONLINE) {
        instance.kb.pinMode(TCA_DIR_SD_DETECT, INPUT_PULLUP);
        return instance.kb.digitalRead(TCA_DIR_SD_DETECT) == LOW;
    }
#endif
    return instance.isCardReady();
#else
    return hw_sd_ready();
#endif
#else
    return false;
#endif
}

uint32_t hw_get_sd_default_spi_freq()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    return LILYGO_WATCH_ULTRA_SD_SPI_FREQ;
#elif defined(ARDUINO_T_DECK_V2)
    return LILYGO_DECK_V2_SD_SPI_FREQ;
#elif defined(ARDUINO_T_DECK)
    return LILYGO_TDECK_SD_SPI_FREQ;
#elif defined(ARDUINO_T_LORA_PAGER)
    return LILYGO_LORA_PAGER_SD_SPI_FREQ;
#elif defined(ARDUINO_TWATCH_BASE)
    return LILYGO_WATCH_SD_SPI_FREQ;
#endif
#endif
    return 0;
}

static void hw_add_sd_mount_freq(uint32_t *freqs, uint8_t max_count, uint8_t *count, uint32_t freq)
{
    if (!freqs || !count || freq == 0 || *count >= max_count) return;
    for (uint8_t i = 0; i < *count; ++i) {
        if (freqs[i] == freq) return;
    }
    freqs[(*count)++] = freq;
}

uint8_t hw_get_sd_mount_freq_list(uint32_t *freqs, uint8_t max_count)
{
    uint8_t count = 0;
    if (!freqs || max_count == 0) return 0;

    hw_add_sd_mount_freq(freqs, max_count, &count, hw_get_sd_default_spi_freq());
    hw_add_sd_mount_freq(freqs, max_count, &count, 40000000U);
    hw_add_sd_mount_freq(freqs, max_count, &count, 25000000U);
    hw_add_sd_mount_freq(freqs, max_count, &count, 20000000U);
    hw_add_sd_mount_freq(freqs, max_count, &count, 10000000U);
    hw_add_sd_mount_freq(freqs, max_count, &count, 4000000U);
    hw_add_sd_mount_freq(freqs, max_count, &count, 1000000U);
    return count;
}

bool hw_mount_sd(uint32_t spi_freq)
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    if (hw_get_device_online() & HW_SD_UNAVAILABLE) {
        LILYGO_LOG_PRINTLN("[SD] mount skipped, unavailable with detected hardware");
        return false;
    }
    if (hw_has_sd_detect_pin() && !hw_is_sd_card_inserted()) {
        LILYGO_LOG_PRINTF("[SD] mount skipped, no card detected, freq:%lu\n", (unsigned long)spi_freq);
        return false;
    }
    if (hw_sd_ready()) {
        LILYGO_LOG_PRINTF("[SD] mount skipped, already ready, requested freq:%lu\n", (unsigned long)spi_freq);
        return true;
    }
    bool mounted = instance.installSD(spi_freq);
    LILYGO_LOG_PRINTF("[SD] mount %s, freq:%lu\n", mounted ? "success" : "failed", (unsigned long)spi_freq);
    return mounted;
#endif
    return false;
}

void hw_unmount_sd()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    LILYGO_LOG_PRINTLN("[SD] unmount requested");
#if defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_DECK_V2) || \
    defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK)
    instance.uninstallSD();
#else
    SD.end();
#endif
#endif
}

void hw_get_filesystem_music(vector < AudioParams_t > &list)
{
    list.clear();

#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
    LILYGO_LOG_PRINTLN("\n================== SD Music List ==================");
    hw_sd_list(list, "/", 0);
#endif

    LILYGO_LOG_PRINTLN("\n================== FFat Music List ==================");
    hw_fat_list(list, "/", 0);

#else
    appendAudioFile(list, AUDIO_SOURCE_FATFS, "/abc.mp3");
    appendAudioFile(list, AUDIO_SOURCE_FATFS, "/ccc.mp3");
    appendAudioFile(list, AUDIO_SOURCE_FATFS, "/ddd.mp3");
#endif
}

void hw_get_network_audio_streams(vector < NetworkAudioStreamParams_t > &list)
{
    list.clear();

#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    vector < NetworkAudioStreamParams_t > sd_list;
    bool sd_ready = instance.installSD();
    if (sd_ready && instance.lockSPI()) {
        File file = SD.open(NETWORK_AUDIO_CONFIG_PATH, FILE_READ);
        if (!file) {
            writeDefaultNetworkStreamConfig();
        } else {
            while (file.available()) {
                String line = file.readStringUntil('\n');
                NetworkAudioStreamParams_t stream;
                if (parseNetworkStreamLine(line, stream)) {
                    sd_list.push_back(stream);
                }
            }
            file.close();
        }
        instance.unlockSPI();
    }

    if (!sd_list.empty()) {
        list = sd_list;
        return;
    }
#endif

    appendDefaultNetworkStreams(list);
}

void hw_set_sd_music_play(audio_source_type_t source_type, const char *filename)
{
    audio_params_t params = {
        .event = APP_EVENT_PLAY,
        .filename = filename,
        .source_type = source_type
    };
    LILYGO_LOG_PRINTF("hw_set_sd_music_play : %s source_type:%d\n", filename, source_type);
#ifdef ARDUINO
    xQueueReset(playerQueue);
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
    xEventGroupSetBits(playerEvent, PLAYER_END);
    if (xQueueSend(playerQueue, &params, 0) == pdPASS) {
        LILYGO_LOG_PRINTLN("hw_set_sd_music_play send done\n");
    } else {
        LILYGO_LOG_PRINTLN("hw_set_sd_music_play send failed\n");
    }
#endif
}

void hw_set_network_audio_stream_play(const char *name, const char *url)
{
    if (!name || !url) {
        return;
    }

#ifdef ARDUINO
    char *name_copy = strdup(name);
    char *url_copy = strdup(url);
    if (!name_copy || !url_copy) {
        if (name_copy) {
            free(name_copy);
        }
        if (url_copy) {
            free(url_copy);
        }
        return;
    }

    audio_params_t params = {
        .event = APP_EVENT_PLAY_STREAM,
        .filename = name_copy,
        .url = url_copy,
        .source_type = AUDIO_SOURCE_FATFS
    };

    xQueueReset(playerQueue);
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
    xEventGroupSetBits(playerEvent, PLAYER_END);
    if (xQueueSend(playerQueue, &params, 0) != pdPASS) {
        free(name_copy);
        free(url_copy);
    }
#else
    (void)name;
    (void)url;
#endif
}

void hw_play_boot_sound_async()
{
#ifdef ARDUINO
    if (!playerQueue || !playerEvent || hw_player_running()) {
        return;
    }
    if (!FFat.exists(BOOT_SOUND_PATH)) {
        LILYGO_LOG_PRINTF("Boot sound file not found: %s\n", BOOT_SOUND_PATH);
        return;
    }
    audio_params_t params = {
        .event = APP_EVENT_PLAY_BOOT_SOUND,
        .filename = BOOT_SOUND_PATH,
        .url = NULL,
        .source_type = AUDIO_SOURCE_FATFS
    };
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
    xQueueSend(playerQueue, &params, 0);
#endif
}

void hw_set_play_stop_async()
{
#ifdef ARDUINO
    xQueueReset(playerQueue);
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
    xEventGroupSetBits(playerEvent, PLAYER_END);
#endif
}

void hw_set_play_stop()
{
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
    xEventGroupSetBits(playerEvent, PLAYER_END);
    uint32_t started = millis();
    while (hw_player_running() && millis() - started < AUDIO_STOP_WAIT_TIMEOUT_MS) {
        delay(2);
    }
    if (hw_player_running()) {
        LILYGO_LOG_W("Audio stop is still pending");
    }
#endif
}

void hw_set_sd_music_pause()
{
    LILYGO_LOG_PRINTF("playerTaskHandler pause!\n");
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
#endif
}

void hw_set_sd_music_resume()
{
    LILYGO_LOG_PRINTF("playerTaskHandler resume!\n");
#ifdef ARDUINO
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
#endif
}

bool hw_player_running()
{
#ifdef ARDUINO
    return playerEvent && (xEventGroupGetBits(playerEvent) & PLAYER_RUNNING);
#endif
    return true;
}

void hw_set_volume(uint8_t volume)
{
#ifdef ARDUINO
    AudioOutputIf *out = instance.getAudioOutput();
    if (out) {
        InstanceLockGuard lock;
        out->setVolume(volume);
    }
#endif
}

uint8_t hw_get_volume()
{
#ifdef ARDUINO
    AudioOutputIf *out = instance.getAudioOutput();
    if (out) {
        InstanceLockGuard lock;
        return out->getVolume();
    }
    return 100;
#else
    return 100;
#endif
}

void hw_audio_beep(uint16_t frequency_hz, uint16_t duration_ms)
{
#ifdef ARDUINO
    if (frequency_hz == 0 || duration_ms == 0 ||
            !playerQueue || !playerEvent ||
            (playerEvent && (xEventGroupGetBits(playerEvent) & PLAYER_RUNNING))) {
        return;
    }
    LILYGO_LOG_PRINTLN("Beep");
    audio_params_t params = {};
    params.event = APP_EVENT_PLAY_BEEP;
    params.frequency_hz = frequency_hz;
    params.duration_ms = duration_ms;
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
    xQueueSend(playerQueue, &params, 0);
#else
    (void)frequency_hz;
    (void)duration_ms;
#endif
}

uint8_t hw_get_codec_input_channels()
{
#ifdef ARDUINO
    return instance.getCodecInputChannels();
#else
    return 1;
#endif
}

float hw_get_mic_gain()
{
#ifdef ARDUINO
    AudioInputIf *in = instance.getAudioInput();
    if (in) {
        InstanceLockGuard lock;
        return in->getMicGain();
    }
    return 0.0f;
#else
    return 0.0f;
#endif
}

void hw_set_mic_gain(float gain)
{
#ifdef ARDUINO
    AudioInputIf *in = instance.getAudioInput();
    if (in) {
        InstanceLockGuard lock;
        in->setMicGain(gain);
    }
#endif
}

bool hw_has_mic_input_source_setting()
{
#if defined(ARDUINO) && defined(T_DECK_V2_REV07) && defined(USING_AUDIO_CODEC)
    return true;
#else
    return false;
#endif
}

void hw_set_mic_input_source(uint8_t source)
{
    uint8_t applied = clamp_mic_input_source(source);
    mic_input_source = applied;
#if defined(ARDUINO)
    apply_mic_input_source(applied);
#endif
    app_settings.mic_input_source = applied;
#ifdef ARDUINO
    save_app_settings();
#endif
    LILYGO_LOG_PRINTF("mic_input_source    :%u\n", (unsigned)applied);
}

uint8_t hw_get_mic_input_source()
{
    return clamp_mic_input_source(mic_input_source);
}

const char *hw_get_mic_input_source_name()
{
    return hw_get_mic_input_source() == MIC_INPUT_SOURCE_JACK ? "3.5mm" : "Internal";
}

bool hw_apply_mic_input_source()
{
    return apply_mic_input_source(mic_input_source);
}

void hw_shutdown()
{
#ifdef ARDUINO
    if (hw_get_power_off_mode() == HW_POWER_OFF_DEEP_SLEEP) {
        hw_sleep();
        return;
    }
    instance.decrementBrightness(0, 5, false);
    instance.shutdown();
#endif
}

hw_power_off_mode_t hw_get_power_off_mode()
{
#ifdef ARDUINO
    switch (instance.getPmicType()) {
    case PMIC_TYPE_AXP202:
    case PMIC_TYPE_AXP2101:
        return HW_POWER_OFF_SHUTDOWN;
    case PMIC_TYPE_BQ25896:
        return HW_POWER_OFF_SHIP_MODE;
    default:
        return HW_POWER_OFF_DEEP_SLEEP;
    }
#else
    return HW_POWER_OFF_SHUTDOWN;
#endif
}

const char *hw_get_power_controller_name()
{
#ifdef ARDUINO
    switch (instance.getPmicType()) {
    case PMIC_TYPE_AXP202:
        return "AXP202";
    case PMIC_TYPE_AXP2101:
        return "AXP2101";
    case PMIC_TYPE_BQ25896:
        return "BQ25896";
    default:
        return "No PMIC";
    }
#else
    return "Simulator PMIC";
#endif
}

bool hw_can_shutdown()
{
#ifdef ARDUINO
    if (hw_get_power_off_mode() == HW_POWER_OFF_SHIP_MODE) {
        return !instance.isAdapterConnected();
    }
#endif
    return true;
}

bool hw_adapter_is_connected()
{
#ifdef ARDUINO
    return instance.isAdapterConnected();
#endif
    return false;
}

void hw_sleep()
{
#ifdef ARDUINO
    if (playerTaskHandler) {
        vTaskDelete(playerTaskHandler);
        playerTaskHandler = NULL;
    }
    {
        AudioInputIf *in = instance.getAudioInput();
        if (in) in->end();
    }
    {
        AudioOutputIf *out = instance.getAudioOutput();
        if (out) out->end();
    }
    instance.decrementBrightness(0, 5, false);
    instance.sleep();
#endif
}

bool hw_get_otg_enable()
{
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
    return  instance.isOTGEnabled();
#else
    return false;
#endif
}

bool hw_set_otg(bool enable)
{
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
    if (enable) {
        return  instance.enableOTG();
    } else {
        instance.disableOTG();
    }
    return true;
#endif
    return false;
}

bool hw_get_charge_enable()
{
#ifdef ARDUINO
    return  instance.isEnableCharge();
#endif
    return false;
}

void hw_set_charger(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        instance.enableCharge();
    } else {
        instance.disableCharge();
    }
#endif
}

uint16_t hw_get_charger_current()
{
#ifdef ARDUINO
    return  instance.getChargeCurrent();
#else
    return 0;
#endif
}

uint8_t hw_get_charger_current_level()
{
#ifdef ARDUINO
    return instance.getChargeCurrentToLevel();
#else
    return 0;
#endif
}

uint16_t hw_set_charger_current_level(uint8_t level)
{
#ifdef ARDUINO

    uint16_t current =  instance.getChargeLevelToCurrent(level);

    LILYGO_LOG_PRINTF("Set charger current to %u mA  level:%d \n", current, level);

    instance.setChargeCurrent(current);

    return instance.getChargeCurrent();
#else

    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    if (level > (sizeof(table) / sizeof(table[0]) - 1)) {
        level = sizeof(table) / sizeof(table[0]) - 1;
    }
    LILYGO_LOG_PRINTF("set charge current:%u mA\n", table[level]);
    return  table[level];
#endif

}

static void power_metric_set(power_monitor_metric_t &metric, float value, power_monitor_source_t source)
{
    if (isnan(value) || isinf(value)) {
        return;
    }
    metric.valid = true;
    metric.value = value;
    metric.source = source;
}

#ifdef ARDUINO
static power_monitor_source_t power_metric_source_from_device(LilyGoPowerMetricSource source)
{
    switch (source) {
    case LILYGO_POWER_SRC_PMU:
        return POWER_MONITOR_SRC_PMU;
    case LILYGO_POWER_SRC_EXTERNAL_GAUGE:
        return POWER_MONITOR_SRC_EXTERNAL_GAUGE;
    case LILYGO_POWER_SRC_INTERNAL_GAUGE:
        return POWER_MONITOR_SRC_INTERNAL_GAUGE;
    case LILYGO_POWER_SRC_ESTIMATED:
        return POWER_MONITOR_SRC_ESTIMATED;
    case LILYGO_POWER_SRC_ADC:
        return POWER_MONITOR_SRC_ADC;
    default:
        return POWER_MONITOR_SRC_NONE;
    }
}

static void power_metric_copy(power_monitor_metric_t &dst, const LilyGoPowerMetric &src)
{
    if (!src.valid) {
        return;
    }
    dst.valid = true;
    dst.value = src.value;
    dst.source = power_metric_source_from_device(src.source);
}

static void power_snapshot_copy(power_monitor_snapshot_t &dst, const LilyGoPowerSnapshot &src)
{
    dst.online_mask = src.onlineMask;
    copy_cstr(dst.pmic_name, sizeof(dst.pmic_name), src.pmicName);
    copy_cstr(dst.gauge_name, sizeof(dst.gauge_name), src.gaugeName);
    dst.pmu_present = src.pmuPresent;
    dst.external_gauge_present = src.externalGaugePresent;
    dst.fuel_gauge_present = src.fuelGaugePresent;
    dst.battery_present_valid = src.batteryPresentValid;
    dst.battery_present = src.batteryPresent;
    dst.vbus_present_valid = src.vbusPresentValid;
    dst.vbus_present = src.vbusPresent;
    dst.charging = src.charging;
    dst.charge_done = src.chargeDone;
    dst.charge_fault = src.chargeFault;
    dst.charge_enabled_valid = src.chargeEnabledValid;
    dst.charge_enabled = src.chargeEnabled;
    dst.otg_supported = src.otgSupported;
    dst.otg_enabled = src.otgEnabled;
    copy_cstr(dst.charge_state, sizeof(dst.charge_state), src.chargeState);
    copy_cstr(dst.ntc_state, sizeof(dst.ntc_state), src.ntcState);
    power_metric_copy(dst.vbus_mv, src.vbusMv);
    power_metric_copy(dst.vbus_ma, src.vbusMa);
    power_metric_copy(dst.sys_mv, src.sysMv);
    power_metric_copy(dst.battery_mv, src.batteryMv);
    power_metric_copy(dst.battery_ma, src.batteryMa);
    power_metric_copy(dst.battery_percent, src.batteryPercent);
    power_metric_copy(dst.temperature_c, src.temperatureC);
    power_metric_copy(dst.battery_temperature_c, src.batteryTemperatureC);
    power_metric_copy(dst.instantaneous_power_w, src.instantaneousPowerW);
    power_metric_copy(dst.average_power_mw, src.averagePowerMw);
    power_metric_copy(dst.remaining_capacity_mah, src.remainingCapacityMah);
    power_metric_copy(dst.full_charge_capacity_mah, src.fullChargeCapacityMah);
    power_metric_copy(dst.design_capacity_mah, src.designCapacityMah);
    power_metric_copy(dst.standby_current_ma, src.standbyCurrentMa);
    power_metric_copy(dst.max_load_current_ma, src.maxLoadCurrentMa);
    power_metric_copy(dst.time_to_empty_min, src.timeToEmptyMin);
    power_metric_copy(dst.time_to_full_min, src.timeToFullMin);
}
#endif

static uint16_t power_metric_u16(const power_monitor_metric_t &metric)
{
    if (!metric.valid || metric.value <= 0.0f) {
        return 0;
    }
    if (metric.value >= 65535.0f) {
        return 65535;
    }
    return static_cast<uint16_t>(metric.value + 0.5f);
}

static int16_t power_metric_i16(const power_monitor_metric_t &metric)
{
    if (!metric.valid) {
        return 0;
    }
    if (metric.value > 32767.0f) {
        return 32767;
    }
    if (metric.value < -32768.0f) {
        return -32768;
    }
    return static_cast<int16_t>(metric.value);
}

void hw_get_power_monitor_snapshot(power_monitor_snapshot_t &snapshot)
{
    snapshot = power_monitor_snapshot_t();
    copy_cstr(snapshot.charge_state, sizeof(snapshot.charge_state), "Unavailable");
    copy_cstr(snapshot.ntc_state, sizeof(snapshot.ntc_state), "Unknown");
#ifdef ARDUINO
    LilyGoPowerSnapshot device_snapshot;
    instance.readPowerSnapshot(device_snapshot);
    power_snapshot_copy(snapshot, device_snapshot);
#else
    snapshot.online_mask = HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE;
    snapshot.pmu_present = true;
    copy_cstr(snapshot.pmic_name, sizeof(snapshot.pmic_name), "Simulator PMU");
    copy_cstr(snapshot.gauge_name, sizeof(snapshot.gauge_name), "Simulator");
    snapshot.fuel_gauge_present = true;
    snapshot.vbus_present_valid = true;
    snapshot.vbus_present = true;
    snapshot.battery_present_valid = true;
    snapshot.battery_present = true;
    snapshot.charging = true;
    copy_cstr(snapshot.charge_state, sizeof(snapshot.charge_state), "Fast charging");
    copy_cstr(snapshot.ntc_state, sizeof(snapshot.ntc_state), "Normal");
    power_metric_set(snapshot.battery_percent, 30 + rand() % (100 - 30 + 1), POWER_MONITOR_SRC_ESTIMATED);
    power_metric_set(snapshot.battery_mv, 4178, POWER_MONITOR_SRC_ESTIMATED);
    power_metric_set(snapshot.vbus_mv, 4998, POWER_MONITOR_SRC_ESTIMATED);
    power_metric_set(snapshot.sys_mv, 4100, POWER_MONITOR_SRC_ESTIMATED);
    power_metric_set(snapshot.temperature_c, 32.0f, POWER_MONITOR_SRC_ESTIMATED);
#endif
}

void hw_get_monitor_params(monitor_params_t &params)
{
    power_monitor_snapshot_t snapshot;
    hw_get_power_monitor_snapshot(snapshot);

    params = monitor_params_t();
    params.has_gauge = snapshot.external_gauge_present;
    params.charging = snapshot.charging;
    copy_cstr(params.charge_state, sizeof(params.charge_state), snapshot.charge_state);
    params.sys_voltage = power_metric_u16(snapshot.sys_mv);
    params.battery_voltage = power_metric_u16(snapshot.battery_mv);
    params.usb_voltage = power_metric_u16(snapshot.vbus_mv);
    params.battery_percent = snapshot.battery_percent.valid ? static_cast<int>(snapshot.battery_percent.value + 0.5f) : 0;
    params.temperature = snapshot.temperature_c.valid ? snapshot.temperature_c.value : 0.0f;
    params.remainingCapacity = power_metric_u16(snapshot.remaining_capacity_mah);
    params.fullChargeCapacity = power_metric_u16(snapshot.full_charge_capacity_mah);
    params.designCapacity = power_metric_u16(snapshot.design_capacity_mah);
    params.instantaneousCurrent = power_metric_i16(snapshot.battery_ma);
    params.instantaneousPower = snapshot.instantaneous_power_w.valid ? snapshot.instantaneous_power_w.value : 0.0f;
    params.standbyCurrent = power_metric_i16(snapshot.standby_current_ma);
    params.averagePower = power_metric_i16(snapshot.average_power_mw);
    params.maxLoadCurrent = power_metric_i16(snapshot.max_load_current_ma);
    params.timeToEmpty = power_metric_u16(snapshot.time_to_empty_min);
    params.timeToFull = power_metric_u16(snapshot.time_to_full_min);
    copy_cstr(params.ntc_state, sizeof(params.ntc_state), snapshot.ntc_state);
}

static imu_params_t imu_params = {};

#if defined(ARDUINO) && defined(USING_BHI260_SENSOR)
static uint16_t bhi260_imu_process_refs = 0;
static bool bhi260_imu_process_configured = false;
static constexpr float BHI260_FACTORY_IMU_SAMPLE_RATE_HZ = 50.0f;
#endif

static void hw_set_imu_accel(float x, float y, float z)
{
    imu_params.accel_valid = true;
    imu_params.accel_x = x;
    imu_params.accel_y = y;
    imu_params.accel_z = z;
    imu_params.accel_magnitude = sqrtf(x * x + y * y + z * z);
    imu_params.accel_sequence++;
}

sensor_type_t hw_get_sensor_type()
{
#if defined(USING_BHI260_SENSOR)
    return SENSOR_TYPE_IMU;
#else
    return SENSOR_TYPE_ACCEL;
#endif
}

// sensor ori to lv_align_t
void _map_ori_lvgl(imu_params_t &params)
{
    params.reverse  = false;
#if defined(ARDUINO_TWATCH_BASE)
    switch (params.orientation) {
    case 0: params.orientation =  1; break;
    case 1: params.orientation =  3; break;
    case 2: params.orientation =  6; break;
    case 3: params.orientation =  4; break;
    case 4:
        params.orientation =  9;
        params.reverse  = true;
        break;
    case 5:
        params.orientation =  9;
        break;
    default: break;
    }
#elif defined(ARDUINO_T_WATCH_S3)
    switch (params.orientation) {
    case 0: params.orientation =  1; break;
    case 1: params.orientation =  3; break;
    case 2: params.orientation =  6; break;
    case 3: params.orientation =  4; break;
    case 4:
        params.orientation =  9;
        params.reverse  = true;
        break;
    case 5:
        params.orientation =  9;
        break;
    default: break;
    }
#elif defined(ARDUINO_TWATCH_2020_V3)
    switch (params.orientation) {
    case 0: params.orientation =  1; break;
    case 1: params.orientation =  3; break;
    case 2: params.orientation =  6; break;
    case 3: params.orientation =  4; break;
    case 4:
        params.orientation =  9;
        params.reverse  = true;
        break;
    case 5:
        params.orientation =  9;
        break;
    default: break;
    }
#endif
}

void hw_get_imu_params(imu_params_t &params)
{
    params = imu_params_t();
#ifdef ARDUINO
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        params =  imu_params;
    }
#elif defined(USING_BMA423_SENSOR)
    AccelerometerData  accelData;
    if (instance.readAccelerometerData(accelData)) {
        params.orientation = AccelerometerUtils::getDirection(accelData);
        hw_set_imu_accel(accelData.mps2.x, accelData.mps2.y, accelData.mps2.z);
        params.accel_valid = imu_params.accel_valid;
        params.accel_x = imu_params.accel_x;
        params.accel_y = imu_params.accel_y;
        params.accel_z = imu_params.accel_z;
        params.accel_magnitude = imu_params.accel_magnitude;
        params.accel_sequence = imu_params.accel_sequence;
        LILYGO_LOG_PRINTF("ori:%d\n", params.orientation);
        _map_ori_lvgl(params);
    }
#endif
#else
    params =  imu_params;
#endif //ARDUINO
}

#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
static bma_activity_t map_bma_activity(ActivityType activity)
{
    switch (activity) {
    case ActivityType::STATIONARY:
        return BMA_ACTIVITY_STATIONARY;
    case ActivityType::WALKING:
        return BMA_ACTIVITY_WALKING;
    case ActivityType::RUNNING:
        return BMA_ACTIVITY_RUNNING;
    default:
        return BMA_ACTIVITY_UNKNOWN;
    }
}

static bma_sensor_event_t map_bma_runtime_event(BMASensorRuntimeEvent event)
{
    switch (event) {
    case BMASensorRuntimeEvent::STEP:
        return BMA_SENSOR_EVENT_STEP;
    case BMASensorRuntimeEvent::SINGLE_TAP:
        return BMA_SENSOR_EVENT_SINGLE_TAP;
    case BMASensorRuntimeEvent::DOUBLE_TAP:
        return BMA_SENSOR_EVENT_DOUBLE_TAP;
    case BMASensorRuntimeEvent::TRIPLE_TAP:
        return BMA_SENSOR_EVENT_TRIPLE_TAP;
    case BMASensorRuntimeEvent::ACTIVITY:
        return BMA_SENSOR_EVENT_ACTIVITY;
    case BMASensorRuntimeEvent::TILT:
        return BMA_SENSOR_EVENT_TILT;
    case BMASensorRuntimeEvent::ANY_MOTION:
        return BMA_SENSOR_EVENT_ANY_MOTION;
    case BMASensorRuntimeEvent::NO_MOTION:
        return BMA_SENSOR_EVENT_NO_MOTION;
    case BMASensorRuntimeEvent::DATA_READY:
        return BMA_SENSOR_EVENT_DATA_READY;
    default:
        return BMA_SENSOR_EVENT_NONE;
    }
}
#endif

void hw_get_bma_sensor_snapshot(bma_sensor_snapshot_t &snapshot)
{
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.activity = BMA_ACTIVITY_UNKNOWN;
    snapshot.last_event = BMA_SENSOR_EVENT_NONE;

#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
    if (!(hw_get_device_online() & HW_BMA_ONLINE)) {
        return;
    }

    BMASensorRuntimeStatus_t status = instance.getRuntimeStatus();
    snapshot.valid = true;
    snapshot.accel_valid = status.dataValid;
    snapshot.accel_x = status.accel.mps2.x;
    snapshot.accel_y = status.accel.mps2.y;
    snapshot.accel_z = status.accel.mps2.z;
    snapshot.magnitude = status.magnitude;
    snapshot.peak_magnitude = status.peakMagnitude;
    snapshot.step_count = status.stepCount;
    snapshot.step_events = status.stepEvents;
    snapshot.single_taps = status.singleTaps;
    snapshot.double_taps = status.doubleTaps;
    snapshot.triple_taps = status.tripleTaps;
    snapshot.activity_events = status.activityEvents;
    snapshot.tilt_events = status.tiltEvents;
    snapshot.motion_events = status.motionEvents;
    snapshot.no_motion_events = status.noMotionEvents;
    snapshot.activity = map_bma_activity(status.activity);
    snapshot.last_event = map_bma_runtime_event(status.lastEvent);

    if (status.dataValid) {
        imu_params_t params = {};
        params.orientation = AccelerometerUtils::getDirection(status.accel);
        _map_ori_lvgl(params);
        snapshot.orientation = params.orientation;
        snapshot.reverse = params.reverse;
    }
#endif
}

void hw_reset_bma_sensor_stats()
{
#if defined(ARDUINO) && defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA_ONLINE) {
        instance.resetRuntimeStatus();
    }
#endif
}

#if  defined(ARDUINO) && defined(USING_BHI260_SENSOR)
void imu_data_process(uint8_t sensor_id, const uint8_t *data, uint32_t size,
                      uint64_t *timestamp, void *user_data)
{
    (void)sensor_id;
    (void)size;
    (void)timestamp;
    (void)user_data;

    float roll, pitch, yaw;
    bhy2_quaternion_to_euler(data, &roll,  &pitch, &yaw);
    imu_params.roll = roll;
    imu_params.pitch = pitch;
    imu_params.heading = yaw;
}

void imu_accel_data_process(uint8_t sensor_id, const uint8_t *data, uint32_t size,
                            uint64_t *timestamp, void *user_data)
{
    (void)size;
    (void)timestamp;
    (void)user_data;

    struct bhy2_data_xyz xyz;
    float scaling = instance.sensor.getScaling(sensor_id);
    bhy2_parse_xyz(data, &xyz);
    hw_set_imu_accel(xyz.x * scaling, xyz.y * scaling, xyz.z * scaling);
}
#endif //ARDUINO

void hw_register_imu_process()
{
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        if (bhi260_imu_process_refs < UINT16_MAX) {
            bhi260_imu_process_refs++;
        }
        if (bhi260_imu_process_configured) {
            LILYGO_LOG_D("BHI260 IMU process already active refs=%u", bhi260_imu_process_refs);
            return;
        }

        float sample_rate = BHI260_FACTORY_IMU_SAMPLE_RATE_HZ;
        uint32_t report_latency_ms = 0; /* Report immediately */
        // LilyGoLib has already processed it
        // instance.sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
        // Enable rotation and acceleration streams.
        instance.sensor.removeResultEvent(BoschSensorID::GAME_ROTATION_VECTOR,
                                          imu_data_process);
        instance.sensor.removeResultEvent(BoschSensorID::ACCEL_PASSTHROUGH,
                                          imu_accel_data_process);

        bool ok = instance.sensor.configure(BoschSensorID::GAME_ROTATION_VECTOR,
                                            sample_rate, report_latency_ms);
        ok = instance.sensor.onResultEvent(BoschSensorID::GAME_ROTATION_VECTOR,
                                           imu_data_process) && ok;
        ok = instance.sensor.configure(BoschSensorID::ACCEL_PASSTHROUGH,
                                       sample_rate, report_latency_ms) && ok;
        ok = instance.sensor.onResultEvent(BoschSensorID::ACCEL_PASSTHROUGH,
                                           imu_accel_data_process) && ok;
        if (!ok) {
            instance.sensor.configure(BoschSensorID::GAME_ROTATION_VECTOR, 0, 0);
            instance.sensor.removeResultEvent(BoschSensorID::GAME_ROTATION_VECTOR,
                                              imu_data_process);
            instance.sensor.configure(BoschSensorID::ACCEL_PASSTHROUGH, 0, 0);
            instance.sensor.removeResultEvent(BoschSensorID::ACCEL_PASSTHROUGH,
                                              imu_accel_data_process);
            bhi260_imu_process_refs = 0;
            bhi260_imu_process_configured = false;
            imu_params.accel_valid = false;
            LILYGO_LOG_E("BHI260 IMU process register failed");
            return;
        }

        bhi260_imu_process_configured = true;
        LILYGO_LOG_I("BHI260 IMU process registered refs=%u rate=%.1fHz",
                     bhi260_imu_process_refs, sample_rate);
    }
#elif defined(USING_BMA423_SENSOR)
    instance.enableSensor(true);
#endif // SENSOR
#endif // ARDUINO
}

void hw_unregister_imu_process()
{
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        if (bhi260_imu_process_refs > 0) {
            bhi260_imu_process_refs--;
        }
        if (bhi260_imu_process_refs > 0) {
            LILYGO_LOG_D("BHI260 IMU process still active refs=%u", bhi260_imu_process_refs);
            return;
        }
        if (!bhi260_imu_process_configured) {
            return;
        }

        instance.sensor.configure(BoschSensorID::GAME_ROTATION_VECTOR, 0, 0);
        instance.sensor.removeResultEvent(BoschSensorID::GAME_ROTATION_VECTOR, imu_data_process);
        instance.sensor.configure(BoschSensorID::ACCEL_PASSTHROUGH, 0, 0);
        instance.sensor.removeResultEvent(BoschSensorID::ACCEL_PASSTHROUGH, imu_accel_data_process);
        bhi260_imu_process_configured = false;
        imu_params.accel_valid = false;
        LILYGO_LOG_I("BHI260 IMU process unregistered");
    }
#elif defined(USING_BMA423_SENSOR)
    instance.disableSensor();
#endif // SENSOR
#endif // ARDUINO
}

void hw_set_keyboard_read_callback(void(*read)(int state, char &c))
{
#if defined(ARDUINO) && (defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD))
    instance.kb.setCallback(read);
#endif
}

void hw_feedback()
{
#if defined(ARDUINO) && FACTORY_HAS_HAPTIC_DRV
    if (_feedback_enable) {
        instance.vibrator();
    }
#endif
}

uint8_t hw_get_haptic_effect()
{
    return haptic_effect;
}

bool hw_set_haptic_effect(uint8_t effect)
{
    effect = clamp_haptic_effect(effect);
    haptic_effect = effect;
    app_settings.haptic_effect = effect;
#if defined(ARDUINO) && FACTORY_HAS_HAPTIC_DRV
    instance.setHapticEffects(effect);
    return save_app_settings();
#else
    return true;
#endif
}

void hw_low_power_loop()
{
#ifdef ARDUINO
    // 测试深度睡眠
    // instance.sleep();
    instance.lightSleep();
#endif
}

void hw_inc_brightness(uint8_t level, bool async)
{
#ifdef ARDUINO
    instance.incrementalBrightness(level, DISP_BACKLIGHT_DELAY_MS, async);
#endif
}

void hw_dec_brightness(uint8_t level, bool async)
{
#ifdef ARDUINO
    instance.decrementBrightness(level, DISP_BACKLIGHT_DELAY_MS, async);
#endif
}

uint8_t hw_get_disp_min_brightness()
{
    return device_limits.min_brightness_level;
}

uint16_t hw_get_disp_max_brightness()
{
    return device_limits.max_brightness_level;
}

uint8_t hw_get_min_charge_current()
{
    return device_limits.min_charge_current_ma;
}

uint16_t hw_get_max_charge_current()
{
    return device_limits.max_charge_current_ma;
}

uint8_t hw_get_charge_level_nums()
{
    return device_limits.max_charge_level_index;
}

uint8_t hw_get_charge_steps()
{
    return device_limits.charge_current_step_ma;
}

void hw_set_cpu_freq(uint32_t mhz)
{
#ifdef ARDUINO
    setCpuFrequencyMhz(mhz);
#endif
}

void hw_disable_input_devices()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    instance.disableRotary();
#endif
}


void hw_enable_input_devices()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    instance.enableRotary();
#endif
}

void hw_enable_keyboard()
{
#if defined(ARDUINO) && (defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK))
    instance.enableKeyboard();
#endif
}

void hw_disable_keyboard()
{
#if defined(ARDUINO) && (defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK))
    instance.disableKeyboard();
#endif
}


void hw_flush_keyboard()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_get_device_online() & HW_KEYBOARD_ONLINE) {
        instance.kb.flush();
    }
#endif
}

bool hw_has_keyboard()
{
    return hw_get_device_online() & HW_KEYBOARD_ONLINE;
}

bool hw_has_encoder()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    return instance.hasEncoder();
#else
    return false;
#endif
}

void hw_set_rotary_step_divider(uint8_t divider)
{
#ifdef ARDUINO
    apply_rotary_step_divider(divider);
#else
    rotary_step_divider = clamp_rotary_step_divider(divider);
#endif
}

void hw_save_rotary_step_divider(uint8_t divider)
{
#ifdef ARDUINO
    uint8_t applied = apply_rotary_step_divider(divider);
    app_settings.rotary_step_divider = applied;
    save_app_settings();
#else
    hw_set_rotary_step_divider(divider);
    app_settings.rotary_step_divider = rotary_step_divider;
#endif
}

uint8_t hw_get_rotary_step_divider()
{
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER) && defined(USING_INPUT_DEV_ROTARY)
    if (instance.hasEncoder()) {
        rotary_step_divider = instance.getRotaryStepDivider();
    }
#endif
    return clamp_rotary_step_divider(rotary_step_divider);
}

uint8_t hw_get_rotary_step_divider_min()
{
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER) && defined(USING_INPUT_DEV_ROTARY)
    return instance.getRotaryStepDividerMin();
#else
    return ROTARY_STEP_DIVIDER_MIN;
#endif
}

uint8_t hw_get_rotary_step_divider_max()
{
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER) && defined(USING_INPUT_DEV_ROTARY)
    return instance.getRotaryStepDividerMax();
#else
    return ROTARY_STEP_DIVIDER_MAX;
#endif
}

bool hw_has_audio_jack_mode_setting()
{
#if defined(ARDUINO) && defined(T_DECK_V2_REV07) && defined(EXPANDS_AUDIO_JACK_SEL)
    return true;
#else
    return false;
#endif
}

void hw_set_audio_jack_mode(uint8_t mode)
{
#ifdef ARDUINO
    uint8_t applied = apply_audio_jack_mode(mode);
#else
    uint8_t applied = clamp_audio_jack_mode(mode);
    audio_jack_mode = applied;
#endif
    app_settings.audio_jack_mode = applied;
#ifdef ARDUINO
    save_app_settings();
#endif
    LILYGO_LOG_PRINTF("audio_jack_mode     :%u\n", (unsigned)applied);
}

uint8_t hw_get_audio_jack_mode()
{
    return clamp_audio_jack_mode(audio_jack_mode);
}

bool hw_has_indicator_led()
{
    return hw_get_device_online() & HW_LED_INDIC_ONLINE;
}

const char *hw_get_expands_fw_version()
{
#ifdef ARDUINO_T_DECK_V2
    static char version[32];
    snprintf(version, sizeof(version), "%s", instance.getExpandFirmwareVersion().c_str());
    return version;
#else
    return "None";
#endif
}

bool hw_has_otg_function()
{
#if defined(ARDUINO)
    return instance.hasOTG();;
#else
    return false;
#endif
}

#if defined(ARDUINO)
#include <Esp.h>
#endif
void hw_print_mem_info()
{
#if defined(ARDUINO)
    LILYGO_LOG_PRINTF("INTERNAL Memory Info:\n");
    LILYGO_LOG_PRINTF("------------------------------------------\n");
    LILYGO_LOG_PRINTF("  Total Size        :   %u B ( %.1f KB)\n", ESP.getHeapSize(), ESP.getHeapSize() / 1024.0);
    LILYGO_LOG_PRINTF("  Free Bytes        :   %u B ( %.1f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
    LILYGO_LOG_PRINTF("  Minimum Free Bytes:   %u B ( %.1f KB)\n", ESP.getMinFreeHeap(), ESP.getMinFreeHeap() / 1024.0);
    LILYGO_LOG_PRINTF("  Largest Free Block:   %u B ( %.1f KB)\n", ESP.getMaxAllocHeap(), ESP.getMaxAllocHeap() / 1024.0);
    LILYGO_LOG_PRINTF("------------------------------------------\n");
    LILYGO_LOG_PRINTF("SPIRAM Memory Info:\n");
    LILYGO_LOG_PRINTF("------------------------------------------\n");
    LILYGO_LOG_PRINTF("  Total Size        :  %u B (%.1f KB)\n", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0);
    LILYGO_LOG_PRINTF("  Free Bytes        :  %u B (%.1f KB)\n", ESP.getFreePsram(), ESP.getFreePsram() / 1024.0);
    LILYGO_LOG_PRINTF("  Minimum Free Bytes:  %u B (%.1f KB)\n", ESP.getMinFreePsram(), ESP.getMinFreePsram() / 1024.0);
    LILYGO_LOG_PRINTF("  Largest Free Block:  %u B (%.1f KB)\n", ESP.getMaxAllocPsram(), ESP.getMaxAllocPsram() / 1024.0);
    LILYGO_LOG_PRINTF("------------------------------------------\n");
#endif
}

static bool ir_send_begin = false;
static bool ir_recv_begin = false;

#if defined(ARDUINO) && defined(USING_IR_REMOTE)
#include <IRsend.h>
IRsend irsend(IR_SEND); // T-Watch S3 GPIO2 pin to use.
#endif


#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
#include <IRrecv.h>
static constexpr uint16_t FACTORY_IR_CAPTURE_BUFFER_SIZE = 1024;
static constexpr uint8_t FACTORY_IR_CAPTURE_TIMEOUT_MS = 50;
IRrecv irrecv(IR_SEND, FACTORY_IR_CAPTURE_BUFFER_SIZE, FACTORY_IR_CAPTURE_TIMEOUT_MS, true);
#endif

void hw_set_remote_code(uint32_t nec_code)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
    irsend.sendNEC(nec_code);
#endif
}

void hw_get_remote_code(uint64_t &result)
{
#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
    decode_results results;
    if (irrecv.decode(&results)) {
        // Serial.print("IR Code received: ");
        // Serial.println(results.value, HEX);
        result = results.value;
        irrecv.resume();  // Receive the next value
    }
#else
    result = random(0, INT_MAX);
#endif
}

void hw_ir_function_select(bool enableSend)
{
#if defined(ARDUINO)
    if (enableSend) {
#if defined(USING_IR_REMOTE) && defined(USING_IR_RECEIVER)
        instance.IRFunctionSelect(IR_FUNC_SENDER);
#endif

#if defined(USING_IR_RECEIVER)
        if (ir_recv_begin) {
            ir_recv_begin = false;
            irrecv.disableIRIn();
            LILYGO_LOG_PRINTLN("IR Receive disabled");
        }
#endif
#if defined(USING_IR_REMOTE)
        if (!ir_send_begin) {
            ir_send_begin = true;
            irsend.begin();
            LILYGO_LOG_PRINTLN("IR Send begin");
        }
#endif
    } else {
#if defined(USING_IR_REMOTE) && defined(USING_IR_RECEIVER)
        instance.IRFunctionSelect(IR_FUNC_RECEIVER);
#endif

#if defined(USING_IR_RECEIVER)
        if (!ir_recv_begin) {
            ir_recv_begin = true;
            irrecv.enableIRIn();
            LILYGO_LOG_PRINTLN("IR Receive enabled");
        }
#endif

        ir_send_begin = false;
    }
#endif
}

#ifdef USING_MAG_COMPASS

#if defined(T_DECK_V2_REV07)
static constexpr uint32_t MAG_CAL_MAGIC = 0x4D414734; // MAG4, Rev0.7 board-side remap
#else
static constexpr uint32_t MAG_CAL_MAGIC = 0x4D414733; // MAG3, board-side remap applied
#endif
static const char *MAG_CAL_KEY = "mag_cal";

typedef struct {
    uint32_t magic;
    mag_calibration_t cal;
} mag_cal_storage_t;

static mag_calibration_t active_mag_cal = {0, 0, 0, false};

// QMC6309 is mounted on the PCB back side. Keep the runtime calibration in the
// same board coordinate frame that hw_mag_read() reports.
static mag_calibration_t hw_mag_board_cal_to_sensor_cal(const mag_calibration_t &cal)
{
    mag_calibration_t mapped;
#if defined(T_DECK_V2_REV07)
    // Rev0.7 rotated the QMC6309 placement relative to Rev0.6: board = {-X, Y, -Z}.
    mapped.x = -cal.x;
    mapped.y = cal.y;
    mapped.z = -cal.z;
#else
    // Rev0.6 and earlier QMC6309 placement: board = {-Y, -X, -Z}.
    mapped.x = -cal.y;
    mapped.y = -cal.x;
    mapped.z = -cal.z;
#endif
    mapped.valid = cal.valid;
    return mapped;
}

static void hw_mag_set_runtime_offset(const mag_calibration_t &cal)
{
    active_mag_cal = cal;
#ifdef ARDUINO
    mag_calibration_t sensor_cal = hw_mag_board_cal_to_sensor_cal(cal);
    if (cal.valid) {
        instance.mag.setOffset(sensor_cal.x, sensor_cal.y, sensor_cal.z);
    } else {
        instance.mag.setOffset(0, 0, 0);
    }
#endif
}

static float hw_mag_heading_from_xy(float x, float y)
{
    if (x == 0.0f && y == 0.0f) {
        return 0.0f;
    }
    float heading = atan2f(y, x) * 180.0f / (float)M_PI;
    if (heading < 0.0f) {
        heading += 360.0f;
    } else if (heading >= 360.0f) {
        heading -= 360.0f;
    }
    return heading;
}

void hw_mag_enable(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        // The desired output data rate in Hz.  Allowed values are 1.0, 10.0, 50.0, 100.0 and 200.0HZ.
        float data_rate_hz = 50.0f;
        // op_mode: Allowed values are SUSPEND, NORMAL, SINGLE_MEASUREMENT, CONTINUOUS_MEASUREMENT
        OperationMode op_mode = OperationMode::CONTINUOUS_MEASUREMENT;
        // full_scale: Allowed values are  FS_8G, FS_16G ,FS_32G
        MagFullScaleRange full_scale = MagFullScaleRange::FS_8G;
        // over_sample_ratio: Allowed values are OSR_1, OSR_2, OSR_4, OSR_8
        MagOverSampleRatio over_sample_ratio = MagOverSampleRatio::OSR_8;
        // down_sample_ratio: QMC6309 does not support downsampling rate settings; this parameter is ignored.
        MagDownSampleRatio down_sample_ratio = MagDownSampleRatio::DSR_1;
        if (!instance.mag.configMagnetometer(op_mode, full_scale, data_rate_hz, over_sample_ratio, down_sample_ratio)) {
            LILYGO_LOG_E("Failed to configure magnetometer");
        }
        hw_mag_load_calibration(nullptr);
    } else {
        instance.mag.setOperationMode(OperationMode::SUSPEND);
    }
#endif // ARDUINO
}

float hw_mag_get_polar()
{
    mag_data_t data;
    if (hw_mag_read(data)) {
        return data.heading_degrees;
    }
    return -1.0f;
}

bool hw_mag_read(mag_data_t &params)
{
#ifdef ARDUINO
    MagnetometerData data;
    if (instance.mag.readData(data)) {
#if defined(T_DECK_V2_REV07)
        params.raw_x = -data.raw.x;
        params.raw_y = data.raw.y;
        params.raw_z = -data.raw.z;
        params.field_x = -data.magnetic_field.x;
        params.field_y = data.magnetic_field.y;
        params.field_z = -data.magnetic_field.z;
#else
        params.raw_x = -data.raw.y;
        params.raw_y = -data.raw.x;
        params.raw_z = -data.raw.z;
        params.field_x = -data.magnetic_field.y;
        params.field_y = -data.magnetic_field.x;
        params.field_z = -data.magnetic_field.z;
#endif
        params.heading_degrees = hw_mag_heading_from_xy(params.field_x, params.field_y);
        params.strength_ut = sqrtf((params.field_x * params.field_x) +
                                   (params.field_y * params.field_y) +
                                   (params.field_z * params.field_z)) * 100.0f;
        params.overflow = data.overflow;
        return true;
    }
    return false;
#else
    static float sim_angle = 0;
    sim_angle = fmod(sim_angle + 0.5, 360);
    params.raw_x = (int16_t)(cosf(sim_angle * (float)M_PI / 180.0f) * 800);
    params.raw_y = (int16_t)(sinf(sim_angle * (float)M_PI / 180.0f) * 800);
    params.raw_z = 100;
    params.field_x = params.raw_x / 1000.0f;
    params.field_y = params.raw_y / 1000.0f;
    params.field_z = params.raw_z / 1000.0f;
    params.heading_degrees = sim_angle;
    params.strength_ut = sqrtf((params.field_x * params.field_x) +
                               (params.field_y * params.field_y) +
                               (params.field_z * params.field_z)) * 100.0f;
    params.overflow = false;
    return true;
#endif
}

void hw_mag_apply_calibration(const mag_calibration_t &cal)
{
    hw_mag_set_runtime_offset(cal);
}

bool hw_mag_save_calibration(const mag_calibration_t &cal)
{
    mag_calibration_t saved = cal;
    saved.valid = true;
    hw_mag_set_runtime_offset(saved);
#ifdef ARDUINO
    mag_cal_storage_t store = {MAG_CAL_MAGIC, saved};
    return prefs.putBytes(MAG_CAL_KEY, &store, sizeof(store)) == sizeof(store);
#else
    return true;
#endif
}

bool hw_mag_load_calibration(mag_calibration_t *cal)
{
#ifdef ARDUINO
    mag_cal_storage_t store = {};
    bool valid = prefs.getBytes(MAG_CAL_KEY, &store, sizeof(store)) == sizeof(store) &&
                 store.magic == MAG_CAL_MAGIC &&
                 store.cal.valid;
    if (valid) {
        hw_mag_set_runtime_offset(store.cal);
        if (cal) {
            *cal = store.cal;
        }
        return true;
    }
#endif
    mag_calibration_t reset = {0, 0, 0, false};
    hw_mag_set_runtime_offset(reset);
    if (cal) {
        *cal = reset;
    }
    return false;
}

void hw_mag_get_calibration(mag_calibration_t &cal)
{
    cal = active_mag_cal;
}

void hw_mag_clear_calibration()
{
#ifdef ARDUINO
    prefs.remove(MAG_CAL_KEY);
#endif
    mag_calibration_t reset = {0, 0, 0, false};
    hw_mag_set_runtime_offset(reset);
}

#endif // USING_MAG_COMPASS

#ifdef USING_BME280

void hw_bme_enable(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        instance.bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                                 Adafruit_BME280::SAMPLING_X1,   // temperature
                                 Adafruit_BME280::SAMPLING_X1, // pressure
                                 Adafruit_BME280::SAMPLING_X1,   // humidity
                                 Adafruit_BME280::FILTER_X2 );
    } else {
        instance.bme.setSampling(Adafruit_BME280::MODE_SLEEP);
    }
#endif
}


void hw_bme_get_data(float &temp, float &humi, float &press, float &alt)
{
#ifdef ARDUINO
    temp = instance.bme.readTemperature();
    humi = instance.bme.readHumidity();
    press = instance.bme.readPressure() / 100.0F;
    alt = instance.bme.readAltitude(1013.25);

#else
    temp = random(0, 25);
    humi = random(40, 95);
    press = random(1000, 1200);
    alt = random(20, 60);
#endif
}

#endif /*USING_BME280*/



static ButtonEventCallback _button_cb = NULL;
static PointerButtonEventCallback _pointer_button_cb = NULL;

#ifdef ARDUINO

static void buttonEventCallback(const DeviceEvent &event, void *user_data)
{
    LILYGO_LOG_PRINTF("Button event callback triggered\n");
    const ButtonEventParam_t *p = instance.getButtonEventParam(event);
    if (_button_cb && p) {
        _button_cb(p->id, p->event);
    }
}

static void pointerButtonEventCallback(const DeviceEvent &event, void *user_data)
{
    (void)user_data;
    if (!_pointer_button_cb) {
        return;
    }

    const ButtonEventParam_t *button = instance.getButtonEventParam(event);
    if (!button || button->event != BUTTON_EVENT_CLICK) {
        return;
    }

#if defined(ARDUINO_T_DECK)
    if (button->id == BUTTON_CENTER) {
        _pointer_button_cb(HW_POINTER_BUTTON_LEFT);
    }
#elif defined(ARDUINO_T_DECK_V2)
    switch (button->id) {
    case BUTTON_LEFT:
        _pointer_button_cb(HW_POINTER_BUTTON_LEFT);
        break;
    case BUTTON_RIGHT:
        _pointer_button_cb(HW_POINTER_BUTTON_RIGHT);
        break;
    case BUTTON_CENTER:
        _pointer_button_cb(HW_POINTER_BUTTON_MIDDLE);
        break;
    default:
        break;
    }
#else
    (void)button;
#endif
}
#endif

#if defined(ARDUINO) && (defined(USING_TRACKBALL) || defined(USING_TRACKBALL_V2) || \
    defined(USING_TDECK_TRACKBALL))

static TrackballEventCallback _trackball_cb = NULL;

static void trackballEventCallback(const DeviceEvent &event, void *user_data)
{
    const TrackballXY_t *data = instance.getTrackballXY(event);
    if (_trackball_cb && data) {
        _trackball_cb(data->delta_x, data->delta_y);
        return;
    }
    if (_trackball_cb) {
        switch (instance.getTrackballDirType(event)) {
        case TRACKBALL_DIR_UP:    _trackball_cb(0, -1); break;
        case TRACKBALL_DIR_DOWN:  _trackball_cb(0, 1); break;
        case TRACKBALL_DIR_LEFT:  _trackball_cb(-1, 0); break;
        case TRACKBALL_DIR_RIGHT: _trackball_cb(1, 0); break;
        default: break;
        }
    }
}


#endif

void hw_set_trackball_callback(TrackballEventCallback callback)
{
#if defined(ARDUINO) && (defined(USING_TRACKBALL) || defined(USING_TRACKBALL_V2) || \
    defined(USING_TDECK_TRACKBALL))
    // instance.setTrackballCallback(callback);
    if (callback) {
        instance.onEvent(TRACKBALL_EVENT, trackballEventCallback);
        _trackball_cb = callback;
    } else {
        instance.removeEvent(TRACKBALL_EVENT, trackballEventCallback);
        _trackball_cb = NULL;
    }
#endif
}

bool hw_pointer_available()
{
#if defined(ARDUINO_T_DECK)
    return instance.hasTrackball();
#elif defined(ARDUINO) && (defined(USING_TRACKBALL) || defined(USING_TRACKBALL_V2))
    return (hw_get_device_online() & HW_PAW_A350_ONLINE) != 0;
#else
    return false;
#endif
}

void hw_set_pointer_button_callback(PointerButtonEventCallback callback)
{
#if defined(ARDUINO) && (defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_V2))
    if (callback) {
        _pointer_button_cb = callback;
        instance.onEvent(BUTTON_EVENT, pointerButtonEventCallback);
    } else {
        instance.removeEvent(BUTTON_EVENT, pointerButtonEventCallback);
        _pointer_button_cb = NULL;
    }
#else
    (void)callback;
#endif
}

void hw_set_button_callback(ButtonEventCallback callback)
{
#if defined(ARDUINO)/*&& (defined(USING_TRACKBALL) || defined(USING_TRACKBALL_V2))*/
    if (callback) {
        instance.onEvent(BUTTON_EVENT, buttonEventCallback);
        _button_cb = callback;
    } else {
        instance.removeEvent(BUTTON_EVENT, buttonEventCallback);
        _button_cb = NULL;
    }
#endif
}

bool hw_has_button_monitor()
{
#ifdef ARDUINO
    return instance.getCapability().hasButton;
#else
    return false;
#endif
}

bool hw_has_pmu_button_monitor()
{
#ifdef ARDUINO
    return instance.getCapability().hasPmuButton;
#else
    return false;
#endif
}

void hw_get_button_monitor_status(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return;
    }
    if (button_monitor_count == 0) {
        snprintf(buffer, size, "Waiting");
        return;
    }
    snprintf(buffer, size, "ID %u %s #%lu", (unsigned)button_monitor_id,
             button_event_name(button_monitor_event), (unsigned long)button_monitor_count);
}

void hw_get_pmu_button_monitor_status(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return;
    }
    if (pmu_button_monitor_count == 0) {
        snprintf(buffer, size, "Waiting");
        return;
    }
    snprintf(buffer, size, "%s #%lu", pmu_button_event_name(pmu_button_monitor_event),
             (unsigned long)pmu_button_monitor_count);
}

const char *hw_get_device_power_tips_string()
{
    switch (hw_get_power_off_mode()) {
    case HW_POWER_OFF_SHIP_MODE:
        return "Ship mode disconnects the battery path. Unplug USB-C first; hold Power or reconnect USB-C to wake.";
    case HW_POWER_OFF_DEEP_SLEEP:
        return "This device has no PMIC. Power off is simulated with deep sleep; press BOOT to wake.";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return "The PMIC cuts system power completely. Hold Power or connect USB-C to start again.";
    }
}

const char *hw_get_firmware_hash_string()
{
#ifdef ARDUINO
    static char hash_string[33] = {0};
    snprintf(hash_string, sizeof(hash_string), "%s", ESP.getSketchMD5().c_str());
    return hash_string;
#else
    return "DummyHashString";
#endif
}

const char *hw_get_chip_id_string()
{
#ifdef ARDUINO
    static char chipid[13] = {0};
    uint64_t chipmacid = 0LL;
    esp_efuse_mac_get_default((uint8_t *)(&chipmacid));
    snprintf(chipid, sizeof(chipid), "%04X%08X", (uint16_t)(chipmacid >> 32), (uint32_t)(chipmacid));
    return chipid;
#endif
    return "DummyChipIDString";
}


void hw_set_usb_rf_switch(bool to_usb)
{
#ifdef ARDUINO
#if defined(HAS_USB_RF_SWITCH)
    instance.setRFSwitch(to_usb);
#endif
#endif
}

__attribute__((weak)) int16_t radio_get_last_transmit_state()
{
    return 0;
}
