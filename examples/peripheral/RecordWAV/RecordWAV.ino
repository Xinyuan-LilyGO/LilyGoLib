/**
 * @file      RecordWAV.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-19
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

// Create variables to store the audio data
uint8_t *wav_buffer;
size_t wav_size;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label1 = lv_label_create(lv_screen_active());
    lv_label_set_text(label1, "RecordWAV");
    lv_obj_center(label1);

    Serial.println("Start Record");

    // Record 5 seconds of audio data

#ifdef ARDUINO_T_LORA_PAGER
    // T-LoRa-Pager uses Codec
    instance.codec.setGain(50.0);
    wav_buffer = instance.codec.recordWAV(5, &wav_size);
#else
    // T-Watch-S3 / T-Watch-S3-Ultar Use PDM Microphone
    wav_buffer = instance.mic.recordWAV(5, &wav_size);
#endif

    Serial.println("Record finish...");

    delay(1000);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Turn on the audio power, the default is off
    instance.powerControl(POWER_SPEAK, true);

#ifdef ARDUINO_T_LORA_PAGER
    instance.codec.setVolume(20);
#endif
}

void loop()
{
    lv_task_handler();

#ifdef ARDUINO_T_LORA_PAGER
    // T-LoRa-Pager uses Codec
    instance.codec.playWAV((uint8_t*)wav_buffer, wav_size);
#else
    // T-Watch-S3 / T-Watch-S3-Ultar Use Player
    instance.player.playWAV(wav_buffer, wav_size);
#endif

    delay(3000);
}
