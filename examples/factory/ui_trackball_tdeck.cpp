/**
 * @file      ui_trackball_tdeck.cpp
 * @brief     Original T-Deck trackball test and sensitivity control.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_TRACKBALL) && defined(ARDUINO_T_DECK)

static constexpr int32_t TDECK_TRACKBALL_SENS_MIN = 1;
static constexpr int32_t TDECK_TRACKBALL_SENS_MAX = 64;
static constexpr int32_t TDECK_TRACKBALL_SENS_DEFAULT = 32;

static lv_obj_t *page_container = NULL;
static lv_obj_t *cursor_dot = NULL;
static lv_obj_t *sensitivity_label = NULL;
static lv_obj_t *button_led = NULL;
static lv_obj_t *button_label = NULL;
static int32_t cursor_max_x = 0;
static int32_t cursor_max_y = 0;
static int32_t cursor_x = 0;
static int32_t cursor_y = 0;
static int32_t sensitivity = TDECK_TRACKBALL_SENS_DEFAULT;

static const char *button_event_name(uint8_t state)
{
    switch (state) {
    case BUTTON_EVENT_CLICK:
        return "Click";
    case BUTTON_EVENT_LONG_PRESSED:
        return "Long press";
    case BUTTON_EVENT_DOUBLE_CLICK:
        return "Double click";
    default:
        return "Unknown";
    }
}

static void update_sensitivity_label()
{
    if (sensitivity_label) {
        lv_label_set_text_fmt(sensitivity_label, "%ldx", (long)sensitivity);
    }
}

static void sensitivity_slider_cb(lv_event_t *event)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    sensitivity = lv_slider_get_value(slider);
    update_sensitivity_label();
}

static void trackball_callback(int8_t delta_x, int8_t delta_y)
{
    if (!cursor_dot) {
        return;
    }

    cursor_x += delta_x * sensitivity;
    cursor_y += delta_y * sensitivity;
    if (cursor_x < -cursor_max_x) cursor_x = -cursor_max_x;
    if (cursor_x > cursor_max_x) cursor_x = cursor_max_x;
    if (cursor_y < -cursor_max_y) cursor_y = -cursor_max_y;
    if (cursor_y > cursor_max_y) cursor_y = cursor_max_y;
    lv_obj_align(cursor_dot, LV_ALIGN_CENTER, cursor_x, cursor_y);
}

static void button_callback(uint8_t id, uint8_t state)
{
    if (id != BUTTON_CENTER) {
        return;
    }
    if (button_led) {
        lv_led_toggle(button_led);
    }
    if (button_label) {
        lv_label_set_text(button_label, button_event_name(state));
    }
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    hw_set_trackball_callback(NULL);
    hw_set_button_callback(NULL);

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    cursor_dot = NULL;
    sensitivity_label = NULL;
    button_led = NULL;
    button_label = NULL;
    menu_show();
}

void ui_trackball_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "Trackball", back_event_handler);

    lv_obj_t *preview_card = ui_create_card(page_container, "Movement");
    lv_obj_remove_flag(preview_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *track_area = lv_obj_create(preview_card);
    int32_t movement_height = lv_display_get_vertical_resolution(NULL) - 48;
    if (movement_height < 120) {
        movement_height = 120;
    }
    lv_obj_set_size(track_area, LV_PCT(100), movement_height);
    lv_obj_set_style_bg_color(track_area, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(track_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(track_area, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(track_area, 1, 0);
    lv_obj_set_style_radius(track_area, 4, 0);
    lv_obj_set_style_pad_all(track_area, 0, 0);
    lv_obj_set_scrollbar_mode(track_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(track_area, LV_OBJ_FLAG_SCROLLABLE);

    cursor_dot = lv_obj_create(track_area);
    lv_obj_set_size(cursor_dot, 24, 24);
    lv_obj_set_style_radius(cursor_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cursor_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(cursor_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cursor_dot, 0, 0);
    lv_obj_remove_flag(cursor_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cursor_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(cursor_dot);

    lv_obj_t *control_card = ui_create_card(page_container, "Original T-Deck");
    lv_obj_t *row = ui_create_card_info(control_card, LV_SYMBOL_SETTINGS,
                                         "Sensitivity", "4x");
    sensitivity_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    lv_obj_t *slider = lv_slider_create(lv_obj_create(control_card));
    lv_slider_set_range(slider, TDECK_TRACKBALL_SENS_MIN, TDECK_TRACKBALL_SENS_MAX);
    lv_slider_set_value(slider, sensitivity, LV_ANIM_OFF);
    lv_obj_set_size(slider, LV_PCT(50), 12);
    lv_obj_set_flex_grow(slider, 1);
    ui_prepare_slider_for_encoder(slider);
    lv_obj_add_event_cb(slider, sensitivity_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(control_card, LV_SYMBOL_SETTINGS, "Cursor gain", slider);

    row = ui_create_card_info(control_card, LV_SYMBOL_OK, "User button", "Waiting");
    button_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    button_led = lv_led_create(row);
    lv_obj_set_size(button_led, 16, 16);
    lv_led_set_color(button_led, lv_palette_main(LV_PALETTE_GREEN));
    lv_led_off(button_led);

    lv_obj_update_layout(track_area);
    cursor_max_x = (lv_obj_get_width(track_area) - lv_obj_get_width(cursor_dot)) / 2;
    cursor_max_y = (lv_obj_get_height(track_area) - lv_obj_get_height(cursor_dot)) / 2;
    cursor_x = 0;
    cursor_y = 0;
    update_sensitivity_label();

    hw_set_trackball_callback(trackball_callback);
    hw_set_button_callback(button_callback);
}

void ui_trackball_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_trackball_main = {
    .setup_func_cb = ui_trackball_enter,
    .exit_func_cb = ui_trackball_exit,
    .user_data = nullptr,
};

#endif /* !EXCLUDE_TRACKBALL && ARDUINO_T_DECK */
