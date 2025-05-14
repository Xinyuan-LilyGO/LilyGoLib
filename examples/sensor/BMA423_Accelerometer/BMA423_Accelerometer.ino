/**
 * @file      BMA423_Accelerometer.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef ARDUINO_T_WATCH_S3

uint32_t interval;
lv_obj_t *label1;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    //Default 4G ,200HZ , Avg4, Continuous mode
    instance.sensor.configAccelerometer();

    instance.sensor.enableAccelerometer();

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);


    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}


void loop()
{
    instance.loop();

    int16_t x, y, z;
    if (interval < millis()) {
        interval = millis() + 50;
        instance.sensor.getAccelerometer(x, y, z);
        Serial.print("X:");
        Serial.print(x); Serial.print(" ");
        Serial.print("Y:");
        Serial.print(y); Serial.print(" ");
        Serial.print("Z:");
        Serial.print(z);
        Serial.println();
        lv_label_set_text_fmt(label1, "X:%d \nY:%d \nZ:%d", x, y, z);
    }

    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support  T-Watch-S3"); delay(1000);
}

#endif

