/**
 * @file      LilyGo_T_Deck.h
 * @brief     Board support for the original LilyGo T-Deck.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-28
 *
 */
#pragma once

#ifdef ARDUINO_T_DECK

#include <Arduino.h>
#include <AW9364LedDriver.hpp>
#include <Button2.h>
#include <SD.h>
#include <SPI.h>
#include <TouchDrv.hpp>
#include "core/LilyGoEventManage.h"
#include "core/LilyGoPowerManageInf.h"
#include "core/LilyGoTypedef.h"
#include "audio/AudioDevice.h"
#include "display/BrightnessController.h"
#include "display/LilyGoDispInterface.h"
#include "gps/GPS.h"
#include "input/TDeckKeyboard.h"

#define newModule() new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, SPI)
#define LILYGO_RADIO_REQUIRE_MODULE
#include "radio/LilyGoRadioHelper.h"

enum TDeckButtonId : uint8_t {
    BUTTON_LEFT = 0,   ///< Left front-panel button.
    BUTTON_CENTER = 1, ///< Trackball center button, shared with BOOT on GPIO0.
    BUTTON_RIGHT = 2,  ///< Right front-panel button.
};

/**
 * @brief Board support and peripheral access for the original LilyGo T-Deck.
 *
 * Call loop() regularly after begin() to dispatch keyboard, trackball, button,
 * and asynchronous GPS events.
 */
class LilyGoDeck : public LilyGo_Display,
    public LilyGoDispArduinoSPI,
    public LilyGoEventManage,
    public BrightnessController<LilyGoDeck, 0, 16, 30>
{
private:
    LilyGoDeck();
    ~LilyGoDeck() = default;
    LilyGoDeck(const LilyGoDeck &) = delete;
    LilyGoDeck &operator=(const LilyGoDeck &) = delete;

public:
    /// ES7210 microphone codec controller.
    EspCodec codec{1};
    /// Audio input interface backed by the ES7210 codec.
    AudioInputCodecDev audioInput{&codec};
    /// I2S speaker output interface.
    AudioOutputDev audioOutput{I2S_SCK, I2S_WS, I2S_SDOUT};
    /// GPIO0 BOOT button, also used by the trackball center switch.
    Button2 bootButton = Button2(TRACKBALL_CLICK);
    /// Optional GPS receiver interface.
    GPS gps;
    /// Built-in keyboard controller.
    TDeckKeyboard kb;
    /// GT911 touch controller.
    TouchDrvGT911 touch;
    /// Display backlight controller with levels from 0 to 16.
    AW9364LedDriver backlight;

    /**
     * @brief Get the singleton board instance.
     * @return Pointer to the LilyGoDeck instance.
     */
    static LilyGoDeck *getInstance()
    {
        static LilyGoDeck device;
        return &device;
    }

    /** @brief Get the board model name. */
    const char *getName();

    /** @brief Get the hardware capabilities reported by this board. */
    const LilyGoDeviceCapability &getCapability() const;

    /**
     * @brief Get the default board initialization options.
     * @note Internal flash FFat initialization is enabled by default.
     */
    LilyGoDeviceInitOptions getDefaultInitOptions() const;

    /**
     * @brief Initialize the board using the default options.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t begin();

    /**
     * @brief Initialize selected board peripherals.
     * @param initOptions Controls which supported peripherals are initialized.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t begin(const LilyGoDeviceInitOptions &initOptions);

    /**
     * @brief Initialize the board using the legacy hardware-disable mask.
     * @param disableHwInit Bitmask composed from the legacy NO_HW_* or NO_INIT_* flags.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t begin(uint32_t disableHwInit);

    /**
     * @brief Process button, keyboard, trackball, and GPS input.
     * @note Call this function repeatedly from the application loop.
     */
    void loop();

    /**
     * @brief Check whether any requested hardware bit is online.
     * @param mask Hardware probe bitmask.
     * @return true if at least one bit in mask is present.
     */
    bool isDeviceOnline(uint32_t mask) const;

    /** @brief Set the display rotation. */
    void setRotation(uint8_t rotation) override;

    /** @brief Get the current display rotation. */
    uint8_t getRotation() override;

    /** @brief Get the logical display width for the current rotation. */
    uint16_t width() override;

    /** @brief Get the logical display height for the current rotation. */
    uint16_t height() override;

    /**
     * @brief Write an RGB565 pixel block to the display.
     * @param x Left coordinate.
     * @param y Top coordinate.
     * @param width Block width in pixels.
     * @param height Block height in pixels.
     * @param color Pointer to width * height RGB565 pixels.
     */
    void pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *color) override;

    /** @brief Report whether RGB565 byte swapping is required. */
    bool needSwapColors() override;

    /**
     * @brief Set display backlight brightness.
     * @param level Brightness level from 0 (off) to 16 (maximum).
     */
    void setBrightness(uint8_t level);

    /** @brief Get the current display backlight level. */
    uint8_t getBrightness() const;

    /** @brief Turn off the backlight and place the display controller in sleep mode. */
    void sleepDisplay();

    /** @brief Wake the display controller and restore its previous nonzero brightness. */
    void wakeupDisplay();

    /**
     * @brief Initialize the GT911 touch controller.
     * @return true if the controller was detected and initialized.
     */
    bool initTouch();

    /** @brief Check whether the touch controller is online. */
    bool hasTouch() override;

    /**
     * @brief Read current touch points.
     * @param x Output array for X coordinates.
     * @param y Output array for Y coordinates.
     * @param count Capacity of each output array.
     * @return Number of points copied to the output arrays.
     */
    uint8_t getPoint(int16_t *x, int16_t *y, uint8_t count) override;

    /**
     * @brief Initialize the built-in keyboard.
     * @return true if the keyboard controller was detected and initialized.
     */
    bool initKeyboard();

    /** @brief Check whether the keyboard controller is online. */
    bool hasKeyboard() override;

    /**
     * @brief Read one buffered keyboard character.
     * @param c Destination for the character.
     * @return KEYBOARD_PRESSED on success, otherwise -1.
     */
    int getKeyChar(char *c) override;

    /** @brief Enable keyboard input and discard buffered characters. */
    void enableKeyboard();

    /** @brief Disable keyboard input and discard buffered characters. */
    void disableKeyboard();

    /**
     * @brief Initialize trackball direction and center-button event handling.
     * @return true if the event queue was created successfully.
     */
    bool initTrackball();

    /** @brief Check whether trackball event handling is initialized. */
    bool hasTrackball() const;

    /** @brief Enable dispatch of trackball direction events. */
    void enableTrackBall();

    /** @brief Disable dispatch of trackball direction events. */
    void disableTrackBall();

    /**
     * @brief Initialize the ES7210 microphone codec.
     * @return true if the codec was detected and configured.
     */
    bool initCodec();

    /**
     * @brief Initialize the I2S speaker output.
     * @return true if the output was opened successfully.
     */
    bool initAmplifier();

    /**
     * @brief Configure active ES7210 microphones and the input bus mode.
     * @param micMask Bitmask of ES7210_SEL_MIC* inputs.
     * @param forceTdm true to force TDM input mode.
     * @return true if the codec accepted the configuration.
     */
    bool configureEs7210Microphones(uint8_t micMask, bool forceTdm);

    /** @brief Get the audio input interface. */
    AudioInputIf *getAudioInput();

    /** @brief Get the audio output interface. */
    AudioOutputIf *getAudioOutput();

    /** @brief Get the number of codec input channels exposed by this board. */
    uint8_t getCodecInputChannels();

    /** @brief Get the number of audio output channels exposed by this board. */
    uint8_t getCodecOutputChannels();

    /**
     * @brief Mount the SD card on the shared SPI bus.
     * @param spiFrequency SPI clock in Hz, or 0 to use LILYGO_TDECK_SD_SPI_FREQ.
     * @return true if a card was mounted successfully.
     */
    bool installSD(uint32_t spiFrequency = 0);

    /** @brief Unmount the SD card and clear its online state. */
    void uninstallSD();

    /** @brief Check whether a mounted SD card is present. */
    bool isCardReady();

    /**
     * @brief Initialize the optional SX1262 radio.
     * @return true if the radio was detected and initialized.
     */
    bool initLoRa();

    /** @brief Check whether the optional radio is online. */
    bool hasRadio() const;

    /**
     * @brief Start asynchronous probing of the optional GPS receiver.
     * @return true if the probe was started.
     * @note Call loop() to feed received data and complete the probe.
     */
    bool initGPS();

    /** @brief Check whether asynchronous GPS probing found a receiver. */
    bool hasGPS();

    /**
     * @brief Get the current hardware probe mask.
     * @note Also updates the GPS bit after asynchronous probing completes.
     */
    uint32_t getDeviceProbe();

    /**
     * @brief Read the battery voltage through the board ADC divider.
     * @return Estimated battery voltage in millivolts.
     */
    float getBattVoltage();

    /** @brief Estimate battery charge percentage from the measured voltage. */
    float getBatteryPercent();

    /**
     * @brief Populate a generic power snapshot using ADC battery and inferred USB data.
     * @param snapshot Destination snapshot.
     * @return true when a valid battery voltage was read.
     */
    bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot);

    /** @brief Return PMIC_TYPE_UNKNOWN because the original T-Deck has no PMIC. */
    PmicType getPmicType() const;

    /** @brief Return false because OTG control is not available on this board. */
    bool hasOTG();

    /** @brief Return false because OTG control is not available on this board. */
    bool isOTGEnabled();

    /** @brief Compatibility stub; always returns false because OTG is unsupported. */
    bool enableOTG();

    /** @brief Compatibility stub; always returns false because OTG is unsupported. */
    bool disableOTG();

    /**
     * @brief Infer USB adapter presence from the ADC voltage.
     * @return true when the measured voltage is greater than 4200 mV.
     */
    bool isAdapterConnected();

    /** @brief Compatibility stub; always returns false because PMIC shutdown is unsupported. */
    bool shutdown();

    /** @brief Return false because charger control is unavailable on this board. */
    bool isEnableCharge();

    /** @brief Compatibility stub; always returns false because charger control is unsupported. */
    bool enableCharge();

    /** @brief Compatibility stub; always returns false because charger control is unsupported. */
    bool disableCharge();

    /** @brief Return 0 because charge-current reporting is unavailable. */
    uint16_t getChargeCurrent();

    /**
     * @brief Compatibility stub for unsupported charge-current control.
     * @param milliampere Requested current; ignored on this board.
     */
    void setChargeCurrent(uint16_t milliampere);

    /**
     * @brief Get the unsupported charger configuration as four zero values.
     * @param minimum Receives 0.
     * @param maximum Receives 0.
     * @param step Receives 0.
     * @param steps Receives 0.
     */
    void getChargeConfig(uint16_t &minimum, uint16_t &maximum, uint16_t &step, uint16_t &steps);

    /**
     * @brief Compatibility stub for charge-level conversion.
     * @param level Requested charge level; ignored.
     * @return Always 0 because charger control is unsupported.
     */
    uint16_t getChargeLevelToCurrent(uint8_t level);

    /** @brief Return 0 because charge-level reporting is unavailable. */
    uint16_t getChargeCurrentToLevel();

    /**
     * @brief Enter light sleep and resume after the GPIO0 button is pressed.
     * @param wakeupSource Must include WAKEUP_SRC_BOOT_BUTTON.
     *
     * GPIO0 is shared by the BOOT button and trackball center switch. The
     * default and only supported light-sleep wake source is therefore both
     * physical controls. Display, keyboard, touch, radio, and audio state is
     * restored before this function returns.
     */
    void lightSleep(WakeupSource_t wakeupSource = WAKEUP_SRC_BOOT_BUTTON);

    /**
     * @brief Enter deep sleep.
     * @param wakeupSource Wake source bitmask. WAKEUP_SRC_BOOT_BUTTON is the
     *        only supported physical source; WAKEUP_SRC_TIMER may be used alone
     *        or combined with it.
     * @param offRtcBackupDomain Ignored because this board has no controllable
     *        RTC backup power domain.
     * @param sleepSeconds Timer duration in seconds. It must be nonzero when
     *        WAKEUP_SRC_TIMER is selected.
     *
     * GPIO0 is shared by the BOOT button and trackball center switch, so either
     * physical control wakes the board. The default deep-sleep wake source is
     * GPIO0. Deep sleep resets the ESP32-S3 and this function does not return.
     */
    void sleep(WakeupSource_t wakeupSource = static_cast<WakeupSource_t>(
                   WAKEUP_SRC_BOOT_BUTTON),
               bool offRtcBackupDomain = false, uint32_t sleepSeconds = 0);

    /**
     * @brief Acquire the mutex for the display, SD card, and radio SPI bus.
     * @param ticksToWait Maximum FreeRTOS ticks to wait.
     * @return true if the mutex was acquired.
     */
    bool lockSPI(TickType_t ticksToWait = portMAX_DELAY);

    /** @brief Release the shared SPI bus mutex. */
    void unlockSPI();

private:
    static void gpsProbeCallback(bool success, const char *model, void *userData);
    uint64_t getWakeupPinMask(WakeupSource_t wakeupSource) const;

    static EventGroupHandle_t eventGroup;
    uint32_t devicesProbe = 0;
    uint8_t brightnessBeforeSleep = 16;
    bool amplifierInitialized = false;
    bool keyboardEnabled = true;
    bool trackballEnabled = true;
};

extern LilyGoDeck &instance;

LILYGO_DECLARE_RADIO();

#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0

#endif // ARDUINO_T_DECK
