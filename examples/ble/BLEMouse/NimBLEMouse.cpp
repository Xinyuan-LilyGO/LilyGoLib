#include "NimBLEMouse.h"

#include <Arduino.h>
#include <HIDTypes.h>
#include <NimBLEAdvertising.h>
#include <NimBLEAdvertisementData.h>
#include <NimBLEConnInfo.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>

namespace {

constexpr uint8_t MOUSE_REPORT_ID = 1;
bool mouseConnected = false;

const uint8_t hidReportDescriptor[] = {
    USAGE_PAGE(1),       0x01,              // Generic Desktop
    USAGE(1),            0x02,              // Mouse
    COLLECTION(1),       0x01,              // Application
    REPORT_ID(1),        MOUSE_REPORT_ID,
    USAGE(1),            0x01,              // Pointer
    COLLECTION(1),       0x00,              // Physical
    USAGE_PAGE(1),       0x09,              // Button
    USAGE_MINIMUM(1),    0x01,
    USAGE_MAXIMUM(1),    0x05,
    LOGICAL_MINIMUM(1),  0x00,
    LOGICAL_MAXIMUM(1),  0x01,
    REPORT_SIZE(1),      0x01,
    REPORT_COUNT(1),     0x05,
    HIDINPUT(1),         0x02,
    REPORT_SIZE(1),      0x03,
    REPORT_COUNT(1),     0x01,
    HIDINPUT(1),         0x03,
    USAGE_PAGE(1),       0x01,              // Generic Desktop
    USAGE(1),            0x30,              // X
    USAGE(1),            0x31,              // Y
    USAGE(1),            0x38,              // Wheel
    LOGICAL_MINIMUM(1),  0x81,
    LOGICAL_MAXIMUM(1),  0x7f,
    REPORT_SIZE(1),      0x08,
    REPORT_COUNT(1),     0x03,
    HIDINPUT(1),         0x06,
    USAGE_PAGE(1),       0x0c,              // Consumer
    USAGE(2),            0x38, 0x02,        // AC Pan
    LOGICAL_MINIMUM(1),  0x81,
    LOGICAL_MAXIMUM(1),  0x7f,
    REPORT_SIZE(1),      0x08,
    REPORT_COUNT(1),     0x01,
    HIDINPUT(1),         0x06,
    END_COLLECTION(0),
    END_COLLECTION(0),
};

class MouseServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        (void)server;
        (void)connInfo;
        mouseConnected = true;
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        (void)reason;
        mouseConnected = false;
        NimBLEDevice::startAdvertising();
    }

    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
    {
        if (!connInfo.isEncrypted()) {
            NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        }
    }
};

MouseServerCallbacks serverCallbacks;

} // namespace

NimBLEMouse::NimBLEMouse(const std::string &deviceName,
                         const std::string &deviceManufacturer,
                         uint8_t batteryLevel)
    : buttons_(0),
      batteryLevel_(batteryLevel),
      deviceName_(deviceName),
      deviceManufacturer_(deviceManufacturer),
      hid_(nullptr),
      inputMouse_(nullptr)
{
}

void NimBLEMouse::begin()
{
    if (hid_) {
        return;
    }

    NimBLEDevice::init(deviceName_);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCallbacks, false);

    hid_ = new NimBLEHIDDevice(server);
    inputMouse_ = hid_->getInputReport(MOUSE_REPORT_ID);
    hid_->setManufacturer(deviceManufacturer_);
    hid_->setPnp(0x02, 0xe502, 0xa111, 0x0210);
    hid_->setHidInfo(0x00, 0x02);
    hid_->setReportMap((uint8_t *)hidReportDescriptor, sizeof(hidReportDescriptor));
    hid_->startServices();

    NimBLEDevice::setSecurityAuth(false, false, false);

    NimBLEAdvertising *advertising = server->getAdvertising();
    advertising->reset();
    advertising->clearData();
    advertising->setPreferredParams(160, 320);

    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_BREDR_UNSUP | BLE_HS_ADV_F_DISC_GEN);
    advData.setName(deviceName_, true);
    advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812));
    advData.setAppearance(HID_MOUSE);

    NimBLEAdvertisementData scanRespData;
    if (!deviceManufacturer_.empty()) {
        scanRespData.setManufacturerData(deviceManufacturer_);
    }

    advertising->setAdvertisementData(advData);
    advertising->setScanResponseData(scanRespData);
    advertising->enableScanResponse(true);
    advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    advertising->start();

    hid_->setBatteryLevel(batteryLevel_);
}

void NimBLEMouse::end()
{
    NimBLEDevice::deinit(true);
    mouseConnected = false;
    hid_ = nullptr;
    inputMouse_ = nullptr;
}

void NimBLEMouse::click(uint8_t button)
{
    buttons_ = button;
    move(0, 0, 0, 0);
    buttons_ = 0;
    move(0, 0, 0, 0);
}

void NimBLEMouse::move(signed char x, signed char y, signed char wheel, signed char hWheel)
{
    if (!isConnected() || !inputMouse_) {
        return;
    }

    uint8_t report[5] = {
        buttons_,
        static_cast<uint8_t>(x),
        static_cast<uint8_t>(y),
        static_cast<uint8_t>(wheel),
        static_cast<uint8_t>(hWheel),
    };
    inputMouse_->setValue(report, sizeof(report));
    inputMouse_->notify();
    delay(7);
}

void NimBLEMouse::buttons(uint8_t button)
{
    if (button == buttons_) {
        return;
    }
    buttons_ = button;
    move(0, 0, 0, 0);
}

void NimBLEMouse::press(uint8_t button)
{
    buttons(buttons_ | button);
}

void NimBLEMouse::release(uint8_t button)
{
    buttons(buttons_ & ~button);
}

bool NimBLEMouse::isPressed(uint8_t button) const
{
    return (button & buttons_) != 0;
}

bool NimBLEMouse::isConnected() const
{
    return mouseConnected;
}

void NimBLEMouse::setBatteryLevel(uint8_t level)
{
    batteryLevel_ = level;
    if (hid_) {
        hid_->setBatteryLevel(batteryLevel_);
    }
}
