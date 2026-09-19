/**
 * @file      ui_trackball.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-08-16
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include <math.h>

#if !defined(EXCLUDE_TRACKBALL) && !defined(ARDUINO_T_DECK)

static lv_obj_t *page_container = NULL;
static lv_timer_t *timer = NULL;
static int32_t ball_max_x;
static int32_t ball_max_y;
static lv_obj_t *center_circle;
static lv_obj_t *track_area;
static lv_obj_t *right_led;
static lv_obj_t *left_led;
static lv_obj_t *center_led;

static void update_pos(int8_t delta_x, int8_t delta_y)
{
    static lv_coord_t ball_x = 0;
    static lv_coord_t ball_y = 0;

    ball_x += delta_x;
    ball_y += delta_y;

    if (ball_x < -ball_max_x) ball_x = -ball_max_x;
    if (ball_x > ball_max_x)  ball_x = ball_max_x;
    if (ball_y < -ball_max_y) ball_y = -ball_max_y;
    if (ball_y > ball_max_y)  ball_y = ball_max_y;
    lv_obj_set_pos(center_circle, ball_x, ball_y);
}

static void button_callback(uint8_t id, uint8_t state)
{
    LILYGO_LOG_PRINTF("id:%d state:%d\n", id, state);
    switch (id) {
    case 0:
        lv_led_toggle(left_led);
        break;
    case 1:
        lv_led_toggle(center_led);
        break;
    case 2:
        lv_led_toggle(right_led);
        break;
    default:
        break;
    }
}

static void back_event_handler(lv_event_t *e)
{
    if (timer) {
        lv_timer_delete(timer); timer = NULL;
    }
    hw_set_trackball_callback(NULL);
    hw_set_button_callback(NULL);

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    menu_show();
}

void ui_trackball_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "Trackball", back_event_handler);

    /* Fill the entire content area with a single card */
    lv_obj_t *card = lv_obj_create(page_container);
    lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Force layout calculation so dimensions are correct */
    lv_obj_update_layout(card);

    /* Get usable dimensions */
    int32_t card_w = lv_obj_get_width(card) - 16;  /* subtract padding */
    int32_t card_h = lv_obj_get_height(card) - 16; /* subtract padding */
    int32_t led_row_h = 30;
    int32_t track_h = card_h - led_row_h - 8; /* 8px gap between track and LEDs */
    int32_t track_w = card_w;

    /* Track area — rectangle, fills most of the card */
    track_area = lv_obj_create(card);
    lv_obj_set_size(track_area, track_w, track_h);
    lv_obj_set_style_bg_color(track_area, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(track_area, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track_area, 8, 0);
    lv_obj_set_style_border_color(track_area, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(track_area, 1, 0);
    lv_obj_align(track_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_scroll_dir(track_area, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(track_area, LV_SCROLLBAR_MODE_OFF);

    /* Movable ball */
    center_circle = lv_obj_create(track_area);
    lv_obj_set_size(center_circle, 30, 30);
    lv_obj_set_style_radius(center_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_circle, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(center_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_circle, 0, 0);
    lv_obj_set_style_shadow_width(center_circle, 8, 0);
    lv_obj_set_style_shadow_color(center_circle, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(center_circle, LV_OPA_40, 0);
    lv_obj_center(center_circle);

    ball_max_x = (track_w - 30) / 2;
    ball_max_y = (track_h - 30) / 2;

    /* LED row — three LEDs at the bottom */
    lv_obj_t *led_row = lv_obj_create(card);
    lv_obj_set_size(led_row, LV_PCT(100), led_row_h);
    lv_obj_set_style_bg_opa(led_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(led_row, 0, 0);
    lv_obj_set_style_radius(led_row, 0, 0);
    lv_obj_set_style_pad_all(led_row, 0, 0);
    lv_obj_align(led_row, LV_ALIGN_BOTTOM_MID, 0, 0);

    left_led = lv_led_create(led_row);
    lv_obj_set_size(left_led, 18, 18);
    lv_obj_align(left_led, LV_ALIGN_LEFT_MID, 20, 0);
    lv_led_off(left_led);
    lv_led_set_color(left_led, lv_palette_main(LV_PALETTE_GREEN));

    center_led = lv_led_create(led_row);
    lv_obj_set_size(center_led, 18, 18);
    lv_obj_align(center_led, LV_ALIGN_CENTER, 0, 0);
    lv_led_off(center_led);
    lv_led_set_color(center_led, lv_palette_main(LV_PALETTE_GREEN));

    right_led = lv_led_create(led_row);
    lv_obj_set_size(right_led, 18, 18);
    lv_obj_align(right_led, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_led_off(right_led);
    lv_led_set_color(right_led, lv_palette_main(LV_PALETTE_GREEN));

    hw_set_trackball_callback(update_pos);
    hw_set_button_callback(button_callback);
}

void ui_trackball_exit(lv_obj_t *parent)
{

}

app_t ui_trackball_main = {
    .setup_func_cb = ui_trackball_enter,
    .exit_func_cb = ui_trackball_exit,
    .user_data = nullptr,
};

#endif /* !EXCLUDE_TRACKBALL && !ARDUINO_T_DECK */
