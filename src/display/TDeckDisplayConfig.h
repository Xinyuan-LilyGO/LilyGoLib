/**
 * @file      TDeckDisplayConfig.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-28
 * @brief     Shared ST7789 and GT911 configuration for T-Deck generations.
 */
#pragma once

#include "LilyGoDispInterface.h"

namespace lilygo {
namespace tdeck_display {

static const CommandTable_t st7789InitList[] = {
    {0x11, {0},  0x80},
    {0x13, {0},  0},
    {0x36, {0x00}, 1},
    {0x3A, {0x55}, 1},
    {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x1A}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x13}, 1},
    {0xC4, {0x20}, 1},
    {0xC6, {0x0F}, 1},
    {0xE0, {0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}, 14},
    {0xE1, {0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x3E}, 14},
    {0x21, {0}, 0},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4 | 0x80},
    {0x29, {0}, 0x80},
};

static const DispRotationConfig_t rotationConfig[4] = {
    {0x60, DISP_HEIGHT, DISP_WIDTH, 0, 0},
    {0x00, DISP_WIDTH, DISP_HEIGHT, 0, 0},
    {0xA0, DISP_HEIGHT, DISP_WIDTH, 0, 0},
    {0xC0, DISP_WIDTH, DISP_HEIGHT, 0, 0},
};

static constexpr int16_t touchWidth = 320;
static constexpr int16_t touchHeight = 240;
static constexpr bool touchSwapXY = true;
static constexpr bool touchMirrorX = false;
static constexpr bool touchMirrorY = true;

template <typename Touch>
inline void configureTouch(Touch &touch)
{
    touch.setMaxCoordinates(touchWidth, touchHeight);
    touch.setSwapXY(touchSwapXY);
    touch.setMirrorXY(touchMirrorX, touchMirrorY);
}

} // namespace tdeck_display
} // namespace lilygo
