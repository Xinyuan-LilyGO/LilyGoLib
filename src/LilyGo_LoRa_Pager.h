/**
 * @file      LilyGo_LoRa_Pager.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-17
 *
 */

#pragma once

#ifdef ARDUINO_T_LORA_PAGER

#include <Arduino.h>
#include <driver/spi_master.h>
#include <SPI.h>
#include <SD.h>
#include <RtcDrv.hpp>
#include <HapticDrivers.hpp>
#include <IoExpanderDrv.hpp>
#include <AW9364LedDriver.hpp>
#include <GaugeDrv.hpp>
#include <ImuDrv.hpp>
#include "core/LilyGoPowerManageInf.h"
#include "display/LilyGoDispInterface.h"
#include <RadioLib.h>
#include "gps/GPS.h"
#include "input/LilyGoKeyboard.h"
#include "nfc/nfc_include.h"
#include "core/LilyGoEventManage.h"
#include "core/LilyGoTypedef.h"
#include "display/BrightnessController.h"
#include "audio/AudioDevice.h"
#include <Button2.h>

#define newModule()   new Module(LORA_CS,LORA_IRQ,LORA_RST,LORA_BUSY)
#include "radio/LilyGoRadioHelper.h"

using custom_feedback_t = void(*)(void *args);

class LilyGoLoRaPager: public LilyGo_Display,
    public LilyGoDispArduinoSPI,
    public LilyGoEventManage,
    public LilyGoPowerManageInf,
    public BrightnessController<LilyGoLoRaPager, 0, 16, 50>
{
private:
    /**
     * @brief Private constructor for singleton pattern.
     */
    LilyGoLoRaPager();
    /**
     * @brief Private destructor.
     */
    ~LilyGoLoRaPager();
    bool _radio_hardware_present = true;
    LilyGoLoRaPager(const LilyGoLoRaPager &) = delete;
    LilyGoLoRaPager &operator=(const LilyGoLoRaPager &) = delete;


public:

    EspCodec            codec;
    AudioInputCodecDev  audioInput{&codec};
    AudioOutputCodecDev audioOutput{&codec};

    /**
     * @brief Get the instance of the AudioOutputCodecDev class.
     * @note  This function returns a pointer to the AudioOutputCodecDev instance.
     */
    AudioOutputIf *getAudioOutput()
    {
        return &audioOutput;
    }
    /**
     * @brief Get the instance of the AudioInputCodecDev class.
     * @note  This function returns a pointer to the AudioInputCodecDev instance.
     */
    AudioInputIf *getAudioInput()
    {
        return &audioInput;
    }

public:
    GPS             gps;
    SensorBHI260AP  sensor;
    SensorPCF85063  rtc;
    HapticDriver_DRV2605   drv;
    GaugeBQ27220    gauge;
    AW9364LedDriver backlight;
    PmicBQ25896     pmic;
    LilyGoKeyboard  kb;
    IoExpanderXL9555 io;
    Button2         bootButton = Button2(0);    //BOOT BUTTON ( button)

    /**
     * @brief  Get the instance of the LilyGoLoRaPager class.
     * @note   This function returns a pointer to the singleton instance of the class.
     * @retval Pointer to the LilyGoLoRaPager instance.
     */
    static LilyGoLoRaPager *getInstance()
    {
        static LilyGoLoRaPager _instance;
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

    /**
     * @brief Main loop function.
     *
     * This function represents the main loop of the system. It is typically called repeatedly to perform
     * continuous operations such as processing events, updating states, etc.
     */
    void loop();

    /**
     * @brief Initialize the LoRa module.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function attempts to initialize the LoRa module. It returns 'true' if the initialization is successful,
     * and 'false' otherwise.
     *
     * @return bool True if LoRa initialization is successful, false otherwise.
     */
    bool initLoRa();

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
     * @brief Initialize the keyboard.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the keyboard input device. It returns 'true' if the initialization is successful,
     * and 'false' otherwise.
     *
     * @return bool True if keyboard initialization is successful, false otherwise.
     */
    bool initKeyboard();

    /**
     * @brief Initialize the driver.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the necessary drivers for the system. It returns 'true' if the initialization
     * is successful, and 'false' otherwise.
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
     * @brief Initialize the sensor.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function initializes the sensor(s) in the system. It returns 'true' if the initialization is successful,
     * and 'false' otherwise.
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

#ifndef RADIOLIB_EXCLUDE_NRF24
    /**
     * @brief Initialize the NRF24 module.
     * @note  Already called in begin, it is only necessary to call when begin specifies not to initialize this device.
     * This function attempts to initialize the NRF24 wireless communication module. It returns 'true' if
     * the initialization is successful, and 'false' otherwise. This function is only available when the
     * RADIOLIB_EXCLUDE_NRF24 macro is not defined.
     *
     * @return bool True if NRF24 initialization is successful, false otherwise.
     */
    bool initNRF24();
#endif

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
     * brightness level. Range 0 ~ 16
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
     * @brief Set the display rotation.
     *
     * This function sets the rotation of the display. The 'rotation' parameter specifies the rotation value.
     *
     * @param rotation The rotation value to set.
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Get the current display rotation.
     *
     * This function retrieves the current rotation value of the display.
     *
     * @return uint8_t The current rotation value.
     */
    uint8_t getRotation();

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
     * @brief Check if the color data needs to be swapped.
     * @note  Pass the query to lvgl whether a swap is needed.
     * @return bool True if color data needs to be swapped, false otherwise.
     */
    bool needSwapColors() override;

    /**
     * @brief Control the power of a specific channel.
     *
     * This function controls the power supply of a specific power control channel. The 'ch' parameter specifies
     * the channel, and the 'enable' parameter indicates whether to enable or disable the power supply.
     *
     * @param ch The power control channel to operate on.
     * @param enable True to enable the power supply, false to disable it.
     */
    void powerControl(PowerCtrlChannel_t ch, bool enable);

    /**
     * @brief Install the SD card.
     *
     * This function attempts to install the SD card. It returns 'true' if the installation is successful, and
     * 'false' otherwise.
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
     * @brief Get the width of the display.
     *
     * This function returns the width of the display.
     *
     * @return uint16_t The width of the display.
     */
    uint16_t  width() override;

    /**
     * @brief Get the height of the display.
     *
     * This function returns the height of the display.
     *
     * @return uint16_t The height of the display.
     */
    uint16_t  height() override;

    /**
     * @brief Get a character from the keyboard input.
     *
     * This function attempts to get a character from the keyboard input. It stores the retrieved character
     * in the memory location pointed to by 'c'. It returns an integer indicating the result of the operation.
     *
     * @param c A pointer to a character variable to store the retrieved character.
     * @return int An integer indicating the result of the operation.
     */
    int getKeyChar(char *c) override;

    /**
     * @brief Enable the keyboard event buffer.
     */
    void enableKeyboard();

    /**
     * @brief Disable the keyboard event buffer.
     */
    void disableKeyboard();

    /**
     * @brief Get the rotary message.
     *
     * This function retrieves the message related to the rotary encoder. It returns a value of type RotaryMsg_t
     * which contains information about the rotary encoder's state.
     *
     * @return RotaryMsg_t The rotary encoder message.
     */
    RotaryMsg_t getRotary() override;

    /**
     * @brief Clear the rotary message.
     *
     * This function clears the message related to the rotary encoder, resetting its state.
     */
    void clearRotaryMsg();

    /**
     * @brief Set rotary encoder step divider.
     *
     * Larger values require more physical encoder counts before one LVGL step is emitted,
     * reducing sensitivity at runtime without rebuilding firmware.
     *
     * @param divider Number of raw encoder counts per output step. Zero is treated as one.
     */
    void setRotaryStepDivider(uint8_t divider);

    /**
     * @brief Get rotary encoder step divider.
     *
     * @return Number of raw encoder counts per output step.
     */
    uint8_t getRotaryStepDivider();

    /**
     * @brief Get the minimum rotary encoder step divider.
     *
     * @return Minimum supported step divider.
     */
    uint8_t getRotaryStepDividerMin();

    /**
     * @brief Get the maximum rotary encoder step divider.
     *
     * @return Maximum supported step divider.
     */
    uint8_t getRotaryStepDividerMax();

    /**
     * @brief Enable the rotary encoder.
     *
     * This function enables the rotary encoder, allowing it to generate events and provide input.
     */
    void enableRotary();

    /**
     * @brief Disable the rotary encoder.
     *
     * This function disables the rotary encoder, preventing it from generating events and providing input.
     */
    void disableRotary();

    /**
     * @brief Attach keyboard feedback.
     *
     * This function enables or disables the keyboard feedback feature. The 'enable' parameter indicates whether
     * to enable or disable the feedback, and the 'effects' parameter specifies the feedback effects (default: 1).
     *
     * @param enable True to enable the keyboard feedback, false to disable it.
     * @param effects The feedback effects setting (default: 1).
     */
    void attachKeyboardFeedback(bool enable, uint8_t effects = 1);

    /**
     * @brief Set the feedback callback function.
     *
     * This function sets the custom feedback callback function. The 'fb' parameter is a pointer to the callback
     * function.
     *
     * @param fb A pointer to the custom feedback callback function.
     */
    void setFeedbackCallback(custom_feedback_t fb);

    /**
     * @brief Trigger the feedback mechanism.
     *
     * This function triggers the feedback mechanism. The 'args' parameter is an optional pointer to additional
     * arguments (default: NULL).
     *
     * @param args An optional pointer to additional arguments.
     */
    void feedback(void *args = NULL);

    /**
     * @brief Trigger the vibrator.
     *
     * This function triggers the vibrator to produce a vibration.
     */
    void vibrator();

    /**
     * @brief Put the device into light sleep mode.
     *
     * @note  This function puts the device into light sleep mode. The 'wakeup_src' parameter specifies the wake-up
     * sources that can wake the device from light sleep.
     *
     * Light sleep will turn off Haptic, GPS, Speaker, NFC, Keyboard , WiFi , Bluetooth .
     * If you need to enable NFC after calling this method, you must call the NFC initialization method again.
     *
     *
     * @param wakeup_src The wake-up source (default: boot button). The boot button is
     * the only supported physical wake-up source.
     */
    void lightSleep(WakeupSource_t wakeup_src = WAKEUP_SRC_BOOT_BUTTON);

    /**
     * @brief Put the device into sleep mode.
     *
     * This function puts the device into sleep mode. The 'wakeup_src' parameter specifies the wake-up sources
     * that can wake the device from sleep.
     *
     * Set to wake up by boot button, deep sleep is about 860 uA , see examples/sleep/WakeUpFromBootButton
     * @param wakeup_src The wake-up sources (default: boot button).
     * @param off_rtc_backup_domain Reserved parameter, no effect.
     * @param sleep_second Timer duration in seconds when WAKEUP_SRC_TIMER is selected.
     * Timer wake-up may be used alone or combined with the boot button.
     */
    void sleep(WakeupSource_t wakeup_src = WAKEUP_SRC_BOOT_BUTTON,
               bool off_rtc_backup_domain = false,
               uint32_t sleep_second = 0);

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
     * @brief Get the device probe value.
     *
     * This function returns a 32-bit unsigned integer representing the device probe value.
     *
     * @return uint32_t The device probe value.
     */
    uint32_t getDeviceProbe();

    /**
     * @brief Return the runtime LoRa hardware detection result.
     *
     * @return true if LoRa hardware is present or runtime detection is disabled.
     */
    bool isLoRaHardwarePresent() const;

    /**
     * @brief Get the device name.
     *
     * This function returns a pointer to a string representing the device name.
     *
     * @return const char* A pointer to the device name string.
     */
    const char *getName();

    /**
     * @brief Get the static device capability descriptor.
     *
     * @return const LilyGoDeviceCapability& Reference to the board capability descriptor.
     */
    const LilyGoDeviceCapability &getCapability() const;

    /**
     * @brief Check if the device has an encoder.
     *
     * This function checks whether the device is equipped with an encoder. It returns 'true' if the device
     * has an encoder, and 'false' otherwise.
     *
     * @return bool True if the device has an encoder, false otherwise.
     */
    bool hasEncoder() override;

    /**
     * @brief Check if the device has a keyboard.
     *
     * This function checks whether the device is equipped with a keyboard. It returns 'true' if the device
     * has a keyboard, and 'false' otherwise.
     *
     * @return bool True if the device has a keyboard, false otherwise.
     */
    bool hasKeyboard() override;

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
        return 16;
    };

    /**
     * @brief Check if the device supports OTG (On-The-Go) functionality.
     *
     * This function checks whether the device has OTG capability.
     *
     * @return bool True if the device supports OTG, false otherwise.
     */
    bool hasOTG() override;

    /**
     * @brief Check if the device has a battery gauge.
     *
     * This function checks whether the device is equipped with a battery gauge IC for monitoring battery status.
     *
     * @return bool True if the device has a battery gauge, false otherwise.
     */
    bool hasGauge() override;

    bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot) override;

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

    /**
     * @brief Check if OTG output is currently enabled.
     *
     * This function checks whether the OTG power output is currently active.
     *
     * @return bool True if OTG output is enabled, false otherwise.
     */
    bool isOTGEnabled() override;

    /**
     * @brief Enable OTG power output.
     *
     * This function enables the OTG (On-The-Go) power output, allowing the device to supply power to
     * connected USB peripherals.
     *
     * @return bool True if OTG was enabled successfully, false otherwise.
     */
    bool enableOTG() override;

    /**
     * @brief Disable OTG power output.
     *
     * This function disables the OTG (On-The-Go) power output, stopping power supply to connected USB peripherals.
     *
     * @return bool True if OTG was disabled successfully, false otherwise.
     */
    bool disableOTG() override;

    /**
     * @brief Get the battery voltage.
     *
     * This function retrieves the current battery voltage reading from the gauge IC.
     *
     * @return float The battery voltage in mv.
     */
    float getBattVoltage() override;

    /**
    * @brief Get the battery percentage.
    *
    * @return float The battery level as a percentage (0-100).
    */
    float getBatteryPercent() override;

    /**
     * @brief Get the battery temperature.
     *
     * This function retrieves the current battery temperature reading from the gauge IC.
     *
     * @return float The battery temperature in degrees Celsius.
     */
    float getTemperature() override;

private:
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
     * @brief Initialize the shared SPI pins.
     *
     * This function is responsible for initializing the pins that are shared by the SPI (Serial Peripheral Interface).
     * It configures the necessary settings for these pins to ensure proper communication over the SPI bus.
     */
    void initShareSPIPins();

    /**
     * @brief Initialize the Power Management Unit (PMU).
     *
     * This function attempts to initialize the PMU and returns a boolean indicating the success of the initialization.
     *
     * @return bool True if the PMU initialization is successful, false otherwise.
     */
    bool initPMU();

    /**
     * @brief  Convert charge level to current.
     * @note   This function converts a given charge level to its corresponding current value.
     * @param  level: The charge level to convert.
     * @retval The corresponding current value.
     */
    uint16_t getChargeLevelToCurrentImpl(uint8_t level) override
    {
        return pmic.getConfig().chargeCurrentStep * level;
    }

    /**
     * @brief  Convert charge current to level.
     * @note   This function converts a given charge current to its corresponding charge level.
     * @retval The corresponding charge level.
     */
    uint16_t getChargeCurrentToLevelImpl() override
    {
        uint16_t current = getChargeCurrent();
        uint16_t step = pmic.getConfig().chargeCurrentStep;
        return current / step;
    }

    uint8_t _effects;
    static EventGroupHandle_t _event;
    bool _feedback_enable = false;
    uint8_t _feedback_effects = 1;
    custom_feedback_t _custom_feedback = nullptr;
    void *_custom_feedback_args = nullptr;
    uint8_t *_boot_images_addr;
    bool _enable_keyboard = true;
    uint32_t _gauge_last_update = 0;
    uint32_t _gauge_update_timestamp = 0;
    uint32_t _gauge_update_interval = 1000;
};

extern RfalNfcClass NFCReader;
extern LilyGoLoRaPager &instance;

LILYGO_DECLARE_RADIO();

#ifndef RADIOLIB_EXCLUDE_NRF24
extern nRF24 nrf24;
#endif

#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#define DEVICE_MAX_CHARGE_CURRENT   2048
#define DEVICE_MIN_CHARGE_CURRENT   128
#define DEVICE_CHARGE_LEVEL_NUMS    32
#define DEVICE_CHARGE_STEPS         64
#define DEVICE_CHARGE_CURRENT_RECOMMEND 704

#define GAUGE_CHIP_BQ27220

#endif // ARDUINO_T_LORA_PAGER
