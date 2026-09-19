#pragma once

#include <NimBLECharacteristic.h>
#include <NimBLEHIDDevice.h>

#include <stdint.h>
#include <string>

#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2
#define MOUSE_MIDDLE 4
#define MOUSE_BACK 8
#define MOUSE_FORWARD 16
#define MOUSE_ALL (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE)

class NimBLEMouse {
public:
    NimBLEMouse(const std::string &deviceName = "ESP32 Bluetooth Mouse",
                const std::string &deviceManufacturer = "Espressif",
                uint8_t batteryLevel = 100);

    void begin();
    void end();
    void click(uint8_t button = MOUSE_LEFT);
    void move(signed char x, signed char y, signed char wheel = 0, signed char hWheel = 0);
    void press(uint8_t button = MOUSE_LEFT);
    void release(uint8_t button = MOUSE_LEFT);
    bool isPressed(uint8_t button = MOUSE_LEFT) const;
    bool isConnected() const;
    void setBatteryLevel(uint8_t level);

private:
    void buttons(uint8_t button);

    uint8_t buttons_;
    uint8_t batteryLevel_;
    std::string deviceName_;
    std::string deviceManufacturer_;
    NimBLEHIDDevice *hid_;
    NimBLECharacteristic *inputMouse_;
};
