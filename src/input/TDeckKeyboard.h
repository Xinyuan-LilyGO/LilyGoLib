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
#include "LilyGoKeyboardConfig.h"

class TDeckKeyboard
{
public:
    using KeyboardReadCallback = void (*)(int state, char &key);
    using KeyboardRawCallback = void (*)(bool pressed, uint8_t key);

    static constexpr uint8_t DefaultAddress = 0x55;
    /** Raw event code used for the dedicated microphone key. */
    static constexpr uint8_t MicrophoneKeyCode = 0xF0;

    bool begin(TwoWire &wire, uint8_t address = DefaultAddress);
    void end();

    int getKey(char *key);
    bool setBrightness(uint8_t level);
    bool setDefaultBrightness(uint8_t level);
    bool setRawMode(bool enabled);
    bool setLayout(KeyboardLayoutType layout);
    KeyboardLayoutType getLayout() const;
    uint8_t getBrightness() const;
    void setCallback(KeyboardReadCallback callback);
    void setRawCallback(KeyboardRawCallback callback);
    bool isOnline() const;

private:
    static constexpr uint8_t RawColumnCount = 5;
    static constexpr uint8_t RawRowCount = 7;
    static constexpr uint8_t PendingEventCount = 40;

    struct PendingEvent {
        bool pressed;
        char key;
    };

    void recordTransferResult(bool success);
    bool sendCommand(uint8_t command);
    bool sendCommand(uint8_t command, uint8_t value);
    int getKeyMode(char *key);
    int getRawMode(char *key);
    void pollRawMatrix();
    void queueRawEvent(bool pressed, char key);
    bool popRawEvent(PendingEvent &event);
    char mapRawKey(uint8_t column, uint8_t row) const;
    char mapSymbolKey(char key) const;
    uint8_t translateKey(uint8_t key) const;

    TwoWire *wire = nullptr;
    uint8_t address = DefaultAddress;
    uint8_t consecutiveErrors = 0;
    uint8_t brightness = 0;
    uint8_t previousKey = 0;
    uint8_t previousMatrix[RawColumnCount] = {};
    uint8_t rawSample[RawColumnCount] = {};
    char activeMatrixKeys[RawColumnCount][RawRowCount] = {};
    PendingEvent pendingEvents[PendingEventCount] = {};
    uint8_t pendingEventHead = 0;
    uint8_t pendingEventTail = 0;
    bool rawMatrixInitialized = false;
    bool rawSampleInitialized = false;
    uint8_t rawSampleCount = 0;
    bool rawMode = false;
    bool symbolKeyPressed = false;
    bool shiftKeyPressed = false;
    KeyboardLayoutType keyboardLayout = KeyboardLayoutType::QWERTY;
    bool online = false;
    KeyboardReadCallback callback = nullptr;
    KeyboardRawCallback rawCallback = nullptr;

};

#endif // ARDUINO_T_DECK
