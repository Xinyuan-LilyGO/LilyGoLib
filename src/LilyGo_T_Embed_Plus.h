/**
 * @file      LilyGo_T_Embed_Plus.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-14
 *
 * Declares the LilyGo T-Embed-Plus board support interface.
 */
#pragma once

#ifdef ARDUINO_T_EMBED_PLUS

#include <Arduino.h>
#include <Adafruit_PN532.h>
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
#include <PmicTI.hpp>
#include <AW9364LedDriver.hpp>

/** @brief Create the RadioLib module used by the on-board CC1101. */
#define newModule() new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY, SPI)
#define LILYGO_RADIO_REQUIRE_MODULE
#include "radio/LilyGoRadioHelper.h"

/**
 * @brief T-Embed-Plus board singleton and common LilyGoLib facade.
 *
 * The board has no keyboard, touch panel, or motion sensor.  Its rotary
 * encoder, PN532, CC1101, audio paths, and power devices are exposed through
 * the same interfaces used by the Factory example.
 */
class LilyGoTEmbedPlus : public LilyGo_Display,
    public LilyGoDispArduinoSPI,
    public LilyGoEventManage,
    public LilyGoPowerManageInf,
    public BrightnessController<LilyGoTEmbedPlus, 0, 16, 30>
{
private:
    LilyGoTEmbedPlus();
    ~LilyGoTEmbedPlus() = default;
    LilyGoTEmbedPlus(const LilyGoTEmbedPlus &) = delete;
    LilyGoTEmbedPlus &operator=(const LilyGoTEmbedPlus &) = delete;

public:
    /// BQ27220 battery fuel gauge.
    GaugeBQ27220 gauge;
    /// BQ25896 battery charger and power management controller.
    PmicBQ25896 pmic;
    /// AW9364 display backlight controller with levels from 0 to 16.
    AW9364LedDriver backlight;
    /// On-board digital microphone input interface.
    AudioInputDev audioInput{BOARD_MIC_CLK, BOARD_MIC_DATA};
    /// MAX98357 I2S speaker output interface.
    AudioOutputDev audioOutput{BOARD_VOICE_BCLK, BOARD_VOICE_LRCLK, BOARD_VOICE_DIN};
    /// PN532 NFC controller.
    Adafruit_PN532 nfc{BOARD_PN532_IRQ, BOARD_PN532_RF_REST, &Wire};
    /// Side user button connected to BOARD_USER_KEY.
    Button2 userButton{BOARD_USER_KEY};

    /**
     * @brief Get the singleton board instance.
     * @return Pointer to the LilyGoTEmbedPlus instance.
     */
    static LilyGoTEmbedPlus *getInstance()
    {
        static LilyGoTEmbedPlus device;
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
     * @note FFat, audio, NFC, and rotary encoder initialization are enabled by default.
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
     * @param disable_hw_init Bitmask composed from the legacy NO_HW_* or NO_INIT_* flags.
     * @return Bitmask of hardware detected during initialization.
     * @deprecated Use begin(const LilyGoDeviceInitOptions &) instead.
     */
    LILYGO_DEPRECATED("Use begin(const LilyGoDeviceInitOptions&) instead.")
    uint32_t begin(uint32_t disable_hw_init);

    /**
     * @brief Process the side user button.
     * @note Call this function repeatedly from the application loop.
     */
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
     * @brief Initialize the on-board CC1101 radio at 315 MHz.
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
     * @return Always true for the T-Embed-Plus.
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
     * @brief Initialize an nRF24 module connected to the expansion interface.
     * @return true if the radio was detected and initialized.
     */
    bool initNRF24();
#endif

    /**
     * @brief Initialize the on-board PN532 NFC controller.
     * @return true if the controller was detected and configured.
     */
    bool initNFC();

    /**
     * @brief Get the PN532 NFC controller instance.
     * @return Reference to the PN532 controller.
     */
    Adafruit_PN532 &getNFC() { return nfc; }

    /**
     * @brief Get the cached PN532 firmware version response.
     * @return Firmware version response, or 0 when the controller is unavailable.
     */
    uint32_t getNFCFirmwareVersion() const { return nfcFirmwareVersion; }

    /**
     * @brief Mount the microSD card on the shared SPI bus.
     * @param spiFrequency SPI clock in Hz, or 0 to use the 40 MHz board default.
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
     * @brief Get the on-board microphone input interface.
     * @return Pointer to the audio input interface.
     */
    AudioInputIf *getAudioInput() { return &audioInput; }

    /**
     * @brief Get the I2S speaker output interface.
     * @return Pointer to the audio output interface.
     */
    AudioOutputIf *getAudioOutput() { return &audioOutput; }

    /**
     * @brief Get the number of microphone input channels.
     * @return One input channel.
     */
    uint8_t getCodecInputChannels() const { return 1; }

    /**
     * @brief Get the number of I2S output channels.
     * @return Two output channels.
     */
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
     * @note The CC1101, WS2812, and MAX98357 share one power rail. A request
     *       to disable POWER_RADIO leaves that shared rail enabled.
     */
    void powerControl(PowerCtrlChannel_t channel, bool enable);

    /**
     * @brief Initialize the BQ25896 charger.
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
     * @return true if PMU or external fuel-gauge data is available.
     */
    bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot) override;

    /**
     * @brief Enter BQ25896 ship mode to shut down the board.
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
     * @brief Check whether BQ25896 OTG boost control is available.
     * @return true if the BQ25896 is online.
     */
    bool hasOTG() override;

    /**
     * @brief Check whether the BQ25896 OTG boost output is enabled.
     * @return true if boost output is enabled.
     */
    bool isOTGEnabled() override;

    /**
     * @brief Enable the BQ25896 OTG boost output.
     * @return true if the PMU accepted the request.
     */
    bool enableOTG() override;

    /**
     * @brief Disable the BQ25896 OTG boost output.
     * @return true if the PMU accepted the request.
     */
    bool disableOTG() override;

    /**
     * @brief Enter light sleep using the selected button wake sources.
     * @param wakeupSource Wake source bitmask. Supported physical sources are
     *        WAKEUP_SRC_BOOT_BUTTON, WAKEUP_SRC_ROTARY_BUTTON, and WAKEUP_SRC_BUTTON.
     *
     * WAKEUP_SRC_BOOT_BUTTON and WAKEUP_SRC_ROTARY_BUTTON select the shared
     * encoder/BOOT key on GPIO0. WAKEUP_SRC_BUTTON selects the USER key on
     * GPIO6. Multiple supported button sources may be combined.
     * The display, backlight, radios, NFC, audio, and SD card are suspended and
     * restored around light sleep. The shared peripheral rail remains enabled
     * so active radio configuration is retained.
     */
    void lightSleep(WakeupSource_t wakeupSource = WAKEUP_SRC_BOOT_BUTTON);

    /**
     * @brief Enter deep sleep using selected button and/or timer wake sources.
     * @param wakeupSource Wake source bitmask. Supported sources are the three
     *        button sources accepted by lightSleep() and WAKEUP_SRC_TIMER.
     * @param offRtcBackupDomain Ignored because this board has no controllable
     *        RTC backup power domain.
     * @param sleepSeconds Timer duration in seconds. It must be nonzero when
     *        WAKEUP_SRC_TIMER is selected.
     *
     * Physical wake-source mappings are the same as lightSleep(). When
     * WAKEUP_SRC_TIMER is selected, sleepSeconds must be non-zero.
     * Before deep sleep, the display, radios, NFC, audio, SD card, shared
     * peripheral power rail, SPI bus, and I2C bus are shut down.
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
    /**
     * @brief Convert a charge-level index to charger current.
     * @param level Charge-level index.
     * @return Charge current in milliamperes.
     */
    uint16_t getChargeLevelToCurrentImpl(uint8_t level) override
    {
        return pmic.getConfig().chargeCurrentStep * level;
    }
    /**
     * @brief Convert the configured charge current to a charge-level index.
     * @return Current charge-level index.
     */
    uint16_t getChargeCurrentToLevelImpl() override
    {
        const uint16_t step = pmic.getConfig().chargeCurrentStep;
        return step ? getChargeCurrent() / step : 0;
    }
    /**
     * @brief Translate abstract wake sources to an ESP32 GPIO mask.
     * @param wakeupSource Wake source bitmask.
     * @return GPIO wake-up mask for the supported physical sources.
     */
    uint64_t getWakeupPinMask(WakeupSource_t wakeupSource) const;

    /** @brief Set all chip-select pins on the shared SPI bus inactive. */
    void initSharedSpiPins();

    /**
     * @brief Initialize the rotary encoder event queue and worker task.
     * @return true if rotary encoder event handling is ready.
     */
    bool initRotary();

    /**
     * @brief FreeRTOS rotary task entry point.
     * @param param Pointer to the LilyGoTEmbedPlus instance.
     */
    static void rotaryTaskEntry(void *param);

    /** @brief Poll the rotary encoder and enqueue decoded events. */
    void rotaryTaskLoop();

    /**
     * @brief Read and debounce the rotary encoder push button.
     * @return true while the encoder button is pressed.
     */
    bool getRotaryButtonState();

    /// Bitmask of detected and initialized peripherals.
    uint32_t devicesProbe = 0;

    /// Cached PN532 firmware version response.
    uint32_t nfcFirmwareVersion = 0;

    /// Current AW9364 backlight level.
    uint8_t brightnessLevel = 0;

    /// Number of encoder transitions required for one reported step.
    volatile uint8_t rotaryStepDivider = 1;

    /// True after the board initialization sequence has completed.
    bool initialized = false;

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
extern LilyGoTEmbedPlus &instance;

/// Declare the shared CC1101 radio instance.
LILYGO_DECLARE_RADIO();
#ifndef RADIOLIB_EXCLUDE_NRF24
/// nRF24 radio connected to the expansion interface.
extern nRF24 nrf24;
#endif

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
/// Recommended battery charge current in milliamperes (640 mA is the closest
/// supported value not exceeding half of the 1300 mAh battery capacity).
#define DEVICE_CHARGE_CURRENT_RECOMMEND 640

#endif // ARDUINO_T_EMBED_PLUS
