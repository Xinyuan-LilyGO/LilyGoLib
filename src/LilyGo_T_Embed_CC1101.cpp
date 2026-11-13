#ifdef ARDUINO_T_EMBED_CC1101

#include "LilyGo_T_Embed_CC1101.h"
#include "LilyGoLog.h"
#include "core/LilyGoGeneral.h"
#include <SensorWireHelper.h>
#include "radio/LilyGoRadioHelper.h"

namespace {

static const CommandTable_t st7789Init[] = {
    {0x11, {0}, 0x80},
    {0x13, {0}, 0},
    {0x36, {0x60}, 1},
    {0x3A, {0x55}, 1},
    {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x1A}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x13}, 1},
    {0xC4, {0x20}, 1},
    {0xC6, {0x0F}, 1},
    {0xE0, {0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}, 14},
    {0xE1, {0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x3E}, 14},
    {0x21, {0}, 0},
    {0x2A, {0x00, 0x00, 0x00, 0xA9}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4 | 0x80},
    {0x29, {0}, 0x80},
};

static const DispRotationConfig_t rotationConfig[] = {
    {0xA0, 320, 170, 0, 35},
    {0x00, 170, 320, 35, 0},
    {0x60, 320, 170, 0, 35},
    {0xC0, 170, 320, 35, 0},
};

static constexpr uint32_t kSdFrequency = 40000000U;
static constexpr uint16_t kChargeCurrent = 500;
static constexpr EventBits_t kRotaryStartPressedFlag = BIT0;

} // namespace

LILYGO_DEFINE_RADIO();
#ifndef RADIOLIB_EXCLUDE_NRF24
nRF24 nrf24 = new Module(BOARD_NRF24_CS, BOARD_NRF24_IRQ, BOARD_NRF24_CE,
                         RADIOLIB_NC, SPI);
#endif
extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);

LilyGoTEmbedCC1101::LilyGoTEmbedCC1101() :
    LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispArduinoSPI(170, 320, st7789Init,
                         sizeof(st7789Init) / sizeof(st7789Init[0]), rotationConfig),
    LilyGoPowerManageInf(pmic, PMIC_TYPE_BQ25896)
{
    _brightness = 0;
    brightnessLevel = 0;
}

const char *LilyGoTEmbedCC1101::getName()
{
    return "LilyGo T-Embed-CC1101";
}

const LilyGoDeviceCapability &LilyGoTEmbedCC1101::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        "LilyGo T-Embed-CC1101", "CC1101", "BQ25896", "BQ27220",
        true,  // SD card
        false, // GPS
        false, // GPS runtime probe
        false, // Touch
        false, // Keyboard
        false, // Trackball
        true,  // Rotary encoder
        false, // BMA423
        false, // BHI260
        true,  // PN532 NFC
        true,  // IR TX
        true,  // IR RX
        false, // Environmental sensor
        false, // Compass
        true,  // Audio output
        true,  // Audio input
        false, // Haptic driver
        false, // External I2C
        true,  // External SPI (NRF24 expansion header)
        false, // External UART
        true,  // External GPIO (NRF24 CS/CE)
        PMIC_TYPE_BQ25896, true, false
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoTEmbedCC1101::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initAudio = true;
    options.initCodec = false;
    options.initNfc = true;
    options.initRotary = true;
    return options;
}

uint32_t LilyGoTEmbedCC1101::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoTEmbedCC1101::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

void LilyGoTEmbedCC1101::initSharedSpiPins()
{
    const int csPins[] = {DISPLAY_CS, BOARD_SD_CS, BOARD_LORA_CS, BOARD_NRF24_CS};
    for (int pin : csPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

uint32_t LilyGoTEmbedCC1101::begin(const LilyGoDeviceInitOptions &options)
{
    // Power on the shared CC1101, WS2812 and MAX98357 rail before probing peripherals.
    pinMode(BOARD_PERIPHERAL_POWER_EN, OUTPUT);
    digitalWrite(BOARD_PERIPHERAL_POWER_EN, HIGH);

    if (initialized) {
        return devicesProbe;
    }
    initialized = true;

    // AW9364 uses GPIO pulses to select one of 16 brightness levels.
    backlight.begin(DISPLAY_BL);
    pinMode(BOARD_PN532_RF_REST, OUTPUT);
    digitalWrite(BOARD_PN532_RF_REST, HIGH);
    pinMode(BOARD_LORA_SW0, OUTPUT);
    pinMode(BOARD_LORA_SW1, OUTPUT);
    digitalWrite(BOARD_LORA_SW1, HIGH);
    digitalWrite(BOARD_LORA_SW0, LOW);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    initSharedSpiPins();
    LilyGoDispArduinoSPI::init(DISPLAY_SCLK, DISPLAY_MISO, DISPLAY_MOSI,
                               DISPLAY_CS, DISPLAY_RST, DISPLAY_DC, -1, 40);

    devicesProbe |= psramFound() ? HW_PSRAM_ONLINE : 0;
    if (options.initFatfs) {
        setupMSC(nullptr, nullptr);
    }
    if (options.initGauge && gauge.begin(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
        devicesProbe |= HW_GAUGE_ONLINE;
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
    setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    return devicesProbe;
}

bool LilyGoTEmbedCC1101::initPMU()
{
    if (!pmic.begin(Wire, BQ25896_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
        return false;
    }
    pmic.charger().setChargeVoltage(4208);
    pmic.charger().setFastChargeCurrent(kChargeCurrent);
    return true;
}

bool LilyGoTEmbedCC1101::initLoRa()
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
bool LilyGoTEmbedCC1101::initNRF24()
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

int16_t LilyGoTEmbedCC1101::setRFFrequencyBand(float frequency)
{
    if (frequency >= 779.0f) {
        digitalWrite(BOARD_LORA_SW1, LOW);
        digitalWrite(BOARD_LORA_SW0, HIGH);
    } else if (frequency >= 387.0f) {
        digitalWrite(BOARD_LORA_SW1, HIGH);
        digitalWrite(BOARD_LORA_SW0, HIGH);
    } else {
        digitalWrite(BOARD_LORA_SW1, HIGH);
        digitalWrite(BOARD_LORA_SW0, LOW);
    }
    return radio.setFrequency(frequency);
}

bool LilyGoTEmbedCC1101::initNFC()
{
    Wire.beginTransmission(0x24);
    if (Wire.endTransmission() != 0 || !nfc.begin()) {
        return false;
    }
    nfcFirmwareVersion = nfc.getFirmwareVersion();
    if (nfcFirmwareVersion == 0 || !nfc.SAMConfig()) {
        nfcFirmwareVersion = 0;
        return false;
    }
    devicesProbe |= HW_NFC_ONLINE;
    return true;
}

bool LilyGoTEmbedCC1101::installSD(uint32_t spiFrequency)
{
    if (!spiFrequency) spiFrequency = kSdFrequency;
    SD.end();
    if (!SD.begin(BOARD_SD_CS, SPI, spiFrequency, "/sd")) {
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

void LilyGoTEmbedCC1101::uninstallSD()
{
    SD.end();
    devicesProbe &= ~HW_SD_ONLINE;
}

void LilyGoTEmbedCC1101::setRotation(uint8_t rotation) { LilyGoDispArduinoSPI::setRotation(rotation); }
uint8_t LilyGoTEmbedCC1101::getRotation() { return LilyGoDispArduinoSPI::getRotation(); }
uint16_t LilyGoTEmbedCC1101::width() { return LilyGoDispArduinoSPI::_width; }
uint16_t LilyGoTEmbedCC1101::height() { return LilyGoDispArduinoSPI::_height; }
void LilyGoTEmbedCC1101::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
}

void LilyGoTEmbedCC1101::setBrightness(uint8_t level)
{
    brightnessLevel = min<uint8_t>(level, DEVICE_MAX_BRIGHTNESS_LEVEL);
    backlight.setBrightness(brightnessLevel);
}

void LilyGoTEmbedCC1101::sleepDisplay() { LilyGoDispArduinoSPI::sleep(); }
void LilyGoTEmbedCC1101::wakeupDisplay() { LilyGoDispArduinoSPI::wakeup(); }

void LilyGoTEmbedCC1101::powerControl(PowerCtrlChannel_t channel, bool enable)
{
    switch (channel) {
    case POWER_DISPLAY_BACKLIGHT:
        backlight.setBrightness(enable ? brightnessLevel : 0);
        break;
    case POWER_RADIO:
        // GPIO15 cannot switch CC1101 alone: LOW would also turn off WS2812 and MAX98357.
        // Keep the shared peripheral rail on even when radio power-off is requested.
        digitalWrite(BOARD_PERIPHERAL_POWER_EN, HIGH);
        break;
    case POWER_NFC: digitalWrite(BOARD_PN532_RF_REST, enable ? HIGH : LOW); break;
    case POWER_SPEAK: break;
    default: break;
    }
}

RotaryMsg_t LilyGoTEmbedCC1101::getRotary()
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

void LilyGoTEmbedCC1101::clearRotaryMsg()
{
    if (rotaryMsg) {
        xQueueReset(rotaryMsg);
    }
}

bool LilyGoTEmbedCC1101::initRotary()
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

    LILYGO_LOG_E("Warning: Failed to init rotary");
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

void LilyGoTEmbedCC1101::enableRotary()
{
    if (rotaryHandler) {
        if (digitalRead(ENCODER_KEY) == LOW) {
            xEventGroupSetBits(rotaryTaskFlag, kRotaryStartPressedFlag);
        }
        vTaskResume(rotaryHandler);
    }
}

void LilyGoTEmbedCC1101::disableRotary()
{
    if (rotaryHandler) {
        vTaskSuspend(rotaryHandler);
    }
}

void LilyGoTEmbedCC1101::setRotaryStepDivider(uint8_t divider) { rotaryStepDivider = constrain(divider, 1, 8); }

void LilyGoTEmbedCC1101::loop()
{
    userButton.loop();
}

void LilyGoTEmbedCC1101::rotaryTaskEntry(void *param)
{
    static_cast<LilyGoTEmbedCC1101 *>(param)->rotaryTaskLoop();
}

bool LilyGoTEmbedCC1101::getRotaryButtonState()
{
    static uint8_t buttonState = HIGH;
    static uint8_t lastReading = HIGH;
    static uint32_t lastDebounceTime = 0;
    constexpr uint8_t debounceDelay = 20;
    const int reading = digitalRead(ENCODER_KEY);

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

void LilyGoTEmbedCC1101::rotaryTaskLoop()
{
    bool lastButtonState = false;
    uint8_t pendingDirection = DIR_NONE;
    uint8_t pendingSteps = 0;

    rotary.begin(true, false, true);
    pinMode(ENCODER_KEY, INPUT_PULLUP);

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

bool LilyGoTEmbedCC1101::readPowerSnapshot(LilyGoPowerSnapshot &snapshot)
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

float LilyGoTEmbedCC1101::getBattVoltage() { return gauge.getVoltage(); }
float LilyGoTEmbedCC1101::getBatteryPercent() { return gauge.getStateOfCharge(); }
float LilyGoTEmbedCC1101::getTemperature() { return gauge.getTemperature(); }
bool LilyGoTEmbedCC1101::hasOTG() { return devicesProbe & HW_PMU_ONLINE; }
bool LilyGoTEmbedCC1101::isOTGEnabled() { return pmic.power().isBoostEnabled(); }
bool LilyGoTEmbedCC1101::enableOTG() { return pmic.power().enableBoost(true); }
bool LilyGoTEmbedCC1101::disableOTG() { return pmic.power().enableBoost(false); }
bool LilyGoTEmbedCC1101::shutdown()
{
    if (pmic.charger().getStatus().vbusPresent) return false;
    return pmic.power().enableShipMode(true);
}

bool LilyGoTEmbedCC1101::lockSPI(TickType_t ticksToWait) { return LilyGoDispArduinoSPI::lock(ticksToWait); }
void LilyGoTEmbedCC1101::unlockSPI() { LilyGoDispArduinoSPI::unlock(); }

uint64_t LilyGoTEmbedCC1101::getWakeupPinMask(WakeupSource_t wakeupSource) const
{
    uint64_t pins = 0;
    if (wakeupSource & (WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON)) {
        pins |= 1ULL << ENCODER_KEY;
    }
    if (wakeupSource & WAKEUP_SRC_BUTTON) {
        pins |= 1ULL << BOARD_USER_KEY;
    }
    return pins;
}

void LilyGoTEmbedCC1101::lightSleep(WakeupSource_t wakeupSource)
{
    const uint64_t wakeupPins = getWakeupPinMask(wakeupSource);
    if (wakeupPins == 0) {
        LILYGO_LOG_E("No supported T-Embed light-sleep wake source selected");
        return;
    }

    if (wakeupPins & (1ULL << ENCODER_KEY)) {
        pinMode(ENCODER_KEY, INPUT_PULLUP);
    }
    if (wakeupPins & (1ULL << BOARD_USER_KEY)) {
        pinMode(BOARD_USER_KEY, INPUT_PULLUP);
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t wakeupResult = esp_sleep_enable_ext1_wakeup_io(
                                       wakeupPins,
                                       ESP_EXT1_WAKEUP_ANY_LOW);
    if (wakeupResult != ESP_OK) {
        LILYGO_LOG_E("Failed to enable T-Embed light-sleep wakeup: %s",
                     esp_err_to_name(wakeupResult));
        return;
    }

    disableRotary();
    const esp_err_t sleepResult = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    enableRotary();

    if (sleepResult != ESP_OK) {
        LILYGO_LOG_E("Failed to enter T-Embed light sleep: %s",
                     esp_err_to_name(sleepResult));
    }
}

void LilyGoTEmbedCC1101::sleep(WakeupSource_t wakeupSource, bool, uint32_t sleepSeconds)
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
        LILYGO_LOG_E("No supported T-Embed deep-sleep button wake source selected");
        return;
    }
    if (!timerWakeup && wakeupPins == 0) {
        LILYGO_LOG_E("No supported T-Embed deep-sleep wake source selected");
        return;
    }

    if (wakeupPins & (1ULL << ENCODER_KEY)) {
        pinMode(ENCODER_KEY, INPUT_PULLUP);
    }
    if (wakeupPins & (1ULL << BOARD_USER_KEY)) {
        pinMode(BOARD_USER_KEY, INPUT_PULLUP);
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
        LILYGO_LOG_E("Failed to configure T-Embed deep-sleep wakeup: %s",
                     esp_err_to_name(wakeupResult));
        return;
    }
    esp_deep_sleep_start();
}

namespace {
LilyGoTEmbedCC1101 &getInstanceRef() { return *LilyGoTEmbedCC1101::getInstance(); }
}
LilyGoTEmbedCC1101 &instance = getInstanceRef();

#endif // ARDUINO_T_EMBED_CC1101
