/**
 * @file      LilyGo_T_Deck.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-28
 *
 */
#ifdef ARDUINO_T_DECK

#include "LilyGo_T_Deck.h"
#include "LilyGoLog.h"
#include "display/TDeckDisplayConfig.h"

#include <algorithm>
#include <cbuf.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>

#ifndef LILYGO_TDECK_SD_SPI_FREQ
#define LILYGO_TDECK_SD_SPI_FREQ 800000U
#endif

LILYGO_DEFINE_RADIO();

extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);

namespace
{
QueueHandle_t trackballQueue = nullptr;
static cbuf keyBuffer(32);

bool lockSharedSpi()
{
    return instance.lockSPI();
}

bool unlockSharedSpi()
{
    instance.unlockSPI();
    return true;
}

struct TrackballEvent {
    TrackballDir_t direction;
    TickType_t tick;
};

struct BatteryCurvePoint {
    uint16_t millivolts;
    uint8_t percent;
};

static constexpr float T_DECK_USB_DETECT_MV = 4200.0f;

float estimateBatteryPercent(float millivolts)
{
    static constexpr BatteryCurvePoint curve[] = {
        {3300, 0},
        {3500, 5},
        {3600, 10},
        {3700, 20},
        {3750, 30},
        {3800, 40},
        {3850, 50},
        {3900, 60},
        {3950, 70},
        {4000, 80},
        {4100, 90},
        {4200, 100},
    };

    if (millivolts <= curve[0].millivolts) {
        return curve[0].percent;
    }
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (millivolts <= curve[i].millivolts) {
            const BatteryCurvePoint &low = curve[i - 1];
            const BatteryCurvePoint &high = curve[i];
            return low.percent +
                   (millivolts - low.millivolts) * (high.percent - low.percent) /
                   (high.millivolts - low.millivolts);
        }
    }
    return 100.0f;
}

void IRAM_ATTR queueTrackballDirection(TrackballDir_t direction)
{
    if (!trackballQueue) {
        return;
    }
    BaseType_t taskWoken = pdFALSE;
    const TrackballEvent event = {direction, xTaskGetTickCountFromISR()};
    xQueueSendFromISR(trackballQueue, &event, &taskWoken);
    if (taskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void IRAM_ATTR trackballUpIsr()
{
    queueTrackballDirection(TRACKBALL_DIR_UP);
}

void IRAM_ATTR trackballDownIsr()
{
    queueTrackballDirection(TRACKBALL_DIR_DOWN);
}

void IRAM_ATTR trackballLeftIsr()
{
    queueTrackballDirection(TRACKBALL_DIR_LEFT);
}

void IRAM_ATTR trackballRightIsr()
{
    queueTrackballDirection(TRACKBALL_DIR_RIGHT);
}

void centerClickHandler(Button2 &)
{
    instance.sendEvent(DeviceEvent::button(BUTTON_CENTER, BUTTON_EVENT_CLICK));
}

void centerLongClickHandler(Button2 &)
{
    instance.sendEvent(DeviceEvent::button(BUTTON_CENTER, BUTTON_EVENT_LONG_PRESSED));
}

void centerDoubleClickHandler(Button2 &)
{
    instance.sendEvent(DeviceEvent::button(BUTTON_CENTER, BUTTON_EVENT_DOUBLE_CLICK));
}
}

EventGroupHandle_t LilyGoDeck::eventGroup = nullptr;

LilyGoDeck::LilyGoDeck() :
    LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispArduinoSPI(
        DISP_WIDTH,
        DISP_HEIGHT,
        lilygo::tdeck_display::st7789InitList,
        sizeof(lilygo::tdeck_display::st7789InitList) / sizeof(lilygo::tdeck_display::st7789InitList[0]),
        lilygo::tdeck_display::rotationConfig)
{
}

const char *LilyGoDeck::getName()
{
    return "LilyGo T-Deck";
}

const LilyGoDeviceCapability &LilyGoDeck::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        "LilyGo T-Deck", "SX1262 (optional)", "None", "None",
        true, false, true, true, true, true, false,
        false, false, false, false, false, false, false,
        true, true, false, true, false, true, true,
        PMIC_TYPE_UNKNOWN, true, false,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoDeck::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    return options;
}

uint32_t LilyGoDeck::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoDeck::begin(uint32_t disableHwInit)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disableHwInit));
}

uint32_t LilyGoDeck::begin(const LilyGoDeviceInitOptions &initOptions)
{
    if (eventGroup) {
        return devicesProbe;
    }

    eventGroup = xEventGroupCreate();
    if (!eventGroup) {
        LILYGO_LOG_E("Failed to create T-Deck event group");
        return 0;
    }

    devicesProbe = 0;
    if (psramFound()) {
        devicesProbe |= HW_PSRAM_ONLINE;
    } else {
        LILYGO_LOG_E("PSRAM not found");
    }

    if (initOptions.initFatfs) {
        setupMSC(lockSharedSpi, unlockSharedSpi);
    }

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    pinMode(BAT_ADC, INPUT);

    const uint8_t chipSelectPins[] = {DISP_CS, SD_CS, LORA_CS};
    for (uint8_t pin : chipSelectPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    pinMode(DISP_MISO, INPUT_PULLUP);

    if (!LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, -1)) {
        LILYGO_LOG_E("Failed to initialize ST7789");
        return devicesProbe;
    }

    backlight.begin(DISP_BL);

    Wire.begin(SDA, SCL);
    if (initOptions.initTouch) {
        initTouch();
    }
    if (initOptions.initKeyboard) {
        delay(500);
        initKeyboard();
    }
    if (initOptions.initPawA350) {
        initTrackball();
    }
    if (initOptions.initSd) {
        installSD();
    }
    if (initOptions.initRadio) {
        initLoRa();
    }
    if (initOptions.initGps) {
        initGPS();
    }
    if (initOptions.initAudio) {
        initAmplifier();
        if (initOptions.initCodec) {
            initCodec();
        }
    }
    return devicesProbe;
}

void LilyGoDeck::loop()
{
    bootButton.loop();

    if (hasKeyboard()) {
        char key = 0;
        if (kb.getKey(&key) > 0 && keyboardEnabled) {
            if (keyBuffer.full()) {
                keyBuffer.read();
            }
            keyBuffer.write(key);
        }
        if (!kb.isOnline()) {
            devicesProbe &= ~HW_KEYBOARD_ONLINE;
        }
    }

    static TickType_t lastTrackballTick[TRACKBALL_DIR_RIGHT + 1] = {};
    TrackballEvent trackballEvent;
    while (trackballQueue && xQueueReceive(trackballQueue, &trackballEvent, 0) == pdTRUE) {
        const uint8_t index = static_cast<uint8_t>(trackballEvent.direction);
        const TickType_t elapsed = trackballEvent.tick - lastTrackballTick[index];
        if (trackballEnabled && elapsed >= pdMS_TO_TICKS(5)) {
            lastTrackballTick[index] = trackballEvent.tick;
            sendEvent(DeviceEvent::trackball(trackballEvent.direction));
        }
    }

    if (gps.probeDone() && gps.probeSuccess()) {
        while (Serial1.available()) {
            gps.encode(static_cast<char>(Serial1.read()));
        }
    }
}

bool LilyGoDeck::isDeviceOnline(uint32_t mask) const
{
    return (devicesProbe & mask) != 0;
}

void LilyGoDeck::setRotation(uint8_t rotation)
{
    LilyGoDispArduinoSPI::setRotation(rotation);
}

uint8_t LilyGoDeck::getRotation()
{
    return LilyGoDispArduinoSPI::getRotation();
}

uint16_t LilyGoDeck::width()
{
    return LilyGoDispArduinoSPI::_width;
}

uint16_t LilyGoDeck::height()
{
    return LilyGoDispArduinoSPI::_height;
}

void LilyGoDeck::pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *color)
{
    LilyGoDispArduinoSPI::pushColors(x, y, width, height, color);
}

bool LilyGoDeck::needSwapColors()
{
    return true;
}

void LilyGoDeck::setBrightness(uint8_t level)
{
    backlight.setBrightness(level);
}

uint8_t LilyGoDeck::getBrightness() const
{
    return backlight.getBrightness();
}

void LilyGoDeck::sleepDisplay()
{
    const uint8_t brightness = getBrightness();
    if (brightness > 0) {
        brightnessBeforeSleep = brightness;
    }
    backlight.setBrightness(0);
    LilyGoDispArduinoSPI::sleep();
}

void LilyGoDeck::wakeupDisplay()
{
    LilyGoDispArduinoSPI::wakeup();
    setBrightness(brightnessBeforeSleep);
}

bool LilyGoDeck::initTouch()
{
    pinMode(TP_INT, OUTPUT);
    digitalWrite(TP_INT, HIGH);
    delay(8);
    pinMode(TP_INT, INPUT);

    touch.setPins(TP_RST, TP_INT);
    const bool result = touch.begin(Wire, GT911_SLAVE_ADDRESS_UNKNOWN, SDA, SCL);
    if (!result) {
        LILYGO_LOG_W("GT911 not detected");
        return false;
    }

    lilygo::tdeck_display::configureTouch(touch);
    devicesProbe |= HW_TOUCH_ONLINE;
    pinMode(TP_INT, INPUT);
    return true;
}

bool LilyGoDeck::hasTouch()
{
    return isDeviceOnline(HW_TOUCH_ONLINE);
}

uint8_t LilyGoDeck::getPoint(int16_t *x, int16_t *y, uint8_t count)
{
    if (!eventGroup || !x || !y || count == 0) {
        return 0;
    }

    const TouchPoints &points = touch.getTouchPoints();
    if (!points.hasPoints()) {
        return 0;
    }

    const uint8_t copyCount = min<uint8_t>(count, points.getPointCount());
    for (uint8_t i = 0; i < copyCount; ++i) {
        const TouchPoint &point = points.getPoint(i);
        x[i] = point.x;
        y[i] = point.y;
    }
    return copyCount;
}

bool LilyGoDeck::initKeyboard()
{
    if (!kb.begin(Wire)) {
        LILYGO_LOG_W("T-Deck keyboard not detected at 0x55");
        return false;
    }
    kb.setDefaultBrightness(127);
    devicesProbe |= HW_KEYBOARD_ONLINE;
    return true;
}

bool LilyGoDeck::hasKeyboard()
{
    return isDeviceOnline(HW_KEYBOARD_ONLINE);
}

int LilyGoDeck::getKeyChar(char *c)
{
    if (!keyboardEnabled || !c || keyBuffer.empty()) {
        return -1;
    }
    keyBuffer.read(c, 1);
    return KEYBOARD_PRESSED;
}

void LilyGoDeck::enableKeyboard()
{
    keyboardEnabled = true;
    keyBuffer.remove(keyBuffer.available());
}

void LilyGoDeck::disableKeyboard()
{
    keyboardEnabled = false;
    keyBuffer.remove(keyBuffer.available());
}

bool LilyGoDeck::initTrackball()
{
    if (!trackballQueue) {
        trackballQueue = xQueueCreate(16, sizeof(TrackballEvent));
    }
    if (!trackballQueue) {
        LILYGO_LOG_E("Failed to create trackball event queue");
        return false;
    }

    pinMode(TRACKBALL_UP, INPUT_PULLUP);
    pinMode(TRACKBALL_DOWN, INPUT_PULLUP);
    pinMode(TRACKBALL_LEFT, INPUT_PULLUP);
    pinMode(TRACKBALL_RIGHT, INPUT_PULLUP);
    attachInterrupt(TRACKBALL_UP, trackballUpIsr, FALLING);
    attachInterrupt(TRACKBALL_DOWN, trackballDownIsr, FALLING);
    attachInterrupt(TRACKBALL_LEFT, trackballLeftIsr, FALLING);
    attachInterrupt(TRACKBALL_RIGHT, trackballRightIsr, FALLING);

    bootButton.setClickHandler(centerClickHandler);
    bootButton.setLongClickHandler(centerLongClickHandler);
    bootButton.setDoubleClickHandler(centerDoubleClickHandler);
    return true;
}

bool LilyGoDeck::hasTrackball() const
{
    return trackballQueue != nullptr;
}

void LilyGoDeck::enableTrackBall()
{
    trackballEnabled = true;
}

void LilyGoDeck::disableTrackBall()
{
    trackballEnabled = false;
}

bool LilyGoDeck::initCodec()
{
    codec.setPins(MIC_I2S_MCLK, MIC_I2S_SCK, MIC_I2S_WS, -1, MIC_I2S_SDIN);
    codec.setEs7210MicMask(ES7210_SEL_MIC1 | ES7210_SEL_MIC3);
    if (!codec.begin(Wire, 0x40, CODEC_TYPE_ES7210)) {
        devicesProbe &= ~HW_CODEC_ONLINE;
        LILYGO_LOG_E("ES7210 microphone codec not detected");
        return false;
    }

    codec.setGain(6.0f);
    devicesProbe |= HW_CODEC_ONLINE;
    return true;
}

bool LilyGoDeck::initAmplifier()
{
    amplifierInitialized = false;
    if (!audioOutput.begin()) {
        LILYGO_LOG_E("Failed to initialize I2S speaker output");
        return false;
    }
    amplifierInitialized = audioOutput.open(16, 2, 16000);
    if (!amplifierInitialized) {
        audioOutput.end();
    }
    return amplifierInitialized;
}

bool LilyGoDeck::configureEs7210Microphones(uint8_t micMask, bool forceTdm)
{
    return codec.configureEs7210Input(micMask, forceTdm);
}

AudioInputIf *LilyGoDeck::getAudioInput()
{
    return &audioInput;
}

AudioOutputIf *LilyGoDeck::getAudioOutput()
{
    return &audioOutput;
}

uint8_t LilyGoDeck::getCodecInputChannels()
{
    return 2;
}

uint8_t LilyGoDeck::getCodecOutputChannels()
{
    return 2;
}

bool LilyGoDeck::installSD(uint32_t spiFrequency)
{
    if (spiFrequency == 0) {
        spiFrequency = LILYGO_TDECK_SD_SPI_FREQ;
    }
    if (!lockSPI()) {
        return false;
    }
    digitalWrite(DISP_CS, HIGH);
    digitalWrite(LORA_CS, HIGH);
    SD.end();
    const bool mounted = SD.begin(SD_CS, SPI, spiFrequency, "/sd") && SD.cardType() != CARD_NONE;
    if (mounted) {
        devicesProbe |= HW_SD_ONLINE;
    } else {
        devicesProbe &= ~HW_SD_ONLINE;
    }
    unlockSPI();
    return mounted;
}

void LilyGoDeck::uninstallSD()
{
    if (lockSPI()) {
        SD.end();
        unlockSPI();
    }
    devicesProbe &= ~HW_SD_ONLINE;
}

bool LilyGoDeck::isCardReady()
{
    return isDeviceOnline(HW_SD_ONLINE) && SD.cardType() != CARD_NONE;
}

bool LilyGoDeck::initLoRa()
{
    if (!lockSPI()) {
        return false;
    }
    digitalWrite(DISP_CS, HIGH);
    digitalWrite(SD_CS, HIGH);
    const int state = radio.begin();
    unlockSPI();

    if (state != RADIOLIB_ERR_NONE) {
        devicesProbe &= ~HW_RADIO_ONLINE;
        LILYGO_LOG_W("Optional SX1262 not detected, code: %d", state);
        return false;
    }
    devicesProbe |= HW_RADIO_ONLINE;
    return true;
}

bool LilyGoDeck::hasRadio() const
{
    return isDeviceOnline(HW_RADIO_ONLINE);
}

void LilyGoDeck::gpsProbeCallback(bool success, const char *model, void *userData)
{
    LilyGoDeck *deck = static_cast<LilyGoDeck *>(userData);
    if (!deck) {
        return;
    }
    if (success) {
        deck->devicesProbe |= HW_GPS_ONLINE;
        LILYGO_LOG_I("Optional GPS detected: %s", model ? model : "Unknown");
    } else {
        deck->devicesProbe &= ~HW_GPS_ONLINE;
        LILYGO_LOG_W("Optional GPS not detected");
    }
}

bool LilyGoDeck::initGPS()
{
    Serial1.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
    const GPSProbe probe = static_cast<GPSProbe>(GPS_PROBE_UBLOX | GPS_PROBE_QUECTEL_L76K);
    const bool started = gps.beginAsyncProbe(&Serial1, probe, gpsProbeCallback, this);
    if (!started) {
        devicesProbe &= ~HW_GPS_ONLINE;
    }
    return started;
}

bool LilyGoDeck::hasGPS()
{
    getDeviceProbe();
    return isDeviceOnline(HW_GPS_ONLINE);
}

uint32_t LilyGoDeck::getDeviceProbe()
{
    if (gps.probeDone()) {
        if (gps.probeSuccess()) {
            devicesProbe |= HW_GPS_ONLINE;
        } else {
            devicesProbe &= ~HW_GPS_ONLINE;
        }
    }
    return devicesProbe;
}

float LilyGoDeck::getBattVoltage()
{
    constexpr size_t sampleCount = 7;
    uint32_t samples[sampleCount];
    for (size_t i = 0; i < sampleCount; ++i) {
        samples[i] = analogReadMilliVolts(BAT_ADC);
        delayMicroseconds(200);
    }
    std::sort(samples, samples + sampleCount);
    return static_cast<float>(samples[sampleCount / 2] * 2U);
}

float LilyGoDeck::getBatteryPercent()
{
    return estimateBatteryPercent(getBattVoltage());
}

bool LilyGoDeck::readPowerSnapshot(LilyGoPowerSnapshot &snapshot)
{
    snapshot = LilyGoPowerSnapshot();
    snapshot.onlineMask = getDeviceProbe();
    snprintf(snapshot.pmicName, sizeof(snapshot.pmicName), "None");
    snprintf(snapshot.gaugeName, sizeof(snapshot.gaugeName), "Voltage estimate");
    snprintf(snapshot.chargeState, sizeof(snapshot.chargeState), "Unavailable");
    snprintf(snapshot.ntcState, sizeof(snapshot.ntcState), "Unknown");

    const float voltage = getBattVoltage();
    if (voltage <= 0.0f) {
        return false;
    }
    snapshot.batteryMv.valid = true;
    snapshot.batteryMv.value = voltage;
    snapshot.batteryMv.source = LILYGO_POWER_SRC_ADC;
    snapshot.vbusPresentValid = true;
    snapshot.vbusPresent = voltage > T_DECK_USB_DETECT_MV;
    snapshot.charging = snapshot.vbusPresent;
    snprintf(snapshot.chargeState, sizeof(snapshot.chargeState),
             snapshot.vbusPresent ? "USB connected" : "On battery");
    snapshot.batteryPresentValid = true;
    snapshot.batteryPresent = voltage > 2500.0f;
    if (snapshot.batteryPresent) {
        snapshot.fuelGaugePresent = true;
        snapshot.batteryPercent.valid = true;
        snapshot.batteryPercent.value = estimateBatteryPercent(voltage);
        snapshot.batteryPercent.source = LILYGO_POWER_SRC_ESTIMATED;
    }
    return true;
}

PmicType LilyGoDeck::getPmicType() const
{
    return PMIC_TYPE_UNKNOWN;
}

bool LilyGoDeck::hasOTG()
{
    return false;
}

bool LilyGoDeck::isOTGEnabled()
{
    return false;
}

bool LilyGoDeck::enableOTG()
{
    return false;
}

bool LilyGoDeck::disableOTG()
{
    return false;
}

bool LilyGoDeck::isAdapterConnected()
{
    return getBattVoltage() > T_DECK_USB_DETECT_MV;
}

bool LilyGoDeck::shutdown()
{
    return false;
}

bool LilyGoDeck::isEnableCharge()
{
    return false;
}

bool LilyGoDeck::enableCharge()
{
    return false;
}

bool LilyGoDeck::disableCharge()
{
    return false;
}

uint16_t LilyGoDeck::getChargeCurrent()
{
    return 0;
}

void LilyGoDeck::setChargeCurrent(uint16_t)
{
}

void LilyGoDeck::getChargeConfig(uint16_t &minimum, uint16_t &maximum, uint16_t &step, uint16_t &steps)
{
    minimum = 0;
    maximum = 0;
    step = 0;
    steps = 0;
}

uint16_t LilyGoDeck::getChargeLevelToCurrent(uint8_t)
{
    return 0;
}

uint16_t LilyGoDeck::getChargeCurrentToLevel()
{
    return 0;
}

uint64_t LilyGoDeck::getWakeupPinMask(WakeupSource_t wakeupSource) const
{
    uint64_t pins = 0;
    if (wakeupSource & WAKEUP_SRC_BOOT_BUTTON) {
        pins |= 1ULL << TRACKBALL_CLICK;
    }
    return pins;
}

void LilyGoDeck::lightSleep(WakeupSource_t wakeupSource)
{
    const uint64_t wakeupPins = getWakeupPinMask(wakeupSource);
    if (wakeupPins == 0) {
        LILYGO_LOG_E("No supported T-Deck light-sleep wake source selected");
        return;
    }

    pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
    delayMicroseconds(50);
    if (digitalRead(TRACKBALL_CLICK) == LOW) {
        LILYGO_LOG_W("T-Deck light sleep skipped: BOOT button is still pressed");
        return;
    }


    gpio_reset_pin((gpio_num_t )GPS_RX);
    gpio_reset_pin((gpio_num_t )GPS_TX);
    pinMode(GPS_RX, OPEN_DRAIN);
    pinMode(GPS_TX, OPEN_DRAIN);

    const bool codecWasOnline = isDeviceOnline(HW_CODEC_ONLINE);
    if (codecWasOnline) {
        codec.end();
        devicesProbe &= (~HW_CODEC_ONLINE);
    }
    const bool amplifierWasInitialized = amplifierInitialized;
    if (amplifierWasInitialized) {
        audioOutput.end();
        amplifierInitialized = false;
    }

    const bool keyboardWasOnline = hasKeyboard();
    const uint8_t keyboardBrightness = keyboardWasOnline ? kb.getBrightness() : 0;

    sleepDisplay();
    if (hasRadio()) {
        radio.sleep();
    }
    if (keyboardWasOnline) {
        if (!kb.setBrightness(0)) {
            LILYGO_LOG_W("Failed to turn off T-Deck keyboard backlight");
        }
        kb.end();
    }
    if (hasTouch()) {
        touch.sleep();
    }

    LILYGO_LOG_I("T-Deck entering light sleep; wake pin GPIO%d", TRACKBALL_CLICK);
    Serial.flush();
    delay(1000);
    Serial.end();

    pinMode(TRACKBALL_CLICK, INPUT_PULLUP);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t wakeupResult = gpio_wakeup_enable(
                                 static_cast<gpio_num_t>(TRACKBALL_CLICK),
                                 GPIO_INTR_LOW_LEVEL);
    if (wakeupResult == ESP_OK) {
        wakeupResult = esp_sleep_enable_gpio_wakeup();
    }

    esp_err_t sleepResult = ESP_ERR_INVALID_STATE;
    if (wakeupResult == ESP_OK) {
        sleepResult = esp_light_sleep_start();
    }
    const esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    gpio_wakeup_disable(static_cast<gpio_num_t>(TRACKBALL_CLICK));
    if (wakeupResult == ESP_OK) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    }

    Serial.begin(115200);
    Serial.println("T-Deck woke from light sleep");

    wakeupDisplay();
    if (hasRadio()) {
        radio.standby();
    }
    if (keyboardWasOnline && initKeyboard()) {
        if (!kb.setBrightness(keyboardBrightness)) {
            LILYGO_LOG_W("Failed to restore T-Deck keyboard backlight");
        }
    }
    if (hasTouch()) {
        touch.wakeup();
        pinMode(TP_INT, INPUT);
    }

    if (wakeupResult != ESP_OK) {
        LILYGO_LOG_E("Failed to enable T-Deck BOOT wakeup: %s",
                     esp_err_to_name(wakeupResult));
    } else if (sleepResult != ESP_OK) {
        LILYGO_LOG_E("T-Deck light sleep failed: %s, BOOT level:%d",
                     esp_err_to_name(sleepResult), digitalRead(TRACKBALL_CLICK));
    } else {
        LILYGO_LOG_I("T-Deck woke from light sleep; cause:%d",
                     static_cast<int>(wakeupCause));
    }

    if (codecWasOnline) {
        initCodec();
    }
    if (amplifierWasInitialized) {
        initAmplifier();
    }

    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);


}

void LilyGoDeck::sleep(WakeupSource_t wakeupSource, bool, uint32_t sleepSeconds)
{
    uint64_t wakeupPins = 0;
    const bool timerWakeup = wakeupSource & WAKEUP_SRC_TIMER;
    const WakeupSource_t physicalSources = static_cast<WakeupSource_t>(
            static_cast<uint32_t>(wakeupSource) & ~static_cast<uint32_t>(WAKEUP_SRC_TIMER));

    if (timerWakeup && sleepSeconds == 0) {
        LILYGO_LOG_E("Timer wakeup requires a non-zero sleep duration");
        return;
    }
    if (physicalSources) {
        wakeupPins = getWakeupPinMask(physicalSources);
        if (wakeupPins == 0) {
            LILYGO_LOG_E("No supported T-Deck deep-sleep wake source selected");
            return;
        }
    } else if (!timerWakeup) {
        LILYGO_LOG_E("No supported T-Deck deep-sleep wake source selected");
        return;
    }

    if (wakeupPins != 0) {
        pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
        delayMicroseconds(50);
        if (digitalRead(TRACKBALL_CLICK) == LOW) {
            LILYGO_LOG_W("T-Deck deep sleep skipped: BOOT button is still pressed");
            return;
        }
    }

    sleepDisplay();
    if (hasRadio()) {
        radio.sleep();
    }
    if (hasKeyboard()) {
        if (!kb.setBrightness(0)) {
            LILYGO_LOG_W("Failed to turn off T-Deck keyboard backlight");
        }
        kb.end();
    }
    if (hasTouch()) {
        touch.sleep();
    }

    LILYGO_LOG_I("T-Deck entering deep sleep");
    Serial.flush();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t wakeupResult = ESP_OK;
    if (timerWakeup) {
        wakeupResult = esp_sleep_enable_timer_wakeup(
                           static_cast<uint64_t>(sleepSeconds) * 1000000ULL);
    }
    if (wakeupResult == ESP_OK && wakeupPins != 0) {
        // GPIO0 is shared by BOOT and the trackball center switch. Configure it
        // only after all peripherals are quiet, then require a stable release.
        gpio_reset_pin(static_cast<gpio_num_t>(TRACKBALL_CLICK));
        pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
        rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(TRACKBALL_CLICK));
        rtc_gpio_pullup_en(static_cast<gpio_num_t>(TRACKBALL_CLICK));
        delay(20);
        if (digitalRead(TRACKBALL_CLICK) == LOW) {
            LILYGO_LOG_E("T-Deck BOOT/trackball button is active before deep sleep");
            ESP.restart();
            return;
        }
        wakeupResult = esp_sleep_enable_ext1_wakeup_io(
                           wakeupPins, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if (wakeupResult != ESP_OK) {
        LILYGO_LOG_E("Failed to configure T-Deck deep-sleep wakeup: %s",
                     esp_err_to_name(wakeupResult));
        ESP.restart();
        return;
    }
    esp_deep_sleep_start();
}

bool LilyGoDeck::lockSPI(TickType_t ticksToWait)
{
    return LilyGoDispArduinoSPI::lock(ticksToWait);
}

void LilyGoDeck::unlockSPI()
{
    LilyGoDispArduinoSPI::unlock();
}

namespace
{
LilyGoDeck &getInstanceRef()
{
    return *LilyGoDeck::getInstance();
}
}

LilyGoDeck &instance = getInstanceRef();

#endif // ARDUINO_T_DECK
