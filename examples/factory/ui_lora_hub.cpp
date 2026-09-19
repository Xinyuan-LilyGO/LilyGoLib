/**
 * @file      ui_lora_hub.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-01
 * 
 */
#include "ui_define.h"

#ifndef EXCLUDE_LORA

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef ARDUINO
#include <Preferences.h>
#endif

#define LORA_HUB_INTERVAL_LIST "1s\n2s\n5s\n10s\n30s\nCustom"
#define LORA_HUB_PAYLOAD_MODE_LIST "Counter\nFixed"
#define LORA_HUB_SYNC_WORD_LIST "Private (0x12)\nPublic (0x34)\nCustom"
#define LORA_HUB_FIXED_PAYLOAD_DEFAULT "LilyGo LoRa Factory"
#define LORA_HUB_SF_LIST       "5\n6\n7\n8\n9\n10\n11\n12"
#define LORA_HUB_CR_LIST       "5\n6\n7\n8"
#define LORA_HUB_MAX_MESSAGES  24
#define LORA_HUB_USER_MAX      6
#define LORA_HUB_INTERVAL_MIN_MS 100
#define LORA_HUB_TITLE_CLICK_GAP_MS 600
#define LORA_HUB_SHORTCUT_PRESS_COUNT 5
#define LORA_HUB_SHORTCUT_PRESS_GAP_MS 1000

static const uint32_t interval_values[] = {1000, 2000, 5000, 10000, 30000};

#define LORA_HUB_TX_POLL_MS 50
#define LORA_HUB_TX_TIMEOUT_GUARD_MS 1500
#define LORA_HUB_TX_PAYLOAD_MAX 160

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
#define LORA_HUB_ULTRA_TOP_PAD        20
#define LORA_HUB_ULTRA_DROPDOWN_WIDTH 180
#define LORA_HUB_MODE_APPLY_DELAY_MS 120
#define LORA_HUB_ULTRA_ADD_BTN_MARGIN 28
#define LORA_HUB_ULTRA_STATE_TITLE_WIDTH 48
#define LORA_HUB_ULTRA_STATE_VALUE_WIDTH 140
#endif

typedef struct {
    char name[24];
    float freq;
    float bandwidth;
    uint8_t power;
    uint8_t sf;
    uint16_t cr;
    uint8_t sync_word;
    uint32_t interval_ms;
    bool builtin;
} lora_profile_entry_t;

typedef enum {
    LORA_SCREEN_LIST = 0,
    LORA_SCREEN_CHAT,
    LORA_SCREEN_EDIT,
} lora_screen_t;

typedef enum {
    LORA_SESSION_LISTEN = 0,
    LORA_SESSION_CHAT,
    LORA_SESSION_AUTO_TX,
    LORA_SESSION_CW,
} lora_session_mode_t;

typedef enum {
    LORA_PAYLOAD_COUNTER = 0,
    LORA_PAYLOAD_FIXED,
} lora_payload_mode_t;

typedef enum {
    LORA_SYNC_PRIVATE = 0,
    LORA_SYNC_PUBLIC,
    LORA_SYNC_CUSTOM,
} lora_sync_mode_t;



#if defined(ARDUINO_LILYGO_LORA_SX1280)
const float default_bandwidth = 203.125;
#elif defined(ARDUINO_T_WATCH_S3_ULTRA)
const float default_bandwidth = 62.5;
#else
const float default_bandwidth = 125.0;
#endif

#if defined(ARDUINO_LILYGO_LORA_SX1280) || defined(ARDUINO_LILYGO_LORA_LR1121)
#ifdef LILYGO_RADIO_2G4_TX_POWER_LIMIT
static const uint8_t factory_2g4_power = LILYGO_RADIO_2G4_TX_POWER_LIMIT;
#else
static const uint8_t factory_2g4_power = 13;
#endif
#endif

static const lora_profile_entry_t factory_profiles[] = {
#if !defined(ARDUINO_LILYGO_LORA_SX1280)
    {"Factory 433", 433.0f, default_bandwidth, 22, 12, 5, 0xCD, 1000, true},
    {"Factory 868", 868.0f, default_bandwidth, 22, 12, 5, 0xCD, 1000, true},
    {"Factory 915", 915.0f, default_bandwidth, 22, 12, 5, 0xCD, 1000, true},
    {"Factory 920", 920.0f, default_bandwidth, 22, 12, 5, 0xCD, 1000, true},
    {"Factory 923", 923.0f, default_bandwidth, 22, 12, 5, 0xCD, 1000, true},
#endif
#if defined(ARDUINO_LILYGO_LORA_SX1280) || defined(ARDUINO_LILYGO_LORA_LR1121)
    {"Factory 2400", 2400.0f, 203.125f, factory_2g4_power, 12, 5, 0xCD, 1000, true},
    {"Factory 2420", 2420.0f, 203.125f, factory_2g4_power, 12, 5, 0xCD, 1000, true},
    {"Factory 2440", 2440.0f, 203.125f, factory_2g4_power, 12, 5, 0xCD, 1000, true},
    {"Factory 2460", 2460.0f, 203.125f, factory_2g4_power, 12, 5, 0xCD, 1000, true},
    {"Factory 2480", 2480.0f, 203.125f, factory_2g4_power, 12, 5, 0xCD, 1000, true},
#endif
};

#define LORA_HUB_FACTORY_COUNT (sizeof(factory_profiles) / sizeof(factory_profiles[0]))
#define LORA_HUB_MAX_PROFILES  (LORA_HUB_FACTORY_COUNT + LORA_HUB_USER_MAX)

static lv_obj_t *page_container = NULL;
static lv_obj_t *add_profile_btn = NULL;
static lv_obj_t *nav_edit_btn = NULL;
static lv_obj_t *confirm_msgbox = NULL;
static lv_obj_t *cw_warning_msgbox = NULL;
static lv_timer_t *rx_timer = NULL;
static lv_timer_t *auto_tx_timer = NULL;
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
static lv_timer_t *factory_mode_apply_timer = NULL;
#endif
static lv_timer_t *tx_complete_timer = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *rf_summary_label = NULL;
static lv_obj_t *traffic_label = NULL;
static lv_obj_t *msg_page = NULL;
static lv_obj_t *msg_cont = NULL;
static lv_obj_t *input_bar = NULL;
static lv_obj_t *input_bar_parent = NULL;
static lv_obj_t *input_ta = NULL;
static lv_obj_t *factory_mode_dd = NULL;
static lv_obj_t *payload_mode_row = NULL;
#if defined(HAS_USB_RF_SWITCH)
static lv_obj_t *antenna_rf_switch_dd = NULL;
#endif
static lv_obj_t *edit_name_ta = NULL;
static lv_obj_t *edit_freq_dd = NULL;
static lv_obj_t *edit_bw_dd = NULL;
static lv_obj_t *edit_power_dd = NULL;
static lv_obj_t *edit_interval_dd = NULL;
static lv_obj_t *edit_interval_custom_row = NULL;
static lv_obj_t *edit_interval_custom_divider = NULL;
static lv_obj_t *edit_interval_ta = NULL;
static lv_obj_t *edit_sf_dd = NULL;
static lv_obj_t *edit_cr_dd = NULL;
static lv_obj_t *edit_sync_dd = NULL;
static lv_obj_t *edit_sync_custom_row = NULL;
static lv_obj_t *edit_sync_custom_divider = NULL;
static lv_obj_t *edit_sync_ta = NULL;
static ui_soft_keyboard_lift_t keyboard_lift = {NULL, NULL, -1};

#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;
#endif

static radio_params_t lora_params;
static lora_profile_entry_t profiles[LORA_HUB_MAX_PROFILES];
static uint8_t profile_count = 0;
static uint8_t active_profile_index = 0;
static uint8_t chat_profile_index = 0;
static bool only_920_profiles = false;
static uint8_t title_click_count = 0;
static uint32_t title_click_tick = 0;
static vector<float> edit_frequencies;
static lora_profile_entry_t editing_profile;
static int16_t editing_source_index = -1;
static bool edit_dirty = false;
static bool edit_return_to_chat = false;
static bool chat_scroll_mode = false;
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER)
static bool profile_filter_shortcut_registered = false;
static bool profile_filter_shortcut_pending = false;
static uint8_t profile_filter_shortcut_count = 0;
static uint32_t profile_filter_shortcut_tick = 0;
#endif
#if defined(HAS_USB_RF_SWITCH)
static bool antenna_rf_switch_to_usb = false;
#endif
static lora_screen_t current_screen = LORA_SCREEN_LIST;
static lora_session_mode_t session_mode = LORA_SESSION_LISTEN;
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
static lora_session_mode_t pending_factory_mode = LORA_SESSION_LISTEN;
#endif
static lora_payload_mode_t auto_payload_mode = LORA_PAYLOAD_COUNTER;
static bool auto_tx_busy = false;
static bool tx_in_progress = false;
static bool radio_configured = false;
static bool tx_started_by_auto = false;
static uint32_t tx_started_tick = 0;
static uint32_t tx_timeout_ms = 0;
static char tx_payload_storage[LORA_HUB_TX_PAYLOAD_MAX];
static uint32_t tx_counter = 0;
static uint32_t rx_counter = 0;
static uint32_t auto_payload_counter = 0;
static int msg_count = 0;
static char recv_buf[256];
static radio_rx_params_t rx_params;

static void render_profile_list(void);
static void title_click_cb(lv_event_t *e);
static void render_chat(uint8_t index);
static void render_profile_editor(int16_t source_index, bool return_to_chat);
static void edit_current_cb(lv_event_t *e);
static void set_auto_tx_status(void);
static void set_hidden(lv_obj_t *obj, bool hidden);
static void hide_keyboard(void);
static bool toggle_920_profile_filter(void);
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER)
static void profile_filter_shortcut_async_cb(void *);
#endif

static const lv_font_t *font_title(void)
{
    uint8_t pref = ui_get_font_size_pref();
    if (pref == 1) return &lv_font_montserrat_14;
    if (pref >= 3) return &lv_font_montserrat_18;
    return &lv_font_montserrat_16;
}

static const lv_font_t *font_body(void)
{
    uint8_t pref = ui_get_font_size_pref();
    if (pref == 1) return &lv_font_montserrat_12;
    if (pref >= 3) return &lv_font_montserrat_16;
    return &lv_font_montserrat_14;
}

static const lv_font_t *font_meta(void)
{
    uint8_t pref = ui_get_font_size_pref();
    if (pref == 1) return &lv_font_montserrat_10;
    if (pref >= 3) return &lv_font_montserrat_14;
    return &lv_font_montserrat_12;
}

static const char *radio_tx_state_text(int16_t state)
{
    switch (state) {
    case 0: return "OK";
    case -2: return "chip not found";
    case -4: return "packet too long";
    case -5: return "TX timeout";
    case -8: return "invalid bandwidth";
    case -9: return "invalid spreading factor";
    case -10: return "invalid coding rate";
    case -12: return "invalid frequency";
    case -13: return "invalid TX power";
    case -16: return "SPI write failed";
    case -17: return "invalid current limit";
    case -101: return "invalid bit rate";
    case -102: return "invalid frequency deviation";
    case -104: return "invalid RX bandwidth";
    case -105: return "invalid sync word";
    default: return "RadioLib error";
    }
}

static bool is_high_freq(float freq)
{
    return freq > 960.0f;
}

static bool frequency_supported(float freq)
{
#if defined(ARDUINO_LILYGO_LORA_SX1280)
    return freq >= 2400.0f && freq <= 2500.0f;
#elif defined(ARDUINO_LILYGO_LORA_LR1121)
    return (freq >= 150.0f && freq <= 960.0f) || (freq >= 2400.0f && freq <= 2500.0f);
#else
    return freq >= 150.0f && freq <= 960.0f;
#endif
}

static bool profile_supported(const lora_profile_entry_t &profile)
{
    if (!frequency_supported(profile.freq) || !isfinite(profile.bandwidth) ||
            profile.sf < 5 || profile.sf > 12 || profile.cr < 5 || profile.cr > 8) return false;
    bool high = is_high_freq(profile.freq);
    uint8_t max_power = high ? 13 : 22;
#ifdef LILYGO_RADIO_2G4_TX_POWER_LIMIT
    if (high) max_power = LILYGO_RADIO_2G4_TX_POWER_LIMIT;
#endif
    if (profile.power > max_power) return false;
    radio_get_bandwidth_list(high);
    for (uint16_t i = 0; i < radio_get_bandwidth_length(); ++i) {
        if (fabsf(profile.bandwidth - radio_get_bandwidth_from_index(i)) < 0.001f) return true;
    }
    return false;
}

static bool profile_visible(const lora_profile_entry_t &profile)
{
    return profile_supported(profile) && (!only_920_profiles || fabsf(profile.freq - 920.0f) < 0.001f);
}

static float edit_frequency_from_index(uint8_t index)
{
    return index < edit_frequencies.size() ? edit_frequencies[index] : editing_profile.freq;
}

static void populate_edit_frequencies(void)
{
    edit_frequencies.clear();
    auto add_frequency = [](float freq) {
        if (!frequency_supported(freq) || (only_920_profiles && fabsf(freq - 920.0f) >= 0.001f)) return;
        for (float value : edit_frequencies) {
            if (fabsf(value - freq) < 0.001f) return;
        }
        edit_frequencies.push_back(freq);
        char option[24];
        snprintf(option, sizeof(option), "%g MHz", (double)freq);
        lv_dropdown_add_option(edit_freq_dd, option, LV_DROPDOWN_POS_LAST);
    };
    lv_dropdown_clear_options(edit_freq_dd);
    for (const auto &profile : factory_profiles) add_frequency(profile.freq);
    for (uint16_t i = 0; i < radio_get_freq_length(); ++i) add_frequency(radio_get_freq_from_index(i));
    add_frequency(editing_profile.freq);
}

static const char *time_text(void)
{
    static char time_buf[16];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) {
        snprintf(time_buf, sizeof(time_buf), "--:--:--");
    } else {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    }
    return time_buf;
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text ? text : "--");
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void format_profile_summary(const lora_profile_entry_t &profile, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "%.1f MHz  BW %g  SF%u  CR4/%u  P%u",
             profile.freq,
             profile.bandwidth,
             (unsigned)profile.sf,
             (unsigned)profile.cr,
             (unsigned)profile.power);
}

static void format_params_summary(const radio_params_t &params, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "%.1f MHz  BW %g  SF%u  CR4/%u  P%u",
             params.freq,
             params.bandwidth,
             (unsigned)params.sf,
             (unsigned)params.cr,
             (unsigned)params.power);
}

static void format_interval_ms(uint32_t interval_ms, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (interval_ms >= 1000 && interval_ms % 1000 == 0) {
        snprintf(out, out_len, "%lus", (unsigned long)(interval_ms / 1000));
    } else {
        snprintf(out, out_len, "%lums", (unsigned long)interval_ms);
    }
}

static uint16_t interval_custom_index(void)
{
    return sizeof(interval_values) / sizeof(interval_values[0]);
}

static uint16_t interval_dropdown_from_ms(uint32_t interval_ms)
{
    for (uint16_t i = 0; i < interval_custom_index(); ++i) {
        if (interval_values[i] == interval_ms) return i;
    }
    return interval_custom_index();
}

static uint32_t parse_interval_seconds_text(const char *text, uint32_t fallback_ms)
{
    if (!text) return fallback_ms ? fallback_ms : 1000;
    while (*text == ' ') ++text;
    if (*text == '\0') return fallback_ms ? fallback_ms : 1000;

    char *end = NULL;
    double seconds = strtod(text, &end);
    if (end == text) return fallback_ms ? fallback_ms : 1000;
    if (seconds < 0.1) seconds = 0.1;

    double max_seconds = (double)UINT32_MAX / 1000.0;
    if (seconds > max_seconds) seconds = max_seconds;
    uint32_t interval_ms = (uint32_t)(seconds * 1000.0 + 0.5);
    return interval_ms < LORA_HUB_INTERVAL_MIN_MS ? LORA_HUB_INTERVAL_MIN_MS : interval_ms;
}

static void format_interval_seconds_text(uint32_t interval_ms, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (interval_ms < LORA_HUB_INTERVAL_MIN_MS) interval_ms = 1000;

    uint32_t seconds = interval_ms / 1000;
    uint32_t frac = interval_ms % 1000;
    if (frac == 0) {
        snprintf(out, out_len, "%lu", (unsigned long)seconds);
    } else if (frac % 100 == 0) {
        snprintf(out, out_len, "%lu.%lu", (unsigned long)seconds, (unsigned long)(frac / 100));
    } else if (frac % 10 == 0) {
        snprintf(out, out_len, "%lu.%02lu", (unsigned long)seconds, (unsigned long)(frac / 10));
    } else {
        snprintf(out, out_len, "%lu.%03lu", (unsigned long)seconds, (unsigned long)frac);
    }
}

static uint32_t estimate_lora_tx_time_ms(size_t payload_len)
{
    float bw = lora_params.bandwidth > 0.0f ? lora_params.bandwidth : default_bandwidth;
    uint8_t sf = lora_params.sf < 5 ? 5 : (lora_params.sf > 12 ? 12 : lora_params.sf);
    uint8_t cr = lora_params.cr < 5 ? 5 : (lora_params.cr > 8 ? 8 : lora_params.cr);
    float tsym_ms = (float)(1UL << sf) / bw;
    bool low_data_rate_optimize = tsym_ms >= 16.0f;
    int32_t denominator = 4 * (sf - (low_data_rate_optimize ? 2 : 0));
    int32_t numerator = (int32_t)(8 * payload_len) - (4 * sf) + 28 + 16;
    int32_t payload_symbols = 8;

    if (numerator > 0 && denominator > 0) {
        payload_symbols += ((numerator + denominator - 1) / denominator) * cr;
    }

    float total_ms = (12.25f + payload_symbols) * tsym_ms;
    if (total_ms < 50.0f) total_ms = 50.0f;
    return (uint32_t)ceilf(total_ms);
}

static uint32_t auto_tx_effective_interval(void)
{
    uint32_t period = lora_params.interval ? lora_params.interval : 1000;
    if (period < LORA_HUB_INTERVAL_MIN_MS) period = LORA_HUB_INTERVAL_MIN_MS;
    return period;
}

static const char *payload_mode_text(void)
{
    return auto_payload_mode == LORA_PAYLOAD_FIXED ? "Fixed" : "Counter";
}

static uint16_t session_mode_to_dropdown(lora_session_mode_t mode)
{
    switch (mode) {
    case LORA_SESSION_CHAT:
        return 1;
    case LORA_SESSION_AUTO_TX:
        return 2;
    case LORA_SESSION_CW:
        return 3;
    case LORA_SESSION_LISTEN:
    default:
        return 0;
    }
}

static lora_session_mode_t session_mode_from_dropdown(uint16_t selected)
{
    if (selected == 1) return LORA_SESSION_CHAT;
    if (selected == 2) return LORA_SESSION_AUTO_TX;
    if (selected == 3) return LORA_SESSION_CW;
    return LORA_SESSION_LISTEN;
}

static bool deserialize_profile(const char *raw, lora_profile_entry_t &out)
{
    char name[24] = {0};
    float freq = 0.0f;
    float bw = 0.0f;
    unsigned power = 0;
    unsigned sf = 0;
    unsigned cr = 0;
    unsigned sync = 0;
    unsigned long interval = 0;
    int matched = sscanf(raw ? raw : "", "%23[^|]|%f|%f|%u|%u|%u|%u|%lu",
                         name, &freq, &bw, &power, &sf, &cr, &sync, &interval);
    if (matched != 8 || name[0] == '\0' || !isfinite(freq) || !isfinite(bw) ||
            freq <= 0.0f || bw <= 0.0f || power > 30 || sf < 5 || sf > 12 ||
            cr < 5 || cr > 8 || sync > 255) return false;

    snprintf(out.name, sizeof(out.name), "%s", name);
    out.freq = freq;
    out.bandwidth = bw;
    out.power = (uint8_t)constrain((int)power, 0, 30);
    out.sf = (uint8_t)constrain((int)sf, 5, 12);
    out.cr = (uint16_t)constrain((int)cr, 5, 8);
    out.sync_word = (uint8_t)constrain((int)sync, 0, 255);
    out.interval_ms = interval == 0 ? 1000 : (uint32_t)interval;
    out.builtin = false;
    // Older profiles stored only one decimal place, losing 2.4 GHz bandwidth precision.
    if (frequency_supported(freq)) {
        radio_get_bandwidth_list(is_high_freq(freq));
        for (uint16_t i = 0; i < radio_get_bandwidth_length(); ++i) {
            float supported_bw = radio_get_bandwidth_from_index(i);
            if (fabsf(out.bandwidth - supported_bw) <= 0.051f) {
                out.bandwidth = supported_bw;
                break;
            }
        }
    }
    return true;
}

static void serialize_profile(const lora_profile_entry_t &profile, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "%s|%.3f|%.3f|%u|%u|%u|%u|%lu",
             profile.name,
             profile.freq,
             profile.bandwidth,
             (unsigned)profile.power,
             (unsigned)profile.sf,
             (unsigned)profile.cr,
             (unsigned)profile.sync_word,
             (unsigned long)profile.interval_ms);
}

static void save_active_profile_index(void)
{
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("loraHub", false)) {
        prefs.putUChar("active", active_profile_index);
        prefs.putUChar("factory", LORA_HUB_FACTORY_COUNT);
        prefs.end();
    }
#endif
}

static bool save_profile_filter(bool only_920)
{
#ifdef ARDUINO
    Preferences prefs;
    if (!prefs.begin("loraHub", false)) return false;
    bool saved = prefs.putBool("only920", only_920) != 0;
    prefs.end();
    return saved;
#else
    (void)only_920;
    return true;
#endif
}

static void save_user_profiles(void)
{
#ifdef ARDUINO
    Preferences prefs;
    if (!prefs.begin("loraHub", false)) return;

    uint8_t user_count = profile_count > LORA_HUB_FACTORY_COUNT ? profile_count - LORA_HUB_FACTORY_COUNT : 0;
    prefs.putUChar("count", user_count);
    for (uint8_t i = 0; i < user_count; ++i) {
        char key[8];
        char raw[128];
        snprintf(key, sizeof(key), "p%u", (unsigned)i);
        serialize_profile(profiles[LORA_HUB_FACTORY_COUNT + i], raw, sizeof(raw));
        prefs.putString(key, raw);
    }
    for (uint8_t i = user_count; i < LORA_HUB_USER_MAX; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "p%u", (unsigned)i);
        prefs.remove(key);
    }
    prefs.putUChar("active", active_profile_index);
    prefs.putUChar("factory", LORA_HUB_FACTORY_COUNT);
    prefs.end();
#endif
}

static void load_profiles(void)
{
    profile_count = 0;
    for (uint8_t i = 0; i < LORA_HUB_FACTORY_COUNT; ++i) {
        profiles[profile_count++] = factory_profiles[i];
    }
    active_profile_index = 0;

#ifdef ARDUINO
    only_920_profiles = false;
    Preferences prefs;
    if (prefs.begin("loraHub", true)) {
        only_920_profiles = frequency_supported(920.0f) && prefs.getBool("only920", false);
        uint8_t saved_active = prefs.getUChar("active", 0);
        uint8_t saved_factory_count = prefs.getUChar("factory", 5);
        if (saved_active < saved_factory_count && saved_active < LORA_HUB_FACTORY_COUNT &&
                saved_factory_count > 1 && LORA_HUB_FACTORY_COUNT > 1) {
            active_profile_index = saved_active;
        }
        uint8_t user_count = prefs.getUChar("count", 0);
        user_count = user_count > LORA_HUB_USER_MAX ? LORA_HUB_USER_MAX : user_count;
        for (uint8_t i = 0; i < user_count && profile_count < LORA_HUB_MAX_PROFILES; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "p%u", (unsigned)i);
            String raw = prefs.getString(key, "");
            lora_profile_entry_t entry;
            if (deserialize_profile(raw.c_str(), entry)) {
                if (saved_active == saved_factory_count + i) active_profile_index = profile_count;
                profiles[profile_count++] = entry;
            }
        }
        prefs.end();
    }
#endif
    if (!profile_visible(profiles[active_profile_index])) active_profile_index = 0;
}

static void profile_to_params(const lora_profile_entry_t &profile)
{
    lora_params.freq = profile.freq;
    lora_params.bandwidth = profile.bandwidth;
    lora_params.power = profile.power;
    lora_params.sf = profile.sf;
    lora_params.cr = profile.cr;
    lora_params.syncWord = profile.sync_word;
    lora_params.interval = profile.interval_ms;
    lora_params.mode = RADIO_RX;
}

static uint8_t find_nearest_float_index(float value, float (*getter)(uint8_t), uint16_t length)
{
    if (length == 0) return 0;
    uint8_t best = 0;
    float best_diff = 100000.0f;
    for (uint8_t i = 0; i < length; ++i) {
        float diff = fabsf(getter(i) - value);
        if (diff < best_diff) {
            best = i;
            best_diff = diff;
        }
    }
    return best;
}

static void select_dropdown_by_float(lv_obj_t *dd, float value, float (*getter)(uint8_t), uint16_t length)
{
    if (!dd || length == 0) return;
    lv_dropdown_set_selected(dd, find_nearest_float_index(value, getter, length));
}

static void set_interval_custom_visible(bool visible)
{
    set_hidden(edit_interval_custom_row, !visible);
    set_hidden(edit_interval_custom_divider, !visible);
    if (!visible && edit_interval_ta) {
        hide_keyboard();
    }
}

static void apply_interval_dropdown_selection(uint16_t selected)
{
    if (selected < interval_custom_index()) {
        editing_profile.interval_ms = interval_values[selected];
        if (edit_interval_ta) {
            char interval_buf[12];
            format_interval_seconds_text(editing_profile.interval_ms, interval_buf, sizeof(interval_buf));
            lv_textarea_set_text(edit_interval_ta, interval_buf);
        }
        set_interval_custom_visible(false);
        return;
    }

    if (edit_interval_ta) {
        editing_profile.interval_ms = parse_interval_seconds_text(lv_textarea_get_text(edit_interval_ta),
                                      editing_profile.interval_ms);
        char interval_buf[12];
        format_interval_seconds_text(editing_profile.interval_ms, interval_buf, sizeof(interval_buf));
        lv_textarea_set_text(edit_interval_ta, interval_buf);
    }
    set_interval_custom_visible(true);
}

static uint16_t sync_dropdown_from_word(uint8_t value)
{
    if (value == 0x12) return LORA_SYNC_PRIVATE;
    if (value == 0x34) return LORA_SYNC_PUBLIC;
    return LORA_SYNC_CUSTOM;
}

static uint8_t parse_sync_word_text(const char *text, uint8_t fallback)
{
    if (!text) return fallback;
    while (*text == ' ') ++text;
    if (*text == '\0') return fallback;

    const char *start = text;
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        start = text + 2;
        base = 16;
    }

    char *end = NULL;
    unsigned long value = strtoul(start, &end, base);
    if (end == start) return fallback;
    if (value > 255) value = 255;
    return (uint8_t)value;
}

static void format_sync_word_text(uint8_t value, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "0x%02X", (unsigned)value);
}

static void set_sync_custom_visible(bool visible)
{
    set_hidden(edit_sync_custom_row, !visible);
    set_hidden(edit_sync_custom_divider, !visible);
    if (!visible && edit_sync_ta) {
        hide_keyboard();
    }
}

static void apply_sync_dropdown_selection(uint16_t selected)
{
    char sync_buf[8];

    if (selected == LORA_SYNC_PRIVATE) {
        editing_profile.sync_word = 0x12;
        format_sync_word_text(editing_profile.sync_word, sync_buf, sizeof(sync_buf));
        if (edit_sync_ta) lv_textarea_set_text(edit_sync_ta, sync_buf);
        set_sync_custom_visible(false);
        return;
    }

    if (selected == LORA_SYNC_PUBLIC) {
        editing_profile.sync_word = 0x34;
        format_sync_word_text(editing_profile.sync_word, sync_buf, sizeof(sync_buf));
        if (edit_sync_ta) lv_textarea_set_text(edit_sync_ta, sync_buf);
        set_sync_custom_visible(false);
        return;
    }

    if (edit_sync_ta) {
        editing_profile.sync_word = parse_sync_word_text(lv_textarea_get_text(edit_sync_ta),
                                    editing_profile.sync_word);
        format_sync_word_text(editing_profile.sync_word, sync_buf, sizeof(sync_buf));
        lv_textarea_set_text(edit_sync_ta, sync_buf);
    }
    set_sync_custom_visible(true);
}

static void update_edit_bw_power_options(void)
{
    bool high = is_high_freq(editing_profile.freq);
    if (edit_bw_dd) {
        lv_dropdown_set_options(edit_bw_dd, radio_get_bandwidth_list(high));
        select_dropdown_by_float(edit_bw_dd, editing_profile.bandwidth,
                                 radio_get_bandwidth_from_index, radio_get_bandwidth_length());
        editing_profile.bandwidth = radio_get_bandwidth_from_index(lv_dropdown_get_selected(edit_bw_dd));
    }
    if (edit_power_dd) {
        lv_dropdown_set_options(edit_power_dd, radio_get_tx_power_list(high));
        select_dropdown_by_float(edit_power_dd, editing_profile.power,
                                 radio_get_tx_power_from_index, radio_get_tx_power_length());
        editing_profile.power = (uint8_t)radio_get_tx_power_from_index(lv_dropdown_get_selected(edit_power_dd));
    }
}

static void update_chat_labels(void)
{
    if (rf_summary_label) {
        char buf[96];
        format_params_summary(lora_params, buf, sizeof(buf));
        lv_label_set_text(rf_summary_label, buf);
    }
    if (traffic_label) {
        if (session_mode == LORA_SESSION_CW) {
            lv_label_set_text_fmt(traffic_label, "TX %lu  RX %lu  CW active",
                                  (unsigned long)tx_counter,
                                  (unsigned long)rx_counter);
        } else if (session_mode == LORA_SESSION_AUTO_TX) {
            char period[12];
            format_interval_ms(auto_tx_effective_interval(), period, sizeof(period));
            lv_label_set_text_fmt(traffic_label, "TX %lu  RX %lu  Auto %s  %s",
                                  (unsigned long)tx_counter,
                                  (unsigned long)rx_counter,
                                  period,
                                  payload_mode_text());
        } else {
            lv_label_set_text_fmt(traffic_label, "TX %lu  RX %lu",
                                  (unsigned long)tx_counter,
                                  (unsigned long)rx_counter);
        }
    }
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void restore_lifted_input_position(void)
{
    ui_soft_keyboard_restore(&keyboard_lift);
}

static lv_obj_t *textarea_lift_obj(lv_obj_t *ta)
{
    if (ta == input_ta) {
        return input_bar;
    }
    if (current_screen == LORA_SCREEN_EDIT) {
        return lv_obj_get_parent(ta);
    }
    return NULL;
}

static void hide_keyboard(void)
{
#ifdef USING_TOUCHPAD
    ui_soft_keyboard_hide(keyboard, &keyboard_lift);
#else
    restore_lifted_input_position();
#endif
    disable_keyboard();
}

static void focus_textarea(lv_obj_t *ta)
{
    if (!ta) return;
    if (ta == input_ta && session_mode != LORA_SESSION_CHAT) return;
    lv_group_t *group = (lv_group_t *)lv_obj_get_group(ta);
    if (group) lv_group_set_editing(group, true);
    enable_keyboard();
#ifdef USING_TOUCHPAD
    if (keyboard && ui_soft_keyboard_should_open()) {
        ui_soft_keyboard_show(keyboard, ta, textarea_lift_obj(ta), &keyboard_lift);
    }
#endif
}

static void update_session_control_visibility(void)
{
    bool chat_mode = session_mode == LORA_SESSION_CHAT;
    bool auto_tx_mode = session_mode == LORA_SESSION_AUTO_TX;

    set_hidden(payload_mode_row, !auto_tx_mode);

    if (!chat_mode) {
        hide_keyboard();
    } else {
        restore_lifted_input_position();
    }
    set_hidden(input_bar, !chat_mode);
}

static void stop_auto_tx_timer(void)
{
    if (auto_tx_timer) {
        lv_timer_del(auto_tx_timer);
        auto_tx_timer = NULL;
    }
    auto_tx_busy = false;
}

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
static void stop_factory_mode_apply_timer(void)
{
    if (factory_mode_apply_timer) {
        lv_timer_del(factory_mode_apply_timer);
        factory_mode_apply_timer = NULL;
    }
}
#endif

static void stop_tx_complete_timer(void)
{
    if (tx_complete_timer) {
        lv_timer_del(tx_complete_timer);
        tx_complete_timer = NULL;
    }
    tx_in_progress = false;
    tx_started_by_auto = false;
}

static void stop_radio_session(void)
{
    radio_configured = false;
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    stop_factory_mode_apply_timer();
#endif
    stop_tx_complete_timer();
    if (rx_timer) {
        lv_timer_del(rx_timer);
        rx_timer = NULL;
    }
    stop_auto_tx_timer();
    session_mode = LORA_SESSION_LISTEN;
    if (HW_RADIO_ONLINE & hw_get_device_online()) {
        hw_set_radio_default();
    }
}

static void delete_add_button(void)
{
    if (add_profile_btn) {
        lv_obj_delete(add_profile_btn);
        add_profile_btn = NULL;
    }
}

static void delete_nav_edit_button(void)
{
    if (nav_edit_btn) {
        lv_obj_delete(nav_edit_btn);
        nav_edit_btn = NULL;
    }
}

static void add_to_default_group(lv_obj_t *obj)
{
    lv_group_t *group = lv_group_get_default();
    if (group && obj && lv_obj_get_group(obj) != group) {
        lv_group_add_obj(group, obj);
    }
}

static void focus_scroll_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (obj) {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(obj);
        if (group) {
            lv_group_set_editing(group, false);
        }
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
    }
}

static void profile_title_focus_visual_cb(lv_event_t *e)
{
    lv_obj_t *title = lv_event_get_target_obj(e);
    if (!title) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_set_style_text_color(title, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_color(title, UI_COLOR_CARD_FOCUS, 0);
        lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(title, 1, 0);
        lv_obj_set_style_border_color(title, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_radius(title, 4, 0);
        lv_obj_set_style_outline_width(title, 1, 0);
        lv_obj_set_style_outline_color(title, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_outline_opa(title, LV_OPA_COVER, 0);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(title, 0, 0);
        lv_obj_set_style_outline_width(title, 0, 0);
        lv_obj_set_style_outline_opa(title, LV_OPA_TRANSP, 0);
    }
}

static lv_indev_type_t event_indev_type(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        indev = lv_indev_active();
    }
    return indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
}

static void chat_area_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_type_t type = event_indev_type(e);
    lv_group_t *group = (lv_group_t *)lv_obj_get_group(obj);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
    } else if (code == LV_EVENT_CLICKED &&
               (type == LV_INDEV_TYPE_ENCODER || type == LV_INDEV_TYPE_KEYPAD)) {
        chat_scroll_mode = !chat_scroll_mode;
        if (group) {
            lv_group_set_editing(group, chat_scroll_mode);
        }
    } else if (code == LV_EVENT_ROTARY && chat_scroll_mode) {
        int32_t diff = lv_event_get_rotary_diff(e);
        lv_obj_scroll_to_y(obj, lv_obj_get_scroll_y(obj) + diff * 24, LV_ANIM_OFF);
        lv_event_stop_processing(e);
    } else if (code == LV_EVENT_KEY && chat_scroll_mode) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
            lv_obj_scroll_to_y(obj, lv_obj_get_scroll_y(obj) + 24, LV_ANIM_OFF);
            lv_event_stop_processing(e);
        } else if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
            lv_obj_scroll_to_y(obj, lv_obj_get_scroll_y(obj) - 24, LV_ANIM_OFF);
            lv_event_stop_processing(e);
        } else if (key == LV_KEY_ESC || key == LV_KEY_ENTER) {
            chat_scroll_mode = false;
            if (group) {
                lv_group_set_editing(group, false);
            }
            lv_event_stop_processing(e);
        }
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_LEAVE || code == LV_EVENT_DELETE) {
        chat_scroll_mode = false;
        if (group) {
            lv_group_set_editing(group, false);
        }
    }
}

static void create_nav_edit_button(void)
{
    delete_nav_edit_button();
    if (!page_container) return;

    lv_obj_t *root = (lv_obj_t *)lv_obj_get_user_data(page_container);
    if (!root || lv_obj_get_child_count(root) == 0) return;

    lv_obj_t *nav = lv_obj_get_child(root, 0);
    if (!nav) return;

    nav_edit_btn = lv_btn_create(nav);
    lv_obj_set_size(nav_edit_btn, 28, 28);
    lv_obj_set_style_radius(nav_edit_btn, 14, 0);
    lv_obj_set_style_pad_all(nav_edit_btn, 0, 0);
    lv_obj_set_style_bg_color(nav_edit_btn, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(nav_edit_btn, 1, 0);
    lv_obj_set_style_border_color(nav_edit_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_align(nav_edit_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(nav_edit_btn, edit_current_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(nav_edit_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    add_to_default_group(nav_edit_btn);

    lv_obj_t *label = lv_label_create(nav_edit_btn);
    lv_label_set_text(label, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_color(label, UI_COLOR_ACCENT, 0);
    lv_obj_center(label);
}

static void reset_page_refs(void)
{
    status_label = NULL;
    rf_summary_label = NULL;
    traffic_label = NULL;
    msg_page = NULL;
    msg_cont = NULL;
    input_bar = NULL;
    input_bar_parent = NULL;
    input_ta = NULL;
    factory_mode_dd = NULL;
    payload_mode_row = NULL;
#if defined(HAS_USB_RF_SWITCH)
    antenna_rf_switch_dd = NULL;
#endif
    edit_name_ta = NULL;
    edit_freq_dd = NULL;
    edit_bw_dd = NULL;
    edit_power_dd = NULL;
    edit_interval_dd = NULL;
    edit_interval_custom_row = NULL;
    edit_interval_custom_divider = NULL;
    edit_interval_ta = NULL;
    edit_sf_dd = NULL;
    edit_cr_dd = NULL;
    edit_sync_dd = NULL;
    edit_sync_custom_row = NULL;
    edit_sync_custom_divider = NULL;
    edit_sync_ta = NULL;
    keyboard_lift.lifted_obj = NULL;
    keyboard_lift.original_parent = NULL;
    keyboard_lift.original_index = -1;
}

static void clear_page(void)
{
    delete_add_button();
    delete_nav_edit_button();
    hide_keyboard();
    reset_page_refs();
    if (page_container) {
        lv_obj_clean(page_container);
    }
}

#if defined(HAS_USB_RF_SWITCH)
static void antenna_set_dropdown_width(lv_obj_t *dd, lv_coord_t width)
{
    if (!dd) return;
    lv_obj_set_width(dd, width);
    lv_obj_set_style_min_width(dd, width, 0);
    lv_obj_set_style_max_width(dd, width, 0);
}
#endif

#if defined(HAS_USB_RF_SWITCH)
static void apply_ultra_rf_switch(void)
{
    hw_set_usb_rf_switch(antenna_rf_switch_to_usb);
}
#endif

static int16_t configure_radio_rx(void)
{
    lora_params.mode = RADIO_RX;
    int16_t state = hw_set_radio_params(lora_params);
#if defined(HAS_USB_RF_SWITCH)
    // Radio initialization can reset the antenna switch.
    apply_ultra_rf_switch();
#endif
    radio_configured = state == 0;
    return state;
}

static void apply_radio_listening(void)
{
    stop_tx_complete_timer();
    session_mode = LORA_SESSION_LISTEN;
    stop_auto_tx_timer();
    int16_t state = configure_radio_rx();
    if (state == 0) {
        set_status("Listening", UI_COLOR_ACCENT);
    } else {
        set_status("Radio config failed", lv_color_hex(0xFF4444));
    }
    update_session_control_visibility();
    update_chat_labels();
}

static void apply_chat_mode(void)
{
    stop_tx_complete_timer();
    session_mode = LORA_SESSION_CHAT;
    stop_auto_tx_timer();
    int16_t state = configure_radio_rx();
    if (state == 0) {
        set_status("Chat", UI_COLOR_ACCENT);
    } else {
        set_status("Radio config failed", lv_color_hex(0xFF4444));
    }
    update_session_control_visibility();
    update_chat_labels();
}

static void add_message(const char *text, bool sent, const char *meta)
{
    if (!msg_cont || !msg_page || !text || text[0] == '\0') return;

    if (msg_count >= LORA_HUB_MAX_MESSAGES) {
        lv_obj_t *first = lv_obj_get_child(msg_cont, 0);
        if (first) {
            lv_obj_delete(first);
            msg_count--;
        }
    }

    lv_obj_t *row = lv_obj_create(msg_cont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_ver(row, 3, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, sent ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, lv_pct(78), 0);
    lv_obj_set_style_bg_color(bubble, sent ? UI_COLOR_ACCENT : lv_color_hex(0x252525), 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_pad_all(bubble, 7, 0);
    lv_obj_set_style_pad_row(bubble, 3, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *body = lv_label_create(bubble);
    lv_label_set_text(body, text);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(body, lv_pct(100), 0);
    lv_obj_set_style_text_color(body, lv_color_white(), 0);
    lv_obj_set_style_text_font(body, font_body(), 0);

    lv_obj_t *time = lv_label_create(bubble);
    if (meta && meta[0]) {
        lv_label_set_text_fmt(time, "%s  %s", time_text(), meta);
    } else {
        lv_label_set_text(time, time_text());
    }
    lv_obj_set_style_text_color(time, sent ? lv_color_hex(0xE8FFF8) : UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(time, font_meta(), 0);

    msg_count++;
    lv_obj_scroll_to_y(msg_page, lv_obj_get_height(msg_cont), LV_ANIM_ON);
}

static void finish_async_tx(int16_t state)
{
    if (tx_complete_timer) {
        lv_timer_del(tx_complete_timer);
        tx_complete_timer = NULL;
    }

    tx_in_progress = false;
    hw_set_radio_listening();

    if (state == 0) {
        tx_counter++;
        if (tx_started_by_auto && session_mode == LORA_SESSION_AUTO_TX) {
            set_auto_tx_status();
        } else {
            set_status("Sent", UI_COLOR_ACCENT);
        }
    } else {
        char meta[64];
        snprintf(meta, sizeof(meta), "TX failed %d: %s", state, radio_tx_state_text(state));
        set_status(meta, lv_color_hex(0xFF4444));
    }

    tx_started_by_auto = false;
    auto_tx_busy = false;
    update_chat_labels();
}

static void tx_complete_timer_cb(lv_timer_t *t)
{
    (void)t;
    int16_t state = 0;
    if (hw_get_radio_tx_done(state)) {
        finish_async_tx(state);
        return;
    }

    if (lv_tick_elaps(tx_started_tick) >= tx_timeout_ms) {
        finish_async_tx(-5);
    }
}

static bool send_payload(const char *payload, bool from_auto_tx = false)
{
    if (!payload || payload[0] == '\0') return false;
    if (!radio_configured) {
        set_status("Radio config failed", lv_color_hex(0xFF4444));
        return false;
    }

    if (tx_in_progress) {
        set_status("TX busy", UI_COLOR_TEXT_SECONDARY);
        return false;
    }

    snprintf(tx_payload_storage, sizeof(tx_payload_storage), "%s", payload);
    size_t payload_len = strlen(tx_payload_storage);
    radio_tx_params_t tx_params;
    tx_params.data = (uint8_t *)tx_payload_storage;
    tx_params.length = payload_len;
    tx_params.state = 0;

    set_status("Sending...", UI_COLOR_TEXT_SECONDARY);
    hw_set_radio_tx(tx_params, false);
    if (tx_params.state != 0) {
        char meta[64];
        snprintf(meta, sizeof(meta), "TX failed %d: %s", tx_params.state, radio_tx_state_text(tx_params.state));
        add_message(tx_payload_storage, true, meta);
        set_status(meta, lv_color_hex(0xFF4444));
        update_chat_labels();
        return false;
    }

    tx_in_progress = true;
    tx_started_by_auto = from_auto_tx;
    tx_started_tick = lv_tick_get();
    tx_timeout_ms = estimate_lora_tx_time_ms(payload_len) + LORA_HUB_TX_TIMEOUT_GUARD_MS;
    add_message(tx_payload_storage, true, NULL);
    tx_complete_timer = lv_timer_create(tx_complete_timer_cb, LORA_HUB_TX_POLL_MS, NULL);
    if (!tx_complete_timer) {
        tx_in_progress = false;
        tx_started_by_auto = false;
        set_status("TX timer failed", lv_color_hex(0xFF4444));
        update_chat_labels();
        return false;
    }
    update_chat_labels();
    return true;
}

static void send_current_input(void)
{
    if (session_mode != LORA_SESSION_CHAT) return;
    const char *text = input_ta ? lv_textarea_get_text(input_ta) : "";
    if (!text || text[0] == '\0') return;
    if (send_payload(text) && input_ta) {
        lv_textarea_set_text(input_ta, "");
    }
}

static void set_auto_tx_status(void)
{
    char period[12];
    char status[32];
    format_interval_ms(auto_tx_effective_interval(), period, sizeof(period));
    snprintf(status, sizeof(status), "Auto TX %s %s", period, payload_mode_text());
    set_status(status, UI_COLOR_ACCENT);
}

static void build_auto_payload(char *out, size_t out_len)
{
    if (!out || out_len == 0 || chat_profile_index >= profile_count) return;

    if (auto_payload_mode == LORA_PAYLOAD_FIXED) {
        snprintf(out, out_len, "%s", LORA_HUB_FIXED_PAYLOAD_DEFAULT);
        return;
    }

    auto_payload_counter++;
    snprintf(out, out_len, "%lu", (unsigned long)auto_payload_counter);
}

static void auto_tx_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (auto_tx_busy || session_mode != LORA_SESSION_AUTO_TX || chat_profile_index >= profile_count) {
        return;
    }
    if (tx_in_progress) {
        return;
    }

    auto_tx_busy = true;

    char payload[80];
    build_auto_payload(payload, sizeof(payload));

    bool ok = send_payload(payload, true);
    if (!ok) {
        auto_tx_busy = false;
    }
}

static bool start_auto_tx_session(void)
{
    if (chat_profile_index >= profile_count) return false;

    session_mode = LORA_SESSION_AUTO_TX;
    int16_t state = configure_radio_rx();
    if (state != 0) {
        session_mode = LORA_SESSION_LISTEN;
        stop_auto_tx_timer();
        if (factory_mode_dd) {
            lv_dropdown_set_selected(factory_mode_dd, 0);
        }
        set_status("Radio config failed", lv_color_hex(0xFF4444));
        update_session_control_visibility();
        update_chat_labels();
        return false;
    }

    uint32_t period = auto_tx_effective_interval();
    stop_auto_tx_timer();
    auto_tx_timer = lv_timer_create(auto_tx_timer_cb, period, NULL);
    set_auto_tx_status();
    update_session_control_visibility();
    update_chat_labels();

    return true;
}

static bool start_cw_session(void)
{
    stop_tx_complete_timer();
    stop_auto_tx_timer();

    lora_params.mode = RADIO_CW;
    int16_t state = hw_set_radio_params(lora_params);
#if defined(HAS_USB_RF_SWITCH)
    // Radio initialization can reset the antenna switch.
    apply_ultra_rf_switch();
#endif
    if (state != 0) {
        session_mode = LORA_SESSION_LISTEN;
        if (factory_mode_dd) {
            lv_dropdown_set_selected(factory_mode_dd,
                                     session_mode_to_dropdown(session_mode));
        }
        configure_radio_rx();
        char status[64];
        snprintf(status, sizeof(status), "CW failed %d: %s", state,
                 radio_tx_state_text(state));
        set_status(status, lv_color_hex(0xFF4444));
        update_session_control_visibility();
        update_chat_labels();
        return false;
    }

    session_mode = LORA_SESSION_CW;
    radio_configured = true;
    set_status("Continuous wave TX", lv_color_hex(0xFFB800));
    add_message("Continuous wave transmission started.", true, "CW");
    update_session_control_visibility();
    update_chat_labels();
    return true;
}

static void apply_factory_mode(lora_session_mode_t mode)
{
    if (mode == LORA_SESSION_CW) {
        start_cw_session();
    } else if (mode == LORA_SESSION_AUTO_TX) {
        start_auto_tx_session();
    } else if (mode == LORA_SESSION_CHAT) {
        apply_chat_mode();
    } else {
        apply_radio_listening();
    }
}

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
static void factory_mode_apply_timer_cb(lv_timer_t *t)
{
    (void)t;
    factory_mode_apply_timer = NULL;
    apply_factory_mode(pending_factory_mode);
}

static void finish_mode_dropdown_event(lv_event_t *e, lv_obj_t *dd)
{
    if (dd) {
        if (lv_dropdown_is_open(dd)) {
            lv_dropdown_close(dd);
        }
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(dd);
        if (group) {
            lv_group_set_editing(group, false);
        }
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev) {
        lv_indev_wait_release(indev);
    }
}

static void schedule_factory_mode_apply(lora_session_mode_t mode)
{
    pending_factory_mode = mode;
    stop_factory_mode_apply_timer();
    factory_mode_apply_timer = lv_timer_create(factory_mode_apply_timer_cb, LORA_HUB_MODE_APPLY_DELAY_MS, NULL);
    if (factory_mode_apply_timer) {
        lv_timer_set_repeat_count(factory_mode_apply_timer, 1);
    } else {
        apply_factory_mode(pending_factory_mode);
    }
}
#endif

static void rx_timer_cb(lv_timer_t *t)
{
    if (session_mode == LORA_SESSION_CW || tx_in_progress || !radio_configured) {
        return;
    }
    rx_params.data = (uint8_t *)recv_buf;
    rx_params.length = sizeof(recv_buf) - 1;
    hw_get_radio_rx(rx_params);
    if (rx_params.state == 0 && rx_params.length != 0) {
        size_t len = rx_params.length;
        if (len >= sizeof(recv_buf)) len = sizeof(recv_buf) - 1;
        recv_buf[len] = '\0';
        rx_counter++;
        char meta[48];
        snprintf(meta, sizeof(meta), "RSSI %d  SNR %d", rx_params.rssi, rx_params.snr);
        add_message(recv_buf, false, meta);
        if (traffic_label) {
            lv_label_set_text_fmt(traffic_label, "TX %lu  RX %lu  %s",
                                  (unsigned long)tx_counter,
                                  (unsigned long)rx_counter,
                                  meta);
        }
        hw_feedback();
        hw_set_radio_listening();
    }
}

static void textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_KEY && ta == input_ta) {
        lv_key_t *key = (lv_key_t *)lv_event_get_param(e);
        if (key && *key == LV_KEY_ENTER && event_indev_type(e) != LV_INDEV_TYPE_ENCODER) {
            send_current_input();
            hide_keyboard();
            lv_event_stop_processing(e);
            return;
        }
    }

    if (code == LV_EVENT_READY && ta == input_ta) {
        send_current_input();
        hide_keyboard();
        lv_event_stop_processing(e);
        return;
    }

    if (current_screen == LORA_SCREEN_EDIT &&
            (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED)) {
        if (ta == edit_interval_ta) {
            editing_profile.interval_ms = parse_interval_seconds_text(lv_textarea_get_text(edit_interval_ta),
                                          editing_profile.interval_ms);
        }
        if (ta == edit_sync_ta) {
            editing_profile.sync_word = parse_sync_word_text(lv_textarea_get_text(edit_sync_ta),
                                        editing_profile.sync_word);
        }
        edit_dirty = true;
    }

    if (code == LV_EVENT_CLICKED && event_indev_type(e) == LV_INDEV_TYPE_POINTER) {
        focus_textarea(ta);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED) {
        hide_keyboard();
    }
}

static void send_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_type_t type = event_indev_type(e);
    if (code == LV_EVENT_PRESSED && type != LV_INDEV_TYPE_POINTER) return;
    if (code == LV_EVENT_CLICKED && type == LV_INDEV_TYPE_POINTER) return;

    hw_feedback();
    send_current_input();
}

static void factory_mode_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    uint16_t selected = dd ? lv_dropdown_get_selected(dd) : 0;
    lora_session_mode_t mode = session_mode_from_dropdown(selected);

    hw_feedback();

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    finish_mode_dropdown_event(e, dd);
    if (mode == LORA_SESSION_CW) {
        stop_factory_mode_apply_timer();
    } else {
        schedule_factory_mode_apply(mode);
        return;
    }
#endif

    if (mode != LORA_SESSION_CW) {
        apply_factory_mode(mode);
        return;
    }

    if (cw_warning_msgbox) return;
    static const char *btns[] = {"Enable CW", "Cancel", ""};
    cw_warning_msgbox = create_msgbox(
        lv_scr_act(), "Antenna required",
        "Connect a suitable antenna before enabling continuous wave transmission.\n\n"
        "Running this test without an antenna can damage the radio front end.",
        btns, [](lv_event_t *event) {
            lv_obj_t *button = lv_event_get_current_target_obj(event);
            lv_obj_t *label = button ? lv_obj_get_child(button, 0) : NULL;
            bool enable = label && strcmp(lv_label_get_text(label), "Enable CW") == 0;

            if (cw_warning_msgbox) {
                destroy_msgbox(cw_warning_msgbox);
                cw_warning_msgbox = NULL;
            }

            if (enable) {
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
                schedule_factory_mode_apply(LORA_SESSION_CW);
#else
                apply_factory_mode(LORA_SESSION_CW);
#endif
            } else if (factory_mode_dd) {
                lv_dropdown_set_selected(factory_mode_dd,
                                         session_mode_to_dropdown(session_mode));
            }
        }, NULL);
}

static void factory_payload_mode_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    uint16_t selected = dd ? lv_dropdown_get_selected(dd) : 0;

    auto_payload_mode = selected == LORA_PAYLOAD_FIXED ? LORA_PAYLOAD_FIXED : LORA_PAYLOAD_COUNTER;
    if (session_mode == LORA_SESSION_AUTO_TX) {
        set_auto_tx_status();
    }
    update_chat_labels();
    hw_feedback();
}

#if defined(HAS_USB_RF_SWITCH)
static void ultra_rf_switch_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    uint16_t selected = dd ? lv_dropdown_get_selected(dd) : 0;

    antenna_rf_switch_to_usb = selected != 0;
    apply_ultra_rf_switch();
    hw_feedback();
}

static void create_ultra_rf_switch_control(lv_obj_t *card)
{
    lv_obj_t *row = ui_create_card_dropdown(card, LV_SYMBOL_USB, "Antenna",
                                            "Built-in\nUSB Antenna",
                                            antenna_rf_switch_to_usb ? 1 : 0,
                                            ultra_rf_switch_cb);
    antenna_rf_switch_dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    antenna_set_dropdown_width(antenna_rf_switch_dd, LORA_HUB_ULTRA_DROPDOWN_WIDTH);
}
#endif

static void create_session_controls(lv_obj_t *card)
{
    if (!card) return;

    lv_obj_t *row = ui_create_card_dropdown(card, LV_SYMBOL_LOOP, "Mode",
                                            "Listen\nChat\nAuto TX\nLoRa CW",
                                            session_mode_to_dropdown(session_mode),
                                            factory_mode_cb);
    factory_mode_dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
#if defined(HAS_USB_RF_SWITCH)
    antenna_set_dropdown_width(factory_mode_dd, LORA_HUB_ULTRA_DROPDOWN_WIDTH);
#endif

    payload_mode_row = ui_create_card_dropdown(card, LV_SYMBOL_UPLOAD, "Payload",
                                               LORA_HUB_PAYLOAD_MODE_LIST,
                                               auto_payload_mode == LORA_PAYLOAD_FIXED ? 1 : 0,
                                               factory_payload_mode_cb);
#if defined(HAS_USB_RF_SWITCH)
    lv_obj_t *payload_dd = lv_obj_get_child(payload_mode_row, lv_obj_get_child_count(payload_mode_row) - 1);
    antenna_set_dropdown_width(payload_dd, LORA_HUB_ULTRA_DROPDOWN_WIDTH);
#endif

    update_session_control_visibility();
}

static void profile_card_event_cb(lv_event_t *e)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= profile_count) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        render_chat(index);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        render_profile_editor(index, false);
    }
    hw_feedback();
}

static void bind_profile_click(lv_obj_t *obj, uint8_t index)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, profile_card_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    lv_obj_add_event_cb(obj, profile_card_event_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)index);
}

static void add_profile_cb(lv_event_t *e)
{
    if (profile_count >= LORA_HUB_MAX_PROFILES) {
        ui_msg_pop_up("LoRa", "Profile storage is full.");
        return;
    }
    render_profile_editor(-1, false);
    hw_feedback();
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_color_t color,
                             lv_coord_t width, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, 34);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    if (lv_color_eq(color, UI_COLOR_ACCENT)) {
        ui_add_accent_focus_style(btn);
    }
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    add_to_default_group(btn);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void create_add_button(void)
{
    delete_add_button();
    add_profile_btn = lv_btn_create(lv_layer_top());
    lv_obj_set_size(add_profile_btn, 52, 52);
    lv_obj_set_style_radius(add_profile_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(add_profile_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(add_profile_btn, 0, 0);
    ui_add_accent_focus_style(add_profile_btn);
    lv_obj_add_event_cb(add_profile_btn, add_profile_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(add_profile_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    add_to_default_group(add_profile_btn);
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    lv_obj_align(add_profile_btn, LV_ALIGN_BOTTOM_RIGHT,
                 -LORA_HUB_ULTRA_ADD_BTN_MARGIN,
                 -LORA_HUB_ULTRA_ADD_BTN_MARGIN);
#else
    lv_obj_align(add_profile_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
#endif

    lv_obj_t *label = lv_label_create(add_profile_btn);
    lv_label_set_text(label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
}

static void render_profile_list(void)
{
    title_click_count = 0;
    stop_radio_session();
    clear_page();
    current_screen = LORA_SCREEN_LIST;
    edit_dirty = false;
    msg_count = 0;
    tx_counter = 0;
    rx_counter = 0;
    chat_scroll_mode = false;
    if (active_profile_index >= profile_count || !profile_visible(profiles[active_profile_index])) {
        for (uint8_t i = 0; i < profile_count; ++i) {
            if (profile_visible(profiles[i])) {
                active_profile_index = i;
                break;
            }
        }
    }

    lv_obj_t *title = lv_label_create(page_container);
    lv_label_set_text(title, "Profiles");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, font_title(), 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_min_height(title, 36, 0);
    lv_obj_set_style_pad_ver(title, 8, 0);
    lv_obj_set_ext_click_area(title, 4);
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(title, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(title, title_click_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(title, profile_title_focus_visual_cb, LV_EVENT_ALL, NULL);
    add_to_default_group(title);

    lv_obj_t *hint = lv_label_create(page_container);
    lv_label_set_text(hint, "Tap to open. Long press to edit.");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint, font_meta(), 0);

    for (uint8_t i = 0; i < profile_count; ++i) {
        if (!profile_visible(profiles[i])) continue;
        char summary[96];
        format_profile_summary(profiles[i], summary, sizeof(summary));

        lv_obj_t *row = lv_obj_create(page_container);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, i == active_profile_index ? UI_COLOR_ACCENT : UI_COLOR_DIVIDER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_style_pad_row(row, 5, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        bind_profile_click(row, i);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_FOCUS, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(row, focus_scroll_cb, LV_EVENT_FOCUSED, NULL);
        add_to_default_group(row);

        lv_obj_t *top = lv_obj_create(row);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_set_style_pad_all(top, 0, 0);
        lv_obj_set_style_pad_column(top, 8, 0);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        bind_profile_click(top, i);

        lv_obj_t *name = lv_label_create(top);
        lv_label_set_text(name, profiles[i].name);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, LV_PCT(62));
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(name, font_title(), 0);
        bind_profile_click(name, i);

        lv_obj_t *tag = lv_label_create(top);
        lv_label_set_text(tag, profiles[i].builtin ? "Factory" : "User");
        lv_obj_set_style_text_color(tag, profiles[i].builtin ? lv_color_hex(0xFFB800) : UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(tag, font_meta(), 0);
        bind_profile_click(tag, i);

        lv_obj_t *sub = lv_label_create(row);
        lv_label_set_text(sub, summary);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sub, LV_PCT(100));
        lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(sub, font_meta(), 0);
        bind_profile_click(sub, i);
    }

    create_add_button();
}

static void build_chat_area(lv_obj_t *parent)
{
    msg_page = lv_obj_create(parent);
    lv_obj_set_size(msg_page, LV_PCT(100), is_screen_small() ? 152 : 210);
    lv_obj_set_style_bg_color(msg_page, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(msg_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(msg_page, 1, 0);
    lv_obj_set_style_border_color(msg_page, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_color(msg_page, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(msg_page, LV_OPA_80, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(msg_page, 8, 0);
    lv_obj_set_style_pad_all(msg_page, 4, 0);
    lv_obj_set_scrollbar_mode(msg_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(msg_page, LV_DIR_VER);
    lv_obj_add_flag(msg_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(msg_page, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(msg_page, chat_area_event_cb, LV_EVENT_ALL, NULL);
    add_to_default_group(msg_page);

    msg_cont = lv_obj_create(msg_page);
    lv_obj_set_size(msg_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(msg_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(msg_cont, 0, 0);
    lv_obj_set_style_pad_all(msg_cont, 0, 0);
    lv_obj_set_style_pad_row(msg_cont, 3, 0);
    lv_obj_set_flex_flow(msg_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(msg_cont, LV_DIR_NONE);
}

static void build_input_area(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    input_bar = bar;
    input_bar_parent = parent;
    lv_obj_set_size(bar, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 8, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_set_style_pad_column(bar, 5, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    input_ta = lv_textarea_create(bar);
    lv_obj_set_size(input_ta, LV_PCT(72), 36);
    lv_textarea_set_one_line(input_ta, true);
    lv_textarea_set_max_length(input_ta, 140);
    lv_textarea_set_placeholder_text(input_ta, "Message");
    lv_obj_set_scroll_dir(input_ta, LV_DIR_NONE);
    lv_obj_remove_flag(input_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(input_ta, textarea_event_cb, LV_EVENT_ALL, NULL);
    ui_prepare_textarea_for_encoder(input_ta);

    lv_obj_t *send_btn = make_button(bar, LV_SYMBOL_RIGHT, UI_COLOR_ACCENT, 54, NULL);
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, NULL);

    update_session_control_visibility();
}

static void edit_current_cb(lv_event_t *e)
{
    render_profile_editor(chat_profile_index, true);
    hw_feedback();
}

static void render_chat(uint8_t index)
{
    if (index >= profile_count || !profile_visible(profiles[index])) return;
    title_click_count = 0;
    stop_radio_session();
    clear_page();
    current_screen = LORA_SCREEN_CHAT;
    chat_profile_index = index;
    active_profile_index = index;
    save_active_profile_index();
    profile_to_params(profiles[index]);
    tx_counter = 0;
    rx_counter = 0;
    auto_payload_counter = 0;
    msg_count = 0;
    chat_scroll_mode = false;

    lv_obj_t *card = ui_create_card(page_container, profiles[index].name);
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_WIFI, "State", "Starting");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    if (status_label) {
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
        lv_obj_t *state_title = lv_obj_get_child(row, 1);
        if (state_title) {
            lv_obj_set_width(state_title, LORA_HUB_ULTRA_STATE_TITLE_WIDTH);
        }
        lv_obj_set_width(status_label, LORA_HUB_ULTRA_STATE_VALUE_WIDTH);
        lv_obj_set_style_min_width(status_label, LORA_HUB_ULTRA_STATE_VALUE_WIDTH, 0);
#endif
        lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    }
    row = ui_create_card_info(card, LV_SYMBOL_SETTINGS, "RF", "--");
    rf_summary_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    if (rf_summary_label) {
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
        lv_obj_set_width(rf_summary_label, 300);
#else
        lv_obj_set_width(rf_summary_label, 190);
#endif
        lv_label_set_long_mode(rf_summary_label, LV_LABEL_LONG_DOT);
    }
    row = ui_create_card_info(card, LV_SYMBOL_LIST, "Traffic", "TX 0  RX 0");
    traffic_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    create_session_controls(card);
#if defined(HAS_USB_RF_SWITCH)
    create_ultra_rf_switch_control(card);
#endif
    create_nav_edit_button();

    build_chat_area(page_container);
    add_message(profiles[index].builtin ? "Factory test ready." : "Listening with this profile.",
                false, "local");
    build_input_area(page_container);

    apply_radio_listening();
    rx_timer = lv_timer_create(rx_timer_cb, 300, NULL);
}

static lv_obj_t *create_textarea_item(lv_obj_t *card, const char *icon, const char *title,
                                      const char *placeholder, uint16_t max_len,
                                      const char *accepted,
                                      lv_obj_t **out_row = NULL,
                                      lv_obj_t **out_divider = NULL)
{
    lv_obj_t *ta = lv_textarea_create(lv_obj_create(card));
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    if (accepted) lv_textarea_set_accepted_chars(ta, accepted);
    lv_obj_set_width(ta, 140);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_obj_set_style_text_color(ta, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ta, LV_DIR_NONE);
    lv_obj_add_event_cb(ta, textarea_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t *row = ui_create_card_item(card, icon, title, ta);
    if (out_row) *out_row = row;
    if (out_divider) {
        uint32_t count = lv_obj_get_child_count(card);
        *out_divider = count ? lv_obj_get_child(card, count - 1) : NULL;
    }
    return ta;
}

static lv_obj_t *create_dropdown_item(lv_obj_t *card, const char *icon, const char *title,
                                      const char *options, lv_event_cb_t cb, const char *tag)
{
    lv_obj_t *dd = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(dd, options ? options : "");
    lv_obj_set_width(dd, 140);
    if (cb) lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, (void *)tag);
    ui_create_card_item(card, icon, title, dd);
    return dd;
}

static void edit_param_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    const char *tag = (const char *)lv_event_get_user_data(e);
    if (!tag) return;

    switch (*tag) {
    case 'f':
        editing_profile.freq = edit_frequency_from_index(lv_dropdown_get_selected(obj));
        update_edit_bw_power_options();
        break;
    case 'b':
        editing_profile.bandwidth = radio_get_bandwidth_from_index(lv_dropdown_get_selected(obj));
        break;
    case 'p':
        editing_profile.power = (uint8_t)radio_get_tx_power_from_index(lv_dropdown_get_selected(obj));
        break;
    case 'i':
        apply_interval_dropdown_selection(lv_dropdown_get_selected(obj));
        break;
    case 's':
        editing_profile.sf = lv_dropdown_get_selected(obj) + 5;
        break;
    case 'c':
        editing_profile.cr = lv_dropdown_get_selected(obj) + 5;
        break;
    case 'w':
        apply_sync_dropdown_selection(lv_dropdown_get_selected(obj));
        break;
    default:
        break;
    }
    edit_dirty = true;
}

static void collect_editor_fields(void)
{
    const char *name = edit_name_ta ? lv_textarea_get_text(edit_name_ta) : "";
    if (name && name[0]) {
        snprintf(editing_profile.name, sizeof(editing_profile.name), "%s", name);
    }
    if (edit_interval_dd) {
        uint16_t selected = lv_dropdown_get_selected(edit_interval_dd);
        if (selected < interval_custom_index()) {
            editing_profile.interval_ms = interval_values[selected];
        } else if (edit_interval_ta) {
            editing_profile.interval_ms = parse_interval_seconds_text(lv_textarea_get_text(edit_interval_ta),
                                          editing_profile.interval_ms);
        }
    }
    if (edit_sync_dd) {
        uint16_t selected = lv_dropdown_get_selected(edit_sync_dd);
        if (selected == LORA_SYNC_PRIVATE) {
            editing_profile.sync_word = 0x12;
        } else if (selected == LORA_SYNC_PUBLIC) {
            editing_profile.sync_word = 0x34;
        } else if (edit_sync_ta) {
            editing_profile.sync_word = parse_sync_word_text(lv_textarea_get_text(edit_sync_ta),
                                        editing_profile.sync_word);
        }
    }
}

static bool save_editor_profile(uint8_t *saved_index)
{
    collect_editor_fields();
    if (!profile_visible(editing_profile)) {
        ui_msg_pop_up("LoRa", "Profile parameters are not supported.");
        return false;
    }
    if (editing_profile.name[0] == '\0') {
        ui_msg_pop_up("LoRa", "Profile name is empty.");
        return false;
    }

    uint8_t target = 0;
    bool overwrite_user = editing_source_index >= LORA_HUB_FACTORY_COUNT &&
                          editing_source_index < (int16_t)profile_count;

    if (overwrite_user) {
        target = (uint8_t)editing_source_index;
    } else {
        if (profile_count >= LORA_HUB_MAX_PROFILES) {
            ui_msg_pop_up("LoRa", "Profile storage is full.");
            return false;
        }
        target = profile_count++;
        editing_profile.builtin = false;
    }

    profiles[target] = editing_profile;
    profiles[target].builtin = false;
    active_profile_index = target;
    save_user_profiles();
    edit_dirty = false;
    if (saved_index) *saved_index = target;
    return true;
}

static void return_after_edit(uint8_t index)
{
    if (edit_return_to_chat && index < profile_count) {
        render_chat(index);
    } else {
        render_profile_list();
    }
}

static void save_editor_cb(lv_event_t *e)
{
    uint8_t saved = active_profile_index;
    if (save_editor_profile(&saved)) {
        return_after_edit(saved);
    }
    hw_feedback();
}

static void cancel_editor_cb(lv_event_t *e)
{
    if (edit_return_to_chat && chat_profile_index < profile_count) {
        render_chat(chat_profile_index);
    } else {
        render_profile_list();
    }
    hw_feedback();
}

static void save_prompt_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    const char *text = "";
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) text = lv_label_get_text(label);

    if (confirm_msgbox) {
        destroy_msgbox(confirm_msgbox);
        confirm_msgbox = NULL;
    }

    if (strcmp(text, "Save") == 0) {
        uint8_t saved = active_profile_index;
        if (save_editor_profile(&saved)) {
            return_after_edit(saved);
        }
    } else if (strcmp(text, "Discard") == 0) {
        if (edit_return_to_chat && chat_profile_index < profile_count) {
            render_chat(chat_profile_index);
        } else {
            render_profile_list();
        }
    }
}

static void show_save_prompt(void)
{
    if (confirm_msgbox) return;
    static const char *btns[] = {"Save", "Discard", "Cancel", ""};
    confirm_msgbox = create_msgbox(lv_scr_act(), "Save Profile",
                                   "Save changes before leaving?", btns,
                                   save_prompt_cb, NULL);
}

static bool delete_profile_index(uint8_t index)
{
    if (index < LORA_HUB_FACTORY_COUNT || index >= profile_count) {
        return false;
    }

    for (uint8_t i = index; i + 1 < profile_count; ++i) {
        profiles[i] = profiles[i + 1];
    }
    profile_count--;
    active_profile_index = 0;
    chat_profile_index = 0;
    save_user_profiles();
    return true;
}

static void delete_prompt_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    const char *text = "";
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) text = lv_label_get_text(label);

    if (confirm_msgbox) {
        destroy_msgbox(confirm_msgbox);
        confirm_msgbox = NULL;
    }

    if (strcmp(text, "Delete") == 0) {
        if (editing_source_index >= 0 && delete_profile_index((uint8_t)editing_source_index)) {
            render_profile_list();
        } else {
            ui_msg_pop_up("LoRa", "Factory profiles cannot be deleted.");
        }
    }
}

static void delete_editor_cb(lv_event_t *e)
{
    if (editing_source_index < LORA_HUB_FACTORY_COUNT ||
            editing_source_index >= (int16_t)profile_count) {
        ui_msg_pop_up("LoRa", "Factory profiles cannot be deleted.");
        return;
    }
    if (confirm_msgbox) return;
    static const char *btns[] = {"Delete", "Cancel", ""};
    confirm_msgbox = create_msgbox(lv_scr_act(), "Delete Profile",
                                   "Delete this user profile?", btns,
                                   delete_prompt_cb, NULL);
    hw_feedback();
}

static void render_profile_editor(int16_t source_index, bool return_to_chat)
{
    title_click_count = 0;
    stop_radio_session();
    clear_page();
    current_screen = LORA_SCREEN_EDIT;
    editing_source_index = source_index;
    edit_return_to_chat = return_to_chat;

    if (source_index >= 0 && source_index < (int16_t)profile_count) {
        editing_profile = profiles[source_index];
        if (profiles[source_index].builtin) {
            char name[24];
            snprintf(name, sizeof(name), "%s Copy", profiles[source_index].name);
            snprintf(editing_profile.name, sizeof(editing_profile.name), "%s", name);
            editing_profile.builtin = false;
        }
    } else {
        uint8_t base = active_profile_index < profile_count ? active_profile_index : (LORA_HUB_FACTORY_COUNT - 1);
        editing_profile = profiles[base];
        snprintf(editing_profile.name, sizeof(editing_profile.name), "User %u",
                 (unsigned)(profile_count - LORA_HUB_FACTORY_COUNT + 1));
        editing_profile.builtin = false;
    }

    lv_obj_t *top = lv_obj_create(page_container);
    lv_obj_set_size(top, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_pad_column(top, 8, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, source_index < 0 ? "New Profile" : "Edit Profile");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, font_title(), 0);

    lv_obj_t *actions = lv_obj_create(top);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, 38);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 6, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_button(actions, "Cancel", lv_color_hex(0x333333), 72, cancel_editor_cb);
    make_button(actions, "Save", UI_COLOR_ACCENT, 62, save_editor_cb);

    lv_obj_t *card = ui_create_card(page_container, "Profile");
    edit_name_ta = create_textarea_item(card, LV_SYMBOL_EDIT, "Name", "Profile name", 23, NULL);
    lv_textarea_set_text(edit_name_ta, editing_profile.name);

    card = ui_create_card(page_container, "LoRa Parameters");
    edit_freq_dd = create_dropdown_item(card, LV_SYMBOL_WIFI, "Frequency",
                                        "", edit_param_event_cb, "f");
    populate_edit_frequencies();
    edit_bw_dd = create_dropdown_item(card, LV_SYMBOL_SETTINGS, "Bandwidth",
                                      radio_get_bandwidth_list(is_high_freq(editing_profile.freq)), edit_param_event_cb, "b");
    edit_power_dd = create_dropdown_item(card, LV_SYMBOL_SETTINGS, "TX Power",
                                         radio_get_tx_power_list(is_high_freq(editing_profile.freq)), edit_param_event_cb, "p");
    edit_interval_dd = create_dropdown_item(card, LV_SYMBOL_LOOP, "Interval",
                                            LORA_HUB_INTERVAL_LIST, edit_param_event_cb, "i");
    edit_interval_ta = create_textarea_item(card, LV_SYMBOL_EDIT, "Seconds", "1",
                                            11, "0123456789.",
                                            &edit_interval_custom_row, &edit_interval_custom_divider);
    edit_sf_dd = create_dropdown_item(card, LV_SYMBOL_SETTINGS, "SF", LORA_HUB_SF_LIST,
                                      edit_param_event_cb, "s");
    edit_cr_dd = create_dropdown_item(card, LV_SYMBOL_SETTINGS, "CR", LORA_HUB_CR_LIST,
                                      edit_param_event_cb, "c");
    edit_sync_dd = create_dropdown_item(card, LV_SYMBOL_EDIT, "Sync Word",
                                        LORA_HUB_SYNC_WORD_LIST, edit_param_event_cb, "w");
    edit_sync_ta = create_textarea_item(card, LV_SYMBOL_EDIT, "Custom Sync", "0xCD",
                                        4, "0123456789xXaAbBcCdDeEfF",
                                        &edit_sync_custom_row, &edit_sync_custom_divider);

    select_dropdown_by_float(edit_freq_dd, editing_profile.freq,
                             edit_frequency_from_index, edit_frequencies.size());
    update_edit_bw_power_options();

    uint16_t interval_sel = interval_dropdown_from_ms(editing_profile.interval_ms);
    char interval_buf[12];
    format_interval_seconds_text(editing_profile.interval_ms, interval_buf, sizeof(interval_buf));
    lv_dropdown_set_selected(edit_interval_dd, interval_sel);
    lv_textarea_set_text(edit_interval_ta, interval_buf);
    set_interval_custom_visible(interval_sel == interval_custom_index());
    lv_dropdown_set_selected(edit_sf_dd, editing_profile.sf > 4 ? editing_profile.sf - 5 : 0);
    lv_dropdown_set_selected(edit_cr_dd, editing_profile.cr > 4 ? editing_profile.cr - 5 : 0);
    char sync_buf[8];
    format_sync_word_text(editing_profile.sync_word, sync_buf, sizeof(sync_buf));
    lv_dropdown_set_selected(edit_sync_dd, sync_dropdown_from_word(editing_profile.sync_word));
    lv_textarea_set_text(edit_sync_ta, sync_buf);
    set_sync_custom_visible(sync_dropdown_from_word(editing_profile.sync_word) == LORA_SYNC_CUSTOM);

    if (source_index >= LORA_HUB_FACTORY_COUNT && source_index < (int16_t)profile_count) {
        card = ui_create_card(page_container, "Danger");
        ui_create_card_button(card, LV_SYMBOL_TRASH, NULL, "Delete Profile", delete_editor_cb);
    }

    lv_obj_t *hint = lv_label_create(page_container);
    lv_label_set_text(hint, "Back asks to save changes. Factory profiles are copied when saved.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint, font_meta(), 0);

    edit_dirty = false;
}

static void cleanup(void)
{
#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER)
    profile_filter_shortcut_count = 0;
    profile_filter_shortcut_tick = 0;
    if (profile_filter_shortcut_pending) {
        lv_async_call_cancel(profile_filter_shortcut_async_cb, NULL);
        profile_filter_shortcut_pending = false;
    }
    if (profile_filter_shortcut_registered) {
        instance.kb.unregisterKeyCombo(ModifierKey::ALT, 'L');
        profile_filter_shortcut_registered = false;
    }
#endif
    if (cw_warning_msgbox) {
        destroy_msgbox(cw_warning_msgbox);
        cw_warning_msgbox = NULL;
    }
    if (confirm_msgbox) {
        destroy_msgbox(confirm_msgbox);
        confirm_msgbox = NULL;
    }
    stop_radio_session();
    delete_add_button();
    delete_nav_edit_button();
#ifdef USING_TOUCHPAD
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
#endif
    hide_keyboard();

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    reset_page_refs();
    current_screen = LORA_SCREEN_LIST;
    edit_dirty = false;
    msg_count = 0;
    tx_counter = 0;
    rx_counter = 0;
    menu_show();
}

static void back_event_handler(lv_event_t *e)
{
    if (current_screen == LORA_SCREEN_EDIT) {
        if (edit_dirty) {
            show_save_prompt();
        } else {
            cancel_editor_cb(e);
        }
        return;
    }

    if (current_screen == LORA_SCREEN_CHAT) {
        render_profile_list();
        return;
    }

    cleanup();
}

static void title_click_cb(lv_event_t *e)
{
    if (event_indev_type(e) == LV_INDEV_TYPE_ENCODER ||
            event_indev_type(e) == LV_INDEV_TYPE_KEYPAD) {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(lv_event_get_target_obj(e));
        if (group) lv_group_set_editing(group, false);
        return;
    }
    if (current_screen != LORA_SCREEN_LIST || confirm_msgbox) {
        title_click_count = 0;
        return;
    }
    if (lv_tick_elaps(title_click_tick) > LORA_HUB_TITLE_CLICK_GAP_MS) title_click_count = 0;
    title_click_tick = lv_tick_get();
    if (++title_click_count < 5) return;
    title_click_count = 0;
    toggle_920_profile_filter();
}

static bool toggle_920_profile_filter(void)
{
    if (current_screen != LORA_SCREEN_LIST || confirm_msgbox || cw_warning_msgbox) {
        return false;
    }
    if (!frequency_supported(920.0f)) {
        ui_msg_pop_up("LoRa", "920 MHz is not supported by this radio.");
        return false;
    }
    bool next_only_920 = !only_920_profiles;
    if (!save_profile_filter(next_only_920)) {
        ui_msg_pop_up("LoRa", "Failed to save profile filter.");
        return false;
    }
    only_920_profiles = next_only_920;
    render_profile_list();
    hw_feedback();
    return true;
}

#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER)
static void profile_filter_shortcut_async_cb(void *)
{
    profile_filter_shortcut_pending = false;
    toggle_920_profile_filter();
}

static void profile_filter_shortcut_cb(void)
{
    if (lv_tick_elaps(profile_filter_shortcut_tick) > LORA_HUB_SHORTCUT_PRESS_GAP_MS) {
        profile_filter_shortcut_count = 0;
    }
    profile_filter_shortcut_tick = lv_tick_get();
    if (++profile_filter_shortcut_count < LORA_HUB_SHORTCUT_PRESS_COUNT) return;
    profile_filter_shortcut_count = 0;

    if (profile_filter_shortcut_pending) return;
    profile_filter_shortcut_pending =
        lv_async_call(profile_filter_shortcut_async_cb, NULL) == LV_RESULT_OK;
}

static void register_profile_filter_shortcut(void)
{
    profile_filter_shortcut_count = 0;
    profile_filter_shortcut_tick = 0;
    profile_filter_shortcut_registered =
        instance.kb.registerKeyCombo(ModifierKey::ALT, 'L',
                                     profile_filter_shortcut_cb);
}
#endif

void ui_lora_hub_enter(lv_obj_t *parent)
{
    title_click_count = 0;
    page_container = ui_create_app_page(parent, "LoRa Hub", back_event_handler);
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    lv_obj_set_style_pad_top(page_container, LORA_HUB_ULTRA_TOP_PAD, 0);
#endif

    if (!(HW_RADIO_ONLINE & hw_get_device_online())) {
        lv_obj_t *card = ui_create_card(page_container, NULL);
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, "Radio module not detected.");
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    hw_get_radio_params(lora_params);
    load_profiles();
    if (active_profile_index >= profile_count) active_profile_index = 0;
    chat_profile_index = active_profile_index;

#if defined(ARDUINO) && defined(ARDUINO_T_LORA_PAGER)
    register_profile_filter_shortcut();
#endif

#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(45));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif

    render_profile_list();
}

void ui_lora_hub_exit(lv_obj_t *parent)
{
}

app_t ui_lora_hub_main = {
    .setup_func_cb = ui_lora_hub_enter,
    .exit_func_cb = ui_lora_hub_exit,
    .user_data = nullptr,
};

#endif
