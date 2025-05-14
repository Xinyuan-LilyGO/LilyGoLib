/**
 * @file      PlayMP3FromPROGMEM.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourceFunction.h>
#include <AudioGeneratorFLAC.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorAAC.h>
// Audio file header
#include "mp3_buffer.h"
#include "wav_buffer.h"
#include "flac_buffer.h"
#include "aac_buffer.h"


#if defined(ARDUINO_T_LORA_PAGER)
class EspAudioOutput : public AudioOutput
{
public:
    EspAudioOutput(EspCodec & codec)
    {
        _codec = &codec;
    }
    bool begin()
    {
        Serial.printf("bps:%d channels:%u hertz:%u\n", bps, channels, hertz);
        if (hertz != 16000 || hertz != 32000 || hertz != 24000 || hertz != 41000 || hertz == 0) {
            hertz = 16000;
        }
        channels = 2;
        alreadyBeing = _codec->open(bps, channels, hertz ) != -1;
        return alreadyBeing;
    };

    bool SetBitsPerSample(int bits)
    {
        Serial.printf("Set bps : %u\n", bps);
        bps = bits;
        return true;
    }
    bool SetChannels(int chan)
    {
        Serial.printf("Set channels : %u\n", channels);
        channels = chan;
        return true;
    }
    bool SetRate(int hz)
    {
        hertz = hz;
        Serial.printf("Set Rate : %u\n", hertz);
        return true;
    }

    bool ConsumeSample(int16_t sample[2])
    {
        return _codec->write((uint8_t*)sample, 4) != -1;
    }

    uint16_t ConsumeSamples(int16_t *samples, uint16_t count)
    {
        _codec->write((uint8_t*)samples, count);
        return count;
    }
    bool stop()
    {
        Serial.println("Closed\n\n\n");
        alreadyBeing = false;
        _codec->close();
        return true;
    }
private:
    bool alreadyBeing = false;
    EspCodec *_codec;
};

EspAudioOutput          *out = NULL;

#else /*ARDUINO_T_LORA_PAGER*/

#include <AudioOutputI2S.h>

// It is recommended to use I2S channel 1.
// If the PDM microphone is turned on,
// the decoder must use channel 1 because PDM can only use channel 0
uint8_t i2sPort = 1;

AudioOutputI2S          *out = NULL;

#endif /*ARDUINO_T_LORA_PAGER*/

AudioGeneratorWAV       *wav = NULL;
AudioFileSourcePROGMEM  *file = NULL;
AudioGeneratorMP3       *mp3 = NULL;
AudioFileSourceID3      *id3 = NULL;
AudioGeneratorFLAC      *flac = NULL;
AudioGeneratorAAC       *aac = NULL;

//lvgl event handler btn index
uint8_t btn_index[4] = {0, 1, 2, 3};

// Play task handler
TaskHandle_t playMP3Handler;
TaskHandle_t playWAVHandler;
TaskHandle_t playFLACHandler;
TaskHandle_t playACCHandler;

void event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uint8_t btn_index = *(uint8_t *)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED) {
        switch (btn_index) {
        case 0:
            vTaskSuspend(playWAVHandler);
            vTaskSuspend(playMP3Handler);
            vTaskSuspend(playFLACHandler);
            vTaskSuspend(playACCHandler);
            Serial.println("Playing WAV");
            file->open(wav_buffer, wav_buffer_len);
            wav->begin(file, out);
            vTaskResume(playWAVHandler);
            break;
        case 1:
            vTaskSuspend(playWAVHandler);
            vTaskSuspend(playMP3Handler);
            vTaskSuspend(playFLACHandler);
            vTaskSuspend(playACCHandler);
            Serial.println("Playing MP3");
            file->open(mp3_buffer, mp3_buffer_len);
            mp3->begin(id3, out);
            vTaskResume(playMP3Handler);
            break;
        case 2:
            vTaskSuspend(playWAVHandler);
            vTaskSuspend(playMP3Handler);
            vTaskSuspend(playFLACHandler);
            vTaskSuspend(playACCHandler);
            Serial.println("Playing FLAC");
            file->open(flac_buffer, flac_buffer_len);
            flac->begin(file, out);
            vTaskResume(playFLACHandler);
            break;
        case 3:
            vTaskSuspend(playWAVHandler);
            vTaskSuspend(playMP3Handler);
            vTaskSuspend(playFLACHandler);
            vTaskSuspend(playACCHandler);
            Serial.println("Playing AAC");
            file->open(aac_buffer, aac_buffer_len);
            aac->begin(file, out);
            vTaskResume(playACCHandler);
            break;
        default:
            break;
        }
    }
}

void playWavTask(void *parms)
{
    vTaskSuspend(NULL);
    while (1) {
        if (wav->isRunning()) {
            if (!wav->loop()) {
                wav->stop();
            }
        } else {
            vTaskSuspend(NULL);
        }
        delay(2);
    }
    vTaskDelete(NULL);
}

void playMP3Task(void *parms)
{
    vTaskSuspend(NULL);
    while (1) {
        if (mp3->isRunning()) {
            if (!mp3->loop()) {
                mp3->stop();
            }
        } else {
            vTaskSuspend(NULL);
        }
        delay(2);
    }
    vTaskDelete(NULL);
}

void playFLACTask(void *parms)
{
    vTaskSuspend(NULL);
    while (1) {
        if (flac->isRunning()) {
            if (!flac->loop()) {
                flac->stop();
            }
        } else {
            vTaskSuspend(NULL);
        }
        delay(2);
    }
    vTaskDelete(NULL);
}

void playACCTask(void *parms)
{
    vTaskSuspend(NULL);
    while (1) {
        if (aac->isRunning()) {
            if (!aac->loop()) {
                aac->stop();
            }
        } else {
            vTaskSuspend(NULL);
        }
        delay(2);
    }
    vTaskDelete(NULL);
}

void setup(void)
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_center(cont);

    // Create intput device group , only T-LoRa-Pager need.
    lv_group_t *group = lv_group_create();
    lv_set_default_group(group);
    lv_group_add_obj(lv_group_get_default(), cont);

    lv_obj_t *label;

    label = lv_label_create(cont);
    lv_label_set_text(label, "PlayMusicFromPROGMEM");

    //Play wav Button
    lv_obj_t *btn1 = lv_btn_create(cont);
    lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, &btn_index[0]);
    lv_obj_set_width(btn1,  LV_PCT(100));

    label = lv_label_create(btn1);
    lv_label_set_text(label, "Play WAV");
    lv_obj_center(label);

    //Play mp3 Button
    btn1 = lv_btn_create(cont);
    lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, &btn_index[1]);
    lv_obj_set_width(btn1,  LV_PCT(100));

    label = lv_label_create(btn1);
    lv_label_set_text(label, "Play MP3");
    lv_obj_center(label);

    //Play flac Button
    btn1 = lv_btn_create(cont);
    lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, &btn_index[2]);
    lv_obj_set_width(btn1,  LV_PCT(100));

    label = lv_label_create(btn1);
    lv_label_set_text(label, "Play FLAC");
    lv_obj_center(label);

    //Play aac Button
    btn1 = lv_btn_create(cont);
    lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, &btn_index[3]);
    lv_obj_set_width(btn1,  LV_PCT(100));

    label = lv_label_create(btn1);
    lv_label_set_text(label, "Play AAC");
    lv_obj_center(label);

    file = new AudioFileSourcePROGMEM();

#if defined(ARDUINO_T_LORA_PAGER)

    // Set up the use of an external decoder
    out = new EspAudioOutput(instance.codec);

    // Set volume
    instance.codec.setVolume(50);

#else

    // Set up the use of an external decoder
    out = new AudioOutputI2S(i2sPort, AudioOutputI2S::EXTERNAL_I2S);

    // Set up hardware connection
    out->SetPinout(I2S_BCLK, I2S_WCLK, I2S_DOUT);

    //Adjust to appropriate gain
    out->SetGain(3.8);

#endif /*ARDUINO_T_LORA_PAGER*/

    wav = new AudioGeneratorWAV();

    id3 = new AudioFileSourceID3(file);

    mp3 = new AudioGeneratorMP3();

    flac = new AudioGeneratorFLAC();

    aac = new AudioGeneratorAAC();

    xTaskCreate(playWavTask, "wav", 8192, NULL, 10, &playWAVHandler);
    xTaskCreate(playMP3Task, "mp3", 8192, NULL, 10, &playMP3Handler);
    xTaskCreate(playFLACTask, "flac", 8192, NULL, 10, &playFLACHandler);
    xTaskCreate(playACCTask, "aac", 8192, NULL, 10, &playACCHandler);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Turn on the audio power, the default is off
    instance.powerControl(POWER_SPEAK, true);
}

void loop()
{
    lv_task_handler();
    delay(5);
}




