/**
 * @file      ui_sys.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#define UI_SYS_DEFERRED_PERIOD_MS 10
#define UI_SYS_LOW_RES_SAFE_MIN_BRIGHTNESS 1
#define UI_SYS_HIGH_RES_SAFE_MIN_BRIGHTNESS 60
#define UI_SYS_LOW_RES_MAX_BRIGHTNESS 16

static lv_obj_t *page_container = NULL;
static lv_timer_t *timer = NULL;
static lv_timer_t *deferred_timer = NULL;
static lv_group_t *menu_g;
static  user_setting_params_t local_param;
static uint8_t local_rotary_step_divider = 1;
static bool local_keyboard_navigation_enabled = true;
static uint8_t local_audio_jack_mode = AUDIO_JACK_MODE_CTIA;
static uint8_t local_mic_input_source = MIC_INPUT_SOURCE_INTERNAL;
static uint32_t get_ip_id = 0;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *temperature, *humidity, *pressure, *altitude;

#if defined(ARDUINO_T_LORA_PAGER) && defined(USING_INPUT_DEV_ROTARY)
#define UI_SYS_HAS_ROTARY_STEP_SETTING 1
#else
#define UI_SYS_HAS_ROTARY_STEP_SETTING 0
#endif

#if defined(ARDUINO_T_LORA_PAGER)
#define UI_SYS_HAS_KEYBOARD_NAV_SETTING 1
#else
#define UI_SYS_HAS_KEYBOARD_NAV_SETTING 0
#endif

#if defined(T_DECK_V2_REV07) && defined(EXPANDS_AUDIO_JACK_SEL)
#define UI_SYS_HAS_AUDIO_JACK_SETTING 1
#else
#define UI_SYS_HAS_AUDIO_JACK_SETTING 0
#endif

#if defined(T_DECK_V2_REV07) && defined(USING_AUDIO_CODEC)
#define UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING 1
#else
#define UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING 0
#endif

#if UI_SYS_HAS_AUDIO_JACK_SETTING || UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING
#define UI_SYS_HAS_AUDIO_SETTING 1
#else
#define UI_SYS_HAS_AUDIO_SETTING 0
#endif

#if UI_SYS_HAS_ROTARY_STEP_SETTING
static uint8_t rotary_step_divider_value_from_index(uint16_t index);
#endif

typedef struct {
    lv_obj_t *datetime_label;
    lv_obj_t *wifi_rssi_label;
    lv_obj_t *batt_voltage_label;
    lv_obj_t *button_label;
    lv_obj_t *pmu_button_label;
    lv_obj_t *storage_label;
} sys_label_t;

static sys_label_t sys_label;
static bool storage_size_loaded = false;
static uint8_t sys_deferred_step = 0;
static uint8_t sys_deferred_device_index = 0;
static uint8_t sys_deferred_device_count = 0;
static uint32_t sys_deferred_device_mask = 0;
static lv_obj_t *sys_deferred_card = NULL;

static void update_storage_label()
{
    if (!sys_label.storage_label || storage_size_loaded) {
        return;
    }

    char buffer[32];
    float size = hw_get_sd_size();
#if defined(HAS_SD_CARD_SOCKET)
    const char *unit = "GB";
#else
    const char *unit = "MB";
#endif
    if (size > 0) {
        snprintf(buffer, sizeof(buffer), "%.2f %s", size, unit);
    } else {
        snprintf(buffer, sizeof(buffer), "N.A");
    }
    lv_label_set_text(sys_label.storage_label, buffer);
    storage_size_loaded = true;
}

static void update_button_monitor_labels()
{
    char status[48];

    if (sys_label.button_label) {
        hw_get_button_monitor_status(status, sizeof(status));
        lv_label_set_text(sys_label.button_label, status);
    }

    if (sys_label.pmu_button_label) {
        hw_get_pmu_button_monitor_status(status, sizeof(status));
        lv_label_set_text(sys_label.pmu_button_label, status);
    }
}

static void format_battery_voltage(char *buffer, size_t size)
{
    power_monitor_snapshot_t snapshot;
    hw_get_power_monitor_snapshot(snapshot);
    if (!snapshot.battery_mv.valid) {
        snprintf(buffer, size, "--");
        return;
    }
    const int voltage = static_cast<int>(snapshot.battery_mv.value + 0.5f);
    const bool adc_usb_connected = snapshot.battery_mv.source == POWER_MONITOR_SRC_ADC &&
                                   snapshot.vbus_present_valid && snapshot.vbus_present;
    snprintf(buffer, size, "%d mV%s", voltage,
             adc_usb_connected ? " / USB" : "");
}

static void sys_timer_event_cb(lv_timer_t *t)
{
    if (sys_label.datetime_label) {
        char datetime[24];
        hw_get_date_time(datetime, sizeof(datetime));
        lv_label_set_text_fmt(sys_label.datetime_label, "%s", datetime);
    }

    if (sys_label.wifi_rssi_label && hw_get_wifi_connected()) {
        lv_label_set_text_fmt(sys_label.wifi_rssi_label, "%d", hw_get_wifi_rssi());
    }
    if (sys_label.batt_voltage_label) {
        char voltage[32];
        format_battery_voltage(voltage, sizeof(voltage));
        lv_label_set_text(sys_label.batt_voltage_label, voltage);
    }
    update_button_monitor_labels();
    update_storage_label();

#ifdef USING_BME280
    float temp,  humi,  press,  alt;
    hw_bme_get_data(temp, humi, press, alt);
    lv_label_set_text_fmt(temperature, "%.1f°C", temp);
    lv_label_set_text_fmt(humidity, "%.0f%%", humi);
    lv_label_set_text_fmt(pressure, "%.0fhPa", press);
    lv_label_set_text_fmt(altitude, "%.0fm", alt);
#endif
}

static long map_r(long x, long in_min, long in_max, long out_min, long out_max)
{
    if (x < in_min) {
        return out_min;
    } else if (x > in_max) {
        return out_max;
    }
    return ((x - in_min) * (out_max - out_min)) / (in_max - in_min) + out_min;
}

static void back_event_handler(lv_event_t *e)
{
    if (deferred_timer) {
        lv_timer_del(deferred_timer);
        deferred_timer = NULL;
    }
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    hw_set_user_setting(local_param);
#if UI_SYS_HAS_ROTARY_STEP_SETTING
    if (hw_has_encoder()) {
        hw_save_rotary_step_divider(local_rotary_step_divider);
    }
#endif

    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }
    menu_show();
}

static void display_brightness_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    local_param.brightness_level = val;
    hw_set_disp_backlight(val);
}

static void keyboard_brightness_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    local_param.keyboard_bl_level = val;
    hw_set_kb_backlight(val);
}

static void led_brightness_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    local_param.led_indicator_level = val;
    hw_set_led_backlight(val);
}

static void disp_timeout_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val =  lv_slider_get_value(obj);
    local_param.disp_timeout_second = val;
}

static void nav_auto_hide_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    bool enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    hw_set_nav_auto_hide_enabled(enabled);

    if (enabled) {
        ui_app_page_enable_nav_auto_hide(page_container, 3000);
    } else {
        ui_app_page_disable_nav_auto_hide(page_container);
    }
}

#if UI_SYS_HAS_KEYBOARD_NAV_SETTING
static void keyboard_navigation_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    local_keyboard_navigation_enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    hw_set_keyboard_navigation_enabled(local_keyboard_navigation_enabled);
    if (local_keyboard_navigation_enabled) {
        enable_keyboard();
    } else {
        disable_keyboard();
    }
}
#endif

#if UI_SYS_HAS_ROTARY_STEP_SETTING
static void rotary_step_divider_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t val = rotary_step_divider_value_from_index(lv_dropdown_get_selected(obj));
    local_rotary_step_divider = val;
    hw_set_rotary_step_divider(val);
}
#endif

static uint8_t audio_jack_mode_from_index(uint16_t index)
{
    return index == AUDIO_JACK_MODE_OMTP ? AUDIO_JACK_MODE_OMTP : AUDIO_JACK_MODE_CTIA;
}

static uint8_t audio_jack_mode_to_index(uint8_t mode)
{
    return mode == AUDIO_JACK_MODE_OMTP ? AUDIO_JACK_MODE_OMTP : AUDIO_JACK_MODE_CTIA;
}

#if UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING
static uint8_t mic_input_source_from_index(uint16_t index)
{
    return index == MIC_INPUT_SOURCE_JACK ? MIC_INPUT_SOURCE_JACK : MIC_INPUT_SOURCE_INTERNAL;
}

static uint8_t mic_input_source_to_index(uint8_t source)
{
    return source == MIC_INPUT_SOURCE_JACK ? MIC_INPUT_SOURCE_JACK : MIC_INPUT_SOURCE_INTERNAL;
}
#endif

static void otg_output_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool turnOn = lv_obj_has_state(obj, LV_STATE_CHECKED);
        if (hw_set_otg(turnOn) == false) {
            lv_obj_clear_state(obj, LV_STATE_CHECKED);
        }
    }
}

#if UI_SYS_HAS_AUDIO_JACK_SETTING
static void audio_jack_mode_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t mode = audio_jack_mode_from_index(lv_dropdown_get_selected(obj));
    local_audio_jack_mode = mode;
    hw_set_audio_jack_mode(mode);
}
#endif

#if UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING
static void mic_input_source_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint8_t source = mic_input_source_from_index(lv_dropdown_get_selected(obj));
    local_mic_input_source = source;
    hw_set_mic_input_source(source);
}
#endif

static void charger_enable_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool turnOn = lv_obj_has_state(obj, LV_STATE_CHECKED);
        local_param.charger_enable = turnOn;
        hw_set_charger(turnOn);
    }
}

static void charger_current_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint16_t val =  lv_slider_get_value(obj);
    local_param.charger_current = hw_set_charger_current_level(val);
}

/* Global font size preference: 0=auto(small screen), 1=small, 2=medium, 3=large */
static uint8_t font_size_pref = 0;
static lv_obj_t *font_preview_label = NULL;
static uint8_t theme_preset_idx = 0;

static void theme_preset_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    theme_preset_idx = lv_dropdown_get_selected(obj);
    ui_apply_theme_preset(theme_preset_idx);
    ui_theme_apply();

    /* Update local_param so other callbacks don't overwrite theme */
    local_param.theme_preset_idx = theme_preset_idx;
    hw_set_user_setting(local_param);
}

static void font_size_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    font_size_pref = lv_dropdown_get_selected(obj);
    ui_set_font_size_pref(font_size_pref);
}

#if UI_SYS_HAS_ROTARY_STEP_SETTING
static uint8_t rotary_step_divider_clamp(uint8_t value)
{
    uint8_t min_value = hw_get_rotary_step_divider_min();
    uint8_t max_value = hw_get_rotary_step_divider_max();
    if (max_value < min_value) {
        max_value = min_value;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint8_t rotary_step_divider_index_from_value(uint8_t value)
{
    uint8_t min_value = hw_get_rotary_step_divider_min();
    value = rotary_step_divider_clamp(value);
    return value - min_value;
}

static uint8_t rotary_step_divider_value_from_index(uint16_t index)
{
    uint8_t min_value = hw_get_rotary_step_divider_min();
    uint8_t max_value = hw_get_rotary_step_divider_max();
    if (max_value < min_value) {
        max_value = min_value;
    }

    uint16_t value = min_value + index;
    if (value > max_value) {
        value = max_value;
    }
    return (uint8_t)value;
}

static void build_rotary_step_divider_options(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    size_t used = 0;
    uint8_t min_value = hw_get_rotary_step_divider_min();
    uint8_t max_value = hw_get_rotary_step_divider_max();
    if (max_value < min_value) {
        max_value = min_value;
    }

    for (uint8_t value = min_value; value <= max_value; value++) {
        int written = snprintf(buffer + used, buffer_size - used,
                               "%s%u", value == min_value ? "" : "\n", value);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            buffer[buffer_size - 1] = '\0';
            break;
        }
        used += written;
        if (value == UINT8_MAX) {
            break;
        }
    }
}
#endif

static void start_sys_refresh_timer()
{
    if (!timer) {
        timer = lv_timer_create(sys_timer_event_cb, 1000, NULL);
    }
}

typedef enum {
    UI_SYS_BUILD_DISPLAY_CARD,
    UI_SYS_BUILD_DISPLAY_BRIGHTNESS,
    UI_SYS_BUILD_DISPLAY_KEYBOARD,
    UI_SYS_BUILD_DISPLAY_LED,
    UI_SYS_BUILD_DISPLAY_TIMEOUT,
    UI_SYS_BUILD_DISPLAY_NAV,
    UI_SYS_BUILD_DISPLAY_FONT,
    UI_SYS_BUILD_DISPLAY_THEME,
    UI_SYS_BUILD_ROTARY,
    UI_SYS_BUILD_KEYBOARD_NAV,
    UI_SYS_BUILD_AUDIO,
    UI_SYS_BUILD_POWER_CARD,
    UI_SYS_BUILD_POWER_OTG,
    UI_SYS_BUILD_POWER_CHARGER,
    UI_SYS_BUILD_POWER_CHARGE_CURRENT,
    UI_SYS_BUILD_BUTTONS_CARD,
    UI_SYS_BUILD_BUTTON_DEVICE,
    UI_SYS_BUILD_BUTTON_PMU,
    UI_SYS_BUILD_ENV_CARD,
    UI_SYS_BUILD_ENV_TEMPERATURE,
    UI_SYS_BUILD_ENV_HUMIDITY,
    UI_SYS_BUILD_ENV_PRESSURE,
    UI_SYS_BUILD_ENV_ALTITUDE,
    UI_SYS_BUILD_DEVICES_CARD,
    UI_SYS_BUILD_DEVICES_ROWS,
    UI_SYS_BUILD_SYSTEM_CARD,
    UI_SYS_BUILD_SYSTEM_EXPAND_FW,
    UI_SYS_BUILD_SYSTEM_MAC,
    UI_SYS_BUILD_SYSTEM_WIFI_SSID,
    UI_SYS_BUILD_SYSTEM_RTC,
    UI_SYS_BUILD_SYSTEM_IP,
    UI_SYS_BUILD_SYSTEM_RSSI,
    UI_SYS_BUILD_SYSTEM_VOLTAGE,
    UI_SYS_BUILD_SYSTEM_STORAGE,
    UI_SYS_BUILD_SYSTEM_LVGL,
    UI_SYS_BUILD_SYSTEM_ARDUINO,
    UI_SYS_BUILD_SYSTEM_BUILD,
    UI_SYS_BUILD_SYSTEM_CHIP_ID,
    UI_SYS_BUILD_DONE,
} ui_sys_build_step_t;

static bool run_sys_deferred_step()
{
    if (!page_container) {
        sys_deferred_step = UI_SYS_BUILD_DONE;
        return true;
    }

    switch (sys_deferred_step) {
    case UI_SYS_BUILD_DISPLAY_CARD:
        sys_deferred_card = ui_create_card(page_container, "Display");
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_BRIGHTNESS;
        break;

    case UI_SYS_BUILD_DISPLAY_BRIGHTNESS: {
        uint16_t min_brightness = hw_get_disp_min_brightness();
        uint16_t max_brightness = hw_get_disp_max_brightness();
        uint16_t safe_min_brightness = max_brightness <= UI_SYS_LOW_RES_MAX_BRIGHTNESS ?
                                       UI_SYS_LOW_RES_SAFE_MIN_BRIGHTNESS :
                                       UI_SYS_HIGH_RES_SAFE_MIN_BRIGHTNESS;
        if (min_brightness < safe_min_brightness) {
            min_brightness = safe_min_brightness;
        }
        ui_create_card_slider(sys_deferred_card, LV_SYMBOL_SETTINGS, "Brightness",
                              min_brightness, max_brightness,
                              local_param.brightness_level, display_brightness_cb);
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_KEYBOARD;
        break;
    }

    case UI_SYS_BUILD_DISPLAY_KEYBOARD:
        if (hw_has_keyboard()) {
            ui_create_card_slider(sys_deferred_card, LV_SYMBOL_SETTINGS, "Keyboard BL",
                                  0, 255, local_param.keyboard_bl_level, keyboard_brightness_cb);
        }
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_LED;
        break;

    case UI_SYS_BUILD_DISPLAY_LED:
        if (hw_has_indicator_led()) {
            ui_create_card_slider(sys_deferred_card, LV_SYMBOL_SETTINGS, "LED",
                                  0, 255, local_param.led_indicator_level, led_brightness_cb);
        }
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_TIMEOUT;
        break;

    case UI_SYS_BUILD_DISPLAY_TIMEOUT:
        ui_create_card_slider(sys_deferred_card, LV_SYMBOL_SETTINGS, "Timeout",
                              0, 180, local_param.disp_timeout_second, disp_timeout_cb);
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_NAV;
        break;

    case UI_SYS_BUILD_DISPLAY_NAV:
        ui_create_card_switch(sys_deferred_card, LV_SYMBOL_SETTINGS, "Auto Hide Bar",
                              hw_get_nav_auto_hide_enabled(), nav_auto_hide_cb);
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_FONT;
        break;

    case UI_SYS_BUILD_DISPLAY_FONT:
        ui_create_card_dropdown(sys_deferred_card, LV_SYMBOL_SETTINGS, "Font Size",
                                "Auto\nSmall\nMedium\nLarge", font_size_pref, font_size_cb);
        sys_deferred_step = UI_SYS_BUILD_DISPLAY_THEME;
        break;

    case UI_SYS_BUILD_DISPLAY_THEME: {
        char theme_options[256] = {0};
        for (uint8_t i = 0; i < UI_THEME_PRESET_COUNT; i++) {
            if (i > 0) lv_strcat(theme_options, "\n");
            lv_strcat(theme_options, ui_theme_presets[i].name);
        }
        ui_create_card_dropdown(sys_deferred_card, LV_SYMBOL_SETTINGS, "Theme",
                                theme_options, theme_preset_idx, theme_preset_cb);
        sys_deferred_step = UI_SYS_BUILD_ROTARY;
        break;
    }

    case UI_SYS_BUILD_ROTARY:
#if UI_SYS_HAS_ROTARY_STEP_SETTING
        sys_deferred_card = ui_create_card(page_container, "Input");
        if (hw_has_encoder()) {
            char rotary_step_options[32];
            build_rotary_step_divider_options(rotary_step_options, sizeof(rotary_step_options));
            ui_create_card_dropdown(sys_deferred_card, LV_SYMBOL_SETTINGS, "Encoder Step",
                                    rotary_step_options,
                                    rotary_step_divider_index_from_value(local_rotary_step_divider),
                                    rotary_step_divider_cb);
        }
#endif
        sys_deferred_step = UI_SYS_BUILD_KEYBOARD_NAV;
        break;

    case UI_SYS_BUILD_KEYBOARD_NAV:
#if UI_SYS_HAS_KEYBOARD_NAV_SETTING
        ui_create_card_switch(sys_deferred_card, LV_SYMBOL_KEYBOARD, "Keyboard Nav",
                              local_keyboard_navigation_enabled, keyboard_navigation_cb);
#endif
        sys_deferred_step = UI_SYS_BUILD_AUDIO;
        break;

    case UI_SYS_BUILD_AUDIO:
#if UI_SYS_HAS_AUDIO_SETTING
        if (hw_has_audio_jack_mode_setting() || hw_has_mic_input_source_setting()) {
            sys_deferred_card = ui_create_card(page_container, "Audio");
#if UI_SYS_HAS_AUDIO_JACK_SETTING
            if (hw_has_audio_jack_mode_setting()) {
                ui_create_card_dropdown(sys_deferred_card, LV_SYMBOL_AUDIO, "Jack",
                                        "CTIA\nTRRS",
                                        audio_jack_mode_to_index(local_audio_jack_mode),
                                        audio_jack_mode_cb);
            }
#endif
#if UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING
            if (hw_has_mic_input_source_setting()) {
                ui_create_card_dropdown(sys_deferred_card, LV_SYMBOL_AUDIO, "Mic Source",
                                        "Internal\n3.5mm",
                                        mic_input_source_to_index(local_mic_input_source),
                                        mic_input_source_cb);
            }
#endif
        }
#endif
        sys_deferred_step = UI_SYS_BUILD_POWER_CARD;
        break;

    case UI_SYS_BUILD_POWER_CARD:
#if FACTORY_HAS_POWER_MANAGE
        sys_deferred_card = ui_create_card(page_container, "Power");
        sys_deferred_step = UI_SYS_BUILD_POWER_OTG;
#else
        sys_deferred_step = UI_SYS_BUILD_BUTTONS_CARD;
#endif
        break;

    case UI_SYS_BUILD_POWER_OTG:
        if (hw_has_otg_function()) {
            bool enableOtg = hw_get_otg_enable();
            ui_create_card_switch(sys_deferred_card, LV_SYMBOL_POWER, "OTG Output",
                                  enableOtg, otg_output_cb);
        }
        sys_deferred_step = UI_SYS_BUILD_POWER_CHARGER;
        break;

    case UI_SYS_BUILD_POWER_CHARGER:
        ui_create_card_switch(sys_deferred_card, LV_SYMBOL_POWER, "Charger",
                              local_param.charger_enable, charger_enable_cb);
        sys_deferred_step = UI_SYS_BUILD_POWER_CHARGE_CURRENT;
        break;

    case UI_SYS_BUILD_POWER_CHARGE_CURRENT: {
        uint8_t total_charge_level = hw_get_charge_level_nums();
        uint8_t curr_charge_level = hw_get_charger_current_level();
        ui_create_card_slider(sys_deferred_card, LV_SYMBOL_POWER, "Charge Current",
                              0, total_charge_level, curr_charge_level, charger_current_cb);
        sys_deferred_step = UI_SYS_BUILD_BUTTONS_CARD;
        break;
    }

    case UI_SYS_BUILD_BUTTONS_CARD:
        if (hw_has_button_monitor() || hw_has_pmu_button_monitor()) {
            sys_deferred_card = ui_create_card(page_container, "Buttons");
            sys_deferred_step = UI_SYS_BUILD_BUTTON_DEVICE;
        } else {
            sys_deferred_step = UI_SYS_BUILD_ENV_CARD;
        }
        break;

    case UI_SYS_BUILD_BUTTON_DEVICE:
        if (hw_has_button_monitor()) {
            char button_status[48];
            hw_get_button_monitor_status(button_status, sizeof(button_status));
            lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_OK,
                                                "Device Button", button_status);
            sys_label.button_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        }
        sys_deferred_step = UI_SYS_BUILD_BUTTON_PMU;
        break;

    case UI_SYS_BUILD_BUTTON_PMU:
        if (hw_has_pmu_button_monitor()) {
            char button_status[48];
            hw_get_pmu_button_monitor_status(button_status, sizeof(button_status));
            lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_POWER,
                                                "PMU Button", button_status);
            sys_label.pmu_button_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        }
        sys_deferred_step = UI_SYS_BUILD_ENV_CARD;
        break;

    case UI_SYS_BUILD_ENV_CARD:
#ifdef USING_BME280
        sys_deferred_card = ui_create_card(page_container, "Environment");
        sys_deferred_step = UI_SYS_BUILD_ENV_TEMPERATURE;
#else
        sys_deferred_step = UI_SYS_BUILD_DEVICES_CARD;
#endif
        break;

    case UI_SYS_BUILD_ENV_TEMPERATURE:
#ifdef USING_BME280
        {
            lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_SETTINGS,
                                                "Temperature", "N.A");
            temperature = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        }
#endif
        sys_deferred_step = UI_SYS_BUILD_ENV_HUMIDITY;
        break;

    case UI_SYS_BUILD_ENV_HUMIDITY:
#ifdef USING_BME280
        {
            lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_SETTINGS,
                                                "Humidity", "N.A");
            humidity = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        }
#endif
        sys_deferred_step = UI_SYS_BUILD_ENV_PRESSURE;
        break;

    case UI_SYS_BUILD_ENV_PRESSURE:
#ifdef USING_BME280
        {
            lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_SETTINGS,
                                                "Pressure", "N.A");
            pressure = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        }
#endif
        sys_deferred_step = UI_SYS_BUILD_ENV_ALTITUDE;
        break;

    case UI_SYS_BUILD_ENV_ALTITUDE:
#ifdef USING_BME280
        {
            lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_SETTINGS,
                                                "Altitude", "N.A");
            altitude = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        }
#endif
        sys_deferred_step = UI_SYS_BUILD_DEVICES_CARD;
        break;

    case UI_SYS_BUILD_DEVICES_CARD:
        sys_deferred_card = ui_create_card(page_container, "Devices");
        sys_deferred_device_index = 0;
        sys_deferred_device_count = hw_get_devices_nums();
        sys_deferred_device_mask = hw_get_device_online();
        sys_deferred_step = UI_SYS_BUILD_DEVICES_ROWS;
        break;

    case UI_SYS_BUILD_DEVICES_ROWS:
        while (sys_deferred_device_index < sys_deferred_device_count) {
            uint8_t index = sys_deferred_device_index++;
            const char *device_name = hw_get_devices_name(index);
            if (lv_strcmp(device_name, "") != 0) {
                const bool online = (sys_deferred_device_mask & (1UL << index)) != 0;
                lv_obj_t *row = ui_create_card_info(sys_deferred_card, LV_SYMBOL_OK,
                                                    device_name, online ? "Online" : "Offline");
                lv_obj_t *status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
                lv_obj_set_style_text_color(status_label,
                                            online ? lv_color_hex(0x2FB344) : lv_color_hex(0xFF4444), 0);
                break;
            }
        }
        if (sys_deferred_device_index >= sys_deferred_device_count) {
            sys_deferred_step = UI_SYS_BUILD_SYSTEM_CARD;
        }
        break;

    case UI_SYS_BUILD_SYSTEM_CARD:
        sys_deferred_card = ui_create_card(page_container, "System Info");
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_EXPAND_FW;
        break;

    case UI_SYS_BUILD_SYSTEM_EXPAND_FW:
        if (hw_has_indicator_led()) {
            ui_create_card_info(sys_deferred_card, LV_SYMBOL_BELL,
                                "Expand FW", hw_get_expands_fw_version());
        }
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_MAC;
        break;

    case UI_SYS_BUILD_SYSTEM_MAC: {
        char buffer[32];
        uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        bool has_mac = hw_get_mac(mac);
        if (has_mac) {
            snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_WIFI, "MAC",
                            has_mac ? buffer : "N.A");
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_WIFI_SSID;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_WIFI_SSID: {
        char wifi_ssid[WIFI_SSID_MAX_LEN];
        hw_get_wifi_ssid(wifi_ssid, sizeof(wifi_ssid));
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_WIFI, "WiFi SSID", wifi_ssid);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_RTC;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_RTC: {
        lv_obj_t *dt_item = ui_create_card_info(sys_deferred_card, LV_SYMBOL_BELL,
                                                "RTC", "00:00:00");
        sys_label.datetime_label = lv_obj_get_child(dt_item, lv_obj_get_child_count(dt_item) - 1);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_IP;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_IP: {
        static char ip_info[24] = "N.A";
        hw_get_ip_address(ip_info, sizeof(ip_info));
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_WIFI, "IP", ip_info);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_RSSI;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_RSSI: {
        lv_obj_t *rssi_item = ui_create_card_info(sys_deferred_card, LV_SYMBOL_WIFI,
                                                  "RSSI", "N.A");
        sys_label.wifi_rssi_label = lv_obj_get_child(rssi_item, lv_obj_get_child_count(rssi_item) - 1);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_VOLTAGE;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_VOLTAGE: {
        char buffer[32];
        format_battery_voltage(buffer, sizeof(buffer));
        lv_obj_t *volt_item = ui_create_card_info(sys_deferred_card, LV_SYMBOL_BATTERY_FULL,
                                                  "Voltage", buffer);
        sys_label.batt_voltage_label = lv_obj_get_child(volt_item, lv_obj_get_child_count(volt_item) - 1);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_STORAGE;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_STORAGE: {
#if defined(HAS_SD_CARD_SOCKET)
        const char *storage_label = "SD Card";
#else
        const char *storage_label = "Storage";
#endif
        lv_obj_t *storage_item = ui_create_card_info(sys_deferred_card, LV_SYMBOL_SD_CARD,
                                                     storage_label, "Loading");
        sys_label.storage_label = lv_obj_get_child(storage_item, lv_obj_get_child_count(storage_item) - 1);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_LVGL;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_LVGL: {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "V%d.%d.%d",
                 lv_version_major(), lv_version_minor(), lv_version_patch());
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_EYE_OPEN, "LVGL", buffer);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_ARDUINO;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_ARDUINO: {
        char ver[16];
        hw_get_arduino_version(ver, sizeof(ver));
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_EYE_OPEN, "Arduino", ver);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_BUILD;
        break;
    }

    case UI_SYS_BUILD_SYSTEM_BUILD:
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_EYE_OPEN,
                            "Build", __DATE__ " " __TIME__);
        sys_deferred_step = UI_SYS_BUILD_SYSTEM_CHIP_ID;
        break;

    case UI_SYS_BUILD_SYSTEM_CHIP_ID:
        ui_create_card_info(sys_deferred_card, LV_SYMBOL_EYE_OPEN,
                            "Chip ID", hw_get_chip_id_string());
        sys_deferred_step = UI_SYS_BUILD_DONE;
        break;

    case UI_SYS_BUILD_DONE:
    default:
        sys_deferred_step = UI_SYS_BUILD_DONE;
        break;
    }

    return sys_deferred_step == UI_SYS_BUILD_DONE;
}

static void finish_sys_deferred_build(lv_timer_t *t)
{
    if (deferred_timer) {
        lv_timer_del(deferred_timer);
        deferred_timer = NULL;
    } else if (t) {
        lv_timer_del(t);
    }
    if (page_container) {
        start_sys_refresh_timer();
    }
}

static void sys_deferred_event_cb(lv_timer_t *t)
{
    if (run_sys_deferred_step()) {
        finish_sys_deferred_build(t);
    }
}

static void start_sys_deferred_build()
{
    if (deferred_timer) {
        lv_timer_del(deferred_timer);
        deferred_timer = NULL;
    }

    sys_deferred_step = UI_SYS_BUILD_DISPLAY_CARD;
    sys_deferred_card = NULL;
    sys_deferred_device_index = 0;
    sys_deferred_device_count = 0;
    sys_deferred_device_mask = 0;
    deferred_timer = lv_timer_create(sys_deferred_event_cb, UI_SYS_DEFERRED_PERIOD_MS, NULL);
    if (!deferred_timer) {
        while (!run_sys_deferred_step()) {
        }
        start_sys_refresh_timer();
    }
}

void ui_sys_enter(lv_obj_t *parent)
{
    menu_g = lv_group_get_default();
    hw_get_user_setting(local_param);
    theme_preset_idx = local_param.theme_preset_idx;
    sys_label.datetime_label = NULL;
    sys_label.wifi_rssi_label = NULL;
    sys_label.batt_voltage_label = NULL;
    sys_label.button_label = NULL;
    sys_label.pmu_button_label = NULL;
    sys_label.storage_label = NULL;
    storage_size_loaded = false;
#if UI_SYS_HAS_ROTARY_STEP_SETTING
    local_rotary_step_divider = hw_get_rotary_step_divider();
#endif
#if UI_SYS_HAS_KEYBOARD_NAV_SETTING
    local_keyboard_navigation_enabled = hw_get_keyboard_navigation_enabled();
#endif
#if UI_SYS_HAS_AUDIO_JACK_SETTING
    local_audio_jack_mode = hw_get_audio_jack_mode();
#endif
#if UI_SYS_HAS_MIC_INPUT_SOURCE_SETTING
    local_mic_input_source = hw_get_mic_input_source();
#endif

    page_container = ui_create_app_page(parent, "Setting", back_event_handler);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif

    start_sys_deferred_build();
}


void ui_sys_exit(lv_obj_t *parent)
{

}

app_t ui_sys_main = {
    .setup_func_cb = ui_sys_enter,
    .exit_func_cb = ui_sys_exit,
    .user_data = nullptr,
};
