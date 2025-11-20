/**
 * @file      LilyGoWatch2022.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 */
#ifdef ARDUINO_T_WATCH_S3

#include "LilyGoWatchS3.h"
#include "SensorWireHelper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "LilyGoLib.h"
#include "driver/rtc_io.h"

extern void setGroupBitsFromISR(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToSet);
extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);

EventGroupHandle_t LilyGoWatch2022::_event;
static TimerHandle_t timerHandler = NULL;

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

static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

LilyGoWatch2022::LilyGoWatch2022() : LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispSPI(DISP_WIDTH, DISP_HEIGHT),
    LilyGoPowerManage(&pmu),
    _effects(80), devices_probe(0),
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
    return "LilyGo T-Watch S3(2022)";
}

bool LilyGoWatch2022::hasTouch()
{
    return  devices_probe & HW_TOUCH_ONLINE;
}

uint32_t LilyGoWatch2022::getDeviceProbe()
{
    return devices_probe;
}

void LilyGoWatch2022::setBootImage(uint8_t *image)
{
    _boot_images_addr = image;
}

uint32_t LilyGoWatch2022::begin(uint32_t disable_hw_init)
{
    bool res;

    if (_event) {
        return devices_probe;
    }

    _event = xEventGroupCreate();

    while (!psramFound()) {
        Serial.println("ERROR:PSRAM NOT FOUND!");
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;


    if (!(disable_hw_init & NO_INIT_FATFS)) {
        setupMSC(_lock_callback, _unlock_callback);
    }

    Wire.begin(SDA, SCL);
    Wire1.begin(TP_SDA, TP_SCL);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

    log_d("Init PMU");
    if (!initPMU()) {
        log_e("Failed to find PMU!");
        assert(0);
    } else {
        devices_probe |= HW_PMU_ONLINE;
        log_i("Initializing PMU succeeded");
    }

    LilyGoDispSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 80);

    if (_boot_images_addr) {
        uint16_t w = this->width();
        uint16_t h = this->height();
        this->pushColors(0, 0, w, h, (uint16_t *)_boot_images_addr);
        incrementalBrightness(250, 20);
    }

    if (!(disable_hw_init & NO_SCAN_I2C_DEV)) {
        SensorWireHelper::dumpDevices(Wire);
        SensorWireHelper::dumpDevices(Wire1);
    }


    if (!(disable_hw_init & NO_HW_TOUCH)) {
        initTouch();
    }

    if (!(disable_hw_init & NO_HW_SENSOR)) {
        initSensor();
    }

    if (!(disable_hw_init & NO_HW_RTC)) {
        initRTC();
    }

    if (!(disable_hw_init & NO_HW_DRV)) {
        initDrv();
    }

    if (!(disable_hw_init & NO_HW_GPS)) {
        initGPS();
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

    if (!(disable_hw_init & NO_HW_LORA)) {

#if    defined(ARDUINO_LILYGO_LORA_SX1262)
        log_d("Radio select  SX1262");
#elif  defined(ARDUINO_LILYGO_LORA_SX1280)
        log_d("Radio select  SX1280");
#elif  defined(ARDUINO_LILYGO_LORA_CC1101)
        log_d("Radio select  CC1101");
#elif  defined(ARDUINO_LILYGO_LORA_LR1121)
        log_d("Radio select  LR1121");
#elif  defined(ARDUINO_LILYGO_LORA_SI4432)
        log_d("Radio select  SI4432");
#else
        log_d("Radio select  None");
#endif

        int state = radio.begin();
        if (state == RADIOLIB_ERR_NONE) {
            devices_probe |= HW_RADIO_ONLINE;
        } else {
            log_e("Radio init failed, code :%d", state);
        }
    }

    setRotation(0);

    return true;
}


bool LilyGoWatch2022::initDrv()
{
    log_d("Init DRV2605");
    bool res = drv.begin(Wire);
    if (!res) {
        log_e("Failed to find DRV2605!");
    } else {
        log_i("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(SensorDRV2605::MODE_INTTRIG);
        drv.useERM();
        drv.setWaveform(0, _effects);  // play effect
        drv.setWaveform(1, 0);  // end waveform
        drv.run();
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}
bool LilyGoWatch2022::initGPS()
{
    log_d("Init GPS");
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    bool res = gps.init(&Serial1);
    if (res) {
        log_i("UBlox GPS init succeeded, using UBlox GPS Module");
        devices_probe |= HW_GPS_ONLINE;
    } else {
        log_e("Warning: Failed to find UBlox GPS Module");
        // if not detect gps , turn off dc3
        pmu.disableDC3();
    }
    return res;
}

bool LilyGoWatch2022::initTouch()
{
    log_d("Init Touch");
    touch.setPins(-1, TP_INT);
    bool res = touch.begin(Wire1, FT6X36_SLAVE_ADDRESS, TP_SDA, TP_SCL);
    if (!res) {
        log_e("Failed to find FT6X36!");
    } else {
        log_i("Initializing FT6X36 succeeded");
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
    log_d("Init BMA423");
    Wire.setClock(1000000UL);
    bool res = sensor.begin(Wire);
    if (!res) {
        log_e("Failed to find BMA423!");
    } else {
        log_i("Initializing BMA423 succeeded");
        sensor.setRemapAxes(SensorBMA423::REMAP_BOTTOM_LAYER_TOP_RIGHT_CORNER);
        sensor.setStepCounterWatermark(1);
        devices_probe |= HW_SENSOR_ONLINE;
    }
    Wire.setClock(400000UL);

    sensor.configAccelerometer();

    sensor.configInterrupt();

    sensor.enableFeature(SensorBMA423::FEATURE_STEP_CNTR |
                         SensorBMA423::FEATURE_ANY_MOTION |
                         SensorBMA423::FEATURE_NO_MOTION |
                         SensorBMA423::FEATURE_ACTIVITY |
                         SensorBMA423::FEATURE_TILT |
                         SensorBMA423::FEATURE_WAKEUP,
                         true);


    sensor.enablePedometerIRQ();
    sensor.enableTiltIRQ();
    sensor.enableWakeupIRQ();
    sensor.enableAnyNoMotionIRQ();
    sensor.enableActivityIRQ();

    pinMode(SENSOR_INT, INPUT_PULLDOWN);
    attachInterrupt(SENSOR_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_SENSOR);
    }, RISING);

    return res;
}

void LilyGoWatch2022::checkSensorStatus()
{
    static SensorEventType_t event;
    uint16_t status = sensor.readIrqStatus();
    if (sensor.isPedometer()) {
        uint32_t stepCounter = sensor.getPedometerCounter();
        log_d("Step count interrupt,step Counter:%u\n", stepCounter);
        event = SENSOR_STEPS_UPDATED;
        sendEvent(SENSOR_EVENT, &event);
    }
    if (sensor.isActivity()) {
        log_d("Activity interrupt");
        event = SENSOR_ACTIVITY_DETECTED;
        sendEvent(SENSOR_EVENT, &event);
    }
    if (sensor.isTilt()) {
        log_d("Tilt interrupt");
        event = SENSOR_TILT_DETECTED;
        sendEvent(SENSOR_EVENT, &event);
    }
    if (sensor.isDoubleTap()) {
        log_d("DoubleTap interrupt");
        event = SENSOR_DOUBLE_TAP_DETECTED;
        sendEvent(SENSOR_EVENT, &event);
    }
    if (sensor.isAnyNoMotion()) {
        log_d("Any motion / no motion interrupt");
        event = SENSOR_ANY_MOTION_DETECTED;
        sendEvent(SENSOR_EVENT, &event);
    }
}


bool LilyGoWatch2022::initRTC()
{
    log_d("Init PCF8563 RTC");
    bool res = rtc.begin(Wire);
    if (!res) {
        log_e("Failed to find PCF8563!");
    } else {
        log_i("Initializing PCF8563 succeeded");
        rtc.setClockOutput(SensorPCF8563::CLK_DISABLE);   //Disable clock output ， Conserve Backup Battery Current Consumption
        rtc.hwClockRead();  //Synchronize RTC clock to system clock
        devices_probe |= HW_RTC_ONLINE;
    }
    return res;
}

bool LilyGoWatch2022::initMicrophone()
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

bool LilyGoWatch2022::initAmplifier()
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

void LilyGoWatch2022::decrementBrightness(uint8_t target_level, uint32_t delay_ms, bool async)
{
    if (!async) {
        uint8_t brightness = getBrightness();
        if (target_level > brightness)
            return;
        for (int i = brightness; i >= target_level; i--) {
            setBrightness(i);
            delay(delay_ms);
        }
        return;
    }

    static uint8_t pvTimerParams;
    pvTimerParams = target_level;
    if (!timerHandler) {
        timerHandler = xTimerCreate("bri", pdMS_TO_TICKS(delay_ms), pdTRUE, &pvTimerParams, [](TimerHandle_t xTimer) {
            uint8_t *target_level = (uint8_t *) pvTimerGetTimerID( xTimer );
            uint8_t brightness = instance.getBrightness();
            brightness--;
            instance.setBrightness(brightness);
            if (brightness <= *target_level ) {
                xTimerStop(timerHandler, portMAX_DELAY);
                xTimerDelete(timerHandler, portMAX_DELAY);
                timerHandler = NULL;
            }
        });
    }
    if (xTimerIsTimerActive(timerHandler) == pdTRUE) {
        return;
    }
    xTimerStart(timerHandler, portMAX_DELAY);

}

void LilyGoWatch2022::incrementalBrightness(uint8_t target_level, uint32_t delay_ms, bool async)
{
    if (!async) {
        uint8_t brightness = getBrightness();
        if (target_level < brightness)
            return;
        for (int i = brightness; i < target_level; i++) {
            setBrightness(i);
            delay(delay_ms);
        }
        return;
    }
    static uint8_t pvTimerParams;
    pvTimerParams = target_level;
    if (!timerHandler) {
        timerHandler = xTimerCreate("bri", pdMS_TO_TICKS(delay_ms), pdTRUE, &pvTimerParams, [](TimerHandle_t xTimer) {
            uint8_t *target_level = (uint8_t *) pvTimerGetTimerID( xTimer );
            uint8_t brightness = instance.getBrightness();
            brightness++;
            instance.setBrightness(brightness);
            if (brightness >= *target_level ) {
                xTimerStop(timerHandler, portMAX_DELAY);
                xTimerDelete(timerHandler, portMAX_DELAY);
                timerHandler = NULL;
            }
        });
    }
    if (xTimerIsTimerActive(timerHandler) == pdTRUE) {
        return;
    }
    xTimerStart(timerHandler, portMAX_DELAY);
}

/*
* This power distribution is really bad.
*****************************************************************************************
* | CHIP       | AXP2101                                | Peripherals        | SchName  |
* | ---------- | -------------------------------------- | ------------------ | -------- |
* | DC1        | 3.3V                            /2A    | ESP32-S3           | VDD3V3   |
* | DC2        | 0.5-1.2V,1.22-1.54V             /2A    | Unused             | X        |
* | DC3        | 0.5-1.2V,1.22-1.54V,1.6-3.4V    /2A    | GPS                | VCC_2_5V |
* | DC4        | 0.5-1.2V,1.22-1.84V             /1.5A  | Unused             | X        |
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
    bool res =  pmu.init(Wire);
    if (!res) {
        return false;
    }

    // Set the minimum common working voltage of the PMU VBUS input,
    // below this value will turn off the PMU
    pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);

    // Set the maximum current of the PMU VBUS input,
    // higher than this value will turn off the PMU
    pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);

    // Set VSY off voltage as 2600mV , Adjustment range 2600mV ~ 3300mV
    pmu.setSysPowerDownVoltage(2600);

    pmu.setALDO2Voltage(3300);  // Display backlight
    pmu.setALDO3Voltage(3300);  // Display and Touch
    pmu.setALDO4Voltage(3300);  // Radio
    pmu.setBLDO2Voltage(3300);  // Drv2605 enable pin
    pmu.setBLDO1Voltage(3300);  // GPS , (The version with BOOT button and RST on the back cover)
    pmu.setDC3Voltage(3300);    // GPS , Earlier versions use DC3 (without BOOT button and RST)
    pmu.setButtonBatteryChargeVoltage(3300);    // RTC backup battery

    // UNUSED POWER CHANNEL
    pmu.disableDC2();
    pmu.disableDC4();
    pmu.disableDC5();
    pmu.disableALDO1();
    // pmu.disableBLDO1();
    pmu.disableCPUSLDO();
    pmu.disableDLDO1();
    pmu.disableDLDO2();

    pmu.enableALDO2();  // Display backlight
    pmu.enableALDO3();  // Display and Touch
    pmu.enableALDO4();  // Radio
    pmu.enableBLDO2();  // Drv2605 enable pin
    pmu.enableDC3();    // GPS , Earlier versions use DC3 (without BOOT button and RST)
    pmu.enableBLDO1();  // GPS , (The version with BOOT button and RST on the back cover)
    pmu.enableButtonBatteryCharge();    // RTC backup battery


    // Set the time of pressing the button to turn off
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);

    // Set the button power-on press time
    pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);

    // Enable internal ADC detection
    pmu.enableBattDetection();
    pmu.enableVbusVoltageMeasure();
    pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.enableTemperatureMeasure();

    //t-watch no chg led
    pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

    // Enable the required interrupt function
    pmu.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ    | XPOWERS_AXP2101_BAT_REMOVE_IRQ      |   // BATTERY
        XPOWERS_AXP2101_VBUS_INSERT_IRQ   | XPOWERS_AXP2101_VBUS_REMOVE_IRQ     |   // VBUS
        XPOWERS_AXP2101_PKEY_SHORT_IRQ    | XPOWERS_AXP2101_PKEY_LONG_IRQ       |   // POWER KEY
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ  | XPOWERS_AXP2101_BAT_CHG_START_IRQ       // CHARGE
    );

    // Clear all interrupt flags
    pmu.clearIrqStatus();

    // Set the precharge charging current
    pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    // It is recommended to charge at less than 130mA
    pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_125MA);
    // Set stop charging termination current
    pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    // T-Watch-S3 uses a high-voltage(4.35V) battery by default.
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V35);

    // Register PMU interrupt management
    pinMode(PMU_INT, INPUT_PULLUP);
    attachInterrupt(PMU_INT, []() {
        setGroupBitsFromISR(_event, HW_IRQ_POWER);
    }, FALLING);

    return true;
}


void LilyGoWatch2022::checkPowerStatus()
{
    static PMUEventType_t event;

    event = PMU_EVENT_NONE;

    bool batteryInsert = pmu.isBatteryConnect();
    // Get PMU Interrupt Status Register
    pmu.getIrqStatus();

    if (pmu.isDropWarningLevel2Irq()) {
        log_d("isDropWarningLevel2");
        event = PMU_EVENT_LOW_VOLTAGE_LEVEL2;
    }
    if (pmu.isDropWarningLevel1Irq()) {
        log_d("isDropWarningLevel1");
        event = PMU_EVENT_LOW_VOLTAGE_LEVEL1;
    }
    if (pmu.isGaugeWdtTimeoutIrq()) {
        log_d("isWdtTimeout");
    }
    if (pmu.isBatChargerOverTemperatureIrq()) {
        log_d("isBatChargeOverTemperature");
        event = PMU_EVENT_CHARGE_HIGH_TEMP;
    }
    if (pmu.isBatWorkOverTemperatureIrq()) {
        log_d("isBatWorkOverTemperature");
    }
    if (pmu.isBatWorkUnderTemperatureIrq()) {
        log_d("isBatWorkUnderTemperature");
    }
    if (pmu.isVbusInsertIrq()) {
        log_d("isVbusInsert");
        event = PMU_EVENT_USBC_INSERT;
    }
    if (pmu.isVbusRemoveIrq()) {
        log_d("isVbusRemove");
        event = PMU_EVENT_USBC_REMOVE;
    }
    if (pmu.isBatInsertIrq()) {
        log_d("isBatInsert");
        event = PMU_EVENT_BATTERY_INSERT;
    }
    if (pmu.isBatRemoveIrq()) {
        log_d("isBatRemove");
        event = PMU_EVENT_BATTERY_REMOVE;
    }
    if (pmu.isPekeyShortPressIrq()) {
        log_d("isPekeyShortPress");
        event = PMU_EVENT_KEY_CLICKED;
    }
    if (pmu.isPekeyLongPressIrq()) {
        log_d("isPekeyLongPress");
        event = PMU_EVENT_KEY_LONG_PRESSED;
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
            event = PMU_EVENT_CHARGE_FINISH;
        }
        if (pmu.isBatChargeStartIrq()) {
            log_d("isBatChargeStart");
            event = PMU_EVENT_CHARGE_STARTED;
        }
    }

    if (pmu.isBatDieOverTemperatureIrq()) {
        log_d("isBatDieOverTemperature");
    }
    if (pmu.isChargeOverTimeoutIrq()) {
        log_d("isChargeOverTimeout");
        event = PMU_EVENT_CHARGE_TIMEOUT;
    }
    if (pmu.isBatOverVoltageIrq()) {
        log_d("isBatOverVoltage");
        event = PMU_EVENT_BATTERY_OVER_VOLTAGE;
    }
    // Clear PMU Interrupt Status Register
    pmu.clearIrqStatus();

    if (event != PMU_EVENT_NONE) {
        sendEvent(POWER_EVENT, &event);
    }
}

void LilyGoWatch2022::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    log_d("Power channel:%u set %s", ch, enable ? " ON" : " OFF");
    switch (ch) {
    case POWER_DISPLAY_BACKLIGHT:
        enable ? pmu.enableALDO2() : pmu.disableALDO2();
        break;
    case POWER_DISPLAY:
        enable ? pmu.enableALDO3() : pmu.disableALDO3();
        break;
    case POWER_RADIO:
        enable ? pmu.enableALDO4() : pmu.disableALDO4();
        break;
    case POWER_HAPTIC_DRIVER:
        enable ? pmu.enableBLDO2() : pmu.disableBLDO2();
        break;
    case POWER_GPS:
        if (enable) {
            pmu.enableBLDO1();
            Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
        } else {
            pmu.disableBLDO1();
            Serial1.end();
            gpio_reset_pin((gpio_num_t )GPS_RX);
            pinMode(GPS_RX, OPEN_DRAIN);
            gpio_reset_pin((gpio_num_t )GPS_TX);
            pinMode(GPS_TX, OPEN_DRAIN);
        }
        break;
    case POWER_SPEAK:
        // TODO:
        enable ? pmu.enableDLDO1() : pmu.disableDLDO1();
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
    if (wakeup_src & WAKEUP_SRC_SENSOR) {
        wakeup_pin |=  _BV(SENSOR_INT);
    }
    if (wakeup_pin == 0) {
        log_e("No wake-up method is set. T-WatchS3 allows setting WAKEUP_SRC_POWER_KEY and WAKEUP_SRC_TOUCH_PANEL as wake-up methods.");
    }
    return wakeup_pin;
}



void LilyGoWatch2022::lightSleep(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = checkWakeupPins(wakeup_src);
    if (wakeup_pin == 0) {
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

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    if (wakeup_src & WAKEUP_SRC_POWER_KEY) {
        rtc_gpio_pullup_en((gpio_num_t)PMU_INT);
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    }


#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
    esp_sleep_enable_ext1_wakeup_io((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_sleep_enable_ext1_wakeup((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#endif

    esp_light_sleep_start();

    radio.standby();

    wakeupDisplay();

    powerControl(POWER_DISPLAY_BACKLIGHT, true);
    powerControl(POWER_HAPTIC_DRIVER, true);
    powerControl(POWER_GPS, true);
    powerControl(POWER_NFC, true);

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                  XPOWERS_AXP2101_PKEY_LONG_IRQ |
                  XPOWERS_AXP2101_VBUS_INSERT_IRQ |
                  XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_START_IRQ |
                  XPOWERS_AXP2101_BAT_CHG_DONE_IRQ
                 );
    pmu.clearIrqStatus();
}

void LilyGoWatch2022::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
{
    uint64_t wakeup_pin = 0;
    esp_sleep_ext1_wakeup_mode_t  wakeup_mode = ESP_EXT1_WAKEUP_ANY_LOW;
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
    }

    if (!(wakeup_src & WAKEUP_SRC_SENSOR)) {
        sensor.disableInterruptCtrl();
        sensor.disableAccelerometer();
        sensor.enablePowerSave();
    } else {
        wakeup_mode = ESP_EXT1_WAKEUP_ANY_HIGH;
    }

    LilyGoDispSPI::sleep();

    LilyGoDispSPI::end();

    // Turn off ADC data monitoring to save power
    pmu.disableTemperatureMeasure();
    // Disable internal ADC detection
    pmu.disableBattDetection();
    pmu.disableVbusVoltageMeasure();
    pmu.disableBattVoltageMeasure();
    pmu.disableSystemVoltageMeasure();

    // Enable PMU sleep
    pmu.enableSleep();

    // Do not turn off the screen power in deep sleep,
    // otherwise the current will increase abnormally by about 600uA.
    // The correct way is to keep the power supply and set the screen and touch to sleep.
    // At this time, the screen and touch consume a total of about 103.4uA (screen 100uA, touch 3.4uA)
    //! pmu.disableALDO2(); // Display

    if (!keep_touch_power) {
        touch.sleep();
        pmu.disableALDO3(); // Display and Touch
    }
    pmu.disableALDO2();     // Display Backlight
    pmu.disableALDO4();     // LoRa
    pmu.disableBLDO2();     // Drv2605 Enable
    pmu.disableDC3();       // GPS , Earlier versions use DC3 (without BOOT button and RST)
    pmu.disableBLDO1();     // GPS , (The version with BOOT button and RST on the back cover)

    // Turn off the RTC backup battery, adding about 200uA
    if (off_rtc_backup_domain) {
        pmu.disableButtonBatteryCharge();   // RTC Battery
    }

    pmu.clearIrqStatus();

    int i = 4;
    while (i--) {
        Serial.printf("%d second sleep ...\n", i);
        delay(1000);
    }

    Serial1.end();

    SPI.end();

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

    if (!(wakeup_src & WAKEUP_SRC_SENSOR)) {
        gpio_reset_pin((gpio_num_t )SENSOR_INT);
        pinMode(SENSOR_INT, OPEN_DRAIN);
    }

    if (wakeup_src & WAKEUP_SRC_TIMER) {
        esp_sleep_enable_timer_wakeup(sleep_second * 1000000UL);
    } else {
#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        esp_sleep_enable_ext1_wakeup_io((wakeup_pin), wakeup_mode);
#else
        esp_sleep_enable_ext1_wakeup((wakeup_pin), wakeup_mode);
#endif
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
        uint8_t tp = touch.getPoint(x_array, y_array, get_point);
        if (tp == 0) {
            xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);
        }
        log_d("TP:%d X:%d Y:%d", tp, x_array[0], y_array[0]);
        return tp;
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
        checkSensorStatus();
    }
}

bool LilyGoWatch2022::lockSPI(TickType_t xTicksToWait)
{
    return true;
}

void LilyGoWatch2022::unlockSPI()
{

}

LilyGoWatch2022 instance;

#endif
















