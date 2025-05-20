/**
 * @file      LilyGoWatchUltra.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-06
 *
 */

#ifdef ARDUINO_T_WATCH_S3_ULTRA
#include "LilyGoWatchUltra.h"
#include "SensorWireHelper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "LilyGoLib.h"
#include "driver/rtc_io.h"

extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);
extern void esp_enable_slow_crystal();
extern void setGroupBitsFromISR(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToSet);

#define PIN_NONE    -1


#define CO5300_206_INIT_SEQUENCE_LENGTH             11u

static const disp_cmd_t co5300_206_cmd[CO5300_206_INIT_SEQUENCE_LENGTH] = {
    {0xFE, {0x00}, 0x01},
    {0xC4, {0x80}, 0x01},
    {0x3A, {0x55}, 0x01},
    {0x35, {0x00}, 0x01},
    {0x53, {0x20}, 0x01},
    {0x63, {0xFF}, 0x01},
    {0x2A, {0x00, 0x16, 0x01, 0xAF}, 0x04},
    {0x2B, {0x00, 0x00, 0x01, 0xF5}, 0x04},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0x51, {0x00}, 0x01},
};


#ifdef USING_BHI_EXPANDS
#define EXPANDS_DISP_RST        SensorBHI260AP::M2SDX
#define EXPANDS_DISP_EN         SensorBHI260AP::M2SCX
#define EXPANDS_DRV_EN          SensorBHI260AP::MCSB4
#define EXPANDS_TOUCH_RST       SensorBHI260AP::M2SDI
#endif

#if    defined(ARDUINO_LILYGO_LORA_SX1262)
SX1262 radio = newModule();
#elif  defined(ARDUINO_LILYGO_LORA_SX1280)
SX1280 radio = newModule();
#elif  defined(ARDUINO_LILYGO_LORA_CC1101)
CC1101 radio = newModule();
#elif  defined(ARDUINO_LILYGO_LORA_LR1121)
LR1121 radio = newModule();
#elif  defined(ARDUINO_LILYGO_LORA_SI4432)
Si4432 radio = newModule();
#endif

#ifdef USING_ST25R3916
RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
RfalNfcClass NFCReader(&nfc_hw);
#endif

EventGroupHandle_t LilyGoUltra::_event = NULL;

static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

LilyGoUltra::LilyGoUltra() : LilyGo_Display(QSPI_DRIVER, false),
    LilyGoDispQSPI(co5300_206_cmd, CO5300_206_INIT_SEQUENCE_LENGTH, DISP_WIDTH, DISP_HEIGHT),
    LilyGoPowerManage(&pmu),
    _effects(80), devices_probe(0), _boot_images_addr(NULL), _lock(NULL),
    _enableDMA(false),
    _enableTearingEffect(false)
{
    // LilyGoDispQSPI::setGapOffset(22, 0);
    LilyGoDispQSPI::setRotation(0);
    _brightness = 0;    //Default disp is brightness is zero
}

LilyGoUltra::~LilyGoUltra()
{

}

bool LilyGoUltra::useDMA()
{
    return _enableDMA;
}

void LilyGoUltra::setDisplayParams(bool enableDMA, bool enableTearingEffect)
{
    _enableDMA = enableDMA;
    _enableTearingEffect = enableTearingEffect;
}

void LilyGoUltra::clearEventBits(const EventBits_t uxBitsToClear)
{
    xEventGroupClearBits(_event, uxBitsToClear);
}

void LilyGoUltra::setEventBits(const EventBits_t uxBitsToSet)
{
    xEventGroupClearBits(_event, uxBitsToSet);
}

void LilyGoUltra::setRotation(uint8_t rotation)
{
    LilyGoDispQSPI::setRotation(rotation);
}

uint8_t LilyGoUltra::getRotation()
{
    return LilyGoDispQSPI::getRotation();
}

uint16_t  LilyGoUltra::width()
{
    return LilyGoDispQSPI::width;
}

uint16_t  LilyGoUltra::height()
{
    return LilyGoDispQSPI::height;
}

void LilyGoUltra::setBootImage(uint8_t *image)
{
    _boot_images_addr = image;
}

void LilyGoUltra::initShareSPIPins()
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

uint32_t LilyGoUltra::begin(uint32_t disable_hw_init)
{
    if (_event) {
        return devices_probe;
    }

    bool res = false;

    _lock = xSemaphoreCreateMutex();

    _event = xEventGroupCreate();

    while (!psramFound()) {
        log_e("PSRAM NOT FOUND!");
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;

    Wire.begin(SDA, SCL);

    if (!(disable_hw_init & NO_SCAN_I2C_DEV)) {
        SensorWireHelper::dumpDevices(Wire);
    }

    initShareSPIPins();

    if (!(disable_hw_init & NO_INIT_FATFS)) {
        setupMSC(_lock_callback, _unlock_callback);
    }

    res = initPMU();
    if (!res) {
        log_e("Failed to find PMU.");
        assert(0);
    } else {
        log_d("Initializing PMU succeeded");
    }


    LilyGoDispQSPI::enableDMA(_enableDMA);

    LilyGoDispQSPI::enableTearingEffect(_enableTearingEffect);

#if defined(USING_BHI_EXPANDS)

    if (!initSensor()) {
        assert(0);
    }

    sensor.digitalWrite(EXPANDS_DISP_EN, HIGH);
    delay(20);
    sensor.digitalWrite(EXPANDS_DISP_RST, HIGH);
    delay(200);
    sensor.digitalWrite(EXPANDS_DISP_RST, LOW);
    delay(300);
    sensor.digitalWrite(EXPANDS_DISP_RST, HIGH);
    delay(200);

    // Enable touch
    sensor.digitalWrite(EXPANDS_TOUCH_RST, HIGH);

    // Enable Drv2605
    sensor.digitalWrite(EXPANDS_DRV_EN, HIGH);

#elif defined(USING_XL9555_EXPANDS)

    if (io.begin(Wire, 0x20)) {
        log_d("Initializing expand succeeded");
        devices_probe |= HW_EXPAND_ONLINE;
        const uint8_t expands[] = {
            EXPANDS_DRV_EN,
            EXPANDS_DISP_EN,
            EXPANDS_TOUCH_RST,
#ifdef EXPANDS_DISP_RST
            EXPANDS_DISP_RST
#endif
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
#ifdef EXPANDS_DISP_RST
        io.digitalWrite(EXPANDS_DISP_RST, LOW);
        delay(50);
        io.digitalWrite(EXPANDS_DISP_RST, HIGH);
#endif
    } else {
        log_d("Initializing expand Failed!");
    }

#endif

#ifndef DISP_RST
#define DISP_RST -1
#endif

    LilyGoDispQSPI::init(DISP_RST, DISP_CS,
                         DISP_TE, DISP_SCK,
                         DISP_D0, DISP_D1,
                         DISP_D2, DISP_D3, 80);


    if (_boot_images_addr) {
        uint16_t w = this->width();
        uint16_t h = this->height();
        this->pushColors(0, 0, w, h, (uint16_t *)_boot_images_addr);
        incrementalBrightness(250, 20);
    }

    esp_enable_slow_crystal();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

#ifndef USING_BHI_EXPANDS
    if (!(disable_hw_init & NO_HW_SENSOR)) {
        initSensor();
    }
#endif

    if (!(disable_hw_init & NO_HW_TOUCH)) {
        initTouch();
    }

    if (!(disable_hw_init & NO_HW_RTC)) {
        initRTC();
    }

    if (!(disable_hw_init & NO_HW_NFC)) {
        initNFC();
    }

    if (!(disable_hw_init & NO_HW_DRV)) {
        initDrv();
    }

    if (!(disable_hw_init & NO_HW_GPS)) {
        initGPS();
    }

    if (!(disable_hw_init & NO_HW_LORA)) {
        int state = radio.begin();
        if (state == RADIOLIB_ERR_NONE) {
            devices_probe |= HW_RADIO_ONLINE;
        } else {
            log_e("Radio init failed, code :%d", state);
        }
    }

    if (!(disable_hw_init & NO_HW_SD)) {
        installSD();
    }

#ifdef USING_PDM_MICROPHONE
    if (!(disable_hw_init & NO_HW_MIC)) {
        initMicrophone();
    }
#endif


#ifdef USING_PCM_AMPLIFIER
    if (!(disable_hw_init & NO_HW_MIC)) {
        initAmplifier();
    }
#endif // USING_PCM_AMPLIFIER

    return devices_probe;
}

bool LilyGoUltra::getTouched()
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_TOUCHPAD) {
        return true;
    }
    return false;
}

uint8_t LilyGoUltra::getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point )
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_TOUCHPAD) {
        uint8_t tp = touch.getPoint(x_array, y_array, get_point);
        if (tp == 0) {
            clearEventBits(HW_IRQ_TOUCHPAD);
        }
        return tp;
    }
    return 0;
}

void LilyGoUltra::setHapticEffects(uint8_t effects)
{
    if (effects > 127)effects = 127;
    _effects = effects;
}

uint8_t LilyGoUltra::getHapticEffects()
{
    return _effects;
}

void LilyGoUltra::vibrator()
{
    if (devices_probe & HW_DRV_ONLINE) {
        drv.setWaveform(0, _effects);
        drv.setWaveform(1, 0);
        drv.run();
    }
}



/*
* *********************************************************************
* | CHIP       | AXP2101                                | Peripherals |
* | ---------- | -------------------------------------- | ----------- |
* | DC1        | 3.3V                            /2A    | ESP32-S3    |
* | DC2        | 0.5-1.2V,1.22-1.54V             /2A    | Unused      |
* | DC3        | 0.5-1.2V,1.22-1.54V,1.6-3.4V    /2A    | Unused      |
* | DC4        | 0.5-1.2V,1.22-1.84V             /1.5A  | Unused      |
* | DC5        | 1.2V,1.4-3.7V                   /1A    | Unused      |
* | LDO1(VRTC) | 3.3V                            /30mA  | RTC & GPS   |
* | ALDO1      | 3.3V                            /300mA | Display     |
* | ALDO2      | 3.3V                            /300mA | SDCard      |
* | ALDO3      | 3.3V                            /300mA | Radio       |
* | ALDO4      | 1.8V                            /300mA | Sensor      |
* | BLDO1      | 3.3V                            /300mA | GPS         |
* | BLDO2      | 3.3V                            /300mA | Speaker     |
* | DLDO1      | 3.3V                            /300mA | NFC         |
* | CPUSLDO    | 1.4V                            /30mA  | Unused      |
* | VBACKUP    | 3.3V                            /30mA  | Unused      |
* *********************************************************************
*/
bool LilyGoUltra::initPMU()
{
    bool res = pmu.init();
    if (!res) {
        return false;
    }

    devices_probe |= HW_PMU_ONLINE;

    // Clear PMU interrupt status
    pmu.clearIrqStatus();

    // Turn off the PMU charging indicator light, no physical connection
    pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF); // NO LED

    pmu.setALDO1Voltage(3300);  // SD Card
    pmu.enableALDO1();

    pmu.setALDO2Voltage(3300);  // Display
    pmu.enableALDO2();

    pmu.setALDO3Voltage(3300);  // Radio
    pmu.enableALDO3();

    pmu.setALDO4Voltage(1800);  // Sensor
    pmu.enableALDO4();

    pmu.setBLDO1Voltage(3300);  // GPS
    pmu.enableBLDO1();

    pmu.setBLDO2Voltage(3300);  // Speaker
    pmu.enableBLDO2();

    pmu.setButtonBatteryChargeVoltage(3300);    // RTC Button battery
    pmu.enableButtonBatteryCharge();

    pmu.enableDLDO1();  // NFC

    // UNUSED POWER CHANNEL
    pmu.disableDC2();
    pmu.disableDC3();
    pmu.disableDC4();
    pmu.disableDC5();
    pmu.disableCPUSLDO();

    // Enable Measure
    pmu.enableBattDetection();
    pmu.enableVbusVoltageMeasure();
    pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.enableTemperatureMeasure();

    // Clear all PMU interrupts
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

    // Enable PMU interrupt
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                  XPOWERS_AXP2101_PKEY_LONG_IRQ |
                  XPOWERS_AXP2101_VBUS_INSERT_IRQ |
                  XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_START_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_DONE_IRQ);

    // Register PMU interrupt management
    pinMode(PMU_INT, INPUT_PULLUP);
    attachInterrupt(PMU_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_POWER);
    }, FALLING);

    // Enable the battery NTC temperature detection function
    pmu.enableTSPinMeasure();

    setChargeCurrent(300);

    return res;
}


void LilyGoUltra::checkPowerStatus()
{
    bool batteryInsert = pmu.isBatteryConnect();
    // Get PMU Interrupt Status Register
    pmu.getIrqStatus();

    if (pmu.isDropWarningLevel2Irq()) {
        log_d("isDropWarningLevel2");
        sendEvent(PMU_EVENT_LOW_VOLTAGE_LEVEL2);
    }
    if (pmu.isDropWarningLevel1Irq()) {
        log_d("isDropWarningLevel1");
        sendEvent(PMU_EVENT_LOW_VOLTAGE_LEVEL1);
    }
    if (pmu.isGaugeWdtTimeoutIrq()) {
        log_d("isWdtTimeout");
        // sendEvent(PMU_EVENT_LOW_VOLTAGE_LEVEL1);
    }
    if (pmu.isBatChargerOverTemperatureIrq()) {
        log_d("isBatChargeOverTemperature");
        sendEvent(PMU_EVENT_CHARGE_HIGH_TEMP);
    }
    if (pmu.isBatWorkOverTemperatureIrq()) {
        log_d("isBatWorkOverTemperature");
        // sendEvent(PMU_EVENT_LOW_VOLTAGE_LEVEL1);
    }
    if (pmu.isBatWorkUnderTemperatureIrq()) {
        log_d("isBatWorkUnderTemperature");
    }
    if (pmu.isVbusInsertIrq()) {
        log_d("isVbusInsert");
        sendEvent(PMU_EVENT_USBC_INSERT);
    }
    if (pmu.isVbusRemoveIrq()) {
        log_d("isVbusRemove");
        sendEvent(PMU_EVENT_USBC_REMOVE);
    }
    if (pmu.isBatInsertIrq()) {
        log_d("isBatInsert");
        sendEvent(PMU_EVENT_BATTERY_INSERT);
    }
    if (pmu.isBatRemoveIrq()) {
        log_d("isBatRemove");
        sendEvent(PMU_EVENT_BATTERY_REMOVE);
    }
    if (pmu.isPekeyShortPressIrq()) {
        log_d("isPekeyShortPress");
        sendEvent(PMU_EVENT_KEY_CLICKED);
    }
    if (pmu.isPekeyLongPressIrq()) {
        log_d("isPekeyLongPress");
        sendEvent(PMU_EVENT_KEY_LONG_PRESSED);
    }
    if (pmu.isPekeyNegativeIrq()) {
        log_d("isPekeyNegative");
    }
    if (pmu.isPekeyPositiveIrq()) {
        log_d("isPekeyPositive");
    }
    if (pmu.isWdtExpireIrq()) {
        log_d("isWdtExpire");
    }
    if (pmu.isLdoOverCurrentIrq()) {
        log_d("isLdoOverCurrentIrq");
    }
    if (pmu.isBatfetOverCurrentIrq()) {
        log_d("isBatfetOverCurrentIrq");
    }

    if (batteryInsert) {
        if (pmu.isBatChargeDoneIrq()) {
            log_d("isBatChargeDone");
            sendEvent(PMU_EVENT_CHARGE_FINISH);
        }
        if (pmu.isBatChargeStartIrq()) {
            log_d("isBatChargeStart");
            sendEvent(PMU_EVENT_CHARGE_STARTED);
        }
    }

    if (pmu.isBatDieOverTemperatureIrq()) {
        log_d("isBatDieOverTemperature");
    }
    if (pmu.isChargeOverTimeoutIrq()) {
        log_d("isChargeOverTimeout");
        sendEvent(PMU_EVENT_CHARGE_TIMEOUT);
    }
    if (pmu.isBatOverVoltageIrq()) {
        log_d("isBatOverVoltage");
        sendEvent(PMU_EVENT_BATTERY_OVER_VOLTAGE);
    }
    // Clear PMU Interrupt Status Register
    pmu.clearIrqStatus();
}

/**
 * @brief   Hang on SD card
 * @retval Returns true if successful, otherwise false
 */
bool LilyGoUltra::installSD()
{
    log_d("Init SD");

#ifdef EXPANDS_SD_DET
    io.pinMode(EXPANDS_SD_DET, INPUT);
    if (io.digitalRead(EXPANDS_SD_DET) == HIGH) {
        return false;
    }
#endif /*EXPANDS_SD_DET*/

    // Set mount point to /fs
    if (!SD.begin(SD_CS, SPI, 4000000U, "/fs")) {
        log_e("Failed to detect SD Card!!");
        return false;
    }
    if (SD.cardType() != CARD_NONE) {
        log_i("SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        devices_probe |= HW_SD_ONLINE;
        return true;
    }
    return false;
}

void LilyGoUltra::uninstallSD()
{
    SD.end();
}

bool LilyGoUltra::isCardReady()
{
    bool rlst = false;
    if (lockSPI(pdTICKS_TO_MS(100))) {
        rlst =  SD.sectorSize() != 0;
        unlockSPI();
    }
    return rlst;
}

void LilyGoUltra::setBrightness(uint8_t level)
{
    LilyGoDispQSPI::setBrightness(level);
}

uint8_t LilyGoUltra::getBrightness()
{
    return LilyGoDispQSPI::_brightness;
}

void LilyGoUltra::decrementBrightness(uint8_t target_level, uint32_t delay_ms, bool reserve)
{
    uint8_t brightness = getBrightness();
    if (target_level > brightness)
        return;
    for (int i = brightness; i >= target_level; i--) {
        setBrightness(i);
        delay(delay_ms);
    }
}

void LilyGoUltra::incrementalBrightness(uint8_t target_level, uint32_t delay_ms, bool reserve)
{
    uint8_t brightness = getBrightness();
    if (target_level < brightness)
        return;
    for (int i = brightness; i < target_level; i++) {
        setBrightness(i);
        delay(delay_ms);
    }
}


void LilyGoUltra::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispQSPI::pushColors( x1,  y1,  x2,  y2, color);
}

void LilyGoUltra::powerControl(enum PowerCtrlChannel ch, bool enable)
{
    switch (ch) {
    case POWER_DISPLAY:
        enable ? pmu.enableALDO2() : pmu.disableALDO2();
        break;
    case POWER_RADIO:
        enable ? pmu.enableALDO3() : pmu.disableALDO3();
        break;
    case POWER_HAPTIC_DRIVER:
        io.digitalWrite(EXPANDS_DRV_EN, enable);
        break;
    case POWER_GPS:
        if (enable) {
            pmu.enableBLDO1();
            Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
            pinMode(GPS_PPS, INPUT);
        } else {
            pmu.disableBLDO1();
            gpio_reset_pin((gpio_num_t )GPS_RX);
            gpio_reset_pin((gpio_num_t )GPS_TX);
            gpio_reset_pin((gpio_num_t )GPS_PPS);
            pinMode(GPS_RX, OPEN_DRAIN);
            pinMode(GPS_RX, OPEN_DRAIN);
            pinMode(GPS_PPS, OPEN_DRAIN);
        }
        break;
    case POWER_NFC:
        enable ? pmu.enableDLDO1() : pmu.disableDLDO1();
        break;
    case POWER_SD_CARD:
        enable ? pmu.enableALDO1() : pmu.disableALDO1();
        break;
    case POWER_SPEAK:
        enable ? pmu.enableBLDO2() : pmu.disableBLDO2();
        break;
    case POWER_SENSOR:
        enable ? pmu.enableALDO4() : pmu.disableALDO4();
        break;
    default:
        break;
    }
}

uint64_t LilyGoUltra::checkWakeupPins(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = 0;
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        wakeup_pin |=  _BV(TP_INT);
    }
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        wakeup_pin |=  _BV(PMU_INT);
    }
    if (wakeup_src & WAKEUP_SRC_BOOT_BUTTON) {
        wakeup_pin |=  _BV(0);
    }
    if (wakeup_pin == 0) {
        log_e("No wake-up method is set. T-Watch Ultra allows setting WAKEUP_SRC_POWER_KEY and WAKEUP_SRC_TOUCH_PANEL, WAKEUP_SRC_BOOT_BUTTON as wake-up methods.");
    }
    return wakeup_pin;
}

void LilyGoUltra::lightSleep(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = checkWakeupPins(wakeup_src);
    if (wakeup_pin == 0) {
        return;
    }

    radio.sleep();

    powerControl(POWER_HAPTIC_DRIVER, false);
    powerControl(POWER_GPS, false);
    powerControl(POWER_SPEAK, false);
    powerControl(POWER_NFC, false);
    uninstallSD();
    powerControl(POWER_SD_CARD, false);

    sleepDisplay();

    disablePowerMeasure();

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    }

    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        log_d("Enable power touch panel from wakeup source.");
    } else {
        touch.sleep();
        detachInterrupt(TP_INT);
    }

    pinMode(NFC_CS, OPEN_DRAIN);

    Wire.end();
    gpio_reset_pin((gpio_num_t )SDA);
    gpio_reset_pin((gpio_num_t )SCL);
    gpio_reset_pin((gpio_num_t )TP_INT);
    gpio_reset_pin((gpio_num_t )PMU_INT);

    pinMode(SDA, OPEN_DRAIN);
    pinMode(SCL, OPEN_DRAIN);
    pinMode(TP_INT, OPEN_DRAIN);
    pinMode(PMU_INT, OPEN_DRAIN);

#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
    esp_sleep_enable_ext1_wakeup_io((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_sleep_enable_ext1_wakeup((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#endif

    esp_light_sleep_start();

    Wire.begin(SDA, SCL);

    radio.standby();

    wakeupDisplay();

    powerControl(POWER_HAPTIC_DRIVER, true);
    powerControl(POWER_GPS, true);
    powerControl(POWER_NFC, true);
    powerControl(POWER_SD_CARD, true);
    installSD();

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                  XPOWERS_AXP2101_PKEY_LONG_IRQ |
                  XPOWERS_AXP2101_VBUS_INSERT_IRQ |
                  XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_START_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_DONE_IRQ);

    pmu.clearIrqStatus();

    enablePowerMeasure();

    wakeupTouch();

    pinMode(NFC_CS, OUTPUT);
    digitalWrite(NFC_CS, HIGH);

    pinMode(PMU_INT, INPUT_PULLUP);
    attachInterrupt(PMU_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_POWER);
    }, FALLING);
}


void LilyGoUltra::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
{
    uint64_t wakeup_pin = 0;
    bool keep_touch_power = false;
    if ((wakeup_src & WAKEUP_SRC_TIMER)) {
        if (sleep_second == 0) {
            log_e("Too little sleep time.");
            return;
        }
    } else {
        wakeup_pin = checkWakeupPins(wakeup_src);
        if (wakeup_pin == 0) {
            return;
        }
    }

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        log_d("Enable power button from wakeup source.");
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);

    }
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        log_d("Enable power touch panel from wakeup source.");
        keep_touch_power = true;
    } else {
        touch.sleep();
    }

    if (wakeup_src & WAKEUP_SRC_BOOT_BUTTON) {
        log_d("Enable power boot button from wakeup source.");
    }

    LilyGoDispQSPI::sleep();

    LilyGoDispQSPI::end();

    uninstallSD();

#if  defined(USING_BHI_EXPANDS)

    sensor.digitalWrite(EXPANDS_DISP_RST, LOW);
    sensor.digitalWrite(EXPANDS_DRV_EN, LOW);
    sensor.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
    sensor.digitalWrite(EXPANDS_DISP_EN, LOW);

#elif defined(USING_XL9555_EXPANDS)
    const uint8_t expands[] = {
#ifdef EXPANDS_DISP_RST
        EXPANDS_DISP_RST,
#endif
        EXPANDS_DRV_EN,
        // EXPANDS_TOUCH_RST,
        EXPANDS_DISP_EN,
    };
    for (auto pin : expands) {
        io.digitalWrite(pin, LOW);
        delay(1);
    }
#endif

    // Turn off ADC data monitoring to save power
    disablePowerMeasure();

    // Enable PMU sleep
    pmu.enableSleep();

    // Do not turn off the screen power in deep sleep,
    // otherwise the current will increase abnormally by about 600uA.
    // The correct way is to keep the power supply and set the screen and touch to sleep.
    // At this time, the screen and touch consume a total of about 103.4uA (screen 100uA, touch 3.4uA)
    //! pmu.disableALDO2(); // Display

    powerControl(POWER_HAPTIC_DRIVER, false);
    powerControl(POWER_GPS, false);
    powerControl(POWER_SPEAK, false);
    powerControl(POWER_NFC, false);
    powerControl(POWER_SENSOR, false);
    powerControl(POWER_SD_CARD, false);
    powerControl(POWER_RADIO, false);

    // Turn off the RTC backup battery, adding about 200uA
    if (off_rtc_backup_domain) {
        pmu.disableButtonBatteryCharge();   // RTC Battery
    }

    pmu.clearIrqStatus();

    int i = 4;
    while (i--) {
        log_d("%d second sleep ...", i);
        delay(500);
    }

    Serial1.end();

    SPI.end();

    Wire.end();

    const uint8_t pins[] = {
        DISP_D0,
        DISP_D1,
        DISP_D2,
        DISP_D3,
        DISP_SCK,
        DISP_CS,
        DISP_TE,
        37,

        // TP_INT,
        RTC_INT,
        // PMU_INT,
        NFC_INT,
        SENSOR_INT,

        NFC_CS,

        MIC_SCK,
        MIC_DAT,

        I2S_BCLK,
        I2S_WCLK,
        I2S_DOUT,

        SD_CS,

        SDA,
        SCL,

        MOSI,
        MISO,
        SCK,

        GPS_TX,
        GPS_RX,
        GPS_PPS,

        LORA_CS,
        LORA_RST,
        LORA_BUSY,
        LORA_IRQ,
    };

    for (auto pin : pins) {
        log_d("Set pin %d to open drain\n", pin);
        gpio_reset_pin((gpio_num_t )pin);
        pinMode(pin, OPEN_DRAIN);
    }

    if (!(wakeup_src & WAKEUP_SRC_POWER_KEY)) {
        gpio_reset_pin((gpio_num_t )PMU_INT);
        pinMode(PMU_INT, OPEN_DRAIN);
    }

    if (!(wakeup_src & WAKEUP_SRC_TOUCH_PANEL)) {
        gpio_reset_pin((gpio_num_t )TP_INT);
        pinMode(TP_INT, OPEN_DRAIN);
    }

    if (wakeup_src & WAKEUP_SRC_TIMER) {
        esp_sleep_enable_timer_wakeup(sleep_second * 1000000UL);
    } else {
#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        esp_sleep_enable_ext1_wakeup_io((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#else
        esp_sleep_enable_ext1_wakeup((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    }

    esp_deep_sleep_start();
}

void LilyGoUltra::sleepDisplay()
{
    LilyGoDispQSPI::sleep();
    io.digitalWrite(EXPANDS_DISP_EN, LOW);
}

void LilyGoUltra::wakeupDisplay()
{
    io.digitalWrite(EXPANDS_DISP_EN, HIGH);
    LilyGoDispQSPI::wakeup();
}

uint32_t LilyGoUltra::getDeviceProbe()
{
    return devices_probe;
}

const char *LilyGoUltra::getName()
{
    return "LilyGo T-Watch Ultra (2025)";
}

bool LilyGoUltra::hasTouch()
{
    return  devices_probe & HW_TOUCH_ONLINE;
}

bool LilyGoUltra::initNFC()
{
    bool res = false;
#ifdef USING_ST25R3916
    log_d("Init NFC");
    pinMode(NFC_INT, INPUT);
    res = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!res) {
        log_e("Failed to find NFC Reader!");
    } else {
        log_d("Initializing NFC Reader succeeded");
        devices_probe |= HW_NFC_ONLINE;
    }
#endif
    return res;
}


bool LilyGoUltra::initDrv()
{
    bool res = false;
    log_d("Init DRV2605 Haptic Driver");
    res = drv.begin(Wire);
    if (!res) {
        log_e("Failed to find DRV2605!");
    } else {
        log_d("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(SensorDRV2605::MODE_INTTRIG);
        drv.useERM();
        // set the effect to play
        drv.setWaveform(0, 15);  // play effect
        drv.setWaveform(1, 0);   // end waveform
        drv.run();
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}


bool LilyGoUltra::initGPS()
{
    bool res = false;
    // GPS BAUD 38400 DEFAULT
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    log_d("Init GPS");
    res = gps.init(&Serial1);
    if (!res) {
        log_e("Warning: Failed to find UBlox GPS Module\n");
    } else {
        log_d("UBlox GPS init succeeded, using UBlox GPS Module\n");
        devices_probe |= HW_GPS_ONLINE;
    }
    return res;
}

#ifndef TP_RST
#define TP_RST -1
#endif

bool LilyGoUltra::initTouch()
{
    io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
    delay(20);
    io.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
    delay(60);

    bool res = false;
    uint8_t touch_panel_addr = 0x5A;
    Wire.beginTransmission(0x1A);
    if (Wire.endTransmission() == 0) {
        touch_panel_addr = 0x1A;
        log_d("TouchPanel using 0x1A address");
    }
    touch.setPins(TP_RST, TP_INT);
    res = touch.begin(Wire, touch_panel_addr, TP_SDA, TP_SCL);
    if (!res) {
        log_e("Failed to find TouchPanel!");
    } else {
        log_d("Initializing TouchPanel succeeded");
        log_d("TouchPanel model: %s", touch.getModelName());

        devices_probe |= HW_TOUCH_ONLINE;

        pinMode(TP_INT, INPUT_PULLUP);
        attachInterrupt(TP_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_TOUCHPAD);
        }, FALLING);

    }
    return res;
}

#ifdef USING_XL9555_EXPANDS
#define BOSCH_BHI260_KLIO
#else
#define BOSCH_BHI260_GPIO
#endif
#include <BoschFirmware.h>

bool LilyGoUltra::initSensor()
{
    bool res = false;
    Wire.setClock(1000000UL);
    log_d("Init BHI260AP Sensor");
    sensor.setPins(PIN_NONE);
    // Set the firmware array address and firmware size
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    // Set to load firmware from flash
    sensor.setBootFromFlash(false);
    res = sensor.begin(Wire);
    if (!res) {
        log_e("Failed to find BHI260AP!");
    } else {
        log_d("Initializing BHI260AP succeeded");
        devices_probe |= HW_SENSOR_ONLINE;

        pinMode(SENSOR_INT, INPUT);
        attachInterrupt(SENSOR_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_SENSOR);
        }, RISING);

    }
    Wire.setClock(400000UL);
    return res;
}

bool LilyGoUltra::initRTC()
{
    bool res = false;
    log_v("Init PCF85063 RTC");
    res = rtc.begin(Wire);
    if (!res) {
        log_e("Failed to find PCF85063!");
    } else {
        devices_probe |= HW_RTC_ONLINE;
        log_v("Initializing PCF85063 succeeded");
        rtc.hwClockRead();  //Synchronize RTC clock to system clock
        rtc.setClockOutput(SensorPCF85063::CLK_LOW);

        pinMode(RTC_INT, INPUT_PULLUP);
        attachInterrupt(RTC_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_RTC);
        }, FALLING);
    }
    return res;
}

bool LilyGoUltra::initMicrophone()
{
    log_v("Init Microphone");
    bool res = false;
#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    res = mic.init(MIC_SCK, MIC_DAT);
#else
    // Set up the pins used for audio input
    mic.setPinsPdmRx(MIC_SCK, MIC_DAT);
    // Initialize the I2S bus in standard mode
    res = mic.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
#endif
    if (res) {
        log_v("Microphone init succeeded");
    } else {
        log_e("Warning: Failed to init Microphone");
    }
    return res;
}


bool LilyGoUltra::initAmplifier()
{
    log_v("Init PCM Amplifier");
    bool res = false;
#if  ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(5,0,0)
    player.setPins(I2S_BCLK, I2S_WCLK, I2S_DOUT);
    // start I2S at the sample rate with 16-bits per sample
    res = player.begin(I2S_MODE_STD, 160000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#else
    res = player.init(I2S_BCLK, I2S_WCLK, I2S_DOUT);
#endif
    if (res) {
        log_v("PCM Amplifier init succeeded");
    } else {
        log_e("Warning: Failed to init PCM Amplifier");
    }
    return res;
}



void LilyGoUltra::loop()
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_POWER) {
        clearEventBits(HW_IRQ_POWER);
        checkPowerStatus();
    }

    if (bits & HW_IRQ_RTC) {
        clearEventBits(HW_IRQ_RTC);
        sendEvent(RTC_EVENT_INTERRUPT);
    }

    if (bits & HW_IRQ_SENSOR) {
        clearEventBits(HW_IRQ_SENSOR);
        sensor.update();
        sendEvent(SENSOR_EVENT_INTERRUPT);
    }

    lockSPI();
    NFCReader.rfalNfcWorker();
    unlockSPI();

}

void LilyGoUltra::wakeupTouch()
{
    io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
    delay(20);
    io.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
    delay(60);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(TP_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_TOUCHPAD);
    }, FALLING);
}

bool LilyGoUltra::lockSPI(TickType_t xTicksToWait)
{
    return xSemaphoreTake(_lock, xTicksToWait) == pdTRUE;
}

void LilyGoUltra::unlockSPI()
{
    xSemaphoreGive(_lock);
}

LilyGoUltra instance;

#endif //ARDUINO_T_WATCH_S3_ULTRA
