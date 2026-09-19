/**
 * @file      LilyGoWatch.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-05-30
 *
 */
#ifdef ARDUINO_TWATCH_BASE

#include "LilyGoLog.h"
#include "LilyGoWatch.h"
#include "SensorWireHelper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "LilyGoLib.h"
#include "core/LilyGoGeneral.h"
#include "driver/rtc_io.h"
#include <SD.h>

#ifndef LILYGO_WATCH_SD_SPI_FREQ
#define LILYGO_WATCH_SD_SPI_FREQ 4000000U
#endif

#ifndef LILYGO_WATCH_BUTTON_ID
#define LILYGO_WATCH_BUTTON_ID 0
#endif

SPIClass sd_spi =  SPIClass(HSPI);

EventGroupHandle_t LilyGoWatch::_event;
static uint32_t _devices_probe = 0;
static bool _pn532_detected = false;
static uint32_t _pn532_firmware_version = 0;

static bool configurePN532PassiveRetries(Adafruit_PN532 &nfc)
{
    uint8_t command[] = {
        PN532_COMMAND_RFCONFIGURATION,
        0x05,
        0xFF,
        0x01,
        0x00,
    };
    if (!nfc.sendCommandCheckAck(command, sizeof(command))) {
        return false;
    }

    // Adafruit PN532 1.3.4 setPassiveActivationRetries() leaves this response
    // unread. Consume it so the next reader command starts from an idle PN532.
    static const uint8_t expected[] = {
        PN532_I2C_READY,
        PN532_PREAMBLE,
        PN532_STARTCODE1,
        PN532_STARTCODE2,
        0x02,
        0xFE,
        PN532_PN532TOHOST,
        PN532_COMMAND_RFCONFIGURATION + 1,
        0xF8,
        PN532_POSTAMBLE,
    };
    uint8_t response[sizeof(expected)] = {};
    const size_t received = Wire.requestFrom((uint8_t)PN532_I2C_ADDRESS,
                            sizeof(response));
    for (size_t i = 0; i < received && i < sizeof(response); ++i) {
        response[i] = Wire.read();
    }
    return received == sizeof(response) &&
           memcmp(response, expected, sizeof(expected)) == 0;
}

static void clickHandler(Button2 &btn)
{
    instance.sendEvent(DeviceEvent::button(LILYGO_WATCH_BUTTON_ID, BUTTON_EVENT_CLICK));
}

static void longClickHandler(Button2 &btn)
{
    instance.sendEvent(DeviceEvent::button(LILYGO_WATCH_BUTTON_ID, BUTTON_EVENT_LONG_PRESSED));
}

static void doubleClickHandler(Button2 &btn)
{
    instance.sendEvent(DeviceEvent::button(LILYGO_WATCH_BUTTON_ID, BUTTON_EVENT_DOUBLE_CLICK));
}

static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

LilyGoWatch::LilyGoWatch() : LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispSPI(DISP_WIDTH, DISP_HEIGHT),
    LilyGoPowerManageInf(pmic, PMIC_TYPE_AXP202),
    BMASensorHelper(this),
    _boot_images_addr(NULL),
    _touchType(0)
{
}

LilyGoWatch::~LilyGoWatch()
{

}

void LilyGoWatch::clearEventBits(const EventBits_t uxBitsToClear)
{
    xEventGroupClearBits(_event, uxBitsToClear);
}

void LilyGoWatch::setEventBits(const EventBits_t uxBitsToSet)
{
    xEventGroupSetBits(_event, uxBitsToSet);
}

const char *LilyGoWatch::getName()
{
    return "LilyGo T-Watch (2019)";
}

const LilyGoDeviceCapability &LilyGoWatch::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        /* boardName */       "LilyGo T-Watch (2019)",
        /* radioName */       "None",
        /* pmicName */        "AXP202",
        /* gaugeName */       "PMU internal",
        /* hasSd */           true,
        /* hasGps */          false,
        /* gpsRuntimeProbe */ false,
        /* hasTouch */        true,
        /* hasKeyboard */     false,
        /* hasTrackball */    false,
        /* hasRotary */       false,
        /* hasBma423 */       true,
        /* hasBhi260 */       false,
        /* hasNfc */          true, // Optional PN532 backplate; see HW_NFC_ONLINE.
        /* hasIrTx */         false,
        /* hasIrRx */         false,
        /* hasEnvSensor */    false,
        /* hasCompass */      false,
        /* hasAudioOut */     false,
        /* hasAudioIn */      false,
        /* hasHaptic */       false,
        /* hasExternalI2c */  true,
        /* hasExternalSpi */  false,
        /* hasExternalUart */ false,
        /* hasExternalGpio */ false,
        /* pmicType */        PMIC_TYPE_AXP202,
        /* hasButton */       true,
        /* hasPmuButton */    true,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoWatch::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = false;
    options.initRtc = true;
    options.initCodec = false;
    return options;
}

bool LilyGoWatch::hasTouch()
{
    return  _devices_probe & HW_TOUCH_ONLINE;
}

uint32_t LilyGoWatch::getDeviceProbe()
{
    return _devices_probe;
}

void LilyGoWatch::setBootImage(uint8_t *image)
{
    _boot_images_addr = image;
}

void LilyGoWatch::setTouchType(uint8_t type)
{
    _touchType = type;
}

uint32_t LilyGoWatch::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoWatch::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

uint32_t LilyGoWatch::begin(const LilyGoDeviceInitOptions &init_options)
{
    if (_event) {
        return _devices_probe;
    }

    _event = xEventGroupCreate();

    while (!psramFound()) {
        LILYGO_LOG_E("PSRAM NOT FOUND!");
        delay(1000);
    }

    _devices_probe |= HW_PSRAM_ONLINE;

    pinMode(BUTTON_INT, INPUT);

    Wire.begin(SDA, SCL);
    Wire1.begin(TP_SDA, TP_SCL);

    if (init_options.scanI2c) {
        LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire));
        LILYGO_LOG_ONLY(SensorWireHelper::dumpDevices(Wire1));
    }

    if (init_options.initPmu) {
        LILYGO_LOG_D("Init PMU");
        if (!initPMU()) {
            LILYGO_LOG_E("Failed to find PMU!");
            assert(0);
        } else {
            _devices_probe |= HW_PMU_ONLINE;
            LILYGO_LOG_I("Initializing PMU succeeded");
        }
    }

    LilyGoDispSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 20);

    if (_boot_images_addr) {
        uint16_t w = this->width();
        uint16_t h = this->height();
        this->pushColors(0, 0, w, h, (uint16_t *)_boot_images_addr);
        incrementalBrightness(250, 20);
    }


    if (init_options.initTouch) {
        initTouch();
    }

    if (init_options.initSensor) {
        initSensor();
    }

    if (init_options.initRtc) {
        initRTC();
    }

    if (init_options.initNfc) {
        initNFC();
    }

    if (init_options.initSd) {
        installSD();
    }

    setRotation(2);


    pinMode(36, INPUT);
    bootButton.setClickHandler(clickHandler);
    bootButton.setLongClickHandler(longClickHandler);
    bootButton.setDoubleClickHandler(doubleClickHandler);

    return _devices_probe;
}

Adafruit_PN532 &LilyGoWatch::getNFC()
{
    // The driver constructor configures GPIOs, so defer it until NFC is requested.
    static Adafruit_PN532 nfc(PN532_IRQ, PN532_RST, &Wire);
    return nfc;
}

uint32_t LilyGoWatch::getNFCFirmwareVersion() const
{
    return _pn532_firmware_version;
}

bool LilyGoWatch::initNFC()
{
    if (_devices_probe & HW_NFC_ONLINE) {
        return true;
    }

    Wire.beginTransmission(PN532_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) {
        _devices_probe &= ~HW_NFC_ONLINE;
        _pn532_firmware_version = 0;
        LILYGO_LOG_D("PN532 backplate not found");
        return false;
    }
    LILYGO_LOG_I("PN532 backplate found");

    // PN532 and SD use mutually exclusive backplates. Once the PN532 address
    // responds, keep SD unavailable even if the reader configuration fails.
    _pn532_detected = true;
    _devices_probe |= HW_SD_UNAVAILABLE;
    if (_devices_probe & HW_SD_ONLINE) {
        uninstallSD();
        sd_spi.end();
    }

    Adafruit_PN532 &nfc = getNFC();
    if (!nfc.begin()) {
        LILYGO_LOG_D("PN532 driver initialization failed");
        return false;
    }

    const uint32_t version = nfc.getFirmwareVersion();
    if ((version >> 24) != 0x32) {
        LILYGO_LOG_D("PN532 firmware query failed");
        return false;
    }

    if (!nfc.SAMConfig() || !configurePN532PassiveRetries(nfc)) {
        LILYGO_LOG_D("PN532 reader configuration failed");
        return false;
    }

    _pn532_firmware_version = version;
    _devices_probe |= HW_NFC_ONLINE;
    LILYGO_LOG_I("PN532 firmware %u.%u on SDA %u, SCL %u",
                 (unsigned)((version >> 16) & 0xFF),
                 (unsigned)((version >> 8) & 0xFF), (unsigned)SDA, (unsigned)SCL);
    return true;
}


bool LilyGoWatch::installSD(uint32_t spi_freq)
{
    if (_pn532_detected) {
        LILYGO_LOG_D("SD unavailable with PN532 backplate");
        return false;
    }

    LILYGO_LOG_D("Init SD");

    sd_spi.begin(SCK, MISO, MOSI);
    if (spi_freq == 0) {
        spi_freq = LILYGO_WATCH_SD_SPI_FREQ;
    }
    SD.end();
    // Set mount point to /fs
    if (!SD.begin(SS, sd_spi, spi_freq, "/sd")) {
        LILYGO_LOG_E("Failed to detect SD Card!!");
        return false;
    }
    if (SD.cardType() != CARD_NONE) {
        LILYGO_LOG_I("SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        _devices_probe |= HW_SD_ONLINE;
        return true;
    }
    return false;
}

void LilyGoWatch::uninstallSD()
{
    _devices_probe &= (~HW_SD_ONLINE);
    lockSPI();
    SD.end();
    unlockSPI();
}

bool LilyGoWatch::isCardReady()
{
    return !_pn532_detected && SD.cardType() != CARD_NONE;
}

bool LilyGoWatch::initTouch()
{
    LILYGO_LOG_D("Init Touch");
    touch.setPins(-1, TP_INT);
    bool res = touch.begin(Wire1, FT6X36_SLAVE_ADDRESS, TP_SDA, TP_SCL);
    if (!res) {
        LILYGO_LOG_E("Failed to find FT6X36!");
    } else {
        LILYGO_LOG_I("Initializing FT6X36 succeeded");
        touch.interruptTrigger(); //enable Interrupt
        _devices_probe |= HW_TOUCH_ONLINE;

        if (_touchType == 0) {
            LILYGO_LOG_I("Touch Type: Rotate");
            touch.setMaxCoordinates(320, 320);
            // touch.setSwapXY(true);
            touch.setMirrorXY(true, true);
        } else {
            LILYGO_LOG_I("Touch Type: Normal");
            touch.setMaxCoordinates(240, 240);
        }

        pinMode(TP_INT, INPUT);
        attachInterrupt(TP_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_TOUCHPAD);
        }, FALLING);
    }
    return res;
}

bool LilyGoWatch::initSensor()
{
    LILYGO_LOG_D("Init BMA Sensor");
    Wire.setClock(1000000UL);

    if (beginSensor(Wire, BMA4XX_I2C_ADDR_SDO_HIGH, SensorRemap::TOP_LAYER_BOTTOM_LEFT_CORNER)) {
        _devices_probe |= HW_BMA_ONLINE;
    }

    Wire.setClock(400000UL);

    pinMode(SENSOR_INT, INPUT);
    attachInterrupt(SENSOR_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_SENSOR);
    }, RISING);

    return true;
}

bool LilyGoWatch::initRTC()
{
    LILYGO_LOG_D("Init PCF8563 RTC");
    bool res = rtc.begin(Wire);
    if (!res) {
        LILYGO_LOG_E("Failed to find PCF8563!");
    } else {
        LILYGO_LOG_I("Initializing PCF8563 succeeded");
        rtc.setClockOutput(SensorPCF8563::CLK_DISABLE);   //Disable clock output ， Conserve Backup Battery Current Consumption
        rtc.hwClockRead();  //Synchronize RTC clock to system clock
        _devices_probe |= HW_RTC_ONLINE;
    }
    return res;
}

bool LilyGoWatch::getTouched()
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_TOUCHPAD) {
        // xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);  //No clear
        return true;
    }
    return false;
}

uint8_t LilyGoWatch::getBrightness()
{
    return LilyGoDispSPI::_brightness;
}

void LilyGoWatch::setBrightness(uint8_t level)
{
    LilyGoDispSPI::setBrightness(level);
}

/*
******************************************************************************
* | CHANNEL     | AXP202                                | Peripherals        |
* | ---------- | -------------------------------------- | ------------------|
* | DC2        | 0.5-1.2V,1.22-1.54V             /2A    | Unused            |
* | DC3        | 0.5-1.2V,1.22-1.54V,1.6-3.4V    /2A    | ESP32             |
* | LDO1       | 3.3V                            /30mA  | Unused            |
* | LDO2      | 3.3V                            /300mA | Display Backlight  |
* | LDO3      | 3.3V                            /300mA |Backplane power supply|
* | LDO4      | 3.3V                            /300mA | Unused             |
*****************************************************************************
*/
bool LilyGoWatch::initPMU()
{
    bool res =  pmic.begin(Wire, AXP202_SLAVE_ADDRESS);
    if (!res) {
        return false;
    }

    // Set the minimum common working voltage of the PMU VBUS input,
    // below this value will turn off the PMU
    pmic.getPower().setInputVoltageLimit(4300);

    // Set the maximum current of the PMU VBUS input,
    // higher than this value will turn off the PMU
    pmic.getPower().setInputCurrentLimit(900);

    // Set VSY off voltage as 2600mV , Adjustment range 2600mV ~ 3300mV
    pmic.getPower().setMinimumSystemVoltage(2600);

    // Display backlight
    pmic.getChannel()->setVoltage(AXP202Channel::CH_LDO2, 3300);
    pmic.getChannel()->enable(AXP202Channel::CH_LDO2, true);

    // Backplane power supply
    pmic.getChannel()->setVoltage(AXP202Channel::CH_LDO3, 3300);
    pmic.getChannel()->enable(AXP202Channel::CH_LDO3, true);

    // UNUSED POWER CHANNEL
    pmic.getChannel()->enable(AXP202Channel::CH_DCDC2, false);
    pmic.getChannel()->enable(AXP202Channel::CH_LDO4, false);

    // Set the time of pressing the button to turn off
    pmic.pwron().setOnDurationMs(4000);

    // Set the button power-on press time
    pmic.pwron().setOffDurationMs(128);

    // Enable internal ADC detection
    pmic.adc().enableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT
    );

    // No charging LED
    pmic.led().setMode(PmicLedBase::Mode::MANUAL);
    pmic.led().setManualState(PmicLedBase::ManualState::HiZ);

    pmic.getCharger()->setChargeVoltage(4288);
    pmic.getCharger()->setFastChargeCurrent(DEVICE_CHARGE_CURRENT_RECOMMEND);
    pmic.getCharger()->setPreChargeCurrent(128);

    pmic.getIrq()->disable(AXP202Irq::IRQ_ALL_MASK);
    pmic.getIrq()->enable(
        AXP202Irq::IRQ_VBUS_INSERT |
        AXP202Irq::IRQ_VBUS_REMOVE |
        AXP202Irq::IRQ_BAT_CHG_START |
        AXP202Irq::IRQ_BAT_CHG_DONE |
        AXP202Irq::IRQ_PEKEY_SHORT_PRESS |
        AXP202Irq::IRQ_PEKEY_LONG_PRESS);
    pmic.getIrq()->clearStatus();


    // Register PMU interrupt management
    // GPIO35 is input-only and has no internal pull-up. PMU_INT has an
    // external pull-up on the board.
    pinMode(PMU_INT, INPUT);
    attachInterrupt(PMU_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_POWER);
    }, FALLING);

    return true;
}

void LilyGoWatch::checkPowerStatus()
{
    bool batteryInsert = pmic.isBatteryConnect();

    // Get PMU Interrupt Status Register
    uint64_t irqStatus = pmic.irq().readStatus(true);

    if (AXP202Irq::isVbusInsert(irqStatus)) {
        LILYGO_LOG_D("isVbusInsert");
        sendEvent(DeviceEvent::power(PMU_EVENT_USBC_INSERT));
    }
    if (AXP202Irq::isVbusRemove(irqStatus)) {
        LILYGO_LOG_D("isVbusRemove");
        sendEvent(DeviceEvent::power(PMU_EVENT_USBC_REMOVE));
    }
    if (AXP202Irq::isBatInsert(irqStatus)) {
        LILYGO_LOG_D("isBatInsert");
        sendEvent(DeviceEvent::power(PMU_EVENT_BATTERY_INSERT));
    }
    if (AXP202Irq::isBatRemove(irqStatus)) {
        LILYGO_LOG_D("isBatRemove");
        sendEvent(DeviceEvent::power(PMU_EVENT_BATTERY_REMOVE));
    }
    if (AXP202Irq::isPekeyShortPress(irqStatus)) {
        LILYGO_LOG_D("isPekeyShortPress");
        sendEvent(DeviceEvent::power(PMU_EVENT_KEY_CLICKED));
    }
    if (AXP202Irq::isPekeyLongPress(irqStatus)) {
        LILYGO_LOG_D("isPekeyLongPress");
        sendEvent(DeviceEvent::power(PMU_EVENT_KEY_LONG_PRESSED));
    }
    if (AXP202Irq::isTimerTimeout(irqStatus)) {
        LILYGO_LOG_D("isWdtExpire");
    }
    if (batteryInsert) {
        if (AXP202Irq::isBatChgDone(irqStatus)) {
            LILYGO_LOG_D("isBatChgDone");
            sendEvent(DeviceEvent::power(PMU_EVENT_CHARGE_FINISH));
        }
        if (AXP202Irq::isBatChgStart(irqStatus)) {
            LILYGO_LOG_D("isBatChgStart");
            sendEvent(DeviceEvent::power(PMU_EVENT_CHARGE_STARTED));
        }
    }
}

void LilyGoWatch::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    LILYGO_LOG_D("Power channel:%u set %s", ch, enable ? " ON" : " OFF");
    switch (ch) {
    case POWER_DISPLAY_BACKLIGHT:
        if (enable) {
            pmic.getChannel()->enable(AXP202Channel::CH_LDO2, true);
        } else {
            pmic.getChannel()->enable(AXP202Channel::CH_LDO2, false);
        }
        break;
    default:
        break;
    }
}


uint64_t LilyGoWatch::checkWakeupPins(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = 0;
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        LILYGO_LOG_D("Enable touch panel from wakeup source.");
        wakeup_pin |=  (1ULL << (TP_INT));
    }
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        LILYGO_LOG_D("Enable power button from wakeup source.");
        wakeup_pin |=  (1ULL << (PMU_INT));
    }
    if (wakeup_src & WAKEUP_SRC_BUTTON) {
        LILYGO_LOG_D("Enable button from wakeup source.");
        wakeup_pin |=  (1ULL << (BUTTON_INT));
    }
    if (wakeup_pin == 0) {
        LILYGO_LOG_E("No wake-up method is set. T-Watch supports WAKEUP_SRC_POWER_KEY, WAKEUP_SRC_TOUCH_PANEL, and WAKEUP_SRC_BUTTON (GPIO36).");
    }
    return wakeup_pin;
}

void LilyGoWatch::lightSleep(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = checkWakeupPins(wakeup_src);
    if (wakeup_pin == 0) {
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t err;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    err = esp_sleep_enable_ext1_wakeup_io(wakeup_pin, ESP_EXT1_WAKEUP_ALL_LOW);
#else
    err = esp_sleep_enable_ext1_wakeup(wakeup_pin, ESP_EXT1_WAKEUP_ALL_LOW);
#endif
    if (err != ESP_OK) {
        LILYGO_LOG_E("Failed to enable ext1 wakeup: %s", esp_err_to_name(err));
        return;
    }

    sleepDisplay();

    pmic.getIrq()->disable(AXP202Irq::IRQ_ALL_MASK);
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        pmic.getIrq()->enable(AXP202Irq::IRQ_PKEY_NEGATIVE |
                              AXP202Irq::IRQ_PEKEY_SHORT_PRESS);
    }
    // Clearing the PMIC status is the final bus operation before sleep so its
    // active-low interrupt line cannot remain asserted by an old event.
    pmic.getIrq()->clearStatus();

    err = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    if (err != ESP_OK) {
        LILYGO_LOG_E("Failed to enter light sleep: %s", esp_err_to_name(err));
    }

    wakeupDisplay();

    pmic.getIrq()->disable(AXP202Irq::IRQ_ALL_MASK);
    pmic.getIrq()->enable(
        AXP202Irq::IRQ_VBUS_INSERT |
        AXP202Irq::IRQ_VBUS_REMOVE |
        AXP202Irq::IRQ_BAT_CHG_START |
        AXP202Irq::IRQ_BAT_CHG_DONE |
        AXP202Irq::IRQ_PEKEY_SHORT_PRESS |
        AXP202Irq::IRQ_PEKEY_LONG_PRESS);
    pmic.getIrq()->clearStatus();
}

void LilyGoWatch::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
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
        wakeup_result = esp_sleep_enable_ext1_wakeup_io(wakeup_pin, ESP_EXT1_WAKEUP_ALL_LOW);
#else
        wakeup_result = esp_sleep_enable_ext1_wakeup(wakeup_pin, ESP_EXT1_WAKEUP_ALL_LOW);
#endif
    }
    if (wakeup_result != ESP_OK) {
        LILYGO_LOG_E("Failed to configure T-Watch deep-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        return;
    }

    pmic.getIrq()->disable(AXP202Irq::IRQ_ALL_MASK);

    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        LILYGO_LOG_D("Enable power button from wakeup source.");
    }
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        LILYGO_LOG_D("Enable power touch panel from wakeup source.");
        keep_touch_power = true;
    }

    if (sensor != nullptr) {
        sensor->setOperationMode(OperationMode::SUSPEND);
    }

    LilyGoDispSPI::sleep();

    LilyGoDispSPI::end();

    // Turn off ADC data monitoring to save power
    pmic.adc().disableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT
    );

    // Enable PMU sleep
    pmic.enableSleep(true);

    // Display Backlight
    pmic.getChannel()->enable(AXP202Channel::CH_LDO2, false);

    if (!keep_touch_power) {
        touch.sleep();
    }

    int i = 4;
    while (i--) {
        LILYGO_LOG_PRINTF("%d second sleep ...\n", i);
        delay(1000);
    }

    Serial1.end();

    SPI.end();

    if ((wakeup_src & WAKEUP_SRC_POWER_KEY) &&
            !pmic.getIrq()->enable(AXP202Irq::IRQ_PKEY_NEGATIVE |
                                   AXP202Irq::IRQ_PEKEY_SHORT_PRESS)) {
        LILYGO_LOG_E("Failed to enable AXP202 power-key wake interrupt.");
        ESP.restart();
        return;
    }
    // Clear pending PMIC events immediately before closing I2C. PMU_INT is
    // active low and must be high when deep sleep starts.
    if (!pmic.getIrq()->clearStatus()) {
        LILYGO_LOG_E("Failed to clear AXP202 interrupt status before deep sleep.");
        ESP.restart();
        return;
    }
    pinMode(PMU_INT, INPUT);
    delay(2);
    const int pmu_int_level = digitalRead(PMU_INT);
    LILYGO_LOG_D("AXP202 PMU_INT level before deep sleep: %d", pmu_int_level);
    if ((wakeup_src & WAKEUP_SRC_POWER_KEY) && pmu_int_level == LOW) {
        LILYGO_LOG_E("AXP202 interrupt line is still low before deep sleep.");
        ESP.restart();
        return;
    }
    Wire.end();
    Wire1.end();

    const uint8_t pins[] = {
        DISP_MOSI,
        DISP_SCK,
        DISP_CS,
        DISP_DC,
        DISP_BL,
        // TP_INT,
        RTC_INT,
        // PMU_INT,
        TP_SDA,
        TP_SCL,

        // SENSOR_INT,

        SDA,
        SCL,

        MOSI,
        MISO,
        SCK,
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

    gpio_reset_pin((gpio_num_t )SENSOR_INT);
    pinMode(SENSOR_INT, OPEN_DRAIN);

    esp_deep_sleep_start();
}


void LilyGoWatch::sleepDisplay()
{
    // LilyGoDispSPI::sleep();
}

void LilyGoWatch::wakeupDisplay()
{
    // LilyGoDispSPI::wakeup();
}

uint16_t LilyGoWatch::width()
{
    return DISP_WIDTH;
}

uint16_t LilyGoWatch::height()
{
    return DISP_HEIGHT;
}

uint8_t LilyGoWatch::getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point )
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
            if (_touchType == 0) {
                int16_t tmp_x, tmp_y;
                for (int i = 0; i < pointsToCopy; ++i) {
                    tmp_x = x_array[i];
                    tmp_y = y_array[i];
                    x_array[i] = map(tmp_x, 0, 320, 0, 240);
                    y_array[i] = map(tmp_y, 0, 320, 0, 240);
                }
            }
            return pointsToCopy;
        }

        xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);

    }
    return 0;
}

void LilyGoWatch::setRotation(uint8_t rotation)
{
    LilyGoDispSPI::setRotation(rotation);
}

uint8_t LilyGoWatch::getRotation()
{
    return  LilyGoDispSPI::getRotation();
}

bool LilyGoWatch::needSwapColors()
{
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    return true;
#else
    return false;
#endif
}


void LilyGoWatch::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispSPI::pushColors( x1,  y1,  x2,  y2, color);
}

void LilyGoWatch::vibrator()
{
    if (_pn532_detected) return;
}

void LilyGoWatch::loop()
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
        loopSensor();
    }

    bootButton.loop();
}

bool LilyGoWatch::lockSPI(TickType_t xTicksToWait)
{
    return true;
}

void LilyGoWatch::unlockSPI()
{
}

bool LilyGoWatch::shutdown()
{
    pmic.shutdown();
    return true;
}

namespace
{
LilyGoWatch &getInstanceRef()
{
    return *LilyGoWatch::getInstance();
}
}

LilyGoWatch &instance = getInstanceRef();

#endif
