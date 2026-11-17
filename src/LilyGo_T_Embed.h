/**
 * @file      LilyGo_T_Embed.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @brief     Board support for the original LilyGo T-Embed.
 * @date      2026-11-17
 *
 */
#pragma once

#ifdef ARDUINO_T_EMBED

#include <Arduino.h>
#include <SD.h>
#include <SI4735.h>
#include <SPI.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "audio/AudioDevice.h"
#include "core/LilyGoEventManage.h"
#include "core/LilyGoPowerManageInf.h"
#include "core/LilyGoTypedef.h"
#include "display/BrightnessController.h"
#include "display/LilyGoDispInterface.h"
#include "rotary/Rotary.h"

#include <AW9364LedDriver.hpp>

/**
 * @brief Original T-Embed board singleton and common LilyGoLib facade.
 *
 * The board exposes its ST7789 display, rotary encoder, AW9364 backlight,
 * ES7210 microphone inputs, MAX98357 audio output, microSD interface, APA102
 * pixels, and optional SI473x receiver through the interfaces used by the
 * LilyGoLib examples.
 */
class LilyGoTEmbed : public LilyGo_Display,
    public LilyGoDispArduinoSPI,
    public LilyGoEventManage,
    public BrightnessController<LilyGoTEmbed, 0, 16, 30>
{
private:
    /** @brief Construct the singleton board instance. */
    LilyGoTEmbed();
    ~LilyGoTEmbed() = default;
    LilyGoTEmbed(const LilyGoTEmbed &) = delete;
    LilyGoTEmbed &operator=(const LilyGoTEmbed &) = delete;

public:
    /// AW9364 display backlight controller with levels from 0 to 16.
    AW9364LedDriver backlight;
    /// ES7210 microphone codec controller on I2S port 1.
    EspCodec codec{1};
    /// ES7210 microphone input interface.
    AudioInputCodecDev audioInput{&codec};
    /// MAX98357 I2S speaker output interface.
    AudioOutputDev audioOutput{I2S_SCK, I2S_WS, I2S_SDOUT};
    /// Optional SI473x broadcast receiver connected to the shared I2C bus.
    SI4735 si4735;

    /**
     * @brief Get the singleton board instance.
     * @return Pointer to the LilyGoTEmbed instance.
     */
    static LilyGoTEmbed *getInstance()
    {
        static LilyGoTEmbed device;
        return &device;
    }

    /**
     * @brief Get the board model name.
     * @return Null-terminated board model name.
     */
    const char *getName();

    /**
     * @brief Get the hardware capabilities reported by this board.
     * @return Constant reference to the board capability descriptor.
     */
    const LilyGoDeviceCapability &getCapability() const;

    /**
     * @brief Get the default board initialization options.
     * @return Default peripheral initialization options for this board.
     * @note Filesystem, audio, microSD, rotary encoder, and optional SI473x
     *       probing are enabled by default.
     */
    LilyGoDeviceInitOptions getDefaultInitOptions() const;

    /**
     * @brief Initialize the board using the default options.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t begin();

    /**
     * @brief Initialize selected board peripherals.
     * @param options Controls which supported peripherals are initialized.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t begin(const LilyGoDeviceInitOptions &options);

    /**
     * @brief Initialize the board using the legacy hardware-disable mask.
     * @param disableHwInit Bitmask composed from legacy NO_HW_* or NO_INIT_* flags.
     * @return Bitmask of hardware detected during initialization.
     * @deprecated Use begin(const LilyGoDeviceInitOptions &) instead.
     */
    LILYGO_DEPRECATED("Use begin(const LilyGoDeviceInitOptions&) instead.")
    uint32_t begin(uint32_t disableHwInit);

    /** @brief Process periodic board work. The original T-Embed needs no polling. */
    void loop() {}

    /**
     * @brief Get the current hardware probe mask.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t getDeviceProbe() { return devicesProbe; }

    /**
     * @brief Check whether any requested hardware bit is online.
     * @param mask Hardware probe bitmask.
     * @return true if at least one bit in @p mask is present.
     */
    bool isDeviceOnline(uint32_t mask) const { return (devicesProbe & mask) != 0; }

    /**
     * @brief Initialize the ES7210 microphone codec.
     * @return true if the codec was detected and initialized.
     */
    bool initCodec();

    /**
     * @brief Initialize the MAX98357 I2S audio output.
     * @return true if the output interface was initialized and opened.
     */
    bool initAmplifier();

    /**
     * @brief Probe the optional SI473x receiver on the shared I2C bus.
     * @return true if an SI473x receiver was detected.
     */
    bool initSi4735();

    /**
     * @brief Check whether the optional SI473x receiver is powered and online.
     * @return true if the receiver is available and currently powered.
     */
    bool si4735Enabled() const { return si4735Powered && isDeviceOnline(HW_SI473X_ONLINE); }

    /**
     * @brief Select active ES7210 microphone inputs and serial output mode.
     * @param micMask Bitmask composed from ES7210_SEL_MIC1 through ES7210_SEL_MIC4.
     * @param forceTdm true to keep the four-slot TDM stream active.
     * @return true if the codec accepted the configuration.
     */
    bool configureEs7210Microphones(uint8_t micMask, bool forceTdm);

    /**
     * @brief Get the ES7210 microphone input interface.
     * @return Pointer to the audio input interface.
     */
    AudioInputIf *getAudioInput() { return &audioInput; }

    /**
     * @brief Get the MAX98357 speaker output interface.
     * @return Pointer to the audio output interface.
     */
    AudioOutputIf *getAudioOutput() { return &audioOutput; }

    /**
     * @brief Get the default number of microphone input channels.
     * @return Two input channels.
     */
    uint8_t getCodecInputChannels() const { return 2; }

    /**
     * @brief Get the number of I2S speaker output channels.
     * @return Two output channels.
     */
    uint8_t getCodecOutputChannels() const { return 2; }

    /**
     * @brief Mount the microSD card on its dedicated SPI bus.
     * @param spiFrequency SPI clock in Hz, or 0 to use the board default.
     * @return true if a card was mounted successfully.
     */
    bool installSD(uint32_t spiFrequency = 0);

    /** @brief Unmount the microSD card and clear its online state. */
    void uninstallSD();

    /**
     * @brief Check whether a microSD card is mounted.
     * @return true if the card was mounted successfully.
     */
    bool isCardReady() const { return isDeviceOnline(HW_SD_ONLINE); }

    /**
     * @brief Check whether rotary encoder event handling is initialized.
     * @return true if the rotary encoder worker task is available.
     */
    bool hasEncoder() override { return rotaryHandler != nullptr; }

    /**
     * @brief Read one rotary encoder event.
     * @return The next queued event, or the retained button state if no event is
     *         received within 50 milliseconds.
     */
    RotaryMsg_t getRotary() override;

    /** @brief Discard all queued rotary encoder events. */
    void clearRotaryMsg();

    /** @brief Resume rotary encoder event handling. */
    void enableRotary();

    /** @brief Suspend rotary encoder event handling. */
    void disableRotary();

    /**
     * @brief Set the number of encoder transitions required for one reported step.
     * @param divider Step divider clamped to the range 1 to 8.
     */
    void setRotaryStepDivider(uint8_t divider);

    /**
     * @brief Get the current rotary encoder step divider.
     * @return Current divider in the range 1 to 8.
     */
    uint8_t getRotaryStepDivider() const { return rotaryStepDivider; }

    /**
     * @brief Get the minimum rotary encoder step divider.
     * @return Minimum divider value.
     */
    uint8_t getRotaryStepDividerMin() const { return 1; }

    /**
     * @brief Get the maximum rotary encoder step divider.
     * @return Maximum divider value.
     */
    uint8_t getRotaryStepDividerMax() const { return 8; }

    /**
     * @brief Set the display rotation.
     * @param rotation Rotation index supported by the display driver.
     */
    void setRotation(uint8_t rotation) override;

    /**
     * @brief Get the current display rotation.
     * @return Current rotation index.
     */
    uint8_t getRotation() override;

    /**
     * @brief Get the logical display width for the current rotation.
     * @return Display width in pixels.
     */
    uint16_t width() override;

    /**
     * @brief Get the logical display height for the current rotation.
     * @return Display height in pixels.
     */
    uint16_t height() override;

    /**
     * @brief Write an RGB565 pixel block to the display.
     * @param x Left coordinate.
     * @param y Top coordinate.
     * @param width Block width in pixels.
     * @param height Block height in pixels.
     * @param color Pointer to @p width multiplied by @p height RGB565 pixels.
     */
    void pushColors(uint16_t x, uint16_t y, uint16_t width,
                    uint16_t height, uint16_t *color) override;

    /**
     * @brief Check whether RGB565 byte swapping is required.
     * @return Always true for this display interface.
     */
    bool needSwapColors() override { return true; }

    /**
     * @brief Set the AW9364 display backlight brightness.
     * @param level Brightness level from 0 (off) to 16 (maximum).
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Get the current display backlight level.
     * @return Current brightness level in the range 0 to 16.
     */
    uint8_t getBrightness() const { return brightnessLevel; }

    /** @brief Turn off the backlight and place the display controller in sleep mode. */
    void sleepDisplay();

    /** @brief Wake the display controller and restore the previous backlight level. */
    void wakeupDisplay();

    /**
     * @brief Control a supported board peripheral power function.
     * @param channel Peripheral power channel to control.
     * @param enable true to enable the channel, false to disable it.
     * @note BOARD_POWERON is a shared rail and is not changed by this method.
     */
    void powerControl(PowerCtrlChannel_t channel, bool enable);

    /**
     * @brief Read the battery voltage from the ADC divider.
     * @return Estimated battery voltage in millivolts.
     */
    float getBattVoltage();

    /**
     * @brief Estimate the battery state of charge from its voltage.
     * @return Estimated battery state of charge as a percentage.
     */
    float getBatteryPercent();

    /**
     * @brief Read the estimated battery and board power state.
     * @param snapshot Destination power snapshot.
     * @return true if a valid ADC battery voltage was read.
     */
    bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot);

    /**
     * @brief Get the board PMIC type.
     * @return PMIC_TYPE_UNKNOWN because the board has no PMIC controller.
     */
    PmicType getPmicType() const { return PMIC_TYPE_UNKNOWN; }

    /**
     * @brief Check whether OTG boost is supported.
     * @return Always false.
     */
    bool hasOTG() { return false; }

    /**
     * @brief Check whether OTG boost is enabled.
     * @return Always false.
     */
    bool isOTGEnabled() { return false; }

    /**
     * @brief Try to enable OTG boost.
     * @return Always false.
     */
    bool enableOTG() { return false; }

    /**
     * @brief Try to disable OTG boost.
     * @return Always false.
     */
    bool disableOTG() { return false; }

    /**
     * @brief Check whether an external power adapter is detected.
     * @return Always false.
     */
    bool isAdapterConnected() { return false; }

    /**
     * @brief Check whether the battery is charging.
     * @return Always false.
     */
    bool isCharging() { return false; }

    /**
     * @brief Check whether a battery appears to be connected.
     * @return true if the measured battery voltage is above 2500 millivolts.
     */
    bool isBatteryConnect() { return getBattVoltage() > 2500.0f; }

    /**
     * @brief Request a board shutdown.
     * @return false because this board has no software-controlled shutdown IC.
     */
    bool shutdown();

    /**
     * @brief Enter light sleep and wake from the encoder/BOOT button.
     * @param wakeupSource Wake source bitmask. WAKEUP_SRC_BOOT_BUTTON and
     *        WAKEUP_SRC_ROTARY_BUTTON map to the encoder button on GPIO0.
     * @note The display sleeps while BOARD_POWERON remains enabled so the
     *       codec, amplifier, pixels, and microSD card retain power.
     */
    void lightSleep(WakeupSource_t wakeupSource = WAKEUP_SRC_ROTARY_BUTTON);

    /**
     * @brief Enter deep sleep using the encoder button and/or timer wake source.
     * @param wakeupSource Wake source bitmask. Supported sources are
     *        WAKEUP_SRC_BOOT_BUTTON, WAKEUP_SRC_ROTARY_BUTTON, and WAKEUP_SRC_TIMER.
     * @param offRtcBackupDomain Ignored because this board has no controllable
     *        RTC backup power domain.
     * @param sleepSeconds Timer duration in seconds. It must be nonzero when
     *        WAKEUP_SRC_TIMER is selected.
     * @note Deep sleep turns off BOARD_POWERON after the display enters sleep.
     *       Wake-up resets the ESP32-S3 and begin() initializes the board again.
     */
    void sleep(WakeupSource_t wakeupSource = WAKEUP_SRC_ROTARY_BUTTON,
               bool offRtcBackupDomain = false, uint32_t sleepSeconds = 0);

    /**
     * @brief Acquire the display SPI bus mutex.
     * @param ticksToWait Maximum FreeRTOS ticks to wait.
     * @return true if the mutex was acquired.
     */
    bool lockSPI(TickType_t ticksToWait = portMAX_DELAY);

    /** @brief Release the display SPI bus mutex. */
    void unlockSPI();

private:
    /**
     * @brief Initialize the rotary encoder event queue and worker task.
     * @return true if rotary encoder event handling is ready.
     */
    bool initRotary();

    /**
     * @brief FreeRTOS rotary task entry point.
     * @param param Pointer to the LilyGoTEmbed instance.
     */
    static void rotaryTaskEntry(void *param);

    /** @brief Poll the rotary encoder and enqueue decoded events. */
    void rotaryTaskLoop();

    /**
     * @brief Read and debounce the rotary encoder push button.
     * @return true while the encoder button is pressed.
     */
    bool getRotaryButtonState();

    /**
     * @brief Translate abstract wake sources to an ESP32 GPIO mask.
     * @param wakeupSource Wake source bitmask.
     * @return GPIO wake-up mask for supported physical sources.
     */
    uint64_t getWakeupPinMask(WakeupSource_t wakeupSource) const;

    /// Dedicated microSD SPI controller.
    SPIClass sdSpi{FSPI};
    /// Bitmask of detected and initialized peripherals.
    uint32_t devicesProbe = 0;
    /// Current AW9364 backlight level.
    uint8_t brightnessLevel = 0;
    /// Last nonzero brightness level restored after display sleep.
    uint8_t brightnessBeforeSleep = 16;
    /// Number of encoder transitions required for one reported step.
    volatile uint8_t rotaryStepDivider = 1;
    /// True after the board initialization sequence has completed.
    bool initialized = false;
    /// True while the MAX98357 audio output interface is open.
    bool amplifierInitialized = false;
    /// True after the optional SI473x receiver has been powered up.
    bool si4735Powered = false;
    /// Rotary encoder state decoder.
    Rotary rotary{ENCODER_INA, ENCODER_INB};
    /// Queue used to deliver rotary encoder events.
    QueueHandle_t rotaryMsg = nullptr;
    /// Rotary encoder worker task handle.
    TaskHandle_t rotaryHandler = nullptr;
    /// Event flags used to suppress a held button after task resume.
    EventGroupHandle_t rotaryTaskFlag = nullptr;
};

/// Global singleton board instance.
extern LilyGoTEmbed &instance;

/// Maximum AW9364 display backlight level.
#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
/// Minimum AW9364 display backlight level.
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0

#endif // ARDUINO_T_EMBED
