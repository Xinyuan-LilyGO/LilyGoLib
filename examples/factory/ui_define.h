/**
 * @file      ui_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
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

#if !defined(EXCLUDE_BLE_SCANNER)
#include <string>
using std::string;
#endif
using std::vector;

#define DEFAULT_OPA          100

/* ── Theme color presets ── */
typedef struct {
    uint32_t accent;       /* Main accent color */
    uint32_t accent_dim;   /* Pressed state */
    uint32_t card_bg;      /* Card background */
    uint32_t track;        /* Slider/progress track */
    const char *name;      /* Display name */
} ui_theme_preset_t;

static const ui_theme_preset_t ui_theme_presets[] = {
    { 0x00D4AA, 0x009977, 0x1A1A1A, 0x333333, "Mint"     },  /* 0: Current default */
    { 0x0088FF, 0x0066CC, 0x1A1A2E, 0x333344, "Ocean"    },  /* 1: Blue */
    { 0xFF6B35, 0xCC5522, 0x2A1A1A, 0x443333, "Sunset"   },  /* 2: Orange */
    { 0xAA55FF, 0x8833CC, 0x1E1A2A, 0x3A3344, "Violet"   },  /* 3: Purple */
    { 0xFF3366, 0xCC2255, 0x2A1A20, 0x443338, "Cherry"   },  /* 4: Pink/Red */
    { 0x88CC00, 0x669900, 0x1A2A1A, 0x334433, "Lime"     },  /* 5: Green */
    { 0xFFB800, 0xCC9200, 0x2A2A1A, 0x444433, "Amber"    },  /* 6: Gold */
    { 0xFFFFFF, 0xCCCCCC, 0x2A2A2A, 0x555555, "Mono"     },  /* 7: Monochrome */
};
#define UI_THEME_PRESET_COUNT (sizeof(ui_theme_presets) / sizeof(ui_theme_presets[0]))

/* ── Active color palette (runtime mutable) ── */
extern uint32_t ui_active_accent;
extern uint32_t ui_active_accent_dim;
extern uint32_t ui_active_card_bg;
extern uint32_t ui_active_track;

#define UI_COLOR_BG           lv_color_black()
#define UI_COLOR_CARD_BG      lv_color_hex(ui_active_card_bg)
#define UI_COLOR_CARD_FOCUS   lv_color_hex(0x2A2A2A)
#define UI_COLOR_ACCENT       lv_color_hex(ui_active_accent)
#define UI_COLOR_ACCENT_DIM   lv_color_hex(ui_active_accent_dim)
#define UI_COLOR_ACCENT_FOCUS_BORDER lv_color_black()
#define UI_COLOR_ACCENT_FOCUS_OUTLINE lv_color_white()
#define UI_COLOR_WARNING      lv_color_hex(0xFF6B35)
#define UI_COLOR_TEXT_PRIMARY  lv_color_white()
#define UI_COLOR_TEXT_SECONDARY lv_color_hex(0x888888)
#define UI_COLOR_DIVIDER      lv_color_hex(0x333333)
#define UI_COLOR_TRACK        lv_color_hex(ui_active_track)
#define UI_COLOR_KNOB         lv_color_white()

/* ── Shared styles ── */
typedef struct {
    lv_style_t card;
    lv_style_t card_item;
    lv_style_t accent_btn;
    lv_style_t accent_focus_ring;
    lv_style_t focus_glow;
    lv_style_t text_secondary;
    lv_style_t accent_text;   /* Text + shadow color that follows theme accent */
    lv_style_t accent_shadow; /* Shadow-only style for glow effects */
    lv_style_t divider;
    lv_style_t slider_track;
    lv_style_t slider_knob;
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
lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data);
void destroy_msgbox(lv_obj_t *msgbox);

lv_indev_t *lv_get_encoder_indev();
lv_indev_t *lv_get_keyboard_indev();
void menu_show();
void menu_hidden();
void set_default_group(lv_group_t *group);

lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title);

void theme_init();
void ui_styles_refresh(void);
void ui_theme_apply(void);
void ui_add_accent_focus_style(lv_obj_t *obj);

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
void ui_enable_edge_swipe_back(lv_obj_t *root, lv_obj_t *back_target,
                               lv_event_code_t back_event = LV_EVENT_CLICKED);
void ui_destroy_app_page(lv_obj_t *content);
void ui_app_page_enable_nav_auto_hide(lv_obj_t *content, uint32_t timeout_ms);
void ui_app_page_disable_nav_auto_hide(lv_obj_t *content);
lv_obj_t *ui_create_card(lv_obj_t *parent, const char *title);
lv_obj_t *ui_create_card_item(lv_obj_t *card, const char *icon, const char *title, lv_obj_t *widget);
lv_obj_t *ui_create_card_slider(lv_obj_t *card, const char *icon, const char *title,
                                 int32_t min, int32_t max, int32_t val, lv_event_cb_t cb);
void ui_prepare_slider_for_encoder(lv_obj_t *slider);
void ui_style_textarea(lv_obj_t *textarea);
void ui_prepare_textarea_for_encoder(lv_obj_t *textarea);
typedef struct {
    lv_obj_t *lifted_obj;
    lv_obj_t *original_parent;
    int32_t original_index;
} ui_soft_keyboard_lift_t;
bool ui_soft_keyboard_should_open(void);
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

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif
