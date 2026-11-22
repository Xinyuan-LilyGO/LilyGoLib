/**
 * @file      LilyGo_T_Embed_Plus_V2.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-20
 *
 * Declares the independent LilyGo T-Embed-Plus V2 board interface.
 */
#pragma once

#if defined(ARDUINO_T_EMBED_PLUS_V2)

#include <Arduino.h>
#include <IoExpanderDrv.hpp>
#include "nfc/nfc_include.h"
#include <RadioLib.h>
#include <SD.h>
#include <SPI.h>
#include <Button2.h>
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

#include <GaugeDrv.hpp>
#include <PmicSilergy.hpp>
#include <AW9364LedDriver.hpp>

/** @brief Create the RadioLib module used by the on-board CC1101. */
#define newModule() new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY, SPI)
/** @brief Require the board-specific RadioLib module declaration. */
#define LILYGO_RADIO_REQUIRE_MODULE
#include "radio/LilyGoRadioHelper.h"

/**
 * @brief LilyGo T-Embed-Plus V2 singleton and board interface.
 *
 * V2 uses an XL9555 GPIO expander for board power and reset control, an
 * ST25R3916 NFC controller on the display's shared SPI bus, and a SY6970
 * charger with a BQ27220 fuel gauge. The public interface follows the other
 * LilyGoLib board classes while keeping the V2 implementation independent.
 */
class LilyGoTEmbedPlus : public LilyGo_Display,
    public LilyGoDispArduinoSPI,
    public LilyGoEventManage,
    public LilyGoPowerManageInf,
    public BrightnessController<LilyGoTEmbedPlus, 0, 16, 30>
{
private:
    /** @brief Construct the singleton board object. */
    LilyGoTEmbedPlus();
    /** @brief Destroy the singleton board object. */
    ~LilyGoTEmbedPlus() = default;
    /** @brief Disable copying of the singleton board object. */
    LilyGoTEmbedPlus(const LilyGoTEmbedPlus &) = delete;
    /** @brief Disable assignment of the singleton board object. */
    LilyGoTEmbedPlus &operator=(const LilyGoTEmbedPlus &) = delete;

public:
    /// BQ27220 battery fuel gauge.
    GaugeBQ27220 gauge;
    /// SY6970 battery charger and power management controller.
    PmicSY6970 pmic;
    /// XL9555 GPIO expander used for board power, reset, and RF controls.
    IoExpanderXL9555 io;
    /// AW9364 display backlight controller with levels from 0 to 16.
    AW9364LedDriver backlight;
    /// On-board digital microphone input interface.
    AudioInputDev audioInput{MIC_I2S_SCK, MIC_I2S_SDIN};
    /// MAX98357 I2S speaker output interface.
    AudioOutputDev audioOutput{I2S_BCLK, I2S_LRCLK, I2S_DIN};
    /// Side user button connected to USER_KEY.
    Button2 userButton{USER_KEY};

    /**
     * @brief Get the singleton board instance.
     * @return Pointer to the LilyGoTEmbedPlus instance.
     */
    static LilyGoTEmbedPlus *getInstance()
    {
        static LilyGoTEmbedPlus device;
        return &device;
    }

    /** @brief Get the board model name. */
    const char *getName();

    /**
     * @brief Get the hardware capabilities reported by this board.
     * @return Constant reference to the board capability descriptor.
     */
    const LilyGoDeviceCapability &getCapability() const;

    /**
     * @brief Get the default board initialization options.
     * @return Default peripheral initialization options for this board.
     * @note FFat, audio, ST25R3916 NFC, and rotary encoder initialization are
     *       enabled by default.
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
     * @param disable_hw_init Bitmask composed from legacy NO_HW_* or NO_INIT_*
     *        flags.
     * @return Bitmask of hardware detected during initialization.
     * @deprecated Use begin(const LilyGoDeviceInitOptions &) instead.
     */
    LILYGO_DEPRECATED("Use begin(const LilyGoDeviceInitOptions&) instead.")
    uint32_t begin(uint32_t disable_hw_init);

    /** @brief Process the side user button and rotary input state. */
    void loop();

    /**
     * @brief Get the current hardware probe mask.
     * @return Bitmask of hardware detected during initialization.
     */
    uint32_t getDeviceProbe() override { return devicesProbe; }

    /**
     * @brief Check whether any requested hardware bit is online.
     * @param mask Hardware probe bitmask.
     * @return true if at least one bit in mask is present.
     */
    bool isDeviceOnline(uint32_t mask) const { return (devicesProbe & mask) != 0; }

    /**
     * @brief Initialize the on-board CC1101 radio.
     * @return true if the radio was detected and initialized.
     */
    bool initLoRa();

    /**
     * @brief Check whether the on-board CC1101 radio is online.
     * @return true if the CC1101 was initialized successfully.
     */
    bool hasRadio() const { return devicesProbe & HW_RADIO_ONLINE; }

    /**
     * @brief Check whether CC1101 hardware is fitted to this board.
     * @return Always true for the T-Embed-Plus V2.
     */
    bool isLoRaHardwarePresent() const { return true; }

    /**
     * @brief Select the RF path and set the CC1101 carrier frequency.
     * @param frequency Carrier frequency in MHz.
     * @return RadioLib status code; RADIOLIB_ERR_NONE indicates success.
     */
    int16_t setRFFrequencyBand(float frequency);
#ifndef RADIOLIB_EXCLUDE_NRF24
    /**
     * @brief Initialize the on-board nRF24 radio.
     * @return true if the radio was detected and initialized.
     */
    bool initNRF24();
#endif

    /**
     * @brief Initialize the on-board ST25R3916 NFC controller.
     * @return true if the controller was detected and configured.
     * @note The controller uses NFC_CS/NFC_INT on the display's shared SPI bus.
     */
    bool initNFC();

    /**
     * @brief Mount the microSD card on the shared SPI bus.
     * @param spiFrequency SPI clock in Hz, or 0 to use the board default.
     * @return true if a card was mounted successfully.
     */
    bool installSD(uint32_t spiFrequency = 0);

    /** @brief Unmount the microSD card and clear its online state. */
    void uninstallSD();

    /**
     * @brief Check whether a mounted microSD card is present.
     * @return true if the card was mounted successfully.
     */
    bool isCardReady() const { return devicesProbe & HW_SD_ONLINE; }

    /**
     * @brief Check whether rotary encoder event handling is initialized.
     * @return true if the rotary encoder worker task is available.
     */
    bool hasEncoder() override { return rotaryHandler != nullptr; }

    /**
     * @brief Read one rotary encoder event.
     * @return The next queued event, or the retained button state when no event
     *         is received within the input polling interval.
     */
    RotaryMsg_t getRotary() override;

    /** @brief Discard all queued rotary encoder events. */
    void clearRotaryMsg();

    /** @brief Resume rotary encoder event handling. */
    void enableRotary();

    /** @brief Suspend rotary encoder event handling. */
    void disableRotary();

    /**
     * @brief Set the number of encoder transitions required for one step.
     * @param divider Step divider clamped to the range 1 to 8.
     */
    void setRotaryStepDivider(uint8_t divider);

    /**
     * @brief Get the current rotary encoder step divider.
     * @return Current divider in the range 1 to 8.
     */
    uint8_t getRotaryStepDivider() const { return rotaryStepDivider; }

    /** @return Minimum supported rotary encoder step divider, 1. */
    uint8_t getRotaryStepDividerMin() const { return 1; }

    /** @return Maximum supported rotary encoder step divider, 8. */
    uint8_t getRotaryStepDividerMax() const { return 8; }

    /**
     * @brief Get the on-board microphone input interface.
     * @return Pointer to the audio input interface.
     */
    AudioInputIf *getAudioInput() { return &audioInput; }

    /**
     * @brief Get the I2S speaker output interface.
     * @return Pointer to the audio output interface.
     */
    AudioOutputIf *getAudioOutput() { return &audioOutput; }

    /** @return Number of microphone input channels, one. */
    uint8_t getCodecInputChannels() const { return 1; }

    /** @return Number of I2S output channels, two. */
    uint8_t getCodecOutputChannels() const { return 2; }

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
     * @param x1 Left coordinate.
     * @param y1 Top coordinate.
     * @param x2 Block width in pixels.
     * @param y2 Block height in pixels.
     * @param color Pointer to x2 * y2 RGB565 pixels.
     */
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) override;

    /**
     * @brief Check whether RGB565 byte swapping is required.
     * @return Always true for this display interface.
     */
    bool needSwapColors() override { return true; }

    /**
     * @brief Set the display backlight brightness.
     * @param level Brightness level from 0 (off) to 16 (maximum).
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Get the current display backlight level.
     * @return Current brightness level in the range 0 to 16.
     */
    uint8_t getBrightness() const { return brightnessLevel; }

    /** @brief Place the display controller in sleep mode. */
    void sleepDisplay();

    /** @brief Wake the display controller from sleep mode. */
    void wakeupDisplay();

    /**
     * @brief Control a board peripheral power channel.
     * @param channel Peripheral power channel to control.
     * @param enable true to enable the channel, false to disable it.
     */
    void powerControl(PowerCtrlChannel_t channel, bool enable);

    /**
     * @brief Initialize the SY6970 charger.
     * @return true if the charger was detected and configured.
     */
    bool initPMU();

    /**
     * @brief Check whether the BQ27220 fuel gauge is online.
     * @return true if the fuel gauge was initialized successfully.
     */
    bool hasGauge() override { return devicesProbe & HW_GAUGE_ONLINE; }

    /**
     * @brief Read the current battery and power state.
     * @param snapshot Destination power snapshot.
     * @return true if PMU or fuel-gauge data is available.
     */
    bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot) override;

    /**
     * @brief Enter SY6970 ship mode to shut down the board.
     * @return true if ship mode was enabled successfully.
     * @note Shutdown is rejected while VBUS is present.
     */
    bool shutdown() override;

    /**
     * @brief Read the battery voltage from the BQ27220.
     * @return Battery voltage in millivolts.
     */
    float getBattVoltage() override;

    /**
     * @brief Read the battery state of charge from the BQ27220.
     * @return Battery state of charge as a percentage.
     */
    float getBatteryPercent() override;

    /**
     * @brief Read the battery temperature from the BQ27220.
     * @return Battery temperature in degrees Celsius.
     */
    float getTemperature() override;

    /**
     * @brief Check whether SY6970 OTG boost control is available.
     * @return true if the SY6970 is online.
     */
    bool hasOTG() override;

    /**
     * @brief Check whether the SY6970 OTG boost output is enabled.
     * @return true if boost output is enabled.
     */
    bool isOTGEnabled() override;

    /**
     * @brief Enable the SY6970 OTG boost output.
     * @return true if the PMU accepted the request.
     */
    bool enableOTG() override;

    /**
     * @brief Disable the SY6970 OTG boost output.
     * @return true if the PMU accepted the request.
     */
    bool disableOTG() override;

    /**
     * @brief Enter light sleep using the selected button wake sources.
     * @param wakeupSource Wake source bitmask for supported board buttons.
     */
    void lightSleep(WakeupSource_t wakeupSource = WAKEUP_SRC_BOOT_BUTTON);

    /**
     * @brief Enter deep sleep using selected button and/or timer wake sources.
     * @param wakeupSource Wake source bitmask.
     * @param offRtcBackupDomain Ignored; V2 has no controllable RTC backup rail.
     * @param sleepSeconds Timer duration in seconds when timer wake is selected.
     */
    void sleep(WakeupSource_t wakeupSource = WAKEUP_SRC_BOOT_BUTTON,
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
    /** @brief Convert a charge-level index to charger current. */
    uint16_t getChargeLevelToCurrentImpl(uint8_t level) override
    {
        return pmic.getConfig().chargeCurrentStep * level;
    }
    /** @brief Convert configured charge current to a charge-level index. */
    uint16_t getChargeCurrentToLevelImpl() override
    {
        const uint16_t step = pmic.getConfig().chargeCurrentStep;
        return step ? getChargeCurrent() / step : 0;
    }
    /** @brief Translate abstract wake sources to an ESP32 GPIO mask. */
    uint64_t getWakeupPinMask(WakeupSource_t wakeupSource) const;
    /** @brief Set all non-display chip-select pins inactive on the shared SPI bus. */
    void initSharedSpiPins();
    /** @brief Initialize the XL9555 and board power/reset outputs. */
    bool initIoExpander(const LilyGoDeviceInitOptions &options);
    /** @brief Select the CC1101 RF path through the XL9555. */
    void setRfSwitch(uint8_t sw1, uint8_t sw0);
    /** @brief Initialize the rotary encoder event queue and worker task. */
    bool initRotary();
    /** @brief FreeRTOS rotary task entry point. */
    static void rotaryTaskEntry(void *param);
    /** @brief Poll the rotary encoder and enqueue decoded events. */
    void rotaryTaskLoop();
    /** @brief Read and debounce the rotary encoder push button. */
    bool getRotaryButtonState();

    /// Bitmask of detected and initialized peripherals.
    uint32_t devicesProbe = 0;
    /// Current AW9364 backlight level.
    uint8_t brightnessLevel = 0;
    /// Number of encoder transitions required for one reported step.
    volatile uint8_t rotaryStepDivider = 1;
    /// True after the board initialization sequence has completed.
    bool initialized = false;
    /// Rotary encoder state decoder.
    Rotary rotary{ROTARY_A, ROTARY_B};
    /// Queue used to deliver rotary encoder events.
    QueueHandle_t rotaryMsg = nullptr;
    /// Rotary encoder worker task handle.
    TaskHandle_t rotaryHandler = nullptr;
    /// Event flags used to suppress a held button after task resume.
    EventGroupHandle_t rotaryTaskFlag = nullptr;
};

/// Global singleton board instance.
extern LilyGoTEmbedPlus &instance;
/** @brief Declare the shared CC1101 radio instance. */
LILYGO_DECLARE_RADIO();
#ifndef RADIOLIB_EXCLUDE_NRF24
/// On-board nRF24 radio.
extern nRF24 nrf24;
#endif
/// ST25R3916 NFC controller on the display's shared SPI bus.
extern RfalNfcClass NFCReader;

/// Maximum display backlight level.
#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
/// Minimum display backlight level.
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
/// Maximum supported battery charge current in milliamperes.
#define DEVICE_MAX_CHARGE_CURRENT 2048
/// Minimum supported battery charge current in milliamperes.
#define DEVICE_MIN_CHARGE_CURRENT 128
/// Number of selectable battery charge-current levels.
#define DEVICE_CHARGE_LEVEL_NUMS 32
/// Battery charge-current step size in milliamperes.
#define DEVICE_CHARGE_STEPS 64
/// Recommended battery charge current for the 1300 mAh battery.
#define DEVICE_CHARGE_CURRENT_RECOMMEND 640

#endif
