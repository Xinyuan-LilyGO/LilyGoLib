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

#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"
#include "Esp.h"

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
#include <FFat.h>

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
#define FILESYSTEM                  FFat
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

#ifdef USING_BHI260_SENSOR
    "BHI260AP 6-Axis Sensor",
#else
    "",
#endif
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
#else
    "",
#endif

#ifdef USING_EXTERN_NRF2401
    "NRF2401 Sub 1G",
#else
    "",
#endif

#ifdef USING_SI473X_RADIO
    "SI4735 Radio",
#else
    "",
#endif

#ifdef USING_BME280
    "BME280 Pressure & Temperature",
#else
    "",
#endif

#ifdef USING_MAG_QMC5883
    "QMC5883P Magnetometer",
#else
    "",
#endif

#ifdef USING_BMA423_SENSOR
    "BMA423 Accelerometer",
#else
    "",
#endif

#ifdef USING_QMI8658_SENSOR
    "QMI8658 6-Axis Sensor",
#else
    "",
#endif

};

static bool sync_date_time = false;

#if  defined(USING_ST25R3916) && defined(ARDUINO)
static void nrf_notify_callback();
static void ndef_event_callback(ndefTypeId id, void *data);
#endif

extern void hw_nrf24_begin();
extern void hw_radio_begin();


#ifndef ARDUINO
int random(int min, int max)
{
    if (min > max) {
        int temp = min;
        min = max;
        max = temp;
    }
    int range = max - min + 1;
    return rand() % range + min;
}
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
                if (HW_CODEC_ONLINE & hw_get_device_online()) {
                    // Serial.printf("Set sample rate:%d bitsPerSample:%d\n", frameInfo.samprate, frameInfo.bitsPerSample);
                    int ret = instance.codec.open(frameInfo.bitsPerSample, frameInfo.nChans, frameInfo.samprate);
                    // Serial.printf("esp_codec_dev_open:0x%X\n", ret);
                }
            }
            if (HW_CODEC_ONLINE & hw_get_device_online()) {
                int ret = instance.codec.write((uint8_t *)outBuf, (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps));
                if (ret != 0) {
                    Serial.printf("esp_codec_dev_write:0x%X\n", ret);
                }
            }
#endif
        }

WAIT:
        EventBits_t eventBits =  xEventGroupWaitBits(playerEvent, PLAYER_PLAY | PLAYER_END
                                 , pdFALSE, pdFALSE, portMAX_DELAY);

        if (eventBits & PLAYER_END) {
            // printf("TASK END\n");
            break;
        }

    } while (true);

    MP3FreeDecoder(decoder);
    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);

#if  defined(USING_PCM_AMPLIFIER)
    instance.powerControl(POWER_SPEAK, false);
#elif defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.close();
    }
#endif
    return true;
}

static void hw_sd_play(audio_source_type_t source, const char *filename)
{
    bool isMP3 = String(filename).endsWith(".mp3");
    bool lock = false;

    String str = "/" + String(filename);
    File f;

    // Serial.printf("Playing file: %s source:%d\n", str.c_str(), source);
    if (source == AUDIO_SOURCE_SDCARD) {
        Serial.printf("Open from SD: %s\n", str.c_str());
        // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared, lock the SPI bus before use
        instance.lockSPI();
        f = SD.open(str);
        if (f) {
            lock = true;
        } else {
            Serial.printf("SD Open %s failed!\n", filename);
            // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
            instance.unlockSPI();
            return;
        }
    } else {
        Serial.printf("Open from FFat: %s\n", str.c_str());
        f = FFat.open(str);
        if (!f) {
            Serial.printf("FFat Open %s failed!\n", filename);
            return;
        }
    }

    size_t file_size = f.size();
    if (file_size == 0) {
        Serial.printf("File %s size is 0!\n", filename);
        f.close();
        if (lock) {
            // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
            instance.unlockSPI();
        }
        return ;
    }
    uint8_t *buf  = (uint8_t *)ps_malloc(file_size);
    if (!buf) {
        Serial.println("ps malloc failed!");
        f.close();
        if (lock) {
            // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
            instance.unlockSPI();
        }
        return ;
    }

    size_t read_size =  f.readBytes((char *)buf, file_size);
    f.close();

    // SPI bus-shared and releases the bus after use.
    if (lock) {
        instance.unlockSPI();  //Release lock
    }

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
            Serial.printf("Event: filename:%s source:%d\n", params.filename, params.source_type);
            hw_sd_play(params.source_type, params.filename);
            break;
        case APP_EVENT_PLAY_KEY:
            // Serial.println("APP_EVENT_PLAY_KEY");
            playMP3((uint8_t * )keyboard_audio, keyboard_audio_mp3_len);
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

static int16_t i2s_buffer[FFT_SIZE * 2];
static float fft_input[FFT_SIZE * 2] __attribute__((aligned(16)));
static float window[FFT_SIZE] __attribute__((aligned(16)));
static int16_t left_channel[FFT_SIZE];
static int16_t right_channel[FFT_SIZE];
static int read_count = 0;

static void process_channel_fft(int16_t *channel_data, float *bands, float freq_per_bin)
{
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input[2 * i] = (float)channel_data[i] * 3.0f / 32768.0f * window[i];
        fft_input[2 * i + 1] = 0;
    }

    dsps_fft2r_fc32_aes3(fft_input, FFT_SIZE);
    dsps_bit_rev_fc32(fft_input, FFT_SIZE);
    dsps_cplx2reC_fc32(fft_input, FFT_SIZE);

    float magnitudes[FFT_SIZE / 2];
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float real = fft_input[2 * i];
        float imag = fft_input[2 * i + 1];
        magnitudes[i] = sqrt(real * real + imag * imag);

        if (magnitudes[i] < 0.00001) magnitudes[i] = 0.00001;
        magnitudes[i] = 20 * log10(magnitudes[i]);
        magnitudes[i] = (magnitudes[i] + 40) / 40;
        magnitudes[i] = constrain(magnitudes[i], 0, 1);
    }

    int bin_count = (FFT_SIZE / 2) / FREQ_BANDS;
    memset(bands, 0, FREQ_BANDS * sizeof(float));

    for (int band = 0; band < FREQ_BANDS; band++) {
        int start_bin = band * bin_count;
        int end_bin = start_bin + bin_count;
        if (end_bin > FFT_SIZE / 2) end_bin = FFT_SIZE / 2;

        float sum = 0;
        int count = 0;
        for (int bin = start_bin; bin < end_bin; bin++) {
            sum += magnitudes[bin];
            count++;
        }

        if (count > 0) {
            bands[band] = sum / count;
        }
    }
}





#endif /*ARDUINO*/


void hw_audio_get_fft_data(FFTData *fft_data)
{
#ifdef ARDUINO
    float freq_per_bin = (float)SAMPLE_RATE / FFT_SIZE;

#if defined(USING_PDM_MICROPHONE)
    int32_t pdm_sample;
    instance.mic.readBytes((char *)i2s_buffer, FFT_SIZE * 2 * sizeof(int16_t));
#elif defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.read((uint8_t *)i2s_buffer, FFT_SIZE * 2 * sizeof(int16_t));
    } else {
        return;
    }
#endif

    read_count++;
    if (read_count % 10 == 0) {
        Serial.printf("Left: %d, Right: %d\n", i2s_buffer[0], i2s_buffer[1]);
    }

    for (int i = 0; i < FFT_SIZE; i++) {
        left_channel[i] = i2s_buffer[2 * i];
        right_channel[i] = i2s_buffer[2 * i + 1];
    }

    process_channel_fft(left_channel, fft_data->left_bands, freq_per_bin);
    process_channel_fft(right_channel, fft_data->right_bands, freq_per_bin);
#endif /*ARDUINO*/
}

bool hw_set_mic_start()
{
#ifdef ARDUINO
    int ret ;

#ifdef USING_AUDIO_CODEC
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        ret = instance.codec.open(16, instance.getCodecInputChannels(), 16000);
        if (ret < 0) {
            log_e("Audio codec open failed:0x%X", ret);
            return false;
        }
    } else {
        return false;
    }
#endif /*USING_AUDIO_CODEC*/

    ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        log_e("fft init failed = %i\n", ret);
        return false;
    }

    dsps_wind_hann_f32(window, FFT_SIZE);

#endif /*ARDUINO*/

    return true;
}

void hw_set_mic_stop()
{
#ifdef ARDUINO
#ifdef USING_AUDIO_CODEC
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.close();
    }
#endif
    dsps_fft2r_deinit_fc32();
#endif /*ARDUINO*/
}





#if  defined(USING_ST25R3916) && defined(ARDUINO)

extern void ui_nfc_pop_up(wifi_conn_params_t &params);

static void nrf_notify_callback()
{
    Serial.println("NDEF Detected.");
    hw_feedback();
}

static void ndef_event_callback(ndefTypeId id, void *data)
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
        Serial.printf("LanguageCode: %s\nSentence: %s\n", reinterpret_cast < const char * > (text.bufLanguageCode.buffer), reinterpret_cast < const char * > (text.bufSentence.buffer));
        msg.concat("LanguageCode: ");
        msg.concat(reinterpret_cast < const char * > (text.bufLanguageCode.buffer));
        msg.concat("\nSentence: ");
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
        Serial.printf("NDEF_TYPE_RTD_AAR :%s\n", (char *)bufAarString.buffer);
        break;
    case NDEF_TYPE_MEDIA:
        break;
    case NDEF_TYPE_MEDIA_VCARD:
        break;
    case NDEF_TYPE_MEDIA_WIFI: {
        ndefTypeWifi *wifi = (ndefTypeWifi *)data;
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
#endif  /*USING_ST25R3916*/



bool hw_start_nfc_discovery()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    instance.powerControl(POWER_NFC, true);
    return beginNFC(nrf_notify_callback, ndef_event_callback);
#else
    return false;
#endif
}

void hw_stop_nfc_discovery()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    deinitNFC();
    instance.powerControl(POWER_NFC, false);
#endif
}

#ifdef ARDUINO_T_LORA_PAGER
const uint8_t mic_gain = 10;
#else
const uint8_t mic_gain = 10;
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
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.setVolume(100);
        instance.codec.setGain(mic_gain);
    } else {
        log_w("Audio codec not online!");
    }
#endif //USING_AUDIO_CODEC

#ifdef USING_INPUT_DEV_KEYBOARD
    instance.attachKeyboardFeedback(true, 80);

    instance.setFeedbackCallback([](void *args) {

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
        user_setting.charger_current = DEVICE_CHARGE_CURRENT_RECOMMEND;
        user_setting.charger_enable = true;
        user_setting.gmtOffset_sec = GMT_OFFSET_SECOND;
        prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
    }

    user_setting.charger_current = hw_get_charger_current();

    hw_set_disp_backlight(user_setting.brightness_level);

    hw_set_kb_backlight(user_setting.keyboard_bl_level);

    instance.onEvent([](DeviceEvent_t event, void *params, void *user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            log_d("ON EVENT PMU CLICK");
        }
    }, POWER_EVENT, NULL);
    
    prefs.begin("my-config");
    


#else
    user_setting.brightness_level = 10;
    user_setting.keyboard_bl_level = 255;
    user_setting.disp_timeout_second = 30;
    user_setting.charger_current = 1000;
    user_setting.charger_enable = true;
#endif

    // #if  defined(USING_ST25R3916) && defined(ARDUINO)
    //     beginNFC(nrf_notify_callback, ndef_event_callback);
    // #endif

}

void hw_get_user_setting(user_setting_params_t &param)
{
    param = user_setting;
    printf("Get brightness_level    :%u\n", user_setting.brightness_level);
    printf("Get keyboard_bl_level   :%u\n", user_setting.keyboard_bl_level);
    printf("Get disp_timeout_second :%u\n", user_setting.disp_timeout_second);
    printf("Get charger_current     :%u\n", user_setting.charger_current);
    printf("Get charger_enable      :%u\n", user_setting.charger_enable);
    printf("Get gmtOffset_sec       :%d\n", user_setting.gmtOffset_sec);
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
    printf("set gmtOffset_sec       :%d\n", param.gmtOffset_sec);

}

void hw_save_wifi_creds(const char *ssid, const char *password)
{
#ifdef ARDUINO
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_password", password);
    printf("Saved WiFi creds: ssid=%s\n", ssid);
#endif
}

void hw_load_wifi_creds(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
#ifdef ARDUINO
    String s = prefs.getString("wifi_ssid", "");
    String p = prefs.getString("wifi_password", "");
    strncpy(ssid, s.c_str(), ssid_len - 1);
    ssid[ssid_len - 1] = '\0';
    strncpy(password, p.c_str(), password_len - 1);
    password[password_len - 1] = '\0';
    printf("Loaded WiFi creds: ssid=%s\n", ssid);
#endif
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
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.getDateTime(&timeinfo);
        char datetime[128] = {0};
        snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        param  = datetime;
    } else {
        param = "2000/01/01 00:00:00";
    }
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
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.getDateTime(&timeinfo);
    } else {
        timeinfo = {0};
    }
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

    bool debug = param.enable_debug;

    if (millis() < interval && debug == false) {
        return false;
    }
    interval = millis() + 1000;

    memset(&param, 0, sizeof(gps_params_t));


    param.model = instance.gps.getModel().c_str();
    param.rx_size = instance.gps.loop(debug);

    if (debug) {
        return false;
    }

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
            if (hw_get_device_online() & HW_RTC_ONLINE) {
                instance.rtc.convertUtcToTimezone(utc_tm, GMT_OFFSET_SECOND);
                instance.rtc.setDateTime(utc_tm);
                instance.rtc.hwClockRead();
            }
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
    uint32_t hw_online =   HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE;
#ifdef USING_INPUT_DEV_KEYBOARD
    hw_online |= HW_KEYBOARD_ONLINE;
#endif
    return hw_online;
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

void hw_set_led_backlight(uint8_t level)
{
#if defined(ARDUINO) && defined(USING_LED_INDICATOR)
    instance.setLedIndicatorBrightness(level);
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

#ifdef ARDUINO
static void listDir(vector < AudioParams_t > &list, fs::FS &fs, const char * dirname, uint8_t levels, audio_source_type_t source_type)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                std::string next_dir = dirname;
                if (next_dir != "/") next_dir += "/";
                next_dir += file.name();
                listDir(list, fs, next_dir.c_str(), levels - 1, source_type);
            }
        } else {
            String filename = file.name();
            if (filename.endsWith(".mp3") || filename.endsWith(".wav")) {
                list.push_back({source_type, filename.c_str()});
            }

            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}
#endif

void hw_fat_list(vector < AudioParams_t > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO)
    Serial.printf("FFAT Listing directory: %s\n", dirname);
    listDir(list, FFat, dirname, levels, AUDIO_SOURCE_FATFS);
#endif
}

bool hw_sd_list(vector < AudioParams_t > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.lockSPI();
    if (instance.installSD()) {
        Serial.println("SD Card mount success.");
    } else {
        Serial.println("SD Card mount failed.");
        instance.unlockSPI();
        return false;
    }
    listDir(list, SD, dirname, levels, AUDIO_SOURCE_SDCARD);
    instance.unlockSPI();
#endif
    return true;
}

void hw_mount_sd()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.installSD();
#endif
}

void hw_get_filesystem_music(vector < AudioParams_t > &list)
{
    list.clear();

#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
    Serial.println("\n================== SD Music List ==================");
    hw_sd_list(list, "/", 0);
#endif

    Serial.println("\n================== FFat Music List ==================");
    hw_fat_list(list, "/", 0);

#else
    list.push_back({AUDIO_SOURCE_FATFS, "/abc.mp3"});
    list.push_back({AUDIO_SOURCE_FATFS, "/ccc.mp3"});
    list.push_back({AUDIO_SOURCE_FATFS, "/ddd.mp3"});
#endif
}

void hw_set_sd_music_play(audio_source_type_t source_type, const char *filename)
{
    audio_params_t params = {
        .event = APP_EVENT_PLAY,
        .filename = filename,
        .source_type = source_type
    };
    printf("hw_set_sd_music_play : %s source_type:%d\n", filename, source_type);
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    if (hw_player_running()) {
        xEventGroupSetBits(playerEvent, PLAYER_END);
        Serial.println("Wait hw_player_running stop...");
        while (hw_player_running()) {
            delay(2);
        }
        Serial.println("hw_player_running stopped.");
    }
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
    xQueueSend(playerQueue, &params, portMAX_DELAY);
    Serial.println("hw_set_sd_music_play send done\n");
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

void hw_set_volume(uint8_t volume)
{
#if defined(ARDUINO) && defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.setVolume(volume);
    } else {
        printf("Audio codec not online!\n");
    }
#endif //USING_AUDIO_CODEC
}

uint8_t hw_get_volume()
{
#if defined(ARDUINO) && defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        return instance.codec.getVolume();
    } else {
        return 0;
    }
#else
    return 100;
#endif //USING_AUDIO_CODEC
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
    instance.ppm.getFaultStatus();
    if (instance.ppm.isNTCFault()) {
        params.ntc_state = instance.ppm.getNTCStatusString();
    } else {
        params.ntc_state = "Normal";
    }
#elif defined(USING_PMU_MANAGE)
    params.type = MONITOR_PMU;
    params.charge_state = instance.pmu.isCharging() ? "Charging" : "Not charging";
    params.usb_voltage = instance.pmu.getVbusVoltage();
    params.sys_voltage = instance.pmu.getSystemVoltage();
    params.battery_voltage = instance.pmu.getBattVoltage();
    params.battery_percent = instance.pmu.getBatteryPercent();
    params.temperature = instance.pmu.getTemperature();
    params.ntc_state = "Normal"; //TODO:
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
    params.ntc_state = "Normal";
#endif
}

static imu_params_t imu_params = {0, 0, 0, 0};

void hw_get_imu_params(imu_params_t &params)
{
#ifdef ARDUINO
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        params =  imu_params;
    }
#elif defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA423_ONLINE) {
        params.orientation = instance.sensor.direction();
    }
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
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        float sample_rate = 100.0;      /* Read out data measured at 100Hz */
        uint32_t report_latency_ms = 0; /* Report immediately */
        // LilyGoLib has already processed it
        // instance.sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
        // Enable Quaternion function
        instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, sample_rate, report_latency_ms);
        // Register event callback function
        instance.sensor.onResultEvent(SensorBHI260AP::GAME_ROTATION_VECTOR, imu_data_process);
    }
#elif defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA423_ONLINE) {
        instance.sensor.configAccelerometer();
        instance.sensor.enableAccelerometer();
    }
#endif // SENSOR
#endif // ARDUINO
}

void hw_unregister_imu_process()
{
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, 0, 0);
    }
#elif defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA423_ONLINE) {
        instance.sensor.disableAccelerometer();
    }
#endif // SENSOR
#endif // ARDUINO
}

//* ble //

void hw_enable_ble(const char *devName)
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
#endif
}

void hw_deinit_ble()
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)

#endif
}

void hw_disable_ble()
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)

#endif
}

size_t hw_get_ble_message(char *buffer, size_t buffer_size)
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
#endif
    return 0;
}

const char  *hw_get_ble_kb_name()
{
    return "Keyboard";
}

void hw_set_ble_kb_enable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    bleKeyboard.setName("Keyboard");
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

void hw_low_power_loop()
{
#ifdef ARDUINO
    instance.lightSleep();
    // #ifdef USING_ST25R3916
    //     beginNFC(nrf_notify_callback, ndef_event_callback);
    // #endif
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

void hw_enable_keyboard()
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    instance.enableKeyboard();
#endif
}

void hw_disable_keyboard()
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    instance.disableKeyboard();
#endif
}


void hw_flush_keyboard()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_get_device_online() & HW_KEYBOARD_ONLINE) {
        instance.kb.flush();
    }
#endif
}

bool hw_has_keyboard()
{
    return hw_get_device_online() & HW_KEYBOARD_ONLINE;
}

bool hw_has_indicator_led()
{
    return hw_get_device_online() & HW_LED_INDIC_ONLINE;
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


#if defined(ARDUINO) && defined(USING_IR_REMOTE)
#include <IRsend.h>
IRsend irsend(IR_SEND); // T-Watch S3 GPIO2 pin to use.
#endif


#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
#include <IRremoteESP8266.h>
#include <IRrecv.h>
IRrecv irrecv(IR_SEND); // T-Watch S3 GPIO15 pin to use.
#endif

void hw_set_remote_code(uint32_t nec_code)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
    static bool isBegin = false;
    if (!isBegin) {
        isBegin = true;
        irsend.begin();
    }
    irsend.sendNEC(nec_code);
#endif
}

void hw_get_remote_code(uint64_t &result)
{
#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
    decode_results results;
    if (irrecv.decode(&results)) {
        Serial.print("IR Code received: ");
        Serial.println(results.value, HEX);
        result = results.value;
        irrecv.resume();  // Receive the next value
    }
#else
    result = random(0, INT_MAX);
#endif
}

void hw_ir_function_select(bool enableSend)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE) && defined(USING_IR_RECEIVER)
    if (enableSend) {
        instance.IRFunctionSelect(IR_FUNC_SENDER);
        irrecv.disableIRIn();
    } else {
        instance.IRFunctionSelect(IR_FUNC_RECEIVER);
        irrecv.enableIRIn();
    }
#endif
}

#ifdef USING_MAG_QMC5883
void hw_mag_enable(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        /* Config Magnetometer */
        instance.mag.configMagnetometer(SensorQSTMagnetic::MODE_CONTINUOUS,
                                        SensorQSTMagnetic::RANGE_8G,
                                        SensorQSTMagnetic::DATARATE_100HZ,
                                        SensorQSTMagnetic::OSR_1,
                                        SensorQSTMagnetic::DSR_1);
    } else {
        instance.mag.setMode(SensorQSTMagnetic::MODE_SUSPEND);
    }
#endif // ARDUINO
}

float hw_mag_get_polar()
{
#ifdef ARDUINO
    Polar polar;
    if (instance.mag.readPolar(polar)) {
        return polar.polar;
    }
    return 0.0f;
#else
    static float sim_angle = 0;
    sim_angle = fmod(sim_angle + 0.5, 360);
    return sim_angle;
#endif
}

#endif // USING_MAG_QMC5883

#ifdef USING_BME280

void hw_bme_enable(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        instance.bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                                 Adafruit_BME280::SAMPLING_X1,   // temperature
                                 Adafruit_BME280::SAMPLING_X1, // pressure
                                 Adafruit_BME280::SAMPLING_X1,   // humidity
                                 Adafruit_BME280::FILTER_X2 );
    } else {
        instance.bme.setSampling(Adafruit_BME280::MODE_SLEEP);
    }
#endif
}


void hw_bme_get_data(float &temp, float &humi, float &press, float &alt)
{
#ifdef ARDUINO
    temp = instance.bme.readTemperature();
    humi = instance.bme.readHumidity();
    press = instance.bme.readPressure() / 100.0F;
    alt = instance.bme.readAltitude(1013.25);

#else
    temp = random(0, 25);
    humi = random(40, 95);
    press = random(1000, 1200);
    alt = random(20, 60);
#endif
}

#endif /*USING_BME280*/


using TrackballEventCallback = void(*)(uint8_t dir);
using ButtonEventCallback = void(*)(uint8_t idx, uint8_t state);

#if defined(ARDUINO) && defined(USING_TRACKBALL)

static TrackballEventCallback _trackball_cb = NULL;
static ButtonEventCallback    _button_cb = NULL;


static void trackballEventCallback(DeviceEvent_t event, void *params, void *user_data)
{
    if (_trackball_cb && params) {
        TrackballDir_t dir = *(static_cast < TrackballDir_t * > (params));
        _trackball_cb(dir);
    }
}

static void buttonEventCallback(DeviceEvent_t event, void *params, void *user_data)
{
    if (_button_cb && params) {
        ButtonEventParam_t *p = static_cast < ButtonEventParam_t * > (params);
        _button_cb(p->id, p->event);
    }
}

#endif

void hw_set_trackball_callback(TrackballEventCallback callback)
{
#if defined(ARDUINO) && defined(USING_TRACKBALL)
    // instance.setTrackballCallback(callback);
    if (callback) {
        instance.onEvent(trackballEventCallback, TRACKBALL_EVENT, NULL);
        _trackball_cb = callback;
    } else {
        instance.removeEvent(trackballEventCallback, TRACKBALL_EVENT);
        _trackball_cb = NULL;
    }
#endif
}

void hw_set_button_callback(ButtonEventCallback callback)
{
#if defined(ARDUINO) && defined(USING_TRACKBALL)
    if (callback) {
        instance.onEvent(buttonEventCallback, BUTTON_EVENT, NULL);
        _button_cb = callback;
    } else {
        instance.removeEvent(buttonEventCallback, BUTTON_EVENT);
        _button_cb = NULL;
    }
#endif
}

const char *hw_get_device_power_tips_string()
{
#if defined(USING_PPM_MANAGE)
    return "Select a shutdown method:\n"
           "1. Sleep: Set to sleep mode and press the Boot button to wake up.\n"
           "2. Shutdown: Turn off the device (requires removing the USB-C port to shut down).\n"
           "After shutting down, press and hold the Power button or plug in a USB-C port to activate the device.";
#else
    return "Select a shutdown method:\n"
           "1. Sleep: Set to sleep mode and press the Boot button to wake up.\n"
           "2. Shutdown: Turn off the device. After shutting down, press and hold the Power button or plug in a\n"
           "USB-C cable to activate the device.";
#endif
}

const char *hw_get_firmware_hash_string()
{
#ifdef ARDUINO
    static char hash_string[33] = {0};
    snprintf(hash_string, sizeof(hash_string), "%s", ESP.getSketchMD5().c_str());
    return hash_string;
#else
    return "DummyHashString";
#endif
}

const char *hw_get_chip_id_string()
{
#ifdef ARDUINO
    static char chipid[13] = {0};
    uint64_t chipmacid = 0LL;
    esp_efuse_mac_get_default((uint8_t *)(&chipmacid));
    snprintf(chipid, sizeof(chipid), "%04X%08X", (uint16_t)(chipmacid >> 32), (uint32_t)(chipmacid));
    return chipid;
#endif
    return "DummyChipIDString";
}


void hw_set_usb_rf_switch(bool to_usb)
{
#ifdef ARDUINO
#if defined(HAS_USB_RF_SWITCH)
    instance.setRFSwitch(to_usb);
#endif
#endif
}


void hw_set_audio_effect_3d(bool enable)
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    instance.setAudioEffect3D(enable);
#endif
}

void hw_set_audio_effect_ab_class(bool enable)
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    if (enable) {
        instance.setAudioMode(AUDIO_CLASS_AB);
    } else {
        instance.setAudioMode(AUDIO_CLASS_D);
    }
#endif
}

