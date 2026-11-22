/**
 * @file      LilyGo_T_Embed_Plus_V2.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-20
 *
 * Implements LilyGo T-Embed-Plus V2 board support and power management.
 */

#if defined(ARDUINO_T_EMBED_PLUS_V2)

#include "LilyGo_T_Embed_Plus_V2.h"
#include "LilyGoLog.h"
#include "core/LilyGoGeneral.h"
#include <SensorWireHelper.h>
#include "radio/LilyGoRadioHelper.h"
#include <Preferences.h>

#ifndef LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY
#define LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY 1300U
#endif

#ifndef LILYGO_T_EMBED_PLUS_GAUGE_FULL_CHARGE_CAPACITY
#define LILYGO_T_EMBED_PLUS_GAUGE_FULL_CHARGE_CAPACITY 1300U
#endif

#ifndef LILYGO_T_EMBED_PLUS_GAUGE_CONFIG_VERSION
#define LILYGO_T_EMBED_PLUS_GAUGE_CONFIG_VERSION 1U
#endif

namespace {

static const CommandTable_t st7789Init[] = {
    {0x11, {0}, 0 | 0x80},
    {0x13, {0}, 0},
    {0x3A, {0x05}, 1},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x28}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x1F}, 1},
    {0xC6, {0x13}, 1},
    {0xD0, {0xA7}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xD6, {0xA1}, 1},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
    {0x21, {0}, 0},
    {0x29, {0}, 0x80}
};

static const DispRotationConfig_t rotationConfig[] = {
    {0xA0, 320, 170, 0, 35},
    {0x00, 170, 320, 35, 0},
    {0x60, 320, 170, 0, 35},
    {0xC0, 170, 320, 35, 0},
};

static constexpr uint32_t kSdFrequency = 40000000U;
static constexpr EventBits_t kRotaryStartPressedFlag = BIT0;
static constexpr char kNvsNamespace[] = "tembed_plus";
static constexpr char kGaugeConfigKey[] = "gauge_cfg";
static constexpr uint32_t kGaugeConfigMagic = 0x47554346; // GUCF
static constexpr uint8_t kGaugeConfigOk = 1;
static constexpr uint8_t kGaugeConfigFailed = 2;

struct GaugeConfigRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t designCapacity;
    uint16_t fullChargeCapacity;
    uint8_t status;
    uint8_t reserved[7];
};

static bool gaugeConfigMatchesTarget(const GaugeConfigRecord &record)
{
    return record.magic == kGaugeConfigMagic &&
           record.version == LILYGO_T_EMBED_PLUS_GAUGE_CONFIG_VERSION &&
           record.size == sizeof(GaugeConfigRecord) &&
           record.designCapacity == LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY &&
           record.fullChargeCapacity == LILYGO_T_EMBED_PLUS_GAUGE_FULL_CHARGE_CAPACITY;
}

static bool loadGaugeConfigRecord(GaugeConfigRecord &record)
{
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        LILYGO_LOG_W("Gauge config NVS open failed");
        return false;
    }
    const bool loaded = prefs.getBytes(kGaugeConfigKey, &record, sizeof(record)) == sizeof(record);
    prefs.end();
    return loaded && gaugeConfigMatchesTarget(record);
}

static void saveGaugeConfigRecord(uint8_t status)
{
    GaugeConfigRecord record = {};
    record.magic = kGaugeConfigMagic;
    record.version = LILYGO_T_EMBED_PLUS_GAUGE_CONFIG_VERSION;
    record.size = sizeof(record);
    record.designCapacity = LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY;
    record.fullChargeCapacity = LILYGO_T_EMBED_PLUS_GAUGE_FULL_CHARGE_CAPACITY;
    record.status = status;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        LILYGO_LOG_W("Gauge config NVS open failed");
        return;
    }
    if (prefs.putBytes(kGaugeConfigKey, &record, sizeof(record)) != sizeof(record)) {
        LILYGO_LOG_W("Gauge config NVS save failed");
    }
    prefs.end();
}

static bool configureGaugeCapacityIfNeeded(GaugeBQ27220 &gauge)
{
    GaugeConfigRecord record = {};

    if (gauge.refresh() &&
            gauge.getDesignCapacity() == LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY) {
        LILYGO_LOG_I("Gauge capacity already matches target design=%u",
                     (unsigned)LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY);
        saveGaugeConfigRecord(kGaugeConfigOk);
        return true;
    }

    if (loadGaugeConfigRecord(record)) {
        if (record.status == kGaugeConfigOk) {
            LILYGO_LOG_I("Gauge capacity config already recorded design=%u full=%u",
                         (unsigned)record.designCapacity,
                         (unsigned)record.fullChargeCapacity);
            return true;
        }
        if (record.status == kGaugeConfigFailed) {
            LILYGO_LOG_W("Skip gauge capacity config after previous failure design=%u full=%u",
                         (unsigned)record.designCapacity,
                         (unsigned)record.fullChargeCapacity);
            return false;
        }
    }

    LILYGO_LOG_I("Configure gauge capacity design=%u full=%u",
                 (unsigned)LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY,
                 (unsigned)LILYGO_T_EMBED_PLUS_GAUGE_FULL_CHARGE_CAPACITY);
    const bool ok = gauge.setNewCapacity(LILYGO_T_EMBED_PLUS_GAUGE_DESIGN_CAPACITY,
                                         LILYGO_T_EMBED_PLUS_GAUGE_FULL_CHARGE_CAPACITY);
    saveGaugeConfigRecord(ok ? kGaugeConfigOk : kGaugeConfigFailed);
    return ok;
}

} // namespace

LILYGO_DEFINE_RADIO();
RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
RfalNfcClass NFCReader(&nfc_hw);
#ifndef RADIOLIB_EXCLUDE_NRF24
nRF24 nrf24 = new Module(NRF24_CS, NRF24_IRQ, NRF24_CE,
                         RADIOLIB_NC, SPI);
#endif
extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);

LilyGoTEmbedPlus::LilyGoTEmbedPlus() :
    LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispArduinoSPI(170, 320, st7789Init,
                         sizeof(st7789Init) / sizeof(st7789Init[0]), rotationConfig),
    LilyGoPowerManageInf(pmic, PMIC_TYPE_SY6970)
{
    _brightness = 0;
    brightnessLevel = 0;
}

const char *LilyGoTEmbedPlus::getName()
{
    return "LilyGo T-Embed-Plus V2";
}

const LilyGoDeviceCapability &LilyGoTEmbedPlus::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        "LilyGo T-Embed-Plus V2", // Board name
        "CC1101",                  // Radio name
        "SY6970",                  // PMIC name
        "BQ27220",                 // Fuel gauge name
        true,  // SD card
        false, // GPS
        false, // GPS runtime probe
        false, // Touch
        false, // Keyboard
        false, // Trackball
        true,  // Rotary encoder
        false, // BMA423
        false, // BHI260
        true,  // NFC
        true,  // IR TX
        true,  // IR RX
        false, // Environmental sensor
        false, // Compass
        true,  // Audio output
        true,  // Audio input
        false, // Haptic driver
        true,  // External I2C (QWIIC)
        false, // External SPI (nRF24 is on-board)
        false, // External UART
        false, // External GPIO
        PMIC_TYPE_SY6970,  // PMIC type
        true,              // Board button
        false,             // PMIC button
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoTEmbedPlus::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initAudio = true;
    options.initCodec = false;
    options.initNfc = true;
    options.initRotary = true;
    return options;
}

uint32_t LilyGoTEmbedPlus::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoTEmbedPlus::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

void LilyGoTEmbedPlus::initSharedSpiPins()
{
    // Keep every non-display SPI target deselected before the display sends its
    // initialization commands. The display driver configures DISP_CS itself.
    const int csPins[] = {SD_CS, LORA_CS, NRF24_CS, NFC_CS};
    for (int pin : csPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

bool LilyGoTEmbedPlus::initIoExpander(const LilyGoDeviceInitOptions &options)
{
    if (!io.begin(Wire, EXPANDS_ADDR)) {
        LILYGO_LOG_E("XL9555 init failed");
        return false;
    }

    const uint8_t outputs[] = {
        EXPANDS_CC_SW1,
        EXPANDS_CC_SW0,
        EXPANDS_LCD_RST,
        EXPANDS_LOW_PWR_EN,
        EXPANDS_AMP_EN,
        EXPANDS_C5_RST,
        EXPANDS_MOD_SEL,
        EXPANDS_EX_PWR_EN1,
        EXPANDS_EX_PWR_EN2,
    };
    for (uint8_t pin : outputs) {
        io.pinMode(pin, OUTPUT);
        io.digitalWrite(pin, LOW);
    }

    // LOW_PWR_3V3 supplies the radio, NFC, RGB, IR, microphone, and amplifier.
    io.digitalWrite(EXPANDS_LOW_PWR_EN, HIGH);
    io.digitalWrite(EXPANDS_AMP_EN, options.initAudio ? HIGH : LOW);
    devicesProbe |= HW_EXPAND_ONLINE;
    setRfSwitch(HIGH, LOW);

    io.digitalWrite(EXPANDS_LCD_RST, LOW);
    delay(20);
    io.digitalWrite(EXPANDS_LCD_RST, HIGH);
    delay(120);
    return true;
}

void LilyGoTEmbedPlus::setRfSwitch(uint8_t sw1, uint8_t sw0)
{
    if (!(devicesProbe & HW_EXPAND_ONLINE)) {
        return;
    }
    io.digitalWrite(EXPANDS_CC_SW1, sw1);
    io.digitalWrite(EXPANDS_CC_SW0, sw0);
}

uint32_t LilyGoTEmbedPlus::begin(const LilyGoDeviceInitOptions &options)
{
    if (initialized) {
        return devicesProbe;
    }
    initialized = true;

    Wire.begin(SDA, SCL);
    initIoExpander(options);

    // AW9364 uses GPIO pulses to select one of 16 brightness levels.
    backlight.begin(DISP_BL);
    SPI.begin(SCK, MISO, MOSI);
    initSharedSpiPins();
    // Initialize the display only after all other shared SPI devices are idle.
    LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI,
                               DISP_CS, DISP_RST, DISP_DC, -1, 40);

    devicesProbe |= psramFound() ? HW_PSRAM_ONLINE : 0;
    if (options.initFatfs) {
        setupMSC(nullptr, nullptr);
    }
    if (options.initGauge && gauge.begin(Wire, SDA, SCL)) {
        devicesProbe |= HW_GAUGE_ONLINE;
        configureGaugeCapacityIfNeeded(gauge);
    }
    if (options.initPmu && initPMU()) {
        devicesProbe |= HW_PMU_ONLINE;
    }
    if (options.scanI2c) {
        LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire));
    }
    if (options.initNfc) {
        initNFC();
    }
    if (options.initRadio) {
        initLoRa();
    }
    if (options.initSd) {
        installSD();
    }
    if (options.initAudio) {
        const bool audioInputReady = audioInput.begin();
        const bool audioOutputReady = audioOutput.begin();
        if (audioInputReady || audioOutputReady) {
            devicesProbe |= HW_CODEC_ONLINE;
        }
    }
    if (options.initRotary) {
        initRotary();
    }

    userButton.setClickHandler([](Button2 &) {
        instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_CLICK));
    });
    userButton.setLongClickHandler([](Button2 &) {
        instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_LONG_PRESSED));
    });
    // setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    return devicesProbe;
}

bool LilyGoTEmbedPlus::initPMU()
{
    if (!pmic.begin(Wire, SY6970_SLAVE_ADDRESS, SDA, SCL)) {
        return false;
    }
    pmic.charger().setChargeVoltage(4288);
    // Both revisions use a 1300 mAh battery; keep charging at or below 0.5 C.
    pmic.charger().setFastChargeCurrent(DEVICE_CHARGE_CURRENT_RECOMMEND);
    return true;
}

bool LilyGoTEmbedPlus::initLoRa()
{
    pinMode(LORA_IRQ, INPUT);
    pinMode(LORA_BUSY, INPUT);
    const int state = radio.begin(315.0f);
    if (state != RADIOLIB_ERR_NONE) {
        devicesProbe &= ~HW_RADIO_ONLINE;
        LILYGO_LOG_E("CC1101 init failed: %d", state);
        return false;
    }
    devicesProbe |= HW_RADIO_ONLINE;
    return true;
}

#ifndef RADIOLIB_EXCLUDE_NRF24
bool LilyGoTEmbedPlus::initNRF24()
{
    const int state = nrf24.begin();
    if (state != RADIOLIB_ERR_NONE) {
        devicesProbe &= ~HW_NRF24_ONLINE;
        LILYGO_LOG_E("nRF24 init failed: %d", state);
        return false;
    }
    devicesProbe |= HW_NRF24_ONLINE;
    return true;
}
#endif

int16_t LilyGoTEmbedPlus::setRFFrequencyBand(float frequency)
{
    if (frequency >= 779.0f) {
        setRfSwitch(LOW, HIGH);
    } else if (frequency >= 387.0f) {
        setRfSwitch(HIGH, HIGH);
    } else {
        setRfSwitch(HIGH, LOW);
    }
    return radio.setFrequency(frequency);
}

bool LilyGoTEmbedPlus::initNFC()
{
    // ST25R3916 shares the display, SD, and radio SPI bus; Wire is only used by I2C devices.
    pinMode(NFC_INT, INPUT);
    const bool ready = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!ready) {
        devicesProbe &= ~HW_NFC_ONLINE;
        LILYGO_LOG_E("ST25R3916 init failed");
        return false;
    }
    devicesProbe |= HW_NFC_ONLINE;
    return true;
}

bool LilyGoTEmbedPlus::installSD(uint32_t spiFrequency)
{
    if (!spiFrequency) spiFrequency = kSdFrequency;
    SD.end();
    if (!SD.begin(SD_CS, SPI, spiFrequency, "/sd")) {
        devicesProbe &= ~HW_SD_ONLINE;
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        devicesProbe &= ~HW_SD_ONLINE;
        return false;
    }
    devicesProbe |= HW_SD_ONLINE;
    return true;
}

void LilyGoTEmbedPlus::uninstallSD()
{
    SD.end();
    devicesProbe &= ~HW_SD_ONLINE;
}

void LilyGoTEmbedPlus::setRotation(uint8_t rotation) { LilyGoDispArduinoSPI::setRotation(rotation); }
uint8_t LilyGoTEmbedPlus::getRotation() { return LilyGoDispArduinoSPI::getRotation(); }
uint16_t LilyGoTEmbedPlus::width() { return LilyGoDispArduinoSPI::_width; }
uint16_t LilyGoTEmbedPlus::height() { return LilyGoDispArduinoSPI::_height; }

void LilyGoTEmbedPlus::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
}

void LilyGoTEmbedPlus::setBrightness(uint8_t level)
{
    brightnessLevel = min<uint8_t>(level, DEVICE_MAX_BRIGHTNESS_LEVEL);
    backlight.setBrightness(brightnessLevel);
}

void LilyGoTEmbedPlus::sleepDisplay() { LilyGoDispArduinoSPI::sleep(); }
void LilyGoTEmbedPlus::wakeupDisplay() { LilyGoDispArduinoSPI::wakeup(); }

void LilyGoTEmbedPlus::powerControl(PowerCtrlChannel_t channel, bool enable)
{
    switch (channel) {
    case POWER_DISPLAY_BACKLIGHT:
        backlight.setBrightness(enable ? brightnessLevel : 0);
        break;
    case POWER_RADIO:
        // The V2 LOW_PWR_3V3 rail also supplies NFC, RGB, IR, and audio.
        if (devicesProbe & HW_EXPAND_ONLINE) {
            // io.digitalWrite(EXPANDS_LOW_PWR_EN, HIGH);
        }
        break;
    case POWER_NFC:
        // ST25R3916 shares LOW_PWR_3V3, so it cannot be switched independently.
        (void)enable;
        break;
    case POWER_SPEAK:
        if (devicesProbe & HW_EXPAND_ONLINE) {
            // io.digitalWrite(EXPANDS_AMP_EN, enable ? HIGH : LOW);
        }
        break;
    default: break;
    }
}

RotaryMsg_t LilyGoTEmbedPlus::getRotary()
{
    static bool centerBtnPressed = false;
    RotaryMsg_t message = {};

    if (rotaryMsg && xQueueReceive(rotaryMsg, &message, pdMS_TO_TICKS(50)) == pdPASS) {
        centerBtnPressed = message.centerBtnPressed;
        return message;
    }

    message.centerBtnPressed = centerBtnPressed;
    return message;
}

void LilyGoTEmbedPlus::clearRotaryMsg()
{
    if (rotaryMsg) {
        xQueueReset(rotaryMsg);
    }
}

bool LilyGoTEmbedPlus::initRotary()
{
    if (rotaryHandler) {
        return true;
    }

    rotaryStepDivider = 1;
    rotaryMsg = xQueueCreate(5, sizeof(RotaryMsg_t));
    rotaryTaskFlag = xEventGroupCreate();
    if (rotaryMsg && rotaryTaskFlag &&
            xTaskCreate(rotaryTaskEntry, "rotary", 2 * 1024, this, 10, &rotaryHandler) == pdPASS) {
        LILYGO_LOG_I("Rotary init succeeded, backend: state-table polling");
        return true;
    }

    LILYGO_LOG_E("Failed to init rotary");
    if (rotaryMsg) {
        vQueueDelete(rotaryMsg);
        rotaryMsg = nullptr;
    }
    if (rotaryTaskFlag) {
        vEventGroupDelete(rotaryTaskFlag);
        rotaryTaskFlag = nullptr;
    }
    rotaryHandler = nullptr;
    return false;
}

void LilyGoTEmbedPlus::enableRotary()
{
    if (rotaryHandler) {
        if (digitalRead(ROTARY_C) == LOW) {
            xEventGroupSetBits(rotaryTaskFlag, kRotaryStartPressedFlag);
        }
        vTaskResume(rotaryHandler);
    }
}

void LilyGoTEmbedPlus::disableRotary()
{
    if (rotaryHandler) {
        vTaskSuspend(rotaryHandler);
    }
}

void LilyGoTEmbedPlus::setRotaryStepDivider(uint8_t divider) { rotaryStepDivider = constrain(divider, 1, 8); }

void LilyGoTEmbedPlus::loop()
{
    userButton.loop();
}

void LilyGoTEmbedPlus::rotaryTaskEntry(void *param)
{
    static_cast<LilyGoTEmbedPlus *>(param)->rotaryTaskLoop();
}

bool LilyGoTEmbedPlus::getRotaryButtonState()
{
    static uint8_t buttonState = HIGH;
    static uint8_t lastReading = HIGH;
    static uint32_t lastDebounceTime = 0;
    constexpr uint8_t debounceDelay = 20;
    const int reading = digitalRead(ROTARY_C);

    const EventBits_t eventBits = xEventGroupGetBits(rotaryTaskFlag);
    if (eventBits & kRotaryStartPressedFlag) {
        if (reading == HIGH) {
            xEventGroupClearBits(rotaryTaskFlag, kRotaryStartPressedFlag);
        } else {
            return false;
        }
    }

    if (reading != lastReading) {
        lastDebounceTime = millis();
        lastReading = reading;
    }
    if ((millis() - lastDebounceTime) > debounceDelay) {
        buttonState = reading;
    }
    return buttonState == LOW;
}

void LilyGoTEmbedPlus::rotaryTaskLoop()
{
    bool lastButtonState = false;
    uint8_t pendingDirection = DIR_NONE;
    uint8_t pendingSteps = 0;

    rotary.begin(true, false, true);
    pinMode(ROTARY_C, INPUT_PULLUP);

    while (true) {
        RotaryMsg_t message = {};
        message.centerBtnPressed = getRotaryButtonState();

        uint8_t result = rotary.process();
        if (result != DIR_NONE) {
            if (result == pendingDirection) {
                ++pendingSteps;
            } else {
                pendingDirection = result;
                pendingSteps = 1;
            }

            if (pendingSteps >= rotaryStepDivider) {
                pendingDirection = DIR_NONE;
                pendingSteps = 0;
            } else {
                result = DIR_NONE;
            }
        }

        const bool buttonChanged = message.centerBtnPressed != lastButtonState;
        if (result != DIR_NONE || buttonChanged) {
            if (result == DIR_CW) {
                message.dir = ROTARY_DIR_DOWN;
                message.enc_diff = -1;
            } else if (result == DIR_CCW) {
                message.dir = ROTARY_DIR_UP;
                message.enc_diff = 1;
            }

            message.centerBtnClicked = buttonChanged && message.centerBtnPressed;
            message.centerBtnReleased = buttonChanged && !message.centerBtnPressed;
            lastButtonState = message.centerBtnPressed;
            xQueueSend(rotaryMsg, &message, portMAX_DELAY);
        }
        delay(2);
    }
}

bool LilyGoTEmbedPlus::readPowerSnapshot(LilyGoPowerSnapshot &snapshot)
{
    LilyGoPowerManageInf::readPowerSnapshot(snapshot);
    if (!(devicesProbe & HW_GAUGE_ONLINE)) return snapshot.pmuPresent;
    gauge.refresh();
    copyPowerLabel(snapshot.gaugeName, sizeof(snapshot.gaugeName), "BQ27220");
    setPowerMetric(snapshot.batteryPercent, gauge.getStateOfCharge(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.batteryMv, gauge.getVoltage(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.batteryMa, gauge.getCurrent(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.temperatureC, gauge.getTemperature(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    snapshot.externalGaugePresent = true;
    snapshot.fuelGaugePresent = snapshot.batteryPercent.valid;
    snapshot.batteryPresentValid = true;
    snapshot.batteryPresent = snapshot.batteryMv.valid && snapshot.batteryMv.value > 2500.0f;
    return true;
}

float LilyGoTEmbedPlus::getBattVoltage() { return gauge.getVoltage(); }
float LilyGoTEmbedPlus::getBatteryPercent() { return gauge.getStateOfCharge(); }
float LilyGoTEmbedPlus::getTemperature() { return gauge.getTemperature(); }
bool LilyGoTEmbedPlus::hasOTG() { return devicesProbe & HW_PMU_ONLINE; }
bool LilyGoTEmbedPlus::isOTGEnabled() { return pmic.power().isBoostEnabled(); }
bool LilyGoTEmbedPlus::enableOTG() { return pmic.power().enableBoost(true); }
bool LilyGoTEmbedPlus::disableOTG() { return pmic.power().enableBoost(false); }
bool LilyGoTEmbedPlus::shutdown()
{
    if (pmic.charger().getStatus().vbusPresent) return false;
    return pmic.power().enableShipMode(true);
}

bool LilyGoTEmbedPlus::lockSPI(TickType_t ticksToWait) { return LilyGoDispArduinoSPI::lock(ticksToWait); }
void LilyGoTEmbedPlus::unlockSPI() { LilyGoDispArduinoSPI::unlock(); }

uint64_t LilyGoTEmbedPlus::getWakeupPinMask(WakeupSource_t wakeupSource) const
{
    uint64_t pins = 0;
    if (wakeupSource & (WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON)) {
        pins |= 1ULL << ROTARY_C;
    }
    if (wakeupSource & WAKEUP_SRC_BUTTON) {
        pins |= 1ULL << USER_KEY;
    }
    return pins;
}

void LilyGoTEmbedPlus::lightSleep(WakeupSource_t wakeupSource)
{
    const uint64_t wakeupPins = getWakeupPinMask(wakeupSource);
    if (wakeupPins == 0) {
        LILYGO_LOG_E("No supported light-sleep wake source selected");
        return;
    }

    if (wakeupPins & (1ULL << ROTARY_C)) {
        pinMode(ROTARY_C, INPUT_PULLUP);
    }
    if (wakeupPins & (1ULL << USER_KEY)) {
        pinMode(USER_KEY, INPUT_PULLUP);
    }
    delayMicroseconds(50);
    if (((wakeupPins & (1ULL << ROTARY_C)) && digitalRead(ROTARY_C) == LOW) ||
            ((wakeupPins & (1ULL << USER_KEY)) && digitalRead(USER_KEY) == LOW)) {
        LILYGO_LOG_W("Light sleep skipped: wake button is still pressed");
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t wakeupResult = esp_sleep_enable_ext1_wakeup_io(
                                       wakeupPins,
                                       ESP_EXT1_WAKEUP_ANY_LOW);
    if (wakeupResult != ESP_OK) {
        LILYGO_LOG_E("Failed to enable light-sleep wakeup: %s",
                     esp_err_to_name(wakeupResult));
        return;
    }

    const bool radioWasOnline = isDeviceOnline(HW_RADIO_ONLINE);
#ifndef RADIOLIB_EXCLUDE_NRF24
    const bool nrf24WasOnline = isDeviceOnline(HW_NRF24_ONLINE);
#endif
    const bool nfcWasOnline = isDeviceOnline(HW_NFC_ONLINE);
    const bool audioWasOnline = isDeviceOnline(HW_CODEC_ONLINE);
    const bool sdWasOnline = isDeviceOnline(HW_SD_ONLINE);

    disableRotary();
    if (radioWasOnline) {
        radio.sleep();
    }
#ifndef RADIOLIB_EXCLUDE_NRF24
    if (nrf24WasOnline) {
        nrf24.sleep();
    }
#endif
    powerControl(POWER_NFC, false);
    if (audioWasOnline) {
        audioInput.end();
        audioOutput.end();
        powerControl(POWER_SPEAK, false);
        devicesProbe &= ~HW_CODEC_ONLINE;
    }
    if (sdWasOnline) {
        uninstallSD();
    }
    sleepDisplay();
    backlight.setBrightness(0);

    // Keep LOW_PWR_3V3 enabled so the CC1101 retains its runtime configuration.
    // The amplifier's XL9555 enable is low while the I2S clocks are stopped.
    const esp_err_t sleepResult = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);

    wakeupDisplay();
    delay(120);
    backlight.setBrightness(brightnessLevel);
    if (radioWasOnline) {
        radio.standby();
    }
#ifndef RADIOLIB_EXCLUDE_NRF24
    if (nrf24WasOnline) {
        nrf24.standby();
    }
#endif
    if (nfcWasOnline) {
        powerControl(POWER_NFC, true);
        if (!initNFC()) {
            devicesProbe &= ~HW_NFC_ONLINE;
            LILYGO_LOG_W("Failed to restore ST25R3916 after light sleep");
        }
    }
    if (audioWasOnline) {
        powerControl(POWER_SPEAK, true);
        const bool audioInputReady = audioInput.begin();
        const bool audioOutputReady = audioOutput.begin();
        if (audioInputReady || audioOutputReady) {
            devicesProbe |= HW_CODEC_ONLINE;
        } else {
            LILYGO_LOG_W("Failed to restore audio after light sleep");
        }
    }
    if (sdWasOnline && !installSD()) {
        LILYGO_LOG_W("Failed to restore SD card after light sleep");
    }
    enableRotary();

    if (sleepResult != ESP_OK) {
        LILYGO_LOG_E("Failed to enter light sleep: %s",
                     esp_err_to_name(sleepResult));
    }
}

void LilyGoTEmbedPlus::sleep(WakeupSource_t wakeupSource, bool, uint32_t sleepSeconds)
{
    const bool timerWakeup = wakeupSource & WAKEUP_SRC_TIMER;
    const WakeupSource_t physicalSources = static_cast<WakeupSource_t>(
            static_cast<uint32_t>(wakeupSource) & ~static_cast<uint32_t>(WAKEUP_SRC_TIMER));
    const uint64_t wakeupPins = getWakeupPinMask(physicalSources);

    if (timerWakeup && sleepSeconds == 0) {
        LILYGO_LOG_E("Timer wakeup requires a non-zero sleep duration");
        return;
    }
    if (physicalSources && wakeupPins == 0) {
        LILYGO_LOG_E("No supported deep-sleep button wake source selected");
        return;
    }
    if (!timerWakeup && wakeupPins == 0) {
        LILYGO_LOG_E("No supported deep-sleep wake source selected");
        return;
    }

    if (wakeupPins & (1ULL << ROTARY_C)) {
        pinMode(ROTARY_C, INPUT_PULLUP);
    }
    if (wakeupPins & (1ULL << USER_KEY)) {
        pinMode(USER_KEY, INPUT_PULLUP);
    }
    delayMicroseconds(50);
    if (((wakeupPins & (1ULL << ROTARY_C)) && digitalRead(ROTARY_C) == LOW) ||
            ((wakeupPins & (1ULL << USER_KEY)) && digitalRead(USER_KEY) == LOW)) {
        LILYGO_LOG_W("Deep sleep skipped: wake button is still pressed");
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t wakeupResult = ESP_OK;
    if (timerWakeup) {
        wakeupResult = esp_sleep_enable_timer_wakeup(
                           static_cast<uint64_t>(sleepSeconds) * 1000000ULL);
    }
    if (wakeupResult == ESP_OK && wakeupPins != 0) {
        wakeupResult = esp_sleep_enable_ext1_wakeup_io(
                           wakeupPins, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if (wakeupResult != ESP_OK) {
        LILYGO_LOG_E("Failed to configure deep-sleep wakeup: %s",
                     esp_err_to_name(wakeupResult));
        return;
    }

    disableRotary();
    if (isDeviceOnline(HW_RADIO_ONLINE)) {
        radio.sleep();
    }
#ifndef RADIOLIB_EXCLUDE_NRF24
    if (isDeviceOnline(HW_NRF24_ONLINE)) {
        nrf24.sleep();
    }
#endif
    powerControl(POWER_NFC, false);
    if (isDeviceOnline(HW_CODEC_ONLINE)) {
        audioInput.end();
        audioOutput.end();
        powerControl(POWER_SPEAK, false);
    }
    if (isDeviceOnline(HW_SD_ONLINE)) {
        uninstallSD();
    }

    sleepDisplay();
    backlight.setBrightness(0);

    // Deep sleep resets the application, so peripheral state is not retained.
    if (devicesProbe & HW_EXPAND_ONLINE) {
        io.digitalWrite(EXPANDS_AMP_EN, LOW);
        setRfSwitch(LOW, LOW);
        io.digitalWrite(EXPANDS_LOW_PWR_EN, LOW);
    }

    SPI.end();
    Wire.end();
    Serial.flush();
    esp_deep_sleep_start();
}

namespace {
LilyGoTEmbedPlus &getInstanceRef() { return *LilyGoTEmbedPlus::getInstance(); }
}
LilyGoTEmbedPlus &instance = getInstanceRef();

#endif // ARDUINO_T_EMBED_PLUS_V2
