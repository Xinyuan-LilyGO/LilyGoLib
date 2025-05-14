/**
 * @file      LilyGoWatchUltra.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-06
 *
 */
#pragma once

#ifdef ARDUINO_T_WATCH_S3_ULTRA

#include <Arduino.h>
#include <driver/spi_master.h>
#include <SPI.h>
#include <SensorPCF85063.hpp>
#include <SensorDRV2605.hpp>
#include <SensorBHI260AP.hpp>
#include <TouchDrvCSTXXX.hpp>
#include <RadioLib.h>
#include <SD.h>
#include "GPS.h"
#include "PDM.h"
#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
#include <ESP_I2S.h>
#endif
#ifdef USING_XL9555_EXPANDS
#include <ExtensionIOXL9555.hpp>
#endif
#ifdef USING_ST25R3916
#include "nfc_include.h"
#endif
#include "LilyGoDispInterface.h"
#include "LilyGoEventManage.h"
#include "LilyGoTypedef.h"
#include "LilyGoPowerManage.h"

#define newModule()   new Module(LORA_CS,LORA_IRQ,LORA_RST,LORA_BUSY)

/*
| I2C Devices                                                                                      | 7-Bit Address |
| ------------------------------------------------------------------------------------------------ | ------------- |
| [Touch Panel CST9217]                                                                            | 0x1A          |
| [Smart sensor BHI260AP](https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ap/) | 0x28          |
| [Haptic driver DRV2605](https://www.ti.com/product/DRV2605)                                      | 0x5A          |
| [Power Manager AXP2101](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)        | 0x34          |
| [Expands IO XL9555](https://xinluda.com/en/I2C-to-GPIO-extension/20240718576.html)               | 0x20          |
| [Real-Time Clock PCF85063A](https://www.nxp.com/products/PCF85063A)                              | 0x51          |
*/

class LilyGoUltra: public LilyGo_Display, public LilyGoDispQSPI, public LilyGoEventManage, public LilyGoPowerManage
{
public:
    XPowersAXP2101 pmu;
    GPS gps;
    SensorBHI260AP sensor;
    SensorPCF85063 rtc;
    TouchDrvCST92xx touch;
    SensorDRV2605 drv;

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

#ifdef USING_XL9555_EXPANDS
    ExtensionIOXL9555 io;
#endif

    LilyGoUltra();
    ~LilyGoUltra();

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
     * @brief Set the display parameters.
     *
     * This function is used to configure specific parameters of the display device,
     * including whether to enable Direct Memory Access (DMA) and whether to enable the tearing effect.
     * It is important to note that this function must be called before the begin function;
     * otherwise, the settings will be ineffective.
     *
     * * In actual use, although enabling DMA will bring a significant increase in frame rate,
     * the display effect is not as good as disabling DMA. Therefore, DMA transmission is disabled by default.
     *
     * @param enableDMA A boolean value indicating whether to enable Direct Memory Access (DMA).
     *                  If true, DMA is enabled; if false, DMA is disabled.
     *                  LilyGoUltra default disabled DMA
     * @param enableTearingEffect A boolean value indicating whether to enable the tearing effect.
     *                            If true, the tearing effect is enabled; if false, it is disabled.
     *                            LilyGoUltra default disabled tearing effect
     */
    void setDisplayParams(bool enableDMA, bool enableTearingEffect);

    /**
     * @brief Begin the device.
     *
     * This function initializes the system and starts its operation. The 'disable_hw_init' parameter can be used
     * to disable hardware initialization if set to a non - zero value. It returns a 32-bit unsigned integer
     * which might represent the result of the initialization process.
     *
     * @param disable_hw_init Optional parameter to disable hardware initialization (default: 0).
     * @return uint32_t A value indicating the result of the initialization.
     */
    uint32_t begin(uint32_t disable_hw_init = 0);

    /**
     * @brief Main loop function.
     *
     * This function represents the main loop of the system. It is typically called repeatedly to perform
     * continuous operations.
     */
    void loop();

    /**
     * @brief Initialize the NFC module.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function attempts to initialize the Near - Field Communication (NFC) module. It returns 'true' if
     * the initialization is successful, and 'false' otherwise.
     *
     * @return bool True if NFC initialization is successful, false otherwise.
     */
    bool initNFC();

    /**
     * @brief Initialize the driver.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the necessary drivers. It returns 'true' if the initialization is successful,
     * and 'false' otherwise.
     *
     * @return bool True if driver initialization is successful, false otherwise.
     */
    bool initDrv();

    /**
     * @brief Initialize the GPS module.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function attempts to initialize the Global Positioning System (GPS) module. It returns 'true' if
     * the initialization is successful, and 'false' otherwise.
     *
     * @return bool True if GPS initialization is successful, false otherwise.
     */
    bool initGPS();

    /**
     * @brief Initialize the touch screen.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the touch screen functionality. It returns 'true' if the initialization is
     * successful, and 'false' otherwise.
     *
     * @return bool True if touch screen initialization is successful, false otherwise.
     */
    bool initTouch();

    /**
     * @brief Initialize the sensor.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the sensor. It returns 'true' if the initialization is successful, and 'false'
     * otherwise.
     *
     * @return bool True if sensor initialization is successful, false otherwise.
     */
    bool initSensor();

    /**
     * @brief Initialize the Real - Time Clock (RTC).
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the Real - Time Clock. It returns 'true' if the initialization is successful,
     * and 'false' otherwise.
     *
     * @return bool True if RTC initialization is successful, false otherwise.
     */
    bool initRTC();

    /**
     * @brief Initialize the microphone.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the microphone. It returns 'true' if the initialization is successful, and
     * 'false' otherwise.
     *
     * @return bool True if microphone initialization is successful, false otherwise.
     */
    bool initMicrophone();

    /**
     * @brief Initialize the amplifier.
     *
     * This function initializes the amplifier. It returns 'true' if the initialization is successful, and
     * 'false' otherwise.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * @return bool True if amplifier initialization is successful, false otherwise.
     */
    bool initAmplifier();

    /**
     * @brief Lock the SPI bus.
     *
     * This function attempts to lock the Serial Peripheral Interface (SPI) bus. The 'xTicksToWait' parameter
     * specifies the maximum time to wait for the lock. It returns 'true' if the lock is successful, and 'false'
     * otherwise.
     *
     * @param xTicksToWait Time to wait for the lock (default: portMAX_DELAY).
     * @return bool True if the lock is successful, false otherwise.
     */
    bool lockSPI(TickType_t xTicksToWait = portMAX_DELAY);

    /**
     * @brief Unlock the SPI bus.
     *
     * This function unlocks the previously locked SPI bus.
     */
    void unlockSPI();

    /**
     * @brief Set the display brightness.
     *
     * This function sets the brightness level of the display. The 'level' parameter specifies the desired
     * brightness level. Range 0 ~ 255
     *
     * @param level The brightness level to set.
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Get the current display brightness.
     *
     * This function retrieves the current brightness level of the display.
     *
     * @return uint8_t The current brightness level.
     */
    uint8_t getBrightness();

    /**
     * @brief Decrease the display brightness to the target level.
     *
     * This function gradually decreases the display brightness to the 'target_level'. The 'delay_ms' parameter
     * specifies the delay between each decrement step, and the 'reserve' parameter might be used for some
     * additional reservation settings.
     *
     * @param target_level The target brightness level to reach.
     * @param delay_ms Delay between each brightness decrement step (default: 5ms).
     * @param reserve Optional parameter for additional reservation settings (default: false).
     */
    void decrementBrightness(uint8_t target_level, uint32_t delay_ms = 5, bool reserve = false);

    /**
     * @brief Increase the display brightness to the target level.
     *
     * This function gradually increases the display brightness to the 'target_level'. The 'delay_ms' parameter
     * specifies the delay between each increment step, and the 'reserve' parameter might be used for some
     * additional reservation settings.
     *
     * @param target_level The target brightness level to reach.
     * @param delay_ms Delay between each brightness increment step (default: 5ms).
     * @param reserve Optional parameter for additional reservation settings (default: false).
     */
    void incrementalBrightness(uint8_t target_level, uint32_t delay_ms = 5, bool reserve = false);

    /**
     * @brief Set the display rotation.
     *
     * This function sets the rotation of the display. The 'rotation' parameter specifies the rotation value.
     * 
     * Can't set rotation 90° or 270°
     * 
     * @param rotation The rotation value to set.
     */
    void setRotation(uint8_t rotation) override;

    /**
     * @brief Get the current display rotation.
     *
     * This function retrieves the current rotation value of the display.
     *
     * @return uint8_t The current rotation value.
     */
    uint8_t getRotation() override;

    /**
     * @brief Get the width of the display.
     *
     * This function returns the width of the display.
     *
     * @return uint16_t The width of the display.
     */
    uint16_t width() override;

    /**
     * @brief Get the height of the display.
     *
     * This function returns the height of the display.
     *
     * @return uint16_t The height of the display.
     */
    uint16_t height() override;

    /**
     * @brief Push color data to the display.
     *
     * This function pushes color data to a specific rectangular area on the display. The 'x1', 'y1', 'x2', 'y2'
     * parameters define the rectangular area, and the 'color' parameter points to the color data.
     *
     * @param x1 The starting x - coordinate of the rectangular area.
     * @param y1 The starting y - coordinate of the rectangular area.
     * @param x2 The ending x - coordinate of the rectangular area.
     * @param y2 The ending y - coordinate of the rectangular area.
     * @param color A pointer to the color data.
     */
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) override;

    /**
     * @brief Check if the touch screen is touched.
     *
     * This function checks whether the touch screen is currently being touched. It returns 'true' if touched,
     * and 'false' otherwise.
     *
     * @return bool True if the touch screen is touched, false otherwise.
     */
    bool getTouched();

    /**
     * @brief Get touch points.
     *
     * This function retrieves the touch points on the touch screen. The 'x_array' and 'y_array' parameters are
     * used to store the x and y coordinates of the touch points respectively. The 'get_point' parameter specifies
     * the number of touch points to retrieve (default: 1).
     *
     * @param x_array A pointer to an array to store the x - coordinates of touch points.
     * @param y_array A pointer to an array to store the y - coordinates of touch points.
     * @param get_point The number of touch points to retrieve (default: 1).
     * @return uint8_t The actual number of touch points retrieved.
     */
    uint8_t getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point = 1) override;

    /**
     * @brief Check if the touch screen is available.
     *
     * This function checks whether the touch screen functionality is available. It returns 'true' if available,
     * and 'false' otherwise.
     *
     * @return bool True if the touch screen is available, false otherwise.
     */
    bool hasTouch() override;

    /**
     * @brief Control the power of a specific channel.
     *
     * This function controls the power supply of a specific power control channel. The 'ch' parameter specifies
     * the channel, and the 'enable' parameter indicates whether to enable or disable the power supply.
     *
     * @param ch The power control channel to operate on.
     * @param enable True to enable the power supply, false to disable it.
     */
    void powerControl(enum PowerCtrlChannel ch, bool enable);

    /**
     * @brief Install the SD card.
     *
     * This function attempts to install the SD card. It returns 'true' if the installation is successful, and
     * 'false' otherwise.
     *
     * @return bool True if SD card installation is successful, false otherwise.
     */
    bool installSD();

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
     * @brief Put the device into light sleep mode.
     *
     * This function puts the device into light sleep mode. The 'wakeup_src' parameter specifies the wake-up
     * sources that can wake the device from light sleep.
     *
     * Light sleep will turn off Haptic, GPS, Speaker, NFC , WiFi , Bluetooth .
     * If you need to enable NFC after calling this method, you must call the NFC initialization method again.
     *
     * @param wakeup_src The wake-up sources (default: power key, boot button, and touch panel).
     */
    void lightSleep(WakeupSource_t wakeup_src =
                        (WakeupSource_t)(WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_TOUCH_PANEL));

    /**
     * @brief Put the device into sleep mode.
     *
     * This function puts the device into sleep mode. The 'wakeup_src' parameter specifies the wake-up sources
     * that can wake the device from sleep, and the 'off_rtc_backup_domain' parameter indicates whether to turn
     * off the RTC backup domain.
     *
     * @param wakeup_src The wake-up sources (default: power key, boot button).
     * @param off_rtc_backup_domain Whether to turn off the RTC backup domain (default: true).
     */
    void sleep(WakeupSource_t wakeup_src =
                   (WakeupSource_t)(WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_BOOT_BUTTON ),
               bool off_rtc_backup_domain = true, uint32_t sleep_second = 0);

    /**
     * @brief Put the display into sleep mode.
     *
     * When called, this function puts the display into sleep mode, significantly reducing the current consumption
     * to about 10mA.
     */
    void sleepDisplay();

    /**
     * @brief Wake up the display.
     *
     * This function wakes up the display when it is in sleep mode.
     */
    void wakeupDisplay();


    void wakeupTouch();

    /**
     * @brief Set haptic effects.
     *
     * This function sets the haptic effects. The 'effects' parameter specifies the desired haptic effects setting.
     *
     * @param effects The haptic effects setting to set.
     */
    void setHapticEffects(uint8_t effects);

    /**
     * @brief Get the current haptic effects setting.
     *
     * This function retrieves the current haptic effects setting.
     *
     * @return uint8_t The current haptic effects setting.
     */
    uint8_t getHapticEffects();

    /**
     * @brief Trigger the vibrator.
     *
     * This function triggers the vibrator to produce a vibration.
     */
    void vibrator();

    /**
     * @brief Get the device name.
     *
     * This function returns a pointer to a string representing the device name.
     *
     * @return const char* A pointer to the device name string.
     */
    const char *getName();

    /**
     * @brief Get the device probe value.
     *
     * This function returns a 32-bit unsigned integer representing the device probe value.
     *
     * @return uint32_t The device probe value.
     */
    uint32_t getDeviceProbe();


    /**
     * @brief Get the device display is enable DMA.
     * @return true enabled , false not enable
     */
    bool useDMA();

private:
    /**
     * @brief Initialize the shared SPI pins.
     *
     * This function is responsible for initializing the pins that are shared by the SPI (Serial Peripheral Interface).
     * It configures the necessary settings for these pins to ensure proper communication over the SPI bus.
     */
    void initShareSPIPins();

    /**
     * @brief Check the power status.
     *
     * This function examines the current power status of the device or system. It can be used to detect
     * whether the power supply is stable, if there are any power - related issues, or to obtain power - related metrics.
     */
    void checkPowerStatus();

    /**
     * @brief Clear specified event bits.
     *
     * Given a set of event bits defined by the 'uxBitsToClear' parameter, this function clears those bits
     * in the event flag register or relevant data structure. Clearing these bits can be used to reset certain
     * event states or signals.
     *
     * @param uxBitsToClear A mask representing the event bits to be cleared.
     */
    void clearEventBits(const EventBits_t uxBitsToClear);

    /**
     * @brief Set specified event bits.
     *
     * This function sets the event bits specified by the 'uxBitsToSet' parameter in the event flag register
     * or relevant data structure. Setting these bits can be used to mark the occurrence of certain events
     * or to trigger specific actions based on those events.
     *
     * @param uxBitsToSet A mask representing the event bits to be set.
     */
    void setEventBits(const EventBits_t uxBitsToSet);

    /**
     * @brief Check the wake-up pins based on the wake-up source.
     *
     * Based on the provided wake-up source ('wakeup_src'), this function checks the status of the wake-up pins.
     * It can determine if any of the wake-up conditions associated with those pins are met. The function returns
     * a 64 - bit unsigned integer which may contain information about the state of the wake-up pins.
     *
     * @param wakeup_src The wake-up source that determines which pins to check.
     * @return uint64_t A value indicating the status of the wake-up pins.
     */
    uint64_t checkWakeupPins(WakeupSource_t wakeup_src);

    /**
     * @brief Initialize the Power Management Unit (PMU).
     *
     * This function attempts to initialize the Power Management Unit of the device. The PMU is responsible
     * for managing the power consumption, battery charging, and other power - related functions.
     * It returns true if the initialization is successful and false otherwise.
     *
     * @return bool True if the PMU initialization is successful, false otherwise.
     */
    bool initPMU();


    static EventGroupHandle_t _event;
    uint8_t _effects;
    uint32_t devices_probe;
    uint8_t *_boot_images_addr;
    xSemaphoreHandle _lock;
    bool _enableDMA, _enableTearingEffect;
};

#ifdef USING_ST25R3916
extern RfalNfcClass NFCReader;
#endif

extern LilyGoUltra instance;

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

#endif // ARDUINO_T_WATCH_S3_ULTRA















