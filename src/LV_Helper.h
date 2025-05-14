/**
 * @file      LV_Helper.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 */

#pragma once

#include "LilyGoDispInterface.h"
#include <Arduino.h>
#include <lvgl.h>

#if LV_USE_FS_POSIX != 1 || LV_FS_POSIX_LETTER != 'A'
#warning "Lvgl fs mismatch, may not be able to use fs function"
#endif

void beginLvglHelper(LilyGo_Display &display, bool debug = false);
void updateLvglHelper();

void lv_set_default_group(lv_group_t *group);
