/**
 * @file      ui_t_embed_ws2812.cpp
 * @brief     T-Embed-Plus WS2812 effects and production test.
 */
#include "ui_define.h"

#if defined(ARDUINO_T_EMBED_PLUS)

#include <Adafruit_NeoPixel.h>

namespace {

constexpr uint32_t kEffectIntervalMs = 40;
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

Adafruit_NeoPixel strip(WS2812_NUM_LEDS, WS2812_DATA_PIN, NEO_GRB + NEO_KHZ800);
lv_obj_t *pageContainer = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_timer_t *effectTimer = nullptr;
uint32_t colors[WS2812_NUM_LEDS] = {};
uint16_t phase = 0;
uint8_t effectMode = EFFECT_RAINBOW;
uint8_t brightnessLevel = 4;
uint8_t speed = 50;
uint8_t hue = 0;
bool stripInitialized = false;

void beginStripOnce()
{
    if (!stripInitialized) {
        stripInitialized = strip.begin();
    }
}

uint32_t rgbColor(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}

uint32_t colorWheel(uint8_t position)
{
    position = 255 - position;
    if (position < 85) {
        return rgbColor(255 - position * 3, 0, position * 3);
    }
    if (position < 170) {
        position -= 85;
        return rgbColor(0, position * 3, 255 - position * 3);
    }
    position -= 170;
    return rgbColor(position * 3, 255 - position * 3, 0);
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
    for (uint8_t i = 0; i < WS2812_NUM_LEDS; ++i) {
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
        for (uint8_t i = 0; i < WS2812_NUM_LEDS; ++i) {
            colors[i] = colorWheel(phase8 + i * 256 / WS2812_NUM_LEDS);
        }
        break;
    case EFFECT_COLOR_WIPE: {
        const uint8_t lit = (phase / 24) % (WS2812_NUM_LEDS + 1);
        for (uint8_t i = 0; i < lit; ++i) colors[i] = base;
        break;
    }
    case EFFECT_THEATER:
        for (uint8_t i = 0; i < WS2812_NUM_LEDS; ++i) {
            if (((i + phase / 8) % 3) == 0) colors[i] = base;
        }
        break;
    case EFFECT_COMET: {
        const uint8_t head = (phase / 12) % WS2812_NUM_LEDS;
        for (uint8_t i = 0; i < WS2812_NUM_LEDS; ++i) {
            const uint8_t distance = (head + WS2812_NUM_LEDS - i) % WS2812_NUM_LEDS;
            const uint8_t tail = distance == 0 ? 255 : (distance == 1 ? 120 : (distance == 2 ? 40 : 0));
            if (tail) colors[i] = scaleColor(base, tail);
        }
        break;
    }
    case EFFECT_SCANNER: {
        const uint8_t span = (WS2812_NUM_LEDS - 1) * 2;
        uint8_t position = (phase / 12) % span;
        if (position >= WS2812_NUM_LEDS) position = span - position;
        for (uint8_t i = 0; i < WS2812_NUM_LEDS; ++i) {
            const uint8_t distance = i > position ? i - position : position - i;
            const uint8_t level = distance == 0 ? 255 : (distance == 1 ? 80 : 0);
            if (level) colors[i] = scaleColor(base, level);
        }
        break;
    }
    case EFFECT_SPARKLE:
        fillColors(scaleColor(base, 20));
        colors[random(WS2812_NUM_LEDS)] = base;
        break;
    default:
        fillColors(base);
        break;
    }
}

void refreshOutput()
{
    renderEffect();
    strip.setBrightness(brightnessLevel * 255 / kBrightnessLevels);
    for (uint8_t i = 0; i < WS2812_NUM_LEDS; ++i) {
        strip.setPixelColor(i, colors[i]);
    }
    strip.show();
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

void effectCb(lv_event_t *event)
{
    uint16_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(event));
    effectMode = selected < EFFECT_COUNT ? selected : EFFECT_OFF;
    phase = 0;
    refreshOutput();
}

void brightnessCb(lv_event_t *event)
{
    brightnessLevel = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    refreshOutput();
}

void speedCb(lv_event_t *event)
{
    speed = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
}

void hueCb(lv_event_t *event)
{
    hue = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    refreshOutput();
}

void stopOutput()
{
    if (effectTimer) {
        lv_timer_delete(effectTimer);
        effectTimer = nullptr;
    }
    strip.clear();
    strip.show();
    // Keep GPIO14 attached to RMT. Reconfiguring it would invalidate the
    // Adafruit NeoPixel driver's cached RMT allocation.
}

void backEventCb(lv_event_t *)
{
    stopOutput();
    if (pageContainer) {
        ui_destroy_app_page(pageContainer);
        pageContainer = nullptr;
    }
    statusLabel = nullptr;
    menu_show();
}

} // namespace

void ui_t_embed_ws2812_init()
{
    beginStripOnce();
    strip.clear();
    strip.show();
}

void ui_t_embed_ws2812_enter(lv_obj_t *parent)
{
    beginStripOnce();
    phase = 0;
    pageContainer = ui_create_app_page(parent, "LED Strip", backEventCb);

    lv_obj_t *card = ui_create_card(pageContainer, "Effect");
    lv_obj_t *statusRow = ui_create_card_info(card, LV_SYMBOL_EYE_OPEN, "Status", "Starting");
    statusLabel = lv_obj_get_child(statusRow, lv_obj_get_child_count(statusRow) - 1);
    ui_create_card_dropdown(card, LV_SYMBOL_SETTINGS, "Mode", effectOptions, effectMode, effectCb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Brightness", 1, kBrightnessLevels,
                          brightnessLevel, brightnessCb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Speed", 1, 255, speed, speedCb);
    ui_create_card_slider(card, LV_SYMBOL_SETTINGS, "Hue", 0, 255, hue, hueCb);

    card = ui_create_card(pageContainer, "Output");
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Strip", "GPIO14 x 8");
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Format", "WS2812B GRB");

    refreshOutput();
    effectTimer = lv_timer_create(effectTimerCb, kEffectIntervalMs, nullptr);
}

void ui_t_embed_ws2812_exit(lv_obj_t *)
{
    stopOutput();
}

app_t ui_t_embed_ws2812_main = {
    .setup_func_cb = ui_t_embed_ws2812_enter,
    .exit_func_cb = ui_t_embed_ws2812_exit,
    .user_data = nullptr,
};

#endif // ARDUINO_T_EMBED_PLUS
