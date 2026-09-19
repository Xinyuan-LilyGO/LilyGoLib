/**
 * @file      LilyGoWatchUltra.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-06
 *
 */

#ifdef ARDUINO_T_WATCH_S3_ULTRA
#include "LilyGoLog.h"
#include "LilyGoWatchUltra.h"
#include "SensorWireHelper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "LilyGoLib.h"
#include "core/LilyGoGeneral.h"
#include "driver/rtc_io.h"
#include <Preferences.h>

#ifndef LILYGO_WATCH_ULTRA_SD_SPI_FREQ
#define LILYGO_WATCH_ULTRA_SD_SPI_FREQ 4000000U //4MHZ
#endif

extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);

static uint8_t BATTER_PARAMS[] = {
    0x01, 0xf5, 0x40, 0x00, 0x1b, 0x1e, 0x28, 0x0f, 0x0c, 0x1e, 0x32, 0x02, 0x14, 0x05, 0x0a, 0x04,
    0x74, 0xfc, 0xf4, 0x0d, 0x43, 0x10, 0x52, 0xfb, 0xa6, 0x01, 0xea, 0x04, 0x64, 0x06, 0x52, 0x06,
    0x18, 0x0a, 0xe7, 0x0f, 0x9f, 0x0f, 0x51, 0x09, 0xf7, 0x0e, 0x89, 0x0e, 0x71, 0x04, 0x58, 0x04,
    0x43, 0x09, 0x32, 0x0e, 0x1c, 0x0e, 0x14, 0x09, 0x04, 0x0d, 0xe9, 0x0d, 0xde, 0x03, 0xc8, 0x03,
    0xb3, 0x08, 0x9d, 0x0d, 0x79, 0x0d, 0x3a, 0x07, 0xf5, 0x9e, 0x56, 0x47, 0x36, 0x20, 0x24, 0x17,
    0xc5, 0x98, 0x7e, 0x66, 0x4e, 0x44, 0x38, 0x1a, 0x12, 0x0a, 0xf6, 0x00, 0x00, 0xf6, 0x00, 0xf6,
    0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xf6, 0x00, 0x00, 0xf6, 0x00, 0xf6,
    0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xfb, 0x00, 0x00, 0xf6, 0x00, 0x00, 0xf6, 0x00, 0xf6,
};

static constexpr uint8_t co5300_206_seq_length = 11;
static const disp_cmd_t co5300_206_cmd[co5300_206_seq_length] = {
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

LILYGO_DEFINE_RADIO();

RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
RfalNfcClass NFCReader(&nfc_hw);

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


static void clickHandler(Button2 &btn)
{
    LILYGO_LOG_PRINTF("Click event\n");
    instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_CLICK));
}

static void longClickHandler(Button2 &btn)
{
    LILYGO_LOG_PRINTF("Long click event\n");
    instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_LONG_PRESSED));
}

static void doubleClickHandler(Button2 &btn)
{
    LILYGO_LOG_PRINTF("Double click event\n");
    instance.sendEvent(DeviceEvent::button(0, BUTTON_EVENT_DOUBLE_CLICK));
}


LilyGoUltra::LilyGoUltra() : LilyGo_Display(QSPI_DRIVER, true),
    LilyGoDispQSPI(co5300_206_cmd, co5300_206_seq_length, DISP_WIDTH, DISP_HEIGHT),
    LilyGoPowerManageInf(pmic, PMIC_TYPE_AXP2101),
    _audioOutput(I2S_BCLK, I2S_WCLK, I2S_DOUT),
    _audioInput(MIC_SCK, MIC_DAT),
    _effects(1), devices_probe(0), _boot_images_addr(NULL), _lock(NULL),
    _enableDMA(false),
    _enableTearingEffect(false)
{
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
    xEventGroupSetBits(_event, uxBitsToSet);
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

uint32_t LilyGoUltra::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoUltra::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

uint32_t LilyGoUltra::begin(const LilyGoDeviceInitOptions &init_options)
{
    if (_event) {
        return devices_probe;
    }

    bool res = false;

    Preferences prefs;
    prefs.begin("lilygo", false);
    bool batteryCalibrated = prefs.getBool("calibration");
    prefs.end();

    if (batteryCalibrated) {
        LILYGO_LOG_D("Battery already calibrated");
    } else {
        LILYGO_LOG_D("Battery not calibrated");
    }

    _lock = xSemaphoreCreateMutex();

    _event = xEventGroupCreate();

    while (!psramFound()) {
        LILYGO_LOG_E("PSRAM NOT FOUND!");
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;

    Wire.begin(SDA, SCL);

    if (init_options.scanI2c) {
        LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire));
    }

    initShareSPIPins();

    if (init_options.initFatfs) {
        setupMSC(_lock_callback, _unlock_callback);
    }

    if (init_options.initPmu) {
        res = initPMU(batteryCalibrated == false);
        if (!res) {
            LILYGO_LOG_E("Failed to find PMU.");
            assert(0);
        } else {
            LILYGO_LOG_D("Initializing PMU succeeded");
        }
    }

    LilyGoDispQSPI::enableDMA(_enableDMA);

    LilyGoDispQSPI::enableTearingEffect(_enableTearingEffect);

    if (io.begin(Wire, XL9555_SLAVE_ADDRESS0)) {
        LILYGO_LOG_D("Initializing expand succeeded");
        devices_probe |= HW_EXPAND_ONLINE;
        const uint8_t expands[] = {
            EXPANDS_DRV_EN,
            EXPANDS_DISP_EN,
            EXPANDS_TOUCH_RST,
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
    } else {
        LILYGO_LOG_D("Initializing expand Failed!");
    }


    if (init_options.initHaptic) {
        initDrv();
    }

    const int disp_rst = -1;
    LilyGoDispQSPI::init(disp_rst, DISP_CS,
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

    if (init_options.initSensor) {
        initSensor();
    }

    if (init_options.initTouch) {
        initTouch();
    }

    if (init_options.initRtc) {
        initRTC();
    }

    if (init_options.initNfc) {
        initNFC();
    }

    if (init_options.initGps) {
        initGPS();
    }

    if (init_options.initRadio) {
        initLoRa();
    }

    if (init_options.initSd) {
        installSD();
    }

    if (init_options.initAudio) {
        initMicrophone();
    }

    if (init_options.initAudio) {
        initAmplifier();
    }

    pinMode(0, INPUT);
    bootButton.setClickHandler(clickHandler);
    bootButton.setLongClickHandler(longClickHandler);
    bootButton.setDoubleClickHandler(doubleClickHandler);

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
        if (get_point == 0) {
            xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);
            return 0;
        }
        TouchPoints data = touch.getTouchPoints();
        if (x_array == nullptr || y_array == nullptr) {
            return 0;
        }
        if (data.hasPoints()) {
            uint8_t pointsToCopy = (get_point < data.getPointCount()) ? get_point : data.getPointCount();
            for (int i = 0; i < pointsToCopy; i++) {
                const TouchPoint &pt = data.getPoint(i);
                // log_d("Point %d: x=%d, y=%d\n", i, pt.x, pt.y);
                x_array[i] = pt.x;
                y_array[i] = pt.y;
            }
            return pointsToCopy;
        }
        xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);
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
* | LDO1(VRTC) | 3.3V                            /30mA  | GPS BackUp  |
* | ALDO1      | 3.3V                            /300mA | SDCard      |
* | ALDO2      | 3.3V                            /300mA | Display     |
* | ALDO3      | 3.3V                            /300mA | Radio       |
* | ALDO4      | 1.8V                            /300mA | Sensor      |
* | BLDO1      | 3.3V                            /300mA | GPS         |
* | BLDO2      | 3.3V                            /300mA | Speaker     |
* | DLDO1      | 3.3V                            /300mA | NFC         |
* | CPUSLDO    | 1.4V                            /30mA  | Unused      |
* | VBACKUP    | 3.3V                            /30mA  | RTC Button battery      |
* *********************************************************************
*/
bool LilyGoUltra::initPMU(bool batteryCalibration)
{
    bool res =  pmic.begin(Wire, AXP2101_SLAVE_ADDRESS);
    if (!res) {
        return false;
    }

    if (batteryCalibration) {
        if (pmic.power().writeGaugeData(BATTER_PARAMS, sizeof(BATTER_PARAMS))) {
            LILYGO_LOG_D("Battery calibration data write success");
            Preferences prefs;
            prefs.begin("lilygo", false);
            prefs.putBool("calibration", true);
            prefs.end();
        } else {
            LILYGO_LOG_E("Battery calibration data write failed");
        }
    }

    devices_probe |= HW_PMU_ONLINE;

    // SD Card
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO1, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO1, true);

    // Display
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO2, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, true);

    // Radio
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO3, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, true);

    // Sensor
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO4, 1800);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, true);

    // GPS
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_BLDO1, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, true);

    // Speaker
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_BLDO2, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, true);

    // RTC backup battery
    pmic.charger().setButtonBatteryChargeVoltage(3300);
    pmic.enableModule(AXP2101Core::Module::BTN_CHARGE, true);

    // NFC
    pmic.getChannel()->enable(AXP2101Channel::CH_DLDO1, true);

    // UNUSED POWER CHANNEL
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC2, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC3, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC4, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC5, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_CPUSLDO, false);

    // Set the time of pressing the button to turn off
    pmic.pwron().setOnDurationMs(4000);

    // Set the button power-on press time
    pmic.pwron().setOffDurationMs(128);

    // Enable Measure
    pmic.adc().enableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT |
        PmicAdcBase::Channel::DIE_TEMPERATURE |
        PmicAdcBase::Channel::BAT_TEMPERATURE
    );


    pmic.led().setMode(PmicLedBase::Mode::MANUAL);
    pmic.led().setManualState(PmicLedBase::ManualState::HiZ);

    // Enable PMU interrupt
    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);
    pmic.getIrq()->enable(
        AXP2101Irq::IRQ_VBUS_INSERT |
        AXP2101Irq::IRQ_VBUS_REMOVE |
        AXP2101Irq::IRQ_BAT_CHG_START |
        AXP2101Irq::IRQ_BAT_CHG_DONE |
        AXP2101Irq::IRQ_PEKEY_SHORT_PRESS |
        AXP2101Irq::IRQ_PEKEY_LONG_PRESS);
    // Clear all PMU interrupts
    pmic.getIrq()->clearStatus();

    // Register PMU interrupt management
    pinMode(PMU_INT, INPUT_PULLUP);
    attachInterrupt(PMU_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_POWER);
    }, FALLING);

    // Enable the battery NTC temperature detection function
    pmic.enableModule(AXP2101Core::Module::TS_MEASURE, true);

    // T-Watch-S3 is designed for high-voltage(4.2V) batteries by default.
    pmic.getCharger()->setChargeVoltage(4288);

    pmic.getCharger()->setPreChargeCurrent(128);

    // The charging current should not be greater than half of the battery capacity.
    pmic.getCharger()->setFastChargeCurrent(DEVICE_CHARGE_CURRENT_RECOMMEND);

    return true;
}


void LilyGoUltra::checkPowerStatus()
{
    bool batteryInsert = pmic.isBatteryConnect();
    // Get PMU Interrupt Status Register
    uint64_t irqStatus = pmic.irq().readStatus();

    if (pmic.irq().isGaugeWdtTimeout(irqStatus)) {
        LILYGO_LOG_D("isWdtTimeout");
    }
    if (pmic.irq().isDieOverTemp(irqStatus)) {
        LILYGO_LOG_D("isBatChargeOverTemperature");
        sendEvent(DeviceEvent::power(PMU_EVENT_CHARGE_HIGH_TEMP));
    }
    if (pmic.irq().isVbusInsert(irqStatus)) {
        LILYGO_LOG_D("isVbusInsert");
        sendEvent(DeviceEvent::power(PMU_EVENT_USBC_INSERT));
    }
    if (pmic.irq().isVbusRemove(irqStatus)) {
        LILYGO_LOG_D("isVbusRemove");
        sendEvent(DeviceEvent::power(PMU_EVENT_USBC_REMOVE));
    }
    if (pmic.irq().isBatInsert(irqStatus)) {
        LILYGO_LOG_D("isBatInsert");
        sendEvent(DeviceEvent::power(PMU_EVENT_BATTERY_INSERT));
    }
    if (pmic.irq().isBatRemove(irqStatus)) {
        LILYGO_LOG_D("isBatRemove");
        sendEvent(DeviceEvent::power(PMU_EVENT_BATTERY_REMOVE));
    }
    if (pmic.irq().isPekeyShortPress(irqStatus)) {
        LILYGO_LOG_D("isPekeyShortPress");
        sendEvent(DeviceEvent::power(PMU_EVENT_KEY_CLICKED));
    }
    if (pmic.irq().isPekeyLongPress(irqStatus)) {
        LILYGO_LOG_D("isPekeyLongPress");
        sendEvent(DeviceEvent::power(PMU_EVENT_KEY_LONG_PRESSED));
    }
    if (pmic.irq().isWdtExpire(irqStatus)) {
        LILYGO_LOG_D("isWdtExpire");
    }
    if (pmic.irq().isLdoOverCurr(irqStatus)) {
        LILYGO_LOG_D("isLdoOverCurrentIrq");
    }
    if (pmic.irq().isBatfetOverCurr(irqStatus)) {
        LILYGO_LOG_D("isBatfetOverCurrentIrq");
    }
    if (batteryInsert) {
        if (pmic.irq().isBatChgDone(irqStatus)) {
            LILYGO_LOG_D("isBatChargeDone");
            sendEvent(DeviceEvent::power(PMU_EVENT_CHARGE_FINISH));
        }
        if (pmic.irq().isBatChgStart(irqStatus)) {
            LILYGO_LOG_D("isBatChargeStart");
            sendEvent(DeviceEvent::power(PMU_EVENT_CHARGE_STARTED));
        }
    }
    if (pmic.irq().isDieOverTemp(irqStatus)) {
        LILYGO_LOG_D("isBatDieOverTemperature");
    }
}

/**
 * @brief   Hang on SD card
 * @retval Returns true if successful, otherwise false
 */
bool LilyGoUltra::installSD(uint32_t spi_freq)
{
    devices_probe &= (~HW_SD_ONLINE);
#ifdef EXPANDS_SD_DET
    io.pinMode(EXPANDS_SD_DET, INPUT);
    if (io.digitalRead(EXPANDS_SD_DET) == HIGH) {
        return false;
    }
#endif /*EXPANDS_SD_DET*/

    if (spi_freq == 0) {
        spi_freq = LILYGO_WATCH_ULTRA_SD_SPI_FREQ;
    }
    SD.end();
    // Set mount point to /fs
    if (!SD.begin(SD_CS, SPI, spi_freq, "/sd")) {
        LILYGO_LOG_E("Failed to detect SD Card!!");
        return false;
    }
    if (SD.cardType() != CARD_NONE) {
        LILYGO_LOG_I("SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        devices_probe |= HW_SD_ONLINE;
        return true;
    }
    return false;
}

void LilyGoUltra::uninstallSD()
{
    devices_probe &= (~HW_SD_ONLINE);
    SD.end();
}

bool LilyGoUltra::isCardReady()
{
    bool rlst = false;
#ifdef EXPANDS_SD_DET
    if (io.digitalRead(EXPANDS_SD_DET) == HIGH) {
        LILYGO_LOG_D("SD is not insert");
        return false;
    }
#endif
    LILYGO_LOG_D("SD is insert detected");
    if (lockSPI(pdTICKS_TO_MS(100))) {
        rlst =  SD.sectorSize() != 0;
        LILYGO_LOG_D("SD Card %s", rlst ? "Ready" : "Not Ready");
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

bool LilyGoUltra::needSwapColors()
{
    return true;
}

void LilyGoUltra::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispQSPI::pushColors( x1,  y1,  x2,  y2, color);
}

void LilyGoUltra::powerControl(enum PowerCtrlChannel ch, bool enable)
{
    switch (ch) {
    case POWER_DISPLAY:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, false);
        }
        break;
    case POWER_RADIO:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, false);
        }
        break;
    case POWER_HAPTIC_DRIVER:
        io.digitalWrite(EXPANDS_DRV_EN, enable);
        break;
    case POWER_GPS:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, true);
            Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
            pinMode(GPS_PPS, INPUT);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, false);
            gpio_reset_pin((gpio_num_t )GPS_RX);
            gpio_reset_pin((gpio_num_t )GPS_TX);
            gpio_reset_pin((gpio_num_t )GPS_PPS);
            pinMode(GPS_RX, OPEN_DRAIN);
            pinMode(GPS_RX, OPEN_DRAIN);
            pinMode(GPS_PPS, OPEN_DRAIN);
        }
        break;
    case POWER_NFC:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_DLDO1, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_DLDO1, false);
        }
        break;
    case POWER_SD_CARD:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO1, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO1, false);
        }
        break;
    case POWER_SPEAK:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, false);
        }
        break;
    case POWER_SENSOR:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, false);
        }
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
        LILYGO_LOG_E("No wake-up method is set. T-Watch Ultra allows setting WAKEUP_SRC_POWER_KEY and WAKEUP_SRC_TOUCH_PANEL, WAKEUP_SRC_BOOT_BUTTON as wake-up methods.");
    }
    return wakeup_pin;
}

void LilyGoUltra::lightSleep(WakeupSource_t wakeup_src)
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
        LILYGO_LOG_E("Failed to enable T-Watch Ultra light-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        return;
    }

#ifndef ARDUINO_LILYGO_LORA_SX1280
    // SX1280 died here, the reason is not analyzed yet, waiting to be processed
    radio.sleep();
#endif

    powerControl(POWER_HAPTIC_DRIVER, false);
    powerControl(POWER_GPS, false);
    powerControl(POWER_SPEAK, false);
    powerControl(POWER_NFC, false);
    uninstallSD();
    powerControl(POWER_SD_CARD, false);

    sleepDisplay();

    pmic.adc().disableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT |
        PmicAdcBase::Channel::DIE_TEMPERATURE |
        PmicAdcBase::Channel::BAT_TEMPERATURE);

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        pmic.getIrq()->enable(AXP2101Irq::IRQ_PEKEY_SHORT_PRESS);
    }

    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        LILYGO_LOG_D("Enable power touch panel from wakeup source.");
    } else {
        touch.sleep();
        detachInterrupt(TP_INT);
    }

    pinMode(NFC_CS, OPEN_DRAIN);

    pmic.getIrq()->clearStatus();
    Wire.end();
    gpio_reset_pin((gpio_num_t )SDA);
    gpio_reset_pin((gpio_num_t )SCL);
    gpio_reset_pin((gpio_num_t )TP_INT);
    gpio_reset_pin((gpio_num_t )PMU_INT);

    pinMode(SDA, OPEN_DRAIN);
    pinMode(SCL, OPEN_DRAIN);
    pinMode(TP_INT, OPEN_DRAIN);
    pinMode(PMU_INT, OPEN_DRAIN);

    const esp_err_t sleep_result = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    if (sleep_result != ESP_OK) {
        LILYGO_LOG_E("Failed to enter T-Watch Ultra light sleep: %s",
                     esp_err_to_name(sleep_result));
    }

    Wire.begin(SDA, SCL);

    radio.standby();

    wakeupDisplay();

    powerControl(POWER_HAPTIC_DRIVER, true);
    powerControl(POWER_GPS, true);
    powerControl(POWER_NFC, true);
    powerControl(POWER_SD_CARD, true);
    installSD();

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);
    pmic.getIrq()->enable(
        AXP2101Irq::IRQ_VBUS_INSERT |
        AXP2101Irq::IRQ_VBUS_REMOVE |
        AXP2101Irq::IRQ_BAT_CHG_START |
        AXP2101Irq::IRQ_BAT_CHG_DONE |
        AXP2101Irq::IRQ_PEKEY_SHORT_PRESS |
        AXP2101Irq::IRQ_PEKEY_LONG_PRESS);
    pmic.getIrq()->clearStatus();

    pmic.adc().enableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT |
        PmicAdcBase::Channel::DIE_TEMPERATURE |
        PmicAdcBase::Channel::BAT_TEMPERATURE);

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
        LILYGO_LOG_E("Failed to configure T-Watch Ultra deep-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        return;
    }

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);

    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        LILYGO_LOG_D("Enable power button from wakeup source.");
        // pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
        pmic.getIrq()->enable(AXP2101Irq::IRQ_PEKEY_SHORT_PRESS);

    }
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        LILYGO_LOG_D("Enable power touch panel from wakeup source.");
        keep_touch_power = true;
    } else {
        touch.sleep();
    }

    if (wakeup_src & WAKEUP_SRC_BOOT_BUTTON) {
        LILYGO_LOG_D("Enable power boot button from wakeup source.");
    }

    LilyGoDispQSPI::sleep();

    LilyGoDispQSPI::end();

    uninstallSD();

    const uint8_t expands[] = {
        EXPANDS_DRV_EN,
        // EXPANDS_TOUCH_RST,
        EXPANDS_DISP_EN,
    };
    for (auto pin : expands) {
        io.digitalWrite(pin, LOW);
        delay(1);
    }

    // Turn off ADC data monitoring to save power
    pmic.adc().disableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT |
        PmicAdcBase::Channel::DIE_TEMPERATURE |
        PmicAdcBase::Channel::BAT_TEMPERATURE);

    // Enable PMU sleep
    pmic.power().enableSleep();


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
        // RTC Battery
        pmic.enableModule(AXP2101Core::Module::BTN_CHARGE, false);
    }

    int i = 4;
    while (i--) {
        LILYGO_LOG_D("%d second sleep ...", i);
        delay(500);
    }

    Serial1.end();

    SPI.end();

    // This must be the final PMIC access before sleep. A pending PMIC status
    // holds PMU_INT low and would wake the ESP immediately.
    pmic.getIrq()->clearStatus();
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
        LILYGO_LOG_D("Set pin %d to open drain\n", pin);
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
    if (gps.probeDone() && !gps.probeInProgress() && gps.probeSuccess()) {
        devices_probe |= HW_GPS_ONLINE;
    }
    return devices_probe;
}

const char *LilyGoUltra::getName()
{
    return "LilyGo T-Watch Ultra (2025)";
}

const LilyGoDeviceCapability &LilyGoUltra::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        /* boardName */       "LilyGo T-Watch Ultra (2025)",
#ifdef USING_RADIO_NAME
        /* radioName */       USING_RADIO_NAME,
#else
        /* radioName */       "None",
#endif
        /* pmicName */        "AXP2101",
        /* gaugeName */       "PMU internal",
        /* hasSd */           true,
        /* hasGps */          true,
        /* gpsRuntimeProbe */ false,
        /* hasTouch */        true,
        /* hasKeyboard */     false,
        /* hasTrackball */    false,
        /* hasRotary */       false,
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
        /* hasExternalI2c */  false,
        /* hasExternalSpi */  false,
        /* hasExternalUart */ false,
        /* hasExternalGpio */ false,
        /* pmicType */        PMIC_TYPE_AXP2101,
        /* hasButton */       true,
        /* hasPmuButton */    true,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoUltra::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initRtc = true;
    options.initCodec = false;
    return options;
}

bool LilyGoUltra::hasTouch()
{
    return  devices_probe & HW_TOUCH_ONLINE;
}

bool LilyGoUltra::initNFC()
{
    LILYGO_LOG_D("Init NFC");
    pinMode(NFC_INT, INPUT);
    bool res = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!res) {
        LILYGO_LOG_E("Failed to find NFC Reader!");
    } else {
        LILYGO_LOG_D("Initializing NFC Reader succeeded");
        devices_probe |= HW_NFC_ONLINE;
    }
    return res;
}

bool LilyGoUltra::initDrv()
{
    LILYGO_LOG_D("Init DRV2605 Haptic Driver");
    bool res = drv.begin(Wire, DRV2605_SLAVE_ADDRESS);
    if (!res) {
        LILYGO_LOG_E("Failed to find DRV2605!");
    } else {
        LILYGO_LOG_D("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(HapticMode::INTERNAL_TRIGGER);
        drv.setWaveform(0, _effects);  // play effect
        drv.setWaveform(1, 0);   // end waveform
        drv.run();
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}

void LilyGoUltra::gpsProbeCallback(bool success, const char *model, void *user_data)
{
    LilyGoUltra *watch = static_cast<LilyGoUltra *>(user_data);
    if (!watch) {
        return;
    }

    if (success) {
        LILYGO_LOG_D("%s GPS init succeeded\n", model ? model : "UBlox");
        watch->devices_probe |= HW_GPS_ONLINE;
    } else {
        LILYGO_LOG_E("Warning: Failed to find Ublox GPS Module\n");
        watch->devices_probe &= ~HW_GPS_ONLINE;
    }
}

bool LilyGoUltra::initGPS()
{
    // GPS BAUD 38400 DEFAULT
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    LILYGO_LOG_D("Init GPS");
    bool res = gps.beginAsyncProbe(&Serial1, GPS_PROBE_UBLOX, LilyGoUltra::gpsProbeCallback, this);
    if (res) LILYGO_LOG_D("GPS async probe started");
    return res;
}

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
        LILYGO_LOG_D("TouchPanel using 0x1A address");
    }

    const int tp_rst = -1;
    const int tp_int = TP_INT;
    touch.setPins(tp_rst, tp_int);
    res = touch.begin(Wire, touch_panel_addr, TP_SDA, TP_SCL);
    if (!res) {
        LILYGO_LOG_E("Failed to find TouchPanel!");
    } else {
        LILYGO_LOG_D("Initializing TouchPanel succeeded");
        LILYGO_LOG_D("TouchPanel model: %s", touch.getModelName());

        devices_probe |= HW_TOUCH_ONLINE;

        pinMode(TP_INT, INPUT_PULLUP);
        attachInterrupt(TP_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_TOUCHPAD);
        }, FALLING);

    }
    return res;
}

#define BOSCH_BHI260_KLIO
#include <BoschFirmware.h>

bool LilyGoUltra::initSensor()
{
    bool res = false;
    Wire.setClock(1000000UL);
    LILYGO_LOG_D("Init BHI260AP Sensor");
    // Set the firmware array address and firmware size
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    // Set to load firmware from flash
    sensor.setBootFromFlash(false);
    res = sensor.begin(Wire, BHI260AP_SLAVE_ADDRESS_L);
    if (!res) {
        LILYGO_LOG_E("Failed to find BHI260AP!");
    } else {
        LILYGO_LOG_D("Initializing BHI260AP succeeded");
        devices_probe |= HW_BHI260AP_ONLINE;

        // sensor.setRemapAxes(SensorBHI260AP::TOP_LAYER_RIGHT_CORNER); // Initial test version
        sensor.setRemapAxes(SensorRemap::TOP_LAYER_BOTTOM_RIGHT_CORNER);

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
    LILYGO_LOG_V("Init PCF85063 RTC");
    res = rtc.begin(Wire);
    if (!res) {
        LILYGO_LOG_E("Failed to find PCF85063!");
    } else {
        devices_probe |= HW_RTC_ONLINE;
        LILYGO_LOG_V("Initializing PCF85063 succeeded");
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
    LILYGO_LOG_V("Init Microphone");
    bool res = false;
    res = _audioInput.begin();
    if (res) {
        LILYGO_LOG_I("Microphone init succeeded");
    } else {
        LILYGO_LOG_E("Warning: Failed to init Microphone");
    }
    return res;
}


bool LilyGoUltra::initAmplifier()
{
    LILYGO_LOG_V("Init Audio Amplifier");
    bool res = false;
    res = _audioOutput.begin();
    _audioOutput.setMuteCallback([](bool muted) {
        LILYGO_LOG_D("Audio Amplifier %s", muted ? "muted" : "unmuted");
        instance.powerControl(POWER_SPEAK, muted);
    });
    if (res) {
        LILYGO_LOG_I("Audio Amplifier init succeeded");
    } else {
        LILYGO_LOG_E("Warning: Failed to init Audio Amplifier");
    }
    return res;
}

bool LilyGoUltra::initLoRa()
{
    int state = radio.begin();
    if (state == RADIOLIB_ERR_NONE) {
        devices_probe |= HW_RADIO_ONLINE;
        LILYGO_LOG_I("✅Radio init succeeded, module: %s", USING_RADIO_NAME);
        setRFSwitch(false); // Default to Built-in LoRa antenna
        return true;
    }
    devices_probe &= ~HW_RADIO_ONLINE;
    LILYGO_LOG_E("❌Radio init failed, code :%d , Use %s", state, USING_RADIO_NAME);
    return false;
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
        if (!sensor.update()) {
            static uint32_t last_bhi_update_error = 0;
            uint32_t now = millis();
            if (last_bhi_update_error == 0 || now - last_bhi_update_error > 2000) {
                last_bhi_update_error = now;
                LILYGO_LOG_W("BHI260 update failed: %s", sensor.getError());
            }
        }
        sendEvent(SENSOR_EVENT);
    }

    // Keep the GNSS parser fed after the asynchronous probe completes.
    if (gps.probeDone() && !gps.probeInProgress() && gps.probeSuccess()) {
        while (Serial1.available()) {
            gps.encode((char)Serial1.read());
        }
    }

    // if (devices_probe & HW_NFC_ONLINE) {
    //     lockSPI();
    //     NFCReader.rfalNfcWorker();
    //     unlockSPI();
    // }

    bootButton.loop();
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


void LilyGoUltra::setRFSwitch(bool to_usb)
{
    io.pinMode(EXPANDS_LORA_RF_SW, OUTPUT);
    if (to_usb) {
        LILYGO_LOG_D("Set RF Switch to USB Iface");
        io.digitalWrite(EXPANDS_LORA_RF_SW, LOW); // to USB
    } else {
        LILYGO_LOG_D("Set RF Switch to Built-in LoRa Antenna");
        io.digitalWrite(EXPANDS_LORA_RF_SW, HIGH); // to Built-in LoRa antenna
    }
}

uint16_t LilyGoUltra::getChargeLevelToCurrentImpl(uint8_t level)
{
    static const uint16_t table[] = {
        0, 100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000
    };
    if (level < 14)
        return table[level];
    return 0;
}

uint16_t LilyGoUltra::getChargeCurrentToLevelImpl()
{
    static const uint16_t table[] = {
        0, 100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000
    };
    uint16_t cur = getChargeCurrent();
    int i = 0;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (cur == table[i]) {
            return i;
        }
    }
    return i;
}

bool LilyGoUltra::shutdown()
{
    pmic.shutdown();
    return true;
}

namespace
{
LilyGoUltra &getInstanceRef()
{
    return *LilyGoUltra::getInstance();
}
}
LilyGoUltra &instance = getInstanceRef();

#endif //ARDUINO_T_WATCH_S3_ULTRA
