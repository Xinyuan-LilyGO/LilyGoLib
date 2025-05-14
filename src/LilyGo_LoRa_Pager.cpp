/**
 * @file      LilyGo_LoRa_Pager.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-17
 *
 */

#ifdef ARDUINO_T_LORA_PAGER
#include "LilyGo_LoRa_Pager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "LilyGoLib.h"
#include <SensorWireHelper.h>
#include "driver/rtc_io.h"

extern void esp_enable_slow_crystal();
extern void setGroupBitsFromISR(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToSet);

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

RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
RfalNfcClass NFCReader(&nfc_hw);

#define TASK_ROTARY_START_PRESSED_FLAG  _BV(0)

EventGroupHandle_t LilyGoLoRaPager::_event;
static TimerHandle_t timerHandler = NULL;
static QueueHandle_t rotaryMsg;
static TaskHandle_t  rotaryHandler = NULL;
static EventGroupHandle_t  rotaryTaskFlag = NULL;
static void rotaryTask(void *p);
extern void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb);


#ifndef RADIOLIB_EXCLUDE_NRF24
nRF24 nrf24 = new Module(44/*CS*/, 9/*IRQ*/, 43/*CE*/);
#endif

const CommandTable_t st7796_init_list[19] = {
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

static bool _lock_callback(void)
{
    return instance.lockSPI();
}

static bool _unlock_callback(void)
{
    instance.unlockSPI();
    return true;
}

LilyGoLoRaPager::LilyGoLoRaPager() : LilyGo_Display(SPI_DRIVER, false),
    LilyGoDispArduinoSPI(DISP_WIDTH, DISP_HEIGHT, st7796_init_list,
                         sizeof(st7796_init_list) / sizeof(st7796_init_list[0]))
{
    _effects = 80;
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

bool LilyGoLoRaPager::hasEncoder()
{
    return true;
}

bool LilyGoLoRaPager::hasKeyboard()
{
    return devices_probe & HW_KEYBOARD_ONLINE;
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

uint32_t LilyGoLoRaPager::begin(uint32_t disable_hw_init)
{
    bool res = false;

    if (_event) {
        return devices_probe;
    }

    _event = xEventGroupCreate();

    devices_probe = 0x00;

    while (!psramFound()) {
        log_d("ERROR:PSRAM NOT FOUND!"); delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;

    Wire.begin(SDA, SCL);

    SensorWireHelper::dumpDevices(Wire, Serial);

    if (!gauge.begin(Wire, SDA, SCL)) {
        log_e("Failed to find GAUGE.");
    } else {
        log_d("Initializing GAUGE succeeded");
        devices_probe |= HW_GAUGE_ONLINE;
        uint16_t newDesignCapacity = 1500;
        uint16_t newFullChargeCapacity = 1500;
        gauge.setNewCapacity(newDesignCapacity, newFullChargeCapacity);
    }

    res = initPMU();
    if (!res) {
        log_e("Failed to find PMU.");
    } else {
        log_d("Initializing PMU succeeded");
        devices_probe |= HW_PMU_ONLINE;
    }

#ifdef USING_XL9555_EXPANDS
    if (io.begin(Wire, 0x20)) {
        log_d("Initializing expand succeeded");
        devices_probe |= HW_EXPAND_ONLINE;
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
        log_d("Initializing expand Failed!");
    }
#endif /*USING_XL9555_EXPANDS*/

    //BHI260AP Address: 0x28
    if (!(disable_hw_init & NO_HW_SENSOR)) {
        if (initSensor()) {
#ifdef USING_BHI_EXPANDS
            sensor.digitalWrite(BHI_GPS_EN, HIGH);
            sensor.digitalWrite(BHI_LORA_EN, HIGH);
            sensor.digitalWrite(BHI_DRV_EN, HIGH);
            sensor.digitalWrite(BHI_KB_RST, HIGH);
            sensor.digitalWrite(BHI_NFC_EN, HIGH);
            sensor.digitalWrite(BHI_DISP_RST, LOW);
            delay(50);
            sensor.digitalWrite(BHI_DISP_RST, HIGH);
#endif /*USING_BHI_EXPANDS*/
        }
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

    if (!(disable_hw_init & NO_INIT_FATFS)) {
        setupMSC(_lock_callback, _unlock_callback);
    }

    esp_enable_slow_crystal();



    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

    initShareSPIPins();

    pinMode(NFC_INT, INPUT);

    if (!(disable_hw_init & NO_SCAN_I2C_DEV)) {
        SensorWireHelper::dumpDevices(Wire);
    }

    if (!(disable_hw_init & NO_HW_RTC)) {
        initRTC();
    }

    if (!(disable_hw_init & NO_HW_NFC)) {
        initNFC();
    }

    if (!(disable_hw_init & NO_HW_KEYBOARD)) {
        initKeyboard();
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
        int retry = 2;
        do {
            log_d("Init SD");
            res = installSD();
            if (!res) {
                log_e("Warning: Failed to find SD");
            } else {
                log_d("SD init succeeded.");
                devices_probe |= HW_SD_ONLINE;
                break;
            }
        } while (--retry);
    }


#ifdef USING_PDM_MICROPHONE
#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    if (!(disable_hw_init & NO_HW_MIC)) {
        log_d("Init Microphone");
        res = mic.init(MIC_SCK, MIC_DAT);
        if (res) {
            log_i("Microphone init succeeded");
        } else {
            log_e("Warning: Failed to find Microphone");
        }
    }
#endif
#endif /*USING_PDM_MICROPHONE*/

#ifdef USING_AUDIO_CODEC
    codec.setPins(I2S_MCLK, I2S_SCK, I2S_WS, I2S_SDOUT, I2S_SDIN);
    if (codec.begin(Wire, 0x18)) {
        devices_probe |= HW_CODEC_ONLINE;
        log_i("Codec init succeeded");
    } else {
        log_e("Warning: Failed to find Codec");
    }
    codec.setPaPinCallback([](bool enable, void *user_data) {
        ((ExtensionIOXL9555 *)user_data)->digitalWrite(EXPANDS_AMP_EN, enable);
    }, &io);
#endif /*USING_AUDIO_CODEC*/

    // Create message queue
    rotaryMsg = xQueueCreate(5, sizeof(RotaryMsg_t));

    rotaryTaskFlag = xEventGroupCreate();

    // Create a rotary encoder processing task
    xTaskCreate(rotaryTask, "rotary", 2 * 1024, NULL, 10, &rotaryHandler);

    return devices_probe;
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
    if (devices_probe & HW_KEYBOARD_ONLINE) {
        return kb.getKey(c);
    }
    return -1;
}

bool LilyGoLoRaPager::initPMU()
{
    bool res = ppm.init(Wire, SDA, SCL);
    if (!res) {
        return false;
    }
    return res;
}

/**
 * @brief   Hang on SD card
 * @retval Returns true if successful, otherwise false
 */
bool LilyGoLoRaPager::installSD()
{

#ifdef EXPANDS_SD_DET
    io.pinMode(EXPANDS_SD_DET, INPUT);
    if (io.digitalRead(EXPANDS_SD_DET)) {
        return false;
    }
#endif /*EXPANDS_SD_DET*/

    initShareSPIPins();
    // Set mount point to /fs
    if (!SD.begin(SD_CS, SPI, 4000000U, "/fs")) {
        log_e("Failed to detect SD Card!!");
        return false;
    }
    if (SD.cardType() != CARD_NONE) {
        log_d("SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
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

void LilyGoLoRaPager::decrementBrightness(uint8_t target_level, uint32_t delay_ms, bool async)
{
    if (target_level <= 0)target_level = 0;
    if (target_level > 16)target_level = 16;

    if (!async) {
        uint8_t brightness = getBrightness();
        if (target_level > brightness)
            return;
        for (int i = brightness; i >= target_level; i--) {
            setBrightness(i);
            delay(delay_ms);
        }
    } else {
        // NO BLOCK
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
}


void LilyGoLoRaPager::incrementalBrightness(uint8_t target_level, uint32_t delay_ms, bool async)
{
    if (target_level <= 0)target_level = 0;
    if (target_level > 16)target_level = 16;

    if (!async) {
        uint8_t brightness = getBrightness();
        if (target_level < brightness)
            return;
        for (int i = brightness; i < target_level; i++) {
            setBrightness(i);
            delay(delay_ms);
        }
    } else {

        // NO BLOCK
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
#if  defined(USING_BHI_EXPANDS)
        sensor.digitalWrite(BHI_LORA_EN, enable);
#elif defined(USING_XL9555_EXPANDS)
        io.digitalWrite(EXPANDS_LORA_EN, enable);
#endif
        break;
    case POWER_HAPTIC_DRIVER:
#if  defined(USING_BHI_EXPANDS)
        sensor.digitalWrite(BHI_DRV_EN, enable);
#elif defined(USING_XL9555_EXPANDS)
        io.digitalWrite(EXPANDS_DRV_EN, enable);
#endif
        break;
    case POWER_GPS:
#if  defined(USING_BHI_EXPANDS)
        sensor.digitalWrite(BHI_GPS_EN, enable);
#elif defined(USING_XL9555_EXPANDS)
        io.digitalWrite(EXPANDS_GPS_EN, enable);
#endif
        break;
    case POWER_NFC:
#if  defined(USING_BHI_EXPANDS)
        sensor.digitalWrite(BHI_NFC_EN, enable);
#elif defined(USING_XL9555_EXPANDS)
        io.digitalWrite(EXPANDS_NFC_EN, enable);
#endif
        break;
    case POWER_SD_CARD:
#if  defined(USING_XL9555_EXPANDS)
        io.digitalWrite(EXPANDS_SD_EN, enable);
#endif
        break;
    case POWER_SPEAK:
#if  defined(USING_BHI_EXPANDS)

#elif defined(USING_XL9555_EXPANDS)
        io.digitalWrite(EXPANDS_AMP_EN, enable);
#endif
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
    if (wakeup_src & WAKEUP_SRC_ROTARY_BUTTON) {
        wakeup_pin |=  _BV(ROTARY_C);
    }
    if (wakeup_src & WAKEUP_SRC_BOOT_BUTTON) {
        wakeup_pin |=  _BV(0);
    }
    if (wakeup_pin == 0) {
        log_e("No wake-up method is set. T-LoRa-Pager allows setting  WAKEUP_SRC_BOOT_BUTTON and WAKEUP_SRC_ROTARY_BUTTON as wake-up methods.");
    }
    return wakeup_pin;
}

void LilyGoLoRaPager::lightSleep(WakeupSource_t wakeup_src)
{
    uint64_t wakeup_pin = checkWakeupPins(wakeup_src);
    if (wakeup_pin == 0) {
        return;
    }

    ppm.disableMeasure();

    radio.sleep();

    kb.end();

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
    log_d("Disable GPS RST Pin");
    io.digitalWrite(EXPANDS_GPS_RST, LOW);
#endif


    gpio_reset_pin((gpio_num_t )GPS_RX);
    gpio_reset_pin((gpio_num_t )GPS_TX);
    gpio_reset_pin((gpio_num_t )GPS_PPS);
    pinMode(GPS_RX, OPEN_DRAIN);
    pinMode(GPS_RX, OPEN_DRAIN);

    sleepDisplay();

    pinMode(NFC_CS, OPEN_DRAIN);

    pinMode(0, INPUT);

    Serial.flush();
    delay(1000);

#if  ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
    esp_sleep_enable_ext1_wakeup_io((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_sleep_enable_ext1_wakeup((wakeup_pin), ESP_EXT1_WAKEUP_ANY_LOW);
#endif

    esp_light_sleep_start();

#ifdef EXPANDS_GPS_RST
    io.digitalWrite(EXPANDS_GPS_RST, HIGH);
#endif


    pinMode(NFC_CS, OUTPUT);
    digitalWrite(NFC_CS, HIGH);

    radio.standby();

    wakeupDisplay();

    powerControl(POWER_HAPTIC_DRIVER, true);
    powerControl(POWER_GPS, true);
    powerControl(POWER_NFC, true);
    powerControl(POWER_KEYBOARD, true);
    powerControl(POWER_SD_CARD, true);
    installSD();

    ppm.enableMeasure();

    initKeyboard();

    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
}

void LilyGoLoRaPager::sleep(WakeupSource_t wakeup_src, bool off_rtc_backup_domain, uint32_t sleep_second)
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

    vTaskDelete(rotaryHandler);

    ppm.disableMeasure();

    kb.end();

    backlight.setBrightness(0);

#if  defined(USING_BHI_EXPANDS)

    sensor.digitalWrite(BHI_GPS_EN, LOW);
    sensor.digitalWrite(BHI_LORA_EN, LOW);
    sensor.digitalWrite(BHI_DISP_RST, HIGH);
    sensor.digitalWrite(BHI_DRV_EN, LOW);
    sensor.digitalWrite(BHI_KB_RST, HIGH);
    sensor.digitalWrite(BHI_NFC_EN, LOW);

#elif defined(USING_XL9555_EXPANDS)

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

#endif

    drv.stop();

    sensor.reset();

    // Output all sensors info to Serial
    BoschSensorInfo info = sensor.getSensorInfo();
    info.printInfo(Serial);

    LilyGoDispArduinoSPI::sleep();

    LilyGoDispArduinoSPI::end();

    int i = 3;

    while (i--) {
        log_d("%d second sleep ...", i);
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

#if  defined(USING_PDM_MICROPHONE)
        MIC_SCK,
        MIC_DAT,
#endif

#if  defined(USING_PCM_AMPLIFIER)
        I2S_BCLK,
        I2S_WCLK,
        I2S_DOUT,
#endif

#if defined(USING_AUDIO_CODEC)
        I2S_WS,
        I2S_SCK,
        I2S_MCLK,
        I2S_SDIN,
        I2S_SDOUT,
#endif
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
        log_d("Set pin %d to open drain\n", pin);
        gpio_reset_pin((gpio_num_t )pin);
        pinMode(pin, OPEN_DRAIN);
    }
    Serial.flush();

    delay(200);

    Serial.end();

    delay(1000);

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

uint32_t LilyGoLoRaPager::getDeviceProbe()
{
    return devices_probe;
}

bool LilyGoLoRaPager::initNFC()
{
    bool res = false;
    log_d("Init NFC");
    res = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!res) {
        log_e("Failed to find NFC Reader");
    } else {
        log_d("Initializing NFC Reader succeeded");
        devices_probe |= HW_NFC_ONLINE;
    }
    return res;
}

bool LilyGoLoRaPager::initKeyboard()
{
    kb.setPins(KB_BACKLIGHT);
    bool res = kb.begin(Wire, KB_INT);
    if (!res) {
        log_e("Failed to find Keyboard");
    } else {
        log_d("Initializing Keyboard succeeded");
        devices_probe |= HW_KEYBOARD_ONLINE;
    }
    // kb.setBrightness(50);
    return res;
}

bool LilyGoLoRaPager::initDrv()
{
    bool res = false;
    //DRV2605 Address: 0x5A
    log_d("Init DRV2605 Haptic Driver");
    res = drv.begin(Wire);
    if (!res) {
        log_e("Failed to find DRV2605");
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
    if (devices_probe & HW_DRV_ONLINE) {
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
    if (devices_probe & HW_DRV_ONLINE) {
        drv.setWaveform(0, _effects);
        drv.setWaveform(1, 0);
        drv.run();
    }
}

bool LilyGoLoRaPager::initGPS()
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



#ifdef USING_XL9555_EXPANDS
#define BOSCH_BHI260_KLIO
#else
#define BOSCH_BHI260_GPIO
#endif
#include <BoschFirmware.h>


bool LilyGoLoRaPager::initSensor()
{
    bool res = false;
    Wire.setClock(1000000UL);
    log_d("Init BHI260AP Sensor");
    sensor.setPins(-1);
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    sensor.setBootFromFlash(false);
    res = sensor.begin(Wire);
    if (!res) {
        log_e("Failed to find BHI260AP");
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

bool LilyGoLoRaPager::initRTC()
{
    bool res = false;
    log_d("Init PCF85063 RTC");
    res = rtc.begin(Wire);
    if (!res) {
        log_e("Failed to find PCF85063");
    } else {
        devices_probe |= HW_RTC_ONLINE;
        log_d("Initializing PCF85063 succeeded");
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
        log_d("Initializing NRF2401 Extern Module succeeded");
        devices_probe |= HW_NRF24_ONLINE;
        return true;
    }
#endif
    log_e("Failed to find NRF2401 Extern Module");
    // io.digitalWrite(EXPANDS_GPIO_EN, LOW);
    return false;
}

void LilyGoLoRaPager::loop()
{
    EventBits_t bits = xEventGroupGetBits(_event);
    // if (bits & HW_IRQ_POWER) {
    //     xEventGroupClearBits(_event, HW_IRQ_POWER);
    // }

    if (bits & HW_IRQ_RTC) {
        xEventGroupClearBits(_event, HW_IRQ_RTC);
        sendEvent(RTC_EVENT_INTERRUPT);
    }

    if (bits & HW_IRQ_SENSOR) {
        xEventGroupClearBits(_event, HW_IRQ_SENSOR);
        sensor.update();
    }

    lockSPI();
    NFCReader.rfalNfcWorker();
    unlockSPI();
}

RotaryMsg_t LilyGoLoRaPager::getRotary()
{
    static RotaryMsg_t msg;
    if (xQueueReceive(rotaryMsg, &msg, pdMS_TO_TICKS(50)) == pdPASS) {
        return (msg);
    } else {
        msg.centerBtnPressed = false;
        msg.dir = ROTARY_DIR_NONE;
    }
    return (msg);
}

void LilyGoLoRaPager::clearRotaryMsg()
{
    UBaseType_t uxMessagesWaiting;
    uxMessagesWaiting = uxQueueMessagesWaiting(rotaryMsg);
    while (uxMessagesWaiting > 0) {
        RotaryMsg_t msg;;
        xQueueReceive(rotaryMsg, &msg, 0);
        uxMessagesWaiting = uxQueueMessagesWaiting(rotaryMsg);
    }
}

void LilyGoLoRaPager::disableRotary()
{
    if (rotaryHandler) {
        vTaskSuspend(rotaryHandler);
    }
}

void LilyGoLoRaPager::enableRotary()
{
    if (rotaryHandler) {
        if (digitalRead(ROTARY_C) == LOW) {
            xEventGroupSetBits(rotaryTaskFlag, TASK_ROTARY_START_PRESSED_FLAG);
        }
        vTaskResume(rotaryHandler);
    }
}

static bool getButtonState()
{
    static uint8_t buttonState;
    static uint8_t lastButtonState = HIGH;
    static uint32_t lastDebounceTime = 0;
    const uint8_t debounceDelay = 20;
    int reading = digitalRead(ROTARY_C);

    EventBits_t eventBits = xEventGroupGetBits(rotaryTaskFlag);
    if (eventBits & TASK_ROTARY_START_PRESSED_FLAG) {
        if (reading == HIGH) {
            xEventGroupClearBits(rotaryTaskFlag, TASK_ROTARY_START_PRESSED_FLAG);
        } else {
            return false;
        }
    }

    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }
    if (millis() - lastDebounceTime > debounceDelay) {
        if (reading != buttonState) {
            buttonState = reading;
            if (buttonState == LOW) {
                lastButtonState = reading;
                return true;
            }
        }
    }
    lastButtonState = reading;
    return false;
}

static void rotaryTask(void *p)
{
    RotaryMsg_t msg;
    bool last_btn_state = false;
    instance.rotary.begin();
    pinMode(ROTARY_C, INPUT);
    while (1) {
        msg.centerBtnPressed = getButtonState();
        uint8_t result = instance.rotary.process();
        if (result || msg.centerBtnPressed != last_btn_state) {
            switch (result) {
            case DIR_CW:
                msg.dir = ROTARY_DIR_UP;
                break;
            case DIR_CCW:
                msg.dir = ROTARY_DIR_DOWN;
                break;
            default:
                msg.dir = ROTARY_DIR_NONE;
                break;
            }
            last_btn_state = msg.centerBtnPressed;
            xQueueSend(rotaryMsg, (void *)&msg, portMAX_DELAY);
        }
        delay(2);
    }
    vTaskDelete(NULL);
}


LilyGoLoRaPager instance;

#endif //ARDUINO_T_LORA_PAGER
