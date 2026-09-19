 /**
 * @file      TDeckKeyboard.cpp
 * @brief     I2C adapter for the original T-Deck ESP32-C3 keyboard.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-28
 */
#ifdef ARDUINO_T_DECK

#include "TDeckKeyboard.h"

namespace {
constexpr uint8_t CommandBrightness = 0x01;
constexpr uint8_t CommandDefaultBrightness = 0x02;
constexpr uint8_t CommandRawMode = 0x03;
constexpr uint8_t CommandKeyMode = 0x04;
}

bool TDeckKeyboard::begin(TwoWire &keyboardWire, uint8_t keyboardAddress)
{
    wire = &keyboardWire;
    address = keyboardAddress;
    consecutiveErrors = 0;
    previousKey = 0;
    wire->beginTransmission(address);
    online = wire->endTransmission() == 0;
    if (online) {
        setRawMode(false);
    }
    return online;
}

void TDeckKeyboard::end()
{
    previousKey = 0;
    online = false;
    wire = nullptr;
}

int TDeckKeyboard::getKey(char *key)
{
    if (!online || !wire || !key) {
        return -1;
    }

    const size_t received = wire->requestFrom(address, static_cast<uint8_t>(1));
    if (received != 1 || !wire->available()) {
        recordTransferResult(false);
        return -1;
    }

    recordTransferResult(true);
    const uint8_t currentKey = wire->read();
    if (previousKey != 0 && currentKey != previousKey) {
        char released = static_cast<char>(previousKey);
        if (rawCallback) {
            rawCallback(false, previousKey);
        }
        if (callback) {
            callback(0, released);
        }
    }
    if (currentKey != 0 && currentKey != previousKey) {
        char pressed = static_cast<char>(currentKey);
        if (rawCallback) {
            rawCallback(true, currentKey);
        }
        if (callback) {
            callback(1, pressed);
        }
    }
    previousKey = currentKey;
    *key = static_cast<char>(currentKey);
    return currentKey == 0 ? 0 : 1;
}

bool TDeckKeyboard::setBrightness(uint8_t level)
{
    if (!sendCommand(CommandBrightness, level)) {
        return false;
    }
    brightness = level;
    return true;
}

bool TDeckKeyboard::setDefaultBrightness(uint8_t level)
{
    return sendCommand(CommandDefaultBrightness, level);
}

bool TDeckKeyboard::setRawMode(bool enabled)
{
    return sendCommand(enabled ? CommandRawMode : CommandKeyMode);
}

uint8_t TDeckKeyboard::getBrightness() const
{
    return brightness;
}

void TDeckKeyboard::setCallback(KeyboardReadCallback newCallback)
{
    callback = newCallback;
}

void TDeckKeyboard::setRawCallback(KeyboardRawCallback newCallback)
{
    rawCallback = newCallback;
}

bool TDeckKeyboard::isOnline() const
{
    return online;
}

void TDeckKeyboard::recordTransferResult(bool success)
{
    if (success) {
        consecutiveErrors = 0;
        return;
    }
    if (consecutiveErrors < UINT8_MAX) {
        ++consecutiveErrors;
    }
    if (consecutiveErrors >= 10) {
        online = false;
    }
}

bool TDeckKeyboard::sendCommand(uint8_t command)
{
    if (!online || !wire) {
        return false;
    }
    wire->beginTransmission(address);
    wire->write(command);
    const bool success = wire->endTransmission() == 0;
    recordTransferResult(success);
    return success;
}

bool TDeckKeyboard::sendCommand(uint8_t command, uint8_t value)
{
    if (!online || !wire) {
        return false;
    }
    wire->beginTransmission(address);
    wire->write(command);
    wire->write(value);
    const bool success = wire->endTransmission() == 0;
    recordTransferResult(success);
    return success;
}

#endif // ARDUINO_T_DECK
