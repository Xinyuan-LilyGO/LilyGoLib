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
#include "LilyGoLog.h"

// Enable verbose matrix and key event logs with
// -DLILYGO_TDECK_KEYBOARD_DEBUG=1 when diagnosing the C3 keyboard.
#ifndef LILYGO_TDECK_KEYBOARD_DEBUG
#define LILYGO_TDECK_KEYBOARD_DEBUG 0
#endif

namespace {
constexpr uint8_t CommandBrightness = 0x01;
constexpr uint8_t CommandDefaultBrightness = 0x02;
constexpr uint8_t CommandRawMode = 0x03;
constexpr uint8_t CommandKeyMode = 0x04;

// Matrix values from the ESP32-C3 keyboard firmware.  The firmware's key
// mode turns several of these positions into control characters, so the
// driver uses the matrix coordinates in raw mode and performs translation
// here instead.
constexpr char KeyMap[5][7] = {
    {'q', 'w', '\0', 'a', '\0', ' ', '\0'},
    {'e', 's', 'd', 'p', 'x', 'z', '\0'},
    {'r', 'g', 't', '\0', 'v', 'c', 'f'},
    {'u', 'h', 'y', '\n', 'b', 'n', 'j'},
    {'o', 'l', 'i', '\b', '$', 'm', 'k'},
};

// Symbol values used by the original keyboard firmware in QWERTY mode.
constexpr char SymbolMap[5][7] = {
    {'#', '*', '\0', '*', '\0', '\0', '0'},
    {'2', '4', '5', '@', '8', '7', '\0'},
    {'3', '/', '(', '\0', '?', '9', '6'},
    {'_', ':', ')', '\0', '!', ',', ';'},
    {'+', '"', '-', '\0', '\0', '.', '\''},
};
}

bool TDeckKeyboard::begin(TwoWire &keyboardWire, uint8_t keyboardAddress)
{
    wire = &keyboardWire;
    address = keyboardAddress;
    consecutiveErrors = 0;
    previousKey = 0;
    memset(previousMatrix, 0, sizeof(previousMatrix));
    memset(rawSample, 0, sizeof(rawSample));
    memset(activeMatrixKeys, 0, sizeof(activeMatrixKeys));
    pendingEventHead = 0;
    pendingEventTail = 0;
    rawMatrixInitialized = false;
    rawSampleInitialized = false;
    rawSampleCount = 0;
    rawMode = false;
    symbolKeyPressed = false;
    shiftKeyPressed = false;
    keyboardLayout = KeyboardLayoutType::QWERTY;
    wire->beginTransmission(address);
    online = wire->endTransmission() == 0;
    if (online) {
        if (!setRawMode(true)) {
            LILYGO_LOG_W("T-Deck keyboard failed to enter raw mode");
        }
    }
    return online;
}

void TDeckKeyboard::end()
{
    previousKey = 0;
    rawMatrixInitialized = false;
    rawSampleInitialized = false;
    rawSampleCount = 0;
    pendingEventHead = 0;
    pendingEventTail = 0;
    rawMode = false;
    online = false;
    wire = nullptr;
}

int TDeckKeyboard::getKey(char *key)
{
    return rawMode ? getRawMode(key) : getKeyMode(key);
}

int TDeckKeyboard::getKeyMode(char *key)
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
    const uint8_t rawKey = wire->read();
    const uint8_t currentKey = translateKey(rawKey);
    const bool changed = currentKey != previousKey;
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
    *key = (changed && currentKey != 0) ? static_cast<char>(currentKey) : '\0';
    return (changed && currentKey != 0) ? 1 : 0;
}

int TDeckKeyboard::getRawMode(char *key)
{
    PendingEvent event = {};
    if (!popRawEvent(event)) {
        pollRawMatrix();
        if (!popRawEvent(event)) {
            if (key) {
                *key = '\0';
            }
            return -1;
        }
    }

    if (rawCallback) {
        // The public callback receives the translated character.  Matrix
        // decoding remains an implementation detail of this driver.
        rawCallback(event.pressed, static_cast<uint8_t>(event.key));
    }
    if (callback && event.key != '\0' &&
            static_cast<uint8_t>(event.key) != MicrophoneKeyCode) {
        char translated = event.key;
        callback(event.pressed ? 1 : 0, translated);
    }
    if (key) {
        // getKey() is the character input API: only presses produce a
        // character.  Releases remain available through the callbacks.
        *key = event.pressed &&
               static_cast<uint8_t>(event.key) != MicrophoneKeyCode ? event.key : '\0';
    }
    return event.pressed && event.key != '\0' &&
           static_cast<uint8_t>(event.key) != MicrophoneKeyCode ? 1 : 0;
}

void TDeckKeyboard::queueRawEvent(bool pressed, char key)
{
    const uint8_t next = static_cast<uint8_t>((pendingEventTail + 1) % PendingEventCount);
    if (next == pendingEventHead) {
        return;
    }
    pendingEvents[pendingEventTail] = {pressed, key};
    pendingEventTail = next;
}

bool TDeckKeyboard::popRawEvent(PendingEvent &event)
{
    if (pendingEventHead == pendingEventTail) {
        return false;
    }
    event = pendingEvents[pendingEventHead];
    pendingEventHead = static_cast<uint8_t>((pendingEventHead + 1) % PendingEventCount);
    return true;
}

void TDeckKeyboard::pollRawMatrix()
{
    if (!wire || !online) {
        return;
    }

    uint8_t current[RawColumnCount] = {};
    const size_t received = wire->requestFrom(address, RawColumnCount);
    if (received != RawColumnCount) {
        while (wire->available()) {
            wire->read();
        }
        recordTransferResult(false);
        return;
    }

    for (uint8_t column = 0; column < RawColumnCount; column++) {
        if (wire->available()) {
            current[column] = static_cast<uint8_t>(wire->read());
        }
    }
    recordTransferResult(true);

    // The C3 scans its five columns sequentially.  A transaction can overlap
    // that scan, so require two identical samples before accepting a matrix
    // as stable.  This prevents a partial scan from becoming key events.
    if (!rawSampleInitialized) {
        memcpy(rawSample, current, sizeof(rawSample));
        rawSampleInitialized = true;
        rawSampleCount = 1;
        return;
    }
    bool sampleChanged = false;
    for (uint8_t column = 0; column < RawColumnCount; column++) {
        if (current[column] != rawSample[column]) {
            sampleChanged = true;
            break;
        }
    }
    if (sampleChanged) {
#if LILYGO_TDECK_KEYBOARD_DEBUG
        LILYGO_LOG_I("T-Deck keyboard sample: %02X %02X %02X %02X %02X",
                     current[0], current[1], current[2], current[3], current[4]);
#endif
        memcpy(rawSample, current, sizeof(rawSample));
        rawSampleCount = 1;
        return;
    }
    if (rawSampleCount < 2) {
        ++rawSampleCount;
        if (rawSampleCount < 2) {
            return;
        }
    }

    if (!rawMatrixInitialized) {
        rawMatrixInitialized = true;
        memset(previousMatrix, 0, sizeof(previousMatrix));
    }

    bool matrixChanged = false;
    for (uint8_t column = 0; column < RawColumnCount; column++) {
        if (current[column] != previousMatrix[column]) {
            matrixChanged = true;
            break;
        }
    }
    if (matrixChanged) {
#if LILYGO_TDECK_KEYBOARD_DEBUG
        LILYGO_LOG_I("T-Deck keyboard raw: %02X %02X %02X %02X %02X (previous %02X %02X %02X %02X %02X)",
                     current[0], current[1], current[2], current[3], current[4],
                     previousMatrix[0], previousMatrix[1], previousMatrix[2],
                     previousMatrix[3], previousMatrix[4]);
#endif
    }

    for (uint8_t column = 0; column < RawColumnCount; column++) {
        const uint8_t changed = static_cast<uint8_t>(current[column] ^ previousMatrix[column]);
        for (uint8_t row = 0; row < RawRowCount; row++) {
            const uint8_t mask = static_cast<uint8_t>(1u << row);
            if ((changed & mask) == 0) {
                continue;
            }

            const bool pressed = (current[column] & mask) != 0;
            char translated = '\0';
            if (pressed) {
                if (column == 0 && row == 2) {
                    symbolKeyPressed = true;
                }
                if ((column == 1 && row == 6) || (column == 2 && row == 3)) {
                    shiftKeyPressed = true;
                }
                translated = mapRawKey(column, row);
                activeMatrixKeys[column][row] = translated;
            } else {
                translated = activeMatrixKeys[column][row];
                activeMatrixKeys[column][row] = '\0';
                if (column == 0 && row == 2) {
                    symbolKeyPressed = false;
                }
                if ((column == 1 && row == 6) || (column == 2 && row == 3)) {
                    shiftKeyPressed = false;
                }
            }
            queueRawEvent(pressed, translated);
#if LILYGO_TDECK_KEYBOARD_DEBUG
            LILYGO_LOG_I("T-Deck keyboard %s: column=%u row=%u raw=0x%02X key=0x%02X",
                         pressed ? "pressed" : "released",
                         column, row, current[column],
                         static_cast<uint8_t>(translated));
#endif
        }
        previousMatrix[column] = current[column];
    }
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
    if (!sendCommand(enabled ? CommandRawMode : CommandKeyMode)) {
        return false;
    }
    rawMode = enabled;
#if LILYGO_TDECK_KEYBOARD_DEBUG
    LILYGO_LOG_I("T-Deck keyboard mode: %s", enabled ? "raw" : "key");
#endif
    return true;
}

bool TDeckKeyboard::setLayout(KeyboardLayoutType layout)
{
    keyboardLayout = layout == KeyboardLayoutType::AZERTY ?
                      KeyboardLayoutType::AZERTY : KeyboardLayoutType::QWERTY;
    return true;
}

KeyboardLayoutType TDeckKeyboard::getLayout() const
{
    return keyboardLayout;
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

char TDeckKeyboard::mapSymbolKey(char key) const
{
    switch (key) {
    case 'q': return '#';
    case 'w': return '1';
    case 'e': return '2';
    case 'r': return '3';
    case 't': return '(';
    case 'y': return ')';
    case 'u': return '_';
    case 'i': return '-';
    case 'o': return '+';
    case 'p': return '@';
    case 'a': return '*';
    case 's': return '4';
    case 'd': return '5';
    case 'f': return '6';
    case 'g': return '/';
    case 'h': return ':';
    case 'j': return ';';
    case 'k': return '\'';
    case 'l': return '"';
    case 'z': return '7';
    case 'x': return '8';
    case 'c': return '9';
    case 'v': return '?';
    case 'b': return '!';
    case 'n': return ',';
    case '$': return '`';
    default: return '\0';
    }
}

char TDeckKeyboard::mapRawKey(uint8_t column, uint8_t row) const
{
    if (column >= RawColumnCount || row >= RawRowCount) {
        return '\0';
    }

    const char base = KeyMap[column][row];
    if (column == 0 && row == 6) {
        if (symbolKeyPressed) {
            return '0';
        }
        return static_cast<char>(MicrophoneKeyCode);
    }
    if (symbolKeyPressed) {
        if (keyboardLayout == KeyboardLayoutType::AZERTY) {
            return mapSymbolKey(base);
        }
        return SymbolMap[column][row];
    }

    char key = base;
    if (keyboardLayout == KeyboardLayoutType::AZERTY) {
        // These coordinates are the AZERTY keycaps on the device. The C3
        // firmware's QWERTY table labels them as Backspace, M, and $, so
        // translate by the physical AZERTY legends instead.
        if (column == 4 && row == 3) return 'm';
        if (column == 4 && row == 5) return '$';
        if (column == 4 && row == 4) return '\b';

        switch (key) {
        case 'q': key = 'a'; break;
        case 'a': key = 'q'; break;
        case 'w': key = 'z'; break;
        case 'z': key = 'w'; break;
        default: break;
        }
    }
    if (shiftKeyPressed && key >= 'a' && key <= 'z') {
        key = static_cast<char>(key - ('a' - 'A'));
    }
    return key;
}

uint8_t TDeckKeyboard::translateKey(uint8_t key) const
{
    if (keyboardLayout != KeyboardLayoutType::AZERTY) {
        return key;
    }

    switch (key) {
    case 'q': return 'a';
    case 'a': return 'q';
    case 'w': return 'z';
    case 'z': return 'w';
    case 'Q': return 'A';
    case 'A': return 'Q';
    case 'W': return 'Z';
    case 'Z': return 'W';
    case 'm':
    case 'M': return '\b';
    case '$': return 'm';
    case '\b': return '$';
    default: return key;
    }
}

#endif // ARDUINO_T_DECK
