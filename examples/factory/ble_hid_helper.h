/**
 * @file      ble_hid_helper.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-08
 * @brief     Board-independent BLE keyboard and mouse HID transport.
 */
#pragma once

#include <stdint.h>

namespace ble_hid_helper
{

enum class State : uint8_t {
    STOPPED,
    STARTING,
    ADVERTISING,
    CONNECTED,
    FAILED,
    TIMEOUT,
    UNAVAILABLE,
};

bool startAsync(const char *deviceName, uint32_t timeoutMs);
void poll();
void stop(uint32_t waitMs = 1500);

State state();
const char *stateText();
bool isStarted();
bool isConnected();
bool disconnectAndForget();

void moveMouse(int8_t x, int8_t y, int8_t wheel = 0, int8_t horizontalWheel = 0);
void clickMouse(uint8_t buttonMask);
void typeChar(char character);

} // namespace ble_hid_helper
