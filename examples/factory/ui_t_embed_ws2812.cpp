/**
 * @file      ui_t_embed_ws2812.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-19
 *
 * Persistent T-Embed strip effects, background playback, and sleep-safe status LEDs.
 */
#include "ui_define.h"
#include <LilyGoLog.h>

#ifdef ARDUINO
#include <Preferences.h>
#endif

#if !defined(EXCLUDE_T_EMBED_LED)

#if defined(WS2812_NUM_LEDS)
#include <Adafruit_NeoPixel.h>
#define TEMBED_LED_COUNT WS2812_NUM_LEDS
#if defined(RGB_DATA)
#define TEMBED_WS2812_DATA_PIN RGB_DATA
#else
#define TEMBED_WS2812_DATA_PIN WS2812_DATA_PIN
#endif
#else
#define TEMBED_LED_COUNT APA102_NUM_LEDS
#endif

namespace {

constexpr uint32_t kEffectIntervalMs = 40;
constexpr uint32_t kSaveDelayMs = 600;
constexpr uint8_t kBrightnessLevels = 32;

enum Ws2812Effect : uint8_t {
    EFFECT_OFF,
    EFFECT_SOLID,
    EFFECT_BREATHING,
    EFFECT_RAINBOW,
    EFFECT_COLOR_WIPE,
    EFFECT_THEATER,
    EFFECT_COMET,
    EFFECT_SCANNER,
    EFFECT_SPARKLE,
    EFFECT_COUNT,
};

const char *effectOptions =
    "Off\n"
    "Solid\n"
    "Breathing\n"
    "Rainbow\n"
    "Color Wipe\n"
    "Theater\n"
    "Comet\n"
    "Scanner\n"
    "Sparkle";

const char *effectNames[EFFECT_COUNT] = {
    "Off", "Solid", "Breathing", "Rainbow", "Color Wipe",
    "Theater", "Comet", "Scanner", "Sparkle",
};

#if defined(WS2812_NUM_LEDS)
Adafruit_NeoPixel strip(TEMBED_LED_COUNT, TEMBED_WS2812_DATA_PIN, NEO_GRB + NEO_KHZ800);
#endif
lv_obj_t *pageContainer = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_timer_t *effectTimer = nullptr;
lv_timer_t *saveTimer = nullptr;
uint32_t colors[TEMBED_LED_COUNT] = {};
uint16_t phase = 0;
uint8_t effectMode = EFFECT_RAINBOW;
uint8_t brightnessLevel = 4;
uint8_t speed = 50;
uint8_t hue = 0;
bool stripInitialized = false;
bool keepOnExit = false;
bool suspended = true;
bool statusOverride = false;
uint32_t statusColor = 0;
bool settingsLoaded = false;
bool settingsDirty = false;

struct StripSettings {
    uint8_t version;
    uint8_t effect;
    uint8_t brightness;
    uint8_t speed;
    uint8_t hue;
    uint8_t keepOnExit;
};

void loadSettingsOnce()
{
    if (settingsLoaded) return;
    settingsLoaded = true;
#ifdef ARDUINO
    Preferences prefs;
    if (!prefs.begin("led_strip", true)) return;
    StripSettings saved = {};
    const bool valid = prefs.getBytesLength("config") == sizeof(saved) &&
                       prefs.getBytes("config", &saved, sizeof(saved)) == sizeof(saved) &&
                       saved.version == 1 && saved.effect < EFFECT_COUNT &&
                       saved.brightness >= 1 && saved.brightness <= kBrightnessLevels &&
                       saved.speed >= 1 && saved.keepOnExit <= 1;
    prefs.end();
    if (!valid) {
        LILYGO_LOG_W("[LEDStrip] Invalid saved settings; using defaults");
        return;
    }
    effectMode = saved.effect;
    brightnessLevel = saved.brightness;
    speed = saved.speed;
    hue = saved.hue;
    keepOnExit = saved.keepOnExit != 0;
#endif
}

void saveSettings()
{
    if (!settingsDirty) return;
#ifdef ARDUINO
    const StripSettings saved = {1, effectMode, brightnessLevel, speed, hue,
                                 static_cast<uint8_t>(keepOnExit)};
    Preferences prefs;
    if (!prefs.begin("led_strip", false)) {
        LILYGO_LOG_W("[LEDStrip] Cannot open settings for saving");
        return;
    }
    const bool written = prefs.putBytes("config", &saved, sizeof(saved)) == sizeof(saved);
    prefs.end();
    if (!written) {
        LILYGO_LOG_W("[LEDStrip] Cannot save strip settings");
        return;
    }
#endif
    settingsDirty = false;
}

void flushSettings()
{
    if (saveTimer) {
        lv_timer_delete(saveTimer);
        saveTimer = nullptr;
    }
    saveSettings();
}

void queueSettingsSave()
{
    settingsDirty = true;
    if (saveTimer) {
        lv_timer_reset(saveTimer);
    } else {
        saveTimer = lv_timer_create([](lv_timer_t *) { flushSettings(); }, kSaveDelayMs, nullptr);
        if (!saveTimer) saveSettings();
    }
}

void beginStripOnce()
{
    if (!stripInitialized) {
#if defined(WS2812_NUM_LEDS)
        stripInitialized = strip.begin();
#else
        pinMode(APA102_DATA_PIN, OUTPUT);
        pinMode(APA102_CLOCK_PIN, OUTPUT);
        digitalWrite(APA102_DATA_PIN, LOW);
        digitalWrite(APA102_CLOCK_PIN, LOW);
        stripInitialized = true;
#endif
    }
}

#if defined(APA102_NUM_LEDS)
void apa102WriteByte(uint8_t value)
{
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        digitalWrite(APA102_DATA_PIN, value & mask ? HIGH : LOW);
        digitalWrite(APA102_CLOCK_PIN, HIGH);
        digitalWrite(APA102_CLOCK_PIN, LOW);
    }
}

void apa102Show(uint8_t brightness)
{
    for (uint8_t i = 0; i < 4; ++i) apa102WriteByte(0x00);
    const uint8_t globalBrightness = max<uint8_t>(1, brightness * 31 / kBrightnessLevels);
    for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
        apa102WriteByte(0xE0 | globalBrightness);
        apa102WriteByte(colors[i] & 0xFF);
        apa102WriteByte((colors[i] >> 8) & 0xFF);
        apa102WriteByte((colors[i] >> 16) & 0xFF);
    }
    for (uint8_t i = 0; i < 4; ++i) apa102WriteByte(0xFF);
}
#endif

uint32_t rgbColor(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

uint32_t colorWheel(uint8_t position)
{
    return ui_led_strip_hue_color(position);
}

uint32_t scaleColor(uint32_t color, uint8_t amount)
{
    const uint8_t red = ((color >> 16) & 0xFF) * amount / 255;
    const uint8_t green = ((color >> 8) & 0xFF) * amount / 255;
    const uint8_t blue = (color & 0xFF) * amount / 255;
    return rgbColor(red, green, blue);
}

void clearColors()
{
    memset(colors, 0, sizeof(colors));
}

void fillColors(uint32_t color)
{
    for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
        colors[i] = color;
    }
}

void renderEffect()
{
    clearColors();
    const uint32_t base = colorWheel(hue);
    const uint8_t phase8 = (uint8_t)phase;

    switch (effectMode) {
    case EFFECT_OFF:
        break;
    case EFFECT_SOLID:
        fillColors(base);
        break;
    case EFFECT_BREATHING: {
        uint16_t breath = phase % 512;
        if (breath > 255) breath = 511 - breath;
        fillColors(scaleColor(base, 32 + breath * 223 / 255));
        break;
    }
    case EFFECT_RAINBOW:
        for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
            colors[i] = colorWheel(phase8 + i * 256 / TEMBED_LED_COUNT);
        }
        break;
    case EFFECT_COLOR_WIPE: {
        const uint8_t lit = (phase / 24) % (TEMBED_LED_COUNT + 1);
        for (uint8_t i = 0; i < lit; ++i) colors[i] = base;
        break;
    }
    case EFFECT_THEATER:
        for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
            if (((i + phase / 8) % 3) == 0) colors[i] = base;
        }
        break;
    case EFFECT_COMET: {
        const uint8_t head = (phase / 12) % TEMBED_LED_COUNT;
        for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
            const uint8_t distance = (head + TEMBED_LED_COUNT - i) % TEMBED_LED_COUNT;
            const uint8_t tail = distance == 0 ? 255 : (distance == 1 ? 120 : (distance == 2 ? 40 : 0));
            if (tail) colors[i] = scaleColor(base, tail);
        }
        break;
    }
    case EFFECT_SCANNER: {
        const uint8_t span = (TEMBED_LED_COUNT - 1) * 2;
        uint8_t position = (phase / 12) % span;
        if (position >= TEMBED_LED_COUNT) position = span - position;
        for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
            const uint8_t distance = i > position ? i - position : position - i;
            const uint8_t level = distance == 0 ? 255 : (distance == 1 ? 80 : 0);
            if (level) colors[i] = scaleColor(base, level);
        }
        break;
    }
    case EFFECT_SPARKLE:
        fillColors(scaleColor(base, 20));
        colors[random(TEMBED_LED_COUNT)] = base;
        break;
    default:
        fillColors(base);
        break;
    }
}

void clearOutput()
{
#if defined(WS2812_NUM_LEDS)
    strip.clear();
    strip.show();
    // Keep the NeoPixel driver's cached RMT allocation attached to its GPIO.
#else
    clearColors();
    apa102Show(1);
#endif
}

void refreshOutput()
{
    if (suspended || (!pageContainer && !keepOnExit && !statusOverride)) {
        clearOutput();
        return;
    }
    if (statusOverride) fillColors(statusColor);
    else renderEffect();
    const uint8_t level = statusOverride && brightnessLevel < 4 ? 4 : brightnessLevel;
#if defined(WS2812_NUM_LEDS)
    strip.setBrightness(level * 255 / kBrightnessLevels);
    for (uint8_t i = 0; i < TEMBED_LED_COUNT; ++i) {
        strip.setPixelColor(i, colors[i]);
    }
    strip.show();
#else
    apa102Show(level);
#endif
    if (statusLabel) {
        lv_label_set_text_fmt(statusLabel, "%s  B:%u/32  S:%u",
                              effectNames[effectMode], brightnessLevel, speed);
    }
}

void effectTimerCb(lv_timer_t *)
{
    uint8_t advance = (speed + 19) / 20;
    if (advance == 0) advance = 1;
    phase += advance;
    refreshOutput();
}

void updateOutputState()
{
    const bool animate = !suspended && !statusOverride && (pageContainer || keepOnExit) &&
                         effectMode != EFFECT_OFF && effectMode != EFFECT_SOLID;
    if (animate && !effectTimer) {
        effectTimer = lv_timer_create(effectTimerCb, kEffectIntervalMs, nullptr);
    } else if (!animate && effectTimer) {
        lv_timer_delete(effectTimer);
        effectTimer = nullptr;
    }
    refreshOutput();
}

void effectCb(lv_event_t *event)
{
    uint16_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(event));
    effectMode = selected < EFFECT_COUNT ? selected : EFFECT_OFF;
    phase = 0;
    updateOutputState();
    queueSettingsSave();
}

void brightnessCb(lv_event_t *event)
{
    brightnessLevel = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    refreshOutput();
    queueSettingsSave();
}

void speedCb(lv_event_t *event)
{
    speed = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    refreshOutput();
    queueSettingsSave();
}

void hueCb(lv_event_t *event)
{
    hue = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    refreshOutput();
    queueSettingsSave();
}

void keepOnExitCb(lv_event_t *event)
{
    keepOnExit = lv_dropdown_get_selected(lv_event_get_target_obj(event)) != 0;
    queueSettingsSave();
}

void pageDeleted(lv_event_t *event)
{
    if (lv_event_get_target_obj(event) != pageContainer) return;
    pageContainer = nullptr;
    statusLabel = nullptr;
    flushSettings();
    updateOutputState();
}

void leavePage()
{
    lv_obj_t *page = pageContainer;
    pageContainer = nullptr;
    statusLabel = nullptr;
    flushSettings();
    updateOutputState();
    if (page) ui_destroy_app_page(page);
}

void backEventCb(lv_event_t *)
{
    leavePage();
    menu_show();
}

} // namespace

void ui_t_embed_ws2812_init()
{
    beginStripOnce();
    loadSettingsOnce();
    clearOutput();
}

void ui_t_embed_ws2812_set_status_color(uint8_t red, uint8_t green, uint8_t blue)
{
    beginStripOnce();
    statusOverride = true;
    statusColor = rgbColor(red, green, blue);
    updateOutputState();
}

void ui_t_embed_ws2812_clear_status_color()
{
    statusOverride = false;
    updateOutputState();
}

void ui_t_embed_ws2812_suspend()
{
    suspended = true;
    flushSettings();
    updateOutputState();
}

void ui_t_embed_ws2812_resume()
{
    beginStripOnce();
    loadSettingsOnce();
    suspended = false;
    updateOutputState();
}

void ui_t_embed_ws2812_enter(lv_obj_t *parent)
{
    beginStripOnce();
    loadSettingsOnce();
    if (pageContainer) leavePage();
    suspended = false;
    statusOverride = false;
    pageContainer = ui_create_app_page(parent, "LED Strip", backEventCb);
    lv_obj_add_event_cb(pageContainer, pageDeleted, LV_EVENT_DELETE, nullptr);

    lv_obj_t *card = ui_create_card(pageContainer, "Effect");
    lv_obj_t *statusRow = ui_create_card_info(card, LV_SYMBOL_EYE_OPEN, "Status", "Starting");
    statusLabel = lv_obj_get_child(statusRow, lv_obj_get_child_count(statusRow) - 1);
    ui_create_card_dropdown(card, LV_SYMBOL_SETTINGS, "Mode", effectOptions, effectMode, effectCb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Brightness", 1, kBrightnessLevels,
                          brightnessLevel, brightnessCb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Speed", 1, 255, speed, speedCb);
    ui_create_card_hue_slider(card, LV_SYMBOL_SETTINGS, "Hue", hue, hueCb);

    card = ui_create_card(pageContainer, "Output");
    ui_create_card_dropdown(card, LV_SYMBOL_SETTINGS, "On Exit", "Turn off\nKeep on",
                            keepOnExit ? 1 : 0, keepOnExitCb);
#if defined(WS2812_NUM_LEDS)
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Strip", "GPIO14 x 8");
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Format", "WS2812B GRB");
#else
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Strip", "GPIO42/45 x 7");
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Format", "APA102 BGR");
#endif

    updateOutputState();
}

void ui_t_embed_ws2812_exit(lv_obj_t *)
{
    leavePage();
}

app_t ui_t_embed_ws2812_main = {
    .setup_func_cb = ui_t_embed_ws2812_enter,
    .exit_func_cb = ui_t_embed_ws2812_exit,
    .user_data = nullptr,
};

#endif
