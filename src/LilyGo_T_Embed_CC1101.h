/**
 * @file      LilyGo_T_Embed_CC1101.h
 * @brief     Board support for the LilyGo T-Embed-CC1101.
 */
#pragma once

#ifdef ARDUINO_T_EMBED_CC1101

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

#define newModule() new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY, SPI)
#define LILYGO_RADIO_REQUIRE_MODULE
#include "radio/LilyGoRadioHelper.h"

/**
 * @brief T-Embed-CC1101 board singleton and common LilyGoLib facade.
 *
 * The board has no keyboard, touch panel, or motion sensor.  Its rotary
 * encoder, PN532, CC1101, audio paths, and power devices are exposed through
 * the same interfaces used by the Factory example.
 */
class LilyGoTEmbedCC1101 : public LilyGo_Display,
    public LilyGoDispArduinoSPI,
    public LilyGoEventManage,
    public LilyGoPowerManageInf,
    public BrightnessController<LilyGoTEmbedCC1101, 0, 16, 30>
{
private:
    LilyGoTEmbedCC1101();
    ~LilyGoTEmbedCC1101() = default;
    LilyGoTEmbedCC1101(const LilyGoTEmbedCC1101 &) = delete;
    LilyGoTEmbedCC1101 &operator=(const LilyGoTEmbedCC1101 &) = delete;

public:
    GaugeBQ27220 gauge;
    PmicBQ25896 pmic;
    AW9364LedDriver backlight;
    AudioInputDev audioInput{BOARD_MIC_CLK, BOARD_MIC_DATA};
    AudioOutputDev audioOutput{BOARD_VOICE_BCLK, BOARD_VOICE_LRCLK, BOARD_VOICE_DIN};
    Adafruit_PN532 nfc{BOARD_PN532_IRQ, BOARD_PN532_RF_REST, &Wire};
    Button2 userButton{BOARD_USER_KEY};

    static LilyGoTEmbedCC1101 *getInstance()
    {
        static LilyGoTEmbedCC1101 device;
        return &device;
    }

    const char *getName();
    const LilyGoDeviceCapability &getCapability() const;
    LilyGoDeviceInitOptions getDefaultInitOptions() const;
    uint32_t begin();
    uint32_t begin(const LilyGoDeviceInitOptions &options);
    LILYGO_DEPRECATED("Use begin(const LilyGoDeviceInitOptions&) instead.")
    uint32_t begin(uint32_t disable_hw_init);
    void loop();

    uint32_t getDeviceProbe() override { return devicesProbe; }
    bool isDeviceOnline(uint32_t mask) const { return (devicesProbe & mask) != 0; }
    bool initLoRa();
    bool hasRadio() const { return devicesProbe & HW_RADIO_ONLINE; }
    bool isLoRaHardwarePresent() const { return true; }
    int16_t setRFFrequencyBand(float frequency);
#ifndef RADIOLIB_EXCLUDE_NRF24
    bool initNRF24();
#endif

    bool initNFC();
    Adafruit_PN532 &getNFC() { return nfc; }
    uint32_t getNFCFirmwareVersion() const { return nfcFirmwareVersion; }

    bool installSD(uint32_t spiFrequency = 0);
    void uninstallSD();
    bool isCardReady() const { return devicesProbe & HW_SD_ONLINE; }

    bool hasEncoder() override { return rotaryHandler != nullptr; }
    RotaryMsg_t getRotary() override;
    void clearRotaryMsg();
    void enableRotary();
    void disableRotary();
    void setRotaryStepDivider(uint8_t divider);
    uint8_t getRotaryStepDivider() const { return rotaryStepDivider; }
    uint8_t getRotaryStepDividerMin() const { return 1; }
    uint8_t getRotaryStepDividerMax() const { return 8; }

    AudioInputIf *getAudioInput() { return &audioInput; }
    AudioOutputIf *getAudioOutput() { return &audioOutput; }
    uint8_t getCodecInputChannels() const { return 1; }
    uint8_t getCodecOutputChannels() const { return 2; }

    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override;
    uint16_t width() override;
    uint16_t height() override;
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) override;
    bool needSwapColors() override { return true; }
    void setBrightness(uint8_t level);
    uint8_t getBrightness() const { return brightnessLevel; }
    void sleepDisplay();
    void wakeupDisplay();
    void powerControl(PowerCtrlChannel_t channel, bool enable);
    bool initPMU();

    bool hasGauge() override { return devicesProbe & HW_GAUGE_ONLINE; }
    bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot) override;
    bool shutdown() override;
    float getBattVoltage() override;
    float getBatteryPercent() override;
    float getTemperature() override;
    bool hasOTG() override;
    bool isOTGEnabled() override;
    bool enableOTG() override;
    bool disableOTG() override;
    void lightSleep(WakeupSource_t wakeupSource = WAKEUP_SRC_BOOT_BUTTON);
    void sleep(WakeupSource_t wakeupSource = WAKEUP_SRC_BOOT_BUTTON,
               bool offRtcBackupDomain = false, uint32_t sleepSeconds = 0);
    bool lockSPI(TickType_t ticksToWait = portMAX_DELAY);
    void unlockSPI();

private:
    uint16_t getChargeLevelToCurrentImpl(uint8_t level) override
    {
        return pmic.getConfig().chargeCurrentStep * level;
    }
    uint16_t getChargeCurrentToLevelImpl() override
    {
        const uint16_t step = pmic.getConfig().chargeCurrentStep;
        return step ? getChargeCurrent() / step : 0;
    }
    void initSharedSpiPins();
    bool initRotary();
    static void rotaryTaskEntry(void *param);
    void rotaryTaskLoop();
    bool getRotaryButtonState();
    uint32_t devicesProbe = 0;
    uint32_t nfcFirmwareVersion = 0;
    uint8_t brightnessLevel = 0;
    volatile uint8_t rotaryStepDivider = 1;
    bool initialized = false;
    Rotary rotary{ENCODER_INA, ENCODER_INB};
    QueueHandle_t rotaryMsg = nullptr;
    TaskHandle_t rotaryHandler = nullptr;
    EventGroupHandle_t rotaryTaskFlag = nullptr;
};

extern LilyGoTEmbedCC1101 &instance;
LILYGO_DECLARE_RADIO();
#ifndef RADIOLIB_EXCLUDE_NRF24
extern nRF24 nrf24;
#endif

#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#define DEVICE_MAX_CHARGE_CURRENT 2048
#define DEVICE_MIN_CHARGE_CURRENT 128
#define DEVICE_CHARGE_LEVEL_NUMS 32
#define DEVICE_CHARGE_STEPS 64
#define DEVICE_CHARGE_CURRENT_RECOMMEND 512

#endif // ARDUINO_T_EMBED_CC1101
