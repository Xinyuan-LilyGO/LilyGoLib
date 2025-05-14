/**
 * @file      hal_interface.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 */
#include "hal_interface.h"
#include <math.h>
#include <lvgl.h>

#define NVS_NAME    "pager"
static user_setting_params_t user_setting;

typedef struct _device_const_var {
    uint16_t max_brightness;
    uint16_t min_brightness;
    uint16_t max_charge_current;
    uint16_t min_charge_current;
    uint8_t  charge_level_nums;
    uint8_t  charge_steps;
} device_const_var_t;

#ifdef ARDUINO

#define  CONFIG_BLE_KEYBOARD
#include <LilyGoLib.h>
#include <esp_mac.h>
#include <WiFi.h>
#include <SD.h>
#include <cbuf.h>
#include <Preferences.h>
#include "audio/keyboard_audio.h"
#include "driver/rtc_io.h"
#include "app_nfc.h"

static Preferences           prefs;
static TaskHandle_t          recTaskHandle;
static TaskHandle_t          playerTaskHandler = NULL;
static QueueHandle_t         playerQueue  = NULL;
static EventGroupHandle_t    playerEvent = NULL;
static bool                  pps_trigger = false;

#define PLAYER_PLAY                 _BV(0)
#define PLAYER_END                  _BV(1)
#define PLAYER_RUNNING              _BV(2)


#if defined(HAS_SD_CARD_SOCKET)
#define FILESYSTEM                  SD
#else
#include <FFat.h>
#define FILESYSTEM                  FFat
#endif

#include <BLEDevice.h>
#if  defined(ARDUINO) && defined(USING_UART_BLE)
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static BLEServer *pServer = NULL;
static BLECharacteristic *pTxCharacteristic;
static bool deviceConnected = false;
#endif

#if defined(USING_BLE_KEYBOARD)
#include <BleKeyboard.h>
BleKeyboard bleKeyboard;
#endif

#endif

static device_const_var_t dev_conts_var = {
    .max_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL,
    .min_brightness = DEVICE_MIN_BRIGHTNESS_LEVEL,
    .max_charge_current = DEVICE_MAX_CHARGE_CURRENT,
    .min_charge_current = DEVICE_MIN_CHARGE_CURRENT,
    .charge_level_nums = DEVICE_CHARGE_LEVEL_NUMS,
    .charge_steps = DEVICE_CHARGE_STEPS,
};


static const char *hw_devices[] = {
    USING_RADIO_NAME,

#ifdef USING_INPUT_DEV_TOUCHPAD
    "Touch Panel",
#else
    "",
#endif
    "Haptic Drive",
    "Power management",
    "Real-time clock",
    "PSRAM",
    "GPS",
#ifdef HAS_SD_CARD_SOCKET
    "SD card",
#else
    "",
#endif
#ifdef USING_ST25R3916
    "NFC",
#else
    "",
#endif
    "Motion sensor",
#ifdef USING_INPUT_DEV_KEYBOARD
    "Keyboard",
#else
    "",
#endif

#ifdef USING_BQ_GAUGE
    "Gauge",
#else
    "",
#endif

#ifdef USING_XL9555_EXPANDS
    "Expands Control",
#else
    "",
#endif

#ifdef USING_AUDIO_CODEC
    "Audio codec",
#endif

#ifdef USING_EXTERN_NRF2401
    "NRF2401",
#endif
};

static bool sync_date_time = false;

#ifdef USING_ST25R3916
static void nrf_notify_callback();
static void ndef_event_callback(ndefTypeId id, void*data);
#endif

#ifdef ARDUINO

size_t getArduinoLoopTaskStackSize(void)
{
    return 30 * 1024;
}

#include <mp3dec.h>

static bool playMP3(uint8_t *src, size_t src_len)
{
    int16_t outBuf[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
    uint8_t *readPtr = NULL;
    int bytesAvailable = 0, err = 0, offset = 0;
    MP3FrameInfo frameInfo;
    HMP3Decoder decoder = NULL;
    bool codec_begin = false;

    bytesAvailable = src_len;
    readPtr = src;

    decoder = MP3InitDecoder();
    if (decoder == NULL) {
        log_e("Could not allocate decoder");
        return false;
    }
    xEventGroupSetBits(playerEvent, PLAYER_RUNNING);
    do {
        offset = MP3FindSyncWord(readPtr, bytesAvailable);
        if (offset < 0) {
            break;
        }
        readPtr += offset;
        bytesAvailable -= offset;
        err = MP3Decode(decoder, &readPtr, &bytesAvailable, outBuf, 0);
        if (err) {
            log_e("Decode ERROR: %d", err);
            MP3FreeDecoder(decoder);
            xEventGroupClearBits(playerEvent, PLAYER_RUNNING);
            return false;
        } else {
            MP3GetLastFrameInfo(decoder, &frameInfo);
#if  defined(USING_PCM_AMPLIFIER)

            if (!codec_begin) {
                codec_begin = true;
                instance.powerControl(POWER_SPEAK, true);
                log_d("Start PCM Play...");
#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
                printf("sample rate:%d bitPs:%d ch:%d\n", frameInfo.samprate, frameInfo.bitsPerSample, (i2s_channel_t)frameInfo.nChans);
                instance.player.configureTX(frameInfo.samprate, frameInfo.bitsPerSample, (i2s_channel_t)frameInfo.nChans);
#else
                instance.player.configureTX(frameInfo.samprate, (i2s_data_bit_width_t)frameInfo.bitsPerSample, (i2s_slot_mode_t)frameInfo.nChans);
#endif
            }

            instance.player.write((uint8_t *)outBuf, (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps));

#elif defined(USING_AUDIO_CODEC)
            if (!codec_begin) {
                codec_begin = true;
                Serial.printf("Set sample rate:%d bitsPerSample:%d\n", frameInfo.samprate, frameInfo.bitsPerSample);
                int ret = instance.codec.open(frameInfo.bitsPerSample, frameInfo.nChans, frameInfo.samprate);
                Serial.printf("esp_codec_dev_open:0x%X\n", ret);
            }
            int ret = instance.codec.write((uint8_t *)outBuf, (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps));
            if (ret != 0) {
                Serial.printf("esp_codec_dev_write:0x%X\n", ret);
            }
#endif
        }

WAIT:
        EventBits_t eventBits =  xEventGroupWaitBits(playerEvent, PLAYER_PLAY | PLAYER_END
                                 , pdFALSE, pdFALSE, portMAX_DELAY);

        if (eventBits & PLAYER_END) {
            printf("TASK END\n");
            break;
        }

    } while (true);

    MP3FreeDecoder(decoder);
    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);

#if  defined(USING_PCM_AMPLIFIER)
    instance.powerControl(POWER_SPEAK, false);
#elif defined(USING_AUDIO_CODEC)
    instance.codec.close();
#endif
    return true;
}

static void hw_sd_paly(const char *filename)
{
    bool isMP3 = String(filename).endsWith(".mp3");
    // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared, lock the SPI bus before use
    instance.lockSPI();
    String str = "/" + String(filename);
    File f = FILESYSTEM.open(str);
    if (!f) {
        Serial.printf("Open %s failed!\n", filename);
        // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
        instance.unlockSPI();
        return;
    }
    size_t file_size = f.size();
    uint8_t *buf  = (uint8_t *)ps_malloc(file_size);
    if (!buf) {
        Serial.println("ps malloc failed!");
        f.close();
        // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
        instance.unlockSPI();
        return ;
    }
    size_t read_size =  f.readBytes((char *)buf, file_size);
    f.close();

    // SPI bus-shared and releases the bus after use.
    instance.unlockSPI();  //Release lock

    if (read_size == file_size) {
        Serial.print("Playing ");
        Serial.println(filename);
        if (isMP3) {
            playMP3(buf, read_size);
        } else {
        }
        Serial.println("Play done..");
    }
    free(buf);
}

static void playerTask(void *args)
{
    audio_params_t params;
    while (1) {
        if (xQueueReceive(playerQueue, &params, portMAX_DELAY) != pdPASS) {
            continue;
        }
        switch (params.event) {
        case APP_EVENT_PLAY:
            hw_sd_paly(params.filename);
            break;
        case APP_EVENT_PLAY_KEY:
            Serial.println("APP_EVENT_PLAY_KEY");
            playMP3((uint8_t* )keyboard_audio, keyboard_audio_mp3_len);
            break;
        case APP_EVENT_RECOVER:
            break;
        default:
            break;
        }
    }
    playerTaskHandler = NULL;
    vTaskDelete(NULL);
}

#endif

#ifdef ARDUINO
#define MIC_PRESSURE

#ifdef MIC_PRESSURE

static int16_t pdm_to_pcm(int32_t pdm_sample)
{
    return (int16_t)(pdm_sample >> 16);
}

static float pcm_to_pressure(int16_t pcm_sample)
{
    float dBFS = 20 * log10(abs(pcm_sample) / 32768.0);
    float SPL = dBFS + (-22.0);
    float pressure = pow(10, SPL / 20.0) * 20;
    return pressure;
}

static float map_pressure(float pressure)
{
    float min_pressure = 0.01;
    float max_pressure = 1;
    float mapped_pressure = (pressure - min_pressure) / (max_pressure - min_pressure) * 100.0;
    if (mapped_pressure < 0) mapped_pressure = 0;
    if (mapped_pressure > 100) mapped_pressure = 100;
    return mapped_pressure;
}
#endif  //MIC_PRESSURE

#endif


void hw_set_mic_start()
{
#ifdef ARDUINO
#ifdef USING_AUDIO_CODEC
    instance.codec.open(16, 2, 16000);
#endif
#endif
}

void hw_set_mic_stop()
{
#ifdef ARDUINO
#ifdef USING_AUDIO_CODEC
    instance.codec.close();
#endif
#endif
}

#ifdef ARDUINO
static uint8_t pencent = 0;
static const float SENSITIVITY = 30.0;
static const float V_REF = 3.3;
static const int16_t MAX_16BIT = 32767;
static const int SAMPLE_COUNT = 256;
static int16_t samples[SAMPLE_COUNT];
#endif

int16_t hw_get_microphone_pressure_level()
{
#ifdef ARDUINO
#ifdef MIC_PRESSURE
#if defined(USING_PDM_MICROPHONE)
    int32_t pdm_sample;
    size_t bytes_read;
    bytes_read = instance.mic.readBytes((char *)&pdm_sample, sizeof(pdm_sample));
    if (bytes_read == sizeof(pdm_sample)) {
        int16_t pcm_sample = pdm_to_pcm(pdm_sample);
        float pressure = pcm_to_pressure(pcm_sample);
        float mapped_pressure = map_pressure(pressure);
        // Serial.printf("pcm_sample:%d pressure:%.2f mapped_pressure:%.2f\n", pcm_sample, pressure, mapped_pressure);
        return (int16_t)mapped_pressure;
    }
    return 0;
#elif defined(USING_AUDIO_CODEC)
    if (instance.codec.read((uint8_t * )&samples, SAMPLE_COUNT * sizeof(int16_t)) == 0) {
        float totalPressure = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            float voltage = ((float)samples[i] / MAX_16BIT) * V_REF * 1000;
            float pressure = voltage / SENSITIVITY;
            totalPressure += pressure;
        }
        float averagePressure = totalPressure / SAMPLE_COUNT;
        float minPressure = -0.01;
        float maxPressure = 10;
        float convertedValue = 0;
        if (maxPressure > minPressure) {
            convertedValue = ((averagePressure - minPressure) / (maxPressure - minPressure)) * 100;
            if (convertedValue < 0) convertedValue = 0;
            if (convertedValue > 100) convertedValue = 100;
        }
        pencent = convertedValue;
        return pencent;
    }
    return 0;
#endif
#endif  //MIC_PRESSURE
#else
    return rand() % 100;
#endif
}

extern void hw_nrf24_begin();
extern void hw_radio_begin();


#ifdef USING_ST25R3916

extern void ui_nfc_pop_up(wifi_conn_params_t &params);

static void nrf_notify_callback()
{
    Serial.println("NDEF Detected.");
    hw_feedback();
}

static void ndef_event_callback(ndefTypeId id, void*data)
{
    static ndefTypeRtdDeviceInfo   devInfoData;
    static ndefConstBuffer         bufAarString;
    static ndefRtdUri              url;
    static ndefRtdText             text;
    static String msg = "";
    static wifi_conn_params_t params;
    msg = "";
    switch (id) {
    case NDEF_TYPE_EMPTY:
        break;
    case NDEF_TYPE_RTD_DEVICE_INFO:
        memcpy(&devInfoData, data, sizeof(ndefTypeRtdDeviceInfo));
        break;
    case NDEF_TYPE_RTD_TEXT:
        memcpy(&text, data, sizeof(ndefRtdText));
        Serial.printf("LanguageCode:%s Sentence:%s\n", reinterpret_cast < const char * > (text.bufLanguageCode.buffer), reinterpret_cast < const char * > (text.bufSentence.buffer));
        msg.concat("LanguageCode:");
        msg.concat(reinterpret_cast < const char * > (text.bufLanguageCode.buffer));
        msg.concat("Sentence:");
        msg.concat(reinterpret_cast < const char * > (text.bufSentence.buffer));
        ui_msg_pop_up("NFC Text", msg.c_str());
        break;
    case NDEF_TYPE_RTD_URI:
        memcpy(&url, data, sizeof(ndefRtdUri));
        Serial.printf("PROTOCOL:%s URL:%s\n", reinterpret_cast < const char * > (url.bufProtocol.buffer), reinterpret_cast < const char * > (url.bufUriString.buffer));
        msg.concat("PROTOCOL:");
        msg.concat(reinterpret_cast < const char * > (url.bufProtocol.buffer));
        msg.concat("URL:");
        msg.concat(reinterpret_cast < const char * > (url.bufUriString.buffer));
        ui_msg_pop_up("NFC Url", msg.c_str());
        break;
    case NDEF_TYPE_RTD_AAR:
        memcpy(&bufAarString, data, sizeof(ndefConstBuffer));
        Serial.printf("NDEF_TYPE_RTD_AAR :%s\n", (char*)bufAarString.buffer);
        break;
    case NDEF_TYPE_MEDIA:
        break;
    case NDEF_TYPE_MEDIA_VCARD:
        break;
    case NDEF_TYPE_MEDIA_WIFI: {
        ndefTypeWifi * wifi = (ndefTypeWifi*)data;
        params.ssid = std::string(reinterpret_cast < const char * > (wifi->bufNetworkSSID.buffer), wifi->bufNetworkSSID.length);
        params.password = std::string(reinterpret_cast < const char * > (wifi->bufNetworkKey.buffer), wifi->bufNetworkKey.length);
        Serial.printf("ssid:<%s> password:<%s>\n", params.ssid.c_str(), params.password.c_str());
        ui_nfc_pop_up(params);
    }
    break;
    default:
        break;
    }
}
#endif



void hw_init()
{
#ifdef ARDUINO
    playerQueue =  xQueueCreate(2, sizeof(audio_params_t));
    playerEvent =  xEventGroupCreate();

    hw_radio_begin();

#ifdef USING_EXTERN_NRF2401
    hw_nrf24_begin();
#endif


#ifdef USING_AUDIO_CODEC
    instance.codec.setVolume(100);
    instance.codec.setGain(50.0);
#endif //USING_AUDIO_CODEC

#ifdef USING_INPUT_DEV_KEYBOARD
    instance.attachKeyboardFeedback(true, 80);

    instance.setFeedbackCallback([](void*args) {

        lv_indev_t *drv = (lv_indev_t *)args;

        if (lv_indev_get_type(drv) == LV_INDEV_TYPE_KEYPAD) {

            instance.vibrator();

            audio_params_t params = {
                .event = APP_EVENT_PLAY_KEY,
                .filename = NULL
            };
            xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
            if (hw_player_running()) {
                xEventGroupSetBits(playerEvent, PLAYER_END);
                while (hw_player_running()) {
                    delay(2);
                }
            }
            xEventGroupSetBits(playerEvent, PLAYER_PLAY);
            xQueueSend(playerQueue, &params, portMAX_DELAY);

        } else {

            instance.vibrator();

        }
    });
#endif //USING_INPUT_DEV_KEYBOARD


    xTaskCreate(playerTask, "app/play", 8 * 1024, NULL, 12, &playerTaskHandler);

    prefs.begin(NVS_NAME);
    if (prefs.getBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t)) != sizeof(user_setting_params_t)) {  // simple check that data fits
        log_e("Data is not correct size!,set default setting");
        user_setting.brightness_level = 50;
        user_setting.keyboard_bl_level = 80;
        user_setting.disp_timeout_second = 30;
        user_setting.charger_current = 1000;
        user_setting.charger_enable = true;
        prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
    }

    user_setting.charger_current = hw_get_charger_current();

    hw_set_disp_backlight(user_setting.brightness_level);

    hw_set_kb_backlight(user_setting.keyboard_bl_level);

    instance.onEvent([](DeviceEvent_t event, void * user_data) {
        log_d("ON EVENT PMU CLICK");
    }, PMU_EVENT_KEY_CLICKED, NULL);


#else
    user_setting.brightness_level = 10;
    user_setting.keyboard_bl_level = 255;
    user_setting.disp_timeout_second = 30;
    user_setting.charger_current = 1000;
    user_setting.charger_enable = true;
#endif

#ifdef USING_ST25R3916
    beginNFC(nrf_notify_callback, ndef_event_callback);
#endif

}

void hw_get_user_setting(user_setting_params_t &param)
{
    param = user_setting;
    printf("Get brightness_level    :%u\n", user_setting.brightness_level);
    printf("Get keyboard_bl_level   :%u\n", user_setting.keyboard_bl_level);
    printf("Get disp_timeout_second :%u\n", user_setting.disp_timeout_second);
    printf("Get charger_current     :%u\n", user_setting.charger_current);
    printf("Get charger_enable      :%u\n", user_setting.charger_enable);
}

void hw_set_user_setting(user_setting_params_t &param)
{
    user_setting = param;
#ifdef ARDUINO
    prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
#endif
    printf("set brightness_level    :%u\n", param.brightness_level);
    printf("set keyboard_bl_level   :%u\n", param.keyboard_bl_level);
    printf("set disp_timeout_second :%u\n", param.disp_timeout_second);
    printf("set charger_current     :%u\n", param.charger_current);
    printf("set charger_enable      :%u\n", param.charger_enable);

}

const uint32_t hw_get_disp_timeout_ms()
{
    return user_setting.disp_timeout_second * 1000UL;
}

uint16_t hw_get_devices_nums()
{
    return sizeof(hw_devices) / sizeof(hw_devices[0]);
}

const char *hw_get_devices_name(int index)
{
    if (index > hw_get_devices_nums()) {
        return "NULL";
    }
    return hw_devices[index];
}

const char *hw_get_variant_name()
{
#ifdef ARDUINO
    return instance.getName();
#else
    return "LilyGo T-LoRa-Pager (2025)";
#endif
}


bool hw_get_mac(uint8_t *mac)
{
#ifdef ARDUINO
    esp_efuse_mac_get_default(mac);
    return true;
#endif
    return false;
}

void hw_get_wifi_ssid(string &param)
{
#ifdef ARDUINO
    param = WiFi.isConnected() ?  WiFi.SSID().c_str() : "N.A";
#else
    param = "NO CONFIG";
#endif
}


void hw_get_date_time(string &param)
{
#ifdef ARDUINO
    struct tm timeinfo;
    instance.rtc.getDateTime(&timeinfo);
    char datetime[128] = {0};
    snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    param  = datetime;
#else
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    char datetime[128] = {0};
    snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    param  = datetime;
#endif
}

void hw_get_date_time(struct tm &timeinfo)
{
#ifdef ARDUINO
    instance.rtc.getDateTime(&timeinfo);
#else
    time_t now;
    time(&now);
    timeinfo = *localtime(&now);
#endif
}


wl_status_t hw_get_wifi_status()
{
#ifdef ARDUINO
    return WiFi.status();
#else
    return WL_NO_SSID_AVAIL;
#endif
}

void hw_get_ip_address(string &param)
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        param = WiFi.localIP().toString().c_str();
        return;
    }
#endif
    param = "N.A";
}

int16_t hw_get_wifi_rssi()
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        return (WiFi.RSSI());
    }
#endif
    return -99;
}

int16_t hw_get_battery_voltage()
{
#ifdef ARDUINO

#if  defined(USING_BQ_GAUGE)
    if (HW_GAUGE_ONLINE & hw_get_device_online()) {
        instance.gauge.refresh();
        return instance.gauge.getVoltage();
    } else {
        printf("Gauge Not online !\n");
        return 0;
    }
#elif defined(USING_PMU_MANAGE)
    return instance.pmu.getBattVoltage();
#else
    return 0;
#endif

#else
    return 0;
#endif
}

float hw_get_sd_size()
{
    float size = 0.0;
#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
    size = SD.cardSize() / 1024 / 1024 / 1024.0;

#elif defined(USING_FATFS)
    size = FFat.totalBytes() / 1024 / 1024;
#endif

#endif
    return size;
}

void hw_get_arduino_version(string &param)
{
#ifdef ARDUINO
    param.clear();
    param.append("V");
    param.append(std::to_string(ESP_ARDUINO_VERSION_MAJOR));
    param.append(".");
    param.append(std::to_string(ESP_ARDUINO_VERSION_MINOR));
    param.append(".");
    param.append(std::to_string(ESP_ARDUINO_VERSION_PATCH));
#else
    param = "V2.0.17";
#endif
}


void hw_gps_attach_pps()
{
#ifdef GPS_PPS
    pinMode(GPS_PPS, INPUT);
    attachInterrupt(GPS_PPS, []() {
        pps_trigger ^= 1;
    }, CHANGE);
#endif
}

void hw_gps_detach_pps()
{
#ifdef GPS_PPS
    detachInterrupt(GPS_PPS);
    pinMode(GPS_PPS, OPEN_DRAIN);
#endif
}

bool hw_get_gps_info(gps_params_t &param)
{
#ifdef ARDUINO
    static uint32_t interval = 0;
    param.pps = pps_trigger;

    if (millis() < interval) {
        return false;
    }
    interval = millis() + 1000;

    memset(&param, 0, sizeof(gps_params_t));


    param.model = instance.gps.getModel().c_str();
    param.rx_size = instance.gps.loop();

    bool location = instance.gps.location.isValid();
    bool datetime = (instance.gps.date.year() > 2000);

    if (location) {
        param.lat = instance.gps.location.lat();
        param.lng = instance.gps.location.lng();
        param.speed = instance.gps.speed.kmph();
    }

    if (datetime) {
        if (!sync_date_time) {
            sync_date_time = true;
            struct tm utc_tm = {0};
            time_t utc_timestamp;
            struct tm *local_tm;
            utc_tm.tm_year = instance.gps.date.year() - 1900;
            utc_tm.tm_mon = instance.gps.date.month() - 1;
            utc_tm.tm_mday = instance.gps.date.day();
            utc_tm.tm_hour = instance.gps.time.hour();
            utc_tm.tm_min = instance.gps.time.minute();
            utc_tm.tm_sec = instance.gps.time.second();
            instance.rtc.convertUtcToTimezone(utc_tm, GMT_OFFSET_SECOND);
            instance.rtc.setDateTime(utc_tm);
            instance.rtc.hwClockRead();
        }
        param.datetime.tm_year = instance.gps.date.year() - 1900;
        param.datetime.tm_mon = instance.gps.date.month() - 1;
        param.datetime.tm_mday = instance.gps.date.day();
        param.datetime.tm_hour = instance.gps.time.hour();
        param.datetime.tm_min =  instance.gps.time.minute();
        param.datetime.tm_sec = instance.gps.time.second();
    }

    if (instance.gps.satellites.isValid()) {
        param.satellite = instance.gps.satellites.value();
    }

    return location && datetime;
#else
    param.model = "Dummy";
    param.lat = 0.0;
    param.lng = 0.0;
    param.speed = rand() % 120;
    param.rx_size = 366666;
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo  = localtime(&now);
    param.datetime = *timeinfo;
    param.satellite = rand() % 30;
    return true;
#endif
}


uint32_t hw_get_device_online()
{
#ifdef ARDUINO
    return instance.getDeviceProbe();
#else
    return HW_RADIO_ONLINE | HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE;
#endif
}


void hw_set_disp_backlight(uint8_t level)
{
#ifdef ARDUINO
    instance.setBrightness(level);
#endif
}

uint8_t hw_get_disp_backlight()
{
#ifdef ARDUINO
    return instance.getBrightness();
#else
    return 100;
#endif
}

bool hw_get_disp_is_on()
{
#ifdef ARDUINO
    return instance.getBrightness() != 0;
#else
    return true;
#endif
}

void hw_set_kb_backlight(uint8_t level)
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.setBrightness(level);
#endif
}

uint8_t hw_get_kb_backlight()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    return instance.kb.getBrightness();
#else
    return 100;
#endif
}

int16_t hw_set_wifi_scan()
{
#ifdef ARDUINO
    printf("hw_set_wifi_scan\n");
    return  WiFi.scanNetworks(true);
#endif
    return 0;
}

bool hw_get_wifi_scanning()
{
#ifdef ARDUINO
    return WiFi.getStatusBits() & WIFI_SCANNING_BIT ;
#endif
    return false;
}


void hw_get_wifi_scan_result(vector < wifi_scan_params_t > &list)
{
    list.clear();
#ifdef ARDUINO
    int16_t nums = WiFi.scanComplete();
    if (nums < 0) {
        printf("Nothing network found. return code : %d\n", nums);
        return;
    } else {
        printf("find %d network\n", nums);
    }
    // uint8_t networkItem, String &ssid, uint8_t &encryptionType, int32_t &RSSI, uint8_t *&BSSID, int32_t &channel
    wifi_scan_params_t param;
    for (int i = 0; i < nums; ++i) {
        String ssid;
        uint8_t encryptionType;
        int32_t rssi;
        uint8_t *BSSID;
        int32_t channel;
        WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, BSSID, channel);
        printf("SSID:%s RSSI:%d\n", ssid.c_str(), rssi);
        param.authmode = encryptionType;
        param.ssid = ssid.c_str();
        param.rssi = rssi;
        param.channel = channel;
        memcpy(param.bssid, BSSID, 6);
        list.push_back(param);
    }
#else
    wifi_scan_params_t param;
    param.authmode = 1;
    param.ssid = "LilyGo-AABB0";
    param.rssi = -10;
    param.channel = 0;
    list.push_back(param);
#endif
}

void hw_set_wifi_connect(wifi_conn_params_t &params)
{
    printf("hw_set_wifi_connect:ssid:<%s> password <%s>\n", params.ssid.c_str(), params.password.c_str());
#ifdef ARDUINO
    String ssid = params.ssid.c_str();
    String password = params.password.c_str();
    Serial.print("SSID :"); Serial.println(ssid);
    Serial.print("PWD :"); Serial.println(password);
    WiFi.begin(ssid, password);
#endif
}

bool hw_get_wifi_connected()
{
#ifdef ARDUINO
    return WiFi.isConnected();
#endif
    return false;
}



void hw_sd_list(vector < string > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO)

    instance.lockSPI();

#if defined(HAS_SD_CARD_SOCKET)
    instance.installSD();
#endif


    Serial.printf("Listing directory: %s\n", "/");
    File root = FILESYSTEM.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        instance.unlockSPI();
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        instance.unlockSPI();
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                hw_sd_list(list, file.path(), levels - 1);
            }
        } else {
            String filename = file.name();
            if (filename.endsWith(".mp3") || filename.endsWith(".wav")) {
                list.push_back(filename.c_str());
            }
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }

    instance.unlockSPI();

#else
    list.push_back("/abc.mp3");
    list.push_back("/ccc.mp3");
    list.push_back("/dddd.mp3");
#endif
}

void hw_mount_sd()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.installSD();
#endif
}

void hw_get_sd_music(vector < string > &list)
{
    list.clear();
    hw_sd_list(list, "/", 0);
}


void hw_set_sd_music_play(const char *filename)
{
    audio_params_t params = {
        .event = APP_EVENT_PLAY,
        .filename = filename
    };
    printf("play : %s\n", filename);
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    if (hw_player_running()) {
        xEventGroupSetBits(playerEvent, PLAYER_END);
        while (hw_player_running()) {
            delay(2);
        }
    }
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
    xQueueSend(playerQueue, &params, portMAX_DELAY);
#endif
}

void hw_set_play_stop()
{
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    if (hw_player_running()) {
        xEventGroupSetBits(playerEvent, PLAYER_END);
        while (hw_player_running()) {
            delay(2);
        }
    }
#endif
}

void hw_set_sd_music_pause()
{
    printf("playerTaskHandler pause!\n");
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
#endif
}

void hw_set_sd_music_resume()
{
    printf("playerTaskHandler resume!\n");
#ifdef ARDUINO
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
#endif
}

bool hw_player_running()
{
#ifdef ARDUINO
    return xEventGroupGetBits(playerEvent) & PLAYER_RUNNING;
#endif
    return true;
}

void hw_shutdown()
{
#ifdef ARDUINO
    instance.decrementBrightness(0, 5, false);
#if defined(USING_PPM_MANAGE)
    instance.ppm.shutdown();
#elif defined(USING_PMU_MANAGE)
    instance.pmu.shutdown();
#endif
#endif
}

void hw_sleep()
{
#ifdef ARDUINO
    vTaskDelete(playerTaskHandler);

#ifdef USING_PDM_MICROPHONE
    instance.mic.end();
#endif

#ifdef USING_PCM_AMPLIFIER
    instance.player.end();
#endif

    instance.decrementBrightness(0, 5, false);
    instance.sleep();
#endif
}

bool hw_get_otg_enable()
{
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
    return  instance.ppm.isEnableOTG();
#else
    return false;
#endif
}

bool hw_set_otg(bool enable)
{
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
    if (enable) {
        return  instance.ppm.enableOTG();
    } else {
        instance.ppm.disableOTG();
    }
    return true;
#endif
    return false;
}

bool hw_get_charge_enable()
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    return  instance.ppm.isEnableCharge();
#elif defined(USING_PMU_MANAGE)
    return  instance.isEnableCharge();
#endif
#else
    return false;
#endif
}

void hw_set_charger(bool enable)
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    if (enable) {
        instance.ppm.enableCharge();
    } else {
        instance.ppm.disableCharge();
    }
#elif defined(USING_PMU_MANAGE)
    if (enable) {
        instance.enableCharge();
    } else {
        instance.disableCharge();
    }
#endif
#endif
}

uint16_t hw_get_charger_current()
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    return  instance.ppm.getChargerConstantCurr();
#elif defined(USING_PMU_MANAGE)
    return  instance.getChargeCurrent();
#endif
#else
    return 0;
#endif
}

void hw_set_charger_current(uint16_t milliampere)
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    instance.ppm.setChargerConstantCurr(milliampere);
#elif defined(USING_PMU_MANAGE)
    instance.setChargeCurrent(milliampere);
#endif
#endif
}

uint8_t hw_get_charger_current_level()
{
#if defined(USING_PPM_MANAGE)
    return user_setting.charger_current / dev_conts_var.charge_steps;
#elif defined(USING_PMU_MANAGE)
    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    uint16_t cur =  instance.getChargeCurrent();
    for (int i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (cur == table[i]) {
            return i;
        }
    }
    return 0;
#else
    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    uint16_t cur =  user_setting.charger_current;
    for (int i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (cur == table[i]) {
            return i;
        }
    }
    return 0;
#endif
}

uint16_t hw_set_charger_current_level(uint8_t level)
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    printf("set charge current:%u mA\n", level * dev_conts_var.charge_steps);
    instance.ppm.setChargerConstantCurr(level * dev_conts_var.charge_steps);
    return  level * dev_conts_var.charge_steps;
#elif defined(USING_PMU_MANAGE)
    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    if (level > (sizeof(table) / sizeof(table[0]) - 1)) {
        level = sizeof(table) / sizeof(table[0]) - 1;
    }
    printf("set charge current:%u mA\n", table[level]);
    instance.setChargeCurrent(table[level]);
    return  table[level];
#endif
#else

    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    if (level > (sizeof(table) / sizeof(table[0]) - 1)) {
        level = sizeof(table) / sizeof(table[0]) - 1;
    }
    printf("set charge current:%u mA\n", table[level]);
    return  table[level];
#endif

}

void hw_get_monitor_params(monitor_params_t &params)
{
#ifdef ARDUINO
    memset(&params, 0, sizeof(monitor_params_t));

#if defined(USING_PPM_MANAGE)
    params.type = MONITOR_PPM;
    params.charge_state = instance.ppm.getChargeStatusString();
    params.usb_voltage = instance.ppm.getVbusVoltage();
    params.sys_voltage = instance.ppm.getSystemVoltage();
#elif defined(USING_PMU_MANAGE)
    params.type = MONITOR_PMU;
    params.charge_state = "None";
    params.usb_voltage = instance.pmu.getVbusVoltage();
    params.sys_voltage = instance.pmu.getSystemVoltage();
    params.battery_voltage = instance.pmu.getBattVoltage();
    params.battery_percent = instance.pmu.getBatteryPercent();
    params.temperature = instance.pmu.getTemperature();
#endif

#ifdef USING_BQ_GAUGE
    if (hw_get_device_online() & HW_GAUGE_ONLINE) {
        instance.gauge.refresh();
        params.battery_percent = instance.gauge.getStateOfCharge();
        params.battery_voltage = instance.gauge.getVoltage();
        params.instantaneousCurrent = instance.gauge.getCurrent();
        params.remainingCapacity = instance.gauge.getRemainingCapacity();
        params.fullChargeCapacity = instance.gauge.getFullChargeCapacity();
        params.standbyCurrent = instance.gauge.getStandbyCurrent();
        params.temperature = instance.gauge.getTemperature();
        params.designCapacity = instance.gauge.getDesignCapacity();
        params.averagePower = instance.gauge.getAveragePower();
        params.maxLoadCurrent = instance.gauge.getMaxLoadCurrent();
        BatteryStatus batteryStatus = instance.gauge.getBatteryStatus();

        if (batteryStatus.isInDischargeMode()) {
            params.timeToEmpty = instance.gauge.getTimeToEmpty();
            params.timeToFull = 0;
        } else {
            if (batteryStatus.isFullChargeDetected()) {
                Serial.println("\t- Full charge detected.");
                params.timeToFull = 0;
                params.timeToEmpty = 0;
            } else {
                params.timeToEmpty = 0;
                params.timeToFull = instance.gauge.getTimeToFull();
            }
        }
    }
#endif

#else
    params.type = MONITOR_PPM;
    params.battery_percent = 30 + rand() % (100 - 30 + 1);;
    params.battery_voltage = 4178;
    params.charge_state = "Fast charging";
    params.usb_voltage = 4998;
#endif
}

static imu_params_t imu_params = {0, 0, 0, 0};

void hw_get_imu_params(imu_params_t &params)
{
#ifdef ARDUINO
#if defined(USING_BHI260_SENSOR)
    params =  imu_params;
#elif defined(USING_BMA423_SENSOR)
    params.orientation = instance.sensor.direction();
#endif // SENSOR
#else
    params =  imu_params;
#endif //ARDUINO
}

#if  defined(ARDUINO) && defined(USING_BHI260_SENSOR)
void imu_data_process(uint8_t sensor_id, uint8_t *data_ptr, uint32_t len, uint64_t *timestamp, void *user_data)
{
    float roll, pitch, yaw;
    bhy2_quaternion_to_euler(data_ptr, &roll,  &pitch, &yaw);
    imu_params.roll = roll;
    imu_params.pitch = pitch;
    imu_params.heading = yaw;
}
#endif //ARDUINO

void hw_register_imu_process()
{
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    float sample_rate = 100.0;      /* Read out data measured at 100Hz */
    uint32_t report_latency_ms = 0; /* Report immediately */
    instance.sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
    // Enable Quaternion function
    instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, sample_rate, report_latency_ms);
    // Register event callback function
    instance.sensor.onResultEvent(SensorBHI260AP::GAME_ROTATION_VECTOR, imu_data_process);
#elif defined(USING_BMA423_SENSOR)
    instance.sensor.configAccelerometer();
    instance.sensor.enableAccelerometer();
#endif // SENSOR
#endif // ARDUINO
}

void hw_unregister_imu_process()
{
#if  defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, 0, 0);
#elif defined(USING_BMA423_SENSOR)
    instance.sensor.disableAccelerometer();
#endif // SENSOR
#endif // ARDUINO
}

//* ble //

#if  defined(ARDUINO) && defined(USING_UART_BLE)
static cbuf ble_message(256);
class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        deviceConnected = true;
    };

    void onDisconnect(BLEServer *pServer)
    {
        deviceConnected = false;
        pServer->startAdvertising();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        String rxValue = pCharacteristic->getValue();

        if (rxValue.length() > 0) {
            Serial.println("*********");
            Serial.print("Received Value: ");
            for (int i = 0; i < rxValue.length(); i++) {
                Serial.print(rxValue[i]);
            }
            Serial.println();
            Serial.println("*********");

            ble_message.write(rxValue.c_str(), rxValue.length());
        }
    }
};
#endif

void hw_enable_ble(const char *devName)
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
    static bool isEnableBle = false;
    if (!isEnableBle) {
        uint64_t chipmacid = 0LL;
        esp_efuse_mac_get_default((uint8_t *)(&chipmacid));
        // Create the BLE Device
        String name = devName + String("_") + String((chipmacid >> 8) & 0xFF) + String("_") + String(chipmacid & 0xFF) ;
        BLEDevice::init(name);
        // Create the BLE Server
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        // Create the BLE Service
        BLEService *pService = pServer->createService(SERVICE_UUID);
        // Create a BLE Characteristic
        pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
        pTxCharacteristic->addDescriptor(new BLE2902());
        BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX,
                                               BLECharacteristic::PROPERTY_WRITE);
        pRxCharacteristic->setCallbacks(new MyCallbacks());
        // Start the service
        pService->start();
    }
    // Start advertising
    pServer->getAdvertising()->start();

#endif
}

void hw_deinit_ble()
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
    BLEDevice::deinit();
#endif
}

void hw_disable_ble()
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
    pServer->getAdvertising()->stop();
    ble_message.remove(ble_message.available());
#endif
}

size_t hw_get_ble_message(char *buffer, size_t buffer_size)
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
    size_t size =  ble_message.available();
    if (size) {
        size_t read_size = size;
        if (size > buffer_size) {
            read_size = buffer_size;
        }
        ble_message.read(buffer, read_size);
    }
    return size;
#else
    return 0;
#endif
}

void hw_set_ble_kb_enable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    bleKeyboard.setName(hw_get_variant_name());
    bleKeyboard.begin();
#endif
#endif
}

void hw_set_ble_kb_disable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
    bleKeyboard.end();
    log_d("Disable ble devices");
#endif
}

void hw_set_ble_kb_char(const char *c)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        bleKeyboard.print(c);
    }
#endif
#endif
}

void hw_set_ble_kb_key(uint8_t key)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        bleKeyboard.press(key);
    }
#endif
#endif
}

void hw_set_ble_kb_release()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        bleKeyboard.releaseAll();
    }
#endif
#endif
}

bool hw_get_ble_kb_connected()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        return true;
    }
#endif
#endif
    return false;
}

void hw_set_ble_key(media_key_value_t key)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        switch (key) {
        case MEDIA_VOLUME_UP:
            bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            break;
        case MEDIA_VOLUME_DOWN:
            bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
            break;
        case MEDIA_PLAY_PAUSE:
            bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
            break;
        case MEDIA_NEXT:
            bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
            break;
        case MEDIA_PREVIOUS:
            bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
            break;
        default: return;
        }

    }
#endif
#endif
}

void hw_set_keyboard_read_callback(void(*read)(int state, char &c))
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.setCallback(read);
#endif
}

void hw_feedback()
{
#ifdef ARDUINO
    instance.vibrator();
#endif
}

extern void reinstall_nfc();


void hw_low_power_loop()
{
#ifdef ARDUINO
    instance.lightSleep();
    // reinstall_nfc();
#ifdef USING_ST25R3916
    beginNFC(nrf_notify_callback, ndef_event_callback);
#endif
#endif
}

void hw_inc_brightness(uint8_t level)
{
#ifdef ARDUINO
    instance.incrementalBrightness(level);
#endif
}

void hw_dec_brightness(uint8_t level)
{
#ifdef ARDUINO
    instance.decrementBrightness(level);
#endif
}

uint8_t hw_get_disp_min_brightness()
{
    return dev_conts_var.min_brightness;
}

uint16_t hw_get_disp_max_brightness()
{
    return dev_conts_var.max_brightness;
}

uint8_t hw_get_min_charge_current()
{
    return dev_conts_var.min_charge_current;
}

uint16_t hw_get_max_charge_current()
{
    return dev_conts_var.max_charge_current;
}

uint8_t hw_get_charge_level_nums()
{
    return dev_conts_var.charge_level_nums;
}

uint8_t hw_get_charge_steps()
{
    return dev_conts_var.charge_steps;
}

void hw_set_cpu_freq(uint32_t mhz)
{
#ifdef ARDUINO
    setCpuFrequencyMhz(mhz);
#endif
}

void hw_disable_input_devices()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    instance.disableRotary();
#endif
}


void hw_enable_input_devices()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    instance.enableRotary();
#endif
}

void hw_flush_keyboard()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.flush();
#endif
}

bool hw_has_keyboard()
{
#if defined(USING_INPUT_DEV_KEYBOARD)
    return true;
#else
    return false;
#endif
}

bool hw_has_otg_function()
{
#if defined(USING_PPM_MANAGE)
    return true;
#else
    return true;
#endif
}

#if defined(ARDUINO)
#include <Esp.h>
#endif
void hw_print_mem_info()
{
#if defined(ARDUINO)
    printf("INTERNAL Memory Info:\n");
    printf("------------------------------------------\n");
    printf("  Total Size        :   %u B ( %.1f KB)\n", ESP.getHeapSize(), ESP.getHeapSize() / 1024.0);
    printf("  Free Bytes        :   %u B ( %.1f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
    printf("  Minimum Free Bytes:   %u B ( %.1f KB)\n", ESP.getMinFreeHeap(), ESP.getMinFreeHeap() / 1024.0);
    printf("  Largest Free Block:   %u B ( %.1f KB)\n", ESP.getMaxAllocHeap(), ESP.getMaxAllocHeap() / 1024.0);
    printf("------------------------------------------\n");
    printf("SPIRAM Memory Info:\n");
    printf("------------------------------------------\n");
    printf("  Total Size        :  %u B (%.1f KB)\n", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0);
    printf("  Free Bytes        :  %u B (%.1f KB)\n", ESP.getFreePsram(), ESP.getFreePsram() / 1024.0);
    printf("  Minimum Free Bytes:  %u B (%.1f KB)\n", ESP.getMinFreePsram(), ESP.getMinFreePsram() / 1024.0);
    printf("  Largest Free Block:  %u B (%.1f KB)\n", ESP.getMaxAllocPsram(), ESP.getMaxAllocPsram() / 1024.0);
    printf("------------------------------------------\n");
#endif
}
