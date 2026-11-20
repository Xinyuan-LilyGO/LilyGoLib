/**
 * @file      LilyGo_T_Embed.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-17
 *
 */
#ifdef ARDUINO_T_EMBED

#include "LilyGo_T_Embed.h"
#include "LilyGoLog.h"
#include <SensorWireHelper.h>
#include <algorithm>
#include <esp_err.h>
#include <esp_sleep.h>

extern void setupMSC(lock_callback_t lockCb, lock_callback_t unlockCb);

namespace {

static const CommandTable_t st7789Init[] = {
    {0x11, {0}, 0 | 0x80},
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
};

static const DispRotationConfig_t rotationConfig[] = {
    {0xA0, 320, 170, 0, 35}, {0x00, 170, 320, 35, 0},
    {0x60, 320, 170, 0, 35}, {0xC0, 170, 320, 35, 0},
};

static constexpr EventBits_t kRotaryStartPressedFlag = BIT0;

float estimateBatteryPercent(float millivolts)
{
    struct Point { uint16_t mv; uint8_t percent; };
    static constexpr Point curve[] = {
        {3300, 0}, {3500, 5}, {3600, 10}, {3700, 20}, {3800, 40},
        {3900, 60}, {4000, 80}, {4100, 90}, {4200, 100},
    };
    if (millivolts <= curve[0].mv) return 0;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (millivolts <= curve[i].mv) {
            const Point &low = curve[i - 1];
            const Point &high = curve[i];
            return low.percent + (millivolts - low.mv) *
                   (high.percent - low.percent) / (high.mv - low.mv);
        }
    }
    return 100;
}

} // namespace

LilyGoTEmbed::LilyGoTEmbed() :
    LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispArduinoSPI(DISP_WIDTH, DISP_HEIGHT, st7789Init,
                         sizeof(st7789Init) / sizeof(st7789Init[0]), rotationConfig)
{
}

const char *LilyGoTEmbed::getName() { return "LilyGo T-Embed"; }

const LilyGoDeviceCapability &LilyGoTEmbed::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        "LilyGo T-Embed", "None", "None", "None",
        true, false, false, false, false, false, true,
        false, false, false, false, false, false, false,
        true, true, false, true, false, false, false,
        PMIC_TYPE_UNKNOWN, false, false,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoTEmbed::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initSi4735 = true;
    return options;
}

uint32_t LilyGoTEmbed::begin() { return begin(getDefaultInitOptions()); }

uint32_t LilyGoTEmbed::begin(uint32_t disableHwInit)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disableHwInit));
}

uint32_t LilyGoTEmbed::begin(const LilyGoDeviceInitOptions &options)
{
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    if (initialized) return devicesProbe;
    initialized = true;

    pinMode(BAT_ADC, INPUT);
    pinMode(DISPLAY_CS, OUTPUT);
    digitalWrite(DISPLAY_CS, HIGH);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    LilyGoDispArduinoSPI::init(DISPLAY_SCLK, DISPLAY_MISO, DISPLAY_MOSI,
                               DISPLAY_CS, DISPLAY_RST, DISPLAY_DC, -1, 40);
    backlight.begin(DISPLAY_BL);
    devicesProbe |= psramFound() ? HW_PSRAM_ONLINE : 0;

    if (options.initFatfs) setupMSC(nullptr, nullptr);
    if (options.scanI2c) LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire));
    if (options.initSi4735) initSi4735();
    if (options.initSd) installSD();
    if (options.initAudio) {
        initAmplifier();
        if (options.initCodec) initCodec();
    }
    if (options.initRotary) initRotary();
    setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    return devicesProbe;
}

bool LilyGoTEmbed::initCodec()
{
    codec.setPins(MIC_I2S_MCLK, MIC_I2S_SCK, MIC_I2S_WS, -1, MIC_I2S_SDIN);
    codec.setEs7210MicMask(ES7210_SEL_MIC1 | ES7210_SEL_MIC2);
    if (!codec.begin(Wire, 0x40, CODEC_TYPE_ES7210)) {
        devicesProbe &= ~HW_CODEC_ONLINE;
        return false;
    }
    codec.setGain(9.0f);
    devicesProbe |= HW_CODEC_ONLINE;
    return true;
}

bool LilyGoTEmbed::initAmplifier()
{
    amplifierInitialized = audioOutput.begin() && audioOutput.open(16, 2, 16000);
    if (!amplifierInitialized) audioOutput.end();
    return amplifierInitialized;
}

bool LilyGoTEmbed::initSi4735()
{
    si4735.setAudioMuteMcuPin(SI4735_AUDIO_MUTE);
    si4735.setHardwareAudioMute(true);
    si4735.setI2CFastModeCustom(100000);
    const int16_t address = si4735.getDeviceI2CAddress(SI4735_RST);
    if (address == 0) {
        devicesProbe &= ~HW_SI473X_ONLINE;
        return false;
    }
    devicesProbe |= HW_SI473X_ONLINE;
    return true;
}

bool LilyGoTEmbed::configureEs7210Microphones(uint8_t micMask, bool forceTdm)
{
    return codec.configureEs7210Input(micMask, forceTdm);
}

bool LilyGoTEmbed::installSD(uint32_t spiFrequency)
{
    if (!spiFrequency) spiFrequency = LILYGO_TEMBED_SD_SPI_FREQ;
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    SD.end();
    const bool mounted = SD.begin(SD_CS, sdSpi, spiFrequency, "/sd") &&
                         SD.cardType() != CARD_NONE;
    if (mounted) devicesProbe |= HW_SD_ONLINE;
    else devicesProbe &= ~HW_SD_ONLINE;
    return mounted;
}

void LilyGoTEmbed::uninstallSD()
{
    SD.end();
    devicesProbe &= ~HW_SD_ONLINE;
}

void LilyGoTEmbed::setRotation(uint8_t rotation) { LilyGoDispArduinoSPI::setRotation(rotation); }
uint8_t LilyGoTEmbed::getRotation() { return LilyGoDispArduinoSPI::getRotation(); }
uint16_t LilyGoTEmbed::width() { return LilyGoDispArduinoSPI::_width; }
uint16_t LilyGoTEmbed::height() { return LilyGoDispArduinoSPI::_height; }
void LilyGoTEmbed::pushColors(uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height, uint16_t *color)
{
    LilyGoDispArduinoSPI::pushColors(x, y, width, height, color);
}

void LilyGoTEmbed::setBrightness(uint8_t level)
{
    brightnessLevel = min<uint8_t>(level, DEVICE_MAX_BRIGHTNESS_LEVEL);
    backlight.setBrightness(brightnessLevel);
}

void LilyGoTEmbed::sleepDisplay()
{
    if (brightnessLevel) brightnessBeforeSleep = brightnessLevel;
    backlight.setBrightness(0);
    LilyGoDispArduinoSPI::sleep();
}

void LilyGoTEmbed::wakeupDisplay()
{
    LilyGoDispArduinoSPI::wakeup();
    setBrightness(brightnessBeforeSleep);
}

void LilyGoTEmbed::powerControl(PowerCtrlChannel_t channel, bool enable)
{
    if (channel == POWER_SI4735_RADIO) {
        if (enable) {
            if (!isDeviceOnline(HW_SI473X_ONLINE) && !initSi4735()) return;
            if (!si4735Powered) {
                si4735.setup(SI4735_RST, POWER_UP_FM);
                si4735Powered = true;
            }
        } else if (si4735Powered) {
            si4735.powerDown();
            si4735Powered = false;
        }
    } else if (channel == POWER_DISPLAY_BACKLIGHT) {
        backlight.setBrightness(enable ? brightnessLevel : 0);
    } else if (channel == POWER_SPEAK && !enable && amplifierInitialized) {
        audioOutput.close();
        amplifierInitialized = false;
    } else if (channel == POWER_SPEAK && enable && !amplifierInitialized) {
        initAmplifier();
    }
}

RotaryMsg_t LilyGoTEmbed::getRotary()
{
    static bool pressed = false;
    RotaryMsg_t message = {};
    if (rotaryMsg && xQueueReceive(rotaryMsg, &message, pdMS_TO_TICKS(50)) == pdPASS) {
        pressed = message.centerBtnPressed;
        return message;
    }
    message.centerBtnPressed = pressed;
    return message;
}

void LilyGoTEmbed::clearRotaryMsg() { if (rotaryMsg) xQueueReset(rotaryMsg); }

bool LilyGoTEmbed::initRotary()
{
    if (rotaryHandler) return true;
    rotaryMsg = xQueueCreate(5, sizeof(RotaryMsg_t));
    rotaryTaskFlag = xEventGroupCreate();
    if (rotaryMsg && rotaryTaskFlag &&
            xTaskCreate(rotaryTaskEntry, "rotary", 2048, this, 10, &rotaryHandler) == pdPASS) {
        return true;
    }
    if (rotaryMsg) vQueueDelete(rotaryMsg);
    if (rotaryTaskFlag) vEventGroupDelete(rotaryTaskFlag);
    rotaryMsg = nullptr;
    rotaryTaskFlag = nullptr;
    rotaryHandler = nullptr;
    return false;
}

void LilyGoTEmbed::enableRotary()
{
    if (!rotaryHandler) return;
    if (digitalRead(ENCODER_KEY) == LOW) {
        xEventGroupSetBits(rotaryTaskFlag, kRotaryStartPressedFlag);
    }
    vTaskResume(rotaryHandler);
}

void LilyGoTEmbed::disableRotary() { if (rotaryHandler) vTaskSuspend(rotaryHandler); }
void LilyGoTEmbed::setRotaryStepDivider(uint8_t divider) { rotaryStepDivider = constrain(divider, 1, 8); }
void LilyGoTEmbed::rotaryTaskEntry(void *param) { static_cast<LilyGoTEmbed *>(param)->rotaryTaskLoop(); }

bool LilyGoTEmbed::getRotaryButtonState()
{
    static uint8_t state = HIGH;
    static uint8_t last = HIGH;
    static uint32_t changedAt = 0;
    const uint8_t reading = digitalRead(ENCODER_KEY);
    if (xEventGroupGetBits(rotaryTaskFlag) & kRotaryStartPressedFlag) {
        if (reading == HIGH) xEventGroupClearBits(rotaryTaskFlag, kRotaryStartPressedFlag);
        else return false;
    }
    if (reading != last) {
        changedAt = millis();
        last = reading;
    }
    if (millis() - changedAt > 20) state = reading;
    return state == LOW;
}

void LilyGoTEmbed::rotaryTaskLoop()
{
    bool lastPressed = false;
    uint8_t pendingDirection = DIR_NONE;
    uint8_t pendingSteps = 0;
    rotary.begin(true, false, true);
    pinMode(ENCODER_KEY, INPUT_PULLUP);
    while (true) {
        RotaryMsg_t message = {};
        message.centerBtnPressed = getRotaryButtonState();
        uint8_t result = rotary.process();
        if (result != DIR_NONE) {
            if (result == pendingDirection) ++pendingSteps;
            else { pendingDirection = result; pendingSteps = 1; }
            if (pendingSteps >= rotaryStepDivider) {
                pendingDirection = DIR_NONE;
                pendingSteps = 0;
            } else result = DIR_NONE;
        }
        const bool buttonChanged = message.centerBtnPressed != lastPressed;
        if (result != DIR_NONE || buttonChanged) {
            if (result == DIR_CW) { message.dir = ROTARY_DIR_DOWN; message.enc_diff = -1; }
            else if (result == DIR_CCW) { message.dir = ROTARY_DIR_UP; message.enc_diff = 1; }
            message.centerBtnClicked = buttonChanged && message.centerBtnPressed;
            message.centerBtnReleased = buttonChanged && !message.centerBtnPressed;
            lastPressed = message.centerBtnPressed;
            xQueueSend(rotaryMsg, &message, portMAX_DELAY);
        }
        delay(2);
    }
}

float LilyGoTEmbed::getBattVoltage()
{
    uint32_t samples[7];
    for (uint8_t i = 0; i < 7; ++i) {
        samples[i] = analogReadMilliVolts(BAT_ADC);
        delayMicroseconds(200);
    }
    std::sort(samples, samples + 7);
    return samples[3] * 2.0f;
}

float LilyGoTEmbed::getBatteryPercent() { return estimateBatteryPercent(getBattVoltage()); }

bool LilyGoTEmbed::readPowerSnapshot(LilyGoPowerSnapshot &snapshot)
{
    snapshot = LilyGoPowerSnapshot();
    snapshot.onlineMask = devicesProbe;
    snprintf(snapshot.pmicName, sizeof(snapshot.pmicName), "None");
    snprintf(snapshot.gaugeName, sizeof(snapshot.gaugeName), "Voltage estimate");
    const float voltage = getBattVoltage();
    if (voltage <= 0) return false;
    snapshot.batteryMv = {true, voltage, LILYGO_POWER_SRC_ADC};
    snapshot.batteryPercent = {true, estimateBatteryPercent(voltage), LILYGO_POWER_SRC_ESTIMATED};
    snapshot.batteryPresentValid = true;
    snapshot.batteryPresent = voltage > 2500.0f;
    snapshot.fuelGaugePresent = snapshot.batteryPresent;
    return true;
}

bool LilyGoTEmbed::shutdown()
{
    return false;
}

bool LilyGoTEmbed::lockSPI(TickType_t ticksToWait) { return LilyGoDispArduinoSPI::lock(ticksToWait); }
void LilyGoTEmbed::unlockSPI() { LilyGoDispArduinoSPI::unlock(); }

uint64_t LilyGoTEmbed::getWakeupPinMask(WakeupSource_t source) const
{
    return source & (WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON) ?
           (1ULL << ENCODER_KEY) : 0;
}

void LilyGoTEmbed::lightSleep(WakeupSource_t source)
{
    const uint64_t pins = getWakeupPinMask(source);
    if (!pins) return;
    pinMode(ENCODER_KEY, INPUT_PULLUP);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (esp_sleep_enable_ext1_wakeup_io(pins, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) return;
    disableRotary();
    sleepDisplay();
    esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    wakeupDisplay();
    enableRotary();
}

void LilyGoTEmbed::sleep(WakeupSource_t source, bool, uint32_t sleepSeconds)
{
    const bool timer = source & WAKEUP_SRC_TIMER;
    const uint64_t pins = getWakeupPinMask(source);
    if ((!pins && !timer) || (timer && !sleepSeconds)) return;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (pins) {
        pinMode(ENCODER_KEY, INPUT_PULLUP);
        if (esp_sleep_enable_ext1_wakeup_io(pins, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) return;
    }
    if (timer && esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL) != ESP_OK) return;
    sleepDisplay();
    digitalWrite(BOARD_POWERON, LOW);
    esp_deep_sleep_start();
}

namespace {
LilyGoTEmbed &getInstanceRef() { return *LilyGoTEmbed::getInstance(); }
}
LilyGoTEmbed &instance = getInstanceRef();

#endif
