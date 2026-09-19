/**
 * @file      ui_vibration.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-10
 *
 */
#include "ui_define.h"

#if !defined(EXCLUDE_DRV2605)

static lv_obj_t *page_container = NULL;
static lv_obj_t *effect_value_label = NULL;
static lv_obj_t *status_label = NULL;

static void update_effect_labels(uint8_t effect, bool saved)
{
    if (effect_value_label) {
        lv_label_set_text_fmt(effect_value_label, "Effect %u", (unsigned)effect);
    }
    if (status_label) {
        lv_label_set_text(status_label, saved ? "Saved" : "Save failed");
        lv_obj_set_style_text_color(status_label, saved ? UI_COLOR_ACCENT : UI_COLOR_WARNING, 0);
    }
}

static void effect_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    uint8_t effect = (uint8_t)lv_slider_get_value(slider);
    bool saved = hw_set_haptic_effect(effect);
    update_effect_labels(effect, saved);
}

static void preview_button_cb(lv_event_t *e)
{
    (void)e;
    hw_feedback();
    if (status_label) {
        lv_label_set_text(status_label, "Playing");
        lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    }
}

static void back_event_cb(lv_event_t *e)
{
    (void)e;
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    effect_value_label = NULL;
    status_label = NULL;
    menu_show();
}

void ui_vibration_enter(lv_obj_t *parent)
{
    const uint8_t effect = hw_get_haptic_effect();
    page_container = ui_create_app_page(parent, "Vibration", back_event_cb);

    lv_obj_t *status_card = ui_create_card(page_container, "DRV2605");
    ui_create_card_info(status_card, LV_SYMBOL_BELL, "Driver", "Online");
    lv_obj_t *effect_row = ui_create_card_info(status_card, LV_SYMBOL_SETTINGS, "Selected", "");
    effect_value_label = lv_obj_get_child(effect_row, lv_obj_get_child_count(effect_row) - 1);
    status_label = lv_label_create(status_card);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    lv_obj_t *control_card = ui_create_card(page_container, "Effect");
    ui_create_card_slider(control_card, LV_SYMBOL_SETTINGS, "Waveform", 1, 117,
                          effect, effect_slider_cb);
    ui_create_card_button(control_card, LV_SYMBOL_PLAY, "Preview", "Play", preview_button_cb);

    lv_obj_t *info_card = ui_create_card(page_container, "Library");
    ui_create_card_info(info_card, LV_SYMBOL_LIST, "Waveforms", "1 - 117");
    ui_create_card_info(info_card, LV_SYMBOL_SAVE, "Storage", "Persistent");
    lv_label_set_text_fmt(effect_value_label, "Effect %u", (unsigned)effect);
}

void ui_vibration_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_vibration_main = {
    .setup_func_cb = ui_vibration_enter,
    .exit_func_cb = ui_vibration_exit,
    .user_data = nullptr,
};

#endif
