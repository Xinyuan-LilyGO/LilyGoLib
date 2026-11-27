/**
 * @file      ui_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * Shared declarations, feature gates, and UI helpers used by the Factory
 * application pages.
 */
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <LilyGoLib.h>
#include <WiFi.h>
#include <esp_mac.h>
#else
#define RTC_DATA_ATTR
#endif
#include <lvgl.h>
#include <LVGL_Compat.h>
#ifndef ARDUINO
/* Arduino-compatible clock helper for native SDL builds. */
static inline uint32_t millis(void) { return lv_tick_get(); }
#endif
#include <stdio.h>
#include <vector>
#include <time.h>
#include <string.h>
#include "hal_interface.h"

/*
 * A soft keyboard is useful on touch devices and on T-Embed-Plus, which has
 * no physical keyboard but can navigate LVGL widgets with its rotary encoder.
 * Keep this separate from the touch capability macros: defining a touch macro
 * would incorrectly enable touch-specific layout and input code on Embed Plus.
 */
#if (defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2) || \
     defined(USING_TOUCHPAD)) || \
    defined(USING_INPUT_DEV_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
#define FACTORY_HAS_SOFT_KEYBOARD 1
#else
#define FACTORY_HAS_SOFT_KEYBOARD 0
#endif

#if defined(LILYGO_FACTORY_ENABLE_WALKIE) && LILYGO_FACTORY_ENABLE_WALKIE
#define FACTORY_HAS_WALKIE_DEPS 1
#elif defined(ARDUINO) && __has_include(<PCMFlow.h>) && __has_include(<PCMFlowG722.h>)
#define FACTORY_HAS_WALKIE_DEPS 1
#else
#define FACTORY_HAS_WALKIE_DEPS 0
#endif

#if !FACTORY_HAS_WALKIE_DEPS
#ifndef EXCLUDE_WALKIE
#define EXCLUDE_WALKIE
#endif
#endif

#if defined(LILYGO_FACTORY_ENABLE_WIRELESS_MIC) && LILYGO_FACTORY_ENABLE_WIRELESS_MIC
#define FACTORY_HAS_WIRELESS_MIC_DEPS 1
#elif defined(ARDUINO) && __has_include(<PCMFlowG722.h>)
#define FACTORY_HAS_WIRELESS_MIC_DEPS 1
#else
#define FACTORY_HAS_WIRELESS_MIC_DEPS 0
#endif

#if defined(LILYGO_FACTORY_ENABLE_INA219) && LILYGO_FACTORY_ENABLE_INA219
#define FACTORY_HAS_INA219_DEPS 1
#elif defined(ARDUINO) && __has_include(<Wire.h>) && __has_include(<Adafruit_INA219.h>)
#define FACTORY_HAS_INA219_DEPS 1
#else
#define FACTORY_HAS_INA219_DEPS 0
#endif

#if !FACTORY_HAS_INA219_DEPS
#ifndef EXCLUDE_INA219
#define EXCLUDE_INA219
#endif
#endif

#if !defined(EXCLUDE_BLE_SCANNER)
#include <string>
using std::string;
#endif
using std::vector;

#define DEFAULT_OPA          100

/* ── Theme color presets ── */
typedef struct {
    uint32_t bg;           /* Screen and page background */
    uint32_t app_bg;       /* App launcher icon background */
    uint32_t accent;       /* Main accent color */
    uint32_t accent_dim;   /* Pressed state */
    uint32_t card_bg;      /* Card background */
    uint32_t focus_bg;     /* Focused and pressed control background */
    uint32_t track;        /* Slider/progress track */
    uint32_t text_on_accent; /* Text shown on accent backgrounds */
    bool light;            /* Use the light LVGL base theme */
    const char *name;      /* Display name */
} ui_theme_preset_t;

static const ui_theme_preset_t ui_theme_presets[] = {
    { 0x000000, 0x1A1A1A, 0x00D4AA, 0x009977, 0x1A1A1A, 0x2A2A2A, 0x333333, 0xFFFFFF, false, "Mint"       },  /* 0: Current default */
    { 0x000000, 0x1A1A2E, 0x0088FF, 0x0066CC, 0x1A1A2E, 0x292940, 0x333344, 0xFFFFFF, false, "Ocean"      },  /* 1: Blue */
    { 0x000000, 0x2A1A1A, 0xFF6B35, 0xCC5522, 0x2A1A1A, 0x3A2420, 0x443333, 0xFFFFFF, false, "Sunset"     },  /* 2: Orange */
    { 0x000000, 0x1E1A2A, 0xAA55FF, 0x8833CC, 0x1E1A2A, 0x30283F, 0x3A3344, 0xFFFFFF, false, "Violet"     },  /* 3: Purple */
    { 0x000000, 0x2A1A20, 0xFF3366, 0xCC2255, 0x2A1A20, 0x3B252E, 0x443338, 0xFFFFFF, false, "Cherry"     },  /* 4: Pink/Red */
    { 0x000000, 0x1A2A1A, 0x88CC00, 0x669900, 0x1A2A1A, 0x293B29, 0x334433, 0xFFFFFF, false, "Lime"       },  /* 5: Green */
    { 0x000000, 0x2A2A1A, 0xFFB800, 0xCC9200, 0x2A2A1A, 0x3B3A24, 0x444433, 0xFFFFFF, false, "Amber"      },  /* 6: Gold */
    { 0x000000, 0x2A2A2A, 0xFFFFFF, 0xCCCCCC, 0x2A2A2A, 0x3A3A3A, 0x555555, 0x000000, false, "Mono"       },  /* 7: Monochrome */
    { 0xFFFFFF, 0xE6E6E6, 0xD81B60, 0xAD1457, 0xF5F5F5, 0xF8DCE8, 0xD0C3C9, 0xFFFFFF, true, "Rose Light" },  /* 8: Light with rose accent */
    { 0xF4F9FF, 0xE5F1FF, 0x1976D2, 0x125AA2, 0xFFFFFF, 0xDCEBFA, 0xB8CEE5, 0xFFFFFF, true, "Sky Light"  },  /* 9: Light blue */
    { 0xF3FBF8, 0xDFF3EB, 0x00897B, 0x00695C, 0xFFFFFF, 0xD4EEE6, 0xB5D8CF, 0xFFFFFF, true, "Mint Light" },  /* 10: Light green */
    { 0xFAF7FF, 0xEEE6FB, 0x6A4BBC, 0x4E348A, 0xFFFFFF, 0xE7DDF6, 0xCFC1E6, 0xFFFFFF, true, "Lavender Light" },  /* 11: Light purple */
    { 0xFFF9F0, 0xFFF0D7, 0xC85A00, 0x9D4600, 0xFFFFFF, 0xFCE6C7, 0xE8CFAB, 0xFFFFFF, true, "Amber Light" },  /* 12: Light orange */
};
#define UI_THEME_PRESET_COUNT (sizeof(ui_theme_presets) / sizeof(ui_theme_presets[0]))

/* ── Active color palette (runtime mutable) ── */
extern uint32_t ui_active_bg;
extern uint32_t ui_active_app_bg;
extern uint32_t ui_active_accent;
extern uint32_t ui_active_accent_dim;
extern uint32_t ui_active_card_bg;
extern uint32_t ui_active_card_focus;
extern uint32_t ui_active_track;
extern uint32_t ui_active_text_on_accent;
extern bool ui_active_light_theme;
extern uint32_t ui_previous_bg;
extern uint32_t ui_previous_card_focus;
extern uint32_t ui_previous_accent;
extern uint32_t ui_previous_text_primary;

#define UI_COLOR_BG           lv_color_hex(ui_active_bg)
#define UI_COLOR_APP_BG       lv_color_hex(ui_active_app_bg)
#define UI_COLOR_CARD_BG      lv_color_hex(ui_active_card_bg)
#define UI_COLOR_CARD_FOCUS   lv_color_hex(ui_active_card_focus)
#define UI_COLOR_ACCENT       lv_color_hex(ui_active_accent)
#define UI_COLOR_ACCENT_DIM   lv_color_hex(ui_active_accent_dim)
#define UI_COLOR_ACCENT_FOCUS_BORDER UI_COLOR_ACCENT
#define UI_COLOR_ACCENT_FOCUS_OUTLINE UI_COLOR_ACCENT
#define UI_COLOR_FILLED_FOCUS UI_COLOR_ACCENT_DIM
#define UI_COLOR_WARNING      lv_color_hex(0xFF6B35)
#define UI_COLOR_SIGNAL_GOOD   (ui_active_light_theme ? lv_color_hex(0x18794E) : lv_color_hex(0x55CC88))
#define UI_COLOR_SIGNAL_FAIR   (ui_active_light_theme ? lv_color_hex(0x986000) : lv_color_hex(0xE8B44A))
#define UI_COLOR_SIGNAL_WEAK   (ui_active_light_theme ? lv_color_hex(0xC13B3B) : lv_color_hex(0xF47777))
#define UI_COLOR_TEXT_PRIMARY  (ui_active_light_theme ? lv_color_hex(0x222222) : lv_color_white())
#define UI_COLOR_TEXT_SECONDARY lv_color_hex(0x888888)
#define UI_COLOR_CONTROL_TEXT (ui_active_light_theme ? lv_color_hex(0x222222) : lv_color_white())
#define UI_COLOR_MENU_LABEL   UI_COLOR_TEXT_PRIMARY
#define UI_COLOR_APP_ICON     (ui_active_light_theme ? lv_color_hex(0x4A4A4A) : lv_color_white())
#define UI_COLOR_TEXT_ON_ACCENT lv_color_hex(ui_active_text_on_accent)
#define UI_COLOR_DIVIDER      UI_COLOR_TRACK
#define UI_COLOR_TRACK        lv_color_hex(ui_active_track)
#define UI_COLOR_KNOB         lv_color_white()

/* ── Shared styles ── */
typedef struct {
    lv_style_t card;
    lv_style_t card_item;
    lv_style_t app_btn;
    lv_style_t accent_btn;
    lv_style_t accent_focus_ring;
    lv_style_t button_focus;
    lv_style_t button_focus_pressed;
    lv_style_t button_focus_text;
    lv_style_t focus_glow;
    lv_style_t text_secondary;
    lv_style_t accent_text;   /* Text + shadow color that follows theme accent */
    lv_style_t accent_shadow; /* Shadow-only style for glow effects */
    lv_style_t divider;
    lv_style_t slider_track;
    lv_style_t slider_knob;
    lv_style_t slider_focus_knob;
    lv_style_t switch_on;
    lv_style_t switch_off;
} ui_styles_t;

extern ui_styles_t ui_styles;
void ui_styles_init(void);

typedef void (*app_func_t)(lv_obj_t *parent);

typedef struct {
    app_func_t setup_func_cb;
    app_func_t exit_func_cb;
    void *user_data;
} app_t;


enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
};
typedef uint8_t lv_menu_builder_variant_t;

#define MSG_MENU_NAME_CHANGED    100
#define MSG_LABEL_PARAM_CHANGE_1 200
#define MSG_LABEL_PARAM_CHANGE_2 201
#define MSG_TITLE_NAME_CHANGE    203
#define MSG_BLE_SEND_DATA_1      204
#define MSG_BLE_SEND_DATA_2      205
#define MSG_MUSIC_TIME_ID        300
#define MSG_MUSIC_TIME_END_ID    301
#define MSG_FFT_ID               400

extern lv_obj_t *main_screen;

lv_obj_t *ui_create_option(lv_obj_t *parent, const char *title, const char *symbol_txt, lv_obj_t *(*widget_create)(lv_obj_t *parent), lv_event_cb_t btn_event_cb);
lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                      lv_menu_builder_variant_t builder_variant);
lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                        int32_t val, lv_event_cb_t cb, lv_event_code_t filter);
lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk, lv_event_cb_t cb);
lv_obj_t *create_button(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb);
lv_obj_t *create_label(lv_obj_t *parent, const char *icon, const char *txt, const char *default_text);
lv_obj_t *create_dropdown(lv_obj_t *parent, const char *icon, const char *txt, const char *options, uint8_t default_sel, lv_event_cb_t cb);
/**
 * @brief Create a themed modal with a focusable, scrollable content region.
 * @param parent Owner whose deletion also closes the modal.
 * @param title_txt Single-line title; long titles scroll.
 * @param msg_txt Wrapped body text using the active theme font and size choice.
 * @param btns Optional footer actions terminated by an empty string or nullptr.
 * @param btns_event_cb Action callback; header close/Escape uses the final action
 *        as cancellation. With no actions it closes the modal or calls this callback.
 * @param user_data Data supplied to the action callback.
 * @return Message box; its content accepts encoder click-to-scroll navigation.
 */
lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data);
/** @brief Close a helper modal; nullptr and already closed helper boxes are ignored. */
void destroy_msgbox(lv_obj_t *msgbox);
void ui_msgbox_refresh();
/**
 * @brief Open an opaque full-screen settings page with its own input group.
 * @param owner App object whose deletion also closes this settings page.
 * @param title Single-line page title, following the active theme font.
 * @return Scrollable form content; only its child controls enter the input group.
 * Back/Escape closes the page and restores the app focus. Attach a DELETE
 * callback to the returned content to finish edits and release page resources.
 */
lv_obj_t *ui_create_settings_page(lv_obj_t *owner, const char *title);
/** @brief Close a settings page; nullptr and already closed pages are ignored. */
void ui_destroy_settings_page(lv_obj_t *content);

lv_indev_t *lv_get_encoder_indev();
bool lv_consume_encoder_activity();
void lv_set_encoder_input_suppressed(bool suppressed);
lv_indev_t *lv_get_keyboard_indev();
void menu_show();
void menu_hidden();
void set_default_group(lv_group_t *group);

typedef void (*ui_nfc_child_back_cb_t)(void);
void ui_nfc_set_child_back(ui_nfc_child_back_cb_t cb);
void ui_nfc_child_back(void);

lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title);

void theme_init();
void ui_styles_refresh(void);
void ui_theme_apply(void);
void ui_refresh_app_grid_theme(void);
void ui_refresh_status_bar_theme(void);
void ui_add_accent_focus_style(lv_obj_t *obj);
void ui_sync_button_focus_labels(lv_obj_t *obj);
void ui_disable_focus_shadow(lv_obj_t *obj);

void disable_input_devices();
void enable_input_devices();

void set_low_power_mode_flag(bool enable);

void disable_keyboard();
void enable_keyboard();

lv_obj_t *create_floating_button(lv_event_cb_t event_cb, void* user_data);
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb);
lv_obj_t *create_radius_button(lv_obj_t *parent, const void *image, lv_event_cb_t event_cb, void* user_data);

/* ── New card-style UI components ── */
lv_obj_t *ui_create_app_page(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);
/**
 * @brief Append a themed icon action to the right of an app page's navigation bar.
 * @param content Content returned by ui_create_app_page().
 * @param symbol Icon text, such as LV_SYMBOL_SETTINGS.
 * @param callback Click callback; receives user_data.
 * @param user_data Optional callback context.
 * @return Focusable action button owned and deleted by the page, or nullptr.
 * The title shrinks and scrolls to leave space for all header actions.
 */
lv_obj_t *ui_app_page_add_header_button(lv_obj_t *content, const char *symbol,
                                       lv_event_cb_t callback, void *user_data = nullptr);
void ui_enable_edge_swipe_back(lv_obj_t *root, lv_obj_t *back_target,
                               lv_event_code_t back_event = LV_EVENT_CLICKED);
void ui_destroy_app_page(lv_obj_t *content);
void ui_app_page_enable_nav_auto_hide(lv_obj_t *content, uint32_t timeout_ms);
void ui_app_page_disable_nav_auto_hide(lv_obj_t *content);
lv_obj_t *ui_create_card(lv_obj_t *parent, const char *title);
lv_obj_t *ui_create_card_item(lv_obj_t *card, const char *icon, const char *title, lv_obj_t *widget);
lv_obj_t *ui_create_card_slider(lv_obj_t *card, const char *icon, const char *title,
                                 int32_t min, int32_t max, int32_t val, lv_event_cb_t cb);
lv_obj_t *ui_create_card_value_slider(lv_obj_t *card, const char *icon, const char *title,
                                      int32_t min, int32_t max, int32_t val,
                                      const char *value_suffix, lv_event_cb_t cb);
void ui_prepare_slider_for_encoder(lv_obj_t *slider);
void ui_style_textarea(lv_obj_t *textarea);
void ui_prepare_textarea_for_encoder(lv_obj_t *textarea);
typedef struct {
    lv_obj_t *lifted_obj;
    lv_obj_t *original_parent;
    int32_t original_index;
} ui_soft_keyboard_lift_t;
bool ui_soft_keyboard_should_open(void);
bool ui_soft_keyboard_focus_transition_active(void);
void ui_soft_keyboard_show(lv_obj_t *keyboard, lv_obj_t *textarea,
                           lv_obj_t *lift_obj, ui_soft_keyboard_lift_t *lift);
void ui_soft_keyboard_hide(lv_obj_t *keyboard, ui_soft_keyboard_lift_t *lift);
void ui_soft_keyboard_restore(ui_soft_keyboard_lift_t *lift);
lv_obj_t *ui_create_card_switch(lv_obj_t *card, const char *icon, const char *title,
                                 bool checked, lv_event_cb_t cb);
lv_obj_t *ui_create_card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                                   const char *options, uint8_t sel, lv_event_cb_t cb);
lv_obj_t *ui_create_card_button(lv_obj_t *card, const char *icon, const char *title,
                                 const char *btn_text, lv_event_cb_t cb);
lv_obj_t *ui_create_card_info(lv_obj_t *card, const char *icon, const char *title,
                               const char *value);
lv_obj_t *ui_create_status_bar(lv_obj_t *parent);
void ui_status_bar_update(void);

bool is_screen_small(void);
void ui_set_font_size_pref(uint8_t pref);
uint8_t ui_get_font_size_pref(void);
void ui_apply_theme_preset(uint8_t preset_idx);
void update_page_indicator(void);

#if !defined(EXCLUDE_T_EMBED_LED)
void ui_t_embed_ws2812_set_status_color(uint8_t red, uint8_t green, uint8_t blue);
void ui_t_embed_ws2812_clear_status_color(void);
#endif

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif
