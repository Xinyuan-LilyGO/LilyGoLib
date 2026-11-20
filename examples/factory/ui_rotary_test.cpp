/**
 * @file      ui_rotary_test.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-19
 *
 * Provides a visual rotary encoder diagnostic for Factory-supported boards.
 */
#include "ui_define.h"
#include <LV_Helper.h>

#if FACTORY_HAS_ROTARY

#define ROTARY_SCALE_TICK_COUNT       21
#define ROTARY_SCALE_LABEL_COUNT       5

static lv_obj_t *page_container;
static lv_timer_t *diagnostic_timer;
static lv_obj_t *position_label;
static lv_obj_t *direction_label;
static lv_obj_t *event_label;
static lv_obj_t *stat_labels[6];
static lv_obj_t *scale_labels[ROTARY_SCALE_LABEL_COUNT];

#if defined(ARDUINO_T_EMBED_PLUS)
static constexpr uint32_t USER_BUTTON_DEBOUNCE_MS = 20;
static uint32_t user_button_clicks;
static uint32_t user_button_changed_at;
static bool user_button_raw_pressed;
static bool user_button_pressed;

static void reset_user_button_diagnostics()
{
    pinMode(BOARD_USER_KEY, INPUT_PULLUP);
    user_button_raw_pressed = digitalRead(BOARD_USER_KEY) == LOW;
    user_button_pressed = user_button_raw_pressed;
    user_button_clicks = 0;
    user_button_changed_at = millis();
}

static void update_user_button_diagnostics()
{
    const bool pressed = digitalRead(BOARD_USER_KEY) == LOW;
    const uint32_t now = millis();
    if (pressed != user_button_raw_pressed) {
        user_button_raw_pressed = pressed;
        user_button_changed_at = now;
    }
    if (user_button_pressed != user_button_raw_pressed &&
            now - user_button_changed_at >= USER_BUTTON_DEBOUNCE_MS) {
        user_button_pressed = user_button_raw_pressed;
        if (user_button_pressed) {
            user_button_clicks++;
        }
    }
}
#endif

static int32_t physical_position(const lv_rotary_diagnostics_t &snapshot)
{
#if defined(ARDUINO_T_EMBED) || defined(ARDUINO_T_EMBED_PLUS)
    return -snapshot.position;
#else
    return snapshot.position;
#endif
}

static int16_t physical_diff(const lv_rotary_diagnostics_t &snapshot)
{
#if defined(ARDUINO_T_EMBED) || defined(ARDUINO_T_EMBED_PLUS)
    return -snapshot.last_diff;
#else
    return snapshot.last_diff;
#endif
}

static uint32_t clockwise_steps(const lv_rotary_diagnostics_t &snapshot)
{
#if defined(ARDUINO_T_EMBED) || defined(ARDUINO_T_EMBED_PLUS)
    return snapshot.negative_steps;
#else
    return snapshot.positive_steps;
#endif
}

static uint32_t counterclockwise_steps(const lv_rotary_diagnostics_t &snapshot)
{
#if defined(ARDUINO_T_EMBED) || defined(ARDUINO_T_EMBED_PLUS)
    return snapshot.positive_steps;
#else
    return snapshot.negative_steps;
#endif
}

static void update_diagnostics(lv_timer_t *)
{
    lv_rotary_diagnostics_t snapshot = {};
    if (!lv_get_rotary_diagnostics(&snapshot) || !position_label) return;

    bool button_pressed = snapshot.button_pressed;
    uint32_t button_clicks = snapshot.button_clicks;
#if defined(ARDUINO_T_EMBED_PLUS)
    update_user_button_diagnostics();
    button_pressed = user_button_pressed;
    button_clicks = user_button_clicks;
#endif

    const int32_t position = physical_position(snapshot);
    const int16_t diff = physical_diff(snapshot);
    lv_label_set_text_fmt(position_label, "%ld", (long)position);

    for (int i = 0; i < ROTARY_SCALE_LABEL_COUNT; ++i) {
        const int32_t value = position + (i - 2) * 5;
        lv_label_set_text_fmt(scale_labels[i], "%ld", (long)value);
    }

    if (snapshot.event_count == 0) {
        lv_label_set_text(direction_label, "READY");
    } else if (diff > 0) {
        lv_label_set_text_fmt(direction_label, "CW +%d", diff);
    } else {
        lv_label_set_text_fmt(direction_label, "CCW %d", diff);
    }

    if (button_pressed) {
#if defined(ARDUINO_T_EMBED_PLUS)
        lv_label_set_text(event_label, "IO6  PRESS");
#else
        lv_label_set_text_fmt(event_label, "E%lu  BUTTON", (unsigned long)snapshot.event_count);
#endif
        lv_obj_set_style_text_color(event_label, UI_COLOR_ACCENT, 0);
    } else if (snapshot.event_count == 0) {
#if defined(ARDUINO_T_EMBED_PLUS)
        lv_label_set_text(event_label, "Turn / IO6");
#else
        lv_label_set_text(event_label, "Turn encoder");
#endif
        lv_obj_set_style_text_color(event_label, UI_COLOR_TEXT_SECONDARY, 0);
    } else {
        lv_label_set_text_fmt(event_label, "E%lu  %lums",
                              (unsigned long)snapshot.event_count,
                              (unsigned long)snapshot.last_interval_ms);
        lv_obj_set_style_text_color(event_label, UI_COLOR_TEXT_SECONDARY, 0);
    }

    lv_label_set_text_fmt(stat_labels[0], "CW  %lu", (unsigned long)clockwise_steps(snapshot));
    lv_label_set_text_fmt(stat_labels[1], "CCW  %lu", (unsigned long)counterclockwise_steps(snapshot));
    lv_label_set_text_fmt(stat_labels[2], "PRESS  %lu", (unsigned long)button_clicks);
    lv_label_set_text_fmt(stat_labels[3], "MULTI  %lu", (unsigned long)snapshot.multi_step_events);
    lv_label_set_text_fmt(stat_labels[4], "FLIP  %lu", (unsigned long)snapshot.rapid_reverse_events);
    lv_label_set_text_fmt(stat_labels[5], "MAX  %u", snapshot.max_abs_diff);

    lv_obj_set_style_text_color(stat_labels[3],
                                snapshot.multi_step_events ? UI_COLOR_WARNING : UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_color(stat_labels[4],
                                snapshot.rapid_reverse_events ? UI_COLOR_WARNING : UI_COLOR_TEXT_SECONDARY, 0);
}

static void cleanup_page()
{
    if (diagnostic_timer) {
        lv_timer_delete(diagnostic_timer);
        diagnostic_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    position_label = NULL;
    direction_label = NULL;
    event_label = NULL;
    memset(stat_labels, 0, sizeof(stat_labels));
    memset(scale_labels, 0, sizeof(scale_labels));
}

static void back_event_handler(lv_event_t *)
{
    cleanup_page();
    menu_show();
}

static lv_obj_t *create_plain_object(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void create_scale(lv_obj_t *parent, lv_coord_t height)
{
    lv_obj_t *scale = create_plain_object(parent);
    lv_obj_set_size(scale, LV_PCT(100), height);
    lv_obj_set_style_bg_color(scale, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(scale, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scale, 6, 0);
    lv_obj_set_style_border_width(scale, 1, 0);
    lv_obj_set_style_border_color(scale, UI_COLOR_DIVIDER, 0);

    position_label = lv_label_create(scale);
    lv_label_set_text(position_label, "0");
    lv_obj_set_style_text_color(position_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(position_label,
                               height <= 80 ? &lv_font_montserrat_24 : &lv_font_montserrat_32, 0);
    lv_obj_align(position_label, LV_ALIGN_TOP_MID, 0, -2);

    direction_label = lv_label_create(scale);
    lv_label_set_text(direction_label, "READY");
    lv_obj_set_style_text_color(direction_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(direction_label, &lv_font_montserrat_10, 0);
    lv_obj_align(direction_label, LV_ALIGN_TOP_LEFT, 6, 5);

    event_label = lv_label_create(scale);
    lv_label_set_text(event_label, "Turn encoder");
    lv_obj_set_style_text_color(event_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(event_label, &lv_font_montserrat_10, 0);
    lv_obj_align(event_label, LV_ALIGN_TOP_RIGHT, -6, 5);

    lv_obj_update_layout(scale);
    const lv_coord_t width = lv_obj_get_content_width(scale);
    const lv_coord_t tick_bottom = height - 18;
    const lv_coord_t pointer_height = height <= 80 ? 25 : 45;
    for (int i = 0; i < ROTARY_SCALE_TICK_COUNT; ++i) {
        const bool major = (i % 5) == 0;
        const bool center = i == ROTARY_SCALE_TICK_COUNT / 2;
        const lv_coord_t tick_height = center ? pointer_height : (major ? 12 : 7);
        lv_obj_t *tick = create_plain_object(scale);
        lv_obj_set_size(tick, center ? 2 : 1, tick_height);
        lv_obj_set_style_bg_color(tick, center ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_bg_opa(tick, center ? LV_OPA_COVER : LV_OPA_70, 0);
        const lv_coord_t x = (width - 1) * i / (ROTARY_SCALE_TICK_COUNT - 1);
        lv_obj_set_pos(tick, x, tick_bottom - tick_height - (center ? 2 : 0));
        if (center) {
            lv_obj_move_to_index(tick, 0);
        }
    }

    for (int i = 0; i < ROTARY_SCALE_LABEL_COUNT; ++i) {
        scale_labels[i] = lv_label_create(scale);
        lv_label_set_text(scale_labels[i], "0");
        lv_obj_set_width(scale_labels[i], 54);
        lv_obj_set_style_text_align(scale_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(scale_labels[i], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(scale_labels[i],
                                    i == 2 ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
        const lv_coord_t x = (width - 54) * i / (ROTARY_SCALE_LABEL_COUNT - 1);
        lv_obj_align(scale_labels[i], LV_ALIGN_TOP_LEFT, x, height - 17);
    }
}

static void create_stats(lv_obj_t *parent, lv_coord_t height)
{
    lv_obj_t *stats = create_plain_object(parent);
    lv_obj_set_size(stats, LV_PCT(100), height);

    const lv_coord_t width = lv_obj_get_content_width(parent);
    const lv_coord_t column_width = width / 3;
    const lv_coord_t row_height = height / 2;
    const lv_font_t *font = width <= 320 ? &lv_font_montserrat_10 : &lv_font_montserrat_12;
    for (int i = 0; i < 6; ++i) {
        stat_labels[i] = lv_label_create(stats);
        lv_label_set_text(stat_labels[i], "--");
        lv_obj_set_width(stat_labels[i], column_width);
        lv_obj_set_style_text_align(stat_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(stat_labels[i], font, 0);
        lv_obj_set_style_text_color(stat_labels[i], UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_pos(stat_labels[i], (i % 3) * column_width,
                       (i / 3) * row_height + (row_height - lv_font_get_line_height(font)) / 2);
    }
}

static void ui_rotary_test_enter(lv_obj_t *parent)
{
    lv_reset_rotary_diagnostics();
#if defined(ARDUINO_T_EMBED_PLUS)
    reset_user_button_diagnostics();
#endif
    page_container = ui_create_app_page(parent, "Encoder Test", back_event_handler);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(page_container);

    const lv_coord_t available = lv_obj_get_content_height(page_container);
    lv_coord_t scale_height = available * 3 / 5;
    if (scale_height < 72) scale_height = 72;
    if (scale_height > 160) scale_height = 160;
    lv_coord_t stats_height = available - scale_height - 8;
    if (stats_height < 38) stats_height = 38;
    if (stats_height > 84) stats_height = 84;

    create_scale(page_container, scale_height);
    create_stats(page_container, stats_height);
    update_diagnostics(NULL);
    diagnostic_timer = lv_timer_create(update_diagnostics, 40, NULL);
}

static void ui_rotary_test_exit(lv_obj_t *)
{
    cleanup_page();
}

app_t ui_rotary_test_main = {
    .setup_func_cb = ui_rotary_test_enter,
    .exit_func_cb = ui_rotary_test_exit,
    .user_data = nullptr,
};

#endif
