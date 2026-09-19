/**
 * @file      playWAV.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-21
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include "wav_hex.h"

AudioOutputIf *outputDev = instance.getAudioOutput();

static void event_handler(lv_event_t *e)
{
    Serial.println("Play WAV...");
    outputDev->playWAV((uint8_t*)wav_hex, wav_hex_len);
}

void setup()
{
    // Initialize the serial port
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label1 = lv_label_create(lv_screen_active());
    lv_label_set_text(label1, "Play WAV");
    lv_obj_center(label1);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    lv_obj_t *btn1 = lv_button_create(lv_screen_active());
    lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn1, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *label = lv_label_create(btn1);
    lv_label_set_text(label, "Play WAV");
    lv_obj_center(label);

    // Create intput device group , only T-LoRa-Pager need.
    lv_group_t *group = lv_group_create();
    lv_set_default_group(group);
    lv_group_add_obj(lv_group_get_default(), btn1);
}

void loop()
{
    lv_task_handler();
    delay(5);
}
