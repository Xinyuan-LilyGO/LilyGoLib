/**
 * @file      LilyGo_LoRa_Pager.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-17
 *
 */

#ifdef ARDUINO_T_LORA_PAGER
#include "LilyGoLog.h"
#include "LilyGo_LoRa_Pager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "LilyGoLib.h"
#include "core/LilyGoGeneral.h"
#include "input/LilyGoRotaryInput.h"
#include <SensorWireHelper.h>
#include <cbuf.h>
#include "driver/rtc_io.h"
#include <Preferences.h>

#ifndef LILYGO_LORA_PAGER_SD_SPI_FREQ
#define LILYGO_LORA_PAGER_SD_SPI_FREQ 40000000U
#endif

#ifndef LILYGO_LORA_PAGER_ROTARY_COUNTS_PER_STEP
#define LILYGO_LORA_PAGER_ROTARY_COUNTS_PER_STEP 1
#endif

#ifndef LILYGO_LORA_PAGER_ROTARY_MAX_STEP_DIVIDER
#define LILYGO_LORA_PAGER_ROTARY_MAX_STEP_DIVIDER LILYGO_ROTARY_COUNTS_PER_STEP_MAX
#endif

#ifndef LILYGO_LORA_PAGER_ROTARY_DIRECTION_SIGN
#define LILYGO_LORA_PAGER_ROTARY_DIRECTION_SIGN -1
#endif

#ifndef LILYGO_LORA_PAGER_GPS_BAUDRATE
#define LILYGO_LORA_PAGER_GPS_BAUDRATE 38400U
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_RUNTIME_DETECT
#define LILYGO_LORA_PAGER_RADIO_RUNTIME_DETECT 1
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_PIN
#define LILYGO_LORA_PAGER_RADIO_DETECT_PIN LORA_IRQ
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_LOW
#define LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_LOW 1200U
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_HIGH
#define LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_HIGH 2900U
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_ADC_RAIL_LOW
#define LILYGO_LORA_PAGER_RADIO_DETECT_ADC_RAIL_LOW 64U
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_ADC_RAIL_HIGH
#define LILYGO_LORA_PAGER_RADIO_DETECT_ADC_RAIL_HIGH 4030U
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES
#define LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES 11
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES
#define LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES 7
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_SETTLE_MS
#define LILYGO_LORA_PAGER_RADIO_DETECT_SETTLE_MS 2
#endif

#ifndef LILYGO_LORA_PAGER_RADIO_DETECT_PULLDOWN_ADC_LOW
#define LILYGO_LORA_PAGER_RADIO_DETECT_PULLDOWN_ADC_LOW 500U
#endif

#ifndef LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY
#define LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY 1500U
#endif

#ifndef LILYGO_LORA_PAGER_GAUGE_FULL_CHARGE_CAPACITY
#define LILYGO_LORA_PAGER_GAUGE_FULL_CHARGE_CAPACITY 1500U
#endif

#ifndef LILYGO_LORA_PAGER_GAUGE_CONFIG_VERSION
#define LILYGO_LORA_PAGER_GAUGE_CONFIG_VERSION 1U
#endif

LILYGO_DEFINE_RADIO();

RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
RfalNfcClass NFCReader(&nfc_hw);

EventGroupHandle_t LilyGoLoRaPager::_event;
static TimerHandle_t timerHandler = NULL;
static LilyGoRotaryInput rotaryInput;
extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);
static uint32_t _devices_probe = 0;

static constexpr char LORA_PAGER_NVS_NAMESPACE[] = "lora_pager";
static constexpr char LORA_PAGER_GAUGE_CONFIG_KEY[] = "gauge_cfg";
static constexpr uint32_t LORA_PAGER_GAUGE_CONFIG_MAGIC = 0x47554346; // GUCF
static constexpr uint8_t LORA_PAGER_GAUGE_CONFIG_OK = 1;
static constexpr uint8_t LORA_PAGER_GAUGE_CONFIG_FAILED = 2;

struct LoraPagerGaugeConfigRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t designCapacity;
    uint16_t fullChargeCapacity;
    uint8_t status;
    uint8_t reserved[7];
};

struct LoRaDetectAdcStats {
    uint16_t minValue;
    uint16_t maxValue;
    uint16_t median;
};

static uint8_t clampRotaryStepDivider(uint8_t divider)
{
    uint8_t minDivider = LilyGoRotaryInput::getCountsPerStepMin();
    uint8_t maxDivider = LILYGO_LORA_PAGER_ROTARY_MAX_STEP_DIVIDER;
    if (maxDivider < minDivider) {
        maxDivider = minDivider;
    }
    if (divider < minDivider) {
        return minDivider;
    }
    if (divider > maxDivider) {
        return maxDivider;
    }
    return divider;
}

static bool gaugeConfigMatchesTarget(const LoraPagerGaugeConfigRecord &record)
{
    return record.magic == LORA_PAGER_GAUGE_CONFIG_MAGIC &&
           record.version == LILYGO_LORA_PAGER_GAUGE_CONFIG_VERSION &&
           record.size == sizeof(LoraPagerGaugeConfigRecord) &&
           record.designCapacity == LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY &&
           record.fullChargeCapacity == LILYGO_LORA_PAGER_GAUGE_FULL_CHARGE_CAPACITY;
}

static bool loadGaugeConfigRecord(LoraPagerGaugeConfigRecord &record)
{
    Preferences prefs;
    if (!prefs.begin(LORA_PAGER_NVS_NAMESPACE, true)) {
        LILYGO_LOG_W("Gauge config NVS open failed");
        return false;
    }
    bool loaded = prefs.getBytes(LORA_PAGER_GAUGE_CONFIG_KEY, &record, sizeof(record)) == sizeof(record);
    prefs.end();
    return loaded && gaugeConfigMatchesTarget(record);
}

static void saveGaugeConfigRecord(uint8_t status)
{
    LoraPagerGaugeConfigRecord record = {};
    record.magic = LORA_PAGER_GAUGE_CONFIG_MAGIC;
    record.version = LILYGO_LORA_PAGER_GAUGE_CONFIG_VERSION;
    record.size = sizeof(record);
    record.designCapacity = LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY;
    record.fullChargeCapacity = LILYGO_LORA_PAGER_GAUGE_FULL_CHARGE_CAPACITY;
    record.status = status;

    Preferences prefs;
    if (!prefs.begin(LORA_PAGER_NVS_NAMESPACE, false)) {
        LILYGO_LOG_W("Gauge config NVS open failed");
        return;
    }
    if (prefs.putBytes(LORA_PAGER_GAUGE_CONFIG_KEY, &record, sizeof(record)) != sizeof(record)) {
        LILYGO_LOG_W("Gauge config NVS save failed");
    }
    prefs.end();
}

static bool configureGaugeCapacityIfNeeded(GaugeBQ27220 &gauge)
{
    LoraPagerGaugeConfigRecord record = {};

    if (gauge.refresh() && gauge.getDesignCapacity() == LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY) {
        LILYGO_LOG_I("Gauge capacity already matches target design=%u",
                     (unsigned)LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY);
        saveGaugeConfigRecord(LORA_PAGER_GAUGE_CONFIG_OK);
        return true;
    }

    if (loadGaugeConfigRecord(record)) {
        if (record.status == LORA_PAGER_GAUGE_CONFIG_OK) {
            LILYGO_LOG_I("Gauge capacity config already recorded design=%u full=%u",
                         (unsigned)record.designCapacity,
                         (unsigned)record.fullChargeCapacity);
            return true;
        }
        if (record.status == LORA_PAGER_GAUGE_CONFIG_FAILED) {
            LILYGO_LOG_W("Skip gauge capacity config after previous failure design=%u full=%u",
                         (unsigned)record.designCapacity,
                         (unsigned)record.fullChargeCapacity);
            return false;
        }
    }

    LILYGO_LOG_I("Configure gauge capacity design=%u full=%u",
                 (unsigned)LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY,
                 (unsigned)LILYGO_LORA_PAGER_GAUGE_FULL_CHARGE_CAPACITY);
    bool ok = gauge.setNewCapacity(LILYGO_LORA_PAGER_GAUGE_DESIGN_CAPACITY,
                                   LILYGO_LORA_PAGER_GAUGE_FULL_CHARGE_CAPACITY);
    saveGaugeConfigRecord(ok ? LORA_PAGER_GAUGE_CONFIG_OK : LORA_PAGER_GAUGE_CONFIG_FAILED);
    return ok;
}

static void sortLoRaDetectSamples(uint16_t *samples, uint8_t count)
{
    for (uint8_t i = 1; i < count; ++i) {
        uint16_t value = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > value) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = value;
    }
}

static LoRaDetectAdcStats readLoRaDetectAdcStats(uint8_t mode)
{
    uint16_t samples[LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES] = {};

    gpio_reset_pin((gpio_num_t)LILYGO_LORA_PAGER_RADIO_DETECT_PIN);
    pinMode(LILYGO_LORA_PAGER_RADIO_DETECT_PIN, mode);
    delay(LILYGO_LORA_PAGER_RADIO_DETECT_SETTLE_MS);
    (void)analogRead(LILYGO_LORA_PAGER_RADIO_DETECT_PIN);
    delayMicroseconds(250);

    for (uint8_t i = 0; i < LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES; ++i) {
        samples[i] = analogRead(LILYGO_LORA_PAGER_RADIO_DETECT_PIN);
        delayMicroseconds(250);
    }

    sortLoRaDetectSamples(samples, LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES);

    LoRaDetectAdcStats stats = {
        samples[0],
        samples[LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES - 1],
        samples[LILYGO_LORA_PAGER_RADIO_DETECT_SAMPLES / 2]
    };
    return stats;
}

static uint8_t countLoRaDetectPinHigh(uint8_t mode)
{
    uint8_t highSamples = 0;

    gpio_reset_pin((gpio_num_t)LILYGO_LORA_PAGER_RADIO_DETECT_PIN);
    pinMode(LILYGO_LORA_PAGER_RADIO_DETECT_PIN, mode);
    delay(LILYGO_LORA_PAGER_RADIO_DETECT_SETTLE_MS);

    for (uint8_t i = 0; i < LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES; ++i) {
        if (digitalRead(LILYGO_LORA_PAGER_RADIO_DETECT_PIN) == HIGH) {
            ++highSamples;
        }
        delayMicroseconds(250);
    }

    return highSamples;
}

static bool detectNoLoRaDividerByPullResponse(uint8_t &pullupHighSamples,
        uint8_t &pulldownHighSamples)
{
    pullupHighSamples = countLoRaDetectPinHigh(INPUT_PULLUP);
    pulldownHighSamples = countLoRaDetectPinHigh(INPUT_PULLDOWN);
    pinMode(LILYGO_LORA_PAGER_RADIO_DETECT_PIN, INPUT);

    return pullupHighSamples > (LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES / 2) &&
           pulldownHighSamples <= (LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES / 2);
}

static bool detectLoRaHardwarePresent()
{
#if !LILYGO_LORA_PAGER_RADIO_RUNTIME_DETECT
    return true;
#else
    LoRaDetectAdcStats inputAdc = readLoRaDetectAdcStats(INPUT);
    uint16_t minValue = inputAdc.minValue;
    uint16_t maxValue = inputAdc.maxValue;
    uint16_t median = inputAdc.median;
    bool noLoRaByAdc = median >= LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_LOW &&
                       median <= LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_HIGH;
    bool adcNearRail = median <= LILYGO_LORA_PAGER_RADIO_DETECT_ADC_RAIL_LOW ||
                       median >= LILYGO_LORA_PAGER_RADIO_DETECT_ADC_RAIL_HIGH;
    LoRaDetectAdcStats pullupAdc = {};
    LoRaDetectAdcStats pulldownAdc = {};
    uint8_t pullupHighSamples = 0;
    uint8_t pulldownHighSamples = 0;
    bool noLoRaByPullResponse = false;
    bool pulldownAdcLow = false;

    if (noLoRaByAdc || adcNearRail) {
        noLoRaByPullResponse = detectNoLoRaDividerByPullResponse(pullupHighSamples,
                               pulldownHighSamples);
        pullupAdc = readLoRaDetectAdcStats(INPUT_PULLUP);
        pulldownAdc = readLoRaDetectAdcStats(INPUT_PULLDOWN);
        pulldownAdcLow = pulldownAdc.median <= LILYGO_LORA_PAGER_RADIO_DETECT_PULLDOWN_ADC_LOW;
    }

    bool noLoRaDivider = (noLoRaByAdc && !pulldownAdcLow) ||
                         (adcNearRail && noLoRaByPullResponse && !pulldownAdcLow);
    bool present = !noLoRaDivider;
    LILYGO_LOG_I("LoRa detect pull adc pin=%d input=%u/%u/%u pull_up=%u/%u/%u pull_down=%u/%u/%u pulldown_low<=%u pd_low=%d",
                 LILYGO_LORA_PAGER_RADIO_DETECT_PIN,
                 inputAdc.minValue,
                 inputAdc.median,
                 inputAdc.maxValue,
                 pullupAdc.minValue,
                 pullupAdc.median,
                 pullupAdc.maxValue,
                 pulldownAdc.minValue,
                 pulldownAdc.median,
                 pulldownAdc.maxValue,
                 LILYGO_LORA_PAGER_RADIO_DETECT_PULLDOWN_ADC_LOW,
                 pulldownAdcLow);
    LILYGO_LOG_I("LoRa detect adc pin=%d median=%u min=%u max=%u no_lora_window=%u-%u rail=%u pull_up=%u/%u pull_down=%u/%u divider=%d present=%d",
                 LILYGO_LORA_PAGER_RADIO_DETECT_PIN,
                 median,
                 minValue,
                 maxValue,
                 LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_LOW,
                 LILYGO_LORA_PAGER_RADIO_DETECT_NO_LORA_ADC_HIGH,
                 adcNearRail,
                 pullupHighSamples,
                 LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES,
                 pulldownHighSamples,
                 LILYGO_LORA_PAGER_RADIO_DETECT_PULL_SAMPLES,
                 noLoRaDivider,
                 present);
    pinMode(LILYGO_LORA_PAGER_RADIO_DETECT_PIN, INPUT);
    return present;
#endif
}

#ifndef RADIOLIB_EXCLUDE_NRF24
nRF24 nrf24 = new Module(44/*CS*/, 9/*IRQ*/, 43/*CE*/);
#endif

static const CommandTable_t st7796_init_list[19] = {
    {0x01, {0x00}, 0x80},
    {0x11, {0x00}, 0x80},
    {0xF0, {0xC3}, 0x01},
    {0xF0, {0xC3}, 0x01},
    {0xF0, {0x96}, 0x01},
    {0x36, {0x48}, 0x01},
    {0x3A, {0x55}, 0x01},
    {0xB4, {0x01}, 0x01},
    {0xB6, {0x80, 0x02, 0x3B}, 0x03},
    {0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 0x08},
    {0xC1, {0x06}, 0x01},
    {0xC2, {0xA7}, 0x01},
    {0xC5, {0x18}, 0x81},
    {0xE0, {0xF0, 0x09, 0x0b, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 0x0F},
    {0xE1, {0xE0, 0x09, 0x0b, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 0x8F},
    {0xF0, {0x3c}, 0x01},
    {0xF0, {0x69}, 0x81},
    {0x21, {0x00}, 0x01},
    {0x29, {0x00}, 0x01},
};


// 4x10 character map
static constexpr char keymap[4][10] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n'},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\0'},
    {' ',/*Space*/ '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};
// 4x10 symbol map
static constexpr char symbol_map[4][10] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0'},
    {' '/*Space*/, '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

static const LilyGoKeyboardConfig keyboardConfig = {
    .layout = {
        .kb_rows = 4,
        .kb_cols = 10,
        .current_keymap = &keymap[0][0],
        .current_symbol_map = &symbol_map[0][0],
        .has_symbol_key = false,
        .space_as_symbol_key = true
    },
    .modifiers = {
        .symbol_key_value = 0x1E,
        .alt_key_value = 0x14,
        .caps_key_value = 0x1C,
        .caps_b_key_value = 0xFF,
        .fn_key_value = 0xFF,
        .ctrl_key_value = 0xFF,
        .shift_key_value = 0xFF,
        .backspace_value = 0x1D,
    },
};
static cbuf _key_buf(32);

static const DispRotationConfig_t rotation_config[4] = {
    {0xE8, DISP_HEIGHT, DISP_WIDTH, 0, 49},
    {0x48, DISP_WIDTH, DISP_HEIGHT, 49, 0},
    {0x28, DISP_HEIGHT, DISP_WIDTH, 0, 49},
    {0x88, DISP_WIDTH, DISP_HEIGHT, 49, 0},
};

static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

static void clickHandler(Button2 &btn)
{
    LILYGO_LOG_D("Click event");
    instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_CLICK));
}

static void longClickHandler(Button2 &btn)
{
    LILYGO_LOG_D("Long click event");
    instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_LONG_PRESSED));
}

static void doubleClickHandler(Button2 &btn)
{
    LILYGO_LOG_D("Double click event");
    instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_DOUBLE_CLICK));
}

LilyGoLoRaPager::LilyGoLoRaPager() : LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispArduinoSPI(DISP_WIDTH, DISP_HEIGHT, st7796_init_list,
                         sizeof(st7796_init_list) / sizeof(st7796_init_list[0]), rotation_config),
    LilyGoEventManage(), LilyGoPowerManageInf(pmic, PMIC_TYPE_BQ25896)
{
    _effects = 1;
    _brightness = 0;    //Default disp is brightness is zero
    _boot_images_addr = nullptr;
}

LilyGoLoRaPager::~LilyGoLoRaPager()
{

}

const char *LilyGoLoRaPager::getName()
{
    return "LilyGo T-LoRa-Pager (2025)";
}

const LilyGoDeviceCapability &LilyGoLoRaPager::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        /* boardName */       "LilyGo T-LoRa-Pager (2025)",
#ifdef USING_RADIO_NAME
        /* radioName */       USING_RADIO_NAME,
#else
        /* radioName */       "None",
#endif
        /* pmicName */        "BQ25896",
        /* gaugeName */       "BQ27220",
        /* hasSd */           true,
        /* hasGps */          true,
        /* gpsRuntimeProbe */ false,
        /* hasTouch */        false,
        /* hasKeyboard */     true,
        /* hasTrackball */    false,
        /* hasRotary */       true,
        /* hasBma423 */       false,
        /* hasBhi260 */       true,
        /* hasNfc */          true,
        /* hasIrTx */         false,
        /* hasIrRx */         false,
        /* hasEnvSensor */    false,
        /* hasCompass */      false,
        /* hasAudioOut */     true,
        /* hasAudioIn */      true,
        /* hasHaptic */       true,
        /* hasExternalI2c */  true,
        /* hasExternalSpi */  true,
        /* hasExternalUart */ true,
        /* hasExternalGpio */ true,
        /* pmicType */        PMIC_TYPE_BQ25896,
        /* hasButton */       true,
        /* hasPmuButton */    false,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoLoRaPager::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initRtc = true;
    options.initCodec = true;
    return options;
}

bool LilyGoLoRaPager::hasEncoder()
{
    return rotaryInput.isRunning();
}

bool LilyGoLoRaPager::hasKeyboard()
{
    return _devices_probe & HW_KEYBOARD_ONLINE;
}

void LilyGoLoRaPager::setRotation(uint8_t rotation)
{
    LilyGoDispArduinoSPI::setRotation(rotation);
}

uint8_t LilyGoLoRaPager::getRotation()
{
    return LilyGoDispArduinoSPI::getRotation();
}

uint16_t  LilyGoLoRaPager::width()
{
    return LilyGoDispArduinoSPI::_width;
}

uint16_t  LilyGoLoRaPager::height()
{
    return LilyGoDispArduinoSPI::_height;
}

void LilyGoLoRaPager::setBootImage(uint8_t *image)
{
    _boot_images_addr = image;
}

void LilyGoLoRaPager::initShareSPIPins()
{
    const uint8_t share_spi_bus_devices_cs_pins[] = {
#ifdef NFC_RST
        NFC_RST,
#endif
        NFC_CS,
        LORA_CS,
        SD_CS,
        LORA_RST,
    };
    for (auto pin : share_spi_bus_devices_cs_pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

uint32_t LilyGoLoRaPager::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoLoRaPager::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

uint32_t LilyGoLoRaPager::begin(const LilyGoDeviceInitOptions &init_options)
{
    bool res = false;

    LILYGO_LOG_D("LilyGoLib run with %d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);

    if (_event) {
        return _devices_probe;
    }

    _event = xEventGroupCreate();

    _devices_probe = 0x00;

    while (!psramFound()) {
        LILYGO_LOG_E("ERROR:PSRAM NOT FOUND!"); delay(1000);
    }

    _devices_probe |= HW_PSRAM_ONLINE;

    Wire.begin(SDA, SCL);

    if (init_options.scanI2c) {
        LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire, Serial));
    }

    if (init_options.initGauge) {
        if (!gauge.begin(Wire, SDA, SCL)) {
            LILYGO_LOG_E("Failed to find GAUGE.");
        } else {
            LILYGO_LOG_D("Initializing GAUGE succeeded");
            _devices_probe |= HW_GAUGE_ONLINE;
            configureGaugeCapacityIfNeeded(gauge);
        }
    }

    if (init_options.initPmu) {
        res = initPMU();
        if (!res) {
            LILYGO_LOG_E("Failed to find PMU.");
        } else {
            LILYGO_LOG_D("Initializing PMU succeeded");
            _devices_probe |= HW_PMU_ONLINE;
        }
    }

#ifdef USING_XL9555_EXPANDS
    if (io.begin(Wire, 0x20)) {
        LILYGO_LOG_D("Initializing expand succeeded");
        _devices_probe |= HW_EXPAND_ONLINE;
        const uint8_t expands[] = {
#ifdef  EXPANDS_DISP_RST
            EXPANDS_DISP_RST,
#endif  /*EXPANDS_DISP_RST*/
            EXPANDS_KB_RST,
            EXPANDS_LORA_EN,
            EXPANDS_GPS_EN,
            EXPANDS_DRV_EN,
            EXPANDS_AMP_EN,
            EXPANDS_NFC_EN,
#ifdef EXPANDS_GPS_RST
            EXPANDS_GPS_RST,
#endif /*EXPANDS_GPS_RST*/
#ifdef EXPANDS_KB_EN
            EXPANDS_KB_EN,
#endif /*EXPANDS_KB_EN*/
#ifdef EXPANDS_GPIO_EN
            EXPANDS_GPIO_EN,
#endif /*EXPANDS_GPIO_EN*/
#ifdef EXPANDS_SD_PULLEN
            // EXPANDS_SD_PULLEN,
#endif /*EXPANDS_GPIO_EN*/
#ifdef EXPANDS_SD_EN
            EXPANDS_SD_EN,
#endif /*EXPANDS_SD_EN*/
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
        io.pinMode(EXPANDS_SD_PULLEN, INPUT);

#ifdef EXPANDS_DISP_RST
        io.digitalWrite(EXPANDS_DISP_RST, LOW);
        delay(50);
        io.digitalWrite(EXPANDS_DISP_RST, HIGH);
#endif /*EXPANDS_DISP_RST*/
    } else {
        LILYGO_LOG_D("Initializing expand Failed!");
    }
#endif /*USING_XL9555_EXPANDS*/

    //BHI260AP Address: 0x28
    if (init_options.initSensor) {
        initSensor();
    }

    backlight.begin(DISP_BL);

    const uint8_t share_spi_pins[] = {
        LORA_CS,
        LORA_RST,
        NFC_CS,
        SD_CS,
    };
    for (auto pin : share_spi_pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }

    LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, -1);

    if (_boot_images_addr) {
        uint16_t w = this->width();
        uint16_t h = this->height();
        this->pushColors(0, 0, w, h, (uint16_t *)_boot_images_addr);
        incrementalBrightness(250, 20);
    }

    if (init_options.initFatfs) {
        setupMSC(_lock_callback, _unlock_callback);
    }

    esp_enable_slow_crystal();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

    initShareSPIPins();

    pinMode(NFC_INT, INPUT);

    if (init_options.scanI2c) {
        LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire));
    }

    if (init_options.initRtc) {
        initRTC();
    }

    if (init_options.initNfc) {
        initNFC();
    }

    if (init_options.initKeyboard) {
        initKeyboard();
    }

    if (init_options.initHaptic) {
        initDrv();
    }

    if (init_options.initGps) {
        initGPS();
    }

    if (init_options.initRadio) {
        initLoRa();
    }

    if (init_options.initSd) {
        int retry = 2;
        do {
            LILYGO_LOG_D("Init SD");
            res = installSD();
            if (!res) {
                LILYGO_LOG_E("Warning: Failed to find SD");
            } else {
                LILYGO_LOG_D("SD init succeeded.");
                break;
            }
        } while (--retry);
    }

    if (init_options.initAudio && init_options.initCodec) {
        codec.setPins(I2S_MCLK, I2S_SCK, I2S_WS, I2S_SDOUT, I2S_SDIN);
        if (codec.begin(Wire, 0x18, CODEC_TYPE_ES8311)) {
            _devices_probe |= HW_CODEC_ONLINE;
            LILYGO_LOG_I("Codec init succeeded");
            codec.setGain(20);
            codec.setVolume(100);
        } else {
            LILYGO_LOG_E("Warning: Failed to find Codec");
        }

        audioOutput.setMuteCallback([](bool en) {
            instance.powerControl(POWER_SPEAK, en);
        });

    }

    if (init_options.initRotary) {
        LilyGoRotaryInputConfig rotaryConfig;
        rotaryConfig.pinA = ROTARY_A;
        rotaryConfig.pinB = ROTARY_B;
        rotaryConfig.pinButton = ROTARY_C;
        rotaryConfig.countsPerStep = clampRotaryStepDivider(LILYGO_LORA_PAGER_ROTARY_COUNTS_PER_STEP);
        rotaryConfig.directionSign = LILYGO_LORA_PAGER_ROTARY_DIRECTION_SIGN;
        // ROTARY_A/B/C have board-level external pull-ups on T-LoRa-Pager hardware.
        rotaryConfig.encoderUseInternalPullup = false;
        rotaryConfig.buttonUseInternalPullup = false;
        if (rotaryInput.begin(rotaryConfig)) {
            LILYGO_LOG_I("Rotary init succeeded, backend: %s", rotaryInput.backendName());
        } else {
            LILYGO_LOG_E("Warning: Failed to init rotary");
        }
    }

    pinMode(0, INPUT);
    bootButton.setClickHandler(clickHandler);
    bootButton.setLongClickHandler(longClickHandler);
    bootButton.setDoubleClickHandler(doubleClickHandler);

    return _devices_probe;
}

bool LilyGoLoRaPager::lockSPI(TickType_t xTicksToWait)
{
    return  LilyGoDispArduinoSPI::lock(xTicksToWait);
}

void LilyGoLoRaPager::unlockSPI()
{
    LilyGoDispArduinoSPI::unlock();
}

int LilyGoLoRaPager::getKeyChar(char *c)
{
    if (!_key_buf.empty()) {
        _key_buf.read(c, 1);
        return KB_PRESSED;
    }
    return -1;
}

bool LilyGoLoRaPager::initPMU()
{
    bool res = pmic.begin(Wire, BQ25896_SLAVE_ADDRESS, SDA, SCL);
    if (!res) {
        return false;
    }
    // Set the charging target voltage full voltage to 4288mV
    pmic.charger().setChargeVoltage(4288);

    // The charging current should not be greater than half of the battery capacity.
    pmic.charger().setFastChargeCurrent(DEVICE_CHARGE_CURRENT_RECOMMEND);


    return res;
}

/**
 * @brief   Hang on SD card
 * @retval Returns true if successful, otherwise false
 */
bool LilyGoLoRaPager::installSD(uint32_t spi_freq)
{
    _devices_probe &= (~HW_SD_ONLINE);
#ifdef EXPANDS_SD_DET
    io.pinMode(EXPANDS_SD_DET, INPUT);
    if (io.digitalRead(EXPANDS_SD_DET)) {
        return false;
    }
#endif /*EXPANDS_SD_DET*/

    if (spi_freq == 0) {
        spi_freq = LILYGO_LORA_PAGER_SD_SPI_FREQ;
    }
    SD.end();
    // Set mount point to /fs
    if (!SD.begin(SD_CS, SPI, spi_freq, "/sd")) {
        LILYGO_LOG_E("Failed to detect SD Card!!");
        return false;
    }
    if (SD.cardType() != CARD_NONE) {
        LILYGO_LOG_D("SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        _devices_probe |= HW_SD_ONLINE;
        return true;
    }
    return false;
}

void LilyGoLoRaPager::uninstallSD()
{
    lockSPI();
    SD.end();
    unlockSPI();
}

bool LilyGoLoRaPager::isCardReady()
{
    bool rlst = false;
    if (lockSPI(pdTICKS_TO_MS(100))) {
        rlst =  SD.sectorSize() != 0;
        unlockSPI();
    }
    return rlst;
}

void LilyGoLoRaPager::setBrightness(uint8_t level)
{
    backlight.setBrightness(level);
}

uint8_t LilyGoLoRaPager::getBrightness()
{
    return backlight.getBrightness();
}

bool LilyGoLoRaPager::needSwapColors()
{
    return true;
}

void LilyGoLoRaPager::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispArduinoSPI::pushColors( x1,  y1,  x2,  y2, color);
}

void LilyGoLoRaPager::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    switch (ch) {
    case POWER_DISPLAY_BACKLIGHT:
        break;
    case POWER_RADIO:
        io.digitalWrite(EXPANDS_LORA_EN, enable);
        break;
    case POWER_HAPTIC_DRIVER:
        io.digitalWrite(EXPANDS_DRV_EN, enable);
        break;
    case POWER_GPS:
        io.digitalWrite(EXPANDS_GPS_EN, enable);
        break;
    case POWER_NFC:
        io.digitalWrite(EXPANDS_NFC_EN, enable);
        break;
    case POWER_SD_CARD:
        io.digitalWrite(EXPANDS_SD_EN, enable);
        break;
    case POWER_SPEAK:
        io.digitalWrite(EXPANDS_AMP_EN, enable);
        break;
    case POWER_SENSOR:
        break;

    case POWER_KEYBOARD:
#ifdef EXPANDS_KB_EN
        io.digitalWrite(EXPANDS_KB_EN, enable);
#endif
        break;
    default:
        break;
    }
}

void LilyGoLoRaPager::sleepDisplay()
{
    LilyGoDispArduinoSPI::sleep();
}

void LilyGoLoRaPager::wakeupDisplay()
{
    LilyGoDispArduinoSPI::wakeup();
}

uint64_t LilyGoLoRaPager::checkWakeupPins(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = 0;
    if (wakeup_src & WAKEUP_SRC_BOOT_BUTTON) {
        wakeup_pin |=  _BV(0);
    }
    if (wakeup_pin == 0) {
        LILYGO_LOG_E("No wake-up method is set. T-LoRa-Pager supports WAKEUP_SRC_BOOT_BUTTON.");
    }
    return wakeup_pin;
}

void LilyGoLoRaPager::lightSleep(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = checkWakeupPins(wakeup_src);
    if (wakeup_pin == 0) {
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t wakeup_result;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    wakeup_result = esp_sleep_enable_ext1_wakeup_io(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    wakeup_result = esp_sleep_enable_ext1_wakeup(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    if (wakeup_result != ESP_OK) {
        LILYGO_LOG_E("Failed to enable T-LoRa Pager light-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        return;
    }

    rotaryInput.suspend();

    // Convert only once, and close automatically after conversion is complete.
    pmic.adc().startConversion();

    if (_devices_probe & HW_RADIO_ONLINE) {
        radio.sleep();
    }

    uint32_t gps_baudrate = Serial1.baudRate();
    if (gps_baudrate == 0) {
        gps_baudrate = LILYGO_LORA_PAGER_GPS_BAUDRATE;
    }
    Serial1.end();

    if (_devices_probe & HW_KEYBOARD_ONLINE) {
        kb.end();
    }

    powerControl(POWER_HAPTIC_DRIVER, false);
    powerControl(POWER_GPS, false);
    powerControl(POWER_SPEAK, false);
    powerControl(POWER_NFC, false);
    powerControl(POWER_KEYBOARD, false);

    uninstallSD();
    if (io.digitalRead(EXPANDS_SD_DET)) {
        powerControl(POWER_SD_CARD, false);
    }


#ifdef EXPANDS_GPS_RST
    LILYGO_LOG_D("Disable GPS RST Pin");
    io.digitalWrite(EXPANDS_GPS_RST, LOW);
#endif


    gpio_reset_pin((gpio_num_t )GPS_RX);
    gpio_reset_pin((gpio_num_t )GPS_TX);
    gpio_reset_pin((gpio_num_t )GPS_PPS);
    pinMode(GPS_RX, OPEN_DRAIN);
    pinMode(GPS_TX, OPEN_DRAIN);
    pinMode(GPS_PPS, OPEN_DRAIN);

    sleepDisplay();

    pinMode(NFC_CS, OPEN_DRAIN);

    pinMode(0, INPUT);

    Serial.flush();
    delay(1000);

    const esp_err_t sleep_result = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    if (sleep_result != ESP_OK) {
        LILYGO_LOG_E("Failed to enter T-LoRa Pager light sleep: %s",
                     esp_err_to_name(sleep_result));
    }

#ifdef EXPANDS_GPS_RST
    io.digitalWrite(EXPANDS_GPS_RST, HIGH);
#endif


    pinMode(NFC_CS, OUTPUT);
    digitalWrite(NFC_CS, HIGH);

    if (_devices_probe & HW_RADIO_ONLINE) {
        radio.standby();
    }

    wakeupDisplay();

    rotaryInput.resume();

    powerControl(POWER_HAPTIC_DRIVER, true);
    powerControl(POWER_GPS, true);
    powerControl(POWER_NFC, true);
    powerControl(POWER_KEYBOARD, true);
    powerControl(POWER_SD_CARD, true);
    installSD();


    initKeyboard();

    Serial1.begin(gps_baudrate, SERIAL_8N1, GPS_RX, GPS_TX);
    pinMode(GPS_PPS, INPUT);
}

void LilyGoLoRaPager::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
{
    uint64_t wakeup_pin = 0;
    const bool timer_wakeup = wakeup_src & WAKEUP_SRC_TIMER;
    const WakeupSource_t physical_sources = static_cast<WakeupSource_t>(
            static_cast<uint32_t>(wakeup_src) & ~static_cast<uint32_t>(WAKEUP_SRC_TIMER));

    if (timer_wakeup && sleep_second == 0) {
        LILYGO_LOG_E("Timer wakeup requires a non-zero sleep duration.");
        return;
    }
    if (physical_sources) {
        wakeup_pin = checkWakeupPins(physical_sources);
        if (wakeup_pin == 0) {
            return;
        }
        pinMode(0, INPUT_PULLUP);
        delayMicroseconds(50);
        if (digitalRead(0) == LOW) {
            LILYGO_LOG_W("T-LoRa Pager deep sleep skipped: BOOT button is still pressed");
            return;
        }
    } else if (!timer_wakeup) {
        LILYGO_LOG_E("No deep-sleep wake-up method is set.");
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t wakeup_result = ESP_OK;
    if (timer_wakeup) {
        wakeup_result = esp_sleep_enable_timer_wakeup(
                            static_cast<uint64_t>(sleep_second) * 1000000ULL);
    }
    if (wakeup_result == ESP_OK && wakeup_pin != 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        wakeup_result = esp_sleep_enable_ext1_wakeup_io(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#else
        wakeup_result = esp_sleep_enable_ext1_wakeup(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    }
    if (wakeup_result != ESP_OK) {
        LILYGO_LOG_E("Failed to configure T-LoRa Pager deep-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        return;
    }

    rotaryInput.end();

    // Convert only once, and close automatically after conversion is complete.
    pmic.adc().startConversion();


    if (_devices_probe & HW_KEYBOARD_ONLINE) {
        kb.end();
    }

    backlight.setBrightness(0);

    codec.end();

    const uint8_t expands[] = {
#ifdef EXPANDS_DISP_RST
        EXPANDS_DISP_RST,
#endif /*EXPANDS_DISP_RST*/
        EXPANDS_KB_RST,
        EXPANDS_LORA_EN,
        EXPANDS_GPS_EN,
        EXPANDS_DRV_EN,
        EXPANDS_AMP_EN,
        EXPANDS_NFC_EN,
#ifdef EXPANDS_GPS_RST
        EXPANDS_GPS_RST,
#endif /*EXPANDS_GPS_RST*/
#ifdef EXPANDS_KB_EN
        EXPANDS_KB_EN,
#endif /*EXPANDS_KB_EN*/
#ifdef EXPANDS_GPIO_EN
        EXPANDS_GPIO_EN,
#endif /*EXPANDS_GPIO_EN*/
#ifdef EXPANDS_SD_DET
        EXPANDS_SD_DET,
#endif /*EXPANDS_SD_DET*/
        // #ifdef EXPANDS_SD_PULLEN
        //         EXPANDS_SD_PULLEN,
        // #endif /*EXPANDS_GPIO_EN*/
        // #ifdef EXPANDS_SD_EN
        //         EXPANDS_SD_EN,
        // #endif /*EXPANDS_SD_EN*/
    };
    for (auto pin : expands) {
        io.digitalWrite(pin, LOW);
        delay(1);
    }

    drv.stop();

    // Reset the sensor to put it into sleep mode
    sensor.reset();

    LilyGoDispArduinoSPI::sleep();

    LilyGoDispArduinoSPI::end();

    int i = 3;

    while (i--) {
        LILYGO_LOG_D("%d second sleep ...", i);
        delay(1000);
    }
    if (io.digitalRead(EXPANDS_SD_DET)) {
        uninstallSD();
    } else {
        powerControl(POWER_SD_CARD, false);
    }

    Serial1.end();

    SPI.end();

    Wire.end();

    const uint8_t pins[] = {
        SD_CS,
        KB_INT,
        KB_BACKLIGHT,
        ROTARY_A,
        ROTARY_B,
        ROTARY_C,
        RTC_INT,
        NFC_INT,
        SENSOR_INT,
        NFC_CS,

        I2S_WS,
        I2S_SCK,
        I2S_MCLK,
        I2S_SDIN,
        I2S_SDOUT,

        GPS_TX,
        GPS_RX,
        GPS_PPS,
        SCK,
        MISO,
        MOSI,
        DISP_CS,
        DISP_DC,
        DISP_BL,
        SDA,
        SCL,
        LORA_CS,
        LORA_RST,
        LORA_BUSY,
        LORA_IRQ
    };

    for (auto pin : pins) {
        LILYGO_LOG_D("Set pin %d to open drain\n", pin);
        gpio_reset_pin((gpio_num_t )pin);
        pinMode(pin, OPEN_DRAIN);
    }
    Serial.flush();

    delay(200);

    Serial.end();

    delay(1000);

    esp_deep_sleep_start();
}

uint32_t LilyGoLoRaPager::getDeviceProbe()
{
    if (gps.probeDone() && !gps.probeInProgress() && gps.probeSuccess()) {
        _devices_probe |= HW_GPS_ONLINE;
    }
    return _devices_probe;
}

bool LilyGoLoRaPager::initNFC()
{
    bool res = false;
    LILYGO_LOG_D("Init NFC");
    res = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!res) {
        LILYGO_LOG_E("Failed to find NFC Reader");
    } else {
        LILYGO_LOG_D("Initializing NFC Reader succeeded");
        _devices_probe |= HW_NFC_ONLINE;
        // Turn off NFC power
        powerControl(POWER_NFC, false);
    }
    return res;
}

bool LilyGoLoRaPager::initKeyboard()
{
    kb.setPins(KB_BACKLIGHT);
    bool res = kb.begin(keyboardConfig, Wire, KB_INT);
    if (!res) {
        LILYGO_LOG_E("Failed to find Keyboard");
    } else {
        LILYGO_LOG_D("Initializing Keyboard succeeded");
        _devices_probe |= HW_KEYBOARD_ONLINE;
    }
    // kb.setBrightness(50);
    return res;
}

bool LilyGoLoRaPager::initDrv()
{
    //DRV2605 Address: 0x5A
    LILYGO_LOG_D("Init DRV2605 Haptic Driver");
    bool res = drv.begin(Wire, DRV2605_SLAVE_ADDRESS);
    if (!res) {
        LILYGO_LOG_E("Failed to find DRV2605");
    } else {
        LILYGO_LOG_D("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(HapticMode::INTERNAL_TRIGGER);
        drv.setWaveform(0, _effects);  // play effect
        drv.setWaveform(1, 0);   // end waveform
        drv.run();
        _devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}

void LilyGoLoRaPager::attachKeyboardFeedback(bool enable, uint8_t effects)
{
    _feedback_enable = enable;
    _feedback_effects = effects;
}

void LilyGoLoRaPager::setFeedbackCallback(custom_feedback_t fb)
{
    _custom_feedback = fb;
}

void LilyGoLoRaPager::feedback(void *args)
{
    if (!_feedback_enable) {
        return;
    }
    if (_custom_feedback) {
        _custom_feedback(args);
        return;
    }
    if (_devices_probe & HW_DRV_ONLINE) {
        drv.setWaveform(0, _feedback_effects);  // play effect
        drv.setWaveform(1, 0);   // end waveform
        drv.run();
    }
}

void LilyGoLoRaPager::setHapticEffects(uint8_t effects)
{
    if (effects > 127)effects = 127;
    _effects = effects;
}

uint8_t LilyGoLoRaPager::getHapticEffects()
{
    return _effects;
}

void LilyGoLoRaPager::vibrator()
{
    if (_devices_probe & HW_DRV_ONLINE) {
        drv.setWaveform(0, _effects);
        drv.setWaveform(1, 0);
        drv.run();
    }
}

static void lora_pager_gps_probe_cb(bool success, const char *model, void *user_data)
{
    (void)user_data;
    if (success) {
        LILYGO_LOG_D("%s GPS init succeeded\n", model ? model : "UBlox");
        _devices_probe |= HW_GPS_ONLINE;
    } else {
        LILYGO_LOG_E("Warning: Failed to find Ublox GPS Module\n");
        _devices_probe &= ~HW_GPS_ONLINE;
    }
}

bool LilyGoLoRaPager::initGPS()
{
    // GPS BAUD 38400 DEFAULT
    Serial1.begin(LILYGO_LORA_PAGER_GPS_BAUDRATE, SERIAL_8N1, GPS_RX, GPS_TX);
    LILYGO_LOG_D("Init GPS");
    bool res = gps.beginAsyncProbe(&Serial1, GPS_PROBE_UBLOX, lora_pager_gps_probe_cb);
    if (res) LILYGO_LOG_D("GPS async probe started");
    return res;
}



#define BOSCH_BHI260_KLIO
#include <BoschFirmware.h>
bool LilyGoLoRaPager::initSensor()
{
    bool res = false;
    Wire.setClock(1000000UL);
    LILYGO_LOG_D("Init BHI260AP Sensor");
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    sensor.setBootFromFlash(false);
    res = sensor.begin(Wire, BHI260AP_SLAVE_ADDRESS_L);
    if (!res) {
        LILYGO_LOG_E("Failed to find BHI260AP");
    } else {
        LILYGO_LOG_D("Initializing BHI260AP succeeded");
        _devices_probe |= HW_BHI260AP_ONLINE;
        sensor.setRemapAxes(SensorRemap::BOTTOM_LAYER_TOP_LEFT_CORNER);
        pinMode(SENSOR_INT, INPUT);
        attachInterrupt(SENSOR_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_SENSOR);
        }, RISING);

    }
    Wire.setClock(400000UL);
    return res;
}

bool LilyGoLoRaPager::initRTC()
{
    bool res = false;
    LILYGO_LOG_D("Init PCF85063 RTC");
    res = rtc.begin(Wire);
    if (!res) {
        LILYGO_LOG_E("Failed to find PCF85063");
    } else {
        _devices_probe |= HW_RTC_ONLINE;
        LILYGO_LOG_D("Initializing PCF85063 succeeded");
        rtc.hwClockRead();  //Synchronize RTC clock to system clock
        rtc.setClockOutput(SensorPCF85063::CLK_LOW);

        pinMode(RTC_INT, INPUT_PULLUP);
        attachInterrupt(RTC_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_RTC);
        }, FALLING);

    }
    return res;
}


bool LilyGoLoRaPager::initNRF24()
{
#ifndef RADIOLIB_EXCLUDE_NRF24
    // io.digitalWrite(EXPANDS_GPIO_EN, HIGH);
    int state = nrf24.begin();
    if (state == RADIOLIB_ERR_NONE) {
        LILYGO_LOG_D("Initializing NRF2401 Extern Module succeeded");
        _devices_probe |= HW_NRF24_ONLINE;
        return true;
    }
#endif
    LILYGO_LOG_E("Failed to find NRF2401 Extern Module");
    // io.digitalWrite(EXPANDS_GPIO_EN, LOW);
    return false;
}

bool LilyGoLoRaPager::initLoRa()
{
    if (_devices_probe & HW_RADIO_ONLINE) {
        _radio_hardware_present = true;
    } else {
        _radio_hardware_present = detectLoRaHardwarePresent();
    }

    if (!_radio_hardware_present) {
        _devices_probe &= ~HW_RADIO_ONLINE;
        LILYGO_LOG_W("LoRa module not detected, skip radio init");
        return false;
    }
    pinMode(LORA_IRQ, INPUT);
    pinMode(LORA_BUSY, INPUT);

    radio.reset();

    int state = radio.begin();

    if (state != RADIOLIB_ERR_NONE) {
        _devices_probe &= ~HW_RADIO_ONLINE;
        LILYGO_LOG_E("❌Radio init failed, code :%d , Use %s", state, USING_RADIO_NAME);
        return false;
    }

    _devices_probe |= HW_RADIO_ONLINE;

#if defined(ARDUINO_LILYGO_LORA_LR1121)
    // Set RF switch configuration
    static const uint32_t rfswitch_dio_pins[] = {
        RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6,
        RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
    };
    static const Module::RfSwitchMode_t rfswitch_table[] = {
        // mode                  DIO5  DIO6
        { LR11x0::MODE_STBY,   { LOW,  LOW  } },
        { LR11x0::MODE_RX,     { LOW, HIGH  } },
        { LR11x0::MODE_TX,     { HIGH,  LOW } },
        { LR11x0::MODE_TX_HP,  { HIGH,  LOW } },
        { LR11x0::MODE_TX_HF,  { LOW,  LOW  } },
        { LR11x0::MODE_GNSS,   { LOW,  LOW  } },
        { LR11x0::MODE_WIFI,   { LOW,  LOW  } },
        END_OF_MODE_TABLE,
    };

    radio.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);

    // Set TCXO voltage to 3.0V
    radio.setTCXO(3.0);

    LILYGO_LOG_I("✅Radio init succeeded, module: %s", USING_RADIO_NAME);

#endif /*ARDUINO_LILYGO_LORA_LR1121*/

    return true;
}

bool LilyGoLoRaPager::isLoRaHardwarePresent() const
{
    return _radio_hardware_present;
}


void LilyGoLoRaPager::loop()
{
    EventBits_t bits = xEventGroupGetBits(_event);

    if (_devices_probe & HW_GAUGE_ONLINE) {
        if (millis() - _gauge_update_timestamp > _gauge_update_interval) {
            _gauge_update_timestamp = millis();
            gauge.refresh();
        }
    }

    if (bits & HW_IRQ_RTC) {
        xEventGroupClearBits(_event, HW_IRQ_RTC);
        sendEvent(RTC_EVENT_INTERRUPT);
    }

    if (bits & HW_IRQ_SENSOR) {
        xEventGroupClearBits(_event, HW_IRQ_SENSOR);
        if (!sensor.update()) {
            static uint32_t last_bhi_update_error = 0;
            uint32_t now = millis();
            if (last_bhi_update_error == 0 || now - last_bhi_update_error > 2000) {
                last_bhi_update_error = now;
                LILYGO_LOG_W("BHI260 update failed: %s", sensor.getError());
            }
        }
    }

    // if (_devices_probe & HW_NFC_ONLINE) {
    //     lockSPI();
    //     NFCReader.rfalNfcWorker();
    //     unlockSPI();
    // }

    if (_devices_probe & HW_KEYBOARD_ONLINE) {
        static char c;
        if (kb.getKey(&c) > 0) {
            if (_enable_keyboard) {
                if (_key_buf.full()) {
                    _key_buf.read();
                }
                _key_buf.write(c);
            } else {
                LILYGO_LOG_PRINTF("_enable_keyboard not enabled\n");
            }
        }
    }

    // Keep the GNSS parser fed after the asynchronous probe completes. This
    // also prevents NMEA data from accumulating until the GNSS app opens.
    if (gps.probeDone() && !gps.probeInProgress() && gps.probeSuccess()) {
        while (Serial1.available()) {
            gps.encode((char)Serial1.read());
        }
    }

    bootButton.loop();
}

void LilyGoLoRaPager::enableKeyboard()
{
    LILYGO_LOG_D("Enable keyboard");
    _enable_keyboard = true;
    _key_buf.remove(_key_buf.available());
}

void LilyGoLoRaPager::disableKeyboard()
{
    LILYGO_LOG_D("Disable keyboard");
    _enable_keyboard = false;
    _key_buf.remove(_key_buf.available());
}

bool LilyGoLoRaPager::hasOTG()
{
    return _devices_probe & HW_PMU_ONLINE;
}

bool LilyGoLoRaPager::hasGauge()
{
    return _devices_probe & HW_GAUGE_ONLINE;
}

bool LilyGoLoRaPager::readPowerSnapshot(LilyGoPowerSnapshot &snapshot)
{
    LilyGoPowerManageInf::readPowerSnapshot(snapshot);
    if (!snapshot.externalGaugePresent) {
        return snapshot.pmuPresent || snapshot.fuelGaugePresent;
    }

    copyPowerLabel(snapshot.gaugeName, sizeof(snapshot.gaugeName), "BQ27220");
    gauge.refresh();

    setPowerMetric(snapshot.remainingCapacityMah, gauge.getRemainingCapacity(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.fullChargeCapacityMah, gauge.getFullChargeCapacity(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.standbyCurrentMa, gauge.getStandbyCurrent(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.designCapacityMah, gauge.getDesignCapacity(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.averagePowerMw, gauge.getAveragePower(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.maxLoadCurrentMa, gauge.getMaxLoadCurrent(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.batteryPercent, gauge.getStateOfCharge(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.batteryMv, gauge.getVoltage(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.batteryMa, gauge.getCurrent(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    setPowerMetric(snapshot.temperatureC, gauge.getTemperature(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);

    BatteryStatus batteryStatus = gauge.getBatteryStatus();
    if (batteryStatus.isInDischargeMode()) {
        setPowerMetric(snapshot.timeToEmptyMin, gauge.getTimeToEmpty(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    } else if (batteryStatus.isFullChargeDetected()) {
        setPowerMetric(snapshot.timeToFullMin, 0, LILYGO_POWER_SRC_EXTERNAL_GAUGE);
        setPowerMetric(snapshot.timeToEmptyMin, 0, LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    } else {
        setPowerMetric(snapshot.timeToFullMin, gauge.getTimeToFull(), LILYGO_POWER_SRC_EXTERNAL_GAUGE);
    }

    snapshot.fuelGaugePresent = snapshot.batteryPercent.valid;
    snapshot.batteryPresentValid = true;
    snapshot.batteryPresent = snapshot.batteryMv.valid ? snapshot.batteryMv.value > 2500.0f : true;
    return true;
}

bool LilyGoLoRaPager::shutdown()
{
    if (pmic.charger().getStatus().vbusPresent) {
        return false;
    }
    pmic.power().enableShipMode(true);
    // The return value is meaningless; it only returns false when the device cannot be shut down.
    return true;
}

bool LilyGoLoRaPager::isOTGEnabled()
{
    return pmic.power().isBoostEnabled();
}

bool LilyGoLoRaPager::enableOTG()
{
    return pmic.power().enableBoost(true);
}

bool LilyGoLoRaPager::disableOTG()
{
    return pmic.power().enableBoost(false);
}

float LilyGoLoRaPager::getBattVoltage()
{
    return gauge.getVoltage();
}

float LilyGoLoRaPager::getBatteryPercent()
{
    return gauge.getStateOfCharge();
}

float LilyGoLoRaPager::getTemperature()
{
    return gauge.getTemperature();
}

RotaryMsg_t LilyGoLoRaPager::getRotary()
{
    return rotaryInput.read();
}

void LilyGoLoRaPager::clearRotaryMsg()
{
    rotaryInput.clear();
}

void LilyGoLoRaPager::setRotaryStepDivider(uint8_t divider)
{
    rotaryInput.setCountsPerStep(clampRotaryStepDivider(divider));
}

uint8_t LilyGoLoRaPager::getRotaryStepDivider()
{
    return rotaryInput.getCountsPerStep();
}

uint8_t LilyGoLoRaPager::getRotaryStepDividerMin()
{
    return LilyGoRotaryInput::getCountsPerStepMin();
}

uint8_t LilyGoLoRaPager::getRotaryStepDividerMax()
{
    return clampRotaryStepDivider(LILYGO_LORA_PAGER_ROTARY_MAX_STEP_DIVIDER);
}

void LilyGoLoRaPager::disableRotary()
{
    rotaryInput.suspend();
}

void LilyGoLoRaPager::enableRotary()
{
    rotaryInput.resume();
}


namespace
{
LilyGoLoRaPager &getInstanceRef()
{
    return *LilyGoLoRaPager::getInstance();
}
}
LilyGoLoRaPager &instance = getInstanceRef();

#endif //ARDUINO_T_LORA_PAGER
