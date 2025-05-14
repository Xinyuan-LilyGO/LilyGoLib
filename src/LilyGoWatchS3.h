/**
 * @file      LilyGoWatch2022.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 */
#pragma once

#ifdef ARDUINO_T_WATCH_S3

#include <Arduino.h>
#include <FFat.h>
#include <FS.h>
#include <Wire.h>
#include <TouchDrvFT6X36.hpp>
#include <SensorBMA423.hpp>
#include <SensorPCF8563.hpp>
#include <SensorDRV2605.hpp>
#include <RadioLib.h>
#include "GPS.h"
#include "PDM.h"
#include "LilyGoDispInterface.h"
#include "LilyGoEventManage.h"
#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
#include <ESP_I2S.h>
#endif
#include "LilyGoTypedef.h"
#include "LilyGoPowerManage.h"

#define newModule()   new Module(LORA_CS,LORA_IRQ,LORA_RST,LORA_BUSY,SPI)

class LilyGoWatch2022 : public LilyGo_Display,
    public LilyGoDispSPI,
    public LilyGoEventManage,
    public LilyGoPowerManage
{
public:
    GPS gps;
    TouchDrvFT6X36 touch;
    SensorBMA423 sensor;
    SensorPCF8563 rtc;
    SensorDRV2605 drv;
    XPowersAXP2101 pmu;

#ifdef USING_PDM_MICROPHONE
#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    PDM mic;
#else
    I2SClass mic;
#endif
#endif

#ifdef USING_PCM_AMPLIFIER
#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    Player player;
#else
    I2SClass player;
#endif
#endif

    LilyGoWatch2022();
    ~LilyGoWatch2022();

    /**
     * @brief Set the boot image.
     * @note  Must be set before begin, passing in an array of images of the same size as the screen.
     * This function is used to set the boot image. The 'image' parameter is a pointer to the memory location
     * where the boot image data is stored.
     *
     * @param image A pointer to the boot image data.
     */
    void setBootImage(uint8_t * image);

    /**
     * @brief Begin the device
     *
     * @param disable_hw_init Optional parameter to disable hardware initialization (default: 0).
     * @return uint32_t A value indicating the result of the operation.
     */
    uint32_t begin(uint32_t disable_hw_init = 0);

    /**
     * @brief Main loop function.
     *
     * This function is typically called in an infinite loop.
     */
    void loop();

    /**
     * @brief Initialize the driver.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initDrv();

    /**
     * @brief Initialize the GPS module.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initGPS();

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
     * @brief Initialize the microphone.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initMicrophone();

    /**
     * @brief Initialize the amplifier.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if initialization is successful, false otherwise.
     */
    bool initAmplifier();

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
     * @brief Decrease the display brightness to the target level.
     *
     * @param target_level Target brightness level.
     * @param delay_ms Delay between each brightness decrement (default: 5ms).
     * @param async Whether to perform the operation asynchronously (default: false).
     */
    void decrementBrightness(uint8_t target_level, uint32_t delay_ms = 5, bool async = false);

    /**
     * @brief Increase the display brightness to the target level.
     *
     * @param target_level Target brightness level.
     * @param delay_ms Delay between each brightness increment (default: 5ms).
     * @param async Whether to perform the operation asynchronously (default: false).
     */
    void incrementalBrightness(uint8_t target_level, uint32_t delay_ms = 5, bool async = false);

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
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);

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
     * @brief Set haptic effects.
     *
     * @param effects Haptic effects setting.
     */
    void setHapticEffects(uint8_t effects);

    /**
     * @brief Get the current haptic effects setting.
     *
     * @return uint8_t Current haptic effects setting.
     */
    uint8_t getHapticEffects();

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
     * @param wakeup_src Wakeup source (default: power key and touch panel).
     */
    void lightSleep(WakeupSource_t wakeup_src =  (WakeupSource_t)(WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_TOUCH_PANEL));

    /**
     * @brief Put the device into sleep mode.
     *
     * @param wakeup_src Wakeup source (default: power key and touch panel).
     * @param off_rtc_backup_domain Whether to turn off the RTC backup domain (default: true).
     */
    void sleep(WakeupSource_t wakeup_src = (WakeupSource_t)(WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_TOUCH_PANEL),
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
     * @brief Check the sensor status.
     *
     * This function is used to check the status of the sensors, such as whether they are working properly.
     */
    void checkSensorStatus();

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

    static EventGroupHandle_t _event;
    uint8_t _effects;
    uint32_t devices_probe;
    uint8_t *_boot_images_addr;
};

extern LilyGoWatch2022 instance;

#if    defined(ARDUINO_LILYGO_LORA_SX1262)
extern SX1262 radio;
#define USING_RADIO_NAME        "SX1262"
#elif  defined(ARDUINO_LILYGO_LORA_SX1280)
extern SX1280 radio;
#define USING_RADIO_NAME        "SX1280"
#elif  defined(ARDUINO_LILYGO_LORA_CC1101)
extern CC1101 radio;
#define USING_RADIO_NAME        "CC1101"
#elif  defined(ARDUINO_LILYGO_LORA_LR1121)
extern LR1121 radio;
#define USING_RADIO_NAME        "LR1121"
#elif  defined(ARDUINO_LILYGO_LORA_SI4432)
extern Si4432 radio;
#define USING_RADIO_NAME        "SI4432"
#endif

#define DEVICE_MAX_BRIGHTNESS_LEVEL 255
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#define DEVICE_MAX_CHARGE_CURRENT   1000
#define DEVICE_MIN_CHARGE_CURRENT   100
#define DEVICE_CHARGE_LEVEL_NUMS    12
#define DEVICE_CHARGE_STEPS         1

#endif










