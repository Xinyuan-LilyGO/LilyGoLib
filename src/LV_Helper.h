/**
 * @file      LV_Helper.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
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
