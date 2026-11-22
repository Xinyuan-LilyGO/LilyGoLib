/**
 * @file      LV_Helper.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 * Declares LVGL display, input, capture, and rotary diagnostic helpers.
 */

#pragma once

#include "display/LilyGoDispInterface.h"
#include <Arduino.h>
#include <lvgl.h>
#include "LVGL_Compat.h"

#if LV_USE_FS_POSIX != 1 || LV_FS_POSIX_LETTER != 'A'
#warning "Lvgl fs mismatch, may not be able to use fs function"
#endif

void beginLvglHelper(LilyGo_Display &display, bool debug = false);
void updateLvglHelper();

/**
 * @brief Snapshot of rotary encoder events delivered to LVGL.
 *
 * Positive and negative counts follow LVGL's logical encoder direction. Some
 * boards intentionally invert that direction to match their user interface;
 * applications that display physical clockwise/counter-clockwise movement
 * should apply the board-specific mapping.
 */
typedef struct {
    int32_t position;
    uint32_t positive_steps;
    uint32_t negative_steps;
    uint32_t event_count;
    uint32_t multi_step_events;
    uint32_t direction_changes;
    uint32_t rapid_reverse_events;
    uint32_t button_clicks;
    uint32_t last_event_ms;
    uint32_t last_interval_ms;
    uint16_t max_abs_diff;
    int16_t last_diff;
    bool button_pressed;
} lv_rotary_diagnostics_t;

/** Reset the rotary event counters mirrored by the LVGL input driver. */
void lv_reset_rotary_diagnostics();

/**
 * @brief Copy the current rotary event counters.
 * @return True when a rotary input device is registered.
 */
bool lv_get_rotary_diagnostics(lv_rotary_diagnostics_t *snapshot);

/**
 * @brief Consume a pending rotary encoder movement.
 *
 * The flag is set for either encoder direction when the LVGL input driver
 * receives a non-zero rotary delta.
 * @return true when at least one rotary movement was pending.
 */
bool lv_consume_encoder_activity();

/** Clear queued encoder messages and optionally suppress encoder delivery. */
void lv_set_encoder_input_suppressed(bool suppressed);

#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
typedef struct {
    const uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    bool rgb565_swapped;
} lv_screen_capture_t;

/**
 * @brief Get the latest complete screen image mirrored by the LVGL flush driver.
 *
 * The returned pixel buffer remains owned by LV_Helper and must only be read from
 * the same task that runs lv_timer_handler().
 */
bool lv_get_screen_capture(lv_screen_capture_t *capture);
#endif

void lv_set_default_group(lv_group_t *group);
lv_indev_t *lv_get_touch_indev();
lv_indev_t *lv_get_keyboard_indev();
lv_indev_t *lv_get_encoder_indev(); 
