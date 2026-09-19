/**
 * @file      ble_hid_helper.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-08
 * @brief     Board-independent BLE keyboard and mouse HID transport.
 */
#include "ble_hid_helper.h"
#include "hal_interface.h"

#if defined(ARDUINO) && !defined(EXCLUDE_BLE_HID)

#include <Arduino.h>
#include <LilyGoLog.h>
#include <NimBLEAdvertisementData.h>
#include <NimBLEAdvertising.h>
#include <NimBLECharacteristic.h>
#include <NimBLEConnInfo.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <vector>

namespace ble_hid_helper
{
namespace
{
constexpr uint16_t BLE_HID_APPEARANCE = 0x03C0;
constexpr uint8_t KEYBOARD_REPORT_ID = 1;
constexpr uint8_t MOUSE_REPORT_ID = 2;
constexpr uint32_t KEYBOARD_REPORT_DELAY_MS = 7;
constexpr uint32_t MOUSE_CLICK_REPORT_DELAY_MS = 7;

volatile bool connected = false;
volatile bool active = false;
volatile bool appActive = false;
volatile bool starting = false;
volatile bool startFailed = false;
volatile bool startTimedOut = false;
uint32_t startMs = 0;
uint32_t startTimeoutMs = 0;
TaskHandle_t startTaskHandle = nullptr;
char advertisedName[32] = "LilyGo HID";

void logHeap(const char *stage)
{
    LILYGO_LOG_PRINTF("BLEHID %s heap=%u internal=%u psram=%u\n",
                      stage,
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        connected = true;
        server->updateConnParams(connInfo.getConnHandle(), 24, 40, 0, 200);
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override
    {
        connected = false;
        if (active && appActive) {
            NimBLEDevice::startAdvertising();
        }
    }
};

class CompositeHid
{
public:
    bool begin(const char *deviceName)
    {
        if (started) {
            return true;
        }

        logHeap("begin");
        if (!NimBLEDevice::isInitialized()) {
            LILYGO_LOG_PRINTF("BLEHID init begin name=%s\n", deviceName);
            if (!NimBLEDevice::init(deviceName)) {
                LILYGO_LOG_PRINTF("BLEHID init failed\n");
                return false;
            }
            LILYGO_LOG_PRINTF("BLEHID init ok\n");
        } else {
            LILYGO_LOG_PRINTF("BLEHID reuse existing NimBLE stack\n");
        }

        connected = false;
        active = true;
        server = NimBLEDevice::createServer();
        if (!server) {
            LILYGO_LOG_PRINTF("BLEHID create server failed\n");
            active = false;
            return false;
        }
        server->setCallbacks(&callbacks, false);
        server->advertiseOnDisconnect(false);

        if (!hid) {
            hid = new NimBLEHIDDevice(server);
            if (!hid) {
                LILYGO_LOG_PRINTF("BLEHID create HID failed\n");
                active = false;
                return false;
            }
            keyboardInput = hid->getInputReport(KEYBOARD_REPORT_ID);
            keyboardOutput = hid->getOutputReport(KEYBOARD_REPORT_ID);
            mouseInput = hid->getInputReport(MOUSE_REPORT_ID);
            hid->setManufacturer("LilyGo");
            hid->setPnp(0x02, 0xe502, 0xa112, 0x0210);
            hid->setHidInfo(0x00, 0x02);
            hid->setReportMap(const_cast<uint8_t *>(REPORT_MAP), REPORT_MAP_LENGTH);
            hid->setBatteryLevel(100);
            if (keyboardOutput) {
                uint8_t leds = 0;
                keyboardOutput->setValue(&leds, 1);
            }
        } else {
            if (!keyboardInput) keyboardInput = hid->getInputReport(KEYBOARD_REPORT_ID);
            if (!keyboardOutput) keyboardOutput = hid->getOutputReport(KEYBOARD_REPORT_ID);
            if (!mouseInput) mouseInput = hid->getInputReport(MOUSE_REPORT_ID);
        }

        if (!server->start()) {
            LILYGO_LOG_PRINTF("BLEHID server start failed\n");
            active = false;
            return false;
        }

        started = startAdvertising(deviceName);
        if (!started) {
            active = false;
        }
        return started;
    }

    void end()
    {
        active = false;
        releaseKeyboard();
        connected = false;
        if (NimBLEDevice::isInitialized()) {
            NimBLEDevice::stopAdvertising();
            if (server) {
                server->advertiseOnDisconnect(false);
                server->setCallbacks(nullptr, false);
                const std::vector<uint16_t> peers = server->getPeerDevices();
                for (uint16_t connectionHandle : peers) {
                    server->disconnect(connectionHandle);
                }
            }
        }
        started = false;
        mouseButtons = 0;
    }

    bool isStarted() const
    {
        return started;
    }

    bool disconnectAndForget()
    {
        if (!server || !NimBLEDevice::isInitialized()) {
            return false;
        }

        releaseKeyboard();
        const std::vector<uint16_t> peers = server->getPeerDevices();
        bool success = NimBLEDevice::deleteAllBonds();
        for (uint16_t connectionHandle : peers) {
            if (!server->disconnect(connectionHandle)) {
                success = false;
            }
        }
        connected = false;

        if (started && active && appActive) {
            NimBLEDevice::startAdvertising();
        }
        LILYGO_LOG_PRINTF("BLEHID disconnect and forget peers=%u result=%d\n",
                          static_cast<unsigned>(peers.size()), success ? 1 : 0);
        return success;
    }

    void moveMouse(int8_t x, int8_t y, int8_t wheel, int8_t horizontalWheel)
    {
        sendMouse(mouseButtons, x, y, wheel, horizontalWheel);
    }

    void clickMouse(uint8_t buttonMask)
    {
        if (!connected) {
            return;
        }
        sendMouse(mouseButtons | buttonMask, 0, 0, 0, 0);
        delay(MOUSE_CLICK_REPORT_DELAY_MS);
        sendMouse(mouseButtons, 0, 0, 0, 0);
        delay(MOUSE_CLICK_REPORT_DELAY_MS);
    }

    void typeChar(char character)
    {
        uint8_t modifier = 0;
        uint8_t key = 0;
        if (!asciiToHid(character, modifier, key)) {
            return;
        }
        sendKey(modifier, key);
        releaseKeyboard();
    }

private:
    bool startAdvertising(const char *deviceName)
    {
        if (!server) {
            return false;
        }

        NimBLEDevice::setSecurityAuth(false, false, false);
        NimBLEAdvertising *advertising = server->getAdvertising();
        if (!advertising) {
            LILYGO_LOG_PRINTF("BLEHID advertising object missing\n");
            return false;
        }
        advertising->stop();
        advertising->reset();
        advertising->clearData();
        advertising->setPreferredParams(32, 80);

        NimBLEAdvertisementData advertisingData;
        advertisingData.setFlags(BLE_HS_ADV_F_BREDR_UNSUP | BLE_HS_ADV_F_DISC_GEN);
        advertisingData.setName(deviceName, true);
        advertisingData.addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
        advertisingData.setAppearance(BLE_HID_APPEARANCE);

        NimBLEAdvertisementData scanData;
        scanData.setManufacturerData("LilyGo");
        advertising->setAdvertisementData(advertisingData);
        advertising->setScanResponseData(scanData);
        advertising->enableScanResponse(true);
        advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);

        if (!advertising->start()) {
            LILYGO_LOG_PRINTF("BLEHID advertising start failed\n");
            return false;
        }
        LILYGO_LOG_PRINTF("BLEHID advertising started\n");
        return true;
    }

    void sendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t horizontalWheel)
    {
        if (!mouseInput || !connected) {
            return;
        }
        const uint8_t report[5] = {
            buttons,
            static_cast<uint8_t>(x),
            static_cast<uint8_t>(y),
            static_cast<uint8_t>(wheel),
            static_cast<uint8_t>(horizontalWheel),
        };
        mouseInput->setValue(report, sizeof(report));
        mouseInput->notify();
    }

    void sendKey(uint8_t modifier, uint8_t key)
    {
        if (!keyboardInput || !connected) {
            return;
        }
        const uint8_t report[8] = {modifier, 0, key, 0, 0, 0, 0, 0};
        keyboardInput->setValue(report, sizeof(report));
        keyboardInput->notify();
        delay(KEYBOARD_REPORT_DELAY_MS);
    }

    void releaseKeyboard()
    {
        if (!keyboardInput || !connected) {
            return;
        }
        const uint8_t report[8] = {0};
        keyboardInput->setValue(report, sizeof(report));
        keyboardInput->notify();
        delay(KEYBOARD_REPORT_DELAY_MS);
    }

    static bool asciiToHid(char character, uint8_t &modifier, uint8_t &key)
    {
        modifier = 0;
        key = 0;
        if (character >= 'a' && character <= 'z') {
            key = 0x04 + static_cast<uint8_t>(character - 'a');
            return true;
        }
        if (character >= 'A' && character <= 'Z') {
            modifier = 0x02;
            key = 0x04 + static_cast<uint8_t>(character - 'A');
            return true;
        }
        if (character >= '1' && character <= '9') {
            key = 0x1E + static_cast<uint8_t>(character - '1');
            return true;
        }

        switch (character) {
        case '0': key = 0x27; return true;
        case '\n':
        case '\r': key = 0x28; return true;
        case '\b': key = 0x2A; return true;
        case '\t': key = 0x2B; return true;
        case ' ': key = 0x2C; return true;
        case '-': key = 0x2D; return true;
        case '_': modifier = 0x02; key = 0x2D; return true;
        case '=': key = 0x2E; return true;
        case '+': modifier = 0x02; key = 0x2E; return true;
        case '[': key = 0x2F; return true;
        case '{': modifier = 0x02; key = 0x2F; return true;
        case ']': key = 0x30; return true;
        case '}': modifier = 0x02; key = 0x30; return true;
        case '\\': key = 0x31; return true;
        case '|': modifier = 0x02; key = 0x31; return true;
        case ';': key = 0x33; return true;
        case ':': modifier = 0x02; key = 0x33; return true;
        case '\'': key = 0x34; return true;
        case '"': modifier = 0x02; key = 0x34; return true;
        case '`': key = 0x35; return true;
        case '~': modifier = 0x02; key = 0x35; return true;
        case ',': key = 0x36; return true;
        case '<': modifier = 0x02; key = 0x36; return true;
        case '.': key = 0x37; return true;
        case '>': modifier = 0x02; key = 0x37; return true;
        case '/': key = 0x38; return true;
        case '?': modifier = 0x02; key = 0x38; return true;
        case '!': modifier = 0x02; key = 0x1E; return true;
        case '@': modifier = 0x02; key = 0x1F; return true;
        case '#': modifier = 0x02; key = 0x20; return true;
        case '$': modifier = 0x02; key = 0x21; return true;
        case '%': modifier = 0x02; key = 0x22; return true;
        case '^': modifier = 0x02; key = 0x23; return true;
        case '&': modifier = 0x02; key = 0x24; return true;
        case '*': modifier = 0x02; key = 0x25; return true;
        case '(': modifier = 0x02; key = 0x26; return true;
        case ')': modifier = 0x02; key = 0x27; return true;
        default: return false;
        }
    }

    static const uint8_t REPORT_MAP[];
    static const uint16_t REPORT_MAP_LENGTH;
    static ServerCallbacks callbacks;
    NimBLEServer *server = nullptr;
    NimBLEHIDDevice *hid = nullptr;
    NimBLECharacteristic *keyboardInput = nullptr;
    NimBLECharacteristic *keyboardOutput = nullptr;
    NimBLECharacteristic *mouseInput = nullptr;
    bool started = false;
    uint8_t mouseButtons = 0;
};

const uint8_t CompositeHid::REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x05,
    0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,
    0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x05, 0x81, 0x02, 0x75, 0x03, 0x95, 0x01,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38,
    0x02, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95,
    0x01, 0x81, 0x06, 0xC0, 0xC0,
};

const uint16_t CompositeHid::REPORT_MAP_LENGTH = sizeof(CompositeHid::REPORT_MAP);

ServerCallbacks CompositeHid::callbacks;
CompositeHid compositeHid;

void startTask(void *)
{
    LILYGO_LOG_PRINTF("BLEHID start task\n");
    logHeap("task");
    const bool startedOk = compositeHid.begin(advertisedName);
    if (!appActive) {
        compositeHid.end();
    }
    startFailed = !startedOk;
    startTimedOut = false;
    starting = false;
    startTaskHandle = nullptr;
    LILYGO_LOG_PRINTF("BLEHID start task done ok=%d\n", startedOk ? 1 : 0);
    vTaskDelete(nullptr);
}

bool waitForStartTask(uint32_t timeoutMs)
{
    const uint32_t waitStart = millis();
    while (startTaskHandle && millis() - waitStart < timeoutMs) {
        delay(10);
    }
    return startTaskHandle == nullptr;
}
} // namespace

bool startAsync(const char *deviceName, uint32_t timeoutMs)
{
    if (startTaskHandle) {
        startFailed = true;
        startTimedOut = true;
        starting = false;
        return false;
    }

    snprintf(advertisedName, sizeof(advertisedName), "%s",
             deviceName && deviceName[0] ? deviceName : "LilyGo HID");
    appActive = true;
    startFailed = false;
    startTimedOut = false;
    starting = true;
    startMs = millis();
    startTimeoutMs = timeoutMs;
    const BaseType_t result = xTaskCreatePinnedToCore(
                                  startTask, "bleHid", 6144, nullptr,
                                  tskIDLE_PRIORITY + 1, &startTaskHandle, 0);
    if (result != pdPASS) {
        startTaskHandle = nullptr;
        starting = false;
        startFailed = true;
        return false;
    }
    return true;
}

void poll()
{
    if (starting && startTimeoutMs && millis() - startMs > startTimeoutMs) {
        startTimedOut = true;
        starting = false;
        LILYGO_LOG_PRINTF("BLEHID start timeout after %lu ms\n",
                          static_cast<unsigned long>(millis() - startMs));
    }
}

void stop(uint32_t waitMs)
{
    appActive = false;
    if (!startTaskHandle || waitForStartTask(waitMs)) {
        compositeHid.end();
    }
    starting = false;
}

State state()
{
    if (startTimedOut) return State::TIMEOUT;
    if (startFailed) return State::FAILED;
    if (starting) return State::STARTING;
    if (connected) return State::CONNECTED;
    if (compositeHid.isStarted()) return State::ADVERTISING;
    return State::STOPPED;
}

const char *stateText()
{
    switch (state()) {
    case State::STARTING: return "Starting";
    case State::ADVERTISING: return "Advertising";
    case State::CONNECTED: return "Connected";
    case State::FAILED: return "BLE Failed";
    case State::TIMEOUT: return "BLE Timeout";
    case State::UNAVAILABLE: return "N.A";
    default: return "Stopped";
    }
}

bool isStarted()
{
    return compositeHid.isStarted();
}

bool isConnected()
{
    return connected;
}

bool disconnectAndForget()
{
    return compositeHid.disconnectAndForget();
}

void moveMouse(int8_t x, int8_t y, int8_t wheel, int8_t horizontalWheel)
{
    compositeHid.moveMouse(x, y, wheel, horizontalWheel);
}

void clickMouse(uint8_t buttonMask)
{
    compositeHid.clickMouse(buttonMask);
}

void typeChar(char character)
{
    compositeHid.typeChar(character);
}

} // namespace ble_hid_helper

#else

namespace ble_hid_helper
{
bool startAsync(const char *, uint32_t) { return false; }
void poll() {}
void stop(uint32_t) {}
State state() { return State::UNAVAILABLE; }
const char *stateText() { return "N.A"; }
bool isStarted() { return false; }
bool isConnected() { return false; }
bool disconnectAndForget() { return false; }
void moveMouse(int8_t, int8_t, int8_t, int8_t) {}
void clickMouse(uint8_t) {}
void typeChar(char) {}
} // namespace ble_hid_helper

#endif
