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

AudioInputIf *inputDev = instance.getAudioInput();
AudioOutputIf *outputDev = instance.getAudioOutput();

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label1 = lv_label_create(lv_screen_active());
    lv_label_set_text(label1, "RecordWAV");
    lv_obj_center(label1);
    lv_task_handler();

    Serial.println("Start Record");

    int after_seconds = 5;

    uint32_t start_time = millis();
    while (after_seconds--) {
        // if (millis() - start_time >= (5 - after_seconds) * 1000) {
        lv_label_set_text_fmt(label1, "After %d seconds will start recording...\n", 5 - after_seconds);
        Serial.printf("After %d seconds will start recording...\n", 5 - after_seconds);
        // }
        lv_task_handler();
        delay(1000);
    }
    // Record 5 seconds of audio data
    // inputDev->recordWAV(5, &wav_buffer, &wav_size, 16000, 2);
    instance.codec.recordWAV(5, &wav_buffer, &wav_size, 16000, 2);


    while (wav_size == 0) {
        lv_label_set_text_fmt(label1, "Recording failed...");
        lv_task_handler();
        delay(1000);
    }


    Serial.printf("WAV buffer size: %u bytes\n", wav_size);

    Serial.println("Record finish...");

    delay(1000);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // outputDev->setVolume(100);
    instance.codec.setVolume(100);

    lv_label_set_text_fmt(label1, "Wav size: %u bytes\nPlaying...", wav_size);
}

void loop()
{
    lv_task_handler();

    // outputDev->playWAV((uint8_t *)wav_buffer, wav_size);
    instance.codec.playWAV((uint8_t *)wav_buffer, wav_size);
    delay(3000);
}
