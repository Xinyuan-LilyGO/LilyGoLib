/**
 * @file      LilyGoWatch.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-05-30
 *
 */
#pragma once

#ifdef ARDUINO_TWATCH_BASE

#include <Arduino.h>
#include <FFat.h>
#include <FS.h>
#include <Wire.h>
#include <Button2.h>
#include <Adafruit_PN532.h>
#include <TouchDrvFocalTech.hpp>
#include <RtcDrv.hpp>
#include "display/LilyGoDispInterface.h"
#include "core/LilyGoEventManage.h"
#include "core/LilyGoPowerManageInf.h"
#include "core/LilyGoTypedef.h"
#include "display/BrightnessController.h"
#include "sensor/BMASensorHelper.h"
#include "audio/AudioInputIf.h"
#include "audio/AudioOutputIf.h"

// Arduino-ESP32 upstream uses the shared "twatch" variant for 2019/2020
// revisions and only exposes the common pins. Keep the board class buildable
// without requiring this repository's legacy variant copy.
#ifndef DISP_WIDTH
#define DISP_WIDTH      (240)
#endif
#ifndef DISP_HEIGHT
#define DISP_HEIGHT     (240)
#endif
#ifndef DISP_MOSI
#define DISP_MOSI       (19)
#endif
#ifndef DISP_MISO
#define DISP_MISO       (34)
#endif
#ifndef DISP_SCK
#define DISP_SCK        (18)
#endif
#ifndef DISP_RST
#define DISP_RST        (-1)
#endif
#ifndef DISP_CS
#define DISP_CS         (5)
#endif
#ifndef DISP_DC
#define DISP_DC         (27)
#endif
#ifndef DISP_BL
#define DISP_BL         (12)
#endif
#ifndef TP_RST
#define TP_RST          (-1)
#endif
#ifndef PMU_INT
#ifdef APX20X_INT
#define PMU_INT         APX20X_INT
#else
#define PMU_INT         (35)
#endif
#endif
#ifndef SENSOR_INT
#ifdef BMA42X_INT1
#define SENSOR_INT      BMA42X_INT1
#else
#define SENSOR_INT      (39)
#endif
#endif
#ifndef BUTTON_INT
#define BUTTON_INT      (36)
#endif
#ifndef USING_PMU_MANAGE
#define USING_PMU_MANAGE
#endif
#ifndef USING_INPUT_DEV_TOUCHPAD
#define USING_INPUT_DEV_TOUCHPAD
#endif
#ifndef USING_BMA423_SENSOR
#define USING_BMA423_SENSOR
#endif
#ifndef HAS_SD_CARD_SOCKET
#define HAS_SD_CARD_SOCKET
#endif

// Options PN532
#define PN532_IRQ       34
#define PN532_RST       33
#define PN532_BUZZER    13

class LilyGoWatch : public LilyGo_Display,
    public LilyGoDispSPI,
    public LilyGoEventManage,
    public LilyGoPowerManageInf,
    public BrightnessController<LilyGoWatch, 0, 255, 5>,
    public BMASensorHelper
{
private:
    LilyGoWatch();
    ~LilyGoWatch();
    LilyGoWatch(const LilyGoWatch &) = delete;
    LilyGoWatch &operator=(const LilyGoWatch &) = delete;
public:
    TouchDrvFT6X36 touch;
    SensorPCF8563 rtc;
    PmicAXP202 pmic;
    Button2    bootButton = Button2(36);    //USER BUTTON ( button)

    /**
     * @brief Get the instance of the AudioOutputDev class.
     * @note  This function returns a pointer to the AudioOutputDev instance.
     */
    AudioOutputIf *getAudioOutput()
    {
        return nullptr;
    }
    /**
     * @brief Get the instance of the AudioInputDev class.
     * @note  This function returns a pointer to the AudioInputDev instance.
     */
    AudioInputIf *getAudioInput()
    {
        return nullptr;
    }
    /**
     * @brief  Get the instance of the LilyGoWatch class.
     * @note   This function returns a pointer to the singleton instance of the class.
     * @retval Pointer to the LilyGoWatch instance.
     */
    static LilyGoWatch *getInstance()
    {
        static LilyGoWatch _instance;
        return &_instance;
    }

    /**
     * @brief Set the boot image.
     * @note  Must be set before begin, passing in an array of images of the same size as the screen.
     * This function is used to set the boot image. The 'image' parameter is a pointer to the memory location
     * where the boot image data is stored.
     *
     * @param image A pointer to the boot image data.
     */
    void setBootImage(uint8_t *image);

    /**
     * @brief Get the default begin() initialization options for this board.
     * @return LilyGoDeviceInitOptions Options initialized from this board capability.
     */
    LilyGoDeviceInitOptions getDefaultInitOptions() const;

    /**
     * @brief Begin the device with the default initialization options.
     * @return uint32_t Hardware probe mask collected during initialization.
     */
    uint32_t begin();

    /**
     * @brief Begin the device with explicit initialization options.
     * @param init_options Controls which supported devices begin() should initialize.
     * @return uint32_t Hardware probe mask collected during initialization.
     */
    uint32_t begin(const LilyGoDeviceInitOptions &init_options);

    /**
     * @brief Begin the device with a legacy skip-initialization bitmask.
     * @deprecated Use begin(const LilyGoDeviceInitOptions&) instead. This overload will be removed in a future release.
     * @param disable_hw_init Bitmask composed from NO_HW_* / NO_INIT_* macros.
     * @return uint32_t Hardware probe mask collected during initialization.
     */
    LILYGO_DEPRECATED("Use begin(const LilyGoDeviceInitOptions&) instead. The disable_hw_init bitmask overload will be removed in a future release.")
    uint32_t begin(uint32_t disable_hw_init);


    void setTouchType(uint8_t type);

    /**
     * @brief Main loop function.
     *
     * This function is typically called in an infinite loop.
     */
    void loop();

    /**
     * @brief Initialize the touch screen.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initTouch();

    /**
     * @brief Initialize the sensor.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initSensor();

    /**
     * @brief Initialize the Real-Time Clock (RTC).
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initRTC();

    /**
     * @brief Probe and configure the optional PN532 backplate on Wire (SDA 21, SCL 22).
     * @note Called by begin() when initNfc is enabled. Safe to call after a reader reset.
     *       Detecting this backplate unmounts SD and prevents SD mounting until reboot.
     * @return True only when a PN532 responds and reader configuration succeeds.
     */
    bool initNFC();

    /** @brief Get the PN532 I2C driver. Call initNFC() before using it. */
    Adafruit_PN532 &getNFC();

    /** @brief Get the firmware version cached by the successful PN532 probe. */
    uint32_t getNFCFirmwareVersion() const;

    /**
     * @brief Lock the SPI bus.
     *
     * @param xTicksToWait Time to wait for the lock (default: portMAX_DELAY).
     * @return bool True if the lock is successful, false otherwise.
     */
    bool lockSPI(TickType_t xTicksToWait = portMAX_DELAY);

    /**
     * @brief Unlock the SPI bus.
     */
    void unlockSPI();

    /**
     * @brief Set the display brightness.
     *
     * @param level Brightness level. Range 0 ~ 16
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Get the current display brightness.
     *
     * @return uint8_t Current brightness level.
     */
    uint8_t getBrightness();

    /**
     * @brief Set the display rotation.
     *
     * @param rotation Rotation value Range: 0 - 3.
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Get the current display rotation.
     *
     * @return uint8_t Current rotation value.
     */
    uint8_t getRotation();

    /**
     * @brief Push color data to the display.
     *
     * @param x1 Starting x-coordinate.
     * @param y1 Starting y-coordinate.
     * @param x2 Ending x-coordinate.
     * @param y2 Ending y-coordinate.
     * @param color Pointer to the color data.
     */
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) override;

    /**
    * @brief Check if the color data needs to be swapped.
    * @note  Pass the query to lvgl whether a swap is needed.
    * @return bool True if color data needs to be swapped, false otherwise.
    */
    bool needSwapColors() override;

    /**
     * @brief Check if the touch screen is available.
     *
     * @return bool True if the touch screen is available, false otherwise.
     */
    bool hasTouch();

    /**
     * @brief Get the width of the display.
     *
     * @return uint16_t Display width.
     */
    uint16_t width();

    /**
     * @brief Get the height of the display.
     *
     * @return uint16_t Display height.
     */
    uint16_t height();

    /**
     * @brief Get touch points.
     *
     * @param x_array Pointer to an array to store x-coordinates.
     * @param y_array Pointer to an array to store y-coordinates.
     * @param get_point Number of touch points to get.
     * @return uint8_t Number of touch points actually retrieved.
     */
    uint8_t getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point);

    /**
     * @brief Check if the touch screen is touched.
     *
     * @return bool True if the touch screen is touched, false otherwise.
     */
    bool getTouched();

    /**
     * @brief Trigger the vibrator.
     */
    void vibrator();

    /**
     * @brief Put the device into light sleep mode.
     *
     * Light sleep will turn off Haptic, GPS, Speaker , WiFi , Bluetooth .
     * If you need to enable NFC after calling this method, you must call the NFC initialization method again.
     *
     * @param wakeup_src Wake-up source (default: touch panel). Supported physical
     * wake-up sources are the power key, touch panel, and BUTTON_INT (GPIO36).
     */
    void lightSleep(WakeupSource_t wakeup_src = (WakeupSource_t)(WAKEUP_SRC_TOUCH_PANEL));

    /**
     * @brief Put the device into sleep mode.
     * @ On an ESP32, only one wake-up method can be used; unlike the ESP32S3, multiple combinations are not possible.
     * @param wakeup_src Wake-up source (default: power key). Timer wake-up may be
     * used alone or combined with the supported physical sources.
     * @param off_rtc_backup_domain The parameter is retained but has no effect.
     * @param sleep_second Sleep duration in seconds when the timer is used as the wakeup source (default: 0).
       If the timer is not used as the wakeup source, this parameter is ignored.
     */
    void sleep(WakeupSource_t wakeup_src = (WakeupSource_t)(WAKEUP_SRC_POWER_KEY),
               bool off_rtc_backup_domain = true, uint32_t sleep_second = 0);

    /**
     * @brief Put the display into sleep mode.
     */
    void sleepDisplay();

    /**
     * @brief Wake up the display.
     */
    void wakeupDisplay();

    /**
     * @brief Control the power of a specific channel.
     *
     * @param ch Power control channel.
     * @param enable Whether to enable the channel.
     */
    void powerControl(PowerCtrlChannel_t ch, bool enable);

    /**
    * @brief Install the SD card.
    *
    * This function attempts to install the SD card. It returns 'true' if the installation is successful, and
    * 'false' otherwise.
    * Mounting is rejected when the mutually exclusive PN532 backplate has been detected.
    *
    * @return bool True if SD card installation is successful, false otherwise.
    */
    bool installSD(uint32_t spi_freq = 0);

    /**
     * @brief Uninstall the SD card.
     *
     * This function uninstalls the previously installed SD card.
     */
    void uninstallSD();

    /**
     * @brief Check if the SD card is ready.
     *
     * This function checks whether the SD card is ready for use. It returns 'true' if ready, and 'false' otherwise.
     *
     * @return bool True if the SD card is ready, false otherwise.
     */
    bool isCardReady();

    /**
     * @brief Get the device probe value.
     *
     * @return uint32_t Device probe value.
     */
    uint32_t getDeviceProbe();

    /**
     * @brief Get the device name.
     *
     * @return const char* Pointer to the device name string.
     */
    const char *getName();

    /**
     * @brief Get the static device capability descriptor.
     *
     * @return const LilyGoDeviceCapability& Reference to the board capability descriptor.
     */
    const LilyGoDeviceCapability &getCapability() const;

    /**
     * @brief Get the number of codec input channels.(Microphone)
     *
     * This function returns the number of codec input channels available in the device.
     *
     * @return uint8_t The number of codec input channels.
     */
    uint8_t getCodecInputChannels()
    {
        return 1;
    };

    /**
     * @brief Get the number of codec audio output channels.(Speaker)
     *
     * This function returns the number of codec audio output channels available in the device.
     *
     * @return uint8_t The number of codec audio output channels.
     */
    uint8_t getCodecOutputChannels()
    {
        return 1;
    };

    /**
     * @brief Get the maximum display brightness level.
     *
     * This function returns the maximum brightness level that the display can achieve.
     *
     * @return uint8_t The maximum display brightness level.
     */
    uint8_t getDisplayBrightnessMaxLevel()
    {
        return 255;
    };


    /**
     * @brief Shutdown the device.
     *
     * This function performs a complete shutdown of the device. The device will remain in shutdown state
     * until a wake-up event occurs (Only PWR Button pressed one second).
     *
     * @return bool Returns false if the device does not allow turning off; otherwise,
     * returns nothing and the device will power off.
     */
    bool shutdown() override;
private:
    /**
     * @brief Clear the specified event bits.
     *
     * This function is used to clear the specified event bits, resetting the corresponding event states.
     *
     * @param uxBitsToClear The event bit mask to be cleared.
     */
    void clearEventBits(const EventBits_t uxBitsToClear);

    /**
     * @brief Set the specified event bits.
     *
     * This function is used to set the specified event bits to mark the occurrence of specific events.
     *
     * @param uxBitsToSet The event bit mask to be set.
     */
    void setEventBits(const EventBits_t uxBitsToSet);

    /**
     * @brief Check the power status.
     *
     * This function is responsible for checking the current power status of the device or system.
     */
    void checkPowerStatus();

    /**
     * @brief Check the wake-up pins based on the wake-up source.
     *
     * This function checks the wake-up pins according to the given wake-up source and returns relevant information.
     *
     * @param wakeup_src The wake-up source used for the check.
     * @return uint64_t A value representing the result of checking the wake-up pins.
     */
    uint64_t checkWakeupPins(WakeupSource_t wakeup_src);

    /**
     * @brief Initialize the Power Management Unit (PMU).
     *
     * This function attempts to initialize the PMU and returns a boolean indicating the success of the initialization.
     *
     * @return bool True if the PMU initialization is successful, false otherwise.
     */
    bool initPMU();

    uint16_t getChargeLevelToCurrentImpl(uint8_t level) override
    {
        return pmic.getConfig().chargeCurrentStep * level;
    }

    uint16_t getChargeCurrentToLevelImpl() override
    {
        uint16_t current = getChargeCurrent();
        uint16_t step = pmic.getConfig().chargeCurrentStep;
        return current / step;
    }

    static EventGroupHandle_t _event;
    uint8_t *_boot_images_addr;
    bool _touchType;
};

extern LilyGoWatch &instance;

#define DEVICE_MAX_BRIGHTNESS_LEVEL 255
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#define DEVICE_MAX_CHARGE_CURRENT   1000
#define DEVICE_MIN_CHARGE_CURRENT   100
#define DEVICE_CHARGE_LEVEL_NUMS    12
#define DEVICE_CHARGE_STEPS         1
#define DEVICE_CHARGE_CURRENT_RECOMMEND 190


#endif
