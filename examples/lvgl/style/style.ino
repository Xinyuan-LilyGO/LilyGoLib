/**
 * @file      style.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include "lv_example_style.h"

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    //Tips : Select a separate function to see the effect
    lv_example_style_1();
    // lv_example_style_2();
    // lv_example_style_3();
    // lv_example_style_4();
    // lv_example_style_5();
    // lv_example_style_6();
    // lv_example_style_7();
    // lv_example_style_8();
    // lv_example_style_9();
    // lv_example_style_10();
    // lv_example_style_11();
    // lv_example_style_12();
    // lv_example_style_13();
    // lv_example_style_14();
    // lv_example_style_15();
    // lv_example_style_16();
    // lv_example_style_17();
    // lv_example_style_18();

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}


void loop()
{
    lv_task_handler();
    delay(5);
}
