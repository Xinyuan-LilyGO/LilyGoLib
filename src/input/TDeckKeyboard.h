 /**
 * @file      TDeckKeyboard.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-28
 */
#pragma once

#ifdef ARDUINO_T_DECK

#include <Arduino.h>
#include <Wire.h>

class TDeckKeyboard
{
public:
    using KeyboardReadCallback = void (*)(int state, char &key);
    using KeyboardRawCallback = void (*)(bool pressed, uint8_t key);

    static constexpr uint8_t DefaultAddress = 0x55;

    bool begin(TwoWire &wire, uint8_t address = DefaultAddress);
    void end();

    int getKey(char *key);
    bool setBrightness(uint8_t level);
    bool setDefaultBrightness(uint8_t level);
    bool setRawMode(bool enabled);
    uint8_t getBrightness() const;
    void setCallback(KeyboardReadCallback callback);
    void setRawCallback(KeyboardRawCallback callback);
    bool isOnline() const;

private:
    void recordTransferResult(bool success);
    bool sendCommand(uint8_t command);
    bool sendCommand(uint8_t command, uint8_t value);

    TwoWire *wire = nullptr;
    uint8_t address = DefaultAddress;
    uint8_t consecutiveErrors = 0;
    uint8_t brightness = 0;
    uint8_t previousKey = 0;
    bool online = false;
    KeyboardReadCallback callback = nullptr;
    KeyboardRawCallback rawCallback = nullptr;
};

#endif // ARDUINO_T_DECK
