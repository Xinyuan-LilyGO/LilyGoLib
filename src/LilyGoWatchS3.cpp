/**
 * @file      LilyGoWatchS3.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 */
#ifdef ARDUINO_T_WATCH_S3

#include "LilyGoLog.h"
#include "LilyGoWatchS3.h"
#include "SensorWireHelper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "LilyGoLib.h"
#include "core/LilyGoGeneral.h"
#include "driver/rtc_io.h"
#include <Preferences.h>

extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);


// 940mAh battery parameters
static uint8_t BATTERY_PARAMS_940mAh[] = {
    0x01, 0xF5, 0x40, 0x00, 0x1B, 0x1E, 0x28, 0x0F, 0x0C, 0x1E, 0x32, 0x2, 0x14, 0x05, 0x0A, 0x04,
    0x74, 0xFF, 0xB0, 0x0D, 0x43, 0x10, 0x1C, 0xFC, 0x90, 0x01, 0xEA, 0x0C, 0x54, 0x06, 0x1C, 0x06,
    0x12, 0x0A, 0xFE, 0x0F, 0xA6, 0x0F, 0x65, 0x0A, 0x17, 0x0E, 0xE4, 0x0E, 0xE2, 0x04, 0xD6, 0x04,
    0xCC, 0x09, 0xC0, 0x0E, 0xAE, 0x0E, 0xA8, 0x09, 0x9F, 0x0E, 0x89, 0x0E, 0x84, 0x04, 0x72, 0x04,
    0x69, 0x09, 0x65, 0x0E, 0x45, 0x0D, 0xDF, 0x07, 0xE0, 0x40, 0x1D, 0x1D, 0x16, 0x07, 0x0D, 0x03,
    0xC5, 0x98, 0x7E, 0x66, 0x4E, 0x44, 0x38, 0x1A, 0x12, 0x0A, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6,
    0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6,
    0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6
};

// 470mAh battery parameters
static uint8_t BATTERY_PARAMS_470mAh[] = {
    0x01, 0xF5, 0x40, 0x00, 0x1B, 0x1E, 0x28, 0x0F, 0x0C, 0x1E, 0x32, 0x2, 0x14, 0x05, 0x0A, 0x04,
    0x74, 0xFF, 0xB0, 0x0D, 0x43, 0x10, 0x01, 0xFC, 0x90, 0x01, 0xEA, 0x06, 0xE0, 0x06, 0x01, 0x05,
    0xF7, 0x0A, 0xED, 0x0F, 0xE5, 0x0F, 0x85, 0x0A, 0x33, 0x0F, 0xF6, 0x0E, 0xEF, 0x04, 0xE1, 0x04,
    0xD3, 0x09, 0xC3, 0x0E, 0xAD, 0x0E, 0xA7, 0x09, 0x9C, 0x0E, 0x82, 0x0E, 0x7B, 0x04, 0x6C, 0x04,
    0x69, 0x09, 0x5F, 0x0E, 0x06, 0x0D, 0x55, 0x06, 0x90, 0x5E, 0x2F, 0x2A, 0x20, 0x0F, 0x13, 0x09,
    0xC5, 0x98, 0x7E, 0x66, 0x4E, 0x44, 0x38, 0x1A, 0x12, 0x0A, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6,
    0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6,
    0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6
};

EventGroupHandle_t LilyGoWatch2022::_event;
static TimerHandle_t timerHandler = NULL;

LILYGO_DEFINE_RADIO();

static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

LilyGoWatch2022::LilyGoWatch2022() :
    LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispSPI(DISP_WIDTH, DISP_HEIGHT),
    LilyGoPowerManageInf(pmic, PMIC_TYPE_AXP2101),
    BMASensorHelper(this),
    _audioOutput(I2S_BCLK, I2S_WCLK, I2S_DOUT),
    _audioInput(MIC_SCK, MIC_DAT),
    _effects(1),
    devices_probe(0),
    _boot_images_addr(NULL)
{
}

LilyGoWatch2022::~LilyGoWatch2022()
{

}

void LilyGoWatch2022::clearEventBits(const EventBits_t uxBitsToClear)
{
    xEventGroupClearBits(_event, uxBitsToClear);
}

void LilyGoWatch2022::setEventBits(const EventBits_t uxBitsToSet)
{
    xEventGroupSetBits(_event, uxBitsToSet);
}

const char *LilyGoWatch2022::getName()
{
    return "LilyGo T-Watch-S3(2022)";
}

const LilyGoDeviceCapability &LilyGoWatch2022::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        /* boardName */       "LilyGo T-Watch-S3(2022)",
#ifdef USING_RADIO_NAME
        /* radioName */       USING_RADIO_NAME,
#else
        /* radioName */       "None",
#endif
        /* pmicName */        "AXP2101",
        /* gaugeName */       "PMU internal",
        /* hasSd */           false,
        /* hasGps */          true,
        /* gpsRuntimeProbe */ true,
        /* hasTouch */        true,
        /* hasKeyboard */     false,
        /* hasTrackball */    false,
        /* hasRotary */       false,
        /* hasBma423 */       true,
        /* hasBhi260 */       false,
        /* hasNfc */          false,
        /* hasIrTx */         true,
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
        /* hasButton */       false,
        /* hasPmuButton */    true,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoWatch2022::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initRtc = true;
    options.initCodec = false;
    return options;
}

bool LilyGoWatch2022::hasTouch()
{
    return  devices_probe & HW_TOUCH_ONLINE;
}

uint32_t LilyGoWatch2022::getDeviceProbe()
{
    if (gps.probeDone() && !gps.probeInProgress() && gps.probeSuccess()) {
        devices_probe |= HW_GPS_ONLINE;
    }
    return devices_probe;
}

void LilyGoWatch2022::setBootImage(uint8_t *image)
{
    _boot_images_addr = image;
}

uint32_t LilyGoWatch2022::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoWatch2022::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

uint32_t LilyGoWatch2022::begin(const LilyGoDeviceInitOptions &init_options)
{
    if (_event) {
        return devices_probe;
    }

    const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1) {
        LILYGO_LOG_D("Deep-sleep EXT1 wake GPIO mask: 0x%llX",
                     static_cast<unsigned long long>(esp_sleep_get_ext1_wakeup_status()));
    }

    _event = xEventGroupCreate();

    while (!psramFound()) {
        LILYGO_LOG_E("PSRAM NOT FOUND!");
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;


    if (init_options.initFatfs) {
        setupMSC(_lock_callback, _unlock_callback);
    }

    Wire.begin(SDA, SCL);
    Wire1.begin(TP_SDA, TP_SCL);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

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
            devices_probe |= HW_PMU_ONLINE;
            LILYGO_LOG_I("Initializing PMU succeeded");
        }
    }

    LilyGoDispSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 80);

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

    if (init_options.initHaptic) {
        initDrv();
    }

    if (init_options.initGps) {
        initGPS();
    }

    if (init_options.initAudio) {
        initMicrophone();
    }

    if (init_options.initAudio) {
        initAmplifier();
    }

    if (init_options.initRadio) {
        initLoRa();
    }

    Preferences prefs;
    prefs.begin("lilygo", false);
    bool batteryCalibrated = prefs.getBool("calibration");
    prefs.end();

    if (!batteryCalibrated && gps.probeInProgress()) {
        LILYGO_LOG_D("Battery calibration deferred until GPS probe completes");
    } else if (!batteryCalibrated) {
        // The presence of GPS is used to determine if it is a T-Watch-Plus;
        // if so, the 940mAh battery parameter is written into the system.
        _is_watch_plus = devices_probe & HW_GPS_ONLINE;
        calibrationPMU(_is_watch_plus ? 940 : 470);
    }

    setRotation(0);

    return true;
}


bool LilyGoWatch2022::initDrv()
{
    LILYGO_LOG_D("Init DRV2605");
    bool res = drv.begin(Wire, DRV2605_SLAVE_ADDRESS);
    if (!res) {
        LILYGO_LOG_E("Failed to find DRV2605!");
    } else {
        LILYGO_LOG_I("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(HapticMode::INTERNAL_TRIGGER);
        drv.setWaveform(0, _effects);  // play effect
        drv.setWaveform(1, 0);  // end waveform
        drv.run();
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}
void LilyGoWatch2022::calibrateBatteryIfNeeded(bool gps_present)
{
    Preferences prefs;
    prefs.begin("lilygo", false);
    bool batteryCalibrated = prefs.getBool("calibration");
    prefs.end();

    if (batteryCalibrated) return;

    _is_watch_plus = gps_present;
    calibrationPMU(gps_present ? 940 : 470);
}

void LilyGoWatch2022::gpsProbeCallback(bool success, const char *model, void *user_data)
{
    LilyGoWatch2022 *watch = (LilyGoWatch2022 *)user_data;
    if (!watch) return;

    if (success) {
        LILYGO_LOG_I("%s GPS init succeeded", model ? model : "GPS");
        watch->devices_probe |= HW_GPS_ONLINE;
    } else {
        LILYGO_LOG_E("Warning: Failed to find GPS Module");
        watch->devices_probe &= ~HW_GPS_ONLINE;
        // If GPS is not detected, turn off DC3 after the async probe completes.
        watch->pmic.getChannel()->enable(AXP2101Channel::CH_DCDC3, false);
    }
    watch->calibrateBatteryIfNeeded(success);
}

bool LilyGoWatch2022::initGPS()
{
    LILYGO_LOG_D("Init GPS");
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    bool res = gps.beginAsyncProbe(&Serial1, GPS_PROBE_ALL, LilyGoWatch2022::gpsProbeCallback, this);
    if (res) LILYGO_LOG_D("GPS async probe started");
    return res;
}

bool LilyGoWatch2022::initTouch()
{
    LILYGO_LOG_D("Init Touch");
    touch.setPins(-1, TP_INT);
    bool res = touch.begin(Wire1, FT6X36_SLAVE_ADDRESS, TP_SDA, TP_SCL);
    if (!res) {
        LILYGO_LOG_E("Failed to find FT6X36!");
    } else {
        LILYGO_LOG_I("Initializing FT6X36 succeeded");
        touch.interruptTrigger(); //enable Interrupt
        devices_probe |= HW_TOUCH_ONLINE;
        touch.setMaxCoordinates(240, 240);

        pinMode(TP_INT, INPUT_PULLUP);
        attachInterrupt(TP_INT, []() {
            setGroupBitsFromISR(_event, HW_IRQ_TOUCHPAD);
        }, FALLING);
    }
    return res;
}

bool LilyGoWatch2022::initSensor()
{
    LILYGO_LOG_D("Init BMA423");

    Wire.setClock(1000000UL);

    if (beginSensor(Wire, BMA4XX_I2C_ADDR_SDO_HIGH, SensorRemap::BOTTOM_LAYER_TOP_RIGHT_CORNER)) {
        devices_probe |= HW_BMA_ONLINE;
    }

    Wire.setClock(400000UL);

    pinMode(SENSOR_INT, INPUT);
    attachInterrupt(SENSOR_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_SENSOR);
    }, RISING);

    return true;
}

bool LilyGoWatch2022::initRTC()
{
    LILYGO_LOG_D("Init PCF8563 RTC");
    bool res = rtc.begin(Wire);
    if (!res) {
        LILYGO_LOG_E("Failed to find PCF8563!");
    } else {
        LILYGO_LOG_I("Initializing PCF8563 succeeded");
        rtc.setClockOutput(SensorPCF8563::CLK_DISABLE);   //Disable clock output ， Conserve Backup Battery Current Consumption
        rtc.hwClockRead();  //Synchronize RTC clock to system clock
        devices_probe |= HW_RTC_ONLINE;
    }
    return res;
}

bool LilyGoWatch2022::initMicrophone()
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

bool LilyGoWatch2022::initAmplifier()
{
    LILYGO_LOG_V("Init Audio Amplifier");
    bool res = false;
    res = _audioOutput.begin();
    _audioOutput.setMuteCallback([](bool muted) {
        LILYGO_LOG_D("Audio Amplifier %s", muted ? "muted" : "unmuted");
        instance.powerControl(POWER_SPEAK, muted);
    });
    // #endif
    if (res) {
        LILYGO_LOG_I("Audio Amplifier init succeeded");
    } else {
        LILYGO_LOG_E("Warning: Failed to init Audio Amplifier");
    }
    return res;
}

bool LilyGoWatch2022::getTouched()
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_TOUCHPAD) {
        // xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);  //No clear
        return true;
    }
    return false;
}

uint8_t LilyGoWatch2022::getBrightness()
{
    return LilyGoDispSPI::_brightness;
}

void LilyGoWatch2022::setBrightness(uint8_t level)
{
    LilyGoDispSPI::setBrightness(level);
}

/*
* This power distribution is really bad.
*****************************************************************************************
* | CHIP       | AXP2101                                | Peripherals        | SchName  |
* | ---------- | -------------------------------------- | ------------------ | -------- |
* | DC1        | 3.3V                            /2A    | ESP32-S3           | VDD3V3   |
* | DC2        | 0.5-1.2V,1.22-1.54V             /2A    | Unused             | X        |
* | DC3        | 0.5-1.2V,1.22-1.54V,1.6-3.4V    /2A    | GPS                | VCC_2_5V |
* | DC4        | 0.5-1.2V,1.22-1.84V             /1.5A  | GPS               | X        | (LS550G GPS Version)
* | DC5        | 1.2V,1.4-3.7V                   /1A    | Unused             | X        |
* | LDO1(VRTC) | 3.3V                            /30mA  | Unused             | X        |
* | ALDO1      | 3.3V                            /300mA | Unused             | X        |
* | ALDO2      | 3.3V                            /300mA | Display Backlight  | LCD_VDD  |
* | ALDO3      | 3.3V                            /300mA | Display and Touch  | LDO3     |
* | ALDO4      | 3.3V                            /300mA | Radio              | LDO4     |
* | BLDO1      | 3.3V                            /300mA | Unused             | X        |
* | BLDO2      | 3.3V                            /300mA | DRV2605 Enable Pin | LDO5     |
* | DLDO1      | 3.3V                            /300mA | Unused             | X        |
* | CPUSLDO    | 1.4V                            /30mA  | Unused             | X        |
* | VBACKUP    | 3.3V                            /30mA  | RTC Button Battery | RTC_3_3V |
*****************************************************************************************
*/
bool LilyGoWatch2022::initPMU()
{
    bool res =  pmic.begin(Wire, AXP2101_SLAVE_ADDRESS);
    if (!res) {
        return false;
    }

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        const uint64_t irq_status = pmic.irq().readStatus(false);
        LILYGO_LOG_D("AXP2101 IRQ status after deep-sleep wake: 0x%06llX",
                     static_cast<unsigned long long>(irq_status));
    }

    // Set the minimum common working voltage of the PMU VBUS input,
    // below this value will turn off the PMU
    pmic.getPower().setInputVoltageLimit(4360);

    // Set the maximum current of the PMU VBUS input,
    // higher than this value will turn off the PMU
    pmic.getPower().setInputCurrentLimit(900);

    // Set VSY off voltage as 2600mV , Adjustment range 2600mV ~ 3300mV
    pmic.getPower().setMinimumSystemVoltage(2600);

    // Display backlight
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO2, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, true);

    // Display and Touch
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO3, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, true);

    // Radio
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_ALDO4, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, true);

    // Drv2605 enable pin
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_BLDO2, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, true);

    // GPS , (The version with BOOT button and RST on the back cover)
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_BLDO1, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, true);

    // GPS , Earlier versions use DC3 (without BOOT button and RST)
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_DCDC3, 3300);
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC3, true);

    // RTC backup battery
    pmic.charger().setButtonBatteryChargeVoltage(3300);
    pmic.enableModule(AXP2101Core::Module::BTN_CHARGE, true);

    // LS550G GPS Need 0.85V Core voltage,Only LS550G GPS Version
    pmic.getChannel()->setVoltage(AXP2101Channel::CH_DCDC4, 850);
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC4, true);

    // UNUSED POWER CHANNEL
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC2, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC5, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO1, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_CPUSLDO, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_DLDO1, false);
    pmic.getChannel()->enable(AXP2101Channel::CH_DLDO2, false);


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
        PmicAdcBase::Channel::BAT_CURRENT |
        PmicAdcBase::Channel::DIE_TEMPERATURE
    );

    pmic.led().setMode(PmicLedBase::Mode::MANUAL);
    pmic.led().setManualState(PmicLedBase::ManualState::HiZ);

    pmic.getCharger()->setChargeVoltage(4350);
    pmic.getCharger()->setFastChargeCurrent(DEVICE_CHARGE_CURRENT_RECOMMEND);
    pmic.getCharger()->setPreChargeCurrent(50);
    pmic.getCharger()->setTerminationCurrent(25);

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);
    pmic.getIrq()->enable(
        AXP2101Irq::IRQ_VBUS_INSERT |
        AXP2101Irq::IRQ_VBUS_REMOVE |
        AXP2101Irq::IRQ_BAT_CHG_START |
        AXP2101Irq::IRQ_BAT_CHG_DONE |
        AXP2101Irq::IRQ_PEKEY_SHORT_PRESS |
        AXP2101Irq::IRQ_PEKEY_LONG_PRESS);
    pmic.getIrq()->clearStatus();


    // Register PMU interrupt management
    pinMode(PMU_INT, INPUT_PULLUP);
    attachInterrupt(PMU_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_POWER);
    }, FALLING);

    return true;
}

bool LilyGoWatch2022::calibrationPMU(uint16_t batteryCapacity)
{
    uint8_t *batteryParams = NULL;
    if (batteryCapacity == 940) {
        batteryParams = BATTERY_PARAMS_940mAh;
    } else if (batteryCapacity == 470) {
        batteryParams = BATTERY_PARAMS_470mAh;
    } else {
        return false;
    }
    if (pmic.power().writeGaugeData(batteryParams, 128)) {
        LILYGO_LOG_D("Battery calibration data write success");
        Preferences prefs;
        prefs.begin("lilygo", false);
        prefs.putBool("calibration", true);
        prefs.end();
    } else {
        LILYGO_LOG_E("Battery calibration data write failed");
        return false;
    }
    return true;
}

void LilyGoWatch2022::checkPowerStatus()
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

void LilyGoWatch2022::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    LILYGO_LOG_D("Power channel:%u set %s", ch, enable ? " ON" : " OFF");
    switch (ch) {
    case POWER_DISPLAY_BACKLIGHT:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, false);
        }
        break;
    case POWER_DISPLAY:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, false);
        }
        break;
    case POWER_RADIO:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, false);
        }
        break;
    case POWER_HAPTIC_DRIVER:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, false);
        }
        break;
    case POWER_GPS:
        if (enable) {
            // LS550G Version Only
            pmic.getChannel()->enable(AXP2101Channel::CH_DCDC4, true);
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, true);
            Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
        } else {
            // LS550G Version Only
            pmic.getChannel()->enable(AXP2101Channel::CH_DCDC4, false);
            pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, false);
            Serial1.end();
            gpio_reset_pin((gpio_num_t )GPS_RX);
            pinMode(GPS_RX, OPEN_DRAIN);
            gpio_reset_pin((gpio_num_t )GPS_TX);
            pinMode(GPS_TX, OPEN_DRAIN);
        }
        break;
    case POWER_SPEAK:
        if (enable) {
            pmic.getChannel()->enable(AXP2101Channel::CH_DLDO1, true);
        } else {
            pmic.getChannel()->enable(AXP2101Channel::CH_DLDO1, false);
        }
        break;
    default:
        break;
    }
}


uint64_t LilyGoWatch2022::checkWakeupPins(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = 0;
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        wakeup_pin |=  _BV(TP_INT);
    }
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        wakeup_pin |=  _BV(PMU_INT);
    }
    if (wakeup_pin == 0) {
        LILYGO_LOG_E("No wake-up method is set. T-WatchS3 supports WAKEUP_SRC_POWER_KEY and WAKEUP_SRC_TOUCH_PANEL.");
    }
    return wakeup_pin;
}


void LilyGoWatch2022::lightSleep(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = checkWakeupPins(wakeup_src);
    if (wakeup_pin == 0) {
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t wakeup_result;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // The IDF 5 API appends pins, so discard pins left by an earlier sleep.
    esp_sleep_disable_ext1_wakeup_io(0);
    wakeup_result = esp_sleep_enable_ext1_wakeup_io(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    wakeup_result = esp_sleep_enable_ext1_wakeup(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    if (wakeup_result != ESP_OK) {
        LILYGO_LOG_E("Failed to enable T-Watch S3 light-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        return;
    }

#if !defined(ARDUINO_LILYGO_LORA_SX1280)
    // SX1280 died here, the reason is not analyzed yet, waiting to be processed
    radio.sleep();
#endif

    powerControl(POWER_DISPLAY_BACKLIGHT, false);
    powerControl(POWER_HAPTIC_DRIVER, false);
    powerControl(POWER_GPS, false);
    powerControl(POWER_SPEAK, false);
    powerControl(POWER_NFC, false);

    sleepDisplay();

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        rtc_gpio_pullup_en((gpio_num_t)PMU_INT);
        pmic.getIrq()->enable(AXP2101Irq::IRQ_PEKEY_SHORT_PRESS);
    }
    pmic.getIrq()->clearStatus();

    const esp_err_t sleep_result = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // Do not let the touch pin leak into a later deep-sleep EXT1 mask.
    esp_sleep_disable_ext1_wakeup_io(0);
#endif
    if (sleep_result != ESP_OK) {
        LILYGO_LOG_E("Failed to enter T-Watch S3 light sleep: %s",
                     esp_err_to_name(sleep_result));
    }

    radio.standby();

    wakeupDisplay();

    powerControl(POWER_DISPLAY_BACKLIGHT, true);
    powerControl(POWER_HAPTIC_DRIVER, true);
    powerControl(POWER_GPS, true);
    powerControl(POWER_NFC, true);

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);
    pmic.getIrq()->enable(
        AXP2101Irq::IRQ_VBUS_INSERT |
        AXP2101Irq::IRQ_VBUS_REMOVE |
        AXP2101Irq::IRQ_BAT_CHG_START |
        AXP2101Irq::IRQ_BAT_CHG_DONE |
        AXP2101Irq::IRQ_PEKEY_SHORT_PRESS |
        AXP2101Irq::IRQ_PEKEY_LONG_PRESS);
    pmic.getIrq()->clearStatus();
}

void LilyGoWatch2022::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
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
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // esp_sleep_enable_ext1_wakeup_io() appends pins to the existing mask.
    esp_sleep_disable_ext1_wakeup_io(0);
#endif

    pmic.getIrq()->disable(AXP2101Irq::IRQ_ALL_MASK);

    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        LILYGO_LOG_D("Enable power button from wakeup source.");
        if (!pmic.getIrq()->enable(AXP2101Irq::IRQ_PEKEY_SHORT_PRESS)) {
            LILYGO_LOG_E("Failed to enable AXP2101 power-key wake interrupt.");
            return;
        }
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
        PmicAdcBase::Channel::BAT_CURRENT |
        PmicAdcBase::Channel::DIE_TEMPERATURE);

    // Enable PMU sleep
    pmic.power().enableSleep();

    // Do not turn off the screen power in deep sleep,
    // otherwise the current will increase abnormally by about 600uA.
    // The correct way is to keep the power supply and set the screen and touch to sleep.
    // At this time, the screen and touch consume a total of about 103.4uA (screen 100uA, touch 3.4uA)
    // Display
    // pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, false);

    if (!keep_touch_power) {
        touch.sleep();
        // Display and Touch
        pmic.getChannel()->enable(AXP2101Channel::CH_ALDO3, false);
    }
    // Display Backlight
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO2, false);
    // LoRa
    pmic.getChannel()->enable(AXP2101Channel::CH_ALDO4, false);
    // Drv2605 Enable
    pmic.getChannel()->enable(AXP2101Channel::CH_BLDO2, false);
    // GPS , Earlier versions use DC3 (without BOOT button and RST)
    pmic.getChannel()->enable(AXP2101Channel::CH_DCDC3, false);
    // GPS , (The version with BOOT button and RST on the back cover)
    pmic.getChannel()->enable(AXP2101Channel::CH_BLDO1, false);

    // Turn off the RTC backup battery, adding about 200uA
    if (off_rtc_backup_domain) {
        // RTC Battery
        pmic.enableModule(AXP2101Core::Module::BTN_CHARGE, false);
    }

    int i = 4;
    while (i--) {
        LILYGO_LOG_PRINTF("%d second sleep ...\n", i);
        delay(1000);
    }

    Serial1.end();

    SPI.end();

    // Match T-Watch Ultra: clear pending PMIC events as the final PMIC access.
    // PMU_INT is active low and must be released before EXT1 can be armed safely.
    if (!pmic.getIrq()->clearStatus()) {
        LILYGO_LOG_E("Failed to clear AXP2101 interrupt status before deep sleep.");
        ESP.restart();
        return;
    }
    pinMode(PMU_INT, INPUT_PULLUP);
    delay(2);
    const int pmu_int_level = digitalRead(PMU_INT);
    LILYGO_LOG_D("AXP2101 PMU_INT level before deep sleep: %d", pmu_int_level);
    if ((wakeup_src & WAKEUP_SRC_POWER_KEY) && pmu_int_level == LOW) {
        LILYGO_LOG_E("AXP2101 interrupt line is still low before deep sleep.");
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

        MIC_SCK,
        MIC_DAT,

        I2S_BCLK,
        I2S_WCLK,
        I2S_DOUT,

        IR_SEND,

        SDA,
        SCL,

        MOSI,
        MISO,
        SCK,

        GPS_TX,
        GPS_RX,

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

    gpio_reset_pin((gpio_num_t )SENSOR_INT);
    pinMode(SENSOR_INT, OPEN_DRAIN);

    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        // PMU_INT is open-drain. Configure the RTC pad pull explicitly because
        // EXT1 switches the pin from the digital GPIO domain during deep sleep.
        rtc_gpio_pulldown_dis((gpio_num_t)PMU_INT);
        rtc_gpio_pullup_en((gpio_num_t)PMU_INT);
    }

    // Configure wake sources only after GPIO cleanup, matching the known-good
    // pre-SensorLib implementation. Timer and physical wake can be combined.
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
        LILYGO_LOG_E("Failed to configure T-Watch S3 deep-sleep wakeup: %s",
                     esp_err_to_name(wakeup_result));
        ESP.restart();
        return;
    }
    LILYGO_LOG_D("Deep-sleep EXT1 configured GPIO mask: 0x%llX",
                 static_cast<unsigned long long>(wakeup_pin));

    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        delay(10);
        const int final_pmu_int_level = rtc_gpio_get_level((gpio_num_t)PMU_INT);
        LILYGO_LOG_D("AXP2101 PMU_INT level at deep-sleep entry: %d",
                     final_pmu_int_level);
        if (final_pmu_int_level == LOW) {
            LILYGO_LOG_E("AXP2101 interrupt line became low at deep-sleep entry.");
            ESP.restart();
            return;
        }
    }

    esp_deep_sleep_start();
}


void LilyGoWatch2022::sleepDisplay()
{
    // LilyGoDispSPI::sleep();
}

void LilyGoWatch2022::wakeupDisplay()
{
    // LilyGoDispSPI::wakeup();
}

uint16_t LilyGoWatch2022::width()
{
    return DISP_WIDTH;
}

uint16_t LilyGoWatch2022::height()
{
    return DISP_HEIGHT;
}

uint8_t LilyGoWatch2022::getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point )
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

void LilyGoWatch2022::setRotation(uint8_t rotation)
{
    LilyGoDispSPI::setRotation(rotation);
}

uint8_t LilyGoWatch2022::getRotation()
{
    return  LilyGoDispSPI::getRotation();
}

bool LilyGoWatch2022::needSwapColors()
{
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    return true;
#else
    return false;
#endif
}

void LilyGoWatch2022::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispSPI::pushColors( x1,  y1,  x2,  y2, color);
}


void LilyGoWatch2022::setHapticEffects(uint8_t effects)
{
    if (effects > 127)effects = 127;
    _effects = effects;
}

uint8_t LilyGoWatch2022::getHapticEffects()
{
    return _effects;
}

void LilyGoWatch2022::vibrator()
{
    if (devices_probe & HW_DRV_ONLINE) {
        drv.setWaveform(0, _effects);
        drv.setWaveform(1, 0);
        drv.run();
    }
}

bool LilyGoWatch2022::initLoRa()
{
    int state = radio.begin();
    if (state == RADIOLIB_ERR_NONE) {
        devices_probe |= HW_RADIO_ONLINE;
        LILYGO_LOG_I("✅Radio init succeeded, module: %s", USING_RADIO_NAME);
        return true;
    }
    devices_probe &= ~HW_RADIO_ONLINE;
    LILYGO_LOG_E("❌Radio init failed, code :%d , Use %s", state, USING_RADIO_NAME);
    return false;
}


void LilyGoWatch2022::loop()
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

    // Keep the GNSS parser fed after the asynchronous probe completes.
    if (gps.probeDone() && !gps.probeInProgress() && gps.probeSuccess()) {
        while (Serial1.available()) {
            gps.encode((char)Serial1.read());
        }
    }
}

bool LilyGoWatch2022::lockSPI(TickType_t xTicksToWait)
{
    return true;
}

void LilyGoWatch2022::unlockSPI()
{
}

bool LilyGoWatch2022::shutdown()
{
    pmic.shutdown();
    return true;
}

uint16_t LilyGoWatch2022::getChargeLevelToCurrentImpl(uint8_t level)
{
    static const uint16_t table[] = {
        0, 100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000
    };
    if (level < 14)
        return table[level];
    return 0;
}

uint16_t LilyGoWatch2022::getChargeCurrentToLevelImpl()
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

namespace
{
LilyGoWatch2022 &getInstanceRef()
{
    return *LilyGoWatch2022::getInstance();
}
}

LilyGoWatch2022 &instance = getInstanceRef();

#endif
