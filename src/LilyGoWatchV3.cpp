/**
 * @file      LilyGoWatchV3.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-05
 *
 */
#ifdef ARDUINO_TWATCH_2020_V3

#include "LilyGoLog.h"
#include "LilyGoWatchV3.h"
#include "SensorWireHelper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "LilyGoLib.h"
#include "core/LilyGoGeneral.h"
#include "driver/rtc_io.h"

extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);

EventGroupHandle_t LilyGoWatchV3::_event;
static TimerHandle_t timerHandler = NULL;


static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

LilyGoWatchV3::LilyGoWatchV3() : LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispSPI(DISP_WIDTH, DISP_HEIGHT),
    LilyGoPowerManageInf(pmic, PMIC_TYPE_AXP202),
    BMASensorHelper(this),
    _audioOutput(I2S_BCLK, I2S_WCLK, I2S_DOUT),
    _audioInput(MIC_SCK, MIC_DAT),
    _effects(80), devices_probe(0),
    _boot_images_addr(NULL)
{
}

LilyGoWatchV3::~LilyGoWatchV3()
{
}

void LilyGoWatchV3::clearEventBits(const EventBits_t uxBitsToClear)
{
    xEventGroupClearBits(_event, uxBitsToClear);
}

void LilyGoWatchV3::setEventBits(const EventBits_t uxBitsToSet)
{
    xEventGroupSetBits(_event, uxBitsToSet);
}

const char *LilyGoWatchV3::getName()
{
    return "LilyGo T-Watch-V3(2020)";
}

const LilyGoDeviceCapability &LilyGoWatchV3::getCapability() const
{
    static const LilyGoDeviceCapability capability = {
        /* boardName */       "LilyGo T-Watch-V3(2020)",
        /* radioName */       "None",
        /* pmicName */        "AXP202",
        /* gaugeName */       "PMU internal",
        /* hasSd */           false,
        /* hasGps */          false,
        /* gpsRuntimeProbe */ false,
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
        /* pmicType */        PMIC_TYPE_AXP202,
        /* hasButton */       false,
        /* hasPmuButton */    true,
    };
    return capability;
}

LilyGoDeviceInitOptions LilyGoWatchV3::getDefaultInitOptions() const
{
    LilyGoDeviceInitOptions options = lilygo_init_options_from_capability(getCapability());
    options.initFatfs = true;
    options.initRtc = true;
    options.initCodec = false;
    return options;
}

bool LilyGoWatchV3::hasTouch()
{
    return  devices_probe & HW_TOUCH_ONLINE;
}

uint32_t LilyGoWatchV3::getDeviceProbe()
{
    return devices_probe;
}

void LilyGoWatchV3::setBootImage(uint8_t *image)
{
    _boot_images_addr = image;
}

uint32_t LilyGoWatchV3::begin()
{
    return begin(getDefaultInitOptions());
}

uint32_t LilyGoWatchV3::begin(uint32_t disable_hw_init)
{
    return begin(lilygo_init_options_from_disable_mask(getDefaultInitOptions(), disable_hw_init));
}

uint32_t LilyGoWatchV3::begin(const LilyGoDeviceInitOptions &init_options)
{
    if (_event) {
        return devices_probe;
    }

    _event = xEventGroupCreate();

    while (!psramFound()) {
        LILYGO_LOG_E("ERROR:PSRAM NOT FOUND!");
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;

    if (init_options.initFatfs) {
        setupMSC(_lock_callback, _unlock_callback);
    }

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
            devices_probe |= HW_PMU_ONLINE;
            LILYGO_LOG_I("Initializing PMU succeeded");
        }
    }

    LilyGoDispSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 40);

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

    if (init_options.initAudio) {
        initAmplifier();
    }

    if (init_options.initAudio) {
        initMicrophone();
    }

    setRotation(0);

    return true;
}

bool LilyGoWatchV3::initTouch()
{
    LILYGO_LOG_D("Init Touch");
    touch.setPins(TP_RST, TP_INT);
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

bool LilyGoWatchV3::initSensor()
{
    LILYGO_LOG_D("Init BMA Sensor");
    Wire.setClock(1000000UL);

    if (beginSensor(Wire, BMA4XX_I2C_ADDR_SDO_HIGH, SensorRemap::BOTTOM_LAYER_BOTTOM_LEFT_CORNER)) {
        devices_probe |= HW_BMA_ONLINE;
    }

    Wire.setClock(400000UL);

    pinMode(SENSOR_INT, INPUT);
    attachInterrupt(SENSOR_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_SENSOR);
    }, RISING);

    return true;
}

bool LilyGoWatchV3::initRTC()
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

bool LilyGoWatchV3::initMicrophone()
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

bool LilyGoWatchV3::initAmplifier()
{
    LILYGO_LOG_V("Init Audio Amplifier");
    bool res = false;
    res = _audioOutput.begin();

    _audioOutput.setMuteCallback([](bool en) {
        instance.powerControl(POWER_SPEAK, en);
    });

    if (res) {
        LILYGO_LOG_I("Audio Amplifier init succeeded");
    } else {
        LILYGO_LOG_E("Warning: Failed to init Audio Amplifier");
    }
    return res;
}

bool LilyGoWatchV3::getTouched()
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_TOUCHPAD) {
        // xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);  //No clear
        return true;
    }
    return false;
}

uint8_t LilyGoWatchV3::getBrightness()
{
    return LilyGoDispSPI::_brightness;
}

void LilyGoWatchV3::setBrightness(uint8_t level)
{
    LilyGoDispSPI::setBrightness(level);
}

bool LilyGoWatchV3::initPMU()
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

    // Amp voltage
    pmic.getChannel()->setVoltage(AXP202Channel::CH_LDO4, 3300);
    pmic.getChannel()->enable(AXP202Channel::CH_LDO4, false);

    // UNUSED POWER CHANNEL
    pmic.getChannel()->enable(AXP202Channel::CH_DCDC2, false);
    pmic.getChannel()->enable(AXP202Channel::CH_LDO3, false);
    pmic.getChannel()->enable(AXP202Channel::CH_LDOio, false);


    pmic.pwron().setOnDurationMs(4000);
    pmic.pwron().setOffDurationMs(128);

    pmic.adc().enableChannels(
        PmicAdcBase::Channel::VBUS_VOLTAGE |
        PmicAdcBase::Channel::VBUS_CURRENT |
        PmicAdcBase::Channel::VSYS_VOLTAGE |
        PmicAdcBase::Channel::BAT_VOLTAGE |
        PmicAdcBase::Channel::BAT_CURRENT
    );

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


void LilyGoWatchV3::checkPowerStatus()
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

void LilyGoWatchV3::powerControl(PowerCtrlChannel_t ch, bool enable)
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
    case POWER_SPEAK:
        if (enable) {
            pmic.getChannel()->enable(AXP202Channel::CH_LDO4, true);
        } else {
            pmic.getChannel()->enable(AXP202Channel::CH_LDO4, false);
        }
        break;
    default:
        break;
    }
}

uint64_t LilyGoWatchV3::checkWakeupPins(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = 0;
    if (wakeup_src & WAKEUP_SRC_TOUCH_PANEL) {
        wakeup_pin |=  (1ULL << (TP_INT));
    }
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        wakeup_pin |=  (1ULL << (PMU_INT));
    }
    if (wakeup_pin == 0) {
        LILYGO_LOG_E("No wake-up method is set. T-Watch V3 supports WAKEUP_SRC_POWER_KEY and WAKEUP_SRC_TOUCH_PANEL.");
    }
    return wakeup_pin;
}

void LilyGoWatchV3::lightSleep(WakeupSource_t wakeup_src)
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

    powerControl(POWER_DISPLAY_BACKLIGHT, false);
    powerControl(POWER_SPEAK, false);

    sleepDisplay();

    pmic.getIrq()->disable(AXP202Irq::IRQ_ALL_MASK);
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        pmic.getIrq()->enable(AXP202Irq::IRQ_PKEY_NEGATIVE |
                              AXP202Irq::IRQ_PEKEY_SHORT_PRESS);
    }
    // Keep this as the final PMIC access before sleeping. The IRQ output is
    // active low and a pending status would wake the ESP immediately.
    pmic.getIrq()->clearStatus();

    err = esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    if (err != ESP_OK) {
        LILYGO_LOG_E("Failed to enter light sleep: %s", esp_err_to_name(err));
    }

    wakeupDisplay();

    powerControl(POWER_DISPLAY_BACKLIGHT, true);

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

void LilyGoWatchV3::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
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
        LILYGO_LOG_E("Failed to configure T-Watch V3 deep-sleep wakeup: %s",
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

    if (!keep_touch_power) {
        touch.sleep();
    }
    // Display Backlight
    pmic.getChannel()->enable(AXP202Channel::CH_LDO2, false);

    int i = 3;
    while (i--) {
        LILYGO_LOG_PRINTF("%d second sleep ...\n", i);
        delay(500);
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


void LilyGoWatchV3::sleepDisplay()
{
    // LilyGoDispSPI::sleep();
}

void LilyGoWatchV3::wakeupDisplay()
{
    // LilyGoDispSPI::wakeup();
}

uint16_t LilyGoWatchV3::width()
{
    return DISP_WIDTH;
}

uint16_t LilyGoWatchV3::height()
{
    return DISP_HEIGHT;
}

uint8_t LilyGoWatchV3::getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point)
{
    EventBits_t bits = xEventGroupGetBits(_event);
    if (bits & HW_IRQ_TOUCHPAD) {

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

void LilyGoWatchV3::setRotation(uint8_t rotation)
{
    LilyGoDispSPI::setRotation(rotation);
}

uint8_t LilyGoWatchV3::getRotation()
{
    return  LilyGoDispSPI::getRotation();
}

bool LilyGoWatchV3::needSwapColors()
{
    return false;
}

void LilyGoWatchV3::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispSPI::pushColors( x1,  y1,  x2,  y2, color);
}

void LilyGoWatchV3::setHapticEffects(uint8_t effects)
{
    if (effects > 127)effects = 127;
    _effects = effects;
}

uint8_t LilyGoWatchV3::getHapticEffects()
{
    return _effects;
}

void LilyGoWatchV3::vibrator()
{
    analogWrite(MOTOR_PIN, 70);
    delay(30);
    analogWrite(MOTOR_PIN, 0);
}

void LilyGoWatchV3::loop()
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
}

bool LilyGoWatchV3::lockSPI(TickType_t xTicksToWait)
{
    return true;
}

void LilyGoWatchV3::unlockSPI()
{

}

bool LilyGoWatchV3::shutdown()
{
    pmic.shutdown();
    return true;
}

namespace
{
LilyGoWatchV3 &getInstanceRef()
{
    return *LilyGoWatchV3::getInstance();
}
}

LilyGoWatchV3 &instance = getInstanceRef();

#endif

